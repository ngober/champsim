#ifndef CHAMPSIM_H
#define CHAMPSIM_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <assert.h>
#include <signal.h>
#include <sys/types.h>

#include <iostream>
#include <queue>
#include <map>
#include <random>
#include <string>
#include <iomanip>
#include <type_traits>

#include "vmem.h"

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

// CPU
#define NUM_CPUS 1U;
#define CPU_FREQ 4000U;
#define DRAM_IO_FREQ 3200U;
#define PAGE_SIZE 4096U;
#define LOG2_PAGE_SIZE lg2(PAGE_SIZE);

// CACHE
#define BLOCK_SIZE 64
#define LOG2_BLOCK_SIZE 6
#define MAX_READ_PER_CYCLE 8
#define MAX_WRITE_PER_CYCLE 8

#define INFLIGHT 1
#define COMPLETED 2

#define FILL_L1    1
#define FILL_L2    2
#define FILL_LLC   4
#define FILL_DRC   8
#define FILL_DRAM 16

// DRAM
#define DRAM_CHANNELS 1U;      // default: assuming one DIMM per one channel 4GB * 1 => 4GB off-chip memory
#define LOG2_DRAM_CHANNELS lg2(DRAM_CHANNELS);
#define DRAM_RANKS 1U;         // 512MB * 8 ranks => 4GB per DIMM
#define LOG2_DRAM_RANKS lg2(DRAM_RANKS);
#define DRAM_BANKS 8U;         // 64MB * 8 banks => 512MB per rank
#define LOG2_DRAM_BANKS lg2(DRAM_BANKS);
#define DRAM_ROWS 65536U;      // 2KB * 32K rows => 64MB per bank
#define LOG2_DRAM_ROWS lg2(DRAM_ROWS);
#define DRAM_COLUMNS 128U;      // 64B * 32 column chunks (Assuming 1B DRAM cell * 8 chips * 8 transactions = 64B size of column chunks) => 2KB per row
#define LOG2_DRAM_COLUMNS lg2(DRAM_COLUMNS);
#define DRAM_ROW_SIZE (BLOCK_SIZE*DRAM_COLUMNS << 10);

#define DRAM_SIZE (DRAM_CHANNELS*DRAM_RANKS*DRAM_BANKS*DRAM_ROWS*DRAM_ROW_SIZE << 10);
#define DRAM_PAGES ((DRAM_SIZE<<10)>>2);
//#define DRAM_PAGES 10

using namespace std;

extern uint8_t warmup_complete[NUM_CPUS], 
               simulation_complete[NUM_CPUS], 
               all_warmup_complete, 
               all_simulation_complete,
               MAX_INSTR_DESTINATIONS,
               knob_cloudsuite,
               knob_low_bandwidth;

extern uint64_t current_core_cycle[NUM_CPUS], 
                stall_cycle[NUM_CPUS], 
                last_drc_read_mode, 
                last_drc_write_mode,
                drc_blocks;

extern uint64_t minor_fault[NUM_CPUS], major_fault[NUM_CPUS];

void print_stats();

constexpr uint64_t lg2(uint64_t n)
{
    return n < 2 ? 0 : 1+lg2(n/2);
}
#endif
