/// @file prqueue.h
/// @brief Lock-free pointer FIFO queue
/// @defgroup thread_prqueue PrQueue
/// @ingroup thread
/// @{
///
/// A thread-safe, lock-free, optionally growable ring buffer of pointers. Multiple
/// threads can push and pop at the same time.
///
/// PrQueue is low-level plumbing for building other concurrent structures, such as
/// containers or a work queue, rather than a general-purpose collection to reach for
/// directly. It moves only raw `void*` pointers - it never dereferences, allocates,
/// frees, or copies whatever they point to. All lifetime management of the pointed-to
/// data is the caller's responsibility.
///
/// NULL may never be pushed into the queue. prqPop() uses NULL as the "queue is
/// empty" sentinel, so inserting one is an error.
///
/// Pushing a pointer transfers ownership of it to the queue - don't touch it again
/// after a successful prqPush(). Popping transfers ownership back to the caller.
///
/// @section thread_prqueue_fixed_dynamic Fixed vs. dynamic queues
///
/// @code
///   PrQueue q;
///   prqInitFixed(&q, 1024);   // fixed capacity; push fails when full
///
///   // or, a queue that grows and shrinks between bounds:
///   PrQueue q2;
///   prqInitDynamic(&q2, 64, 1024, 65536, PRQ_Grow_100, PRQ_Grow_50);
/// @endcode
///
/// A fixed queue never grows, and prqPush() simply fails when it's full. A dynamic
/// queue grows toward its target size under load and shrinks back within its bounds,
/// but every push and pop on a dynamic queue pays extra atomic bookkeeping to guard
/// against a segment being freed out from under it, often close to double the atomic
/// operations of a fixed queue doing the same work. Prefer a fixed queue whenever
/// there's a defensible upper bound on depth; reach for dynamic only when the depth
/// genuinely can't be bounded.
///
/// @section thread_prqueue_lockfree Lock-free guarantees
///
/// Pushing and popping are lock-free in the classical sense: a thread that suspends
/// or terminates in the middle of an operation cannot corrupt the queue or
/// permanently block other threads, though it can cost performance until it clears.
///
/// Garbage collection (prqCollect()) is the one exception. It reclaims buffer
/// segments that were retired when a dynamic queue grew, and it does use a lock - but
/// that lock never blocks the caller: if it can't be acquired, prqCollect() returns
/// immediately instead of waiting. Call it opportunistically at natural idle points,
/// such as a consumer thread about to go to sleep. GC is not required for
/// correctness; a queue that never runs GC keeps working, it just holds on to
/// retired segments and wastes memory after growth events. A thread that stalls in
/// the middle of a push also blocks GC from pruning until it clears, for the same
/// reason. Fixed queues never grow, so they never need GC.
///
/// @section thread_prqueue_ordering Ordering guarantees
///
/// Pushes from a single thread are popped in order, as long as a single thread (not
/// necessarily the same one) pops them sequentially. Across multiple threads,
/// ordering is only best-effort: pushes and pops generally complete in something
/// close to real-time order, but operations happening at nearly the same time on
/// different threads may be reordered slightly. Don't build anything that needs
/// strict global ordering on top of this queue; rely only on the per-pair FIFO
/// guarantee.

#pragma once

#include <cx/cx.h>
#include <cx/thread/aspin.h>
#include <cx/thread/atomic.h>
#include <cx/thread/mutex.h>

#if DEBUG_LEVEL >= 2 && _64BIT
#define PRQ_PERF_STATS
#endif
CX_C_BEGIN

/// How much a dynamic PrQueue grows or shrinks by when it resizes
typedef enum PrqGrowthEnum {
    PRQ_Grow_None = 1,   ///< Do not grow/shrink at all
    PRQ_Grow_25,         ///< Resize by 25%
    PRQ_Grow_50,         ///< Resize by 50%
    PRQ_Grow_100,        ///< Resize by 100% (default)
    PRQ_Grow_150,        ///< Resize by 150%
    PRQ_Grow_200,        ///< Resize by 200%
} PrqGrowth;

typedef struct PrqSegment PrqSegment;

#ifdef PRQ_PERF_STATS
typedef struct PrqPerfStats {
    atomic(uint64) grow;
    atomic(uint64) grow_collision;
    atomic(uint64) shrink;
    atomic(uint64) shrink_collision;
    atomic(uint64) head_contention;
    atomic(uint64) reserved_contention;
    atomic(uint64) push;
    atomic(uint64) push_optimal;
    atomic(uint64) push_fast;
    atomic(uint64) push_slow;
    atomic(uint64) push_appeared_full;
    atomic(uint64) push_actually_full;
    atomic(uint64) push_collision;
    atomic(uint64) push_retry;
    atomic(uint64) push_full_retry;
    atomic(uint64) push_noreserve_retiring;
    atomic(uint64) pop;
    atomic(uint64) pop_optimal;
    atomic(uint64) pop_fast;
    atomic(uint64) pop_slow;
    atomic(uint64) pop_nonobvious_empty;
    atomic(uint64) pop_assist;
    atomic(uint64) pop_assist_fail;
    atomic(uint64) pop_segtraverse;
    atomic(uint64) pop_collision;
    atomic(uint64) gc_run;
    atomic(uint64) seg_retired;
    atomic(uint64) seg_dealloc;
    atomic(uint64) seg_dealloc_failinuse;
} PrqPerfStats;
#endif

/// Lock-free pointer FIFO queue
///
/// Access it only through the prq* functions - there is no supported direct field
/// access.
typedef struct PrQueue {
    // Initial size of the queue as well as minimum size.
    uint32 minsz;

    // Ideal size of the queue.
    uint32 targetsz;

    // Maximum size of the queue.
    uint32 maxsz;

    // How much to grow the queue at a time.
    PrqGrowth growth;

    // How much to shrink the queue at a time.
    PrqGrowth shrink;

    // Concurrence factor. Defaults to the number of logcal CPUs in the system. This value is
    // used to determine how many queue entries there must be before threads start assisting
    // each other to complete operations.
    uint32 concurrence;

    // Buffer segment that is currently the target for queue pushes and pops. The head of a linked
    // list of buffer segments when the queue is being grown.
    atomic(ptr) current;

    // Linked list of buffer segments that have been retired. These are segments that have been
    // fully emptied and no longer have any valid queue entries, nor can they have any entries
    // pushed into them, but are being held to deallocate later until it can be guaranteed that
    // no threads attempting a pop operation still have a pointer to the segment.
    PrqSegment* retired;

    // Access counter. This is used internally to close a small gap between when a thread retrieves
    // the current segment pointers and when it increments the use counter, since it cannot do that
    // atomically while another thread is retiring the segment.
    atomic(int32) access;

    // Lower 32 bits of timestamp of the last time a segment was added to grow or shrink the queue,
    // because this needs to be atomic and 64-bit atomics don't exist on all platforms.
    atomic(uint32) chgtime;

    // Minimum number of milliseconds the queue must wait to shrink after growing or shrinking
    // (default 500ms).
    uint32 shrinkms;

    // Running average to track the total queue size across GC cycles for possible shrinking.
    uint32 avgcount;
    uint32 avgcount_num;

    // Only 1 thread may run the garbage collection operation at a time. It's recommended to try to
    // run GC optimistcally when a thread has nothing else to do. For example, a consumer thread
    // that is about to sleep.
    Mutex gcmtx;

#ifdef PRQ_PERF_STATS
    // Performance stats for debugging
    PrqPerfStats stats;
#endif
} PrQueue;

typedef struct PrqSegment {
    // Next segment in the chain. When the queue grows, it allocates a larger segment which is
    // temporarily chained to from the original segment in order to handle the transition while
    // many other threads may be still using the original.
    atomic(ptr) nextseg;

    // Next retired segment in retired chain.
    // NOTE: We cannot reuse nextseg for this. nextseg needs to continue to point to the actual
    // segment that replaced this one, so that any threads which grabbed a pointer to this segment
    // before it was retired can still follow it.
    PrqSegment* nextretired;

    // Atomic counter of how many threads are actively using this segment. This, along with the
    // access counter, act as a non-blocking optimistic lock similar to a reader-writer lock but
    // less intrusive. They block garbage collection from deallocating this segment if there is
    // a chance that a thread may still be reading from it (or about to read from it).
    atomic(int32) inuse;

    // Total number of queue slots in this buffer.
    uint32 size;

    // Number of queue slots in this buffer that are used. This number may be slightly higher
    // than the actual number of slots that has been written to. This is the authoritative
    // source for how much of the queue is used.
    atomic(uint32) count;

    // Head of the queue; points at the slot that is first in line to be read. This is cached
    // information for performance optimization only and is not authoritative.
    atomic(uint32) head;

    // Number of write reservations on this segment. Only used when the buffer is expandable, to
    // prevent GC from retiring the segment while there are pending write operations. The high
    // bit is used to signal that the segment is transitioning to the retired status and further
    // writes may not be started.
    atomic(uint32) reserved;

    // The actual ringbuffer
    atomic(ptr) buffer[];
} PrqSegment;

/// Initialize a fixed-size PrQueue
///
/// The queue never grows past sz slots; prqPush() fails once it is full. This is
/// the cheaper of the two flavors to operate, and the one to prefer whenever the
/// maximum depth is known ahead of time.
///
/// Always succeeds, or asserts.
/// @param prq Pointer to uninitialized queue structure
/// @param sz Fixed capacity, in pointer slots
void prqInitFixed(_Out_ PrQueue* prq, uint32 sz);

/// Initialize a growable PrQueue
///
/// The queue starts at minsz slots, grows toward targetsz (and up to maxsz) as it
/// fills, and shrinks back down again as load drops. growth and shrink control how
/// large each resize step is.
///
/// Always succeeds, or asserts.
/// @param prq Pointer to uninitialized queue structure
/// @param minsz Minimum and initial size, in pointer slots
/// @param targetsz Size the queue tries to reach under load
/// @param maxsz Maximum size it will ever grow to
/// @param growth How much to grow by at a time
/// @param shrink How much to shrink by at a time
void prqInitDynamic(_Out_ PrQueue* prq, uint32 minsz, uint32 targetsz, uint32 maxsz,
                    PrqGrowth growth, PrqGrowth shrink);

/// Destroy a PrQueue and release its resources
///
/// Fails if the queue still holds any entries, since this is a low-level API with no
/// idea what the stored pointers mean or how to clean them up. The caller must pop
/// and dispose of everything, and make sure no thread is still pushing, before
/// calling this.
/// @param prq Queue to destroy
/// @return true on success, false if entries remain
_Success_(return) bool prqDestroy(_Pre_valid_ _Post_invalid_ PrQueue* prq);

/// Push a pointer into the queue
///
/// ptr must not be NULL. On success, the queue owns the pointer; don't touch it
/// again until it comes back out of a prqPop() call.
/// @param prq Queue to push into
/// @param ptr Pointer to push. Must not be NULL
/// @return true on success, false if the queue is full and cannot grow
_Success_(return) bool prqPush(_Inout_ PrQueue* prq, _Pre_notnull_ _Post_invalid_ void* ptr);

/// Pop a pointer from the queue
///
/// @param prq Queue to pop from
/// @return The next pointer in the queue, or NULL if the queue is empty
_Must_inspect_result_ _Ret_maybenull_ void* prqPop(_Inout_ PrQueue* prq);

/// Run one garbage collection cycle on the queue
///
/// Reclaims buffer segments that were retired by a previous growth event. Never
/// blocks: if the internal GC lock is already held by another thread, this returns
/// immediately without doing anything. Call it opportunistically, such as from a
/// consumer thread that is about to go idle.
///
/// Not needed for correctness, and a no-op on a fixed queue, which never retires
/// segments.
/// @param prq Queue to run a GC cycle on
/// @return true if the cycle ran, whether or not it collected anything
bool prqCollect(_Inout_ PrQueue* prq);

/// Get an estimated count of items in the queue
///
/// This is only an estimate, and its accuracy drops the busier the queue is. Use
/// prqPop() returning NULL as the authoritative test for "empty," not a count of
/// zero from this function.
/// @param prq Queue to inspect
/// @return Approximate number of valid items currently in the queue
uint32 prqCount(_In_ PrQueue* prq);

/// Fetch a copy of the nth pointer in the queue without removing it
///
/// @warning This is dangerous. By the time the returned pointer is examined, another
/// thread may already have popped and destroyed whatever it points to, so using it is
/// almost certain to crash unless the caller has external guarantees about which
/// threads pop items and what they do with them, and the pointed-to data is itself
/// thread-safe. Only use this in tightly controlled situations; it is not a general
/// substitute for prqPop().
/// @param prq Queue to inspect
/// @param n Index of the item to fetch, starting from the head of the queue
/// @return Copy of the nth pointer
void* prqPeek(_In_ PrQueue* prq, uint32 n);

CX_C_END

/// @}
// end of thread_prqueue group
