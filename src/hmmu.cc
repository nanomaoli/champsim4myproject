#include "hmmu.h"
#include "cache.h"

int MEMORY_MANAGER::add_rq(PACKET *packet)
{
    // check for the latest wirtebacks in the write queue
    int wq_index = WQ.check_queue(packet);
/*    if (wq_index != -1) {
        
	    
        // check fill level
        if (packet->fill_level < fill_level) {

            packet->data = WQ.entry[wq_index].data;

	    if(fill_level == FILL_L2)
	      {
		if(packet->fill_l1i)
		  {
		    upper_level_icache[packet->cpu]->return_data(packet);
		  }
		if(packet->fill_l1d)
		  {
		    upper_level_dcache[packet->cpu]->return_data(packet);
		  }
	      }
	    else
	      {
		if (packet->instruction)
		  upper_level_icache[packet->cpu]->return_data(packet);
		if (packet->is_data)
		  upper_level_dcache[packet->cpu]->return_data(packet);
	      }
        } 

#ifdef SANITY_CHECK
        if (cache_type == IS_ITLB)
            assert(0);
        else if (cache_type == IS_DTLB)
            assert(0);
        else if (cache_type == IS_L1I)
            assert(0);
#endif
        // update processed packets
        if ((cache_type == IS_L1D) && (packet->type != PREFETCH)) {
            if (PROCESSED.occupancy < PROCESSED.SIZE)
                PROCESSED.add_queue(packet);

            DP ( if (warmup_complete[packet->cpu]) {
            cout << "[" << NAME << "_RQ] " << __func__ << " instr_id: " << packet->instr_id << " found recent writebacks";
            cout << hex << " read: " << packet->address << " writeback: " << WQ.entry[wq_index].address << dec;
            cout << " index: " << MAX_READ << " rob_signal: " << packet->rob_signal << endl; });
        }

        HIT[packet->type]++;
        ACCESS[packet->type]++;

        WQ.FORWARD++;
        RQ.ACCESS++;

        return -1;
    } */

    // check for duplicates in the read queue
    int index = RQ.check_queue(packet);
    if (index != -1) {
        
        if (packet->instruction) {
            uint32_t rob_index = packet->rob_index;
            RQ.entry[index].rob_index_depend_on_me.insert (rob_index);
            RQ.entry[index].instruction = 1; // add as instruction type
            RQ.entry[index].instr_merged = 1;

            DP (if (warmup_complete[packet->cpu]) {
            cout << "[INSTR_MERGED] " << __func__ << " cpu: " << packet->cpu << " instr_id: " << RQ.entry[index].instr_id;
            cout << " merged rob_index: " << rob_index << " instr_id: " << packet->instr_id << endl; });
        }
        else 
        {
            // mark merged consumer
            if (packet->type == RFO) {

                uint32_t sq_index = packet->sq_index;
                RQ.entry[index].sq_index_depend_on_me.insert (sq_index);
                RQ.entry[index].store_merged = 1;
            }
            else {
                uint32_t lq_index = packet->lq_index; 
                RQ.entry[index].lq_index_depend_on_me.insert (lq_index);
                RQ.entry[index].load_merged = 1;

                DP (if (warmup_complete[packet->cpu]) {
                cout << "[DATA_MERGED] " << __func__ << " cpu: " << packet->cpu << " instr_id: " << RQ.entry[index].instr_id;
                cout << " merged rob_index: " << packet->rob_index << " instr_id: " << packet->instr_id << " lq_index: " << packet->lq_index << endl; });
            }
            RQ.entry[index].is_data = 1; // add as data type
        }

	if((packet->fill_l1i) && (RQ.entry[index].fill_l1i != 1))
	  {
	    RQ.entry[index].fill_l1i = 1;
	  }
	if((packet->fill_l1d) && (RQ.entry[index].fill_l1d != 1))
	  {
	    RQ.entry[index].fill_l1d = 1;
	  }

        RQ.MERGED++;
        RQ.ACCESS++;

        return index; // merged index
    }

    // check occupancy
    if (RQ.occupancy == HMMU_RQ_SIZE) {
        RQ.FULL++;

        return -2; // cannot handle this request
    }

    // if there is no duplicate, add it to RQ
    index = RQ.tail;

#ifdef SANITY_CHECK
    if (RQ.entry[index].address != 0) {
        cerr << "[" << NAME << "_ERROR] " << __func__ << " is not empty index: " << index;
        cerr << " address: " << hex << RQ.entry[index].address;
        cerr << " full_addr: " << RQ.entry[index].full_addr << dec << endl;
        assert(0);
    }
#endif

    RQ.entry[index] = *packet;

    // ADD LATENCY ---- now assume hmmu doesn't have latency
    /* if (RQ.entry[index].event_cycle < current_core_cycle[packet->cpu])
        RQ.entry[index].event_cycle = current_core_cycle[packet->cpu] + LATENCY;
    else
        RQ.entry[index].event_cycle += LATENCY; */

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

    RQ.TO_CACHE++;
    RQ.ACCESS++;

    return -1;
}

int MEMORY_MANAGER::add_wq(PACKET *packet)
{
    // check for duplicates in the write queue
    int index = WQ.check_queue(packet);
    if (index != -1) {

        WQ.MERGED++;
        WQ.ACCESS++;

        return index; // merged index
    }

    // sanity check
    if (WQ.occupancy >= WQ.SIZE)
        assert(0);

    // if there is no duplicate, add it to the write queue
    index = WQ.tail;
    if (WQ.entry[index].address != 0) {
        cerr << "[" << NAME << "_ERROR] " << __func__ << " is not empty index: " << index;
        cerr << " address: " << hex << WQ.entry[index].address;
        cerr << " full_addr: " << WQ.entry[index].full_addr << dec << endl;
        assert(0);
    }

    WQ.entry[index] = *packet;
/*
    // ADD LATENCY
    if (WQ.entry[index].event_cycle < current_core_cycle[packet->cpu])
        WQ.entry[index].event_cycle = current_core_cycle[packet->cpu] + LATENCY;
    else
        WQ.entry[index].event_cycle += LATENCY;  */

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

    WQ.TO_CACHE++;
    WQ.ACCESS++;

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

void MEMORY_MANAGER::operate()
{
//distribute write requests
    if ((WQ.head == WQ.tail) && WQ.occupancy == 0) {
        //return -1;
    }
    else if (WQ.head < WQ.tail) {
        for (uint32_t i=WQ.head; i<WQ.tail; i++) {
            if (get_memtype(WQ.entry[i].address) < 1) { //send request to DRAM
                if (lower_level->get_occupancy(2, WQ.entry[i].address) == lower_level->get_size(2, WQ.entry[i].address)) {

                    // lower level WQ is full, cannot replace this victim
                    lower_level->increment_WQ_FULL(WQ.entry[i].address);
                    STALL[WQ.entry[i].type]++;
                }
                else {
                    PACKET writeback_packet;

                    //writeback_packet.fill_level = fill_level << 1;
                    writeback_packet.cpu = WQ.entry[i].cpu;
                    writeback_packet.address = WQ.entry[i].address;
                    writeback_packet.full_addr = WQ.entry[i].full_addr;
                    writeback_packet.data = WQ.entry[i].data;
                    writeback_packet.instr_id = WQ.entry[i].instr_id;
                    writeback_packet.ip = 0; // writeback does not have ip
                    writeback_packet.type = WRITEBACK;
                    writeback_packet.event_cycle = WQ.entry[i].event_cycle;

                    lower_level->add_wq(&writeback_packet);
		    
		    WQ.remove_queue(&WQ.entry[i]);
            	    //WQ.num_returned--;
                }
            }
	    else { //send request to NVM
                if (extra_interface->get_occupancy(2, WQ.entry[i].address) == extra_interface->get_size(2, WQ.entry[i].address)) {

                    // lower level WQ is full, cannot replace this victim
                    extra_interface->increment_WQ_FULL(WQ.entry[i].address);
                    STALL[WQ.entry[i].type]++;
                }
                else {
                    PACKET writeback_packet;

                    //writeback_packet.fill_level = fill_level << 1;
                    writeback_packet.cpu = WQ.entry[i].cpu;
                    writeback_packet.address = WQ.entry[i].address;
                    writeback_packet.full_addr = WQ.entry[i].full_addr;
                    writeback_packet.data = WQ.entry[i].data;
                    writeback_packet.instr_id = WQ.entry[i].instr_id;
                    writeback_packet.ip = 0; // writeback does not have ip
                    writeback_packet.type = WRITEBACK;
                    writeback_packet.event_cycle = WQ.entry[i].event_cycle;

                    extra_interface->add_wq(&writeback_packet);
		    
		    WQ.remove_queue(&WQ.entry[i]);
            	    //WQ.num_returned--;
                }
            }
        }
    }
    else {//head >= tail && non-zero occupancy
	for (uint32_t i=WQ.head; i<WQ.SIZE; i++) {
            if (get_memtype(WQ.entry[i].address) < 1) { //send request to DRAM
                if (lower_level->get_occupancy(2, WQ.entry[i].address) == lower_level->get_size(2, WQ.entry[i].address)) {

                    // lower level WQ is full, cannot replace this victim
                    lower_level->increment_WQ_FULL(WQ.entry[i].address);
                    STALL[WQ.entry[i].type]++;
                }
                else {
                    PACKET writeback_packet;

                    //writeback_packet.fill_level = fill_level << 1;
                    writeback_packet.cpu = WQ.entry[i].cpu;
                    writeback_packet.address = WQ.entry[i].address;
                    writeback_packet.full_addr = WQ.entry[i].full_addr;
                    writeback_packet.data = WQ.entry[i].data;
                    writeback_packet.instr_id = WQ.entry[i].instr_id;
                    writeback_packet.ip = 0; // writeback does not have ip
                    writeback_packet.type = WRITEBACK;
                    writeback_packet.event_cycle = WQ.entry[i].event_cycle;

                    lower_level->add_wq(&writeback_packet);
		    
		    WQ.remove_queue(&WQ.entry[i]);
            	    //WQ.num_returned--;
                }
            }
	    else { //send request to NVM
                if (extra_interface->get_occupancy(2, WQ.entry[i].address) == extra_interface->get_size(2, WQ.entry[i].address)) {

                    // lower level WQ is full, cannot replace this victim
                    extra_interface->increment_WQ_FULL(WQ.entry[i].address);
                    STALL[WQ.entry[i].type]++;
                }
                else {
                    PACKET writeback_packet;

                    //writeback_packet.fill_level = fill_level << 1;
                    writeback_packet.cpu = WQ.entry[i].cpu;
                    writeback_packet.address = WQ.entry[i].address;
                    writeback_packet.full_addr = WQ.entry[i].full_addr;
                    writeback_packet.data = WQ.entry[i].data;
                    writeback_packet.instr_id = WQ.entry[i].instr_id;
                    writeback_packet.ip = 0; // writeback does not have ip
                    writeback_packet.type = WRITEBACK;
                    writeback_packet.event_cycle = WQ.entry[i].event_cycle;

                    extra_interface->add_wq(&writeback_packet);
		    
		    WQ.remove_queue(&WQ.entry[i]);
            	    //WQ.num_returned--;
                }
            }
        }
        for (uint32_t i=0; i<WQ.tail; i++) {
            if (get_memtype(WQ.entry[i].address) < 1) { //send request to DRAM
                if (lower_level->get_occupancy(2, WQ.entry[i].address) == lower_level->get_size(2, WQ.entry[i].address)) {

                    // lower level WQ is full, cannot replace this victim
                    lower_level->increment_WQ_FULL(WQ.entry[i].address);
                    STALL[WQ.entry[i].type]++;
                }
                else {
                    PACKET writeback_packet;

                    //writeback_packet.fill_level = fill_level << 1;
                    writeback_packet.cpu = WQ.entry[i].cpu;
                    writeback_packet.address = WQ.entry[i].address;
                    writeback_packet.full_addr = WQ.entry[i].full_addr;
                    writeback_packet.data = WQ.entry[i].data;
                    writeback_packet.instr_id = WQ.entry[i].instr_id;
                    writeback_packet.ip = 0; // writeback does not have ip
                    writeback_packet.type = WRITEBACK;
                    writeback_packet.event_cycle = WQ.entry[i].event_cycle;

                    lower_level->add_wq(&writeback_packet);
		    
		    WQ.remove_queue(&WQ.entry[i]);
            	    //WQ.num_returned--;
                }
            }
	    else { //send request to NVM
                if (extra_interface->get_occupancy(2, WQ.entry[i].address) == extra_interface->get_size(2, WQ.entry[i].address)) {

                    // lower level WQ is full, cannot replace this victim
                    extra_interface->increment_WQ_FULL(WQ.entry[i].address);
                    STALL[WQ.entry[i].type]++;
                }
                else {
                    PACKET writeback_packet;

                    //writeback_packet.fill_level = fill_level << 1;
                    writeback_packet.cpu = WQ.entry[i].cpu;
                    writeback_packet.address = WQ.entry[i].address;
                    writeback_packet.full_addr = WQ.entry[i].full_addr;
                    writeback_packet.data = WQ.entry[i].data;
                    writeback_packet.instr_id = WQ.entry[i].instr_id;
                    writeback_packet.ip = 0; // writeback does not have ip
                    writeback_packet.type = WRITEBACK;
                    writeback_packet.event_cycle = WQ.entry[i].event_cycle;

                    extra_interface->add_wq(&writeback_packet);
		    
		    WQ.remove_queue(&WQ.entry[i]);
            	    //WQ.num_returned--;
                }
            }
        }
    }

//distribute read requests
    if ((RQ.head == RQ.tail) && RQ.occupancy == 0) {
        //return -1;
    }
    else if (RQ.head < RQ.tail) {
        for (uint32_t i=RQ.head; i<RQ.tail; i++) {
            if (get_memtype(RQ.entry[i].address) < 1) { //send request to DRAM
                if (lower_level->get_occupancy(1, RQ.entry[i].address) == lower_level->get_size(1, RQ.entry[i].address))
		    {
			STALL[RQ.entry[i].type]++;
		    }
		else
		    {
			lower_level->add_rq(&RQ.entry[i]);
			RQ.remove_queue(&RQ.entry[i]);
		    }
	    }
	    else { //send request to NVM
                 if (extra_interface->get_occupancy(1, RQ.entry[i].address) == extra_interface->get_size(1, RQ.entry[i].address))
		    {
			STALL[RQ.entry[i].type]++;
		    }
		else
		    {
			extra_interface->add_rq(&RQ.entry[i]);
			RQ.remove_queue(&RQ.entry[i]);
		    }              
            }
        }
    }
    else {//head >= tail && non-zero occupancy
        for (uint32_t i=RQ.head; i<RQ.SIZE; i++) {
            if (get_memtype(RQ.entry[i].address) < 1) { //send request to DRAM
                if (lower_level->get_occupancy(1, RQ.entry[i].address) == lower_level->get_size(1, RQ.entry[i].address))
		    {
			STALL[RQ.entry[i].type]++;
		    }
		else
		    {
			lower_level->add_rq(&RQ.entry[i]);
			RQ.remove_queue(&RQ.entry[i]);
		    }
	    }
	    else { //send request to NVM
                 if (extra_interface->get_occupancy(1, RQ.entry[i].address) == extra_interface->get_size(1, RQ.entry[i].address))
		    {
			STALL[RQ.entry[i].type]++;
		    }
		else
		    {
			extra_interface->add_rq(&RQ.entry[i]);
			RQ.remove_queue(&RQ.entry[i]);
		    }              
            }

	}
        for (uint32_t i=0; i<RQ.tail; i++) {
            if (get_memtype(RQ.entry[i].address) < 1) { //send request to DRAM
                if (lower_level->get_occupancy(1, RQ.entry[i].address) == lower_level->get_size(1, RQ.entry[i].address))
		    {
			STALL[RQ.entry[i].type]++;
		    }
		else
		    {
			lower_level->add_rq(&RQ.entry[i]);
			RQ.remove_queue(&RQ.entry[i]);
		    }
	    }
	    else { //send request to NVM
                 if (extra_interface->get_occupancy(1, RQ.entry[i].address) == extra_interface->get_size(1, RQ.entry[i].address))
		    {
			STALL[RQ.entry[i].type]++;
		    }
		else
		    {
			extra_interface->add_rq(&RQ.entry[i]);
			RQ.remove_queue(&RQ.entry[i]);
		    }              
            }

        }
    }
	
    //handle_fill();
    //handle_writeback();
    //reads_available_this_cycle = MAX_READ;
    //handle_read();

    //if (PQ.occupancy && (reads_available_this_cycle > 0))
        //handle_prefetch();
}

uint32_t MEMORY_MANAGER::get_memtype(uint64_t address)
{
    int shift = 29;

    return (uint32_t) (address >> shift) & (NVM_RATIO - 1);
}
