#pragma once

#include <cx/thread/atomic.h>

// Thread-safe reference counting implementation

typedef atomic(intptr) refcount;

// Initialize a reference counter
_meta_inline void refcountInit(refcount *rc)
{
    // Initialize to -1, which is a magic value that's used for the single-threaded optimization.
    // It's considered to be the same as 1, but acts as a flag that the reference count has never
    // been incremented, making it safe to skip thread synchronization.
    *(intptr*)rc = -1;
}

// Increment a reference counter
_meta_inline void refcountInc(refcount *rc)
{
    // When acquiring a reference, if the magic value is present, this counter has never been
    // incremented before. This is safe to do without atomic operations because by convention,
    // references must not be passed between threads without first acquiring one to give to
    // the target thread.

    // The first time it is incremented, we pretend that the previous value was 1 and jump directly
    // to 2. A SeqCst fence is used for this case to avoid the small possibility that a different
    // CPU picked up the -1 value by loading a neighboring variable from the same cache line.
    // Essentially we get better performance in the single-threaded case by paying a one-time cost
    // when the refcounted variable transitions from single-threaded to (potentially) multi-threaded
    // mode.

    if (*(intptr*)rc != -1)
        atomicFetchAdd(intptr, rc, 1, Relaxed);
    else
        atomicStore(intptr, rc, 2, SeqCst);
}

// Decrement a reference counter. Returns true if the new count is 0 and the relevant object
// should be destroyed.
_meta_inline bool refcountDec(refcount *rc)
{
    // Single-threaded optimization: If this counter was never incremented, just return true so it
    // can be immediately destroyed
    if (*(intptr*)rc == -1)
        return true;

    if (atomicFetchSub(intptr, rc, 1, Release) == 1) {
        // This fence is important! It ensures proper ordering if the last two references are
        // released by separate threads at nearly the same time.
        atomicFence(Acquire);
        return true;
    }
    return false;
}
