#include "page_walker.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <random>
#include <unordered_map>

#include "block.h"
#include "champsim.h"
#include "ooo_cpu.h"
#include "uncore.h"

// These arrays also modified in dram_controller.cc
std::array<bool, DRAM_PAGES> ppage_nru;
std::array<bool, DRAM_PAGES> ppage_alloc; // Is the given physical page allocated?

extern VirtualMemory vmem;

void dealloc_page(uint32_t cpu, uint64_t va, uint64_t pa)
{
    // invalidate corresponding vpage and ppage from the cache hierarchy
    ooo_cpu[cpu].ITLB.invalidate_entry(va);
    ooo_cpu[cpu].DTLB.invalidate_entry(va);
    ooo_cpu[cpu].STLB.invalidate_entry(va);
    for (uint32_t i=0; i<BLOCK_SIZE; i++) {
        uint64_t cl_addr = (pa << (LOG2_PAGE_SIZE-LOG2_BLOCK_SIZE)) | i;
        ooo_cpu[cpu].L1I.invalidate_entry(cl_addr);
        ooo_cpu[cpu].L1D.invalidate_entry(cl_addr);
        ooo_cpu[cpu].L2C.invalidate_entry(cl_addr);
        uncore.LLC.invalidate_entry(cl_addr);
    }
    //page_table.erase(va);
    //ppage_alloc[pa] = false;
}

template <typename T>
class match_page
{
    uint64_t match_addr;
    public:
        explicit match_page(uint64_t match_addr) : match_addr(match_addr >> LOG2_PAGE_SIZE) {}
        bool operator() (T val) { return (val.packet.address>>LOG2_PAGE_SIZE) == match_addr; }
};

void PageWalker::operate()
{
    for (auto entry : active_page_walks)
    {
        // Retire finished page walks
        if (entry.valid && !entry.inflight && entry.level_to_issue == vmem.get_paget_table_level_count())
        {
            entry.packet.data = vmem.va_to_pa(entry.packet.cpu, entry.packet.v_address);
            if (entry.packet.instruction)
                upper_level_icache[entry.packet.cpu]->return_data(&entry.packet);
            if (entry.packet.data)
                upper_level_dcache[entry.packet.cpu]->return_data(&entry.packet);
            entry.valid = false;
        }

        // Advance ready walks
        if (entry.valid && !entry.inflight)
        {
            entry.translation_request = entry.packet;

            entry.translation_request.full_addr = vmem.get_pte_pa(entry.packet.cpu, entry.packet.v_address, entry.level_to_issue);
            entry.translation_request.address = entry.translation_request.full_addr >> LOG2_BLOCK_SIZE;

            // populate packet with the contents of the request
            entry.translation_request.fill_level = FILL_L1;
            entry.translation_request.fill_l1d = 1;
            entry.translation_request.type = LOAD;
            entry.translation_request.event_cycle = current_core_cycle[entry.packet.cpu];

            lower_level->add_rq(&entry.translation_request);
            entry.inflight = true;
            entry.level_to_issue++;
        }
    }

    // Get requests from RQ
    std::size_t count_issued = 0;
    while (RQ.occupancy > 0 && count_issued < PW_ISSUE_WIDTH)
    {
        PACKET &rq_entry = RQ.entry[RQ.head];

        // Detect already in flight walk
        auto it = std::find_if(active_page_walks.begin(), active_page_walks.end(), match_page<ActiveWalkData>(rq_entry.address));

        if (it == active_page_walks.end()) // No duplicate found
        {
            // Find invalid way
            auto it = std::find_if(active_page_walks.begin(), active_page_walks.end(), [](ActiveWalkData x){ return !x.valid; });
            if (it == active_page_walks.end()) // No space available
                break;

            // Add to active walks
            it->valid = true;
            it->inflight = false;
            it->level_to_issue = 0;
            it->packet = rq_entry;
            count_issued++;
        }

        // Pop from queue
        RQ.occupancy--;
        RQ.head++;
        if (RQ.head >= RQ.SIZE)
            RQ.head = 0;
    }
}

int PageWalker::add_rq(PACKET *packet)
{
    assert(packet->address != 0);
    int index = RQ.check_queue(packet);
    if (index != -1)
    {
        RQ.entry[index].instruction |= packet->instruction;
        RQ.entry[index].data        |= packet->data;
        RQ.entry[index].rob_index_depend_on_me.insert(packet->rob_index);
        if (packet->instruction)
        {
            RQ.entry[index].instr_merged = 1;
        }
        if (packet->data)
        {
            RQ.entry[index].load_merged = 1;
        }

        RQ.MERGED++;
        RQ.ACCESS++;

#ifdef DEBUG_PRINT
        if (warmup_complete[packet->cpu])
        {
            std::cout << "[PAGE_WALK_MERGED] " << __func__ << " cpu: " << packet->cpu << " instr_id: " << RQ.entry[index].instr_id;
            std::cout << " merged rob_index: " << packet->rob_index << " instr_id: " << packet->instr_id << std::endl;
        }
#endif //ifdef DEBUG_PRINT

        return index;
    }

    // check occupancy
    if (RQ.occupancy == RQ.SIZE)
    {
        RQ.FULL++;
        return -2; // cannot handle this request
    }

    // if there is no duplicate, add it to RQ
    index = RQ.tail;
    assert(RQ.entry[index].address == 0);

    RQ.entry[index] = *packet;

    RQ.occupancy++;
    RQ.tail++;
    if (RQ.tail >= RQ.SIZE)
        RQ.tail = 0;

#ifdef DEBUG_PRINT
    if (warmup_complete[RQ.entry[index].cpu])
    {
        std::cout << "[PAGE_WALK_RQ] " <<  __func__ << " instr_id: " << RQ.entry[index].instr_id << " address: " << std::hex << RQ.entry[index].address;
        std::cout << " full_addr: " << RQ.entry[index].full_addr << std::dec;
        std::cout << " type: " << +RQ.entry[index].type << " head: " << RQ.head << " tail: " << RQ.tail << " occupancy: " << RQ.occupancy;
        std::cout << " event: " << RQ.entry[index].event_cycle << " current: " << current_core_cycle[RQ.entry[index].cpu] << std::endl;
    }
#endif //ifdef DEBUG_PRINT

    RQ.TO_CACHE++;
    RQ.ACCESS++;

    return -1;
}

void PageWalker::return_data(PACKET *packet)
{
    auto it = std::find_if(active_page_walks.begin(), active_page_walks.end(), [packet](ActiveWalkData x){ return x.translation_request.address == packet->address; });
    assert(it != active_page_walks.end());

    it->inflight = false;
}

