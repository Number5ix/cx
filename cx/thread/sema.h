/// @file sema.h
/// @brief Counting semaphore synchronization primitive
/// @defgroup thread_sema Semaphore
/// @ingroup thread
/// @{
///
/// A counting semaphore for controlling access to a resource with a limited number
/// of concurrent users, or for producer/consumer hand-off of a count of available
/// items.
///
/// A semaphore holds a non-negative count. semaDec() (and its timed/non-blocking
/// variants) waits until the count is greater than zero, then decrements it.
/// semaInc() adds to the count and wakes any waiters that can now proceed.
///
/// Basic usage:
/// @code
///   Semaphore s;
///   semaInit(&s, 0);   // start with no available slots
///
///   // Producer: make one item available
///   semaInc(&s, 1);
///
///   // Consumer: wait for an item to become available
///   semaDec(&s);
///
///   semaDestroy(&s);
/// @endcode

#pragma once

#include <cx/cx.h>
#include <cx/thread/atomic.h>
#include <cx/time/time.h>
#include <cx/utils/macros.h>
#include "aspin.h"
#include "futex.h"

CX_C_BEGIN

/// Semaphore initialization flags
enum SEMA_Flags {
    SEMA_NoSpin = 0x00000001,   ///< Disable adaptive spinning, use kernel futex immediately
};

/// Counting semaphore synchronization primitive
typedef struct Semaphore {
    Futex ftx;            ///< Futex holding the current count
    AdaptiveSpin aspin;   ///< Adaptive spin state
} Semaphore;

void _semaInit(_Out_ Semaphore* sema, int32 count, uint32 flags);

/// void semaInit(Semaphore *sema, int32 count, [flags])
///
/// Initialize a semaphore for use.
///
/// Must be called before using any other semaphore operations.
/// @param sema Pointer to uninitialized semaphore structure
/// @param count Initial count. Must not be negative
/// @param ... (flags) Optional SEMA_Flags (e.g., SEMA_NoSpin)
#define semaInit(sema, count, ...) _semaInit(sema, count, opt_flags(__VA_ARGS__))

/// Destroy a semaphore and release its resources
///
/// Cleans up the semaphore after use. After destruction, the semaphore must be
/// reinitialized before it can be used again.
/// @param sema Semaphore to destroy
/// @return true on success
bool semaDestroy(_Pre_valid_ _Post_invalid_ Semaphore* sema);

/// Attempt to decrement a semaphore with a timeout
///
/// Waits up to the specified timeout for the count to become greater than zero, then
/// decrements it. Uses adaptive spinning before falling back to kernel waits for
/// efficiency.
/// @param sema Semaphore to decrement
/// @param timeout Maximum time to wait in nanoseconds (use timeForever for infinite)
/// @return true if the count was decremented, false if the timeout elapsed
bool semaTryDecTimeout(_Inout_ Semaphore* sema, int64 timeout);

/// Attempt to decrement a semaphore without blocking
///
/// Decrements the count immediately if it is greater than zero, returning false
/// otherwise. Does not block or wait.
/// @param sema Semaphore to decrement
/// @return true if the count was decremented, false if it was already zero
_meta_inline bool semaTryDec(_Inout_ Semaphore* sema)
{
    int32 curcount = atomicLoad(int32, &sema->ftx.val, Relaxed);
    bool ret       = (curcount > 0 &&
                atomicCompareExchange(int32,
                                      strong,
                                      &sema->ftx.val,
                                      &curcount,
                                      curcount - 1,
                                      Acquire,
                                      Relaxed));
    if (ret)
        aspinRecordUncontended(&sema->aspin);
    return ret;
}

/// Decrement a semaphore, blocking until the count is available
///
/// Blocks the calling thread until the count is greater than zero, then decrements
/// it. This is equivalent to semaTryDecTimeout() with timeForever.
/// @param sema Semaphore to decrement
/// @return true on success
_meta_inline bool semaDec(_Inout_ Semaphore* sema)
{
    return semaTryDecTimeout(sema, timeForever);
}

/// Increment a semaphore's count
///
/// Adds count to the semaphore and wakes up to that many waiting threads.
/// @param sema Semaphore to increment
/// @param count Amount to add to the count
/// @return true on success
_meta_inline bool semaInc(_Inout_ Semaphore* sema, int32 count)
{
    atomicFetchAdd(int32, &sema->ftx.val, count, Release);
    futexWakeMany(&sema->ftx, count);
    return true;
}

CX_C_END

/// @}
// end of thread_sema group
