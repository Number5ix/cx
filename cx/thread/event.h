#pragma once

#include <cx/cx.h>
#include <cx/platform/base.h>
#include <cx/utils/compare.h>
#include "sema.h"

// Event: Similar to auto-reset events on Windows.
// Basically a semaphore that has a maximum value of 1.

// Also can be locked in a signaled state that turns it into the equivalent of
// a manual-reset event. This is useful for things like thread shutdown, where
// you want to avoid any potential races without having to spam the event.

CX_C_BEGIN

typedef struct Event {
    // number of threads waiting for the event
    // -1 indicates that the event has been signaled
    atomic(int32) waiting;
    Semaphore sema;
} Event;

enum EVENTINITFUNC_FLAGS {
    EVENTINITFUNC_ = 0,
    EVENTINITFUNC_Spin = 1,
};

// Events normally use the "NoSpin" type semaphores.
// This is because we assume that events are used for occasional signaling between threads,
// and when a thread waits on an event it's more likely than not to need to sleep. It would
// be a waste of CPU for the event to spin waiting for a signal that is very unlikely to
// come before the spinloop ends.
//
// For events used by high-performance queues or other situations where it is expected that
// there will usually be work to do, the caller should initialize the Event with a "Spin" flag,
// which will cause the event to use the normal adapative semaphore.
bool _eventInit(Event *e, uint32 flags);
#define eventInit(e, ...) _eventInit(e, func_flags(EVENTINITFUNC, __VA_ARGS__))

#define eventSignal(e) eventSignalMany(e, 1)
bool eventSignalMany(Event *e, int32 count);
bool eventSignalAll(Event *e);

// signals the event and locks it in the signaled state, so threads attempting to wait on it
// always return immediately
bool eventSignalLock(Event *e);
// manually reset the event to an unsignaled state
bool eventReset(Event *e);

_meta_inline bool eventWait(Event *e)
{
    int32 waiting = atomicLoad(int32, &e->waiting, Relaxed);
    while (waiting != -2 && !atomicCompareExchange(int32, weak, &e->waiting, &waiting,
                                                   waiting + 1, Acquire, Relaxed)) {}

    // if event was signaled, return immediately
    if (waiting < 0)
        return true;

    return semaDec(&e->sema);
}

_meta_inline bool eventWaitTimeout(Event *e, uint64 timeout)
{
    int32 waiting = atomicLoad(int32, &e->waiting, Relaxed);
    while (waiting != -2 && !atomicCompareExchange(int32, weak, &e->waiting, &waiting,
                                                   waiting + 1, Acquire, Relaxed)) {}

    // if event was signaled, return immediately
    if (waiting < 0)
        return true;

    // try to wait
    if (semaTryDecTimeout(&e->sema, timeout))
        return true;

    // Wait timed out: need to undo the add above, but be careful because another
    // thread may have signaled the event in the meantime. This MAY produce a spurious
    // wakeup of another thread if we lose the race with eventSignalMany. Keep that in
    // mind when waiting on events that may have eventWaitTimeout called on them.
    while (waiting != -2 && !atomicCompareExchange(int32, weak, &e->waiting, &waiting,
                                  clamplow(waiting - 1, -1), Release, Relaxed)) {}
    return false;
}

void eventDestroy(Event *e);

CX_C_END
