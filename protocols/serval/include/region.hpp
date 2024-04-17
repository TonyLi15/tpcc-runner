#pragma once

#include <unistd.h>  // for gettid()

#include "protocols/serval/include/readwriteset.hpp"
#include "protocols/serval/include/value.hpp"
#include "utils/numa.hpp"
#define NUM_REGIONS 256  // Caracal's 256

class alignas(64) PerCoreVersionArray
    : public VersionArray {  // Caracal's per-core buffer
 public:
  uint64_t transaction_bitmap_ = 0;

  void initialize() { transaction_bitmap_ = 0; }

  uint64_t setbit(uint64_t bits, uint64_t pos) {
    assert(pos < 64);  // TODO
    return bits | set_bit_at_the_given_location(pos);
  }
  uint64_t set_upper_bit___unsigned() { return ~(~0ULL >> 1); }  // TODO
  uint64_t set_bit_at_the_given_location(uint64_t pos) {         // TODO
    return set_upper_bit___unsigned() >> pos;
  }

  void update_transaction_bitmap(uint64_t tx_id) {
    // change transaction bitmap's corresponding bit to 1
    transaction_bitmap_ = setbit(transaction_bitmap_, tx_id);
  }

  void append(Version* version, uint64_t tx_id) {
    // count the number of bits in transaction_bitmap_
    int length = __builtin_popcountll(transaction_bitmap_);

    // append
    assert(length < MAX_NUM_SLOTS);
    slots_[length] = version;

    // update transaction_bitmap_
    update_transaction_bitmap(tx_id);
  }
};

class RowRegion {  // Caracal's per-core buffer
 public:
  PerCoreVersionArray*
      arrays_[LOGICAL_CORE_SIZE];  // TODO: alignas(64) をつけるか検討

  void initialize(uint64_t core) { arrays_[core]->initialize(); }

  void append(uint64_t core, Version* version, uint64_t tx) {
    arrays_[core]->append(version, tx);
  }

  RowRegion() {
    pid_t main_tid = gettid();  // fetch the main thread's tid
    for (uint64_t core_id = 0; core_id < LOGICAL_CORE_SIZE; core_id++) {
      Numa numa(main_tid,
                core_id);  // move to the designated core from main thread

      arrays_[core_id] = new PerCoreVersionArray();
    }
  }
};

class RowRegionController {
 private:
  RowRegion regions_[NUM_REGIONS];
  RWLock lock_;
  uint64_t used_ = 0;  // how many regions currently used

 public:
  RowRegion* fetch_new_region() {
    lock_.lock();
    assert(used_ < NUM_REGIONS);
    RowRegion* region = &regions_[used_];  // TODO: reconsider
    used_++;
    lock_.unlock();
    return region;
  }
};
