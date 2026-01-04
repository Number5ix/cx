#pragma once

#include <cx/cx.h>
#include <cx/log/log.h>
#include <cx/taskqueue/queue/tqcomplex.h>
#include <cx/taskqueue/queue/tqueue.h>
#include <cx/taskqueue/requires/taskrequiresgate.h>
#include <cx/taskqueue/resource/trfifo.h>
#include <cx/taskqueue/resource/trlifo.h>
#include <cx/taskqueue/resource/trmutex.h>
#include <cx/taskqueue/task/mptask.h>
#include <cx/taskqueue/worker/tqworker.h>

/// @addtogroup taskqueue
/// @{

/// @defgroup tq_config Task Queue Configuration
/// @{

/// void tqPresetSingle(TaskQueueConfig *tqconfig)
///
/// Initialize a queue config for a queue with a single worker thread.
/// Suitable for UI threads or other scenarios requiring serial execution.
/// @param tqconfig Configuration structure to initialize
void tqPresetSingle(_Out_ TaskQueueConfig *tqconfig);

/// void tqPresetMinimal(TaskQueueConfig *tqconfig)
///
/// Initialize a queue config with minimal presets based on the number of physical cores.
/// Starts with 1 worker, grows to half the physical CPU count when busy.
/// Maximum workers equals the physical CPU count.
/// @param tqconfig Configuration structure to initialize
void tqPresetMinimal(_Out_ TaskQueueConfig *tqconfig);

/// void tqPresetBalanced(TaskQueueConfig *tqconfig)
///
/// Initialize a queue config with balanced presets based on logical cores.
/// General-purpose configuration suitable for most applications.
/// 2 idle workers, grows to physical CPU count when busy, max = logical CPU count.
/// @param tqconfig Configuration structure to initialize
void tqPresetBalanced(_Out_ TaskQueueConfig *tqconfig);

/// void tqPresetHeavy(TaskQueueConfig *tqconfig)
///
/// Initialize a queue config for high performance under sustained heavy load.
/// Uses a dedicated manager thread, 4 idle workers, and can scale to 150% of logical CPUs.
/// @param tqconfig Configuration structure to initialize
void tqPresetHeavy(_Out_ TaskQueueConfig* tqconfig);

/// void tqPresetManual(TaskQueueConfig *tqconfig)
///
/// Initialize a queue config for manual ticking without a thread pool.
/// Queue must be ticked manually with tqTick(). Suitable for integration with
/// existing event loops.
/// @param tqconfig Configuration structure to initialize
void tqPresetManual(_Out_ TaskQueueConfig* tqconfig);

/// void tqEnableMonitor(TaskQueueConfig *tqconfig)
///
/// Update an existing config to enable sensible defaults for queue monitoring.
/// Enables detection of stuck tasks, long-running tasks, and tasks waiting too long.
/// @param tqconfig Configuration structure to update
void tqEnableMonitor(_Inout_ TaskQueueConfig *tqconfig);

/// @}

/// @defgroup tq_lifecycle Task Queue Lifecycle
/// @{


/// TaskQueue *tqCreate(strref name, TaskQueueConfig *tqconfig)
///
/// Create a task queue but does not start it.
/// Call tqStart() after creation to begin processing tasks.
/// @param name Name for the queue (used in logging and monitoring)
/// @param tqconfig Configuration created with one of the tqPreset functions
/// @return New task queue instance, or NULL on failure
_Ret_opt_valid_ _Check_return_
TaskQueue *tqCreate(_In_ strref name, _In_ TaskQueueConfig *tqconfig);

/// bool tqStart(TaskQueue *tq)
///
/// Start or re-start a queue to begin processing tasks.
/// Spawns worker threads and begins task execution.
/// @param tq Task queue to start
/// @return true if queue was started successfully
#define tqStart taskqueueStart

/// int64 tqTick(TaskQueue *tq)
///
/// Tick a manual queue to process one or more tasks.
/// Only valid for queues created with TQ_Manual flag.
/// @param tq Manual task queue to tick
/// @return Time until next scheduled task, or -1 if no tasks pending
#define tqTick taskqueueTick

/// bool tqShutdown(TaskQueue *tq, int64 wait)
///
/// Attempt to shut down a queue.
/// If wait is nonzero, will wait up to that duration for workers to exit.
/// @param tq Task queue to shut down
/// @param wait Maximum time to wait for shutdown, or 0 for non-blocking
/// @return true if the queue is completely shut down and all worker threads are finished
#define tqShutdown(tq, wait) taskqueueStop(tq, wait)

/// void tqRelease(TaskQueue **ptq)
///
/// Release a reference to a task queue.
/// Decrements refcount and destroys queue when it reaches zero.
/// @param ptq Pointer to task queue pointer (set to NULL after release)
#define tqRelease(ptq) objRelease(ptq)

/// @}

/// @defgroup tq_task_ops Task Operations
/// @{


/// bool tqAdd(TaskQueue *tq, BasicTask *task)
///
/// Add a task to the queue to run immediately.
/// The caller retains ownership of the task reference.
/// @param tq Task queue to add task to
/// @param task Task to add (any type derived from BasicTask)
/// @return true if task was added successfully
#define tqAdd(tq, task) taskqueueAdd(tq, task)

/// void tqRun(TaskQueue *tq, BasicTask **ptask)
///
/// Convenience function to add a task to the queue and release it.
/// This is the most common pattern for fire-and-forget tasks.
/// @param tq Task queue to run task on
/// @param ptask Pointer to task pointer (task is released after adding)
#define tqRun(tq, ptask) do { taskqueueAdd(tq, *ptask); objRelease(ptask); } while(0)

_meta_inline bool _tqSchedule(_Inout_ TaskQueue* tq, _In_ ComplexTask* task, int64 delay)
{
    ComplexTaskQueue* ctq = objDynCast(ComplexTaskQueue, tq);
    if (!ctq)
        return false;
    return ctaskqueueSchedule(ctq, task, delay);
}
/// bool tqSchedule(TaskQueue *tq, ComplexTask *task, int64 delay)
///
/// Add a task to the queue to run after a delay.
/// Requires a ComplexTask-derived task and a ComplexTaskQueue.
/// @param tq Task queue (must support complex tasks)
/// @param task Complex task to schedule
/// @param delay Delay in system time units before task should run
/// @return true if task was scheduled successfully
#define tqSchedule(tq, task, delay) _tqSchedule(tq, ComplexTask(task), delay)

_meta_inline bool _tqDefer(_Inout_ TaskQueue* tq, _In_ ComplexTask* task)
{
    ComplexTaskQueue* ctq = objDynCast(ComplexTaskQueue, tq);
    if (!ctq)
        return false;
    return ctaskqueueDefer(ctq, task);
}
/// bool tqDefer(TaskQueue *tq, ComplexTask *task)
///
/// Add a task to the queue but defer it indefinitely.
/// Task will not run until explicitly advanced with taskAdvance() or ctaskAdvance().
/// Useful for tasks waiting on external events.
/// @param tq Task queue (must support complex tasks)
/// @param task Complex task to defer
/// @return true if task was deferred successfully
#define tqDefer(tq, task) _tqDefer(tq, ComplexTask(task))

/// Generic callback mechanism for basic use that doesn't need to create classes.
/// The callback should return true for success, false for failure.
/// @param tq Task queue the callback is running on
/// @param data User-provided data pointer
/// @return true for success, false for failure
typedef bool (*UserTaskCB)(TaskQueue *tq, void *data);

/// bool tqCall(TaskQueue *tq, UserTaskCB func, void *userdata)
///
/// Runs a custom function on a thread in a task queue's worker pool.
/// This is a simplified interface for cases where creating a task class is overkill.
/// @param tq Task queue to run callback on
/// @param func Callback function to execute
/// @param userdata Optional data pointer passed to callback
/// @return true if callback task was queued successfully
bool tqCall(_Inout_ TaskQueue *tq, _In_ UserTaskCB func, _In_opt_ void *userdata);

/// int32 tqWorkers(TaskQueue *tq)
///
/// Returns the current number of worker threads in the queue's pool.
/// For manual queues, returns 0.
/// @param tq Task queue to query
/// @return Number of active worker threads
int32 tqWorkers(_In_ TaskQueue *tq);

/// @}

/// @defgroup tq_task_state Task State Queries
/// @{


_meta_inline uint32 _btaskState(BasicTask *bt)
{
    return atomicLoad(uint32, &bt->state, Acquire) & TASK_State_Mask;
}
/// uint32 btaskState(BasicTask *task)
///
/// Get the current state of a task.
/// @param task Task to query (any type derived from BasicTask)
/// @return Task state (one of TASK_Created, TASK_Waiting, TASK_Running, etc.)
#define btaskState(task) _btaskState(BasicTask(task))
/// uint32 taskState(Task *task)
///
/// Get the current state of a task.
/// @param task Task to query
/// @return Task state (one of TASK_Created, TASK_Waiting, TASK_Running, etc.)
#define taskState(task) _btaskState(BasicTask(task))

_meta_inline bool _btaskIsRunning(BasicTask *bt)
{
    return _btaskState(bt) == TASK_Running;
}
/// bool btaskIsRunning(BasicTask *task)
///
/// Check if a task is currently running on a worker.
/// @param task Task to query
/// @return true if task is in TASK_Running state
#define btaskIsRunning(task) _btaskIsRunning(BasicTask(task))
/// bool taskIsRunning(Task *task)
///
/// Check if a task is currently running on a worker.
/// @param task Task to query
/// @return true if task is in TASK_Running state
#define taskIsRunning(task) _btaskIsRunning(BasicTask(task))

_meta_inline bool _btaskIsPending(BasicTask *bt)
{
    uint32 state = _btaskState(bt);
    return state == TASK_Waiting || state == TASK_Deferred || state == TASK_Scheduled;
}
/// bool btaskIsPending(BasicTask *task)
///
/// Check if a task is pending (waiting, deferred, or scheduled).
/// @param task Task to query
/// @return true if task has not yet run and has not completed
#define btaskIsPending(task) _btaskIsPending(BasicTask(task))
/// bool taskIsPending(Task *task)
///
/// Check if a task is pending (waiting, deferred, or scheduled).
/// @param task Task to query
/// @return true if task has not yet run and has not completed
#define taskIsPending(task) _btaskIsPending(BasicTask(task))

_meta_inline bool _btaskIsIdle(BasicTask *bt)
{
    uint32 state = _btaskState(bt);
    return state == TASK_Created || state == TASK_Waiting || state == TASK_Deferred ||
        state == TASK_Scheduled;
}
/// bool btaskIsIdle(BasicTask *task)
///
/// Check if a task is idle (created, waiting, deferred, or scheduled but not running).
/// @param task Task to query
/// @return true if task has never run or is not currently running
#define btaskIsIdle(task) _btaskIsIdle(BasicTask(task))
/// bool taskIsIdle(Task *task)
///
/// Check if a task is idle (created, waiting, deferred, or scheduled but not running).
/// @param task Task to query
/// @return true if task has never run or is not currently running
#define taskIsIdle(task) _btaskIsIdle(BasicTask(task))

_meta_inline bool _btaskIsComplete(BasicTask* bt)
{
    uint32 state = _btaskState(bt);
    return state == TASK_Succeeded || state == TASK_Failed;
}
/// bool btaskIsComplete(BasicTask *task)
///
/// Check if a task has completed (either succeeded or failed).
/// @param task Task to query
/// @return true if task is in a final state (TASK_Succeeded or TASK_Failed)
#define btaskIsComplete(task) _btaskIsComplete(BasicTask(task))
/// bool taskIsComplete(Task *task)
///
/// Check if a task has completed (either succeeded or failed).
/// @param task Task to query
/// @return true if task is in a final state (TASK_Succeeded or TASK_Failed)
#define taskIsComplete(task) _btaskIsComplete(BasicTask(task))

_meta_inline bool _btaskSucceeded(BasicTask *bt)
{
    return _btaskState(bt) == TASK_Succeeded;
}
/// bool btaskSucceeded(BasicTask *task)
///
/// Check if a task completed successfully.
/// @param task Task to query
/// @return true if task is in TASK_Succeeded state
#define btaskSucceeded(task) _btaskSucceeded(BasicTask(task))
/// bool taskSucceeded(Task *task)
///
/// Check if a task completed successfully.
/// @param task Task to query
/// @return true if task is in TASK_Succeeded state
#define taskSucceeded(task) _btaskSucceeded(BasicTask(task))

_meta_inline bool _btaskFailed(BasicTask *bt)
{
    return _btaskState(bt) == TASK_Failed;
}
/// bool btaskFailed(BasicTask *task)
///
/// Check if a task failed.
/// @param task Task to query
/// @return true if task is in TASK_Failed state
#define btaskFailed(task) _btaskFailed(BasicTask(task))
/// bool taskFailed(Task *task)
///
/// Check if a task failed.
/// @param task Task to query
/// @return true if task is in TASK_Failed state
#define taskFailed(task) _btaskFailed(BasicTask(task))

_meta_inline bool _btaskCancelled(BasicTask *bt)
{
    return atomicLoad(uint32, &bt->state, Acquire) & TASK_Cancelled;
}
/// bool btaskCancelled(BasicTask *task)
///
/// Check if a task has been cancelled.
/// Cancelled flag is independent of state and can be set on running or pending tasks.
/// @param task Task to query
/// @return true if TASK_Cancelled flag is set
#define btaskCancelled(task) _btaskCancelled(BasicTask(task))
/// bool taskCancelled(Task *task)
///
/// Check if a task has been cancelled.
/// Cancelled flag is independent of state and can be set on running or pending tasks.
/// @param task Task to query
/// @return true if TASK_Cancelled flag is set
#define taskCancelled(task) _btaskCancelled(BasicTask(task))

/// bool taskCancel(Task *task)
///
/// Request cancellation of a task.
/// The task will be marked cancelled and may be stopped if possible.
/// @param task Task to cancel
/// @return true if cancellation was successful
#define taskCancel(task) btaskCancel(task)

/// bool taskWait(Task *task, int64 timeout)
///
/// Wait for a task to complete with optional timeout.
/// @param task Task to wait for
/// @param timeout Maximum time to wait, or 0 to wait indefinitely
/// @return true if task completed within timeout
#define taskWait(task, timeout) ftaskWait(task, timeout)

/// @}
/// @}

