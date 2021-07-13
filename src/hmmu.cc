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

void MEMORY_MANAGER::handle_dma() // state machine for dma data coherency
{
    //fast memory cache cases
    if (inflight_blocks.size()) {//have some FMC issue to process
        std::map<uint64_t, std::bitset<4>>::iterator it1, it2;
        it1 = inflight_blocks.find(cached_block);
        it2 = inflight_blocks.find(incoming_block);
        if (fmc_type == FMC_WRITEBACK) {
            if (it1->second == 0 && it2->second == 0) {// 0000, now doing: send read requests
                lower_level->add_rq(&cached_packet);
                extra_interface->add_rq(&incoming_packet);
                it1->second.set(3);
                it2->second.set(3);
            }
            if (it1->second == 8 && it2->second == 8) {// 1000, now doing: wait read completion
                if (cached_block_read && incoming_block_read) { //both blocks are read, set the flag
                    it1->second.set(2);
                    it2->second.set(2);
                }
            }
            if (it1->second == 12 && it2->second == 12) {// 1100, now doing: send write requests
                extra_interface->add_wq(&cached_packet);
                lower_level->add_wq(&incoming_packet);
                it1->second.set(1);
                it2->second.set(1);
            }
            if (it1->second == 14 && it2->second == 14) {// 1110, now doing: wait write completion
                if (cached_block_write && incoming_block_write) { //both blocks are written, set the flag
                    it1->second.set(0);
                    it2->second.set(0);
                }
            }
            if (it1->second == 15 && it2->second == 15) {// 1111, block eviction handled! now doing: reset all the states, variables, etc., and update hmmu info
                inflight_blocks.clear();
                cached_block = 0;
                incoming_block = 0;
                cached_block_list.pop_front();
                incoming_block_list.pop_front();
                cached_packet_list.pop_front();
                incoming_packet_list.pop_front();
                cached_block_read = 0;
                incoming_block_read = 0;
                cached_block_write = 0;
                incoming_block_write = 0;
                fmc_type = 0;
                fmc_type_list.pop_front();

                reset_cached_block(cached_packet.hm_addr);
                fill_cache(fmc_set_list.front(), fmc_way_list.front(), &incoming_packet);
                lru_update(fmc_set_list.front(), fmc_way_list.front());
                fmc_set_list.pop_front();
                fmc_way_list.pop_front();
                set_cached_block(incoming_packet.hm_addr);
            }
        }
        else if (fmc_type == FMC_OVERWRITE){
            if (it1->second == 0 && it2->second == 0) {// 0000, now doing: send read requests
                //lower_level->add_rq(&cached_packet);  //now don't care the cached block
                extra_interface->add_rq(&incoming_packet);
                cached_block_read = 1; //manually set this variable
                it1->second.set(3);
                it2->second.set(3);
            }
            if (it1->second == 8 && it2->second == 8) {// 1000, now doing: wait read completion
                if (cached_block_read && incoming_block_read) { //both blocks are read, set the flag
                    it1->second.set(2);
                    it2->second.set(2);
                }
            }
            if (it1->second == 12 && it2->second == 12) {// 1100, now doing: send write requests
                //extra_interface->add_wq(&cached_packet);
                lower_level->add_wq(&incoming_packet);
                cached_block_write = 1; //manually set this variable
                it1->second.set(1);
                it2->second.set(1);
            }
            if (it1->second == 14 && it2->second == 14) {// 1110, now doing: wait write completion
                if (cached_block_write && incoming_block_write) { //both blocks are written, set the flag
                    it1->second.set(0);
                    it2->second.set(0);
                }
            }
            if (it1->second == 15 && it2->second == 15) {// 1111, block eviction handled! now doing: reset all the states, variables, etc., and update hmmu info
                inflight_blocks.clear();
                cached_block = 0;
                incoming_block = 0;
                cached_block_list.pop_front();
                incoming_block_list.pop_front();
                cached_packet_list.pop_front();
                incoming_packet_list.pop_front();
                cached_block_read = 0;
                incoming_block_read = 0;
                cached_block_write = 0;
                incoming_block_write = 0;
                fmc_type = 0;
                fmc_type_list.pop_front();

                reset_cached_block(cached_packet.hm_addr);
                fill_cache(fmc_set_list.front(), fmc_way_list.front(), &incoming_packet);
                lru_update(fmc_set_list.front(), fmc_way_list.front());
                fmc_set_list.pop_front();
                fmc_way_list.pop_front();
                set_cached_block(incoming_packet.hm_addr);
            }
        }
        else if (fmc_type == FMC_WRITE){ //no block eviction in fact, but I generalize this case as 'evicting a empty block'
            if (it1->second == 0 && it2->second == 0) {// 0000, now doing: send read requests
                //lower_level->add_rq(&cached_packet);  //now don't care the cached block
                extra_interface->add_rq(&incoming_packet);
                cached_block_read = 1; //manually set this variable
                it1->second.set(3);
                it2->second.set(3);
            }
            if (it1->second == 8 && it2->second == 8) {// 1000, now doing: wait read completion
                if (cached_block_read && incoming_block_read) { //both blocks are read, set the flag
                    it1->second.set(2);
                    it2->second.set(2);
                }
            }
            if (it1->second == 12 && it2->second == 12) {// 1100, now doing: send write requests
                //extra_interface->add_wq(&cached_packet);
                lower_level->add_wq(&incoming_packet);
                cached_block_write = 1; //manually set this variable
                it1->second.set(1);
                it2->second.set(1);
            }
            if (it1->second == 14 && it2->second == 14) {// 1110, now doing: wait write completion
                if (cached_block_write && incoming_block_write) { //both blocks are written, set the flag
                    it1->second.set(0);
                    it2->second.set(0);
                }
            }
            if (it1->second == 15 && it2->second == 15) {// 1111, block eviction handled! now doing: reset all the states, variables, etc., and update hmmu info
                inflight_blocks.clear();
                cached_block = 0;
                incoming_block = 0;
                cached_block_list.pop_front();
                incoming_block_list.pop_front();
                cached_packet_list.pop_front();
                incoming_packet_list.pop_front();
                cached_block_read = 0;
                incoming_block_read = 0;
                cached_block_write = 0;
                incoming_block_write = 0;
                fmc_type = 0;
                fmc_type_list.pop_front();

                //reset_cached_block(cached_packet.hm_addr); //in fact, no block is evicted when filling an invalid cache line
                fill_cache(fmc_set_list.front(), fmc_way_list.front(), &incoming_packet);
                lru_update(fmc_set_list.front(), fmc_way_list.front());
                fmc_set_list.pop_front();
                fmc_way_list.pop_front();
                set_cached_block(incoming_packet.hm_addr);
            }
        }
    }
    // DRAM-NVM swap case
    if (incoming_page_progress.size()) {
        uint8_t block_it;
        if (incoming_page_progress[0] == 0) { //all entries are modified in the same way, just need to check one entry. 0000, now doing: send read requests
            for (block_it = 0; block_it<(PAGE_SIZE/FMC_BLOCK_SIZE); block_it++) {
                PACKET temp = nvm_packet_list.front();
                temp.address = page_plus_block(incoming_page, block_it); //now temp is a block of the NVM page
                //if this block is cached, read it directly from the dram cache area to reduce latency & deal potential dirty blocks
                //but the cached blocks of this page are allowed to be cached before the swap completes
                //NOTE: previously I used hm_addr as the tag as well for simple simulation code,
                //      but if I want to enable this check, I must calculate the tag bits correctly, since the packet is built from scratch and I don't have information about offset bits!
                temp.hm_addr = page_plus_block(ppage_to_hmpage_map[incoming_page], block_it);
                if (check_hit(&temp) == -1) { //this block is not cached, read from NVM
                    extra_interface->add_rq(&temp);
                }
                else { //this block is cached, read from DRAM
                    lower_level->add_rq(&temp);
                }
                incoming_page_progress[block_it].set(3);
            }
            for (block_it = 0; block_it<(PAGE_SIZE/FMC_BLOCK_SIZE); block_it++) {
                PACKET temp = dram_packet_list.front();
                temp.address = page_plus_block(victim_page, block_it); //now temp is a block of the DRAM page
                lower_level->add_rq(&temp);
                victim_page_progress[block_it].set(3);
            }
        }
        if (incoming_page_progress[0] == 8) { // 1000, now doing: wait read completion
            if (incoming_page_block_read.all() && victim_page_block_read.all()) {
                for (block_it = 0; block_it<(PAGE_SIZE/FMC_BLOCK_SIZE); block_it++) {
                    incoming_page_progress[block_it].set(2);  //set the bit in incoming_page_progress
                    victim_page_progress[block_it].set(2);  //set the bit in victim_page_progress
                }
            }
        }
        if (incoming_page_progress[0] == 12) { // 1100, now doing: send write requests
            for (block_it = 0; block_it<(PAGE_SIZE/FMC_BLOCK_SIZE); block_it++) {
                PACKET temp = nvm_packet_list.front();
                temp.address = page_plus_block(incoming_page, block_it); //now temp is a block of the NVM page
                lower_level->add_wq(&temp);
                incoming_page_progress[block_it].set(1);
            }
            for (block_it = 0; block_it<(PAGE_SIZE/FMC_BLOCK_SIZE); block_it++) {
                PACKET temp = dram_packet_list.front();
                temp.address = page_plus_block(victim_page, block_it); //now temp is a block of the DRAM page
                extra_interface->add_wq(&temp);
                victim_page_progress[block_it].set(1);
            }
        }
        if (incoming_page_progress[0] == 14) { // 1110, now doing: wait write completion
            if (incoming_page_block_write.all() && victim_page_block_write.all()) {
                for (block_it = 0; block_it<(PAGE_SIZE/FMC_BLOCK_SIZE); block_it++) {
                    incoming_page_progress[block_it].set(0);  //set the bit in incoming_page_progress
                    victim_page_progress[block_it].set(0);  //set the bit in victim_page_progress
                }
            }
        }
        if (incoming_page_progress[0] == 15) { // 1111, page swap handled! now doing: reset all the states, variables, etc., and update hmmu info
            incoming_page_progress.clear();
            victim_page_progress.clear();

            //invalidate all the blocks from that page, this prevents duplicate data in DRAM and fast memory cache
            for (uint32_t set=0; set<NUM_SET; set++) {
                for (int way=0; way<NUM_WAY; way++) {
                    uint64_t block_hm_addr = block[set][way].hm_addr;
                    uint64_t block_page = (block_hm_addr >> LOG2_PAGE_SIZE) & ((1 << LOG2_DRAM_PAGES)-1);
                    if (block[set][way].valid && (block_page == ppage_to_hmpage_map[incoming_page])) {
                        block[set][way].valid = 0;
                        //writeback is unnecessary here, since a block is read from fast memory cache for the most updated version, rather than from NVM, if it is cached
                    }
                }
            }

            block_counter[ppage_to_hmpage_map[incoming_page]].reset(); //reset the block caching stats of the NVM page
            block_counter[ppage_to_hmpage_map[victim_page]].reset(); //reset the block revisit stats of the DRAM page
            page_swap(incoming_page, victim_page); //update the mapping in the internal page table

            incoming_page = 0;
            victim_page = 0;
            incoming_page_list.pop_front();
            victim_page_list.pop_front();
            nvm_packet_list.pop_front();
            dram_packet_list.pop_front();
            incoming_page_block_read.reset(); 
	        victim_page_block_read.reset();
	        incoming_page_block_write.reset(); 
	        victim_page_block_write.reset();
        }
    }
}

void MEMORY_MANAGER::handle_write() //distribute write requests, writes can make dram cache dirty
{
    uint32_t index = WQ.head;
    uint8_t w_forwarded = 1;

    if ((WQ.head == WQ.tail) && WQ.occupancy == 0) {//no request in flight, no action
        
    }
    else{
        // update the hm_addr at first, no difference in most of time, but the corner case may happens:
        //      the request is stalled last cycle due to full DRAM/NVM WQ/RQ, but during that period the dma changed the internal pagetable, rendering a new hm_addr
        WQ.entry[index].hm_addr = get_hm_addr(WQ.entry[index].address);
        if (get_memtype(WQ.entry[index].hm_addr) < 1) { //mapped to DRAM
            if (lower_level->get_occupancy(2, WQ.entry[index].address) == lower_level->get_size(2, WQ.entry[index].address)) {

                // DRAM WQ is full, cannot send this write request
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
                set_cached_block(WQ.entry[index].hm_addr); //update the revisit stats
                update_recent_pages(WQ.entry[index].hm_addr);  //update recently used pages
                WQ.remove_queue(&WQ.entry[index]);
                memory_accessed = 1;
            }
        }
	    else { //mapped to NVM

            int way = check_hit(&WQ.entry[index]);
            if (way == -1) {//this block is not in the cache
                
                if (incoming_page_progress.size() && (get_ppn(WQ.entry[index].address) == incoming_page) && incoming_page_block_write[get_bit_number(WQ.entry[index].address)]){
                    //IT IS ACCESSING AN INFLIGHT PAGE AND LUCKILY THIS SUBPAGE BLOCK IS SWAPPED, DRAM latency is used
                    if (lower_level->get_occupancy(2, WQ.entry[index].address) == lower_level->get_size(2, WQ.entry[index].address)) {

                        // DRAM WQ is full, cannot send this write request
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
                        //now the page is still in-flight, the page table is not updated yet, do not modify revisit stats or recent page tracking
                        WQ.remove_queue(&WQ.entry[index]);
                        memory_accessed = 1;
                    }    
                }
                else {//this block is neither cached nor already transmitted in a current swap, NVM latency is the final choice
                    uint8_t not_inflight_yet = 1;
                    std::deque<uint64_t>::iterator it_page, it_block;
                    for (it_page=incoming_page_list.begin(); it_page!=incoming_page_list.end(); ++it_page){
                        if (get_ppn(WQ.entry[index].address) == *it_page) {
                            break;
                        }
                    }
                    if (it_page != incoming_page_list.end()) { //this page is inflight
                        not_inflight_yet = 0;
                    }
                    for (it_block=incoming_block_list.begin(); it_block!=incoming_block_list.end(); ++it_block){
                        if (WQ.entry[index].address == *it_block) {
                            break;
                        }
                    }
                    if (it_block != incoming_block_list.end()) { //this block address is inflight
                        not_inflight_yet = 0;
                    }

                    if (not_inflight_yet) {//take no action if the current address is already in FIFO waitlist
                    //first check if it satisfies triggering a whole page swap
                    uint8_t cached_number = get_cached_number(WQ.entry[index].hm_addr);
                    if (cached_number >= threshold) { // will do a whole page swap, and thus cancel the following block processing
                        incoming_page_list.push_back(get_ppn(WQ.entry[index].address)); //push the NVM page number into the FIFO queue
                        victim_page_list.push_back(search_fast_page()); //push the DRAM page number into the FIFO queue
                        if(inflight_blocks.size() == 0 && incoming_page_progress.size() == 0){ //dma is available, process the oldest page swap pair in the front entry
                            incoming_page = *incoming_page_list.begin();
                            victim_page = *victim_page_list.begin();
                            for(uint8_t i=0; i<(PAGE_SIZE/FMC_BLOCK_SIZE); i++) {
			                    incoming_page_progress[i] = 0;
                                victim_page_progress[i] = 0;
		                    }
                            PACKET temp;
                            //temp.address is waiting for modification in handle_dma, here the main goal is to store the event_cycle
                            temp.event_cycle = WQ.entry[index].event_cycle;
                            temp.is_dma = 1; //this is a dma operation
                            nvm_packet_list.push_back(temp);
                            dram_packet_list.push_back(temp);
                        }
                    }
                    else {//not enough cached blocks for the whole page swap, proceed to the block caching}

                        uint32_t fill_set = get_set(WQ.entry[index].hm_addr);
                        int fill_way = find_lru_victim(WQ.entry[index].hm_addr);

                        if (block[fill_set][fill_way].valid){//eviction happens, so in handle_dma(), need clear that block's caching record in the block_counter
                            if (block[fill_set][fill_way].dirty){ // eviction of a dirty block, referred to as FMC_WRITEBACK
                                if (inflight_blocks.size() || incoming_page_progress.size()) {//some blocks or pages are being tracked, just push this pair into the FIFO queues for future processing
                                    cached_block_list.push_back(block[fill_set][fill_way].address);
                                    incoming_block_list.push_back(WQ.entry[index].address);

                                    fmc_set_list.push_back(fill_set);
                                    fmc_way_list.push_back(fill_way);

                                    PACKET temp;
                                    temp.cpu = block[fill_set][fill_way].cpu;
                                    temp.address = block[fill_set][fill_way].address;
                                    temp.full_addr = block[fill_set][fill_way].full_addr;
                                    temp.data = block[fill_set][fill_way].data;
                                    temp.instr_id = block[fill_set][fill_way].instr_id;
                                    temp.ip = 0; // writeback does not have ip
                                    temp.type = WRITEBACK;
                                    temp.event_cycle = WQ.entry[index].event_cycle;
                                    temp.is_dma = 1; //this is a dma operation
                                    temp.hm_addr = block[fill_set][fill_way].hm_addr;
                                    cached_packet_list.push_back(temp);
                                    WQ.entry[index].is_dma = 1;
                                    incoming_packet_list.push_back(WQ.entry[index]);

                                    uint8_t ft = FMC_WRITEBACK;
                                    fmc_type_list.push_back(ft);
                                }
                                else {//nothing in flight, push to the FIFO queues, and then process the oldest eviction
                                    cached_block_list.push_back(block[fill_set][fill_way].address);
                                    incoming_block_list.push_back(WQ.entry[index].address);

                                    fmc_set_list.push_back(fill_set);
                                    fmc_way_list.push_back(fill_way);

                                    PACKET temp;
                                    temp.cpu = block[fill_set][fill_way].cpu;
                                    temp.address = block[fill_set][fill_way].address;
                                    temp.full_addr = block[fill_set][fill_way].full_addr;
                                    temp.data = block[fill_set][fill_way].data;
                                    temp.instr_id = block[fill_set][fill_way].instr_id;
                                    temp.ip = 0; // writeback does not have ip
                                    temp.type = WRITEBACK;
                                    temp.event_cycle = WQ.entry[index].event_cycle;
                                    temp.is_dma = 1; //this is a dma operation
                                    temp.hm_addr = block[fill_set][fill_way].hm_addr;
                                    cached_packet_list.push_back(temp);
                                    WQ.entry[index].is_dma = 1;
                                    incoming_packet_list.push_back(WQ.entry[index]);

                                    uint8_t ft = FMC_WRITEBACK;
                                    fmc_type_list.push_back(ft);
                                    // retrieve the information for the oldest eviction
                                    cached_block = *cached_block_list.begin();
                                    incoming_block = *incoming_block_list.begin();
                                    cached_packet = *cached_packet_list.begin();
                                    incoming_packet = *incoming_packet_list.begin();
                                    /*inflight_blocks.insert(std::pair<uint64_t, std::bitset<4>>(cached_block, std::string("0000"))); //this intialize the state machine to track the inflight blocks
                                    inflight_blocks.insert(std::pair<uint64_t, std::bitset<4>>(incoming_block, std::string("0000")));*/
                                    inflight_blocks[cached_block] = 0;
                                    inflight_blocks[incoming_block] = 0;
                                    fmc_type = *fmc_type_list.begin();
                                }
                            }
                            else { // eviction of a clean block, referred to as FMC_OVERWRITE
                                if (inflight_blocks.size() || incoming_page_progress.size()) {//some blocks or pages are being tracked, just push this pair into the FIFO queues for future processing
                                    cached_block_list.push_back(block[fill_set][fill_way].address); //dummy packet JUST FOR CODE UNIFORMITY, only use incoming block(from NVM) to overwrite the cached one
                                    incoming_block_list.push_back(WQ.entry[index].address);

                                    fmc_set_list.push_back(fill_set);
                                    fmc_way_list.push_back(fill_way);

                                    PACKET temp;
                                    temp.cpu = block[fill_set][fill_way].cpu;
                                    temp.address = block[fill_set][fill_way].address;
                                    temp.full_addr = block[fill_set][fill_way].full_addr;
                                    temp.data = block[fill_set][fill_way].data;
                                    temp.instr_id = block[fill_set][fill_way].instr_id;
                                    temp.ip = 0; // writeback does not have ip
                                    temp.type = WRITEBACK;
                                    temp.event_cycle = WQ.entry[index].event_cycle;
                                    temp.is_dma = 1; //this is a dma operation
                                    temp.hm_addr = block[fill_set][fill_way].hm_addr;
                                    cached_packet_list.push_back(temp);
                                    WQ.entry[index].is_dma = 1;
                                    incoming_packet_list.push_back(WQ.entry[index]);

                                    uint8_t ft = FMC_OVERWRITE;
                                    fmc_type_list.push_back(ft);
                                }
                                else {//nothing in flight, push to the FIFO queues, and then process the oldest eviction
                                    cached_block_list.push_back(block[fill_set][fill_way].address);
                                    incoming_block_list.push_back(WQ.entry[index].address);

                                    fmc_set_list.push_back(fill_set);
                                    fmc_way_list.push_back(fill_way);

                                    PACKET temp;
                                    temp.cpu = block[fill_set][fill_way].cpu;
                                    temp.address = block[fill_set][fill_way].address;
                                    temp.full_addr = block[fill_set][fill_way].full_addr;
                                    temp.data = block[fill_set][fill_way].data;
                                    temp.instr_id = block[fill_set][fill_way].instr_id;
                                    temp.ip = 0; // writeback does not have ip
                                    temp.type = WRITEBACK;
                                    temp.event_cycle = WQ.entry[index].event_cycle;
                                    temp.is_dma = 1; //this is a dma operation
                                    temp.hm_addr = block[fill_set][fill_way].hm_addr;
                                    cached_packet_list.push_back(temp);
                                    WQ.entry[index].is_dma = 1;
                                    incoming_packet_list.push_back(WQ.entry[index]);

                                    uint8_t ft = FMC_OVERWRITE;
                                    fmc_type_list.push_back(ft);
                                    // retrieve the information for the oldest eviction
                                    cached_block = *cached_block_list.begin();
                                    incoming_block = *incoming_block_list.begin();
                                    cached_packet = *cached_packet_list.begin();
                                    incoming_packet = *incoming_packet_list.begin();
                                    /*inflight_blocks.insert(std::pair<uint64_t, std::bitset<4>>(cached_block, std::string("0000"))); //this intialize the state machine to track the inflight blocks
                                    inflight_blocks.insert(std::pair<uint64_t, std::bitset<4>>(incoming_block, std::string("0000")));*/
                                    inflight_blocks[cached_block] = 0;
                                    inflight_blocks[incoming_block] = 0;
                                    fmc_type = *fmc_type_list.begin();
                                }
                            }
                        }
                        else { //no eviction, no caching record needs to be erased in handle_dma(); referred to as FMC_WRITE
                            if (inflight_blocks.size() || incoming_page_progress.size()) {//some blocks or pages are being tracked, just push this pair into the FIFO queues for future processing
                                cached_block_list.push_back(block[fill_set][fill_way].address); //dummy packet JUST FOR CODE UNIFORMITY, only use incoming block(from NVM) to fill the cache line
                                incoming_block_list.push_back(WQ.entry[index].address);  // ONLY FOR CODE UNIFORMITY

                                fmc_set_list.push_back(fill_set);
                                fmc_way_list.push_back(fill_way);

                                PACKET temp;
                                //temp.cpu = block[fill_set][fill_way].cpu;
                                //temp.address = block[fill_set][fill_way].address;
                                //temp.full_addr = block[fill_set][fill_way].full_addr;
                                //temp.data = block[fill_set][fill_way].data;
                                //temp.instr_id = block[fill_set][fill_way].instr_id;
                                //temp.ip = 0; // writeback does not have ip
                                //temp.type = WRITEBACK;
                                temp.event_cycle = WQ.entry[index].event_cycle;
                                temp.is_dma = 1; //this is a dma operation
                                //temp.hm_addr = block[fill_set][fill_way].hm_addr;
                                cached_packet_list.push_back(temp);
                                WQ.entry[index].is_dma = 1;
                                incoming_packet_list.push_back(WQ.entry[index]);

                                uint8_t ft = FMC_WRITE;
                                fmc_type_list.push_back(ft);
                            }
                            else {//nothing in flight, push to the FIFO queues, and then process the oldest eviction
                                cached_block_list.push_back(block[fill_set][fill_way].address);
                                incoming_block_list.push_back(WQ.entry[index].address);

                                fmc_set_list.push_back(fill_set);
                                fmc_way_list.push_back(fill_way);

                                PACKET temp;
                                //temp.cpu = block[fill_set][fill_way].cpu;
                                //temp.address = block[fill_set][fill_way].address;
                                //temp.full_addr = block[fill_set][fill_way].full_addr;
                                //temp.data = block[fill_set][fill_way].data;
                                //temp.instr_id = block[fill_set][fill_way].instr_id;
                                //temp.ip = 0; // writeback does not have ip
                                //temp.type = WRITEBACK;
                                temp.event_cycle = WQ.entry[index].event_cycle;
                                temp.is_dma = 1; //this is a dma operation
                                //temp.hm_addr = block[fill_set][fill_way].hm_addr;
                                cached_packet_list.push_back(temp);
                                WQ.entry[index].is_dma = 1;
                                incoming_packet_list.push_back(WQ.entry[index]);

                                uint8_t ft = FMC_WRITE;
                                fmc_type_list.push_back(ft);
                                // retrieve the information for the oldest eviction
                                cached_block = *cached_block_list.begin();
                                incoming_block = *incoming_block_list.begin();
                                cached_packet = *cached_packet_list.begin();
                                incoming_packet = *incoming_packet_list.begin();
                                /*inflight_blocks.insert(std::pair<uint64_t, std::bitset<4>>(cached_block, std::string("0000"))); //this intialize the state machine to track the inflight blocks
                                inflight_blocks.insert(std::pair<uint64_t, std::bitset<4>>(incoming_block, std::string("0000")));*/
                                inflight_blocks[cached_block] = 0;
                                inflight_blocks[incoming_block] = 0;
                                fmc_type = *fmc_type_list.begin();
                            }
                        }
                    }
                    }

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
                        memory_accessed = 1;
                    }
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
                    uint32_t set = get_set(WQ.entry[index].hm_addr);
                    block[set][way].dirty = 1; //the block is modified
                    WQ.remove_queue(&WQ.entry[index]);
                    memory_accessed = 1;
                }
            }
        }
    }
}

void MEMORY_MANAGER::handle_read() //distribute read requests, reads can update lru states of DRAM cache
{
    uint32_t index = RQ.head;
    uint8_t r_forwarded = 1;

    if ((RQ.head == RQ.tail) && RQ.occupancy == 0) { //no requests queued, no action
        
    }
    else{
        //update the hm_addr at first
        RQ.entry[index].hm_addr = get_hm_addr(RQ.entry[index].address);
        if (get_memtype(RQ.entry[index].hm_addr) < 1) { //mapped to DRAM
            if (lower_level->get_occupancy(1, RQ.entry[index].address) == lower_level->get_size(1, RQ.entry[index].address)){
                //DRAM RQ is full, cannot send this read request
                r_forwarded = 0;
	            STALL[RQ.entry[index].type]++;
            }
            else{
	            lower_level->add_rq(&RQ.entry[index]);
            }
            if (r_forwarded) {
                set_cached_block(RQ.entry[index].hm_addr); // update the revisit stats
                update_recent_pages(RQ.entry[index].hm_addr); //update recently used pages
                RQ.remove_queue(&RQ.entry[index]);
                memory_accessed = 1;
            }
        }
        else { //mapped to NVM
            int way = check_hit(&RQ.entry[index]);
            //uint32_t fill_set = get_set(RQ.entry[index].hm_addr);
            if (way == -1) {//this block is not in the cache

                if (incoming_page_progress.size() && (get_ppn(RQ.entry[index].address) == incoming_page) && incoming_page_block_write[get_bit_number(RQ.entry[index].address)]){
                    //IT IS ACCESSING AN INFLIGHT PAGE AND LUCKILY THIS SUBPAGE BLOCK IS SWAPPED, DRAM latency is used
                    if (lower_level->get_occupancy(1, RQ.entry[index].address) == lower_level->get_size(1, RQ.entry[index].address)) {

                        // DRAM RQ is full, cannot send this read request
                        r_forwarded = 0;
                        STALL[RQ.entry[index].type]++;
                    }
                    else {
                        lower_level->add_rq(&RQ.entry[index]);
                    }
                    if (r_forwarded) {
                        //now the page is still in-flight, the page table is not updated yet, do not modify revisit stats or recent page tracking
                        RQ.remove_queue(&RQ.entry[index]);
                        memory_accessed = 1;
                    }    
                }
                else {//this block is neither cached nor already transmitted in a current swap, NVM latency is the final choice
                    uint8_t not_inflight_yet = 1;
                    std::deque<uint64_t>::iterator it_page, it_block;
                    for (it_page=incoming_page_list.begin(); it_page!=incoming_page_list.end(); ++it_page){
                        if (get_ppn(RQ.entry[index].address) == *it_page) {
                            break;
                        }
                    }
                    if (it_page != incoming_page_list.end()) { //this page is inflight
                        not_inflight_yet = 0;
                    }
                    for (it_block=incoming_block_list.begin(); it_block!=incoming_block_list.end(); ++it_block){
                        if (RQ.entry[index].address == *it_block) {
                            break;
                        }
                    }
                    if (it_block != incoming_block_list.end()) { //this block address is inflight
                        not_inflight_yet = 0;
                    }

                    if (not_inflight_yet) {//take no action if the current address is already in FIFO waitlist
                        //first check if it satisfies triggering a whole page swap
                        uint8_t cached_number = get_cached_number(RQ.entry[index].hm_addr);
                        if (cached_number >= threshold) { // will do a whole page swap, and thus cancel the following block processing
                            incoming_page_list.push_back(get_ppn(RQ.entry[index].address)); //push the NVM page number into the FIFO queue
                            victim_page_list.push_back(search_fast_page()); //push the DRAM page number into the FIFO queue
                            if(inflight_blocks.size() == 0 && incoming_page_progress.size() == 0){ //dma is available, process the oldest page swap pair in the front entry
                                incoming_page = *incoming_page_list.begin();
                                victim_page = *victim_page_list.begin();
                                for(uint8_t i=0; i<(PAGE_SIZE/FMC_BLOCK_SIZE); i++) {
	    		                    incoming_page_progress[i] = 0;
                                    victim_page_progress[i] = 0;
		                        }
                                PACKET temp;
                                //temp.address is waiting for modification in handle_dma, here the main goal is to store the event_cycle
                                temp.event_cycle = RQ.entry[index].event_cycle;
                                temp.is_dma = 1; //this is a dma operation
                                nvm_packet_list.push_back(temp);
                                dram_packet_list.push_back(temp);
                            }
                        }
                        else {//not enough cached blocks for the whole page swap, proceed to the block caching}

                            uint32_t fill_set = get_set(RQ.entry[index].hm_addr);
                            int fill_way = find_lru_victim(RQ.entry[index].hm_addr);

                            if (block[fill_set][fill_way].valid){//eviction happens, so in handle_dma(), need clear that block's caching record in the block_counter
                                if (block[fill_set][fill_way].dirty){ // eviction of a dirty block, referred to as FMC_WRITEBACK
                                    if (inflight_blocks.size() || incoming_page_progress.size()) {//some blocks or pages are being tracked, just push this pair into the FIFO queues for future processing
                                        cached_block_list.push_back(block[fill_set][fill_way].address);
                                        incoming_block_list.push_back(RQ.entry[index].address);

                                        fmc_set_list.push_back(fill_set);
                                        fmc_way_list.push_back(fill_way);

                                        PACKET temp;
                                        temp.cpu = block[fill_set][fill_way].cpu;
                                        temp.address = block[fill_set][fill_way].address;
                                        temp.full_addr = block[fill_set][fill_way].full_addr;
                                        temp.data = block[fill_set][fill_way].data;
                                        temp.instr_id = block[fill_set][fill_way].instr_id;
                                        temp.ip = 0; // writeback does not have ip
                                        temp.type = LOAD;
                                        temp.event_cycle = RQ.entry[index].event_cycle;
                                        temp.is_dma = 1; //this is a dma operation
                                        temp.hm_addr = block[fill_set][fill_way].hm_addr;
                                        cached_packet_list.push_back(temp);
                                        RQ.entry[index].is_dma = 1;
                                        incoming_packet_list.push_back(RQ.entry[index]);

                                        uint8_t ft = FMC_WRITEBACK;
                                        fmc_type_list.push_back(ft);
                                    }
                                    else {//nothing in flight, push to the FIFO queues, and then process the oldest eviction
                                        cached_block_list.push_back(block[fill_set][fill_way].address);
                                        incoming_block_list.push_back(RQ.entry[index].address);

                                        fmc_set_list.push_back(fill_set);
                                        fmc_way_list.push_back(fill_way);

                                        PACKET temp;
                                        temp.cpu = block[fill_set][fill_way].cpu;
                                        temp.address = block[fill_set][fill_way].address;
                                        temp.full_addr = block[fill_set][fill_way].full_addr;
                                        temp.data = block[fill_set][fill_way].data;
                                        temp.instr_id = block[fill_set][fill_way].instr_id;
                                        temp.ip = 0; // writeback does not have ip
                                        temp.type = LOAD;
                                        temp.event_cycle = RQ.entry[index].event_cycle;
                                        temp.is_dma = 1; //this is a dma operation
                                        temp.hm_addr = block[fill_set][fill_way].hm_addr;
                                        cached_packet_list.push_back(temp);
                                        RQ.entry[index].is_dma = 1;
                                        incoming_packet_list.push_back(RQ.entry[index]);

                                        uint8_t ft = FMC_WRITEBACK;
                                        fmc_type_list.push_back(ft);
                                        // retrieve the information for the oldest eviction
                                        cached_block = *cached_block_list.begin();
                                        incoming_block = *incoming_block_list.begin();
                                        cached_packet = *cached_packet_list.begin();
                                        incoming_packet = *incoming_packet_list.begin();
                                        /*inflight_blocks.insert(std::pair<uint64_t, std::bitset<4>>(cached_block, std::string("0000"))); //this intialize the state machine to track the inflight blocks
                                        inflight_blocks.insert(std::pair<uint64_t, std::bitset<4>>(incoming_block, std::string("0000")));*/
                                        inflight_blocks[cached_block] = 0;
                                        inflight_blocks[incoming_block] = 0;
                                        fmc_type = *fmc_type_list.begin();
                                    }
                                }
                                else { // eviction of a clean block, referred to as FMC_OVERWRITE
                                    if (inflight_blocks.size() || incoming_page_progress.size()) {//some blocks or pages are being tracked, just push this pair into the FIFO queues for future processing
                                        cached_block_list.push_back(block[fill_set][fill_way].address); //dummy packet JUST FOR CODE UNIFORMITY, only use incoming block(from NVM) to overwrite the cached one
                                        incoming_block_list.push_back(RQ.entry[index].address);

                                        fmc_set_list.push_back(fill_set);
                                        fmc_way_list.push_back(fill_way);

                                        PACKET temp;
                                        temp.cpu = block[fill_set][fill_way].cpu;
                                        temp.address = block[fill_set][fill_way].address;
                                        temp.full_addr = block[fill_set][fill_way].full_addr;
                                        temp.data = block[fill_set][fill_way].data;
                                        temp.instr_id = block[fill_set][fill_way].instr_id;
                                        temp.ip = 0; // writeback does not have ip
                                        temp.type = LOAD;
                                        temp.event_cycle = RQ.entry[index].event_cycle;
                                        temp.is_dma = 1; //this is a dma operation
                                        temp.hm_addr = block[fill_set][fill_way].hm_addr;
                                        cached_packet_list.push_back(temp);
                                        RQ.entry[index].is_dma = 1;
                                        incoming_packet_list.push_back(RQ.entry[index]);

                                        uint8_t ft = FMC_OVERWRITE;
                                        fmc_type_list.push_back(ft);
                                    }
                                    else {//nothing in flight, push to the FIFO queues, and then process the oldest eviction
                                        cached_block_list.push_back(block[fill_set][fill_way].address);
                                        incoming_block_list.push_back(RQ.entry[index].address);

                                        fmc_set_list.push_back(fill_set);
                                        fmc_way_list.push_back(fill_way);

                                        PACKET temp;
                                        temp.cpu = block[fill_set][fill_way].cpu;
                                        temp.address = block[fill_set][fill_way].address;
                                        temp.full_addr = block[fill_set][fill_way].full_addr;
                                        temp.data = block[fill_set][fill_way].data;
                                        temp.instr_id = block[fill_set][fill_way].instr_id;
                                        temp.ip = 0; // writeback does not have ip
                                        temp.type = LOAD;
                                        temp.event_cycle = RQ.entry[index].event_cycle;
                                        temp.is_dma = 1; //this is a dma operation
                                        temp.hm_addr = block[fill_set][fill_way].hm_addr;
                                        cached_packet_list.push_back(temp);
                                        RQ.entry[index].is_dma = 1;
                                        incoming_packet_list.push_back(RQ.entry[index]);

                                        uint8_t ft = FMC_OVERWRITE;
                                        fmc_type_list.push_back(ft);
                                        // retrieve the information for the oldest eviction
                                        cached_block = *cached_block_list.begin();
                                        incoming_block = *incoming_block_list.begin();
                                        cached_packet = *cached_packet_list.begin();
                                        incoming_packet = *incoming_packet_list.begin();
                                        /*inflight_blocks.insert(std::pair<uint64_t, std::bitset<4>>(cached_block, std::string("0000"))); //this intialize the state machine to track the inflight blocks
                                        inflight_blocks.insert(std::pair<uint64_t, std::bitset<4>>(incoming_block, std::string("0000")));*/
                                        inflight_blocks[cached_block] = 0;
                                        inflight_blocks[incoming_block] = 0;
                                        fmc_type = *fmc_type_list.begin();
                                    }
                                }
                            }
                            else { //no eviction, no caching record needs to be erased in handle_dma(); referred to as FMC_WRITE
                                if (inflight_blocks.size() || incoming_page_progress.size()) {//some blocks or pages are being tracked, just push this pair into the FIFO queues for future processing
                                    cached_block_list.push_back(block[fill_set][fill_way].address); //dummy packet JUST FOR CODE UNIFORMITY, only use incoming block(from NVM) to fill the cache line
                                    incoming_block_list.push_back(RQ.entry[index].address);  // ONLY FOR CODE UNIFORMITY

                                    fmc_set_list.push_back(fill_set);
                                    fmc_way_list.push_back(fill_way);

                                    PACKET temp;
                                    //temp.cpu = block[fill_set][fill_way].cpu;
                                    //temp.address = block[fill_set][fill_way].address;
                                    //temp.full_addr = block[fill_set][fill_way].full_addr;
                                    //temp.data = block[fill_set][fill_way].data;
                                    //temp.instr_id = block[fill_set][fill_way].instr_id;
                                    //temp.ip = 0; // writeback does not have ip
                                    //temp.type = WRITEBACK;
                                    temp.event_cycle = RQ.entry[index].event_cycle;
                                    temp.is_dma = 1; //this is a dma operation
                                    //temp.hm_addr = block[fill_set][fill_way].hm_addr;
                                    cached_packet_list.push_back(temp);
                                    RQ.entry[index].is_dma = 1;
                                    incoming_packet_list.push_back(RQ.entry[index]);

                                    uint8_t ft = FMC_WRITE;
                                    fmc_type_list.push_back(ft);
                                }
                                else {//nothing in flight, push to the FIFO queues, and then process the oldest eviction
                                    cached_block_list.push_back(block[fill_set][fill_way].address);
                                    incoming_block_list.push_back(RQ.entry[index].address);

                                    fmc_set_list.push_back(fill_set);
                                    fmc_way_list.push_back(fill_way);

                                    PACKET temp;
                                    //temp.cpu = block[fill_set][fill_way].cpu;
                                    //temp.address = block[fill_set][fill_way].address;
                                    //temp.full_addr = block[fill_set][fill_way].full_addr;
                                    //temp.data = block[fill_set][fill_way].data;
                                    //temp.instr_id = block[fill_set][fill_way].instr_id;
                                    //temp.ip = 0; // writeback does not have ip
                                    //temp.type = WRITEBACK;
                                    temp.event_cycle = RQ.entry[index].event_cycle;
                                    temp.is_dma = 1; //this is a dma operation
                                    //temp.hm_addr = block[fill_set][fill_way].hm_addr;
                                    cached_packet_list.push_back(temp);
                                    RQ.entry[index].is_dma = 1;
                                    incoming_packet_list.push_back(RQ.entry[index]);

                                    uint8_t ft = FMC_WRITE;
                                    fmc_type_list.push_back(ft);
                                    // retrieve the information for the oldest eviction
                                    cached_block = *cached_block_list.begin();
                                    incoming_block = *incoming_block_list.begin();
                                    cached_packet = *cached_packet_list.begin();
                                    incoming_packet = *incoming_packet_list.begin();
                                    /*inflight_blocks.insert(std::pair<uint64_t, std::bitset<4>>(cached_block, std::string("0000"))); //this intialize the state machine to track the inflight blocks
                                    inflight_blocks.insert(std::pair<uint64_t, std::bitset<4>>(incoming_block, std::string("0000")));*/
                                    inflight_blocks[cached_block] = 0;
                                    inflight_blocks[incoming_block] = 0;
                                    fmc_type = *fmc_type_list.begin();
                                }
                            }
                        }
                    }

                    if (extra_interface->get_occupancy(1, RQ.entry[index].address) == extra_interface->get_size(1, RQ.entry[index].address)) {

                        // NVM RQ is full, cannot send this read request
                        r_forwarded = 0;
                        STALL[RQ.entry[index].type]++;
                    }
                    else {
                        extra_interface->add_rq(&RQ.entry[index]);
                    }
                    if (r_forwarded) {
                        RQ.remove_queue(&RQ.entry[index]);
                        memory_accessed = 1;
                    }
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
                    uint32_t fill_set = get_set(RQ.entry[index].hm_addr);
                    lru_update(fill_set, way); //update LRU states on reads
                    RQ.remove_queue(&RQ.entry[index]);
                    memory_accessed = 1;
                }
            } 
        }
    }
}

void MEMORY_MANAGER::request_dispatch()
{
    handle_write();
    handle_read();
    //handle_dma();
    // if (current_core_cycle[0]%1024 == 0) {
    //     revisited_stats();
    // }
    // //revisited_stats();
    // tune_threshold();
    if (memory_accessed) {
        revisited_stats();
        tune_threshold();
        memory_accessed = 0;
    }
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

uint64_t MEMORY_MANAGER::get_ppn(uint64_t address)
{
    uint64_t physical_pn = (address >> LOG2_PAGE_SIZE) & ((1 << LOG2_DRAM_PAGES)-1);

    return physical_pn;
}

uint64_t MEMORY_MANAGER::get_hybrid_memory_pn(uint64_t address)
{
    uint64_t physical_pn = (address >> LOG2_PAGE_SIZE) & ((1 << LOG2_DRAM_PAGES)-1);
    uint64_t hybrid_memory_pn = ppage_to_hmpage_map[physical_pn];

    return hybrid_memory_pn;
}

uint64_t MEMORY_MANAGER::get_hm_addr(uint64_t address) //input: physical address. output: hybrid memory address
{
    uint64_t physical_pn = (address >> LOG2_PAGE_SIZE) & ((1 << LOG2_DRAM_PAGES)-1);
    uint64_t page_offset = ((1<<LOG2_PAGE_SIZE)-1) & address;
    uint64_t hybrid_memory_pn = ppage_to_hmpage_map[physical_pn];

    return (((hybrid_memory_pn)<<LOG2_PAGE_SIZE) + page_offset);
}

uint64_t MEMORY_MANAGER::page_plus_block(uint64_t pn, uint8_t block)
{
    uint64_t address = (pn << LOG2_PAGE_SIZE) + (block << LOG2_FMC_BLOCK_SIZE);

    return address;
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
    uint32_t is_recent = 1;
    uint64_t candidate_physical_pn = 0;
    uint64_t candidate_hybrid_memory_pn = 0;
    std::deque<uint64_t>::iterator it;

    while(is_slow || is_recent) //while the mapped candidate is a slow memory page
    {
        candidate_physical_pn = hmmu_rand()%(num_ppages);
        candidate_hybrid_memory_pn = ppage_to_hmpage_map[candidate_physical_pn];
        for (it=recently_used_pages.begin(); it!=recently_used_pages.end(); ++it){
            if (candidate_hybrid_memory_pn == *it) { 
                break;
            }
        }
        if (it == recently_used_pages.end()) { //the for loop completes, no match found
            is_recent = 0; //eligible swap candidate, jump out of while loop
        }
        else {
            is_recent = 1; //this is a recent page, retry searching!
        }
        
        //ppage_to_hmpage_map[(hmmu_rand()%(num_ppages))] <=> DRAM_PAGES/NVM_RATIO
        if(candidate_hybrid_memory_pn < DRAM_PAGES/NVM_RATIO) //already find a fast page
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

/* void MEMORY_MANAGER::update_page_table(uint64_t address) //this function is not used anymore
{
    uint64_t physical_pn = (address >> LOG2_PAGE_SIZE) & ((1 << LOG2_DRAM_PAGES)-1);
    uint64_t victim_ppn = search_fast_page();

    //add function here: everytime do a swap, I should clear all the bits in the bitset corresponding to both hybrid pages involved
    //for the swap initiator page: it will go DRAM, now the bits signifies number of cached blocks, clear it so I leave a clean bitset for the coming victim
    //    ---->this is done in the handle read/write function
    //for the victim, it will go NVM, now the bits signifies number of re-visited blocks, clear it so I leave a clean bitset for the coming initiator
    //since the victim page number is non-visible outside, I should clear the victim bitset here
    uint64_t victim_hm_pn = ppage_to_hmpage_map[victim_ppn];
    block_counter[victim_hm_pn].reset();

    page_swap(physical_pn, victim_ppn);
} */

void MEMORY_MANAGER::operate()
{
    request_dispatch();
    handle_dma();
}

int MEMORY_MANAGER::check_hit(PACKET *packet)
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

    for (int way=0; way<NUM_WAY; way++) {
        if (block[set][way].valid && (block[set][way].tag == (packet->hm_addr & ((((1 << FMC_TAG_WIDTH)-1) << LOG2_FMC_SET) << LOG2_FMC_BLOCK_SIZE)))) {
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
    for (int way=0; way<NUM_WAY; way++) {
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

int MEMORY_MANAGER::find_lru_victim(uint64_t addr)
{
    uint32_t set = get_set(addr);
    int way = 0;

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

void MEMORY_MANAGER::fill_cache(uint32_t set, int way, PACKET *packet) //fill_cache() doesn't change lru, need to invoke lru_update() after the insertion. lru was initialized as way number at the beginning
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

    block[set][way].tag = packet->hm_addr & ((((1 << FMC_TAG_WIDTH)-1) << LOG2_FMC_SET) << LOG2_FMC_BLOCK_SIZE);
    block[set][way].address = packet->address;
    block[set][way].full_addr = packet->full_addr;
    block[set][way].data = packet->data;
    block[set][way].ip = packet->ip;
    block[set][way].cpu = packet->cpu;
    block[set][way].instr_id = packet->instr_id;

    block[set][way].hm_addr = packet->hm_addr;
}

void MEMORY_MANAGER::lru_update(uint32_t set, int way) //invoked when: 1. read the DRAM cache ---- in handle_read() 2. insertion to the DRAM cache ---- in handle_dma()
{
    // update lru replacement state
    for (int i=0; i<NUM_WAY; i++) {
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

void MEMORY_MANAGER::revisited_stats()
{
    revisited_count = 0;
    for (std::deque<uint64_t>::iterator it=recently_used_pages.begin(); it!=recently_used_pages.end(); ++it) {
        revisited_count += block_counter[*it].count(); //sum up the number of revisited blocks of recently used pages
    }
    // found that counting total numbers is too time-consuming, better try tracking some recently accessed pages.
}

void MEMORY_MANAGER::update_recent_pages(uint64_t address) //input an accessed address(a hm_addr), do the recency stuff
{
    uint64_t pn = (address >> LOG2_PAGE_SIZE) & ((1 << LOG2_DRAM_PAGES)-1); //extract page number from address
    std::deque<uint64_t>::iterator it;
    //LOGIC: if already tracked, remove it and re-insert it into the back(MRU)
    //else:
    //  if size() not reaching 2048(or upper limit), do only push_back()
    //  else do push_back() and then pop_front() to maintain the size
    for (it=recently_used_pages.begin(); it!=recently_used_pages.end(); ++it){
        if (pn == *it) {
            break;
        }
    }
    if (it == recently_used_pages.end()) { //this page is not being tracked
        if (recently_used_pages.size() < NUM_RECENT_PAGES) { //simply append this one
            recently_used_pages.push_back(pn);
        }
        else {//reach the max tracking amount
            recently_used_pages.pop_front();
            recently_used_pages.push_back(pn);
        }
    }
    else {//this page is being tracked, promote to back (MRU)
        recently_used_pages.erase(it);
        recently_used_pages.push_back(pn);
    }
}

void MEMORY_MANAGER::tune_threshold()
{
    // current configuration:
    // threshold: 2~6
    // average revisited blocks per page triggering tuning: 20
    uint8_t blocks_per_page;
    if (recently_used_pages.size() > 0) {
        blocks_per_page = (uint8_t) revisited_count/recently_used_pages.size();
    }
    uint8_t anchor = 20;
    if (blocks_per_page > anchor) {
        if (threshold > 2) {
            threshold--;
        }
    }
    else {
        if (threshold < 6) {
            threshold++;
        }
    }
}