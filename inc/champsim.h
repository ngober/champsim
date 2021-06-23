#ifndef CHAMPSIM_H
#define CHAMPSIM_H

#include <cstdint>
#include <tuple>

#include "champsim_constants.h"

// USEFUL MACROS
//#define DEBUG_PRINT
#define SANITY_CHECK
#define LLC_BYPASS
#define DRC_BYPASS
#define NO_CRC2_COMPILE

#ifdef DEBUG_PRINT
#define DP(x) x
#else
#define DP(x)
#endif

// CACHE
#define INFLIGHT 1
#define COMPLETED 2

#define FILL_L1    1
#define FILL_L2    2
#define FILL_LLC   4
#define FILL_DRC   8
#define FILL_DRAM 16

using namespace std;
std::tuple<uint64_t, uint64_t, uint64_t> elapsed_time();
void print_deadlock(uint32_t i);
#endif

