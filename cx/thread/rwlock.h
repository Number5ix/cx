/// @file rwlock.h
/// @brief Reader-writer lock synchronization primitive
/// @defgroup thread_rwlock Reader-Writer Lock
/// @ingroup thread
/// @{
///
/// Reader-writer locks for shared read access with exclusive write access.
///
/// RWLock allows multiple threads to hold read locks simultaneously, but only one thread can
/// hold a write lock at a time (with no concurrent readers). This is ideal for data structures
/// with frequent reads and infrequent writes.
///
/// Implementation details:
/// - Based on futexes with adaptive spinning
/// - Supports timeouts for both read and write acquisition
/// - Writer-preference: pending writers block new readers
/// - Maximum limits: 4095 readers, 2047 waiting readers, 511 writers
///
/// Basic usage:
/// @code
///   RWLock lock;
///   rwlockInit(&lock);
///
///   // Multiple readers can acquire simultaneously
///   rwlockAcquireRead(&lock);
///   // read data
///   rwlockReleaseRead(&lock);
///
///   // Writers have exclusive access
///   rwlockAcquireWrite(&lock);
///   // modify data
///   rwlockReleaseWrite(&lock);
///
///   rwlockDestroy(&lock);
/// @endcode
///
/// Scoped locking:
/// @code
///   withReadLock(&lock) {
///       // read access - lock released automatically
///   }
///
///   withWriteLock(&lock) {
///       // write access - lock released automatically
///   }
/// @endcode

#pragma once

#include <cx/cx.h>
#include <cx/meta/block.h>
#include <cx/platform/base.h>
#include "aspin.h"
#include "futex.h"

#ifdef CX_LOCK_DEBUG
#include <cx/log/log.h>
#endif

CX_C_BEGIN

/// Reader-writer lock initialization flags
enum RWLOCK_Flags {
    RWLOCK_NoSpin = 1,   ///< Disable adaptive spinning, use kernel futex immediately
};

/// Maximum number of concurrent readers
#define RWLOCK_READER_MAX   4095
/// Maximum number of waiting readers
#define RWLOCK_READWAIT_MAX 2047
/// Maximum number of writers (active + pending)
#define RWLOCK_WRITER_MAX   511

#define RWLOCK_READER_MASK     (0x00000fff)
#define RWLOCK_READWAIT_MASK   (0x007ff000)
#define RWLOCK_WRITER_MASK     (0xff800000)
#define RWLOCK_READERS(state)  ((state) & RWLOCK_READER_MASK)
#define RWLOCK_READWAIT(state) (((state) & RWLOCK_READWAIT_MASK) >> 12)
#define RWLOCK_WRITERS(state)  (((state) & RWLOCK_WRITER_MASK) >> 23)
#define RWLOCK_READ_ADD        0x00000001
#define RWLOCK_READWAIT_ADD    0x00001000
#define RWLOCK_WRITE_ADD       0x00800000

/// Reader-writer lock synchronization primitive
///
/// Allows multiple concurrent readers or a single exclusive writer.
/// The state field packs reader count (12 bits), waiting reader count (11 bits),
/// and writer count (9 bits) into a single 32-bit atomic.
typedef struct RWLock {
    atomic(uint32) state;   ///< Packed lock state (readers, waiting readers, writers)
    Futex rftx;             ///< Futex for reader wait queue
    Futex wftx;             ///< Futex for writer wait queue
    AdaptiveSpin aspin;     ///< Adaptive spin state
} RWLock;

void _rwlockInit(_Out_ RWLock* l, uint32 flags);

/// void rwlockInit(RWLock *l, [flags])
///
/// Initialize a reader-writer lock for use.
///
/// Must be called before using any other rwlock operations.
/// @param l Pointer to uninitialized RWLock structure
/// @param ... (flags) Optional RWLOCK_Flags (e.g., RWLOCK_NoSpin)
#define rwlockInit(l, ...) _rwlockInit(l, opt_flags(__VA_ARGS__))

/// Attempt to acquire a read lock with a timeout
///
/// Tries to acquire shared read access, waiting up to the specified timeout. Multiple threads
/// can hold read locks simultaneously. New read acquisitions are blocked if writers are pending
/// (writer preference).
/// @param l RWLock to acquire for reading
/// @param timeout Maximum time to wait in nanoseconds (use timeForever for infinite)
/// @return true if the read lock was acquired, false if timeout expired or maximum readers reached
_When_(return == true, _Acquires_shared_lock_(*l))
    _When_(timeout == timeForever, _Acquires_shared_lock_(*l)) _When_(
        timeout != timeForever,
        _Must_inspect_result_) bool rwlockTryAcquireReadTimeout(_Inout_ RWLock* l, int64 timeout);

/// Attempt to acquire a write lock with a timeout
///
/// Tries to acquire exclusive write access, waiting up to the specified timeout. Only one
/// writer can hold the lock, and no readers can be active when a write lock is held.
/// @param l RWLock to acquire for writing
/// @param timeout Maximum time to wait in nanoseconds (use timeForever for infinite)
/// @return true if the write lock was acquired, false if timeout expired or maximum writers reached
_When_(return == true, _Acquires_exclusive_lock_(*l))
    _When_(timeout == timeForever, _Acquires_exclusive_lock_(*l)) _When_(
        timeout != timeForever,
        _Must_inspect_result_) bool rwlockTryAcquireWriteTimeout(_Inout_ RWLock* l, int64 timeout);

/// Attempt to acquire a read lock without blocking
///
/// Tries to acquire shared read access immediately, returning false if not possible.
/// Does not block or wait.
/// @param l RWLock to acquire for reading
/// @return true if the read lock was acquired, false if writers are active/pending or maximum
/// readers reached
_When_(
    return == true,
           _Acquires_shared_lock_(
               *l)) _Must_inspect_result_ _meta_inline bool rwlockTryAcquireRead(_Inout_ RWLock* l)
{
    uint32 state = atomicLoad(uint32, &l->state, Relaxed);
    // only valid when no writer locks are held or pending
    if (RWLOCK_WRITERS(state) == 0) {
        // cannot acquire if we are at the max
        if (RWLOCK_READERS(state) == RWLOCK_READER_MAX)
            return false;
        if (atomicCompareExchange(uint32,
                                  strong,
                                  &l->state,
                                  &state,
                                  state + RWLOCK_READ_ADD,
                                  Acquire,
                                  Relaxed)) {
            aspinRecordUncontended(&l->aspin);
            return true;   // got the lock
        }
    }

    // no longer have valid conditions in which this lock can be acquired
    return false;
}

/// Attempt to acquire a write lock without blocking
///
/// Tries to acquire exclusive write access immediately, returning false if not possible.
/// Does not block or wait.
/// @param l RWLock to acquire for writing
/// @return true if the write lock was acquired, false if any readers or writers are active
_When_(
    return == true,
           _Acquires_exclusive_lock_(
               *l)) _Must_inspect_result_ _meta_inline bool rwlockTryAcquireWrite(_Inout_ RWLock* l)
{
    uint32 state = atomicLoad(uint32, &l->state, Relaxed);
    // only valid when no other writer locks are held, and there are no (active) readers
    if (RWLOCK_WRITERS(state) == 0 && RWLOCK_READERS(state) == 0) {
        // make sure we didn't hit the limit
        if (RWLOCK_WRITERS(state) == RWLOCK_WRITER_MAX)
            return false;
        if (atomicCompareExchange(uint32,
                                  strong,
                                  &l->state,
                                  &state,
                                  state + RWLOCK_WRITE_ADD,
                                  Acquire,
                                  Relaxed)) {
            aspinRecordUncontended(&l->aspin);
            return true;   // got the lock
        }
    }

    // no longer have valid conditions in which this lock can be acquired
    return false;
}

/// Acquire a read lock, blocking until available
///
/// Blocks the calling thread until shared read access can be acquired. This is equivalent to
/// rwlockTryAcquireReadTimeout() with timeForever.
/// @param l RWLock to acquire for reading
_Acquires_shared_lock_(*l) _meta_inline void rwlockAcquireRead(_Inout_ RWLock* l)
{
    if (!rwlockTryAcquireReadTimeout(l, timeForever))
        relFatalError("Failed to acquire read lock (too many waiting readers?)");
}

/// Execute a block with automatic read lock acquisition and release
///
/// Acquires a read lock before executing the following block, and automatically releases it
/// when the block exits.
/// @param l RWLock to acquire for reading
#define withReadLock(l) blkWrap (rwlockAcquireRead(l), rwlockReleaseRead(l))

/// Execute a block with automatic write lock acquisition and release
///
/// Acquires a write lock before executing the following block, and automatically releases it
/// when the block exits.
/// @param l RWLock to acquire for writing
#define withWriteLock(l) blkWrap (rwlockAcquireWrite(l), rwlockReleaseWrite(l))

#ifdef CX_LOCK_DEBUG
#define _logFmtRwlockArgComp2(level, fmt, nargs, args) \
    _logFmt_##level(LOG_##level, LogDefault, fmt, nargs, args)
#define _logFmtRwlockArgComp(level, fmt, ...) \
    _logFmtRwlockArgComp2(level, fmt, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ })
_Acquires_shared_lock_(*l)
    _meta_inline bool rwlockLogAndAcquireRead(_Inout_ RWLock* l, const char* name,
                                              const char* filename, int line)
{
    _logFmtRwlockArgComp(CX_LOCK_DEBUG,
                         _S"Locking rwlock ${string} for READ at ${string}:${int}",
                         stvar(string, (string)name),
                         stvar(string, (string)filename),
                         stvar(int32, line));
    return rwlockAcquireRead(l);
}

#define rwlockAcquireRead(l) rwlockLogAndAcquireRead(l, #l, __FILE__, __LINE__)
#endif

/// Acquire a write lock, blocking until available
///
/// Blocks the calling thread until exclusive write access can be acquired. This is equivalent to
/// rwlockTryAcquireWriteTimeout() with timeForever.
/// @param l RWLock to acquire for writing
_Acquires_exclusive_lock_(*l) _meta_inline void rwlockAcquireWrite(_Inout_ RWLock* l)
{
    if (!rwlockTryAcquireWriteTimeout(l, timeForever))
        relFatalError("Failed to acquire write lock (too many waiting writers?)");
}

#ifdef CX_LOCK_DEBUG
_Acquires_exclusive_lock_(*l)
    _meta_inline bool rwlockLogAndAcquireWrite(_Inout_ RWLock* l, const char* name,
                                               const char* filename, int line)
{
    _logFmtRwlockArgComp(CX_LOCK_DEBUG,
                         _S"Locking rwlock ${string} for WRITE at ${string}:${int}",
                         stvar(string, (string)name),
                         stvar(string, (string)filename),
                         stvar(int32, line));
    return rwlockAcquireWrite(l);
}

#define rwlockAcquireWrite(l) rwlockLogAndAcquireWrite(l, #l, __FILE__, __LINE__)
#endif

/// Release a previously acquired read lock
///
/// Releases shared read access. If this was the last reader and writers are waiting,
/// one writer will be awakened.
/// @param l RWLock to release
/// @return true on success
_Releases_shared_lock_(*l) _meta_inline bool rwlockReleaseRead(_Inout_ RWLock* l)
{
    devAssert(RWLOCK_READERS(atomicLoad(uint32, &l->state, Relaxed)) > 0);
    uint32 oldstate = atomicFetchSub(uint32, &l->state, RWLOCK_READ_ADD, Release);

    // If we were the last reader and any writers are waiting, unblock one
    if (RWLOCK_READERS(oldstate) == 1 && RWLOCK_WRITERS(oldstate) > 0) {
        devVerify(atomicFetchAdd(int32, &l->wftx.val, 1, Relaxed) == 0);
        futexWake(&l->wftx);
    }

    return true;
}

#ifdef CX_LOCK_DEBUG
_Releases_shared_lock_(*l)
    _meta_inline bool rwlockLogAndReleaseRead(_Inout_ RWLock* l, const char* name,
                                              const char* filename, int line)
{
    _logFmtRwlockArgComp(CX_LOCK_DEBUG,
                         _S"Releasing rwlock ${string} for READ at ${string}:${int}",
                         stvar(string, (string)name),
                         stvar(string, (string)filename),
                         stvar(int32, line));
    return rwlockReleaseRead(l);
}

#define rwlockReleaseRead(l) rwlockLogAndReleaseRead(l, #l, __FILE__, __LINE__)
#endif

/// Release a previously acquired write lock
///
/// Releases exclusive write access, allowing waiting readers or writers to acquire the lock.
/// @param l RWLock to release
/// @return true on success
_Releases_exclusive_lock_(*l) bool rwlockReleaseWrite(_Inout_ RWLock* l);

#ifdef CX_LOCK_DEBUG
_Releases_exclusive_lock_(*l)
    _meta_inline bool rwlockLogAndReleaseWrite(_Inout_ RWLock* l, const char* name,
                                               const char* filename, int line)
{
    _logFmtRwlockArgComp(CX_LOCK_DEBUG,
                         _S"Releasing rwlock ${string} for WRITE at ${string}:${int}",
                         stvar(string, (string)name),
                         stvar(string, (string)filename),
                         stvar(int32, line));
    return rwlockReleaseWrite(l);
}

#define rwlockReleaseWrite(l) rwlockLogAndReleaseWrite(l, #l, __FILE__, __LINE__)
#endif

/// Atomically downgrade a write lock to a read lock
///
/// Converts exclusive write access to shared read access without releasing the lock.
/// This operation is guaranteed not to block. Useful when transitioning from a write
/// operation to a read operation without allowing other writers to interfere.
/// @param l RWLock currently held for writing
/// @return true on success
_Releases_exclusive_lock_(*l)
    _Acquires_shared_lock_(*l) bool rwlockDowngradeWrite(_Inout_ RWLock* l);

/// Destroy a reader-writer lock and release its resources
///
/// Cleans up the RWLock after use. The lock must not be held when destroyed.
/// After destruction, the lock must be reinitialized before it can be used again.
/// @param l RWLock to destroy
void rwlockDestroy(_Pre_valid_ _Post_invalid_ RWLock* l);

CX_C_END

/// @}
// end of thread_rwlock group
