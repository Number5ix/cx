#pragma once

#include <cx/cx.h>
#include <cx/buffer/buffer.h>
#include <cx/thread/atomic.h>
#include <cx/thread/prqueue.h>

/// @file bufpool.h
/// @brief Thread-safe pool of fixed-size buffers

/// @defgroup buffer_bufpool Buffer Pool
/// @ingroup buffer
/// @{
///
/// A thread-safe freelist of fixed-size buffers, for code paths that need to acquire and release
/// buffers continuously and cannot afford to allocate on every operation.
///
/// The motivating case is asynchronous I/O. Completion-based APIs require the caller to own a
/// receive buffer for the entire duration of an operation, so a busy socket needs a supply of
/// them at all times; allocating and freeing per packet puts the allocator on the hot path and
/// fragments the heap. A pool turns that into a pop and a push on a lock-free queue, with zero
/// allocation in steady state.
///
/// The pool grows on demand up to a configured cap and never grows past it. Once the cap is
/// reached and no buffers are free, bufpoolGet() returns NULL and the caller decides what to do.
/// This is deliberate: an unbounded pool converts a transient overload into memory exhaustion,
/// and does it fastest under exactly the flood that is least deserving of the help. `max * bufsz`
/// is the pool's entire memory ceiling and should be a number that can be stated up front.
///
/// Callers must handle a NULL return. What that means is protocol-dependent -- a datagram
/// receiver can simply drop the packet and count it, while a stream receiver must instead stop
/// posting receives and let the transport's own flow control apply the backpressure.
///
/// @note All operations are thread-safe. Buffers may be acquired on one thread and released on
/// another.
///
/// Example:
/// @code
///   BufPool pool;
///   bufpoolInit(&pool, 2048, 64, 4096);   // 2KB buffers, 64 preallocated, 4096 max
///
///   Buffer buf = bufpoolGet(&pool);
///   if (!buf) {
///       ++dropped;                        // at the cap; shed load rather than allocate
///   } else {
///       // ... use buf ...
///       bufpoolPut(&pool, &buf);          // buf is NULL after this
///   }
///
///   bufpoolDestroy(&pool);
/// @endcode

typedef struct BufPool {
    PrQueue freelist;        ///< Buffers available for reuse
    size_t bufsz;            ///< Size of every buffer in this pool
    uint32 max;              ///< Hard cap on live buffers (0 = unlimited)
    atomic(uint32) count;    ///< Buffers currently in existence, pooled or checked out
} BufPool;

/// Initialize a buffer pool.
///
/// Preallocates `initial` buffers so that a burst of traffic immediately after startup does not
/// pay for allocation. The pool will grow beyond that on demand, but never past `max`.
///
/// Sizing guidance: the pool needs to cover the in-flight window -- roughly the number of
/// outstanding operations plus whatever is queued awaiting processing -- with headroom.
/// Under-sizing shows up as failed bufpoolGet() calls, which is why the caller should always
/// count them. Over-sizing costs only address space.
///
/// @param pool Pointer to the pool to initialize
/// @param bufsz Size in bytes of each buffer in the pool
/// @param initial Number of buffers to preallocate
/// @param max Maximum number of live buffers, or 0 for no limit
void bufpoolInit(_Out_ BufPool* pool, size_t bufsz, uint32 initial, uint32 max);

/// Acquire a buffer from the pool.
///
/// Returns a buffer of the pool's configured size with `len` reset to 0. Reuses a free buffer if
/// one is available, otherwise allocates a new one, unless doing so would exceed the cap.
///
/// @param pool Pointer to the pool
/// @return A buffer, or NULL if the pool is at its cap with none free
/// @note The returned buffer must not be resized. Return it with bufpoolPut(); destroying it
/// with bufDestroy() will permanently shrink the pool's effective capacity.
_Must_inspect_result_ _Ret_maybenull_ Buffer bufpoolGet(_Inout_ BufPool* pool);

/// Return a buffer to the pool.
///
/// Ownership transfers back to the pool and the caller's pointer is set to NULL. If the freelist
/// cannot accept the buffer, it is destroyed instead; this is always safe, it only means the
/// pool will allocate again later.
///
/// @param pool Pointer to the pool
/// @param buf Pointer to the buffer to return (set to NULL on return)
_At_(*buf, _Pre_maybenull_ _Post_null_) void bufpoolPut(_Inout_ BufPool* pool,
                                                        _Inout_ Buffer* buf);

/// Get the number of buffers currently checked out of the pool.
///
/// Intended for diagnostics. The value is a snapshot and may be stale the moment it is read.
///
/// @param pool Pointer to the pool
/// @return Approximate number of buffers currently in use
uint32 bufpoolInUse(_In_ BufPool* pool);

/// Run a garbage-collection pass on the pool's internal freelist.
///
/// The freelist is a dynamic queue that grows on demand up to the cap and reclaims that growth
/// only lazily, when this runs. Call it periodically from a thread that is otherwise idle so the
/// pool can shrink back after a burst rather than holding its high-water footprint forever. It is
/// non-blocking: if another thread is already collecting, or there is nothing to reclaim, it
/// returns at once. Purely an optimization -- a pool that never collects still works correctly.
///
/// @param pool Pointer to the pool
void bufpoolCollect(_Inout_ BufPool* pool);

/// Destroy a buffer pool and free all pooled buffers.
///
/// All buffers must have been returned first. Any that are still checked out are leaked, since
/// the pool has no way to reach them.
///
/// @param pool Pointer to the pool to destroy
void bufpoolDestroy(_Pre_valid_ _Post_invalid_ BufPool* pool);

/// @}  // end of bufpool group
