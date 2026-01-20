#pragma once

#include <cx/cx.h>

/// @file buffer.h
/// @brief Simple buffer management

/// @defgroup buffer_simple Simple Buffers
/// @ingroup buffer
/// @{
///
/// Simple dynamically-sized buffers with header metadata.
///
/// Buffers are heap-allocated structures that store arbitrary binary data along with
/// size and length tracking. They support resizing and optional allocation.
///
/// Example:
/// @code
///   buffer buf = bufCreate(1024);
///   memcpy(buf->data, mydata, datalen);
///   buf->len = datalen;
///   bufResize(&buf, 2048);
///   bufDestroy(&buf);
/// @endcode

/// Simple buffer with size tracking
///
/// The buffer structure contains metadata (size and length) followed by the actual data.
/// Access the data through the `data` member as a flexible array.
struct BufferHeader {
    union {
        void* _is_buffer;
        size_t sz;      ///< Total allocated size of buffer in bytes
    };
    size_t len;     ///< Length of valid data currently in buffer
    uint8 data[];   ///< Buffer data (flexible array member)
};

/// buffer bufCreate(size_t size)
///
/// Create a new buffer with the specified size.
/// @param size The size in bytes to allocate for the buffer
/// @return A newly allocated buffer (never NULL)
_Ret_notnull_ buffer bufCreate(size_t size);

/// buffer bufTryCreate(size_t size)
///
/// Create a new buffer with optional allocation (may fail).
///
/// Uses optional allocation which will return NULL on out-of-memory instead of
/// terminating the program. Useful for large allocations that may fail.
/// @param size The size in bytes to allocate for the buffer
/// @return A newly allocated buffer, or NULL if allocation failed
_Must_inspect_result_ _Ret_maybenull_ buffer bufTryCreate(size_t size);

/// void bufResize(buffer* buf, size_t newsize)
///
/// Resize an existing buffer to a new size.
///
/// If the buffer pointer is NULL, creates a new buffer with the specified size.
/// If resizing smaller than current length, the length is truncated.
/// @param buf Pointer to buffer pointer to resize (may be NULL)
/// @param newsize New size in bytes for the buffer
_At_(*buf, _Pre_maybenull_ _Post_notnull_)
void bufResize(_Inout_ buffer* buf, size_t newsize);

/// bool bufTryResize(buffer* buf, size_t newsize)
///
/// Resize an existing buffer with optional allocation (may fail).
///
/// Like bufResize() but uses optional allocation and returns false on failure
/// instead of terminating the program.
/// @param buf Pointer to buffer pointer to resize (may be NULL)
/// @param newsize New size in bytes for the buffer
/// @return true if resize succeeded, false if allocation failed
_At_(*buf, _Pre_maybenull_)
bool bufTryResize(_Inout_ buffer* buf, size_t newsize);

/// void bufDestroy(buffer* buf)
///
/// Destroy a buffer and free its memory.
///
/// Sets the buffer pointer to NULL after freeing.
/// @param buf Pointer to buffer pointer to destroy
_At_(*buf, _Pre_maybenull_ _Post_null_)
void bufDestroy(_Inout_ buffer* buf);

/// @}  // end of buffer_simple group
