#pragma once

#include <cx/cx.h>
#include <cx/log/log.h>
#include <cx/taskqueue/tqobject.h>

// Object-oriented task queue suitable for use in heavily multithreaded applications

// Initialize a queue config for a queue with a single worker (i.e. a UI thread)
void tqPresetSingle(_Out_ TaskQueueConfig *tqconfig);

// Initialize a queue config with minimal presets based on the number of physical cores only
void tqPresetMinimal(_Out_ TaskQueueConfig *tqconfig);

// Initialize a queue config with balanced presets based on the number of logical cores
void tqPresetBalanced(_Out_ TaskQueueConfig *tqconfig);

// Update an existing config to enable sensible defaults for queue monitoring
void tqEnableMonitor(_Inout_ TaskQueueConfig *tqconfig);

// Create a task queue but does not start it
_Ret_opt_valid_ _Check_return_
TaskQueue *tqCreate(_In_ strref name, _In_ TaskQueueConfig *tqconfig);

// Start or re-start a queue.
bool tqStart(_Inout_ TaskQueue *tq);

// Attempt to shut down a queue.
// If wait is nonzero, will wait for workers to exit.
// Returns true if the queue is completely shut down and all worker threads
// are finished.
bool tqShutdown(_Inout_ TaskQueue *tq, int64 wait);

#define tqRelease(tq) objRelease(tq)

bool _tqAdd(_Inout_ TaskQueue *tq, _In_ BasicTask *task);
// bool tqAdd(TaskQueue *tq, BasicTask *task);
// Add a task to the queue to run immediately.
#define tqAdd(tq, task) _tqAdd(tq, BasicTask(task))

// void tqRun(TaskQueue *tq, BasicTask **ptask);
// Convenience function to run a task and release it.
#define tqRun(tq, ptask) do { _tqAdd(tq, BasicTask(*ptask)); objRelease(ptask); } while(0)

bool _tqDefer(_Inout_ TaskQueue *tq, _In_ Task *task, int64 delay);
// bool tqDefer(TaskQueue *tq, Task *task, int64 delay);
// Add a task to the queue to run after a delay. Does not work with basic tasks.
#define tqDefer(tq, task, delay) _tqDefer(tq, Task(task), delay)

// generic callback mechanism for basic use that doesn't need to create classes
typedef bool (*UserTaskCB)(TaskQueue *tq, void *data);

// Runs a custom function on a thread in a task queue's worker pool
bool tqCall(_Inout_ TaskQueue *tq, _In_ UserTaskCB func, _In_opt_ void *userdata);

// Returns the current number of worker threads
int32 tqWorkers(_In_ TaskQueue *tq);

_meta_inline int32 _btaskState(BasicTask *bt)
{
    return atomicLoad(int32, &bt->state, Acquire);
}
#define btaskState(task) _btaskState(BasicTask(task))

_meta_inline bool _taskCancelled(Task *t)
{
    return atomicLoad(bool, &t->cancelled, Acquire);
}
#define taskCancelled(task) _taskCancelled(Task(task))

_meta_inline void _taskCancel(Task *t)
{
    atomicStore(bool, &t->cancelled, true, Release);
}
#define taskCancel(task) _taskCancel(Task(task))
