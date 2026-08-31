#pragma once

#include <cx/cx.h>

CX_C_BEGIN

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
///   Buffer buf = bufCreate(1024);
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
        size_t sz;   ///< Total allocated size of buffer in bytes
    };
    size_t len;      ///< Length of valid data currently in buffer
    uint8 data[];    ///< Buffer data (flexible array member)
};

/// Platform-neutral scatter/gather vector.
///
/// Describes a single contiguous region of memory as part of a vectored I/O operation. This is
/// not the same layout as the OS's own type (`struct iovec` on Unix, `WSABUF` on Windows) --
/// translate into the platform's array immediately before the syscall that needs it.
///
/// @see bufchainGatherIov
typedef struct BufIov {
    void* data;   ///< Pointer to the start of this region
    size_t len;   ///< Number of valid bytes in this region
} BufIov;

/// Buffer bufCreate(size_t size)
///
/// Create a new buffer with the specified size.
/// @param size The size in bytes to allocate for the buffer
/// @return A newly allocated buffer (never NULL)
_Ret_notnull_ Buffer bufCreate(size_t size);

/// Buffer bufTryCreate(size_t size)
///
/// Create a new buffer with optional allocation (may fail).
///
/// Uses optional allocation which will return NULL on out-of-memory instead of
/// terminating the program. Useful for large allocations that may fail.
/// @param size The size in bytes to allocate for the buffer
/// @return A newly allocated buffer, or NULL if allocation failed
_Must_inspect_result_ _Ret_maybenull_ Buffer bufTryCreate(size_t size);

/// void bufResize(Buffer* buf, size_t newsize)
///
/// Resize an existing buffer to a new size.
///
/// If the buffer pointer is NULL, creates a new buffer with the specified size.
/// If resizing smaller than current length, the length is truncated.
/// @param buf Pointer to buffer pointer to resize (may be NULL)
/// @param newsize New size in bytes for the buffer
_At_(*buf, _Pre_maybenull_ _Post_notnull_) void bufResize(_Inout_ Buffer* buf, size_t newsize);

/// bool bufTryResize(Buffer* buf, size_t newsize)
///
/// Resize an existing buffer with optional allocation (may fail).
///
/// Like bufResize() but uses optional allocation and returns false on failure
/// instead of terminating the program.
/// @param buf Pointer to buffer pointer to resize (may be NULL)
/// @param newsize New size in bytes for the buffer
/// @return true if resize succeeded, false if allocation failed
_At_(*buf, _Pre_maybenull_) bool bufTryResize(_Inout_ Buffer* buf, size_t newsize);

/// size_t bufLen(Buffer buf)
///
/// Number of valid bytes in a buffer.
///
/// @param buf Buffer to measure; NULL counts as empty
/// @return Length of the valid data
_Pure _meta_inline size_t bufLen(_In_opt_ Buffer buf)
{
    return buf ? buf->len : 0;
}

/// void bufClear(Buffer buf)
///
/// Discards a buffer's contents without freeing its memory.
///
/// The allocation is kept, so a buffer used over and over as scratch space stops reallocating
/// once it has grown to the size it needs.
///
/// @param buf Buffer to empty; NULL is ignored
_meta_inline void bufClear(_Inout_opt_ Buffer buf)
{
    if (buf)
        buf->len = 0;
}

/// uint8* bufReserve(Buffer* buf, size_t len)
///
/// Makes room for `len` more bytes and returns where to write them.
///
/// The buffer grows if it has to, and creates one if the pointer is NULL. The length is left
/// alone, so add to it yourself once the bytes are written.
///
/// @param buf Pointer to buffer pointer to reserve space in (may be NULL)
/// @param len Number of bytes to make room for
/// @return Pointer to the first reserved byte
///
/// Example:
/// @code
///   uint8 *p = bufReserve(&buf, 4);
///   memcpy(p, "abcd", 4);
///   buf->len += 4;
/// @endcode
_At_(*buf, _Pre_maybenull_ _Post_notnull_) _Ret_notnull_ uint8* bufReserve(_Inout_ Buffer* buf,
                                                                          size_t len);

/// void bufAppendBytes(Buffer* buf, const void* data, size_t len)
///
/// Appends raw bytes to the end of a buffer.
///
/// The buffer grows if it has to, and creates one if the pointer is NULL.
///
/// @param buf Pointer to buffer pointer to append to (may be NULL)
/// @param data Bytes to append
/// @param len Number of bytes
///
/// Example:
/// @code
///   Buffer buf = 0;
///   bufAppendBytes(&buf, "hello", 5);
/// @endcode
_At_(*buf, _Pre_maybenull_ _Post_notnull_) void bufAppendBytes(_Inout_ Buffer* buf,
                                                               _In_reads_bytes_opt_(len)
                                                                   const void* data,
                                                               size_t len);

/// void bufAppend(Buffer* buf, Buffer src)
///
/// Appends the contents of one buffer to another.
///
/// @param buf Pointer to buffer pointer to append to (may be NULL)
/// @param src Buffer to copy the bytes from; NULL or empty appends nothing
///
/// Example:
/// @code
///   bufAppend(&dest, src);
///   bufDestroy(&src);
/// @endcode
_At_(*buf, _Pre_maybenull_ _Post_notnull_) void bufAppend(_Inout_ Buffer* buf, _In_opt_ Buffer src);

/// void bufAppendC(Buffer* buf, Buffer* src)
///
/// Appends one buffer to another and destroys the source.
///
/// The source is destroyed and its pointer set to NULL, so it must not be used again. When the
/// destination is empty this hands its memory over instead of copying the bytes, which is what
/// makes chaining these cheap.
///
/// @param buf Pointer to buffer pointer to append to (may be NULL)
/// @param src Pointer to the buffer to append and destroy
///
/// Example:
/// @code
///   Buffer part = bufCreate(64);
///   ...fill part...
///   bufAppendC(&whole, &part);   // part is now NULL
/// @endcode
_At_(*buf, _Pre_maybenull_) _At_(*src, _Pre_maybenull_ _Post_null_) void
bufAppendC(_Inout_ Buffer* buf, _Inout_ Buffer* src);

/// void bufDestroy(Buffer* buf)
///
/// Destroy a buffer and free its memory.
///
/// Sets the buffer pointer to NULL after freeing.
/// @param buf Pointer to buffer pointer to destroy
_At_(*buf, _Pre_maybenull_ _Post_null_) void bufDestroy(_Inout_ Buffer* buf);

/// @}  // end of buffer_simple group

CX_C_END