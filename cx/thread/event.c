#include "event.h"
#include <cx/time/time.h>
#include <cx/utils/compare.h>
#include <cx/platform/os.h>

bool _eventInit(Event *e, flags_t flags)
{
    int32 ftxflags = 0;
    if (flags & EV_Alertable)
        ftxflags = FUTEX_Alertable;
    futexInit(&e->ftx, 0, ftxflags);
    atomicStore(int32, &e->waiters, 0, Relaxed);

    // Default to not spin unless requested otherwise
    // See the comments in event.h
    aspinInit(&e->aspin, !(flags & EV_Spin));
    return true;
}

bool eventSignalMany(Event *e, int32 count)
{
    devAssert(count > 0);
    AdaptiveSpinState astate;
    aspinBegin(&e->aspin, &astate, timeForever);

    // get the number of waiting threads and reduce it by the amount we're about to wake up
    int32 waiters = atomicLoad(int32, &e->waiters, Relaxed);
    while (waiters > 0 && !atomicCompareExchange(int32, weak, &e->waiters, &waiters, clamplow(waiters - count, 0), Relaxed, Relaxed)) {
        aspinHandleContention(&e->aspin, &astate);
    }

    // can only wake up as many as threads are currently waiting
    count = clamphigh(count, waiters);

    // Set the futex value, which we're using kind of like a semaphore, to the number that needs to wake up.
    // Normally we want this to behave like a binary semaphore, so if the event is signaled but nothing has
    // waited yet (i.e. they are currently in the process of waking up), we don't want to signal it again.
    // Because the event should end up in a signaled state when this is called, enforce a minimum of 1 to set
    // the futex value to, even if there are no waiters.
    int32 val = atomicLoad(int32, &e->ftx.val, Acquire);
    while (val >= 0 && !atomicCompareExchange(int32, weak, &e->ftx.val, &val, clamplow(val + count, 1), Release, Relaxed)) {
        aspinHandleContention(&e->aspin, &astate);
    }

    if (count > 0)
        futexWakeMany(&e->ftx, count);

    // return true if we woke something up or signaled the event
    return count || val == 0;
}

bool eventSignalAll(Event *e)
{
    AdaptiveSpinState astate;
    aspinBegin(&e->aspin, &astate, timeForever);

    // get the number of waiting threads and set it to 0
    int32 waiters = atomicLoad(int32, &e->waiters, Relaxed);
    if (waiters == 0)
        return false;

    while (!atomicCompareExchange(int32, weak, &e->waiters, &waiters, 0, Relaxed, Relaxed)) {
        aspinHandleContention(&e->aspin, &astate);
    }

    // set the futex value the number of waiters so they all wake up.
    // This is a broadcast event, so we don't need to worry about a race between the previous atomic
    // and this one -- any new waiters after we read the number will just have to wait.
    int32 val = atomicLoad(int32, &e->ftx.val, Acquire);
    while (val >= 0 && !atomicCompareExchange(int32, weak, &e->ftx.val, &val, val + waiters, Release, Relaxed)) {
        aspinHandleContention(&e->aspin, &astate);
    }

    // let the herd come thundering
    futexWakeAll(&e->ftx);
    return true;
}

bool eventSignalLock(Event *e)
{
    atomicStore(int32, &e->ftx.val, -1, Release);
    eventSignalAll(e);

    return true;
}

bool eventWaitTimeout(Event *e, uint64 timeout)
{
    // try fast path first
    int32 val = atomicLoad(int32, &e->ftx.val, Relaxed);
    if (val == -1)
        return true;

    if (val == 1 && atomicCompareExchange(int32, strong, &e->ftx.val, &val, 0, Acquire, Relaxed)) {
        aspinRecordUncontended(&e->aspin);
        return true;
    }

    AdaptiveSpinState astate;
    aspinBegin(&e->aspin, &astate, timeout);

    while (val <= 0 || !atomicCompareExchange(int32, weak, &e->ftx.val, &val, val - 1, Acquire, Relaxed)) {
        if (val == -1)
            return true;        // immediate out if event is locked, don't update any stats

        // otherwise need to wait
        if (val == 0) {
            if (aspinTimeout(&e->aspin, &astate))
                return false;

            if (!aspinSpin(&e->aspin, &astate)) {
                // track waiters for wakeup purposes
                atomicFetchAdd(int32, &e->waiters, 1, Relaxed);
                int fret = futexWait(&e->ftx, val, aspinTimeoutRemaining(&astate));
                if (fret == FUTEX_Alerted || fret == FUTEX_Timeout) {
                    // Wait finished early: need to undo the add above, but be careful because another
                    // thread may have signaled the event in the meantime. This MAY produce a spurious
                    // wakeup of another thread if we lose the race with eventSignalMany. Keep that in
                    // mind when waiting on events that may have eventWaitTimeout called on them.
                    int32 waiters = atomicLoad(int32, &e->waiters, Relaxed);
                    while (waiters > 0 && !atomicCompareExchange(int32, weak, &e->waiters, &waiters,
                                                                 waiters - 1, Relaxed, Relaxed)) {
                        aspinHandleContention(&e->aspin, &astate);
                    }
                }
                if (fret == FUTEX_Alerted)
                    return true;    // immediate out if we were alerted, don't update spin stats
            }

            val = atomicLoad(int32, &e->ftx.val, Relaxed);
        }
    }

    aspinAdapt(&e->aspin, &astate);
    return true;
}

bool eventReset(Event *e)
{
    int32 val = atomicLoad(int32, &e->ftx.val, Relaxed);

    do {
        if (val == 0)
            return false;
    } while (!atomicCompareExchange(int32, weak, &e->ftx.val, &val, 0, Release, Relaxed));

    return true;
}

void eventDestroy(Event *e)
{
    memset(e, 0, sizeof(Event));
}
