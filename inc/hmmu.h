#ifndef HMMU_H
#define HMMU_H

#include "memory_class.h"

//HMMU configuration
#define HMMU_WQ_SIZE 64
#define HMMU_RQ_SIZE 64
#define NVM_RATIO 8

#define HMMU_RAND_FACTOR 91827349653

//I decided to track every page(2^20 versus 2^17 bitsets, may not be a trouble)
//#define FMC_MAX_PAGES 131072  16MB/128B=128K, used to decide the size of the addr-bitset mapping
#define FMC_SET 32768  // 16MB/(128B/WAY*4WAY/SET), could config FMC size here
#define LOG2_FMC_SET 15
#define FMC_WAY 4
#define THRESHOLD 4
#define NUM_RECENT_PAGES 2048 //number of the tracked recently used pages

//HMMU
class MEMORY_MANAGER : public MEMORY {
  private:
	//uint32_t page_size;
  	// uint32_t log2_page_size;
  	uint64_t num_ppages;
	uint64_t counter;
	uint64_t random_state;
	std::map<uint64_t, uint64_t> ppage_to_hmpage_map;

	//uint64_t num_fmc_pages; the maximum number of different pages in the fast memory cache
	std::map<uint64_t, std::bitset<32>> block_counter;  //this is used for two tasks: DRAM part for revisit, and NVM part for caching stats; reset on page swap

	uint64_t hmmu_rand();
	uint64_t get_hybrid_memory_pn(uint64_t address); // input: physical address, output: current mapped hybrid memory page number
	uint64_t get_hm_addr(uint64_t address); //use internal page table to translate to hybrid memory address
	uint64_t search_fast_page(); //note: this function returns the physical page number of the candidate, not the mapped hybrid memory page number
	void page_swap(uint64_t requested, uint64_t victim);
	void update_page_table(uint64_t address);

	const uint32_t NUM_SET, NUM_WAY;
	BLOCK **block;

  public:
	const string NAME;
	uint64_t revisited_count;
	uint32_t threshold;
	std::deque<uint64_t> recently_used_pages; 

	PACKET_QUEUE WQ{NAME + "_WQ", HMMU_WQ_SIZE}, //write queue
		     RQ{NAME + "_RQ", HMMU_RQ_SIZE}; //read queue
//constructor
	MEMORY_MANAGER(string v1, uint64_t v2, uint64_t v3, uint32_t v4, uint32_t v5/*, uint8_t v6*/): NAME(v1), num_ppages(v2), counter(v3), NUM_SET(v4), NUM_WAY(v5)/*, threshold(v6)*/ {	
		//initialize the physical to internal address mapping
		for(uint64_t i=0; i<num_ppages; i++) {
			ppage_to_hmpage_map[i] = i;
		}
		//initialize random number generator
		random_state = HMMU_RAND_FACTOR;

		//initialize the cache fashion area in DRAM ->initial LRU value
		block = new BLOCK* [NUM_SET];
		for (uint32_t i=0; i<NUM_SET; i++) {
			block[i] = new BLOCK[NUM_WAY];
			for (uint32_t j=0; j<NUM_WAY; j++) {
				block[i][j].lru = j;
			}
		}

		//initialize the cached_page(page number) to number_of_cached_blocks(bitset) mapping
		for(uint64_t i=0; i<num_ppages; i++) {
			block_counter[i] = 0;
		}

		//initialize the revisited stats
		revisited_count = 0;
		/* let it alone so the empty constructor gets used
		//initialize the tracking list of recently used pages
		for (uint64_t i=0; i<NUM_RECENT_PAGES; i++) {
			recently_used_pages.push_back(i);
		} */
		threshold = THRESHOLD;
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
	int  add_rq(PACKET *packet), 
		 add_wq(PACKET *packet), 
		 add_pq(PACKET *packet);

	void  return_data(PACKET *packet), 
		  operate(), 
		  increment_WQ_FULL(uint64_t address);

 	uint32_t  get_occupancy(uint8_t queue_type, uint64_t address),
		  	  get_size(uint8_t queue_type, uint64_t address),
		  	  get_memtype(uint64_t address);

	void request_dispatch(), handle_write(), handle_read();

	uint32_t check_hit(PACKET *packet),
			 find_lru_victim(uint64_t addr);
	
	uint8_t get_bit_number(uint64_t address),
			get_cached_number(uint64_t address);

	void invalidate_entry(uint64_t inval_addr),
		 fill_cache(uint32_t set, uint32_t way, PACKET *packet),
		 lru_update(uint32_t set, uint32_t way),
		 set_cached_block(uint64_t address),
		 reset_cached_block(uint64_t address);

	uint32_t get_set(uint64_t address);

	void revisited_stats(),
		 update_recent_pages(uint64_t address),
		 tune_threshold();

};

#endif
