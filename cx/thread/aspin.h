#pragma once

#include <cx/math/lcg.h>
#include <cx/thread/atomic.h>
#include <cx/time/clock.h>
#include <cx/time/time.h>
#include <cx/platform/cpu.h>
#include <cx/platform/os.h>

// Common functions for adaptive spin threading primitives

#define ASPIN_MAX_USEC 10                    // hard cap in microseconds
#define ASPIN_INITIAL_TARGET 500             // initial number of cycles to spin

#if DEBUG_LEVEL >= 2
#define ASPIN_PERF_STATS
#endif

typedef struct AdaptiveSpin
{
    atomic(int32) spintarget;
#ifdef ASPIN_PERF_STATS
    atomic(intptr) stats_uncontended;       // uncontended acquisition of primitive
    atomic(intptr) stats_spin;              // times primitive was acquired after spinning
    atomic(intptr) stats_futex;             // times primitive was acquired on non-spin (futex) path
    atomic(intptr) stats_capped;            // times spin loop was capped by ASPIN_MAX_USEC
    atomic(intptr) stats_timeout;           // times the primitive failed due to timeout
    atomic(intptr) stats_yield;             // times the primitive yielded CPU to reduce contention
#endif
} AdaptiveSpin;

typedef struct AdapativeSpinState
{
    int64 now;
    int64 start;
    int64 endtime;
    int64 spincap;
    int32 curtarget;
    int32 spincount;
    int32 contention;
    uint32 rstate;
} AdaptiveSpinState;

_meta_inline void aspinRecordUncontended(AdaptiveSpin *aspin)
{
#ifdef ASPIN_PERF_STATS
    atomicFetchAdd(intptr, &aspin->stats_uncontended, 1, Relaxed);
#endif
}

_meta_inline void aspinRecordSpin(AdaptiveSpin *aspin)
{
#ifdef ASPIN_PERF_STATS
    atomicFetchAdd(intptr, &aspin->stats_spin, 1, Relaxed);
#endif
}

_meta_inline void aspinRecordFutex(AdaptiveSpin *aspin)
{
#ifdef ASPIN_PERF_STATS
    atomicFetchAdd(intptr, &aspin->stats_futex, 1, Relaxed);
#endif
}

_meta_inline void aspinRecordCapped(AdaptiveSpin *aspin)
{
#ifdef ASPIN_PERF_STATS
    atomicFetchAdd(intptr, &aspin->stats_capped, 1, Relaxed);
#endif
}

_meta_inline void aspinRecordTimeout(AdaptiveSpin *aspin)
{
#ifdef ASPIN_PERF_STATS
    atomicFetchAdd(intptr, &aspin->stats_timeout, 1, Relaxed);
#endif
}

_meta_inline void aspinRecordYield(AdaptiveSpin *aspin)
{
#ifdef ASPIN_PERF_STATS
    atomicFetchAdd(intptr, &aspin->stats_yield, 1, Relaxed);
#endif
}

_meta_inline void aspinInit(AdaptiveSpin *aspin, bool nospin)
{
    memset(aspin, 0, sizeof(AdaptiveSpin));

    // never spin if there's only a single core, just a waste of CPU
    if (osPhysicalCPUs() == 1)
        nospin = true;

    atomicStore(int32, &aspin->spintarget, nospin ? -1 : ASPIN_INITIAL_TARGET, Relaxed);
}

_meta_inline void aspinBegin(AdaptiveSpin *aspin, AdaptiveSpinState *ass, int64 timeout)
{
    ass->now = clockTimer();
    ass->start = ass->now;
    ass->spincap = ass->start + ASPIN_MAX_USEC;
    ass->endtime = (timeout == timeForever) ? timeForever : ass->start + timeout;
    ass->curtarget = atomicLoad(int32, &aspin->spintarget, Relaxed);
    ass->spincount = (ass->curtarget == -1) ? 0 : ass->curtarget * 2;       // -1 == nospin
    ass->contention = 0;
    ass->rstate = (ass->now & 0xffffffff);
}

_meta_inline bool aspinSpin(AdaptiveSpin *aspin, AdaptiveSpinState *ass)
{
    // check if we hit the hard cap on spin time
    if (ass->spincount > 0 && ass->now > ass->spincap) {
        ass->spincount = 0;
        aspinRecordCapped(aspin);
    }

    if (ass->spincount > 0) {
        --ass->spincount;
        _CPU_PAUSE;
        return true;
    }

    return false;
}

_meta_inline bool aspinTimeout(AdaptiveSpin *aspin, AdaptiveSpinState *ass)
{
    ass->now = clockTimer();
    if (ass->now > ass->endtime) {
        aspinRecordTimeout(aspin);
        return true;
    }
    return false;
}

_meta_inline void aspinAdapt(AdaptiveSpin *aspin, AdaptiveSpinState *ass)
{
    // don't adapt if we timed out entirely
    if (ass->now > ass->endtime)
        return;

    if (ass->curtarget != -1) {
        if (ass->spincount > 0) {
            // adjust adaptive target based on how much spinning we did
            atomicFetchAdd(int32, &aspin->spintarget, (ass->curtarget - ass->spincount) / 8 + 1, Relaxed);
            aspinRecordSpin(aspin);
        } else {
            if (ass->now <= ass->spincap) {
                // we had to go to futuxes, give it a big boost
                atomicFetchAdd(int32, &aspin->spintarget, ass->curtarget + 2, Relaxed);
            }
            aspinRecordFutex(aspin);
        }
    } else {
        aspinRecordFutex(aspin);
    }
}

_meta_inline int64 aspinTimeoutRemaining(AdaptiveSpinState *ass)
{
    return (ass->endtime == timeForever) ? timeForever : ass->endtime - ass->now;
}

// call this function when there is contention on a CAS
_meta_inline void aspinHandleContention(AdaptiveSpin *aspin, AdaptiveSpinState *ass)
{
    // This algorithm is cruicial to maintaining high performance even under extreme contention.
    // Earlier versions of these primitives started to suffer degredation when many concurrent
    // threads attempted to get the mutex/etc simultaneously. They would spend a lot of time
    // spinning on a CAS on the underlying atomic, attempting to get into a state where they
    // could wait with a futex/semaphore.

    // By calling this function after a failed CAS, a count of failures is maintained. As the
    // count increases, so does the probability that the thread will yield its quantum, reducing
    // contention enough for another thread to succeed on the CAS and break the cycle.

    // We use a poor quality but fast LCG pseudorandom number generator that is good enough
    // for this purpose.

    if (lcgRandom(&ass->rstate) % ++ass->contention > 0) {
        aspinRecordYield(aspin);
        osYield();
    }
}
