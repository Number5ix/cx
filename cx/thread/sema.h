#pragma once

#include <cx/cx.h>
#include <cx/platform/ksema.h>
#include <cx/thread/atomic.h>
#include <cx/time/time.h>

// Adaptive Half-Spin Semaphore (AHSS)

// See comments and attribution in sema.c

#if DEBUG_LEVEL >= 2
#define SEMA_PERF_STATS
#endif

enum SEMA_Flags {
    SEMA_NoSpin            = 0x00000001,
    SEMA_PlatformEvents    = 0x00000002,        // wait may be interrupted by platform-specific events
    SEMA_KSemaInit         = 0x10000000,
    SEMA_KSemaFallback     = 0x20000000,
    SEMA_KSemaInitProgress = 0x80000000,
};

typedef struct Semaphore {
    atomic(int32) count;
    int32 _align;
    atomic(int32) spintarget;
    uint32 flags;
    kernelSema ksema;

#ifdef SEMA_PERF_STATS
    atomic(intptr) stats_uncontested;
    atomic(intptr) stats_spin;
    atomic(intptr) stats_yield;
    atomic(intptr) stats_kernel;
#endif
} Semaphore;

bool semaInit(Semaphore *sema, int32 count);
bool semaDestroy(Semaphore *sema);
bool _semaContendedDec(Semaphore *sema, int64 timeout);

_meta_inline bool semaTryDec(Semaphore *sema)
{
    int32 curcount = atomicLoad(int32, &sema->count, Relaxed);
    bool ret = (curcount > 0 && atomicCompareExchange(int32, strong, &sema->count, &curcount,
                                                      curcount - 1, Acquire, Acquire));
#ifdef SEMA_PERF_STATS
    if (ret)
        atomicFetchAdd(intptr, &sema->stats_uncontested, 1, Relaxed);
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
    int32 oldcount = atomicFetchAdd(int32, &sema->count, count, Release);
    int32 kwaiters = -oldcount < count ? -oldcount : count;
    if (kwaiters > 0) {
        // shouldn't be possible to get here (negative oldcount) without KSemaInit
        devAssert(sema->flags & SEMA_KSemaInit);
        kernelSemaInc(&sema->ksema, kwaiters);
    }
    return true;
}
