#pragma once

#include <cassert>
#include <unordered_map>

#include "utils/atomic_wrapper.hpp"
#include "utils/logger.hpp"

#ifdef LOCK_STAT
#include "utils/lock_stat.hpp"
#include "utils/tsc.hpp"
#endif

class RWLock {
 public:
  RWLock() : cnt(0) {}

  void initialize() { cnt = 0; }

  void lock_shared() {
    int64_t expected;
    while (true) {
      expected = load_acquire(cnt);
      if (expected >= 0 && compare_exchange(cnt, expected, expected + 1)) {
        return;
      }
    }
  }

  void lock_upgrade() {
    // assume that slock is taken before exclusive lock
    int64_t expected;
#ifdef LOCK_STAT
    bool busy = false;
#endif
    while (true) {
      expected = load_acquire(cnt);
      if (expected == 1 && compare_exchange(cnt, expected, -1)) {
#ifdef LOCK_STAT
        ++tl_lock_acquire;
        if (busy) ++tl_lock_contend;
        tl_lock_hold_start = LOCK_STAT_TSC();
#endif
        return;
      }
#ifdef LOCK_STAT
      busy = true;
#endif
    }
  }

  void lock() {
    int64_t expected;
#ifdef LOCK_STAT
    bool busy = false;
#endif
    while (true) {
      expected = load_acquire(cnt);
      if (expected == 0 && compare_exchange(cnt, expected, -1)) {
#ifdef LOCK_STAT
        ++tl_lock_acquire;
        if (busy) ++tl_lock_contend;
        tl_lock_hold_start = LOCK_STAT_TSC();
#endif
        return;
      }
#ifdef LOCK_STAT
      busy = true;
#endif
    }
  }

  bool try_lock_shared() {
    int64_t expected = load_acquire(cnt);
    while (expected >= 0) {
      if (compare_exchange(cnt, expected, expected + 1)) {
        return true;
      }
    }
    return false;
  }

  bool try_lock_upgrade() {
    // assume that slock is taken before lock upgrade
    int64_t expected = load_acquire(cnt);
    if (expected == 1 && compare_exchange(cnt, expected, -1)) {
#ifdef LOCK_STAT
      ++tl_lock_acquire;
      tl_lock_hold_start = LOCK_STAT_TSC();
#endif
      return true;
    } else {
#ifdef LOCK_STAT
      ++tl_lock_contend;
#endif
      return false;
    }
  }

  bool try_lock() {
    int64_t expected = load_acquire(cnt);
    if (expected == 0 && compare_exchange(cnt, expected, -1)) {
#ifdef LOCK_STAT
      ++tl_lock_acquire;
      tl_lock_hold_start = LOCK_STAT_TSC();
#endif
      return true;
    } else {
#ifdef LOCK_STAT
      ++tl_lock_contend;
#endif
      return false;
    }
  }

  void unlock_shared() { fetch_add(cnt, -1); }

  void unlock() {
#ifdef LOCK_STAT
    tl_lock_hold += LOCK_STAT_TSC() - tl_lock_hold_start;
#endif
    fetch_add(cnt, 1);
  }

  int64_t get_cnt() {return load_acquire(cnt);}

 private:
  int64_t cnt = 0;
};
