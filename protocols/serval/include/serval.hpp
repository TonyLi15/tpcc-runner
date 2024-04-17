#pragma once

#include <cassert>
#include <cstring>
#include <set>
#include <stdexcept>

#include "indexes/masstree.hpp"
#include "protocols/common/timestamp_manager.hpp"
#include "protocols/common/transaction_id.hpp"
#include "protocols/serval/include/readwriteset.hpp"
#include "protocols/serval/include/region.hpp"
#include "protocols/serval/include/value.hpp"
#include "utils/logger.hpp"
#include "utils/utils.hpp"

#define BITS 64

template <typename Index>
class Serval {
 public:
  using Key = typename Index::Key;
  using Value = typename Index::Value;
  using Version = typename Value::Version;
  using LeafNode = typename Index::LeafNode;
  using NodeInfo = typename Index::NodeInfo;

  Serval(uint64_t core_id, uint64_t txid, RowRegionController& rrc)
      : core_(core_id), txid_(txid), rrc_(rrc) {}

  ~Serval() {}

  void append_pending_version(TableID table_id, Key key) {
    Index& idx = Index::get_index();

    tables.insert(table_id);
    auto& rw_table = rws.get_table(table_id);
    auto rw_iter = rw_table.find(key);

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

      Version* pending_version = do_append_pending_version(val);

      assert(pending_version);
      // Place it into readwriteset
      auto new_iter = rw_table.emplace_hint(
          rw_iter, std::piecewise_construct, std::forward_as_tuple(key),
          std::forward_as_tuple(pending_version->rec, nullptr,
                                ReadWriteType::UPDATE, false, val,
                                pending_version));  // TODO: チェックする
      // Place it in writeset
      auto& w_table = ws.get_table(table_id);
      w_table.emplace_back(key, new_iter);
    }

    // TODO: Case of found in read or written set
  }

  const Rec* read(TableID table_id, Key key) {
    Index& idx = Index::get_index();

    Value* val;
    typename Index::Result res =
        idx.find(table_id, key, val);  // find corresponding index in masstree

    if (res == Index::Result::NOT_FOUND) {
      throw std::runtime_error(
          "masstree NOT_FOUND");  // TODO: この場合、どうするかを考える
      return nullptr;
    }

    Version* visible = nullptr;

    /*
    epoch_ in the val is stale because any append is executed in the row in the
    current epoch.
    read the final state in the previous epoch
    */
    uint64_t epoch = val->epoch_;
    if (epoch != epoch_) {
      visible = val->master_;
      return execute_read(visible);
    }

    assert(epoch == epoch_);

    /*
    append is executed in the current epoch

    case1: core_bitmap == 0
    - the val is uncontented
    - use global version array

    case2: core_bitmap != 0
    - the val is contented
    - use row region
    - (but also check the global array)
    */

    uint64_t core_bitmap = val->core_bitmap_;

    auto [txid_in_global_array, visible_in_global_array] =
        val->global_array_.search_visible_version(txid_);

    // case1: core_bitmap == 0
    if (core_bitmap == 0) {
      if (!visible_in_global_array) {  // for debug
        std::cout << val->global_array_.length_ << std::flush;
      }
      assert(visible_in_global_array);
      assert(txid_in_global_array < txid_);
      visible = visible_in_global_array;
      return wait_stable_and_execute_read(visible);
    }

    // case2: core_bitmap != 0

    /*
    identify the potencial core

    case1: the potencial core is found
    - identify the transaction

    case2: the potencial core is not found
    - read the final state in the previous epoch
    */
    int potencial_core = find_the_largest_among_smallers(core_bitmap, core_);
    if (potencial_core == -1) {
      visible =
          visible_in_global_array ? visible_in_global_array : val->master_;
      return execute_read(visible);
    }

    /*
   identify the transaction

   case1: the transaction is found

   case2: the transaction is not found
   */
    PerCoreVersionArray* version_array =
        val->row_region_->arrays_[potencial_core];

    int target_tx = find_the_largest_among_smallers(
        version_array->transaction_bitmap_, txid_ % 64);

    // case1: the transaction is found
    if (target_tx != -1) {
      uint64_t target_tx_id = (potencial_core * 64) + target_tx;
      // do read
      assert(target_tx_id != txid_in_global_array);
      visible = (visible_in_global_array && target_tx_id < txid_in_global_array)
                    ? visible_in_global_array
                    : version_array->slots_[target_tx];
      return wait_stable_and_execute_read(visible);
    }

    // case2: the transaction is not found
    /*
      identify the next potencial core

      case1: the next potencial core is found
      - identify the transaction

      case2: the next potencial core is not found
      - read the final state in the previous epoch
    */
    int next_potencial_core =
        find_the_largest_among_smallers(core_bitmap, potencial_core - 1);

    // case1: the next potencial core is found
    if (next_potencial_core != -1) {
      PerCoreVersionArray* next_version_array =
          val->row_region_->arrays_[next_potencial_core];

      int next_target_tx =
          find_the_largest(next_version_array->transaction_bitmap_);  //

      uint64_t next_target_tx_id = (next_potencial_core * 64) + next_target_tx;

      assert(next_target_tx_id != txid_in_global_array);
      visible =
          (visible_in_global_array && next_target_tx_id < txid_in_global_array)
              ? visible_in_global_array
              : version_array->slots_[next_target_tx];
      return wait_stable_and_execute_read(visible);
    }

    // case2: the next potencial core is not found
    visible = visible_in_global_array ? visible_in_global_array : val->master_;
    return execute_read(val->master_);
  }

  Rec* write(TableID table_id, Key key) { return upsert(table_id, key); }

  Rec* upsert(TableID table_id, Key key) {
    const Schema& sch = Schema::get_schema();
    size_t record_size = sch.get_record_size(table_id);
    auto& rw_table = rws.get_table(table_id);
    auto rw_iter = rw_table.find(key);

    if (rw_iter == rw_table.end()) {
      // TODO: この場合は絶対に呼ばれないはず
      throw std::runtime_error("NOT_FOUND");
      return nullptr;
    }

    Version* pending_version = rw_iter->second.pending_version;
    Rec* rec = MemoryAllocator::aligned_allocate(record_size);

    __atomic_store_n(&pending_version->rec, rec, __ATOMIC_SEQ_CST);  // write
    __atomic_store_n(&pending_version->status, Version::VersionStatus::STABLE,
                     __ATOMIC_SEQ_CST);
    return rec;
  }

  uint64_t core_;
  // TxID txid_;
  uint64_t txid_;
  uint64_t epoch_ = 0;

 private:
  std::set<TableID> tables;
  ReadWriteSet<Key, Value> rws;  // read write set
  WriteSet<Key, Value> ws;       // write set

  RowRegion* spare_region_ = nullptr;

  RowRegionController& rrc_;

  Version* do_append_pending_version(Value* val) {
    /*
     the first transaction to come to the val in the current epoch should...

     case1: val->epoch_ < epoch_
     - acquire the lock
      - success:　the first transaction in the current epoch to arrive on
     the val
      - failed: back to case1
     - 1. update val->master_
     - 2. initialize the global_array_ and row_region_ and core_bitmap_
     - 3. append to the global_array_
     - 4. update val->epoch_
     - release the lock

     case2: val->epoch_ == epoch_
     */

    uint64_t epoch;

    while ((epoch = __atomic_load_n(&val->epoch_, __ATOMIC_SEQ_CST)) < epoch_) {
      /*
       case1: val->epoch_ < epoch_
       */
      assert(epoch < epoch_);

      if (!val->try_lock()) continue;

      // the first transaction in the current epoch to arrive on the val

      // 1. update val->master_
      val->master_ = find_final_state_in_one_previous_epoch(val);
      assert(val->master_);

      // 2. initialize the global_array_ and core_bitmap_
      val->core_bitmap_ = 0;
      val->global_array_.initialize();

      // 3. append to the global_array_
      Version* pending_version = create_pending_version();
      val->global_array_.append(pending_version, txid_ % 64);

      // 4. update val->epoch_
      val->epoch_ = epoch_;
      val->unlock();
      return pending_version;
    };

    /*
    case2: val->epoch_ == epoch_
    */
    RowRegion* cur_region =
        __atomic_load_n(&val->row_region_, __ATOMIC_SEQ_CST);

    // the val will or may be contented if the row_region_ is already exist.
    if (may_be_contented(cur_region)) {
      // region is not nullptr -> Per-core version array exist (_1)
      return append_to_contented_row(val, cur_region);
    }

    // the val is may be uncontented
    if (val->try_lock()) {
      // Successfully got the try lock (00)
      return append_to_unconted_row(val);
    }

    // couldn't acquire the lock: the val is getting crowded.
    RowRegion* new_region =
        spare_region_ ? spare_region_ : rrc_.fetch_new_region();
    if (try_install_new_region(val, cur_region, new_region)) {
      return append_to_contented_row(val, new_region);
    }

    // other thread already install new region
    RowRegion* other_region =
        __atomic_load_n(&val->row_region_, __ATOMIC_SEQ_CST);
    spare_region_ = new_region;
    return append_to_contented_row(val, other_region);
  }

  Version* find_final_state_in_one_previous_epoch(Value* val) {
    /*
    case1: append occurred to region in one previous epoch (core_bitmap_ != 0)

    case2: append didn't occur to region in one previous epoch (core_bitmap_ ==
    0)
    - case2-1: any append didn't occurred in one previous epoch
    (val->global_array_.is_empty()) => use master
    - case2-2: append occurred only to global version array
    (!val->global_array_.is_empty())
    */
    auto [id_global_array, latest_global_array] = val->global_array_.latest();

    if (latest_global_array == nullptr) {
      assert(val->core_bitmap_ == 0);
      return val->master_;  //
    }

    if (val->core_bitmap_ == 0) {
      return latest_global_array;
    }

    int core = find_the_largest(val->core_bitmap_);
    PerCoreVersionArray* version_array = val->row_region_->arrays_[core];
    int tx = find_the_largest(version_array->transaction_bitmap_);
    return (uint64_t)(core * 64 + tx) < id_global_array
               ? latest_global_array
               : version_array->slots_[tx];
  }

  Rec* execute_read(Version* visible) {
    assert(visible);
    return visible->rec;
  }

  Rec* wait_stable_and_execute_read(Version* visible) {
    assert(visible);
    while (__atomic_load_n(&visible->status, __ATOMIC_SEQ_CST) ==
           Version::VersionStatus::PENDING) {
      // spin
    }
    return execute_read(visible);
  }

  // 1000...0000
  // 注： 符号付きの右シフトは左が符号ビットで埋まる
  // int64_t set_upper_bit___signed() { return UINT64_MAX ^ (UINT64_MAX >>
  // 1); }
  int64_t set_upper_bit___signed() { return ~(~0ULL >> 1); }
  uint64_t set_upper_bit___unsigned() { return ~(~0ULL >> 1); }

  uint64_t set_bit_at_the_given_location(uint64_t pos) {
    return set_upper_bit___unsigned() >> pos;
  }

  uint64_t mask_to_smallers(int pos_from_left) {
    return set_upper_bit___signed() >> pos_from_left;  // 1111...1000
  }

  int find_the_largest_among_smallers(uint64_t bitmap, int position) {
    uint64_t smallers = bitmap & mask_to_smallers(position);

    if (smallers == 0) return -1;

    return find_the_largest(smallers);
  }

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
  int find_the_location_of_first_bit_from_right(uint64_t x) {
    return __builtin_ffsll(x);
  }
  int find_the_largest(uint64_t smallers) {
    return BITS - (find_the_location_of_first_bit_from_right(smallers) - 1);
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

  bool try_install_new_region(Value* val, RowRegion* cur_region,
                              RowRegion* new_region) {
    assert(cur_region == nullptr);
    return __atomic_compare_exchange_n(&val->row_region_, &cur_region,
                                       new_region, false, __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST);
  }

  // lock should be acuired before this function is called
  Version* append_to_unconted_row(Value* val) {
    Version* new_version = create_pending_version();

    // Append pending version to global version chain
    val->global_array_.append(new_version, txid_ % 64);

    val->unlock();
    return new_version;
  }

  bool may_be_contented(RowRegion* row_region) { return row_region != nullptr; }

  Version* append_to_contented_row(Value* val, RowRegion* region) {
    // 1. Create pending version
    Version* pending_version = create_pending_version();

    // 2. Update core bitmap if this is the first append in the current epoch
    // Otherwise, core_bitmap_ is already updated
    uint64_t core_bitmap =
        __atomic_load_n(&val->core_bitmap_, __ATOMIC_SEQ_CST);
    if (is_first_append_in_this_epoch(core_bitmap)) {
      region->initialize(core_);  // clear transaction bitmap
      __atomic_or_fetch(&val->core_bitmap_,
                        set_bit_at_the_given_location(core_),
                        __ATOMIC_SEQ_CST);  // core 2: 0010 0000 ... 0000
    }

    // 3. Append to per-core version array and update transaction bitmap
    region->append(core_, pending_version, txid_ % 64);

    return pending_version;
  }

  bool is_first_append_in_this_epoch(uint64_t core_bitmap) {
    // core 2: 0010 0000 ... 0000
    return (core_bitmap & set_bit_at_the_given_location(core_)) == 0;
  }

  Version* create_pending_version() {
    Version* version = reinterpret_cast<Version*>(
        MemoryAllocator::aligned_allocate(sizeof(Version)));
    version->rec = nullptr;  // TODO
    version->deleted = false;
    version->status = Version::VersionStatus::PENDING;
    return version;
  }
};