/// @file thread.h
/// @brief Threading system aggregated header

/// @defgroup thread Threading
/// @{
///
/// Cross-platform threading, synchronization primitives, and atomic operations.
///
/// The CX threading system provides a lightweight abstraction over OS threading APIs with
/// support for Windows, UNIX/POSIX, and WebAssembly platforms. It includes:
/// - Thread creation, management, and priority control
/// - Atomic operations (works on MSVC without C11)
/// - Synchronization primitives: Mutex, RWLock, Event, Semaphore
/// - Priority queues and task management
///
/// Threads use reference counting for memory management. Always release threads with
/// thrRelease() when done.

#pragma once

#include <cx/thread/atomic.h>
#include <cx/thread/sema.h>
#include <cx/thread/thread.h>
#include <cx/thread/mutex.h>
#include <cx/thread/rwlock.h>
#include <cx/thread/event.h>
#include <cx/thread/prqueue.h>

/// @}
// end of thread group
