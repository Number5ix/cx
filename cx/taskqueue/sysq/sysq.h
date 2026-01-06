/// @file sysq.h
/// @brief System task queue for background library tasks
/// @defgroup taskqueue_sysq System Task Queue
/// @ingroup taskqueue
/// @{
///
/// Background task queue for asynchronous library operations.
///
/// Similar to system threads (systhread), the system queue (sysq) is a task queue for background
/// tasks run asynchronously by the library itself. It provides a convenient way for library code
/// to defer work without requiring the application to manage task queues.
///
/// Key features:
/// - Started automatically on first use
/// - Shut down automatically at program exit
/// - Supports immediate execution, scheduled delays, and deferred tasks
/// - Thread pool managed internally
///
/// Use this for:
/// - Library-internal background work
/// - Asynchronous cleanup operations
/// - Deferred processing that doesn't need caller control
///
/// For application-level task queues with full control, use TaskQueue directly instead.
///
/// Example:
/// @code
///   // Run a task immediately
///   MyTask *task = myTaskCreate();
///   sysqAdd(task);
///
///   // Schedule a task for later
///   MyTask *delayed = myTaskCreate();
///   sysqSchedule(delayed, timeFromSeconds(5));
///
///   // Run a simple function
///   sysqCall(myCallback, userData);
/// @endcode

#pragma once

#include <cx/taskqueue/taskqueue.h>

bool _sysqAdd(_In_ BasicTask* task);

/// bool sysqAdd(BasicTask *task)
///
/// Add a task to the system queue to run immediately.
///
/// The task is queued for execution on the next available worker thread in the system queue's
/// thread pool. The task object is acquired by the queue and will be released when complete.
///
/// @param task Task to execute (any task type, automatically cast to BasicTask)
/// @return true if the task was successfully added, false on failure
#define sysqAdd(task) _sysqAdd(BasicTask(task))

/// void sysqRun(BasicTask **ptask)
///
/// Convenience function to queue a task and release the caller's reference.
///
/// This is a common pattern where you create a task, queue it, and immediately release your
/// reference since the queue now owns it. This macro does both operations atomically.
///
/// @param ptask Pointer to task pointer (will be set to NULL after release)
#define sysqRun(ptask)     \
    do {                   \
        sysqAdd(*ptask);   \
        objRelease(ptask); \
    } while (0)

bool _sysqSchedule(_In_ ComplexTask* task, int64 delay);

/// bool sysqSchedule(ComplexTask *task, int64 delay)
///
/// Add a task to the system queue to run after a delay.
///
/// The task is held in the queue and executed after the specified delay. Requires a ComplexTask
/// because scheduling uses the internal timing mechanisms only available in complex tasks.
///
/// @param task Task to execute (must be ComplexTask or derived type)
/// @param delay Time to wait before running in nanoseconds (use timeFromSeconds(), timeFromMs(),
/// etc.)
/// @return true if the task was successfully scheduled, false on failure
#define sysqSchedule(task, delay) _sysqSchedule(ComplexTask(task), delay)

bool _sysqDefer(_In_ ComplexTask* task);

/// bool sysqDefer(ComplexTask *task)
///
/// Add a task to the queue to be held indefinitely until advanced.
///
/// The task is added to the queue but not run until explicitly advanced with taskAdvance().
/// This is useful for tasks that need to wait for external events or conditions. Requires
/// a ComplexTask.
///
/// @param task Task to defer (must be ComplexTask or derived type)
/// @return true if the task was successfully deferred, false on failure
#define sysqDefer(task) _sysqDefer(ComplexTask(task))

/// Run a custom function on a worker thread
///
/// Executes a simple callback function on one of the system queue's worker threads without
/// requiring a full task object. This is convenient for simple asynchronous operations.
///
/// The callback is executed on a thread from the system queue's worker pool. The userdata
/// pointer is passed through to the callback.
///
/// @param func Callback function to execute
/// @param userdata Optional user data pointer passed to the callback
/// @return true if the callback was successfully queued, false on failure
bool sysqCall(_In_ UserTaskCB func, _In_opt_ void* userdata);

/// @}
// end of taskqueue_sysq group
