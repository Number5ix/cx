#include "event.h"

bool eventInit(Event *e, flags_t flags)
{
    memset(e, 0, sizeof(Event));
    semaInit(&e->sema, 0);

    // Default to no-spin semaphore unless requested otherwise
    // See the comments in event.h
    if (!(flags & EV_Spin))
        e->sema.flags |= SEMA_NoSpin;
    if (flags & EV_PlatformEvents)
        e->sema.flags |= SEMA_PlatformEvents;
    return true;
}

bool eventSignalMany(Event *e, int32 count)
{
    int32 waiting = atomicLoad(int32, &e->waiting, Relaxed);

    // reduce number of theads waiting, working our way towards -1 (signaled but no waiters)
    while (waiting != -2 && !atomicCompareExchange(int32, weak, &e->waiting, &waiting,
                                                   clamplow(waiting - count, -1), Release, Relaxed)) {}
    // actually release the waiting thread(s)
    if (waiting > 0)
        semaInc(&e->sema, min(waiting, count));

    // return false if it was already signaled
    return (waiting >= 0);
}

bool eventSignalAll(Event *e)
{
    return eventSignalMany(e, 0x7fffffff);
}

bool eventSignalLock(Event *e)
{
    int32 waiting;

    waiting = atomicLoad(int32, &e->waiting, Relaxed);

    // -2 is the special value that indicates the event is locked, if it ever becomes -2 during this
    // loop, it means another thread locked it
    while (waiting != -2) {
        // try to go to -2, but only if we're already in a signaled state with no threads waiting (-1)
        if (waiting == -1 && atomicCompareExchange(int32, strong, &e->waiting, &waiting, -2, Release, Relaxed))
            return true;

        // otherwise try to move towards a signaled state
        if (waiting >= 0)
            eventSignalAll(e);

        waiting = atomicLoad(int32, &e->waiting, Relaxed);
    }

    // another thread beat us to it
    return false;
}

bool eventReset(Event *e)
{
    int32 waiting = atomicLoad(int32, &e->waiting, Relaxed);

    do {
        if (waiting >= 0)
            return false;
    } while (!atomicCompareExchange(int32, weak, &e->waiting, &waiting, 0, Release, Relaxed));

    return true;
}

void eventDestroy(Event *e)
{
    semaDestroy(&e->sema);
}
