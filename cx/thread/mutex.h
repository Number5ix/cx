/// @file mutex.h
/// @brief Mutex synchronization primitive
/// @defgroup thread_mutex Mutex
/// @ingroup thread
/// @{
///
/// Fast, adaptive mutexes for exclusive access to shared resources.
///
/// The CX Mutex implementation uses futexes and adaptive spinning for efficiency:
/// - Uncontended locks complete in user space without kernel calls
/// - Adaptive spinning for short waits before falling back to kernel futex
/// - Supports timeouts for bounded waiting
///
/// Mutexes are NOT reentrant - attempting to acquire the same mutex twice from the
/// same thread will deadlock.
///
/// Basic usage:
/// @code
///   Mutex m;
///   mutexInit(&m);
///
///   mutexAcquire(&m);
///   // critical section
///   mutexRelease(&m);
///
///   mutexDestroy(&m);
/// @endcode
///
/// Scoped locking with automatic release:
/// @code
///   withMutex(&m) {
///       // critical section - mutex released automatically at end of block
///   }
/// @endcode

#pragma once

#include <cx/cx.h>
#include <cx/meta/block.h>
#include <cx/time/time.h>
#include <cx/utils/macros.h>
#include "aspin.h"
#include "futex.h"

#ifdef CX_LOCK_DEBUG
#include <cx/log/log.h>
#endif

CX_C_BEGIN

/// Mutex initialization flags
enum MUTEX_Flags {
    MUTEX_NoSpin = 1,   ///< Disable adaptive spinning, use kernel futex immediately
};

/// Mutex synchronization primitive
///
/// Internal structure using futex-based locking with adaptive spinning.
/// Futex values: 0=unlocked, 1=locked (no waiters), 2=locked with contention
typedef struct Mutex {
    Futex ftx;            ///< Futex for kernel-level synchronization
    AdaptiveSpin aspin;   ///< Adaptive spin state
} Mutex;

void _mutexInit(_Out_ Mutex* m, uint32 flags);

/// void mutexInit(Mutex *m, [flags])
///
/// Initialize a mutex for use.
///
/// Must be called before using any other mutex operations.
/// @param m Pointer to uninitialized mutex structure
/// @param ... (flags) Optional MUTEX_Flags (e.g., MUTEX_NoSpin)
#define mutexInit(m, ...) _mutexInit(m, opt_flags(__VA_ARGS__))

/// Attempt to acquire a mutex with a timeout
///
/// Tries to acquire the mutex, waiting up to the specified timeout. Uses adaptive spinning
/// before falling back to kernel waits for efficiency.
/// @param m Mutex to acquire
/// @param timeout Maximum time to wait in nanoseconds (use timeForever for infinite)
/// @return true if the mutex was acquired, false if timeout expired
_When_(return == true, _Acquires_nonreentrant_lock_(*m))
    _When_(timeout == timeForever, _Acquires_nonreentrant_lock_(*m))
        _When_(timeout != timeForever,
               _Must_inspect_result_) bool mutexTryAcquireTimeout(_Inout_ Mutex* m, int64 timeout);

/// Release a previously acquired mutex
///
/// Releases the mutex so other threads can acquire it. Must be called from the same thread
/// that acquired the mutex. Undefined behavior if called on a mutex not currently held.
/// @param m Mutex to release
/// @return true on success
_Releases_nonreentrant_lock_(*m) bool mutexRelease(_Inout_ Mutex* m);

/// Attempt to acquire a mutex without blocking
///
/// Tries to acquire the mutex immediately, returning false if it is already held by another
/// thread. Does not block or wait.
/// @param m Mutex to acquire
/// @return true if the mutex was acquired, false if already held
_When_(return == true,
              _Acquires_nonreentrant_lock_(
                  *m)) _Must_inspect_result_ _meta_inline bool mutexTryAcquire(_Inout_ Mutex* m)
{
    int32 curstate = atomicLoad(int32, &m->ftx.val, Relaxed);
    if (curstate == 0 &&
        atomicCompareExchange(int32, strong, &m->ftx.val, &curstate, 1, Acquire, Relaxed)) {
        aspinRecordUncontended(&m->aspin);
        return true;
    }
    return false;
}

/// Acquire a mutex, blocking until it becomes available
///
/// Blocks the calling thread until the mutex can be acquired. This is equivalent to
/// mutexTryAcquireTimeout() with timeForever.
/// @param m Mutex to acquire
_Acquires_nonreentrant_lock_(*m) _meta_inline void mutexAcquire(_Inout_ Mutex* m)
{
    mutexTryAcquireTimeout(m, timeForever);
}

/// Execute a block with automatic mutex locking and unlocking
///
/// Acquires the mutex before executing the following block, and automatically releases it
/// when the block exits (including early returns, breaks, or exceptions).
///
/// Example:
/// @code
///   withMutex(&myMutex) {
///       // critical section
///       if (error)
///           break;  // mutex automatically released
///   }
/// @endcode
/// @param m Mutex to lock for the duration of the block
#define withMutex(m) blkWrap (mutexAcquire(m), mutexRelease(m))

#ifdef CX_LOCK_DEBUG
#define _logFmtMutexArgComp2(level, fmt, nargs, args) \
    _logFmt_##level(LOG_##level, LogDefault, fmt, nargs, args)
#define _logFmtMutexArgComp(level, fmt, ...) \
    _logFmtMutexArgComp2(level, fmt, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ })
_Acquires_nonreentrant_lock_(*m)
    _meta_inline bool mutexLogAndAcquire(_Inout_ Mutex* m, const char* name, const char* filename,
                                         int line)
{
    _logFmtMutexArgComp(CX_LOCK_DEBUG,
                        _S"Locking mutex ${string} at ${string}:${int}",
                        stvar(string, (string)name),
                        stvar(string, (string)filename),
                        stvar(int32, line));
    return mutexAcquire(m);
}

#define mutexAcquire(m) mutexLogAndAcquire(m, #m, __FILE__, __LINE__)
#endif

#ifdef CX_LOCK_DEBUG
_Releases_nonreentrant_lock_(*m)
    _meta_inline bool mutexLogAndRelease(_Inout_ Mutex* m, const char* name, const char* filename,
                                         int line)
{
    _logFmtMutexArgComp(CX_LOCK_DEBUG,
                        _S"Releasing mutex ${string} at ${string}:${int}",
                        stvar(string, (string)name),
                        stvar(string, (string)filename),
                        stvar(int32, line));
    return mutexRelease(m);
}

#define mutexRelease(m) mutexLogAndRelease(m, #m, __FILE__, __LINE__)
#endif

/// Destroy a mutex and release its resources
///
/// Cleans up the mutex after use. The mutex must not be held when destroyed.
/// After destruction, the mutex must be reinitialized before it can be used again.
/// @param m Mutex to destroy
void mutexDestroy(_Pre_valid_ _Post_invalid_ Mutex* m);

CX_C_END

/// @}
// end of thread_mutex group
