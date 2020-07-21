#ifndef PAGE_WALKER_H
#define PAGE_WALKER_H

#include "memory_class.h"

#include <cstdlib>
#include <map>
#include <type_traits>

#include "champsim.h"

constexpr unsigned long long VIRTUAL_MEM_SIZE      = 1ull << 48;
constexpr unsigned int       PW_PAGE_SIZE          = 4096;
constexpr unsigned int       PW_ENTRY_SIZE         = 8;
constexpr unsigned int       VM_PAGETABLE_RESERVED = 1 << 10; // Number of pages reserved for page table

constexpr unsigned int PW_NUM_TABLE_ENTRIES = PW_PAGE_SIZE/PW_ENTRY_SIZE; // except for possibly the last table
constexpr unsigned int PW_NUM_LEVELS        = lg(VIRTUAL_MEM_SIZE/PAGE_SIZE, PW_NUM_TABLE_ENTRIES);
constexpr unsigned int PW_ISSUE_WIDTH       = 4;

constexpr unsigned int SWAP_LATENCY = 10000; // Not including time to memory

namespace impl
{
    template <std::size_t LEVEL>
        class PageDirectoryNode
        {
            public:
                bool valid = false;
                uint64_t address;
                std::map<unsigned int, PageDirectoryNode<LEVEL-1>> directory;
                explicit PageDirectoryNode(uint64_t addr) : valid(true), address(addr), directory() {}
        };

    template <>
        class PageDirectoryNode<0>
        {
            public:
                bool valid = false;
                uint64_t address;
                explicit PageDirectoryNode(uint64_t addr) : valid(true), address(addr) {}
        };
};

class PageWalker : public MEMORY
{
    struct ActiveWalkData
    {
        bool valid;
        bool inflight;
        unsigned int level_to_issue;
        PACKET packet;
    };

    std::vector<PACKET> active_page_walks = {};
    impl::PageDirectoryNode<PW_NUM_LEVELS> page_directory;

    public:
    explicit PageWalker(uint64_t root_address) : page_directory(root_address) {}

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

