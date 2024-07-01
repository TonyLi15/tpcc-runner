#pragma once

#include <cassert>
#include <cstring>
#include <set>
#include <stdexcept>
#include <unordered_set>

#include "indexes/masstree.hpp"
#include "protocols/common/readwritelock.hpp"
#include "protocols/common/transaction_id.hpp"
#include "protocols/serval_rc/include/readwriteset.hpp"
#include "protocols/serval_rc/include/value.hpp"
#include "utils/bitmap.hpp"
#include "utils/logger.hpp"
#include "utils/tsc.hpp"
#include "utils/utils.hpp"

template <typename Index> class Serval {
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

    void update_write_bitmaps(TableID table_id, Key key,
                              WriteBitmap *&w_bitmap) {
        Index &idx = Index::get_index();

        tables.insert(table_id);
        std::vector<Key> &w_table = ws.get_table(table_id);
        typename std::vector<Key>::iterator w_iter =
            std::find(w_table.begin(), w_table.end(), key);

        // Case of not append occur
        if (w_iter == w_table.end()) {
            Value *val;
            typename Index::Result res = idx.find(
                table_id, key, val); // find corresponding index in masstree

            if (res == Index::Result::NOT_FOUND) {
                assert(false);
                throw std::runtime_error(
                    "masstree NOT_FOUND"); // TODO: この場合、どうするかを考える
                return;
            }

            // Got value from masstree
            w_bitmap = &val->w_bitmap_;
            val->w_bitmap_.update_bitmap(core_, get_tx_serial(serial_id_));

            // Place it in writeset
            auto &w_table = ws.get_table(table_id);
            w_table.emplace_back(key);
        }

        // TODO: Case of found in read or written set
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
            typename Index::Result res = idx.find(
                table_id, key, val); // find corresponding index in masstree

            if (res == Index::Result::NOT_FOUND) {
                assert(false);
                throw std::runtime_error(
                    "masstree NOT_FOUND"); // TODO: この場合、どうするかを考える
                return;
            }

            // Got value from masstree
            w_bitmap = &val->w_bitmap_;
            pending = val->w_bitmap_.append_pending_version(
                core_, get_tx_serial(serial_id_));
            assert(pending);

            // Place it in writeset
            auto &w_table = ws.get_table(table_id);
            w_table.emplace_back(key);
        }

        // TODO: Case of found in read or written set
    }

    const Rec *read([[maybe_unused]] TableID table_id, [[maybe_unused]] Key key,
                    Version *pending, WriteBitmap *w_bitmap) {
        Rec *rec = wait_stable_and_execute_read(pending);
        w_bitmap->decrement_ref_cnt();
        return rec;
    }

    Rec *write(TableID table_id, WriteBitmap *w_bitmap) {
        return upsert(table_id, w_bitmap);
    }

    Rec *upsert(TableID table_id, [[maybe_unused]] WriteBitmap *w_bitmap) {
        const Schema &sch = Schema::get_schema();
        size_t record_size = sch.get_record_size(table_id);

        // Rec *rec = MemoryAllocator::aligned_allocate(record_size);
        Rec *rec = reinterpret_cast<Rec *>(operator new(record_size));

        Version *pending =
            w_bitmap->identify_write_version(core_, get_tx_serial(serial_id_));
        if (pending) {
            __atomic_store_n(&pending->rec, rec, __ATOMIC_SEQ_CST); // write
            __atomic_store_n(&pending->status, Version::VersionStatus::STABLE,
                             __ATOMIC_SEQ_CST);
        }

        return rec;
    }

    uint64_t core_;
    uint64_t serial_id_; // 0 - 4096
    uint64_t epoch_ = 0;

  private:
    WriteSet<Key> ws; // write set
    std::set<TableID> tables;

    Stat &stat_;

    uint64_t get_core_serial(uint64_t serial_id) { return serial_id / 64; }
    uint64_t get_tx_serial(uint64_t serial_id) { return serial_id % 64; }
    std::pair<uint64_t, uint64_t> decompose_id_serial(uint64_t serial_id) {
        return {get_core_serial(serial_id), get_tx_serial(serial_id)};
    }

    Rec *execute_read(Version *visible) {
        assert(visible);
        return visible->rec;
    }

    Rec *wait_stable_and_execute_read(Version *visible) {
        assert(visible);
        uint64_t start = rdtscp();
        while (__atomic_load_n(&visible->status, __ATOMIC_SEQ_CST) ==
               Version::VersionStatus::PENDING) {
            // spin
            asm volatile("pause" : : : "memory"); // equivalent to "rep; nop"
        }
        // TODO: やばい
        stat_.add(Stat::MeasureType::WaitInExecution, rdtscp() - start);
        return execute_read(visible);
    }
};