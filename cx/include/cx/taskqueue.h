#pragma once

/// @file cx/taskqueue.h
/// @brief Task queue system for multithreaded task execution
///
/// @defgroup taskqueue Task Queue
/// @{
///
/// The CX task queue system provides an object-oriented framework for managing
/// asynchronous task execution in multithreaded applications. It supports everything from simple
/// fire-and-forget tasks to complex dependency graphs with resource management.
///
/// @section tq_architecture Architecture Overview
///
/// The task queue system is built on several layers:
/// - **Task objects**: Encapsulate units of work (BasicTask, Task, ComplexTask, MultiphaseTask)
/// - **Task queues**: Manage task scheduling and execution (TaskQueue, ComplexTaskQueue)
/// - **Workers**: Execute tasks on threads or manually (TQWorker, TQThreadWorker, TQManualWorker)
/// - **Resources**: Manage serialized access to shared resources (TRMutex, TRFifo, TRLifo)
/// - **Requirements**: Express task dependencies and resource needs (TaskRequires, TaskRequiresGate)
///
/// @section tq_task_types Task Types
///
/// **BasicTask** - Bare minimum task with no dependencies or scheduling. Just a run method and state tracking.
/// Suitable for simple operations that need to run on a worker thread.
///
/// **Task** - Extends BasicTask with a name, timing information, and completion callbacks. Can wait
/// for completion with timeouts.
///
/// **ComplexTask** - Extends Task with support for:
/// - Task dependencies (wait for other tasks)
/// - Resource requirements (mutexes, FIFO/LIFO ordering)
/// - Scheduling (run at specific times or after delays)
/// - Deferment (hold until explicitly advanced)
/// - Soft failures (continue even if dependencies fail)
///
/// **MultiphaseTask** (MPTask) - Extends ComplexTask for multi-phase operations with an internal
/// state machine. Phases can be added as an array of functions that execute sequentially, with
/// optional fail phases for error handling.
///
/// @section tq_queue_modes Queue Modes and Presets
///
/// Task queues can be configured in various modes using preset functions:
///
/// **Single Thread** (`tqPresetSingle`) - One worker thread for UI or single-threaded work:
/// - 1 worker thread at all times
/// - Simple serial execution
///
/// **Minimal** (`tqPresetMinimal`) - Lightweight threading for background work:
/// - Starts with 1 worker, grows to half the physical CPU count when busy
/// - Maximum workers = physical CPU count
/// - Suitable for applications that don't heavily rely on task queues
///
/// **Balanced** (`tqPresetBalanced`) - General-purpose configuration:
/// - 2 workers when idle (1 on single-core systems)
/// - Grows to physical CPU count when busy
/// - Maximum workers = logical CPU count (includes hyperthreading)
/// - Fast ramp-up (20ms), moderate ramp-down (500ms)
/// - Default for most applications
///
/// **Heavy** (`tqPresetHeavy`) - High-performance under sustained load:
/// - 4 workers when idle (2 on dual-core systems)
/// - Grows to all logical CPUs when busy
/// - Maximum workers = 150% of logical CPU count
/// - Dedicated manager thread for queue maintenance
/// - Very fast ramp-up (10ms), slow ramp-down (1000ms)
/// - More aggressive garbage collection
///
/// **Manual** (`tqPresetManual`) - No worker threads, call `tqTick()` manually:
/// - Suitable for integration with existing event loops
/// - Can run one task per tick (TQ_Oneshot flag) or all available tasks
/// - No manager or monitor thread
///
/// @section tq_flags Configuration Flags
///
/// **TQ_ManagerThread** - Use a dedicated thread for queue maintenance instead of stealing
/// idle worker time. Recommended for heavy workloads.
///
/// **TQ_Monitor** - Enable queue monitoring for debugging stuck tasks. Reports tasks that are
/// running too long, waiting too long, or stalled without progress.
///
/// **TQ_NoComplex** - Restrict queue to BasicTask/Task only, disabling ComplexTask features.
/// Reduces overhead when dependencies and scheduling aren't needed.
///
/// **TQ_Manual** - No thread pool; queue must be ticked manually with `tqTick()`.
///
/// **TQ_Oneshot** - In manual mode, only run one task per tick instead of all available tasks.
///
/// @section tq_resources Task Resources
///
/// ComplexTask can require resources before execution:
///
/// **TRMutex** - Mutex-based serialization. Simple and efficient for moderate contention.
/// Tasks are woken in approximate FIFO order.
///
/// **TRFifo** - Strictly ordered FIFO queue. Guarantees serialized execution in registration
/// order. Scales efficiently to large numbers of waiting tasks.
///
/// **TRLifo** - LIFO (stack) ordering for specialized use cases where most recent requesters
/// should be serviced first.
///
/// **TRGate** - One-time event gate. Multiple tasks can wait on a gate that opens once,
/// releasing all waiting tasks. Can be sealed to fail tasks that depend on it.
///
/// @section tq_dependencies Task Dependencies
///
/// ComplexTask supports several types of dependencies:
///
/// @code
/// ctaskDependOn(task, dep);        // Wait for dep to complete successfully
/// ctaskWaitFor(task, dep);         // Wait for dep, but don't fail if dep fails
/// ctaskRequireResource(task, res); // Acquire exclusive resource
/// ctaskRequireGate(task, gate);    // Wait for gate to open
/// @endcode
///
/// Dependencies can have timeouts:
/// @code
/// ctaskDependOnTimeout(task, dep, timeS(30));  // Fail if dep not done in 30s
/// @endcode
///
/// @section tq_scheduling Task Scheduling
///
/// ComplexTask can be scheduled to run at future times:
///
/// @code
/// tqSchedule(tq, task, timeMS(100));  // Run after 100ms delay
/// tqDefer(tq, task);                  // Hold until manually advanced
/// taskAdvance(task);                  // Release a deferred task
/// @endcode
///
/// Tasks can also reschedule themselves by returning TASK_Result_Schedule or TASK_Result_Defer
/// from their run method, with the delay specified in the TaskControl structure.
///
/// @section tq_monitoring Queue Monitoring
///
/// Enable monitoring with `tqEnableMonitor()` to debug task queue issues:
/// - Detects tasks running longer than expected
/// - Detects tasks waiting too long for a worker
/// - Detects deferred tasks that haven't made progress
/// - Configurable warning thresholds and suppression intervals
///
/// @section tq_example Basic Usage Example
///
/// @code
/// // Create and configure queue
/// TaskQueueConfig config;
/// tqPresetBalanced(&config);
/// TaskQueue *tq = tqCreate(_S"MyQueue", &config);
/// tqStart(tq);
///
/// // Create and run a task
/// MyTask *task = mytaskCreate();
/// tqRun(tq, &task);  // Adds and releases
///
/// // Shutdown
/// tqShutdown(tq, timeS(5));
/// objRelease(&tq);
/// @endcode
///
/// @}

/// @defgroup tq_task Task Objects
/// @ingroup taskqueue
/// @{
///
/// Task object types that encapsulate units of work for execution on task queues.
///
/// The task queue system provides several task types with increasing levels of functionality:
/// - **BasicTask** - Minimal task with just execution and state tracking
/// - **Task** - Adds naming, timing, and completion notification
/// - **ComplexTask** - Adds dependencies, resources, and scheduling
/// - **MultiphaseTask** - Supports multi-phase execution with state machines
///
/// @}

#include <cx/taskqueue/taskqueue.h>
