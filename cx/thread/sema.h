#pragma once

#include <cx/cx.h>
#include <cx/platform/ksema.h>
#include <cx/thread/atomic.h>
#include <cx/time/time.h>

#if DEBUG_LEVEL >= 2
#define SEMA_PERF_STATS
#endif

enum SEMA_Flags {
    SEMA_NoSpin            = 0x00000001,
    SEMA_KSemaInit         = 0x10000000,
    SEMA_KSemaFallback     = 0x20000000,
    SEMA_KSemaInitProgress = 0x80000000,
};

typedef struct Semaphore {
    atomic_int32 count;
    atomic_int32 spintarget;
    uint32 flags;
    kernelSema ksema;

#ifdef SEMA_PERF_STATS
    atomic_int64 stats_uncontested;
    atomic_int64 stats_spin;
    atomic_int64 stats_yield;
    atomic_int64 stats_kernel;
#endif
} Semaphore;

bool semaInit(Semaphore *sema, int32 count);
bool semaDestroy(Semaphore *sema);
bool _semaContendedDec(Semaphore *sema, int64 timeout);

_meta_inline bool semaTryDec(Semaphore *sema)
{
    int32 curcount = atomic_load_int32(&sema->count, ATOMIC_RELAXED);
    bool ret = (curcount > 0 && atomic_compare_exchange_strong_int32(&sema->count, &curcount,
                                                                 curcount - 1, ATOMIC_ACQUIRE,
                                                                 ATOMIC_ACQUIRE));
#ifdef SEMA_PERF_STATS
    if (ret)
        atomic_fetch_add_int64(&sema->stats_uncontested, 1, ATOMIC_RELAXED);
#endif
    return ret;
}

_meta_inline bool semaDec(Semaphore *sema)
{
    // try lightweight no-contention path inline
    if (semaTryDec(sema))
        return true;

    return _semaContendedDec(sema, timeForever);
}

_meta_inline bool semaTryDecTimeout(Semaphore *sema, int64 timeout)
{
    // try lightweight no-contention path inline
    if (semaTryDec(sema))
        return true;

    return _semaContendedDec(sema, timeout);
}

_meta_inline bool semaInc(Semaphore *sema, int32 count)
{
    int32 oldcount = atomic_fetch_add_int32(&sema->count, count, ATOMIC_RELEASE);
    int32 kwaiters = -oldcount < count ? -oldcount : count;
    if (kwaiters > 0) {
        // shouldn't be possible to get here (negative oldcount) without KSemaInit
        devAssert(sema->flags & SEMA_KSemaInit);
        kernelSemaInc(&sema->ksema, kwaiters);
    }
    return true;
}
