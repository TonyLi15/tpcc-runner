#pragma once

#include <cassert>
#include <cstring>
#include <set>
#include <stdexcept>
#include <unordered_set>

#include "indexes/masstree.hpp"
#include "protocols/cheetah/include/readwriteset.hpp"
#include "protocols/cheetah/include/value.hpp"
#include "protocols/common/readwritelock.hpp"
#include "protocols/common/transaction_id.hpp"
#include "protocols/ycsb_common/definitions.hpp"
#include "utils/bitmap.hpp"
#include "utils/logger.hpp"
#include "utils/tsc.hpp"
#include "utils/utils.hpp"

template <typename Index>
class Serval {
 public:
  using Key = typename Index::Key;
  using Value = typename Index::Value;
  using LeafNode = typename Index::LeafNode;
  using NodeInfo = typename Index::NodeInfo;

  Serval(uint64_t core_id, uint64_t txid, Stat &stat)
      : core_(core_id), serial_id_(txid), stat_(stat) {}

  ~Serval() {}

  void terminate_transaction() {
    for (TableID table_id : tables) {
      auto &w_table = ws.get_table(table_id);
      w_table.clear();
    }
    tables.clear();
  }

  void update_write_bitmaps(TableID table_id, Key key, WriteBitmap *&w_bitmap) {
    Index &idx = Index::get_index();

    tables.insert(table_id);
    std::vector<Key> &w_table = ws.get_table(table_id);
    // No bound is asserted on the per-table write set: it encoded YCSB's
    // fixed ten operations per transaction, and a TPC-C NewOrder legitimately
    // writes up to fifteen Stock rows. The invariant that actually matters,
    // that a transaction touches each row at most once, is checked below.
    typename std::vector<Key>::iterator w_iter =
        std::find(w_table.begin(), w_table.end(), key);

    // Case of not append occur
    if (w_iter == w_table.end()) {
      Value *val;
      typename Index::Result res =
          idx.find(table_id, key, val);  // find corresponding index in masstree

      if (res == Index::Result::NOT_FOUND) {
        assert(false);
        throw std::runtime_error(
            "masstree NOT_FOUND");  // TODO: この場合、どうするかを考える
        return;
      }

      // Got value from masstree
      w_bitmap = &val->w_bitmap_;
      bitmaps_[w_bitmap] = set_bit_at_the_given_location(
          bitmaps_[w_bitmap], get_tx_serial(serial_id_));
      assert(bitmaps_[w_bitmap]);

      // Place it in writeset
      w_table.emplace_back(key);
      return;
    }
    assert(false);
    throw std::runtime_error("error");  // TODO: この場合、どうするかを考える

    // TODO: Case of found in read or written set
  }

  void finalize_update_write_bitmaps() {
    while (!bitmaps_.empty()) {
      auto itr = bitmaps_.begin();
      while (itr != bitmaps_.end()) {
        auto &[w_bitmap, tx_bitmap] = *itr;
        if (!w_bitmap->lock_.try_lock()) {
          itr = std::next(itr);
          continue;
        }
        w_bitmap->update_bitmap(core_, tx_bitmap);
        w_bitmap->lock_.unlock();
        itr = bitmaps_.erase(itr);
      }
    }
    assert(bitmaps_.empty());
  }

  void append_pending_version(TableID table_id, Key key, Version *&pending,
                              WriteBitmap *&w_bitmap) {
    Index &idx = Index::get_index();

    tables.insert(table_id);
    std::vector<Key> &w_table = ws.get_table(table_id);
    typename std::vector<Key>::iterator w_iter =
        std::find(w_table.begin(), w_table.end(), key);

    // Case of not append occur
    if (w_iter == w_table.end()) {
      Value *val;
      typename Index::Result res =
          idx.find(table_id, key, val);  // find corresponding index in masstree

      if (res == Index::Result::NOT_FOUND) {
        assert(false);
        throw std::runtime_error(
            "masstree NOT_FOUND");  // TODO: この場合、どうするかを考える
        return;
      }

      // Got value from masstree
      w_bitmap = &val->w_bitmap_;
      pending = val->w_bitmap_.append_pending_version(
          core_, get_tx_serial(serial_id_), stat_);
      assert(pending);

      // Place it in writeset
      w_table.emplace_back(key);
      return;
    }
    assert(false);
    throw std::runtime_error("error");  // TODO: この場合、どうするかを考える

    // TODO: Case of found in read or written set
  }

  const Rec *read([[maybe_unused]] TableID table_id, [[maybe_unused]] Key key,
                  Version *pending, WriteBitmap *w_bitmap) {
    Rec *rec = wait_stable_and_execute_read(pending);
    w_bitmap->decrement_ref_cnt(stat_);
    return rec;
  }

  Rec *write(TableID table_id, WriteBitmap *w_bitmap) {
    return upsert(table_id, w_bitmap);
  }

  Rec *upsert(TableID table_id, [[maybe_unused]] WriteBitmap *w_bitmap) {
    const Schema &sch = Schema::get_schema();
    size_t record_size = sch.get_record_size(table_id);

    Rec *rec = nullptr;
    Version *pending = w_bitmap->identify_write_version(
        core_, get_tx_serial(serial_id_), stat_);
#ifdef NO_NWR
    // Ablation: pay for the version the Non-visible Write Rule would skip.
    bool is_skipped = false;
    if (!pending) {
      pending = w_bitmap->create_skipped_version(stat_);
      is_skipped = true;
    }
#endif
    if (pending) {
      rec = reinterpret_cast<Rec *>(operator new(record_size));
      __atomic_store_n(&pending->rec, rec, __ATOMIC_SEQ_CST);  // write
      __atomic_store_n(&pending->status, Version::VersionStatus::STABLE,
                       __ATOMIC_SEQ_CST);
#ifdef NO_NWR
      if (is_skipped) skipped_versions_.emplace_back(w_bitmap, pending);
#endif
    }

    return rec;
  }

#ifdef NO_NWR
  /*
  Reclaim the versions materialized above. They are unreachable by any
  reader, so no other core can be holding them; each core frees its own at
  the end of the execution phase, on the critical path, so the ablation
  measures the reclamation NWR also avoids.
  */
  void reclaim_skipped_versions() {
    for (auto &[w_bitmap, version] : skipped_versions_) {
      w_bitmap->gc_skipped_version(version, stat_);
    }
    skipped_versions_.clear();
  }
#endif

  uint64_t core_;
  uint64_t serial_id_;  // 0 - 4096
  uint64_t epoch_ = 0;

 private:
  WriteSet<Key> ws;  // write set
  std::set<TableID> tables;

  Stat &stat_;

  // <core, txbitmap>
  std::unordered_map<WriteBitmap *, uint64_t> bitmaps_;  // used for write phase

#ifdef NO_NWR
  std::vector<std::pair<WriteBitmap *, Version *>> skipped_versions_;
#endif

  uint64_t get_core_serial(uint64_t serial_id) { return serial_id / NUM_TXS_IN_ONE_EPOCH_IN_ONE_CORE; }
  uint64_t get_tx_serial(uint64_t serial_id) { return serial_id % NUM_TXS_IN_ONE_EPOCH_IN_ONE_CORE; }
  std::pair<uint64_t, uint64_t> decompose_id_serial(uint64_t serial_id) {
    return {get_core_serial(serial_id), get_tx_serial(serial_id)};
  }

  Rec *execute_read(Version *visible) {
    assert(visible);
    return visible->rec;
  }

  Rec *wait_stable_and_execute_read(Version *visible) {
    assert(visible);
    while (__atomic_load_n(&visible->status, __ATOMIC_SEQ_CST) ==
           Version::VersionStatus::PENDING) {
      // spin
      asm volatile("pause" : : : "memory");  // equivalent to "rep; nop"
    }
    return execute_read(visible);
  }
};