/// @file condvar.h
/// @brief Condition variable synchronization primitive
/// @defgroup thread_condvar Condition Variable
/// @ingroup thread
/// @{
///
/// A condition variable lets a thread sleep until another thread signals that some
/// shared state has changed, without racing to miss a signal that arrives between
/// checking the state and going to sleep.
///
/// CondVar is always used together with a Mutex that guards the condition being
/// waited on. Call cvarWait() or cvarWaitTimeout() with that mutex already held: the
/// wait atomically releases the mutex while sleeping, then re-acquires it before
/// returning if it was signaled. If the wait times out instead, the mutex is left
/// unlocked rather than being re-acquired.
///
/// Because a signal can arrive just before a wait begins, or a thread can be woken
/// without the condition actually being true yet, always re-check the condition in a
/// loop rather than assuming a single cvarWait() call means the condition holds.
///
/// Basic usage:
/// @code
///   Mutex m;
///   CondVar cv;
///   mutexInit(&m);
///   cvarInit(&cv);
///
///   // Thread 1: wait for the condition
///   mutexAcquire(&m);
///   while (!conditionIsTrue)
///       cvarWait(&cv, &m);
///   mutexRelease(&m);
///
///   // Thread 2: change the condition and wake a waiter
///   mutexAcquire(&m);
///   conditionIsTrue = true;
///   mutexRelease(&m);
///   cvarSignal(&cv);
///
///   cvarDestroy(&cv);
///   mutexDestroy(&m);
/// @endcode
///
/// For simple signaling that doesn't need a mutex-guarded predicate, an Event is
/// usually a better fit. Reach for CondVar specifically when you want the classic
/// "wait until this condition, checked under a lock, becomes true" pattern.
///
/// @note This header is not included by the \<cx/thread.h\> aggregate header. Include
/// \<cx/thread/condvar.h\> directly to use CondVar.

#pragma once

#include <cx/cx.h>
#include "aspin.h"
#include "futex.h"
#include "mutex.h"

CX_C_BEGIN

/// Condition variable initialization flags
enum CONDVAR_Flags {
    CONDVAR_NoSpin = 1,   ///< Disable adaptive spinning, use kernel futex immediately
};

/// Condition variable synchronization primitive
///
/// Used together with a Mutex to let threads wait for a shared condition to become
/// true.
typedef struct CondVar {
    Futex seq;              ///< Futex used to sequence and wake waiters
    atomic(uint32) lastseq; ///< Sequence number observed by the most recent waiter
    AdaptiveSpin aspin;     ///< Adaptive spin state
} CondVar;

void _cvarInit(_Out_ CondVar* cv, uint32 flags);

/// void cvarInit(CondVar *cv, [flags])
///
/// Initialize a condition variable for use.
///
/// Must be called before using any other condition variable operations.
/// @param cv Pointer to uninitialized condition variable structure
/// @param ... (flags) Optional CONDVAR_Flags (e.g., CONDVAR_NoSpin)
#define cvarInit(cv, ...) _cvarInit(cv, opt_flags(__VA_ARGS__))

/// Destroy a condition variable and release its resources
///
/// Cleans up the condition variable after use. There must be no threads waiting on
/// it when destroyed. After destruction, the condition variable must be reinitialized
/// before it can be used again.
/// @param cv Condition variable to destroy
void cvarDestroy(_Inout_ CondVar* cv);

/// Wait on a condition variable with a timeout
///
/// Must be called with m already held. Atomically releases m and blocks the calling
/// thread until another thread calls cvarSignal() or cvarBroadcast() on cv, or the
/// timeout elapses.
///
/// If the wait is signaled, m is re-acquired before this function returns. If the
/// wait times out, m is left unlocked.
/// @param cv Condition variable to wait on
/// @param m Mutex currently held, guarding the condition
/// @param timeout Maximum time to wait in nanoseconds (use timeForever for infinite)
/// @return true if signaled and m was re-acquired; false if the timeout elapsed and
/// m was left unlocked
_Requires_lock_held_(*m) bool cvarWaitTimeout(_Inout_ CondVar* cv, _Inout_ Mutex* m, int64 timeout);

/// Wait on a condition variable indefinitely
///
/// Must be called with m already held. Atomically releases m and blocks the calling
/// thread until another thread calls cvarSignal() or cvarBroadcast() on cv, then
/// re-acquires m before returning. Equivalent to cvarWaitTimeout() with timeForever.
/// @param cv Condition variable to wait on
/// @param m Mutex currently held, guarding the condition
_Requires_lock_held_(*m) _meta_inline bool cvarWait(_Inout_ CondVar* cv, _Inout_ Mutex* m)
{
    return cvarWaitTimeout(cv, m, timeForever);
}

/// Wake one thread waiting on a condition variable
///
/// If no thread is currently waiting, the signal has no lasting effect. It is not
/// remembered for a future wait.
/// @param cv Condition variable to signal
/// @return true on success
bool cvarSignal(_Inout_ CondVar* cv);

/// Wake all threads waiting on a condition variable
///
/// If no threads are currently waiting, the broadcast has no lasting effect. It is
/// not remembered for a future wait.
/// @param cv Condition variable to broadcast to
/// @return true on success
bool cvarBroadcast(_Inout_ CondVar* cv);

CX_C_END

/// @}
// end of thread_condvar group
