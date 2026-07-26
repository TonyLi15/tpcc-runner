#!/usr/bin/env python3
"""Apply the lock-contention instrumentation (LOCK_STAT) to tpcc-runner.

Run from the tpcc-runner repository root on cygnus:

    python3 apply_lock_stat.py

It performs anchored, idempotent edits (safe to run twice) and reports the
result for each file. Nothing is instrumented unless you later build with
-DLOCK_STAT=ON, so the normal build and the main results are unaffected.
"""
import os, sys

def patch(path, replacements, marker):
    if not os.path.isfile(path):
        print(f"  [MISS] {path}  (file not found -- check your tree)"); return False
    s = open(path, encoding="utf-8").read()
    if marker in s:
        print(f"  [SKIP] {path}  (already patched)"); return True
    for old, new in replacements:
        if old not in s:
            print(f"  [FAIL] {path}  (anchor not found -- patch manually)\n"
                  f"         missing anchor starts: {old[:60]!r}"); return False
        s = s.replace(old, new, 1)
    open(path, "w", encoding="utf-8").write(s)
    print(f"  [ OK ] {path}"); return True

def write_new(path, content):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w", encoding="utf-8").write(content)
    print(f"  [ NEW] {path}")

LOCK_STAT_HPP = '''#pragma once

#include <cstdint>

// Thread-local lock-contention counters.
//
// These are populated only when the project is compiled with -DLOCK_STAT
// (see RWLock in protocols/common/readwritelock.hpp) and are flushed into each
// worker's Stat at the end of run_tx(). Without -DLOCK_STAT they add no code and
// no overhead, so the main performance results are unaffected.
//
//   tl_lock_acquire : number of successful exclusive lock acquisitions
//   tl_lock_contend : acquisitions that found the lock already held (contention)
//   tl_lock_hold    : cumulative TSC cycles the exclusive lock was held
//
// Contention rate = tl_lock_contend / tl_lock_acquire.
// Mean hold time  = tl_lock_hold    / tl_lock_acquire  (in TSC cycles).
inline thread_local uint64_t tl_lock_acquire = 0;
inline thread_local uint64_t tl_lock_contend = 0;
inline thread_local uint64_t tl_lock_hold = 0;
inline thread_local uint64_t tl_lock_hold_start = 0;  // scratch: TSC at last acquisition

// Hold-time timestamp source. In the default LOCK_STAT build this is rdtscp().
// Under -DLOCK_STAT_COUNTS_ONLY the rdtscp() is compiled out (returns 0) so the
// acquire/contend COUNTS are still gathered but tl_lock_hold stays 0. This is
// needed for cheetah at extreme skew (>=0.9, write-only), where the serializing
// rdtscp inside the exclusive critical section amplifies hold time enough to
// livelock the run. Counts do not need a timestamp, so counts-only completes.
#if defined(LOCK_STAT) && !defined(LOCK_STAT_COUNTS_ONLY)
#define LOCK_STAT_TSC() rdtscp()
#else
#define LOCK_STAT_TSC() (uint64_t)0
#endif
'''

def main():
    ok = True
    print("Applying LOCK_STAT instrumentation...")

    write_new("utils/lock_stat.hpp", LOCK_STAT_HPP)

    # ---- protocols/common/readwritelock.hpp ---------------------------------
    ok &= patch("protocols/common/readwritelock.hpp", [
        ('#include "utils/atomic_wrapper.hpp"\n#include "utils/logger.hpp"\n\nclass RWLock {',
         '#include "utils/atomic_wrapper.hpp"\n#include "utils/logger.hpp"\n\n'
         '#ifdef LOCK_STAT\n#include "utils/lock_stat.hpp"\n#include "utils/tsc.hpp"\n#endif\n\nclass RWLock {'),
        ('''  void lock_upgrade() {
    // assume that slock is taken before exclusive lock
    int64_t expected;
    while (true) {
      expected = load_acquire(cnt);
      if (expected == 1 && compare_exchange(cnt, expected, -1)) {
        return;
      }
    }
  }''',
         '''  void lock_upgrade() {
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
  }'''),
        ('''  void lock() {
    int64_t expected;
    while (true) {
      expected = load_acquire(cnt);
      if (expected == 0 && compare_exchange(cnt, expected, -1)) {
        return;
      }
    }
  }''',
         '''  void lock() {
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
  }'''),
        ('''  bool try_lock_upgrade() {
    // assume that slock is taken before lock upgrade
    int64_t expected = load_acquire(cnt);
    if (expected == 1 && compare_exchange(cnt, expected, -1)) {
      return true;
    } else {
      return false;
    }
  }''',
         '''  bool try_lock_upgrade() {
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
  }'''),
        ('''  bool try_lock() {
    int64_t expected = load_acquire(cnt);
    if (expected == 0 && compare_exchange(cnt, expected, -1)) {
      return true;
    } else {
      return false;
    }
  }''',
         '''  bool try_lock() {
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
  }'''),
        ('  void unlock_shared() { fetch_add(cnt, -1); }\n\n  void unlock() { fetch_add(cnt, 1); }',
         '''  void unlock_shared() { fetch_add(cnt, -1); }

  void unlock() {
#ifdef LOCK_STAT
    tl_lock_hold += LOCK_STAT_TSC() - tl_lock_hold_start;
#endif
    fetch_add(cnt, 1);
  }'''),
    ], marker="#ifdef LOCK_STAT")

    # ---- benchmarks/ycsb/include/tx_utils.hpp -------------------------------
    ok &= patch("benchmarks/ycsb/include/tx_utils.hpp", [
        ('''    WaitInGC,
    PerfLeader,
    PerfMember,
    Size
  };''',
         '''    WaitInGC,
    PerfLeader,
    PerfMember,
    LockAcquire,
    LockContend,
    LockHold,
    Size
  };'''),
        ('''      "WaitInGC",
      "PerfLeader",
      "PerfMember",
  };''',
         '''      "WaitInGC",
      "PerfLeader",
      "PerfMember",
      "LockAcquire",
      "LockContend",
      "LockHold",
  };'''),
    ], marker="LockAcquire")

    # ---- executables (include + flush at end of run_tx) ---------------------
    flush = '''
#ifdef LOCK_STAT
  // Flush this worker's thread-local lock-contention counters into its Stat.
  t_data.stat.add(Stat::MeasureType::LockAcquire, tl_lock_acquire);
  t_data.stat.add(Stat::MeasureType::LockContend, tl_lock_contend);
  t_data.stat.add(Stat::MeasureType::LockHold, tl_lock_hold);
#endif
'''
    ok &= patch("executables/ycsb_serval.cpp", [
        ('#include "protocols/serval/include/major_gc.hpp"',
         '#include "protocols/serval/include/major_gc.hpp"\n#include "utils/lock_stat.hpp"'),
        ('  t_data.stat.record(Stat::MeasureType::Sync2Time, sync2_total);\n\n'
         '  // t_data.stat.record(Stat::MeasureType::PerfLeader,',
         '  t_data.stat.record(Stat::MeasureType::Sync2Time, sync2_total);\n' + flush +
         '\n  // t_data.stat.record(Stat::MeasureType::PerfLeader,'),
    ], marker="LockAcquire, tl_lock_acquire")

    ok &= patch("executables/ycsb_caracal.cpp", [
        ('#include "protocols/caracal/include/caracal.hpp"',
         '#include "protocols/caracal/include/caracal.hpp"\n#include "utils/lock_stat.hpp"'),
        ('  t_data.stat.record(Stat::MeasureType::Sync2Time, sync2_total);\n'
         '  // t_data.stat.record(Stat::MeasureType::PerfLeader,',
         '  t_data.stat.record(Stat::MeasureType::Sync2Time, sync2_total);\n' + flush +
         '\n  // t_data.stat.record(Stat::MeasureType::PerfLeader,'),
    ], marker="LockAcquire, tl_lock_acquire")

    ok &= patch("executables/ycsb_cheetah.cpp", [
        ('#include "protocols/cheetah/include/value.hpp"',
         '#include "protocols/cheetah/include/value.hpp"\n#include "utils/lock_stat.hpp"'),
        ('  t_data.stat.record(Stat::MeasureType::ExecutionTime, exec_total);\n\n'
         '  // t_data.stat.record(Stat::MeasureType::PerfLeader,',
         '  t_data.stat.record(Stat::MeasureType::ExecutionTime, exec_total);\n' + flush +
         '\n  // t_data.stat.record(Stat::MeasureType::PerfLeader,'),
    ], marker="LockAcquire, tl_lock_acquire")

    # ---- CMakeLists.txt (LOCK_STAT option) ----------------------------------
    ok &= patch("CMakeLists.txt", [
        ('''message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

list(APPEND COMMON_COMPILE_FLAGS "-Werror" "-Wall" "-Wextra" "-fPIC")''',
         '''message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

# Lock-contention instrumentation (RWLock acquire/contend/hold counters).
# OFF by default so normal builds and the main results are unaffected; enable
# with -DLOCK_STAT=ON for the white-box lock-contention experiment.
option(LOCK_STAT "Enable lock-contention instrumentation in RWLock" OFF)
if(LOCK_STAT)
    add_definitions(-DLOCK_STAT)
    message(STATUS "LOCK_STAT: ON (lock-contention instrumentation enabled)")
endif()
# Counts-only mode: keep acquire/contend counts but compile out the rdtscp
# hold-timing (tl_lock_hold stays 0). Needed for cheetah at extreme skew where
# the serializing rdtscp in the critical section livelocks the run.
option(LOCK_STAT_COUNTS_ONLY "Drop rdtscp hold-timing, keep lock counts" OFF)
if(LOCK_STAT_COUNTS_ONLY)
    add_definitions(-DLOCK_STAT_COUNTS_ONLY)
    message(STATUS "LOCK_STAT_COUNTS_ONLY: ON (hold-timing disabled)")
endif()

list(APPEND COMMON_COMPILE_FLAGS "-Werror" "-Wall" "-Wextra" "-fPIC")'''),
    ], marker="option(LOCK_STAT")

    # ---- scripts/ycsb_tony.py (pass -DLOCK_STAT when env LOCK_STAT set) ------
    ok &= patch("scripts/ycsb_tony.py", [
        ('            + " -DRC="\n            + rc\n            + " > ./log/"',
         '            + " -DRC="\n            + rc\n'
         '            + (" -DLOCK_STAT=ON" if os.environ.get("LOCK_STAT") else "")\n'
         '            + " > ./log/"'),
    ], marker='-DLOCK_STAT=ON')

    print("\nDONE." if ok else "\nSOME FILES NEED MANUAL ATTENTION (see FAIL/MISS above).")
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
