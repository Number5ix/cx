/// @file scratch.h
/// @brief Thread-local rotating scratch buffers for temporary storage
///
/// @defgroup utils_scratch Scratch Buffers
/// @ingroup utils
/// @{
///
/// Scratch buffers provide thread-local temporary storage through a rotating buffer pool.
/// Each thread maintains its own set of buffers that are automatically allocated on demand
/// and reused across function calls within the same thread.
///
/// **Critical Usage Rules:**
/// 1. **Do NOT save pointers** to scratch buffers in any persistent structure
/// 2. **Do NOT share pointers** across thread boundaries
/// 3. **Do NOT use in recursive functions** - buffers rotate and will be overwritten
/// 4. **Be aware of function calls** - any function using scratch buffers may invalidate
///    buffers the caller was using
/// 5. **Keep sizes reasonable** - large allocations defeat the purpose
/// 6. **Not for performance-critical paths** - allocation has overhead
///
/// **Buffer Pool Behavior:**
/// - Each thread has @ref SCRATCH_NBUFFERS rotating buffers
/// - Buffers are allocated on first use and reused thereafter
/// - Buffers resize automatically if too small or excessively large
/// - Memory is NOT zero-filled - assume uninitialized content
///
/// Example:
/// @code
///   // Get temporary buffer for string formatting
///   char *temp = scratchGet(256);
///   sprintf(temp, "Value: %d", value);
///   processString(temp);  // Use immediately
///   // Don't save 'temp' - it may be overwritten by next scratchGet call
/// @endcode

#pragma once

#include <cx/cx.h>
#include <cx/utils/compare.h>

/// Number of rotating buffers per thread (must be power of 2)
#define SCRATCH_NBUFFERS 32

/// Minimum size for scratch buffer allocations
#define SCRATCH_MIN_BUFFER_SIZE 64

/// Maximum size before buffer is considered unreasonably large
#define SCRATCH_MAX_REASONABLE_BUFFER_SIZE 1024

CX_C_BEGIN

/// Per-thread scratch buffer state
typedef struct ScratchPerThreadInfo {
    int32 next;                    ///< Next buffer index to allocate
    void* buf[SCRATCH_NBUFFERS];   ///< Rotating buffer pool
} ScratchPerThreadInfo;

// Thread-local pointer to scratch buffer state (internal use)
extern _Thread_local ScratchPerThreadInfo* _scratch_pti;

/// void *scratchGet(size_t sz)
///
/// Allocates a temporary scratch buffer from the thread-local rotating pool.
///
/// Returns the next buffer in the rotation, resizing if necessary. The buffer is NOT
/// zero-filled and may contain stale data from previous uses. Buffers rotate after
/// @ref SCRATCH_NBUFFERS allocations, so earlier buffers may be overwritten.
///
/// **Warning**: Do not assume the returned pointer remains valid after calling other
/// functions that may use scratch buffers, or after @ref SCRATCH_NBUFFERS additional
/// scratchGet calls.
///
/// @param sz Required buffer size in bytes (minimum @ref SCRATCH_MIN_BUFFER_SIZE)
/// @return Pointer to scratch buffer (never NULL)
_Ret_notnull_ inline void* scratchGet(size_t sz)
{
    if (!_scratch_pti) {
        _scratch_pti = (ScratchPerThreadInfo*)xaAlloc(sizeof(ScratchPerThreadInfo), XA_Zero);
    }

    int32 cur          = _scratch_pti->next;
    _scratch_pti->next = (_scratch_pti->next + 1) & (SCRATCH_NBUFFERS - 1);

    sz = clamplow(sz, SCRATCH_MIN_BUFFER_SIZE);
    if (!_scratch_pti->buf[cur]) {
        _scratch_pti->buf[cur] = xaAlloc(sz);
    } else {
        size_t cursz = xaSize(_scratch_pti->buf[cur]);

        if (cursz < sz ||
            (cursz > SCRATCH_MAX_REASONABLE_BUFFER_SIZE &&
             sz < SCRATCH_MAX_REASONABLE_BUFFER_SIZE)) {
            // xaResize would copy data we don't care about
            xaFree(_scratch_pti->buf[cur]);
            _scratch_pti->buf[cur] = xaAlloc(sz);
        }
    }

    return _scratch_pti->buf[cur];
}

CX_C_END

/// @}
