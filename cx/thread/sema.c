#include "sema.h"
#include <cx/time/clock.h>
#include <cx/platform/cpu.h>
#include <cx/platform/os.h>
#include <cx/utils/compare.h>

// customized version of lazyinit that uses shared flags
static void _semaLazyKInit(Semaphore *sema)
{
    // normally these never change, only need to be atomic during lazy init
    atomic_uint32 *flags = (atomic_uint32*)&sema->flags;

    uint32 oldflags = atomic_fetch_or_uint32(flags, SEMA_KSemaInitProgress, ATOMIC_ACQ_REL);
    if (!(oldflags & SEMA_KSemaInitProgress)) {
        // won the race
        if (!kernelSemaInit(&sema->ksema, 0))
            atomic_fetch_or_uint32(flags, SEMA_KSemaFallback, ATOMIC_ACQ_REL);
        atomic_fetch_or_uint32(flags, SEMA_KSemaInit, ATOMIC_ACQ_REL);
    } else {
        // lost the race, poor man's spinlock
        while (!(atomic_load_uint32(flags, ATOMIC_RELAXED) & SEMA_KSemaInit))
            _CPU_PAUSE;
    }
}

bool semaInit(Semaphore *sema, int32 count)
{
    devAssert(count >= 0);
    memset(sema, 0, sizeof(Semaphore));
    atomic_store_int32(&sema->count, count, ATOMIC_ACQ_REL);
    atomic_store_int32(&sema->spintarget, 10, ATOMIC_ACQ_REL);

    // TODO: Set SEMA_NoSpin on single-CPU machines

    return true;
}

bool semaDestroy(Semaphore *sema)
{
    devAssert(atomic_load_int32(&sema->count, ATOMIC_ACQ_REL) >= 0);

    if ((sema->flags & SEMA_KSemaInit) && !(sema->flags & SEMA_KSemaFallback))
        return kernelSemaDestroy(&sema->ksema);

    return true;
}

bool _semaContendedDec(Semaphore *sema, int64 timeout)
{
    bool spin = !(sema->flags & SEMA_NoSpin);
    bool timed = (timeout < timeForever);
    int32 curtarget = atomic_load_int32(&sema->spintarget, ATOMIC_RELAXED);
    int32 spincount = spin ? curtarget * 2 : 1;
    int64 endtime = timed ? clockTimer() + timeout : 0;
    int32 curcount;

    // spincount == 1 for the NoSpin case: assuming semaDec already tried the
    // contention-free path, this will yield the CPU once before falling back to
    // the kernel semaphore

    // fast path
    for (; spincount; --spincount) {
        _CPU_PAUSE;

        // start yielding the CPU once we're past the halfway point, to prevent CPU
        // starvation and runaway adaptation if more threads are spinning than
        // avilable cores
        if (spincount < curtarget)
            osYield();

        // try to decrement
        curcount = atomic_load_int32(&sema->count, ATOMIC_ACQUIRE);
        if (curcount > 0 && atomic_compare_exchange_strong_int32(&sema->count, &curcount,
                                                                 curcount - 1, ATOMIC_ACQUIRE,
                                                                 ATOMIC_RELAXED)) {
            // successfully decremented semaphore on fast path
            // adjust adaptive target if we had to spin, perfect synchronization
            // is not necessary
            if (spin)
                atomic_fetch_add_int32(&sema->spintarget, (curtarget - spincount) / 8, ATOMIC_RELAXED);

#ifdef SEMA_PERF_STATS
            if (spincount >= curtarget)
                atomic_fetch_add_int64(&sema->stats_spin, 1, ATOMIC_RELAXED);
            else
                atomic_fetch_add_int64(&sema->stats_yield, 1, ATOMIC_RELAXED);
#endif
            return true;
        }

        // if doing a timed wait, check that we didn't time out
        if (timed && clockTimer() > endtime)
            return false;
    }

    // slow path

    // update adaptive target, spincount zeroed out at this point
    if (spin)
        atomic_fetch_add_int32(&sema->spintarget, curtarget / 8, ATOMIC_RELAXED);

    // make sure kernel semaphore has been created
    if (!(sema->flags & SEMA_KSemaInit))
        _semaLazyKInit(sema);

    if (!(sema->flags & SEMA_KSemaFallback)) {
        bool ret = true;
        // allow count to go negative for threads that are waiting in the kernel
        curcount = atomic_fetch_sub_int32(&sema->count, 1, ATOMIC_ACQUIRE);

        // check if another thread incremented it after we gave up
        if (curcount > 0) {
#ifdef SEMA_PERF_STATS
            atomic_fetch_add_int64(&sema->stats_yield, 1, ATOMIC_RELAXED);
#endif
            return true;        // don't have to go to kernel after all!
        }

#ifdef SEMA_PERF_STATS
        atomic_fetch_add_int64(&sema->stats_kernel, 1, ATOMIC_RELAXED);
#endif

        if (!timed)
            ret = kernelSemaDec(&sema->ksema);
        else {
            int64 ktimeout = clamplow(clockTimer() - endtime, 0);
            ret = kernelSemaTryDecTimeout(&sema->ksema, ktimeout);
        }

        // undo the count adjustment if we failed to get the semaphore
        if (!ret)
            atomic_fetch_add_int32(&sema->count, 1, ATOMIC_ACQUIRE);
        return ret;
    } else {
        // couldn't create a kernel semaphore, fall back to a suboptimal yield loop
        for (;;) {
            curcount = atomic_load_int32(&sema->count, ATOMIC_ACQUIRE);
            if (curcount > 0 && atomic_compare_exchange_strong_int32(&sema->count, &curcount,
                                                                     curcount - 1, ATOMIC_ACQUIRE,
                                                                     ATOMIC_RELAXED))
                return true;

            osYield();

            if (timed && clockTimer() > endtime)
                return false;
        }
    }

    return false;       // unreachable
}
