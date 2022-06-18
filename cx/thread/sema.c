#include "sema.h"
#include <cx/time/clock.h>
#include <cx/platform/cpu.h>
#include <cx/platform/os.h>
#include <cx/utils/compare.h>

// Adaptive Half-Spin Semaphore (AHSS)

// Partly based on the concepts discussed in detail on Jeff Preshing's blog (https://preshing.com/)
// and implemented in his C++11 threading project (https://github.com/preshing/cpp11-on-multicore)
// under the zlib license.
//
// While AHSS is unquestionably inspired in many important ways by his work, this implementation
// differs in several substantial ways.
//
// The spinloop is adapative and adjusts based on contention and how long the average acquisition
// takes. The back half of the spinloop is a yield loop, which helps prevent CPU starvation when
// under high contention.  Using a single combined spin count target for the spinloop and yield
// loop provides balanced pressure to keep the target close to the optimal value.
//
// Similar to a Windows Critical Section, it defers allocation of the kernel resource until there
// is an actual need for the thread to sleep, so in low-to-no-contention cases it may never be
// needed. A suboptimal fallback mechanism is used in the rare event that no more kernel semaphores
// are available.

// failsafe to guard against pathalogical cases
#define SPIN_TARGET_MAX 30000

// customized version of lazyinit that uses shared flags
static void _semaLazyKInit(Semaphore *sema)
{
    // normally these never change, only need to be atomic during lazy init
    atomic(uint32) *flags = (atomic(uint32)*)&sema->flags;

    uint32 oldflags = atomicFetchOr(uint32, flags, SEMA_KSemaInitProgress, AcqRel);
    if (!(oldflags & SEMA_KSemaInitProgress)) {
        // won the race
        if (!kernelSemaInit(&sema->ksema, 0))
            atomicFetchOr(uint32, flags, SEMA_KSemaFallback, AcqRel);
        atomicFetchOr(uint32, flags, SEMA_KSemaInit, AcqRel);
    } else {
        // lost the race, poor man's spinlock
        while (!(atomicLoad(uint32, flags, Relaxed) & SEMA_KSemaInit))
            _CPU_PAUSE;
    }
}

bool semaInit(Semaphore *sema, int32 count)
{
    devAssert(count >= 0);
    memset(sema, 0, sizeof(Semaphore));
    atomicStore(int32, &sema->count, count, AcqRel);
    atomicStore(int32, &sema->spintarget, 2000, AcqRel);

    if (osPhysicalCPUs() == 1)
        sema->flags |= SEMA_NoSpin;

    return true;
}

bool semaDestroy(Semaphore *sema)
{
    devAssert(atomicLoad(int32, &sema->count, AcqRel) >= 0);

    if ((sema->flags & SEMA_KSemaInit) && !(sema->flags & SEMA_KSemaFallback))
        return kernelSemaDestroy(&sema->ksema);

    return true;
}

bool _semaContendedDec(Semaphore *sema, int64 timeout)
{
    bool spin = !(sema->flags & SEMA_NoSpin);
    bool timed = (timeout < timeForever);
    int32 curtarget = atomicLoad(int32, &sema->spintarget, Relaxed);
    int32 overage = (curtarget > SPIN_TARGET_MAX) ? SPIN_TARGET_MAX - curtarget : 0;
    int32 spincount = spin ? (curtarget - overage) * 2 : 1;
    int64 endtime = timed ? clockTimer() + timeout : 0;
    int yield = spin ? 0 : 1;
    int32 curcount;

    // spincount == 1 for the NoSpin case: assuming semaDec already tried the
    // contention-free path, this will yield the CPU once before falling back to
    // the kernel semaphore

    // fast path
    for (; spincount; --spincount) {
        _CPU_PAUSE;

        // try to decrement
        curcount = atomicLoad(int32, &sema->count, Acquire);
        if (curcount > 0 && atomicCompareExchange(int32, strong, &sema->count, &curcount,
                                                  curcount - 1, Acquire, Acquire)) {
            // successfully decremented semaphore on fast path
            // adjust adaptive target if we had to spin, perfect synchronization
            // is not necessary
            if (spin)
                atomicFetchAdd(int32, &sema->spintarget, (curtarget - overage - spincount) / 8 + 1, Relaxed);

#ifdef SEMA_PERF_STATS
            if (!yield)
                atomicFetchAdd(intptr, &sema->stats_spin, 1, Relaxed);
            else
                atomicFetchAdd(intptr, &sema->stats_yield, 1, Relaxed);
#endif
            return true;
        }

        // if doing a timed wait, check that we didn't time out
        if (timed && clockTimer() > endtime)
            return false;

        // start yielding the CPU once we're past the halfway point, to prevent CPU
        // starvation and runaway adaptation if more threads are spinning than
        // avilable cores
        if (spincount < curtarget)
            yield = true;

        if (yield)
            osYield();
    }

    // slow path

    // give adaptive target a big bump because we really want to avoid the slow path
    if (spin && overage == 0)
        atomicFetchAdd(int32, &sema->spintarget, curtarget + 2, Relaxed);

    // make sure kernel semaphore has been created
    if (!(sema->flags & SEMA_KSemaInit))
        _semaLazyKInit(sema);

    if (!(sema->flags & SEMA_KSemaFallback)) {
        bool ret = true;
        // allow count to go negative for threads that are waiting in the kernel
        curcount = atomicFetchSub(int32, &sema->count, 1, Acquire);

        // check if another thread incremented it after we gave up
        if (curcount > 0) {
#ifdef SEMA_PERF_STATS
            atomicFetchAdd(intptr, &sema->stats_yield, 1, Relaxed);
#endif
            return true;        // don't have to go to kernel after all!
        }

#ifdef SEMA_PERF_STATS
        atomicFetchAdd(intptr, &sema->stats_kernel, 1, Relaxed);
#endif

        if (!timed)
            ret = kernelSemaDec(&sema->ksema, sema->flags & SEMA_PlatformEvents);
        else {
            int64 ktimeout = clamplow(endtime - clockTimer(), 0);
            ret = kernelSemaTryDecTimeout(&sema->ksema, ktimeout, sema->flags & SEMA_PlatformEvents);
        }

        // undo the count adjustment if we failed to get the semaphore
        if (!ret)
            atomicFetchAdd(int32, &sema->count, 1, Release);
        return ret;
    } else {
        // couldn't create a kernel semaphore, fall back to a suboptimal yield loop
        for (;;) {
            curcount = atomicLoad(int32, &sema->count, Acquire);
            if (curcount > 0 && atomicCompareExchange(int32, strong, &sema->count, &curcount,
                                                      curcount - 1, Acquire, Acquire))
                return true;

            osYield();

            if (timed && clockTimer() > endtime)
                return false;
        }
    }

    return false;       // unreachable
}
