/// @file event.h
/// @brief Event synchronization primitive
/// @defgroup thread_event Event
/// @ingroup thread
/// @{
///
/// Events for thread signaling and synchronization.
///
/// Event is similar to auto-reset events on Windows - essentially a semaphore with a maximum
/// value of 1, plus additional features like releasing multiple waiters at once and locking
/// in a signaled state (manual-reset mode).
///
/// Events are designed for occasional signaling between threads. By default, they do NOT use
/// adaptive spinning and go straight to kernel waits, assuming the signal is unlikely to
/// arrive immediately. Use the EV_Spin flag for high-performance scenarios where signals
/// are expected to arrive quickly (e.g., very busy work queues).
///
/// Basic usage:
/// @code
///   Event ev;
///   eventInit(&ev);
///
///   // Thread 1: Wait for signal
///   eventWait(&ev);
///   // ... do work after being signaled
///
///   // Thread 2: Send signal
///   eventSignal(&ev);
///
///   eventDestroy(&ev);
/// @endcode
///
/// Locking in signaled state (manual-reset mode):
/// @code
///   eventSignalLock(&ev);  // All subsequent waits return immediately
/// @endcode

#pragma once

#include <cx/cx.h>
#include "aspin.h"
#include "futex.h"

CX_C_BEGIN

typedef struct UIEvent UIEvent;

/// Event synchronization primitive
///
/// Internal structure using futex-based signaling with optional adaptive spinning.
/// Futex values: 0=not signaled, 1=signaled, >1=signaled for multiple waiters, -1=locked
/// (manual-reset)
typedef struct Event {
    Futex ftx;               ///< Futex for kernel-level synchronization
    atomic(int32) waiters;   ///< Number of threads currently waiting
    AdaptiveSpin aspin;      ///< Adaptive spin state (disabled by default)
    UIEvent* uiev;           ///< Platform-specific UI event integration (optional)
} Event;

/// Event initialization flags
enum EVENTINITFUNC_FLAGS {
    EV_Spin    = 1,   ///< Use adaptive spinning instead of going straight to kernel wait
    EV_UIEvent = 2,   ///< May be woken up early by platform-specific UI events
};

/// Reference-counted event for shared ownership between threads
///
/// A common pattern is an event shared between two threads where one signals the other
/// when something is complete. This makes managing the event's lifetime difficult, as
/// it's not safe for it to live on the stack or for one thread to free it.
///
/// SharedEvent provides a reference-counted wrapper that can be safely cleaned up when
/// both threads are done with it using sheventAcquire() and sheventRelease().
///
/// Example:
/// @code
///   int workerThread(Thread *self) {
///       SharedEvent *ev = stvlNextPtr(SharedEvent, self->args);
///       // do work...
///       eventSignal(&ev->ev);
///       sheventRelease(&ev);  // worker releases its reference
///       return 0;
///   }
///
///   // Main thread creates shared event and passes to worker
///   SharedEvent *completion = sheventCreate(0);
///   Thread *worker = thrCreate(workerThread, _S"worker",
///                               stvar(ptr, sheventAcquire(completion)));
///
///   // Wait for worker to complete
///   eventWait(&completion->ev);
///
///   // Clean up
///   sheventRelease(&completion);  // main thread releases its reference
///   thrRelease(&worker);
/// @endcode
typedef struct SharedEvent {
    Event ev;              ///< Underlying event
    atomic(uintptr) ref;   ///< Reference count
} SharedEvent;

void _eventInit(_Out_ Event* e, uint32 flags);

/// void eventInit(Event *e, [flags])
///
/// Initialize an event for use.
///
/// Events normally do NOT use adaptive spinning. This is because events are assumed to be used
/// for occasional signaling between threads where the wait is likely to need kernel sleep.
/// It would waste CPU to spin waiting for a signal unlikely to arrive soon.
///
/// For events used by high-performance queues or scenarios where work is usually ready,
/// initialize with the EV_Spin flag to enable adaptive spinning.
///
/// @param e Pointer to uninitialized event structure
/// @param ... (flags) Optional EVENTINITFUNC_FLAGS (e.g., EV_Spin, EV_UIEvent)
#define eventInit(e, ...) _eventInit(e, opt_flags(__VA_ARGS__))

/// Signal the event, waking one waiting thread
///
/// Releases one waiting thread (if any) and sets the event to signaled state if no threads
/// are waiting. This is the standard auto-reset behavior.
/// @param e Event to signal
/// @return true if a thread was woken or the event was signaled
#define eventSignal(e) eventSignalMany(e, 1)

/// Signal the event, waking up to a specified number of waiting threads
///
/// Releases up to 'count' waiting threads. If fewer threads are waiting, all are released.
/// The event remains signaled if no threads are waiting.
/// @param e Event to signal
/// @param count Maximum number of threads to wake
/// @return true if any threads were woken or the event was signaled
bool eventSignalMany(_Inout_ Event* e, int32 count);

/// Signal the event, waking all waiting threads
///
/// Broadcast signal that releases all threads currently waiting on the event.
/// This is a one-time broadcast; threads that wait after this call will block normally.
/// @param e Event to signal
/// @return true on success
bool eventSignalAll(_Inout_ Event* e);

/// Wait for an event to be signaled with a timeout
///
/// Blocks until the event is signaled or the timeout expires. When signaled, the event
/// auto-resets to unsignaled state (unless locked with eventSignalLock()).
///
/// @note May produce spurious wakeups if multiple threads call eventWaitTimeout() with
/// timeouts simultaneously.
/// @param e Event to wait on
/// @param timeout Maximum time to wait in nanoseconds (use timeForever for infinite)
/// @return true if the event was signaled, false if timeout expired
bool eventWaitTimeout(_Inout_ Event* e, uint64 timeout);

/// Wait for an event to be signaled indefinitely
///
/// Blocks until the event is signaled. Equivalent to eventWaitTimeout() with timeForever.
/// @param e Event to wait on
_meta_inline void eventWait(_Inout_ Event* e)
{
    eventWaitTimeout(e, timeForever);
}

/// Signal the event and lock it in the signaled state
///
/// Sets the event to permanently signaled (manual-reset mode). All subsequent waits return
/// immediately without blocking. This is useful for thread shutdown where you want to ensure
/// no races without repeatedly signaling.
/// @param e Event to signal and lock
/// @return true on success
bool eventSignalLock(_Inout_ Event* e);

/// Manually reset the event to unsignaled state
///
/// Clears the signaled state, causing subsequent waits to block until the event is signaled
/// again. This can also unlock an event that was locked with eventSignalLock().
/// @param e Event to reset
/// @return true if the event was signaled, false if already unsignaled
bool eventReset(_Inout_ Event* e);

/// Destroy an event and release its resources
///
/// Cleans up the event after use. The event must not have waiting threads when destroyed.
/// After destruction, the event must be reinitialized before it can be used again.
/// @param e Event to destroy
void eventDestroy(_Pre_valid_ _Post_invalid_ Event* e);

/// Create a new reference-counted shared event
///
/// Allocates and initializes a SharedEvent with a reference count of 1.
/// Use sheventAcquire() to add references and sheventRelease() to release them.
/// @param flags Optional EVENTINITFUNC_FLAGS (e.g., EV_Spin, EV_UIEvent)
/// @return Newly allocated SharedEvent
_Ret_valid_ SharedEvent* sheventCreate(uint32 flags);

/// Acquire a reference to a shared event
///
/// Increments the reference count. Each call to sheventAcquire() must be paired with
/// a corresponding sheventRelease().
/// @param ev SharedEvent to acquire
/// @return The same SharedEvent pointer
_Ret_valid_ SharedEvent* sheventAcquire(_In_ SharedEvent* ev);

/// Release a reference to a shared event
///
/// Decrements the reference count and destroys the SharedEvent when it reaches zero.
/// Sets the pointer to NULL after release.
/// @param pev Pointer to SharedEvent pointer
_At_(*pev, _Pre_maybenull_ _Post_null_) void sheventRelease(_Inout_ SharedEvent** pev);

CX_C_END

/// @}
// end of thread_event group
