#ifndef PAGE_WALKER_H
#define PAGE_WALKER_H

#include "memory_class.h"

#include <array>
#include <cstdlib>
#include <map>
#include <type_traits>

#include "champsim.h"

#define PW_REG_SIZE 4
#define PW_ISSUE_WIDTH 1

constexpr unsigned int SWAP_LATENCY = 10000; // Not including time to memory

class PageWalker : public MEMORY
{
    struct ActiveWalkData
    {
        bool valid;
        bool inflight;
        unsigned int level_to_issue;
        PACKET packet, translation_request;
    };

    std::array<ActiveWalkData, PW_REG_SIZE> active_page_walks = {};

    public:
    PageWalker() {}

    // functions
    int  add_rq(PACKET *packet);
    int  add_wq(PACKET *packet) { return 0; } //unimplemented
    int  add_pq(PACKET *packet) { return 0; } //unimplemented
    void return_data(PACKET *packet);
    void operate();
    void increment_WQ_FULL(uint64_t address) {}
    uint32_t get_occupancy(uint8_t queue_type, uint64_t address) { return queue_type == 0 ? RQ.occupancy : 0; }
    uint32_t get_size(uint8_t queue_type, uint64_t address) { return queue_type == 0 ? RQ.SIZE : 0; }
};

#endif

