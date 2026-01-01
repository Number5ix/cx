/// @file thread.h
/// @brief Thread creation and management
/// @defgroup thread_core Core Multithreading
/// @ingroup thread
/// @{

#pragma once

#include <cx/cx.h>
#include <cx/platform/base.h>

#include <cx/thread/threadobj.h>

/// Thread scheduling priority levels
enum ThreadPriority {
    THREAD_Normal = 0,   ///< Normal priority (default)
    THREAD_Batch,        ///< Batch processing priority (below normal)
    THREAD_Low,          ///< Low priority
    THREAD_Idle,         ///< Idle priority (only runs when system is idle)
    THREAD_High,         ///< High priority
    THREAD_Higher,       ///< Higher priority
    THREAD_Realtime      ///< Real-time priority (use with caution)
};

/// Get the current thread object
///
/// Returns the Thread object for the currently executing thread. This only works for threads
/// created through the CX threading system.
/// @return Thread object for the current thread, or NULL if called from a non-CX thread
_Ret_opt_valid_ Thread* thrCurrent(void);

/// Get the OS-specific thread ID for a thread
/// @param thread Thread object
/// @return Platform-specific thread identifier
intptr thrOSThreadID(_In_ Thread* thread);

/// Get the OS-specific thread ID for the currently executing thread
///
/// Unlike thrCurrent(), this function works even on threads not created by CX.
/// @return Platform-specific thread identifier for the current thread
intptr thrCurrentOSThreadID(void);

_Ret_opt_valid_ _Check_return_ Thread*
_thrCreate(_In_ threadFunc func, _In_ strref name, int n, _In_ stvar args[], bool ui);
// Thread* thrCreate(threadFunc func, strref name, ...)
//
// Creates and starts a thread. The return value MUST be released with thrRelease to avoid leaking
// memory.
#define thrCreate(func, name, ...) \
    _thrCreate(func, name, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ }, false)
#define thrCreateUI(func, name, ...) \
    _thrCreate(func, name, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ }, true)

void _thrRun(_In_ threadFunc func, _In_ strref name, int n, _In_ stvar args[]);

/// void thrRun(threadFunc func, strref name, ...)
///
/// Creates and starts a fire-and-forget thread that cannot be waited on or controlled.
///
/// Unlike thrCreate(), this does not return a Thread object. The thread runs independently
/// and cannot be joined or explicitly shut down. Use this for truly independent background
/// tasks that don't require coordination.
/// @param func Thread entry point function
/// @param name Thread name for debugging
/// @param ... Optional arguments passed to the thread function (wrapped as stvar)
#define thrRun(func, name, ...) \
    _thrRun(func, name, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ })

/// Check if a thread is currently running
/// @param thread Thread object
/// @return true if the thread is currently executing, false if it has exited
_meta_inline bool thrRunning(_In_ Thread* thread)
{
    return atomicLoad(bool, &thread->running, Acquire);
}

/// Check if a thread should continue its main loop
///
/// This is the standard pattern for thread main loops. Returns false when another thread has
/// called thrRequestExit() or thrShutdown() on this thread.
///
/// Example:
/// @code
///   int workerThread(Thread *self) {
///       while (thrLoop(self)) {
///           // do work
///
///           // OPTIONAL: if idle, wait on notification event
///           eventWait(self->notify);
///       }
///       return 0;
///   }
/// @endcode
/// @param thread Thread object (usually the 'self' parameter)
/// @return true if the thread should continue running, false if it should exit
_meta_inline bool thrLoop(_In_ Thread* thread)
{
    return !atomicLoad(bool, &thread->requestExit, Acquire);
}

/// Request a thread to exit gracefully
///
/// Sets the thread's exit flag and signals its notification event. The thread should check
/// this flag using thrLoop() or by checking the requestExit flag directly.
///
/// This is a non-blocking request. Use thrWait() or thrShutdown() to wait for the thread
/// to actually finish executing.
/// @param thread Thread object to signal
/// @return true if the exit was requested successfully, false if the thread was NULL or already
/// stopped
bool thrRequestExit(_In_ Thread* thread);

/// Wait for a thread to finish executing
///
/// Blocks the calling thread until the specified thread exits or the timeout expires.
/// @param thread Thread object to wait on
/// @param timeout Maximum time to wait in nanoseconds (use timeFromSeconds(), timeFromMs(), etc.)
/// @return true if the thread exited within the timeout, false if timeout expired
bool thrWait(_In_ Thread* thread, int64 timeout);

/// Request a thread to exit and wait for it to finish
///
/// Combines thrRequestExit() and thrWait() with a 30-second timeout. This is the standard
/// way to cleanly shut down a thread.
/// @param thread Thread object to shut down
/// @return true if the thread exited successfully, false if NULL or timeout expired
bool thrShutdown(_In_ Thread* thread);

/// Shut down multiple threads efficiently in parallel
///
/// Requests all threads to exit simultaneously, then waits for them to finish. This is more
/// efficient than calling thrShutdown() on each thread sequentially because all threads can
/// be shutting down in parallel.
/// @param threads Array of thread objects to shut down
/// @return Number of threads that successfully shut down within the timeout
int thrShutdownMany(_In_ sa_Thread threads);

bool _thrPlatformSetPriority(_Inout_ Thread* thread, int prio);

/// bool thrSetPriority(Thread *thread, priority)
///
/// Set the scheduling priority of a thread using a ThreadPriority enum value.
/// @param thread Thread object
/// @param prio Priority level (e.g., Normal, High, Realtime) - do not prefix with THREAD_
/// @return true if priority was set successfully
#define thrSetPriority(thread, prio) _thrPlatformSetPriority(thread, THREAD_##prio)

/// bool thrSetPriorityV(Thread *thread, int prio)
///
/// Set the scheduling priority of a thread using a raw integer value.
/// @param thread Thread object
/// @param prio Raw priority value (THREAD_Normal, THREAD_High, etc.)
/// @return true if priority was set successfully
#define thrSetPriorityV(thread, prio) _thrPlatformSetPriority(thread, prio)

/// void thrRelease(Thread **pthread)
///
/// Release a reference to a thread object.
///
/// Decrements the reference count and destroys the thread if it reaches zero. The thread
/// continues executing even after releasing the reference, but memory will be leaked if
/// you don't release all references.
/// @param pthread Pointer to thread object pointer (set to NULL after release)
#define thrRelease(pthread) objRelease(pthread)

typedef struct Event Event;

/// Register a thread as a system thread
///
/// System threads are background threads that run independently of the main program and are
/// typically library-created. They are notified at program exit and given an opportunity to
/// perform cleanup before being destroyed.
///
/// This should only be used for infrastructure threads like memory allocators, logging systems,
/// or other low-level services that need special shutdown handling.
/// @param thread Thread object to register as a system thread
void thrRegisterSysThread(_Inout_ Thread* thread);

/// @}
// end of thread_core group
