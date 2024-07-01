#pragma once

#include <cstdint>
#include <mutex>

#include "protocols/common/readwritelock.hpp"
#include "protocols/serval/include/row_region.hpp"
#include "utils/atomic_wrapper.hpp"

template <typename Version_> struct Value {
    using Version = Version_;
    alignas(64) RWLock rwl;
    uint64_t epoch_ = 0;

    Version *master_ = nullptr; // final state

    GlobalVersionArray global_array_; // Global Version Array

    // For contended versions
    RowRegion *row_region_ = nullptr; // Pointer to per-core version array

    bool has_dirty_region() {
        if (__atomic_load_n(&row_region_, __ATOMIC_SEQ_CST)) { // TODO: 再考
            return row_region_->is_dirty();
        }
        return false;
    }

    void initialize() { rwl.initialize(); }

    void lock() { rwl.lock(); }

    bool try_lock() { return rwl.try_lock(); }

    void unlock() { rwl.unlock(); }
};
