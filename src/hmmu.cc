#include "hmmu.h"
#include "cache.h"

int MEMORY_MANAGER::add_rq(PACKET *packet)
{
    // check occupancy
    if (RQ.occupancy == HMMU_RQ_SIZE) {
        RQ.FULL++;

        return -2; // cannot handle this request
    }

    int index = RQ.tail;

#ifdef SANITY_CHECK
    if (RQ.entry[index].address != 0) {
        cerr << "[" << NAME << "_ERROR] " << __func__ << " is not empty index: " << index;
        cerr << " address: " << hex << RQ.entry[index].address;
        cerr << " full_addr: " << RQ.entry[index].full_addr << dec << endl;
        assert(0);
    }
#endif

    RQ.entry[index] = *packet;
    RQ.entry[index].hm_addr = get_hm_addr(RQ.entry[index].address);

    RQ.occupancy++;
    RQ.tail++;
    if (RQ.tail >= RQ.SIZE)
        RQ.tail = 0;

    DP ( if (warmup_complete[RQ.entry[index].cpu]) {
    cout << "[" << NAME << "_RQ] " <<  __func__ << " instr_id: " << RQ.entry[index].instr_id << " address: " << hex << RQ.entry[index].address;
    cout << " full_addr: " << RQ.entry[index].full_addr << dec;
    cout << " type: " << +RQ.entry[index].type << " head: " << RQ.head << " tail: " << RQ.tail << " occupancy: " << RQ.occupancy;
    cout << " event: " << RQ.entry[index].event_cycle << " current: " << current_core_cycle[RQ.entry[index].cpu] << endl; });

    if (packet->address == 0)
        assert(0);

    return -1;
}

int MEMORY_MANAGER::add_wq(PACKET *packet)
{
    // sanity check
    if (WQ.occupancy >= WQ.SIZE)
        assert(0);

    int index = WQ.tail;
    if (WQ.entry[index].address != 0) {
        cerr << "[" << NAME << "_ERROR] " << __func__ << " is not empty index: " << index;
        cerr << " address: " << hex << WQ.entry[index].address;
        cerr << " full_addr: " << WQ.entry[index].full_addr << dec << endl;
        assert(0);
    }

    WQ.entry[index] = *packet;
    WQ.entry[index].hm_addr = get_hm_addr(WQ.entry[index].address);

    WQ.occupancy++;
    WQ.tail++;
    if (WQ.tail >= WQ.SIZE)
        WQ.tail = 0;

    DP (if (warmup_complete[WQ.entry[index].cpu]) {
    cout << "[" << NAME << "_WQ] " <<  __func__ << " instr_id: " << WQ.entry[index].instr_id << " address: " << hex << WQ.entry[index].address;
    cout << " full_addr: " << WQ.entry[index].full_addr << dec;
    cout << " head: " << WQ.head << " tail: " << WQ.tail << " occupancy: " << WQ.occupancy;
    cout << " data: " << hex << WQ.entry[index].data << dec;
    cout << " event: " << WQ.entry[index].event_cycle << " current: " << current_core_cycle[WQ.entry[index].cpu] << endl; });

    return -1;
}

int MEMORY_MANAGER::add_pq(PACKET *packet)
{
    return -1;
}

void MEMORY_MANAGER::return_data(PACKET *packet)
{

}

void MEMORY_MANAGER::increment_WQ_FULL(uint64_t address)
{
    WQ.FULL++;
}

uint32_t MEMORY_MANAGER::get_occupancy(uint8_t queue_type, uint64_t address)
{
    if (queue_type == 0)
        return MSHR.occupancy;
    else if (queue_type == 1)
        return RQ.occupancy;
    else if (queue_type == 2)
        return WQ.occupancy;
    else if (queue_type == 3)
        return PQ.occupancy;

    return 0;
}

uint32_t MEMORY_MANAGER::get_size(uint8_t queue_type, uint64_t address)
{
    if (queue_type == 0)
        return MSHR.SIZE;
    else if (queue_type == 1)
        return RQ.SIZE;
    else if (queue_type == 2)
        return WQ.SIZE;
    else if (queue_type == 3)
        return PQ.SIZE;

    return 0;
}

void MEMORY_MANAGER::handle_write()
{
//distribute write requests
    uint32_t index = WQ.head;
    uint8_t w_forwarded = 1;

    if ((WQ.head == WQ.tail) && WQ.occupancy == 0) {
        //no request in flight, no action
    }
    else{
    if (get_memtype(WQ.entry[index].hm_addr) < 1) { //send request to DRAM
        if (lower_level->get_occupancy(2, WQ.entry[index].address) == lower_level->get_size(2, WQ.entry[index].address)) {

            // DRAM WQ is full, cannot replace this victim
            w_forwarded = 0;
            lower_level->increment_WQ_FULL(WQ.entry[index].address);
            STALL[WQ.entry[index].type]++;
        }
        else {
            PACKET writeback_packet;

            //writeback_packet.fill_level = fill_level << 1;
            writeback_packet.cpu = WQ.entry[index].cpu;
            writeback_packet.address = WQ.entry[index].address;
            writeback_packet.full_addr = WQ.entry[index].full_addr;
            writeback_packet.data = WQ.entry[index].data;
            writeback_packet.instr_id = WQ.entry[index].instr_id;
            writeback_packet.ip = 0; // writeback does not have ip
            writeback_packet.type = WRITEBACK;
            writeback_packet.event_cycle = WQ.entry[index].event_cycle;

            lower_level->add_wq(&writeback_packet);
        }
        if (w_forwarded) {
            WQ.remove_queue(&WQ.entry[index]);
        }
    }
	else { //send request to NVM
        update_page_table(WQ.entry[index].address);
        if (extra_interface->get_occupancy(2, WQ.entry[index].address) == extra_interface->get_size(2, WQ.entry[index].address)) {

            // NVM WQ is full, cannot replace this victim
            w_forwarded = 0;
            extra_interface->increment_WQ_FULL(WQ.entry[index].address);
            STALL[WQ.entry[index].type]++;
        }
        else {
            PACKET writeback_packet;

            //writeback_packet.fill_level = fill_level << 1;
            writeback_packet.cpu = WQ.entry[index].cpu;
            writeback_packet.address = WQ.entry[index].address;
            writeback_packet.full_addr = WQ.entry[index].full_addr;
            writeback_packet.data = WQ.entry[index].data;
            writeback_packet.instr_id = WQ.entry[index].instr_id;
            writeback_packet.ip = 0; // writeback does not have ip
            writeback_packet.type = WRITEBACK;
            writeback_packet.event_cycle = WQ.entry[index].event_cycle;

            extra_interface->add_wq(&writeback_packet);
        }
        if (w_forwarded) {
            WQ.remove_queue(&WQ.entry[index]);
        }
    }
    }
}

void MEMORY_MANAGER::handle_read()
{
//distribute read requests
    uint32_t index = RQ.head;
    uint8_t r_forwarded = 1;

    if ((RQ.head == RQ.tail) && RQ.occupancy == 0) {
        //no action
    }
    else{
    if (get_memtype(RQ.entry[index].hm_addr) < 1) { //send request to DRAM
        if (lower_level->get_occupancy(1, RQ.entry[index].address) == lower_level->get_size(1, RQ.entry[index].address)){
            r_forwarded = 0;
	        STALL[RQ.entry[index].type]++;
        }
        else{
	        lower_level->add_rq(&RQ.entry[index]);
        }
        if (r_forwarded) {
            RQ.remove_queue(&RQ.entry[index]);
        }
    }
    else { //send request to NVM
        update_page_table(RQ.entry[index].address);
        if (extra_interface->get_occupancy(1, RQ.entry[index].address) == extra_interface->get_size(1, RQ.entry[index].address)){
	        r_forwarded = 0;
            STALL[RQ.entry[index].type]++;
        }
        else{
	        extra_interface->add_rq(&RQ.entry[index]);
        }
        if (r_forwarded) {
            RQ.remove_queue(&RQ.entry[index]);
        }             
    }
    }
}

void MEMORY_MANAGER::request_dispatch()
{
    handle_write();
    handle_read();
}

uint32_t MEMORY_MANAGER::get_memtype(uint64_t address)
{
    int shift = 29;

    return (uint32_t) (address >> shift) & (NVM_RATIO - 1);
}

uint64_t MEMORY_MANAGER::get_hybrid_memory_pn(uint64_t address)
{
    uint64_t physical_pn = address >> LOG2_PAGE_SIZE;
    uint64_t hybrid_memory_pn = ppage_to_hmpage_map[physical_pn];

    return hybrid_memory_pn;
}

uint64_t MEMORY_MANAGER::get_hm_addr(uint64_t address)
{
    uint64_t physical_pn = address >> LOG2_PAGE_SIZE;
    uint64_t page_offset = ((1<<LOG2_PAGE_SIZE)-1) & address;
    uint64_t hybrid_memory_pn = ppage_to_hmpage_map[physical_pn];

    return (((hybrid_memory_pn)<<LOG2_PAGE_SIZE) + page_offset);
}

uint64_t MEMORY_MANAGER::hmmu_rand()
{
    random_state += HMMU_RAND_FACTOR + counter;

    random_state ^= (random_state<<13);
    random_state ^= (random_state>>7);
    random_state ^= (random_state<<11);
    random_state ^= (random_state>>1);
  
    return random_state;
}

uint64_t MEMORY_MANAGER::search_fast_page()
{
    uint32_t is_slow = 1;
    uint64_t candidate_physical_pn = 0;
    uint64_t candidate_hybrid_memory_pn = 0;
    while(is_slow) //while the mapped candidate is a slow memory page
    {
        //ppage_to_hmpage_map[(hmmu_rand()%(num_ppages))] > DRAM_PAGES/8
        candidate_physical_pn = hmmu_rand()%(num_ppages);
        candidate_hybrid_memory_pn = ppage_to_hmpage_map[candidate_physical_pn];
        if(candidate_hybrid_memory_pn < DRAM_PAGES/8) //already find a fast page
        {
            is_slow = 0;
        }
        else
        {
            counter++;
        }
    }
    return candidate_physical_pn;
}

void MEMORY_MANAGER::page_swap(uint64_t requested, uint64_t victim)
{
    uint64_t temp = ppage_to_hmpage_map[requested];
    ppage_to_hmpage_map[requested] = ppage_to_hmpage_map[victim];
    ppage_to_hmpage_map[victim] = temp;
}

void MEMORY_MANAGER::update_page_table(uint64_t address)
{
    uint64_t physical_pn = address >> LOG2_PAGE_SIZE;
    uint64_t victim_ppn = search_fast_page();
    page_swap(physical_pn, victim_ppn);
}

void MEMORY_MANAGER::operate()
{

    request_dispatch();
}