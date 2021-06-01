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

    // if there is no duplicate, add it to the write queue
    int index = WQ.tail;
    if (WQ.entry[index].address != 0) {
        cerr << "[" << NAME << "_ERROR] " << __func__ << " is not empty index: " << index;
        cerr << " address: " << hex << WQ.entry[index].address;
        cerr << " full_addr: " << WQ.entry[index].full_addr << dec << endl;
        assert(0);
    }

    WQ.entry[index] = *packet;


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

}

uint32_t MEMORY_MANAGER::get_memtype(uint64_t address)
{
    int shift = 29;

    return (uint32_t) (address >> shift) & (NVM_RATIO - 1);
}
