#ifndef HMMU_H
#define HMMU_H

#include "memory_class.h"

//HMMU configuration
#define HMMU_WQ_SIZE 64
#define HMMU_RQ_SIZE 64
#define NVM_RATIO 8

#define HMMU_RAND_FACTOR 91827349653

//HMMU
class MEMORY_MANAGER : public MEMORY {
  private:
	//uint32_t page_size;
  	// uint32_t log2_page_size;
  	uint64_t num_ppages;
	uint64_t counter;
	uint64_t random_state;
  	// std::deque<uint64_t> ppage_free_list;
  	// uint64_t get_next_free_ppage();
	std::map<uint64_t, uint64_t> ppage_to_hmpage_map;
	uint64_t hmmu_rand();
	uint64_t get_hybrid_memory_pn(uint64_t address); // input: physical address, output: current mapped hybrid memory page number
	uint64_t get_hm_addr(uint64_t address); //use internal page table to translate to hybrid memory address
	uint64_t search_fast_page(); //note: this function returns the physical page number of the candidate, not the mapped hybrid memory page number
	void page_swap(uint64_t requested, uint64_t victim);
	void update_page_table(uint64_t address);

  public:
	const string NAME;
	PACKET_QUEUE WQ{NAME + "_WQ", HMMU_WQ_SIZE}, //write queue
		     RQ{NAME + "_RQ", HMMU_RQ_SIZE}; //read queue
//constructor
	MEMORY_MANAGER(string v1, uint64_t v2, uint64_t v3): NAME(v1), num_ppages(v2), counter(v3){	
		//initialize the physical to internal address mapping
		for(uint64_t i=0; i<num_ppages; i++) {
			ppage_to_hmpage_map[i] = i;
		}
		//initialize random number generator
		random_state = HMMU_RAND_FACTOR;
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
