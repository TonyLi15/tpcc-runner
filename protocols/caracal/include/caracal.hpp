#pragma once

#include <cassert>
#include <cstring>
#include <set>
#include <stdexcept>

#include "indexes/masstree.hpp"
#include "protocols/caracal/include/readwriteset.hpp"
#include "protocols/caracal/include/region.hpp"
#include "protocols/caracal/include/value.hpp"
#include "protocols/common/timestamp_manager.hpp"
#include "protocols/common/transaction_id.hpp"
#include "utils/logger.hpp"
#include "utils/utils.hpp"

template <typename Index>
class Caracal {
 public:
  using Key = typename Index::Key;
  using Value = typename Index::Value;
  using Version = typename Value::Version;
  using LeafNode = typename Index::LeafNode;
  using NodeInfo = typename Index::NodeInfo;

  Caracal(uint64_t core_id, TxID txid, std::vector<RowRegion>& my_regions)
      : core_id_(core_id), txid_(txid), regions_(my_regions), cur_ptr_(0) {}

  ~Caracal() {}

  void append_pending_version(TableID table_id, Key key) {
    // const Schema& sch = Schema::get_schema();
    Index& idx = Index::get_index();

    // size_t record_size = sch.get_record_size(table_id);
    tables.insert(table_id);
    auto& rw_table = rws.get_table(table_id);
    auto rw_iter = rw_table.find(key);

    Version* pending_version = nullptr;

    // Case of not read and written
    if (rw_iter == rw_table.end()) {
      Value* val;
      typename Index::Result res =
          idx.find(table_id, key, val);  // find corresponding index in masstree

      if (res == Index::Result::NOT_FOUND) {
        throw std::runtime_error(
            "masstree NOT_FOUND");  // TODO: この場合、どうするかを考える
        return;
      }

      // Got value from masstree
      RowRegion* region = __atomic_load_n(&val->row_region_, __ATOMIC_SEQ_CST);

      if (region == nullptr) {  // Per-core version array does not exist (_0)
        if (val->try_lock()) {  // Successfully got the try lock (00)
          pending_version = append_to_global_version_chain_00(val);
          goto FINISH;
        }

        // Failed to get the lock (10)
        pending_version = install_ptr_to_row_region_10(val);
        goto FINISH;
      }

      // region is not nullptr -> Per-core version array exist (_1)
      pending_version = update_row_region__1(val);
      goto FINISH;
    }

  FINISH:
    // Place it into readwriteset
    Value* val = reinterpret_cast<Value*>(
        MemoryAllocator::aligned_allocate(sizeof(Value)));
    auto new_iter = rw_table.emplace_hint(
        rw_iter, std::piecewise_construct, std::forward_as_tuple(key),
        std::forward_as_tuple(pending_version->rec, nullptr,
                              ReadWriteType::UPDATE, false, val,
                              pending_version));  // TODO: チェックする
    // Place it in writeset
    auto& w_table = ws.get_table(table_id);
    w_table.emplace_back(key, new_iter);

    // ========================================================
    // TODO: Case of found in read and written set
  }

  const Rec* read(TableID table_id, Key key) {
    Index& idx = Index::get_index();
    // const Schema& sch = Schema::get_schema();
    // size_t record_size = sch.get_record_size(table_id);
    Value* val;
    typename Index::Result res =
        idx.find(table_id, key, val);  // find corresponding index in masstree
    if (res == Index::Result::NOT_FOUND) {
      throw std::runtime_error(
          "masstree NOT_FOUND");  // TODO: この場合、どうするかを考える
      return nullptr;
    }

    // Use val to find visible version

    // ====== 1. load core bitmap and identify the core which create the visible
    // version ======

    // load current core's core bitmap
    uint64_t current_core_bitmap = val->core_bitmap;

    // my_core_pos -> spot the current core's digit in core bitmap (range: 0 -
    // 63)
    int my_core_pos = core_id_ % 64;

    // latest_core_pos -> spot the core that occurs the latest update
    int latest_core_pos =
        mask_and_find_latest_pos(current_core_bitmap, my_core_pos);

    // return nullptr if no visible version available
    if (latest_core_pos == 0) {
      return nullptr;
    }
    // ===========================================================================================

    // ====== 2. load the transaction bitmap from the identified core ======

    // load current per-core version array which potentially has visible version
    PerCoreVersionArray* current_version_array =
        val->row_region_->arrays_[latest_core_pos];
    // ===========================================================================================

    // ====== 3. identify visible version by calculating transaction_bitmap
    // ======

    // load curent transaction bitmap from current version array
    uint64_t current_transaction_bitmap =
        current_version_array->transaction_bitmap;

    // my_tx_pos -> spot the current tx's digit in tx bitmap
    int my_tx_pos = static_cast<int>(txid_.thread_id) % 64;

    // calculate latest_tx_num
    int latest_tx_pos =
        mask_and_find_latest_pos(current_transaction_bitmap, my_tx_pos);

    // the current per-core version array does not have the visible version,
    // needs to find visible version from the previous core
    if (latest_tx_pos == 0) {
    AGAIN:
      // load previous core's version array and transaction bitmap
      PerCoreVersionArray* previous_version_array =
          val->row_region_->arrays_[latest_core_pos - 1];
      uint64_t previous_transaction_bitmap =
          previous_version_array->transaction_bitmap;

      // find the visible version from previous core (no mask needed)
      int previous_latest_tx_pos = find_latest_pos(previous_transaction_bitmap);
      if (previous_latest_tx_pos == 0) {
        goto AGAIN;
      }
      Version* previous_latest_version =
          previous_version_array->slots_[previous_latest_tx_pos];
      return previous_latest_version->rec;
    }

    Version* latest_version = current_version_array->slots_[latest_tx_pos];
    // ===========================================================================================

    return latest_version->rec;
  }

  Rec* write(TableID table_id, Key key) { return upsert(table_id, key); }

  Rec* upsert(TableID table_id, Key key) {
    const Schema& sch = Schema::get_schema();
    size_t record_size = sch.get_record_size(table_id);
    auto& rw_table = rws.get_table(table_id);
    auto rw_iter = rw_table.find(key);

    if (rw_iter == rw_table.end()) {
      // TODO: この場合は絶対に呼ばれないはず
      throw std::runtime_error("masstree NOT_FOUND");
      return nullptr;
    }

    Version* pending_version = rw_iter->second.pending_version;
    pending_version->status = Version::VersionStatus::UPDATED;

    Rec* rec = MemoryAllocator::aligned_allocate(record_size);
    pending_version->rec = rec;  // write
    return pending_version->rec;
  }

  bool precommit() { return true; }

  void abort() {}

  uint64_t core_id_;
  TxID txid_;
  std::vector<RowRegion>& regions_;
  uint64_t regions_tail_;
  int cur_ptr_;

 private:
  std::set<TableID> tables;
  ReadWriteSet<Key, Value> rws;  // read write set
  WriteSet<Key, Value> ws;       // write set

  // 24/3/23 -> move all from public to private
  // 24/3/21 -> change type from unsigned int to uint64_t
  uint64_t clear_left(uint64_t bits, int pos) {
    uint64_t mask = (1ULL << (pos + 1)) - 1;
    return bits & mask;
  }

  uint64_t clear_right(uint64_t bits, int pos) {
    uint64_t mask = ~((1ULL << pos) - 1);
    return bits & mask;
  }

  // Apply type casting for buitlin bit functions

  // returns the location of 1st "1" from right
  template <typename T>
  int first_one_from_right(T x) {
    static_assert(std::is_same_v<T, uint64_t>, "T must be uint64_t");
    return __builtin_ffsll(x);
  }

  // returns the number of leading "0" from left
  template <typename T>
  int num_leading_zeros(T x) {
    static_assert(std::is_same_v<T, uint64_t>, "T must be uint64_t");
    return __builtin_clzll(x);
  }

  // returns the number of trailing "0" from left
  template <typename T>
  int num_trailing_zeros(T x) {
    static_assert(std::is_same_v<T, uint64_t>, "T must be uint64_t");
    return __builtin_ctzll(x);
  }

  // find the most-left "1" after clearing all bits from pos with mask
  int mask_and_find_latest_pos(uint64_t bits, int pos) {
    uint64_t masked_bits = clear_left(bits, pos);
    return (63 - num_leading_zeros(masked_bits));
  }

  // find the most-left "1" after clearing all bitswithout masking
  int find_latest_pos(uint64_t bits) { return (63 - num_leading_zeros(bits)); }

  Version* append_to_global_version_chain_00(Value* val) {
    Version* new_version = create_pending_version();

    // Append pending version
    new_version->prev = val->version;
    val->version = new_version;

    val->unlock();
    return new_version;
  }

  Version* install_ptr_to_row_region_10(Value* val) {
    RowRegion* return_value;
    RowRegion& my_region = regions_[cur_ptr_];

    // Install pointer to per-core version array by __atomic_exchange
    // stores the contents of my_region into val->ptr_to_version_array.
    // The original value of val->ptr_to_version_array is copied into
    // return_value.
    __atomic_exchange(&val->row_region_, &my_region, &return_value,
                      __ATOMIC_SEQ_CST);

    Version* pending_version = create_pending_version();

    // current thread is the first thread to assign per-core version region of
    // the data item CAS is successful, rn val->ptr_to_version_array is
    // my_region
    if (return_value == nullptr) {
      cur_ptr_++;  // move cur_ptr_ to next slot
      my_region.arrays_[core_id_]->append_to_slots(val, core_id_,
                                                   pending_version, txid_);
      return pending_version;
    }

    // other thread already assigned per-core version region of the data item
    // CAS is unsuccessful, val->ptr_to_version_array has some other addresss
    // use ptr which other thread already assigned
    return_value->arrays_[core_id_]->append_to_slots(val, core_id_,
                                                     pending_version, txid_);

    return pending_version;
  }

  Version* update_row_region__1(Value* val) {
    RowRegion my_region = regions_[cur_ptr_];

    // Create pending version, update 2 bitmaps and append to per-core version
    // array
    Version* pending_version = create_pending_version();
    my_region.arrays_[core_id_]->append_to_slots(val, core_id_, pending_version,
                                                 txid_);
    return pending_version;
  }

  Version* create_pending_version() {
    Version* version = reinterpret_cast<Version*>(
        MemoryAllocator::aligned_allocate(sizeof(Version)));
    version->prev = nullptr;
    version->rec = nullptr;  // TODO
    version->deleted = false;
    version->status = Version::VersionStatus::PENDING;
    return version;
  }

  void remove_already_inserted(TableID end_table_id, Key end_key,
                               bool end_exclusive) {
    Index& idx = Index::get_index();
    for (TableID table_id : tables) {
      auto& w_table = ws.get_table(table_id);
      for (auto w_iter = w_table.begin(); w_iter != w_table.end(); ++w_iter) {
        Key key = w_iter->first;
        if (end_exclusive && table_id == end_table_id && key == end_key) return;
        auto rw_iter = w_iter->second;
        auto rwt = rw_iter->second.rwt;
        bool is_new = rw_iter->second.is_new;
        if (rwt == ReadWriteType::INSERT && is_new) {
          // On INSERT, insert the record to shared index
          Value* val = rw_iter->second.val;
          Version* version = val->version;
          idx.remove(table_id, key);
          val->version = nullptr;
          // GarbageCollector::collect(largest_ts, val);
          MemoryAllocator::deallocate(version->rec);
          MemoryAllocator::deallocate(version);
        }
        if (!end_exclusive && table_id == end_table_id && key == end_key)
          return;
      }
    }
  }

  void unlock_writeset(TableID end_table_id, Key end_key, bool end_exclusive) {
    for (TableID table_id : tables) {
      auto& w_table = ws.get_table(table_id);
      for (auto w_iter = w_table.begin(); w_iter != w_table.end(); ++w_iter) {
        if (end_exclusive && table_id == end_table_id &&
            w_iter->first == end_key)
          return;
        auto rw_iter = w_iter->second;
        Value* val = rw_iter->second.val;
        val->unlock();
        if (!end_exclusive && table_id == end_table_id &&
            w_iter->first == end_key)
          return;
      }
    }
  }

  // Acquire val->lock() before calling this function
  Version* get_correct_version(Value* val) {
    Version* version = val->version;

    // look for the latest version with write_ts <= start_ts
    // while (version != nullptr && start_ts < version->write_ts) {
    //   version = version->prev;
    // }

    return version;  // this could be nullptr
  }

  // Acquire val->lock() before calling this function
  void delete_from_tree(TableID table_id, Key key, Value* val) {
    Index& idx = Index::get_index();
    idx.remove(table_id, key);
    Version* version = val->version;
    val->version = nullptr;
    // GarbageCollector::collect(largest_ts, val);
    MemoryAllocator::deallocate(version->rec);
    MemoryAllocator::deallocate(version);
    return;
  }

  // Acquire val->lock() before calling this function
  void gc_version_chain(Value* val) {
    Version* gc_version_plus_one = nullptr;
    Version* gc_version = val->version;

    // // look for the latest version with write_ts <= start_ts
    // while (gc_version != nullptr && smallest_ts < gc_version->write_ts) {
    //   gc_version_plus_one = gc_version;
    //   gc_version = gc_version->prev;
    // }

    if (gc_version == nullptr) return;

    // keep one
    gc_version_plus_one = gc_version;
    gc_version = gc_version->prev;

    gc_version_plus_one->prev = nullptr;

    Version* temp;
    while (gc_version != nullptr) {
      temp = gc_version->prev;
      MemoryAllocator::deallocate(gc_version->rec);
      MemoryAllocator::deallocate(gc_version);
      gc_version = temp;
    }
  }
};