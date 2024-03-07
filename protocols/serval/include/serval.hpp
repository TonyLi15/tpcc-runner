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

template <typename Index>
class Serval {
public:
    using Key = typename Index::Key;
    using Value = typename Index::Value;
    using Version = typename Value::Version;
    using LeafNode = typename Index::LeafNode;
    using NodeInfo = typename Index::NodeInfo;
    using VersionStatus = typename Version::Status;

    Serval(uint64_t core_id, TxID txid, std::vector<RowRegion>& my_regions)
        : core_id_(core_id), txid_(txid), regions_(my_regions), cur_ptr_(0) {}

    ~Serval() {}

    void append_pending_version(TableID table_id, Key key) {
        const Schema& sch = Schema::get_schema();
        Index& idx = Index::get_index();

        size_t record_size = sch.get_record_size(table_id);
        tables.insert(table_id);
        auto& rw_table = rws.get_table(table_id);
        auto rw_iter = rw_table.find(key);

        Version* pending_version;

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
            RowRegion* region = __atomic_load_n(val->ptr_to_version_array, __ATOMIC_SEQ_CST);

            if (region == nullptr) {    // Per-core version array does not exist (_0)
                if (val->try_lock()) {  // Successfully got the try lock (00)
                    pending_version = append_to_global_version_chain_00(val);
                    goto FINISH;
                }

                // Failed to get the lock (10)
                pending_version = install_ptr_to_row_region_10(cur_ptr_);
                goto FINISH;
            }

            // region is not nullptr -> Per-core version array exist (_1)
            pending_version = update_row_region__1();
            goto FINISH;
        }

FINISH:
        // Place it into readwriteset
        auto new_iter = rw_table.emplace_hint(
            rw_iter, std::piecewise_construct, std::forward_as_tuple(key),
            std::forward_as_tuple(
                version->rec, nullptr, ReadWriteType::UPDATE, false, val,
                pending_version));  // TODO: チェックする
        // Place it in writeset
        auto& w_table = ws.get_table(table_id);
        w_table.emplace_back(key, new_iter);
    }

    const Rec* read(TableID table_id, Key key) {
        Index& idx = Index::get_index();
        size_t record_size = sch.get_record_size(table_id);
        Value* val;
        typename Index::Result res =
            idx.find(table_id, key, val);  // find corresponding index in masstree
        if (res == Index::Result::NOT_FOUND) {
            throw std::runtime_error("masstree NOT_FOUND");  // TODO: この場合、どうするかを考える
            return nullptr;
        }

        // Use val to find visible version

        // __buitin_ffsll(unsigned int): returns the location of 1st "1" from right to left
        unsigned int latest_core_num = __builtin_ffsll(val->core_bitmap);

        unsigned int latest_node = numa_node_of_cpu(latest_core_num);
        PerCoreVersionArray* current_version_array =
            getPerCoreVersionArray(latest_core_num, latest_node);

        uint64_t current_transaction_bitmap = current_version_array.transaction_bitmap;

        // Compare with my tx_id
        // To-Do: pass my tx_id and locate to transaction bitmap correctly
        // To-Do: change all bits to 0 if it's larger than my tx_id
        // __builtin_clzll(unsigned int): returns the number of leading "0"
        unsigned int latest_tx_num = __builtin_clzll(current_transaction_bitmap);

        if (latest_tx_num = 0) {
            return nullptr;
        }

        Version* latest_version = current_version_array.slots[latest_tx_num];

        return latest_version->rec;
    }

    Rec* write(TableID table_id, Key key) {
        return upsert(table_id, key);
    }

    Rec* upsert(TableID table_id, Key key) {
        const Schema& sch = Schema::get_schema();
        size_t record_size = sch.get_record_size(table_id);
        auto& w_table = ws.get_table(table_id);
        auto w_iter = w_table.find(key);

        if (w_iter == w_table.end()) {
            // TODO: この場合は絶対に呼ばれないはず
            throw std::runtime_error("masstree NOT_FOUND");
            return nullptr;
        }

        Value* val = w_iter->second.val;
        Version* pending_version = w_iter->second.pending_version;
        pending_version->status = VersionStatus::UPDATED;
        // これをバージョンにくっつけたらupsert終わり
        Rec* rec = MemoryAllocator::aligned_allocate(record_size);
        pending_version->rec = rec;  // write
        return pending_version->rec;
    }

    bool precommit() {
        // LOG_INFO("PRECOMMIT, ts: %lu, s_ts: %lu, l_ts: %lu", start_ts,
        // smallest_ts,
        //          largest_ts);
        // Index& idx = Index::get_index();

        // LOG_INFO("LOCKING RECORDS");
        // // Lock records
        // for (TableID table_id : tables) {
        //   auto& w_table = ws.get_table(table_id);
        //   std::sort(w_table.begin(), w_table.end(),
        //             [](const auto& lhs, const auto& rhs) {
        //               return lhs.first <= rhs.first;
        //             });
        //   for (auto w_iter = w_table.begin(); w_iter != w_table.end(); ++w_iter)
        //   {
        //     LOG_DEBUG("     LOCK (t: %lu, k: %lu)", table_id, w_iter->first);
        //     auto rw_iter = w_iter->second;
        //     Value* val = rw_iter->second.val;
        //     val->lock();
        //     if (val->is_detached_from_tree()) {
        //       remove_already_inserted(table_id, w_iter->first, true);
        //       unlock_writeset(table_id, w_iter->first, false);
        //       return false;
        //     }
        //     auto rwt = rw_iter->second.rwt;
        //     bool is_new = rw_iter->second.is_new;
        //     if (rwt == ReadWriteType::INSERT && is_new) {
        //       // On INSERT(is_new=true), insert the record to shared index
        //       NodeInfo ni;
        //       auto res = idx.insert(table_id, w_iter->first, val, ni);
        //       if (res == Index::Result::NOT_INSERTED) {
        //         remove_already_inserted(table_id, w_iter->first, true);
        //         unlock_writeset(table_id, w_iter->first, false);
        //         return false;
        //       } else if (res == Index::Result::OK) {
        //         // to prevent phantoms, abort if timestamp of the node is larger
        //         // than start_ts
        //         LeafNode* leaf = reinterpret_cast<LeafNode*>(ni.node);
        //         if (leaf->get_ts() > start_ts) {
        //           remove_already_inserted(table_id, w_iter->first, false);
        //           unlock_writeset(table_id, w_iter->first, false);
        //           return false;
        //         }
        //       }
        //     } else if (rwt == ReadWriteType::INSERT && !is_new) {
        //       // On INSERT(is_new=false), check the latest version timestamp and
        //       // whether it is deleted
        //       uint64_t read_ts = val->version->read_ts;
        //       uint64_t write_ts = val->version->write_ts;
        //       bool deleted = val->version->deleted;
        //       if (read_ts > start_ts || write_ts > start_ts || !deleted) {
        //         remove_already_inserted(table_id, w_iter->first, true);
        //         unlock_writeset(table_id, w_iter->first, false);
        //         return false;
        //       }
        //     } else if (rwt == ReadWriteType::UPDATE ||
        //                rwt == ReadWriteType::DELETE) {
        //       // On UPDATE/DELETED, check the latest version timestamp and
        //       whether
        //       // it is not deleted
        //       uint64_t read_ts = val->version->read_ts;
        //       uint64_t write_ts = val->version->write_ts;
        //       bool deleted = val->version->deleted;
        //       if (read_ts > start_ts || write_ts > start_ts || deleted) {
        //         remove_already_inserted(table_id, w_iter->first, true);
        //         unlock_writeset(table_id, w_iter->first, false);
        //         return false;
        //       }
        //     }
        //   }
        // }

        // LOG_INFO("APPLY CHANGES TO INDEX");
        // // Apply changes to index
        // for (TableID table_id : tables) {
        //   auto& w_table = ws.get_table(table_id);
        //   for (auto w_iter = w_table.begin(); w_iter != w_table.end(); ++w_iter)
        //   {
        //     auto rw_iter = w_iter->second;
        //     Value* val = rw_iter->second.val;
        //     if (!rw_iter->second.is_new) {
        //       Version* version = reinterpret_cast<Version*>(
        //           MemoryAllocator::aligned_allocate(sizeof(Version)));
        //       version->read_ts = start_ts;
        //       version->write_ts = start_ts;
        //       version->prev = val->version;
        //       version->rec = rw_iter->second.write_rec;
        //       version->deleted = (rw_iter->second.rwt == ReadWriteType::DELETE);
        //       val->version = version;
        //     }
        //     gc_version_chain(val);
        //     val->unlock();
        //   }
        // }
        return true;
    }

    void abort() {
        // for (TableID table_id : tables) {
        //   auto& rw_table = rws.get_table(table_id);
        //   auto& w_table = ws.get_table(table_id);
        //   for (auto w_iter = w_table.begin(); w_iter != w_table.end(); ++w_iter)
        //   {
        //     auto rw_iter = w_iter->second;
        //     auto rwt = rw_iter->second.rwt;
        //     bool is_new = rw_iter->second.is_new;
        //     if (rwt == ReadWriteType::INSERT && is_new) {
        //       Value* val = rw_iter->second.val;  // This points to locally
        //       allocated
        //                                          // value when Insert(is_new =
        //                                          true)
        //       if (val->version) {
        //         // if version is not a nullptr, this means that no attempts have
        //         // been made to insert val to index. Thus, the version is not
        //         // touched and not deallocated. Otherwise version is already
        //         // deallocated by the remove_already_inserted function
        //         MemoryAllocator::deallocate(val->version->rec);
        //         MemoryAllocator::deallocate(val->version);
        //         MemoryAllocator::deallocate(val);
        //       }
        //     } else {
        //       MemoryAllocator::deallocate(rw_iter->second.write_rec);
        //     }
        //   }
        //   rw_table.clear();
        //   w_table.clear();
        // }
        // tables.clear();
    }

    uint64_t core_id_;
    TxID txid_;
    // ここはRowRegion(num_of_thread)で定義すればいいと思います
    std::vector<RowRegion>& regions_;
    uint64_t regions_tail_;
    int cur_ptr_;

private:
    std::set<TableID> tables;
    ReadWriteSet<Key, Value> rws;  // read write set
    WriteSet<Key, Value> ws;       // write set

    Version* append_to_global_version_chain_00(Value* val) {
        Version* new_version = create_pending_version();

        // Append pending version
        new_version->prev = val->version;
        val->version = new_version;

        val.unlock();
        return new_version;
    }

    Version* install_ptr_to_row_region_10(Value* val) {
        RowRegion* return_value;
        RowRegion* my_region = regions_[cur_ptr_];

        // Install pointer to per-core version array by __atomic_exchange
        // stores the contents of my_region into val->ptr_to_version_array.
        // The original value of val->ptr_to_version_array is copied into
        // return_value.
        __atomic_exchange(val->ptr_to_version_array, my_region, return_value, __ATOMIC_SEQ_CST);

        Version* pending_version = create_pending_version();

        // current thread is the first thread to assign per-core version region of
        // the data item CAS is successful, rn val->ptr_to_version_array is
        // my_region
        if (return_value == nullptr) {
            cur_ptr_++;  // move cur_ptr_ to next slot
            my_region->arrays_[core_id_]->append_to_slots(pending_version);
            return pending version;
        }

        // other thread already assigned per-core version region of the data item
        // CAS is unsuccessful, val->ptr_to_version_array has some other addresss
        // use ptr which other thread already assigned
        return_value->arrays_[core_id_]->append_to_slots(pending_version);

        return pending_version;
    }

    Version* update_row_region__1() {
        RowRegion* my_region = regions_[cur_ptr_];

        // Create pending version, update 2 bitmaps and append to per-core version
        // array
        Version* pending_version = create_pending_version();
        my_region->arrays_[core_id_]->append_to_slots(pending_version);
        return pending_version;
    }

    Version* create_pending_version() {
        Version* version =
            reinterpret_cast<Version*>(MemoryAllocator::aligned_allocate(sizeof(Version)));
        version->prev = nullptr;
        version->rec = nullptr;  // TODO
        version->deleted = false;
        version->status = VersionStatus::PENDING;
        return version;
    }

    void remove_already_inserted(TableID end_table_id, Key end_key, bool end_exclusive) {
        Index& idx = Index::get_index();
        for (TableID table_id: tables) {
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
                if (!end_exclusive && table_id == end_table_id && key == end_key) return;
            }
        }
    }

    void unlock_writeset(TableID end_table_id, Key end_key, bool end_exclusive) {
        for (TableID table_id: tables) {
            auto& w_table = ws.get_table(table_id);
            for (auto w_iter = w_table.begin(); w_iter != w_table.end(); ++w_iter) {
                if (end_exclusive && table_id == end_table_id && w_iter->first == end_key) return;
                auto rw_iter = w_iter->second;
                Value* val = rw_iter->second.val;
                val->unlock();
                if (!end_exclusive && table_id == end_table_id && w_iter->first == end_key) return;
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