#pragma once

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
