#ifndef HMMU_H
#define HMMU_H

#include "memory_class.h"

//HMMU configuration
#define HMMU_WQ_SIZE 64
#define HMMU_RQ_SIZE 64
#define NVM_RATIO 8

//HMMU
class MEMORY_MANAGER : public MEMORY {
  public:
	const string NAME;
	PACKET_QUEUE WQ{NAME + "_WQ", HMMU_WQ_SIZE}, //write queue
		     RQ{NAME + "_RQ", HMMU_RQ_SIZE}; //read queue
//constructor
	MEMORY_MANAGER(string v1): NAME(v1){	
	};
//destructor
	~MEMORY_MANAGER(){
		for (uint32_t i=0; i<NUM_CPUS; i++) {
            		upper_level_icache[i] = NULL;
            		upper_level_dcache[i] = NULL;
        }

        lower_level = NULL;
        extra_interface = NULL;

	};
//functions
	int  add_rq(PACKET *packet), add_wq(PACKET *packet), add_pq(PACKET *packet);
	void  return_data(PACKET *packet), operate(), increment_WQ_FULL(uint64_t address);
 	uint32_t  get_occupancy(uint8_t queue_type, uint64_t address),
		  get_size(uint8_t queue_type, uint64_t address),
		  get_memtype(uint64_t address);
	void request_dispatch(), handle_write(), handle_read();

};

#endif
