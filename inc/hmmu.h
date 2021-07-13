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
#define FMC_BLOCK_SIZE 128
#define LOG2_FMC_BLOCK_SIZE 7
#define FMC_SET 32768  // 16MB/(128B/WAY*4WAY/SET), could config FMC size here
#define LOG2_FMC_SET 15
#define FMC_WAY 4
#define FMC_TAG_WIDTH 10
#define THRESHOLD 4
#define NUM_RECENT_PAGES 2048 //number of the tracked recently used pages

#define FMC_WRITE 1
#define FMC_OVERWRITE 2
#define FMC_WRITEBACK 4

//HMMU
class MEMORY_MANAGER : public MEMORY {
  //private:
  public:
  	uint64_t num_ppages;
	uint64_t counter;
	uint64_t random_state;
	std::map<uint64_t, uint64_t> ppage_to_hmpage_map;

	//uint64_t num_fmc_pages; the maximum number of different pages in the fast memory cache
	std::map<uint64_t, std::bitset<32>> block_counter;  //this is used for two tasks: DRAM part for revisit, and NVM part for caching stats; reset on page swap

	//uint8_t fmc_eviction, fmc_writeback;
	uint8_t fmc_type;
	uint8_t cached_block_read, incoming_block_read;
	uint8_t cached_block_write, incoming_block_write;
	uint64_t cached_block, incoming_block;
	PACKET cached_packet, incoming_packet;
	std::map<uint64_t, std::bitset<4>> inflight_blocks; // used to track two blocks involved in a dirty block eviction, two entries: cached block--read sent/read done/write sent/write done, incoming block--same
	std::deque<uint64_t> cached_block_list;
	std::deque<uint64_t> incoming_block_list;
	std::deque<PACKET> cached_packet_list; // not really mean caching some packet, only to tell them apart, this one for packet with address in the cached block
	std::deque<PACKET> incoming_packet_list; // this one for packet from the current request
	std::deque<uint32_t> fmc_set_list; //these store the set and the lru victim way for the placement of incoming block
	std::deque<int> fmc_way_list;
	std::deque<uint8_t> fmc_type_list; // the type of access to the fmc, if it evicts something, if it needs writeback?

	//uint8_t dma_busy; //flag for signifying that there are in-flight swapping pages
	uint64_t victim_page, incoming_page;
	std::deque<PACKET> nvm_packet_list; // a dummy packet working as iterator of page blocks
	std::deque<PACKET> dram_packet_list;
	std::deque<uint64_t> victim_page_list; //victim is in DRAM, incoming is the NVM one being access
	std::deque<uint64_t> incoming_page_list; // should store physical page number here, since the memory use physical address and I need to use that to track read/write
	std::map<uint8_t, std::bitset<4>> victim_page_progress;
	std::map<uint8_t, std::bitset<4>> incoming_page_progress;
	std::bitset<32> incoming_page_block_read; 
	std::bitset<32> victim_page_block_read;
	std::bitset<32> incoming_page_block_write; 
	std::bitset<32> victim_page_block_write;
	//std::bitset<32> incoming_block_swapped;
	//std::bitset<32> victim_block_swapped;
	// in the state machine, the page progress bits for each blocks of a page are always set at the same time
	//by having this bitset, I can set one bit in a time, so as to record the difference between the completion time of each block number
	//after finishing this utility, I could look up it in handle_write/read funcs in case of accessing a in-flight page.

	uint64_t hmmu_rand();
	uint64_t get_ppn(uint64_t address);
	uint64_t get_hybrid_memory_pn(uint64_t address); // input: physical address, output: current mapped hybrid memory page number
	uint64_t get_hm_addr(uint64_t address); //use internal page table to translate to hybrid memory address
	uint64_t page_plus_block(uint64_t pn, uint8_t block);
	uint64_t search_fast_page(); //note: this function returns the physical page number of the candidate, not the mapped hybrid memory page number
	void page_swap(uint64_t requested, uint64_t victim);
	//void update_page_table(uint64_t address); //this function is not used anymore

	const uint32_t NUM_SET;
	const int NUM_WAY;
	BLOCK **block;

  //public:
	uint8_t memory_accessed;
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
			for (int j=0; j<NUM_WAY; j++) {
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
		memory_accessed = 0;

		fmc_type = 0;
		cached_block_read = 0;
		incoming_block_read = 0;
		cached_block_write = 0;
		incoming_block_write = 0;

		//dma_busy = 0;
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

	void request_dispatch(), handle_write(), handle_read(), handle_dma();

	int check_hit(PACKET *packet),
		find_lru_victim(uint64_t addr);
	
	uint8_t get_bit_number(uint64_t address),
			get_cached_number(uint64_t address);

	void invalidate_entry(uint64_t inval_addr),
		 fill_cache(uint32_t set, int way, PACKET *packet),
		 lru_update(uint32_t set, int way),
		 set_cached_block(uint64_t address),
		 reset_cached_block(uint64_t address);

	uint32_t get_set(uint64_t address);

	void revisited_stats(),
		 update_recent_pages(uint64_t address),
		 tune_threshold();

};

#endif
