#pragma once

#include <cstdint>
#include <mutex>

#include "protocols/common/readwritelock.hpp"
#include "protocols/serval/include/region.hpp"
#include "utils/atomic_wrapper.hpp"

#define MAX_NUM_SLOTS 64

// 前方宣言
class RowRegion;

class Version {
 public:
  enum class VersionStatus { PENDING, STABLE };  // status of version

  alignas(64) void* rec;  // nullptr if deleted = true (immutable)
  VersionStatus status;
  bool deleted;  // (immutable)
};

class VersionArray {
 public:
  Version* slots_[MAX_NUM_SLOTS];  // To-do: make the size of slot variable &
                                   // extend the size of slots
};

// ascending order
class GlobalVersionArray : public VersionArray {
 public:
  uint64_t length_ = 0;
  uint64_t ids_[MAX_NUM_SLOTS] = {0};  // corresponding to slots_

  std::tuple<uint64_t, Version*> search_visible_version(uint64_t tx_id) {
    // ascending order
    int i = length_ - 1;
    // 5:  5 20 24 50
    while (0 <= i && tx_id <= ids_[i]) {
      i--;
    }

    assert(-1 <= i);
    if (i == -1) return {0, nullptr};

    assert(ids_[i] < tx_id);
    // tx_id: 4
    // ids_: 1, [3], 4, 5, 6, 10
    return {ids_[i], slots_[i]};
  }

  void append(Version* version, uint64_t tx_id) {
    assert(length_ < MAX_NUM_SLOTS);

    // ascending order
    int i = length_ - 1;
    while (0 <= i && tx_id < ids_[i]) {
      ids_[i + 1] = ids_[i];
      slots_[i + 1] = slots_[i];
      i--;
    }

    ids_[i + 1] = tx_id;
    slots_[i + 1] = version;
    length_++;
  }

  void initialize() { length_ = 0; }

  bool is_empty() { return length_ == 0; }

  std::tuple<uint64_t, Version*> latest() {
    if (length_ == 0) return {0, nullptr};

    return {ids_[length_ - 1], slots_[length_ - 1]};
  }
};

template <typename Version_>
struct Value {
  using Version = Version_;
  alignas(64) RWLock rwl;
  uint64_t epoch_ = 0;

  Version* master_ = nullptr;  // final state

  GlobalVersionArray global_array_;  // Global Version Array

  // For contended versions
  RowRegion* row_region_ = nullptr;  // Pointer to per-core version array
  uint64_t core_bitmap_ = 0;

  void initialize() { rwl.initialize(); }

  void lock() { rwl.lock(); }

  bool try_lock() { return rwl.try_lock(); }

  void unlock() { rwl.unlock(); }
};
