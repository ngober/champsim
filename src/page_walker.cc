#include "page_walker.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <random>
#include <unordered_map>

#include "block.h"
#include "champsim.h"

std::random_device rd;
std::mt19937_64 rng{rd()};
std::uniform_int_distribution<uint64_t> ppage_numbers {VM_PAGETABLE_RESERVED, DRAM_PAGES}; // used to generate random physical page numbers
std::uniform_int_distribution<uint64_t> adj_ppages {0, 1<<10}; // how many pages are adjacent to one another

std::unordered_map<uint64_t, uint64_t> page_table;

// These arrays also modified in dram_controller.cc
std::array<bool, DRAM_PAGES> ppage_nru;
std::array<bool, DRAM_PAGES> ppage_alloc; // Is the given physical page allocated?

constexpr uint64_t make_unique(uint32_t cpu, uint64_t addr)
{
    return addr ^ rotr(cpu, lg2(NUM_CPUS));
}

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
    page_table.erase(va);
    ppage_alloc[pa] = false;
}

uint64_t va_to_pa(uint32_t cpu, uint64_t va)
{
    assert(va != 0);

    uint8_t  swap = 0;
    uint64_t unique_va = make_unique(cpu, va);
    uint64_t vpage = make_unique(cpu, va >> LOG2_PAGE_SIZE);
    uint64_t offset = va & ((1<<LOG2_PAGE_SIZE) - 1);

    auto pr = page_table.find(vpage);
    if (pr == page_table.end()) // no VA => PA translation found 
    {
        // TODO do not search PT sector
        if (std::all_of(ppage_alloc.begin(), ppage_alloc.end(), [](bool x){return x;})) // not enough memory
        {
            // NRU replacement
            auto nru_it = std::find(ppage_nru.begin(), ppage_nru.end(), true);
            if (nru_it == ppage_nru.end())
            {
                ppage_nru.fill(true);
                pr = page_table.begin();
            }
            else
            {
                pr = std::next(page_table.begin(), std::distance(ppage_nru.begin, nru_it));
            }
            assert(pr != page_table.end());
            uint64_t mapped_ppage = pr->second;

#ifdef DEBUG_PRINT
            if (warmup_complete[cpu])
                std::cout << "[SWAP] update page table NRU_vpage: " << std::hex << pr->first << " new_vpage: " << vpage << " ppage: " << pr->second << std::dec << std::endl;
#endif
            dealloc_page(cpu, pr->first, mapped_ppage);

            // update page table with new VA => PA mapping
            page_table.insert(std::make_pair(vpage, {false, mapped_ppage}));
            ppage_alloc[mapped_ppage] = true;

            // swap complete
            major_fault[cpu]++;
            stall_cycle[cpu] = current_core_cycle[cpu] + SWAP_LATENCY;
        }
        else
        {
            uint64_t next_ppage;
            bool fragmented = false;
            if (num_adjacent_page > 0)
            {
                next_ppage = previous_ppage + 1;
            }
            else
            {
                next_ppage = ppage_numbers(rng);
                fragmented = true;
            }

            while (ppage_alloc[next_ppage]) // next_ppage is not available
            {
#ifdef DEBUG_PRINT
                if (warmup_complete[cpu])
                    std::cout << "vpage: " << std::hex << ppage_check->first << " is already mapped to ppage: " << next_ppage << std::dec << std::endl; //FIXME
#endif
                fragmented = 1;
                next_ppage = ppage_numbers(rng); // try again
            }

            // insert translation to page tables
            pr = page_table.insert(std::make_pair(vpage, {false, next_ppage}));
            previous_ppage = next_ppage;
            num_adjacent_page--;
            num_page[cpu]++;
            ppage_alloc[next_ppage] = true;

            // try to allocate pages contiguously
            if (fragmented)
            {
                num_adjacent_page = adj_ppages(rng);
#ifdef DEBUG_PRINT
                if (warmup_complete[cpu])
                    std::cout << "Recalculate num_adjacent_page: " << num_adjacent_page << std::endl;
#endif
            }

            minor_fault[cpu]++;
            //TODO latency for minor faults?
        }
    }

    assert(pr != page_table.end())
    uint64_t ppage = pr->second;
    uint64_t pa = (ppage << LOG2_PAGE_SIZE) | offset;

#ifdef DEBUG_PRINT
    if (warmup_complete[cpu])
        std::cout << "[PAGE_TABLE] vpage: " << std::hex << vpage << " => ppage: " << ppage << " vadress: " << unique_va << " paddress: " << pa << std::dec << std::endl;
#endif
    return pa;
}

std::size_t get_table_index(uint64_t addr, std::size_t level)
{
    assert(level <= PW_NUM_LEVELS);
    if (level < PW_NUM_LEVELS)
    {
        return (addr >> (LOG2_PAGE_SIZE+level*lg2(PW_NUM_TABLE_ENTRIES))) % PW_NUM_TABLE_ENTRIES;
    }
    else //if (level == PW_NUM_LEVELS)
    {
        // get the leftover high-order bits
        return (addr >> (LOG2_PAGE_SIZE+level*lg2(PW_NUM_TABLE_ENTRIES))) % (VIRTUAL_MEM_SIZE/((level-1) * PW_NUM_TABLE_ENTRIES));
    }
}

template <typename T>
class match_page
{
    uint64_t match_addr;
    public:
        explicit match_page(uint64_t match_addr) : match_addr(match_addr >> LOG2_PAGE_SIZE) {}
        bool operator() (T val) { return (val.address>>LOG2_PAGE_SIZE) == match_addr; }
};

void PageWalker::operate()
{
    for (auto entry : active_page_walks)
    {
        // Retire finished page walks
        if (entry.valid && !entry.inflight && entry.level_to_issue == 0)
        {
            entry.packet.data = va_to_pa(entry.packet.cpu, entry.packet.v_address);
            if (entry.packet.instruction)
                upper_level_icache[entry.packet.cpu].return_data(&entry.packet);
            if (entry.packet.data)
                upper_level_dcache[entry.packet.cpu].return_data(&entry.packet);
            entry.valid = false;
        }

        // Advance ready walks
        if (entry.valid && !entry.inflight)
        {
            PACKET walk_pkt;

            //TODO if directory page not found
            auto it = page_directory.directory[get_table_index(entry.packet.address, PW_NUM_LEVELS)];
            for (std::size_t i = PW_NUM_LEVELS-1; i > entry.level_to_issue; --i)
            {
                it = it->directory[get_table_index(entry.packet.address, i)];
            }

            walk_pkt.address = it->address >> LOG2_BLOCK_SIZE;
            walk_pkt.full_addr = it->address;
            walk_pkt.v_address = entry.packet.virtual_address >> LOG2_BLOCK_SIZE; //FIXME
            walk_pkt.full_v_addr = entry.packet.virtual_address;

            // populate packet with the contents of the request
            walk_pkt.fill_level = FILL_L1;
            walk_pkt.fill_l1d = 1;
            walk_pkt.type = LOAD;
            walk_pkt.cpu = entry.packet.cpu;
            walk_pkt.data_index = entry.packet.data_index;
            walk_pkt.lq_index = entry.packet.lq_index;
            walk_pkt.instr_id = entry.packet.instr_id;
            walk_pkt.rob_index = entry.packet.rob_index;
            walk_pkt.ip = entry.packet.ip;
            walk_pkt.asid[0] = entry.packet.asid[0];
            walk_pkt.asid[1] = entry.packet.asid[1];
            walk_pkt.event_cycle = current_core_cycle[entry.packet.cpu];

            lower_level->add_rq(&packet);
            entry.inflight = true;
            entry.level_to_issue--;
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
            auto it = std::find_if(active_page_walks.begin(), active_page_walks.end(), [](std::pair<Status, PACKET> x){ return !x.valid; });
            if (it == active_page_walks.end()) // No space available
                break;

            // Add to active walks
            it->valid = true;
            it->inflight = false;
            it->level_to_issue = PW_NUM_LEVELS;
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
    assert(packet->address != 0)
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
    assert(RQ.entry[index].address == 0)

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
    auto it = std::find(active_page_walks.begin(), active_page_walks.end(), [packet](ActiveWalkData x){ return x.address == packet->address; });
    assert(it != active_page_walks.end());

    it->inflight = false;
}

