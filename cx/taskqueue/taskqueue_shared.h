#pragma once

typedef struct TaskQueue TaskQueue;
typedef struct LogCategory LogCategory;
typedef struct Event Event;

// Callback for UI events. Should return true if all UI events were processed,
// or false if there are more pending and the worker should continue looping.
typedef bool(*TQUICallback)(TaskQueue *tq);

// Configuration for pool of queue workers
typedef struct TaskQueueConfig
{
    int wInitial;           // initial number of workers upon starting the queue
    int wIdle;              // absolute minimum number of workers when queue is completely idle
    int wBusy;              // target number of workers when the queue is busy
    int wMax;               // absolute maximum number of workers

    int64 tIdle;            // length of time before the queue is considered idle
    int64 tRampUp;          // how long to wait between addings workers to the pool
    int64 tRampDown;        // how long to wait before removing a worker to the pool

    int loadFactor;         // average queue depth per worker that is acceptable

    TQUICallback ui;        // if set, this pool is handling UI events and should use an
    // EV_UIEvent event, as well as call this callback from the worker
    // thread(s)

    // monitor configuration
    int64 mInterval;        // If nonzero, how often for the queue monitor to run
    int64 mSuppress;        // How long to suppress monitor output after a warning is emitted
    int64 mTaskRunning;     // Warn about tasks that have been running on a worker for longer than this time
    int64 mTaskWaiting;     // Warn about tasks that have been waiting for a worker to run on longer than this time
    int64 mTaskStalled;     // Warn about deferred tasks that have not made forward process in this time
    LogCategory *mLogCat;   // If set, send monitor logs to a custom category
} TaskQueueConfig;

enum TaskStateEnum
{
    TASK_Created,
    TASK_Waiting,
    TASK_Running,
    TASK_Deferred,
    TASK_Succeeded,
    TASK_Failed,
};

typedef struct TaskControl
{
    // ------ NON-BASICTASK BELOW ------
    // The fields and information below this point only apply to the full Task object and derviates.
    // BasicTask objects can only be run a single time and either succeed or fail.

    // If defer is true, the task will be returned to the queue to be run again.
    // If defertime is set to a nonzero value, it is interpreted as a relative timespan for how long to defer
    // the task before running it again.

    // If defertime is set to 0 and no progress was made, it will be deferred and not retried until the
    // queue processes at least one other task.

    // defertime may also be set to timeForever, in which case it will stay in the deferred list (and
    // be exempt from monitor warnings) until the task's advance method is called.

    bool defer;             // set if the task needs to be returned to the queue to run again
    int64 defertime;        // how long to defer this task
    bool progress;          // whether forward progress was made this iteration

    Event *notifyev;        // event to signal after task is completed or failed
} TaskControl;
