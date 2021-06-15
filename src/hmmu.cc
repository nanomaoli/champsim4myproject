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
    if (get_memtype(WQ.entry[index].hm_addr) < 1) { //mapped to DRAM
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
	else { //mapped to NVM

        uint32_t way = check_hit(&WQ.entry[index]);
        if (way == -1) {//this block is not in the cache, finally will be billed NVM latency
            uint32_t fill_set = get_set(WQ.entry[index].hm_addr);
            uint32_t fill_way = find_lru_victim(WQ.entry[index].hm_addr);
            //going to cache this block
            if (block[fill_set][fill_way].valid){//eviction happens, need clear that block's record in the block_counter
                reset_cached_block(block[fill_set][fill_way].hm_addr);
                //TODO write back to NVM if dirty, COMPLETE THIS PART AFTER FINISHING DMA MODEL & TIMING ISSUES
                /*if (block[fill_set][fill_way].dirty){
                    extra_interface->add_wq(build a writeback packet using block data);
                }*/
            }

            fill_cache(fill_set, fill_way, &WQ.entry[index]);
            //lru_update(fill_set, fill_way); LRU update should happen IN THIS PLACE, but not use here for writes
            set_cached_block(WQ.entry[index].hm_addr);//set the bit in the bitset to record the cached block
            //check if the number of cached blocks hit the threshold
            uint8_t cached_number = get_cached_number(WQ.entry[index].hm_addr);
            if (cached_number >= THRESHOLD) {
                //invalidate all the blocks from that page
                uint64_t target_page = (WQ.entry[index].hm_addr >> LOG2_PAGE_SIZE) & ((1 << LOG2_DRAM_PAGES)-1);
                for (uint32_t set=0; set<NUM_SET; set++) {
                    for (uint32_t way=0; way<NUM_WAY; way++) {
                        uint64_t block_hm_addr = block[set][way].hm_addr;
                        uint64_t block_page = (block_hm_addr >> LOG2_PAGE_SIZE) & ((1 << LOG2_DRAM_PAGES)-1);
                        if (block[set][way].valid && block_page == target_page) {
                            block[set][way].valid = 0;
                            //TODO write back to DRAM area if dirty
                            //lower_level->add_wq(build a writeback packet using block data);
                        }
                    }
                }
                //reset all bits in the corresponding bitset
                block_counter[target_page].reset();

                //do the page swap
                update_page_table(WQ.entry[index].address);
            }
            // changed get_memtype func for mem choice
            // there would be a bug:
            // one packet triggered a cache fill
            // then the threshold is hit, and do a page swap
            // but then the NVM queue is full, stalled to next cycle
            // if get_memtype uses old hm_addr, which will go to NVM again
            // and at that time cache blocks are evicted, the subpage block filling will happen again
            // IF STALLED TO NEXT CYCLE, NEED TO UPDATE HM_ADDR IN THE PACKET 
            // represent that the data migration completes in the stalled time
            // QUESTION: THIS MAY REQUIRE DMA TO ADD THE R/W QUEUES IN THE ABOVE, SO THE STALL AND MIGRATION MATCH
            // QUESTION: MAYBE DO PAGE SWAP EARLIER, RIGHT AFTER COMPARING THE THRESHOLD,
            //           AND THEN THE DIRTY BLOCKS IN DRAM CACHE COULD USE TO UPDATE THE DRAM PAGE
            if (extra_interface->get_occupancy(2, WQ.entry[index].address) == extra_interface->get_size(2, WQ.entry[index].address)) {

                // NVM WQ is full, cannot replace this victim
                w_forwarded = 0;
                extra_interface->increment_WQ_FULL(WQ.entry[index].address);
                STALL[WQ.entry[index].type]++;
                //update hm_addr
                WQ.entry[index].hm_addr = get_hm_addr(WQ.entry[index].address);
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
        else {//this block is cached, DRAM latency gets used
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
        //SUMMARY: set a bit in the bitset(to be defined) to count the cache occupancy,
        //then check if there is enough subpage blocks to trigger a page swap
        //if true, invalid all these subpage blocks and invoke page swap
        //if not enough, nothing special would happen
        //in both cases, then send the current request to NVM(as the old code does)
        //for the case when way is not -1, ALREADY CACHED, just send request to DRAM
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
        uint32_t way = check_hit(&RQ.entry[index]);
        if (way == -1) {//this block is not in the cache, finally will be billed NVM latency
            uint32_t fill_set = get_set(RQ.entry[index].hm_addr);
            uint32_t fill_way = find_lru_victim(RQ.entry[index].hm_addr);
            //going to cache this block
            if (block[fill_set][fill_way].valid){//eviction happens, need clear that block's record in the block_counter
                reset_cached_block(block[fill_set][fill_way].hm_addr);
                //TODO write back to NVM if dirty, COMPLETE THIS PART AFTER FINISHING DMA MODEL & TIMING ISSUES
                /*if (block[fill_set][fill_way].dirty){
                    extra_interface->add_wq(build a writeback packet using block data);
                }*/
            }

            fill_cache(fill_set, fill_way, &RQ.entry[index]);
            lru_update(fill_set, fill_way); //update LRU states on reads
            set_cached_block(RQ.entry[index].hm_addr);//set the bit in the bitset to record the cached block
            //check if the number of cached blocks hit the threshold
            uint8_t cached_number = get_cached_number(RQ.entry[index].hm_addr);
            if (cached_number >= THRESHOLD) {
                //invalidate all the blocks from that page
                uint64_t target_page = (RQ.entry[index].hm_addr >> LOG2_PAGE_SIZE) & ((1 << LOG2_DRAM_PAGES)-1);
                for (uint32_t set=0; set<NUM_SET; set++) {
                    for (uint32_t way=0; way<NUM_WAY; way++) {
                        uint64_t block_hm_addr = block[set][way].hm_addr;
                        uint64_t block_page = (block_hm_addr >> LOG2_PAGE_SIZE) & ((1 << LOG2_DRAM_PAGES)-1);
                        if (block[set][way].valid && block_page == target_page) {
                            block[set][way].valid = 0;
                            //TODO write back to DRAM area if dirty
                            //lower_level->add_wq(build a writeback packet using block data);
                        }
                    }
                }
                //reset all bits in the corresponding bitset
                block_counter[target_page].reset();

                //do the page swap
                update_page_table(RQ.entry[index].address);
            }

            if (extra_interface->get_occupancy(1, RQ.entry[index].address) == extra_interface->get_size(1, RQ.entry[index].address)){
	            r_forwarded = 0;
                STALL[RQ.entry[index].type]++;
                //update hm_addr
                RQ.entry[index].hm_addr = get_hm_addr(RQ.entry[index].address);
            }
            else{
	            extra_interface->add_rq(&RQ.entry[index]);
            }
            if (r_forwarded) {
                RQ.remove_queue(&RQ.entry[index]);
            } 
        }
        else {//this block is cached, DRAM latency gets used
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
    //int shift = 29;
    //return (uint32_t) (address >> shift) & (NVM_RATIO - 1);
    // CHANGE TO STH USING PAGE NUMBER, AND OUTPUT A REPRESENTATIVE NUMBER
    uint32_t mem = 0;
    uint64_t pn = (address >> LOG2_PAGE_SIZE) & ((1 << LOG2_DRAM_PAGES)-1);
    if (pn < DRAM_PAGES/NVM_RATIO) {
        mem = 0; //go DRAM
    }
    else mem = 1; //go NVM

    return mem;
}

uint64_t MEMORY_MANAGER::get_hybrid_memory_pn(uint64_t address)
{
    uint64_t physical_pn = (address >> LOG2_PAGE_SIZE) & ((1 << LOG2_DRAM_PAGES)-1);
    uint64_t hybrid_memory_pn = ppage_to_hmpage_map[physical_pn];

    return hybrid_memory_pn;
}

uint64_t MEMORY_MANAGER::get_hm_addr(uint64_t address)
{
    uint64_t physical_pn = (address >> LOG2_PAGE_SIZE) & ((1 << LOG2_DRAM_PAGES)-1);
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
    uint64_t physical_pn = (address >> LOG2_PAGE_SIZE) & ((1 << LOG2_DRAM_PAGES)-1);
    uint64_t victim_ppn = search_fast_page();
    page_swap(physical_pn, victim_ppn);
}

void MEMORY_MANAGER::operate()
{

    request_dispatch();
}

uint32_t MEMORY_MANAGER::check_hit(PACKET *packet)
{
    //this cache operates in the hybrid memory address space, so use hm_addr
    uint32_t set = get_set(packet->hm_addr);
    int hit_way = -1;

    if (NUM_SET < set) {
        cerr << "[" << NAME << "_ERROR] " << __func__ << " invalid set index: " << set << " NUM_SET: " << NUM_SET;
        cerr << " address: " << hex << packet->address << " hm_addr:" << hex << packet->hm_addr << " full_addr: " << packet->full_addr << dec;
        cerr << " event: " << packet->event_cycle << endl;
        assert(0);
    }
    //fast memory cache hit, here just use hm_addr as tag for simulation
    for (uint32_t way=0; way<NUM_WAY; way++) {
        if (block[set][way].valid && (block[set][way].tag == packet->hm_addr)) {
            hit_way = way;
            break;
        }
    }
    return hit_way;
}

void MEMORY_MANAGER::invalidate_entry(uint64_t inval_addr) //this is for invalidating a block with a given address
{
    uint32_t set = get_set(inval_addr);
    //int match_way = -1;

    if (NUM_SET < set) {
        cerr << "[" << NAME << "_ERROR] " << __func__ << " invalid set index: " << set << " NUM_SET: " << NUM_SET;
        cerr << " inval_addr: " << hex << inval_addr << dec << endl;
        assert(0);
    }

    // invalidate
    for (uint32_t way=0; way<NUM_WAY; way++) {
        if (block[set][way].valid && (block[set][way].tag == inval_addr)) {

            block[set][way].valid = 0;

            //match_way = way;

            DP ( if (warmup_complete[cpu]) {
            cout << "[" << NAME << "] " << __func__ << " inval_addr: " << hex << inval_addr;  
            cout << " tag: " << block[set][way].tag << " data: " << block[set][way].data << dec;
            cout << " set: " << set << " way: " << way << " lru: " << block[set][way].lru << " cycle: " << current_core_cycle[cpu] << endl; });

            break;
        }
    }

    //return match_way;
}

uint32_t MEMORY_MANAGER::get_set(uint64_t address)
{
    return (uint32_t) ((address & (((1 << LOG2_FMC_SET) - 1) << 7)) >> 7);     
}

uint32_t MEMORY_MANAGER::find_lru_victim(uint64_t addr)
{
    uint32_t set = get_set(addr);
    uint32_t way = 0;

    // first see if there is any invalid line
    for (way=0; way<NUM_WAY; way++) {
        if (block[set][way].valid == false) {

            DP ( if (warmup_complete[cpu]) {
            cout << "[" << NAME << "] " << __func__ << " instr_id: " << instr_id << " invalid set: " << set << " way: " << way;
            cout << hex << " address: " << (full_addr>>LOG2_BLOCK_SIZE) << " victim address: " << block[set][way].address << " data: " << block[set][way].data;
            cout << dec << " lru: " << block[set][way].lru << endl; });

            break;
        }
    }

    // set is full, pick LRU victim
    if (way == NUM_WAY) {
        for (way=0; way<NUM_WAY; way++) {
            if (block[set][way].lru == NUM_WAY-1) {

                DP ( if (warmup_complete[cpu]) {
                cout << "[" << NAME << "] " << __func__ << " instr_id: " << instr_id << " replace set: " << set << " way: " << way;
                cout << hex << " address: " << (full_addr>>LOG2_BLOCK_SIZE) << " victim address: " << block[set][way].address << " data: " << block[set][way].data;
                cout << dec << " lru: " << block[set][way].lru << endl; });

                break;
            }
        }
    }

    if (way == NUM_WAY) {
        cerr << "[" << NAME << "] " << __func__ << " no victim! set: " << set << endl;
        assert(0);
    }

    return way;
}

void MEMORY_MANAGER::fill_cache(uint32_t set, uint32_t way, PACKET *packet)
{
    //set the valid bit if it was 0
    if (block[set][way].valid == 0) {
        block[set][way].valid = 1;
    }

    //currently don't enable prefetch from NVM to DRAM, maybe useful

    //still need dirty bit in case: a block is evicted back to NVM due to LRU 
    block[set][way].dirty = 0;
    block[set][way].used = 0;

    block[set][way].delta = packet->delta;
    block[set][way].depth = packet->depth;
    block[set][way].signature = packet->signature;
    block[set][way].confidence = packet->confidence;

    block[set][way].tag = packet->hm_addr;
    block[set][way].address = packet->address;
    block[set][way].full_addr = packet->full_addr;
    block[set][way].data = packet->data;
    block[set][way].ip = packet->ip;
    block[set][way].cpu = packet->cpu;
    block[set][way].instr_id = packet->instr_id;

    block[set][way].hm_addr = packet->hm_addr;
}

void MEMORY_MANAGER::lru_update(uint32_t set, uint32_t way)
{
    // update lru replacement state
    for (uint32_t i=0; i<NUM_WAY; i++) {
        if (block[set][i].lru < block[set][way].lru) {
            block[set][i].lru++;
        }
    }
    block[set][way].lru = 0; // promote to the MRU position
}

void MEMORY_MANAGER::set_cached_block(uint64_t address) //input is a hm_addr
{
    uint64_t hm_pn = (address >> LOG2_PAGE_SIZE) & ((1 << LOG2_DRAM_PAGES)-1);
    uint8_t bit_number = get_bit_number(address);
    block_counter[hm_pn].set(bit_number);
}

void MEMORY_MANAGER::reset_cached_block(uint64_t address)  //erase the cached_block record
{
    uint64_t hm_pn = (address >> LOG2_PAGE_SIZE) & ((1 << LOG2_DRAM_PAGES)-1);
    uint8_t bit_number = get_bit_number(address);
    block_counter[hm_pn].reset(bit_number);
}

uint8_t MEMORY_MANAGER::get_bit_number(uint64_t address)
{
    return (uint8_t) ((address & (((1 << 5) - 1) << 7)) >> 7);
}

uint8_t MEMORY_MANAGER::get_cached_number(uint64_t address)
{
    uint64_t hm_pn = (address >> LOG2_PAGE_SIZE) & ((1 << LOG2_DRAM_PAGES)-1);
    
    return (uint8_t) block_counter[hm_pn].count();
}