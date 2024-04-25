#pragma once

#include <unistd.h>  // for gettid()

#include "protocols/caracal/include/readwriteset.hpp"
#include "protocols/caracal/include/value.hpp"
#include "utils/numa.hpp"
#define NUM_REGIONS 256 // Caracal's default value


class PerCoreVersionArray {  // Caracal's per-core buffer
 public:
  uint64_t transaction_bitmap;  // -> Caracal's per-core buffer's header
  Version* slots[4];  // To-do: make the size of slot variable & extend the size
                      // of slots
  int current_slot;

  PerCoreVersionArray(unsigned int cpu, unsigned int node)
      : cpu_(cpu), node_(node) {}

  uint64_t setbit(uint64_t bits, int pos) {
    if (pos < 0 || pos >= 64) {
      // invalid pos return the original bits
      return bits;
    }
    return bits | (static_cast<uint64_t>(1) << pos);
  }

  void append_to_slots(Value<Version>* val, uint64_t core_id,
                       Version* pending_version, TxID tx_id) {
    // append pending version (tx_id) to current available slot
    slots[current_slot] = pending_version;
    current_slot++;

    // change transaction bitmap's corresponding bit to 1
    // 24/3/22: update the shift function
    transaction_bitmap = setbit(transaction_bitmap, tx_id.txid);

    // Update Core Bitmap by __atomic_or_fetch()
    // TODO: オーバーヘッド大きそう
    __atomic_or_fetch(&val->core_bitmap, 1ULL << core_id, __ATOMIC_SEQ_CST);
  }

  unsigned int get_cpu() { return cpu_; }
  unsigned int get_node() { return node_; }

 private:
  unsigned int cpu_;
  unsigned int node_;
};

class RowRegion {  // Caracal's per-core buffer
 public:
  PerCoreVersionArray**
      arrays_;  // each row region has [num_cores] per-core version arrays

  RowRegion(uint64_t num_cores) : arrays_(new PerCoreVersionArray*[num_cores]) {
    pid_t main_tid = gettid();  // fetch the main thread's tid
    for (uint64_t core_id = 0; core_id < num_cores; core_id++) {
      Numa numa(main_tid,
                core_id);  // move to the designated core from main thread
      unsigned int cpu, node;
      if (getcpu(&cpu, &node)) exit(EXIT_FAILURE);

      arrays_[core_id] = new PerCoreVersionArray(cpu, node);
    }
  }
};


// Row Region Controller: Used for fetching new region and count the used regions
class RowRegionController{
  	public:
		RowRegion* fetch_new_region(){
			lock_.lock();
			assert(used_ < NUM_REGIONS);
			RowRegion* region = &regions_[used_];
			used_++;
			lock_.unlock();
			return region;
		}
	private:
		RowRegion regions_[NUM_REGIONS];
		RWLock lock_;
		uint64_t used_ = 0;
};