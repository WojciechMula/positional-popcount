#ifdef __linux__

/* ****************************
*  Definitions
******************************/
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <libgen.h>
#include <random>
#include <string>
#include <vector>

#include "pospopcnt.h"
#include "linux-perf-events.h"
#include "popcnt.h"

void print16(uint32_t *flags) {
    for (int k = 0; k < 16; k++)
        printf(" %8u ", flags[k]);
    printf("\n");
}

std::vector<unsigned long long>
compute_mins(std::vector< std::vector<unsigned long long> > allresults) {
    if (allresults.size() == 0)
        return std::vector<unsigned long long>();
    
    std::vector<unsigned long long> answer = allresults[0];
    
    for (size_t k = 1; k < allresults.size(); k++) {
        assert(allresults[k].size() == answer.size());
        for (size_t z = 0; z < answer.size(); z++) {
            if (allresults[k][z] < answer[z])
                answer[z] = allresults[k][z];
        }
    }
    return answer;
}

std::vector<double>
compute_averages(std::vector< std::vector<unsigned long long> > allresults) {
    if (allresults.size() == 0)
        return std::vector<double>();
    
    std::vector<double> answer(allresults[0].size());
    
    for (size_t k = 0; k < allresults.size(); k++) {
        assert(allresults[k].size() == answer.size());
        for (size_t z = 0; z < answer.size(); z++) {
            answer[z] += allresults[k][z];
        }
    }

    for (size_t z = 0; z < answer.size(); z++) {
        answer[z] /= allresults.size();
    }
    return answer;
}


/**
 * @brief 
 * 
 * @param n          Number of integers.
 * @param iterations Number of iterations.
 * @param fn         Target function pointer.
 * @param verbose    Flag enabling verbose output.
 * @return           Returns true if the results are correct. Returns false if the results
 *                   are either incorrect or the target function is not supported.
 */
bool benchmark(uint16_t n, uint32_t iterations, pospopcnt_u16_method_type fn, bool verbose, bool test) {
    std::vector<int> evts;
    std::vector<uint16_t> vdata(n);
    evts.push_back(PERF_COUNT_HW_CPU_CYCLES);
    evts.push_back(PERF_COUNT_HW_INSTRUCTIONS);
    evts.push_back(PERF_COUNT_HW_BRANCH_MISSES);
    evts.push_back(PERF_COUNT_HW_CACHE_REFERENCES);
    evts.push_back(PERF_COUNT_HW_CACHE_MISSES);
    LinuxEvents<PERF_TYPE_HARDWARE> unified(evts);
    std::vector<unsigned long long> results; // tmp buffer
    std::vector< std::vector<unsigned long long> > allresults;
    results.resize(evts.size());
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 0xFFFF);

    bool isok = true;
    for (uint32_t i = 0; i < iterations; i++) {
        for (size_t k = 0; k < vdata.size(); k++) {
            vdata[k] = dis(gen); // random init.
        }
        uint32_t correctflags[16] = {0};
        pospopcnt_u16_scalar_naive(vdata.data(), vdata.size(), correctflags); // this is our gold standard
        uint32_t flags[16] = {0};
        
        unified.start();
        fn(vdata.data(), vdata.size(), flags);
        unified.end(results);

        uint64_t tot_obs = 0;
        for (size_t k = 0; k < 16; ++k) tot_obs += flags[k];
        if (tot_obs == 0) { // when a method is not supported it returns all zero
            return false;
        }

        for (size_t k = 0; k < 16; k++) {
            if (correctflags[k] != flags[k]) {
                if (test) {
                    printf("bug:\n");
                    printf("expected : ");
                    print16(correctflags);
                    printf("got      : ");
                    print16(flags);
                    return false;
                } else {
                    isok = false;
                }
            }
        }
        allresults.push_back(results);
    }

    std::vector<unsigned long long> mins = compute_mins(allresults);
    std::vector<double> avg = compute_averages(allresults);
    
    if (verbose) {
        printf("instructions per cycle %4.2f, cycles per 16-bit word:  %4.3f, "
               "instructions per 16-bit word %4.3f \n",
                double(mins[1]) / mins[0], double(mins[0]) / n, double(mins[1]) / n);
        // first we display mins
        printf("min: %8llu cycles, %8llu instructions, \t%8llu branch mis., %8llu "
               "cache ref., %8llu cache mis.\n",
                mins[0], mins[1], mins[2], mins[3], mins[4]);
        printf("avg: %8.1f cycles, %8.1f instructions, \t%8.1f branch mis., %8.1f "
               "cache ref., %8.1f cache mis.\n",
                avg[0], avg[1], avg[2], avg[3], avg[4]);
    } else {
        printf("cycles per 16-bit word:  %4.3f \n", double(mins[0]) / n);
        // printf("%4.3f \n", double(mins[0]) / n);
    }

    return isok;
}

void measurepopcnt(uint16_t n, uint32_t iterations, bool verbose) {
    std::vector<int> evts;
    std::vector<uint16_t> vdata(n);
    evts.push_back(PERF_COUNT_HW_CPU_CYCLES);
    evts.push_back(PERF_COUNT_HW_INSTRUCTIONS);
    evts.push_back(PERF_COUNT_HW_BRANCH_MISSES);
    evts.push_back(PERF_COUNT_HW_CACHE_REFERENCES);
    evts.push_back(PERF_COUNT_HW_CACHE_MISSES);
    LinuxEvents<PERF_TYPE_HARDWARE> unified(evts);
    std::vector<unsigned long long> results; // tmp buffer
    std::vector< std::vector<unsigned long long> > allresults;
    results.resize(evts.size());
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 0xFFFF);

    n = vdata.size() / (512 / 16) * (512/16);
    for (uint32_t i = 0; i < iterations; i++) {
        for (size_t k = 0; k < vdata.size(); k++) {
            vdata[k] = dis(gen); // random init.
        }
        uint64_t expected = popcnt_harley_seal((const __m512i*) vdata.data(), vdata.size() / (512 / 16));       
        unified.start();
        uint64_t measured = popcnt_harley_seal((const __m512i*) vdata.data(), vdata.size() / (512 / 16));
        unified.end(results);
        assert(measured == expected);
        allresults.push_back(results);
    }

    std::vector<unsigned long long> mins = computemins(allresults);
    std::vector<double> avg = computeavgs(allresults);
    printf("%-40s\t","avx512popcnt");    
    if (verbose) {
        printf("instructions per cycle %4.2f, cycles per 16-bit word:  %4.3f, "
                "instructions per 16-bit word %4.3f \n",
                double(mins[1]) / mins[0], double(mins[0]) / n, double(mins[1]) / n);
        // first we display mins
        printf("min: %8llu cycles, %8llu instructions, \t%8llu branch mis., %8llu "
                "cache ref., %8llu cache mis.\n",
                mins[0], mins[1], mins[2], mins[3], mins[4]);
        printf("avg: %8.1f cycles, %8.1f instructions, \t%8.1f branch mis., %8.1f "
                "cache ref., %8.1f cache mis.\n",
                avg[0], avg[1], avg[2], avg[3], avg[4]);
    } else {
        printf("cycles per 16-bit word:  %4.3f \n", double(mins[0]) / n);
    }

     
}

static void print_usage(char *command) {
    printf(" Try %s -n 100000 -i 15 -v \n", command);
    printf("-n is the number of 16-bit words \n");
    printf("-i is the number of tests or iterations \n");
    printf("-v makes things verbose\n");
}

int main(int argc, char **argv) {
    size_t n = 10000000;
    size_t iterations = 100;
    bool verbose = false;
    int c;

    while ((c = getopt(argc, argv, "vhn:i:")) != -1) {
        switch (c) {
        case 'n':
            n = atoi(optarg);
            break;
        case 'v':
            verbose = true;
            break;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        case 'i':
            iterations = atoi(optarg);
            break;
        default:
            abort();
        }
    }
    printf("n = %zu \n", n);
    
    for (size_t k = 0; k < PPOPCNT_NUMBER_METHODS; k++) {
        printf("%-40s\t", pospopcnt_u16_method_names[k].c_str());
        fflush(NULL);
        // std::cout << pospopcnt_u16_method_names[k] << "\t";
        bool isok = benchmark(n, iterations, PPOPCNT_U16_METHODS[k], verbose, true);
        if (isok == false) {
            printf("Problem detected with %s.\n", pospopcnt_u16_method_names[k].c_str());
            // printf("0\n");
        }
        if (verbose)
            printf("\n");
    }
    measurepopcnt(n, iterations, verbose);
    if (!verbose)
        printf("Try -v to get more details.\n");

    return EXIT_SUCCESS;
}
#else //  __linux__

#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("This is a linux-specific benchmark\n");
    return EXIT_SUCCESS;
}

#endif
