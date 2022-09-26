#pragma once

#include <cx/cx.h>
#include "futex.h"
#include "aspin.h"

// Event: Similar to auto-reset events on Windows.
// Basically a semaphore that has a maximum value of 1, but includes some extra
// features such as releasing an arbitrary number of threads on demand.

// Also can be locked in a signaled state that turns it into the equivalent of
// a manual-reset event. This is useful for things like thread shutdown, where
// you want to avoid any potential races without having to spam the event.

CX_C_BEGIN

typedef struct UIEvent UIEvent;
typedef struct Event {
    // Futex values:
    //  0 - Event is not signaled, any thread will have to wait
    //  1 - Event is signaled
    // >1 - Event is signaled for multiple waiters to wake up
    // -1 - Event is locked, all waits will complete instantly
    Futex ftx;
    atomic(int32) waiters;
    AdaptiveSpin aspin;
    UIEvent *uiev;
} Event;

enum EVENTINITFUNC_FLAGS {
    EV_Spin = 1,            // Use adaptive spinloop instead of going straight to sleep
    EV_UIEvent = 2,         // May be woken up early by platform-specific UI events
};

// Events normally do not use the adaptive spin framework.
// This is because we assume that events are used for occasional signaling between threads,
// and when a thread waits on an event it's more likely than not to need to sleep. It would
// be a waste of CPU for the event to spin waiting for a signal that is very unlikely to
// come before the spinloop ends.
//
// For events used by high-performance queues or other situations where it is expected that
// there will usually be work to do, the caller should initialize the Event with a "Spin" flag,
// which will cause the event to use adaptive spinning.
bool _eventInit(Event *e, uint32 flags);
#define eventInit(e, ...) _eventInit(e, opt_flags(__VA_ARGS__))

#define eventSignal(e) eventSignalMany(e, 1)
bool eventSignalMany(Event *e, int32 count);
bool eventSignalAll(Event *e);

bool eventWaitTimeout(Event *e, uint64 timeout);
_meta_inline bool eventWait(Event *e)
{
    return eventWaitTimeout(e, timeForever);
}

// signals the event and locks it in the signaled state, so threads attempting to wait on it
// always return immediately
bool eventSignalLock(Event *e);
// manually reset the event to an unsignaled state
bool eventReset(Event *e);
void eventDestroy(Event *e);

CX_C_END
