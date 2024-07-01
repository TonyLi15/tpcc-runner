#pragma once

#include <algorithm> // for find()
#include <unistd.h>  // for gettid()

#include "protocols/caracal/include/readwriteset.hpp"
#include "protocols/caracal/include/version.hpp"
#include "protocols/common/readwritelock.hpp"
#include "protocols/ycsb_common/definitions.hpp"
#include "utils/bitmap.hpp"
#include "utils/numa.hpp"

/*
  global id:
  upper 32 bits: epoch number
  lower 32 bits: serialization id in the epoch
*/

static uint64_t convert_to_global_id(uint64_t epoch, uint64_t serial_id) {
    return epoch << 32 | serial_id;
}

// TODO: 効率化
[[maybe_unused]] static uint64_t get_serial_id(uint64_t global_id) {
    return global_id & ~fill_the_left_side_until_before_the_given_position(32);
}

static uint64_t get_epoch(uint64_t global_id) { return global_id >> 32; }

class PerCoreBuffer { // Caracal's per-core buffer
  public:
    // use pair data structure
    std::vector<std::pair<uint64_t, Version *>> ids_slots_; // ascending order

    bool appendable(Version *version, uint64_t epoch, uint64_t serial_id) {
        assert(version);

        uint64_t global_id = convert_to_global_id(epoch, serial_id);

        // append to per-core buffer
        assert(ids_slots_.size() < MAX_SLOTS_OF_PER_CORE_BUFFER);
        // Find the position to append the version
        auto it = std::upper_bound(ids_slots_.begin(), ids_slots_.end(),
                                   std::make_pair(global_id, nullptr),
                                   [](const auto &pair, const auto &new_pair) {
                                       return pair.first < new_pair.first;
                                   }); // ascending order
        // Append to the corresponding position
        ids_slots_.insert(it, std::make_pair(global_id, version));

        if (ids_slots_.size() < MAX_SLOTS_OF_PER_CORE_BUFFER)
            return true; // the buffer is not full
        return false;    // the buffer is full
    }

    void initialize() { ids_slots_.clear(); }
};

class GlobalVersionArray {
  public:
    RWLock rwl;

    std::vector<std::pair<uint64_t, Version *>> ids_slots_; // ascending order

    void lock() { rwl.lock(); }

    bool try_lock() { return rwl.try_lock(); }

    void unlock() { rwl.unlock(); }

    void batch_append(PerCoreBuffer *buffer, uint64_t epoch_for_gc) {
        for (uint64_t i = 0; i < buffer->ids_slots_.size(); i++) {
            append_with_gc(buffer->ids_slots_[i].second,
                           buffer->ids_slots_[i].first, epoch_for_gc);
        }
        buffer->initialize();
    }

    // for debug
    bool is_exist(uint64_t epoch, uint64_t serial_id) {
        uint64_t global_id = convert_to_global_id(epoch, serial_id);

        // Use std::find with a custom comparator
        auto it = std::find_if(
            ids_slots_.begin(), ids_slots_.end(),
            [global_id](const auto &pair) { return pair.first == global_id; });
        return it != ids_slots_.end();
    }

    // これに失敗すると、GCにバグがある可能性が高い
    bool is_exist_visible_version(uint64_t global_id) {
        if (global_id != 0) {
            auto result = std::find_if(ids_slots_.begin(), ids_slots_.end(),
                                       [global_id](const auto &pair) {
                                           return pair.first < global_id;
                                       });
            if (result == ids_slots_.end()) {
                return false;
            }
        }
        return true;
    }

    bool is_visible(uint64_t global_id, uint64_t visible_id) {
        if (get_epoch(visible_id) < get_epoch(global_id)) {
            return true;
        }
        assert(get_epoch(visible_id) == get_epoch(global_id));
        assert(get_serial_id(visible_id) < get_serial_id(global_id));
        return true;
    }

    std::pair<uint64_t, Version *> search_visible_version(uint64_t epoch,
                                                          uint64_t serial_id) {
        uint64_t global_id = convert_to_global_id(epoch, serial_id);

        assert(!ids_slots_.empty());

        // =========== for debug ===========
        assert(is_exist_visible_version(global_id));
        // =========== for debug ===========

        // Find the first element whose id is greater than the given serial_id
        auto it = std::upper_bound(
            ids_slots_.begin(), ids_slots_.end(), global_id,
            [](uint64_t id, const auto &pair) { return id <= pair.first; });

        assert(it != ids_slots_.begin());
        // Move the iterator back to the previous element
        --it;
        assert(is_visible(global_id, it->first));

        // Return the tx_id and the corresponding Version* object
        return {it->first, it->second};
    }

    void append(Version *version, uint64_t epoch, uint64_t serial_id) {
        assert(version);
        append_with_gc(version, convert_to_global_id(epoch, serial_id), epoch);
    }
    void append_with_gc(Version *version, uint64_t global_id,
                        [[maybe_unused]] uint64_t epoch_for_gc) {
        assert(version);
        // Find the position to append the version
        auto it = std::upper_bound(ids_slots_.begin(), ids_slots_.end(),
                                   std::make_pair(global_id, nullptr),
                                   [](const auto &pair, const auto &new_pair) {
                                       return pair.first < new_pair.first;
                                   });

        // Append to the corresponding position
        ids_slots_.insert(it, std::make_pair(global_id, version));

        minor_gc(epoch_for_gc);
    }

    void gc(typename std::vector<std::pair<uint64_t, Version *>>::iterator itr,
            [[maybe_unused]] uint64_t epoch) {
        auto [id, version] = *itr;
        assert(id < epoch);
        assert(version);
        assert(version->status == Version::VersionStatus::STABLE);
        assert(version->rec);
        delete reinterpret_cast<Record *>(version->rec);
        delete version;
    }
    void minor_gc(uint64_t cur_e) {
        assert(!ids_slots_.empty());
        uint64_t cur_epoch = convert_to_global_id(cur_e, 0);
        assert(is_exist_visible_version(cur_epoch));
        auto itr = ids_slots_.begin();
        while (std::next(itr) != ids_slots_.end()) {
            if (cur_epoch <= std::next(itr)->first)
                break;
            gc(itr, cur_epoch);
            itr = ids_slots_.erase(itr);
        }
        assert(!ids_slots_.empty());
        assert(is_exist_visible_version(cur_epoch));
    }
    void major_gc(uint64_t old_e) {
        assert(!ids_slots_.empty());
        uint64_t old_epoch = convert_to_global_id(old_e, 0);
        auto itr = ids_slots_.begin();
        while (itr != ids_slots_.end()) {
            if (old_epoch < itr->first)
                break;
            gc(itr, old_epoch);
        }
    }

    bool is_empty() { return ids_slots_.empty(); }
};

class RowBuffer { // Caracal's per-core buffer
  public:
    PerCoreBuffer
        *buffers_[LOGICAL_CORE_SIZE]; // TODO: alignas(64) をつけるか検討

    RowBuffer() {
        pid_t main_tid = gettid(); // fetch the main thread's tid
        for (uint64_t core_id = 0; core_id < LOGICAL_CORE_SIZE; core_id++) {
            Numa numa(main_tid,
                      core_id); // move to the designated core from main thread
            // buffers_[core_id] =
            //     new (MemoryAllocator::allocate(sizeof(PerCoreBuffer)))
            //         PerCoreBuffer(); // TOOD: あってる？
            buffers_[core_id] = new PerCoreBuffer();
        }
    }

    ~RowBuffer() {
        for (uint64_t core_id = 0; core_id < LOGICAL_CORE_SIZE; core_id++) {
            // MemoryAllocator::deallocate(buffers_[core_id]);
            delete buffers_[core_id];
        }
    }
};

class RowBufferController {
  private:
    RowBuffer buffers_[NUM_REGIONS];
    RWLock lock_;
    uint64_t used_ = 0; // how many buffers currently used

  public:
    RowBuffer *fetch_new_buffer() {
        lock_.lock();
        assert(used_ < NUM_REGIONS);
        if (NUM_REGIONS <= used_) {
            throw std::runtime_error("NUM_REGIONS <= used_");
        }
        RowBuffer *buffer = &buffers_[used_]; // TODO: reconsider
        used_++;
        lock_.unlock();
        return buffer;
    }
};
