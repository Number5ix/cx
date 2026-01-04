#pragma once

/// @file cx/taskqueue/taskqueue_shared.h
/// @brief Shared types and configuration structures for the task queue system
///
/// @defgroup taskqueue_types Task Queue Types
/// @ingroup taskqueue
/// @{
///
/// Core types, enums, and configuration structures used throughout the task queue system.

typedef struct TaskQueue TaskQueue;
typedef struct LogCategory LogCategory;
typedef struct Event Event;

/// Callback for UI event processing in task queue workers.
///
/// Called by task queue workers when configured to handle UI events. Should return true if all
/// UI events were processed, or false if there are more pending events and the worker should
/// continue looping.
/// @param tq The task queue requesting UI processing
/// @return true if all UI events processed, false if more events remain
typedef bool (*TQUICallback)(TaskQueue* tq);

/// Task queue configuration flags
enum TaskQueueFlagsEnum {
    /// Use a dedicated manager thread for queue maintenance instead of stealing idle worker time.
    ///
    /// Without this flag, maintenance (purging completed tasks, releasing deferred tasks) is
    /// scheduled on idle worker threads. With it, a dedicated manager thread performs maintenance
    /// on a schedule and as needed. Recommended for heavy workloads.
    TQ_ManagerThread = 0x0001,

    /// Enable queue monitoring to detect and report stalled tasks.
    ///
    /// Periodically checks the queue for tasks running too long, waiting too long, or deferred
    /// without progress. Emits log messages to help debug queue state issues.
    TQ_Monitor = 0x0002,

    /// Restrict queue to BasicTask and Task only (no ComplexTask features).
    ///
    /// Disables support for complex tasks with dependencies, scheduling, and resources. Uses
    /// simpler code paths with less overhead when these features aren't needed.
    TQ_NoComplex = 0x0004,

    /// Manual mode - no thread pool, tick() must be called manually.
    ///
    /// Disables automatic worker threads. The queue's tick() function must be called explicitly
    /// to process tasks. Not compatible with TQ_ManagerThread or TQ_Monitor. Useful for
    /// integration with existing event loops.
    TQ_Manual = 0x0008,

    /// In manual mode, only run one task per tick() call.
    ///
    /// Without this flag, tick() runs as many ready tasks as possible. With it, tick() runs
    /// exactly one task per call, which may be desired when interleaving task processing with
    /// other work in a loop. Only relevant in manual mode.
    TQ_Oneshot = 0x0010,
};

/// Thread pool configuration for a task queue
typedef struct TaskQueueThreadPoolConfig {
    int wInitial;      ///< Initial number of workers when starting the queue
    int wIdle;         ///< Absolute minimum workers when queue is completely idle
    int wBusy;         ///< Target number of workers when queue is busy
    int wMax;          ///< Absolute maximum number of workers

    int64 tIdle;       ///< Time before queue is considered idle (allows worker reduction)
    int64 tRampUp;     ///< Delay between adding workers to the pool
    int64 tRampDown;   ///< Delay before removing an idle worker from the pool

    int loadFactor;    ///< Average queue depth per worker that is acceptable before scaling up

    TQUICallback ui;   ///< If set, workers handle UI events using this callback
} TaskQueueThreadPoolConfig;

/// Queue monitoring configuration for detecting stalled tasks
typedef struct TaskQueueMonitorConfig {
    int64 mInterval;        ///< How often the monitor checks the queue (0 to disable)
    int64 mSuppress;        ///< Suppress repeated warnings for this duration after emitting one
    int64 mTaskRunning;     ///< Warn if a task has been running longer than this
    int64 mTaskWaiting;     ///< Warn if a task has been waiting for a worker longer than this
    int64 mTaskStalled;     ///< Warn if a deferred task hasn't made progress in this time
    LogCategory* mLogCat;   ///< Custom log category for monitor messages (NULL for default)
} TaskQueueMonitorConfig;

/// Complete configuration for a task queue
typedef struct TaskQueueConfig {
    uint32 flags;           ///< Combination of TaskQueueFlagsEnum values
    int64 mGC;              ///< How often a garbage collection cycle should run
    TaskQueueThreadPoolConfig pool;     ///< Thread pool configuration
    TaskQueueMonitorConfig monitor;     ///< Monitor configuration
} TaskQueueConfig;

/// Task state values
///
/// Task state is stored atomically and can include the TASK_Cancelled flag combined with
/// any state value. Use TASK_State_Mask to extract just the state without the cancelled flag.
enum TaskStateEnum {
    TASK_Created,       ///< Task has been created but not yet queued
    TASK_Waiting,       ///< Task is queued and waiting for a worker
    TASK_Running,       ///< Task is currently executing on a worker
    TASK_Scheduled,     ///< Task is scheduled to run at a future time
    TASK_Deferred,      ///< Task is deferred, waiting for explicit advancement
    TASK_Succeeded,     ///< Task completed successfully
    TASK_Failed,        ///< Task failed during execution
    TASK_State_Mask = 0x7fffffff,   ///< Mask to extract state value without flags
    TASK_Cancelled  = 0x80000000    ///< Flag indicating task has been cancelled (combines with state)
};

/// Task control structure for output parameters from task execution
///
/// This structure is passed to task run() methods as an output parameter, allowing tasks to
/// communicate state changes back to the task queue system without affecting the return value.
typedef struct TaskControl {
    /// Event to signal after task completes or fails.
    ///
    /// Used by Task and derived classes. If set, the task queue will signal this event when
    /// the task reaches a terminal state (TASK_Succeeded or TASK_Failed). This allows other
    /// threads to wait for task completion.
    Event* notifyev;

    /// Delay before rescheduling this task.
    ///
    /// Used by ComplexTask and derived classes when returning TASK_Result_Schedule or
    /// TASK_Result_Defer from run(). Specifies how long to wait before running the task again.
    /// A delay of -1 indicates the task should run whenever any other task completes.
    int64 delay;
} TaskControl;

/// @}
