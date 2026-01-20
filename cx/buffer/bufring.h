#pragma once

#include <cx/cx.h>
#include "cx/buffer/buffer.h"
#include "cx/utils/compare.h"

/// @file bufring.h
/// @brief Ring buffer implementation for efficient streaming I/O

/// @defgroup buffer_bufring Buffer Ring
/// @ingroup buffer
/// @{
///
/// A ring buffer that provides efficient streaming I/O with minimal memory allocation
/// overhead and automatic capacity expansion. Data is read from the head and written to the
/// tail, with automatic capacity expansion and cleanup as needed.
///
/// Key features:
/// - Ring buffer architecture for efficient wraparound without data movement
/// - Automatic capacity expansion when space is exhausted
/// - Automatic cleanup when data is consumed to reduce memory usage
/// - Persistent base buffer is retained and reused to reduce allocation thrashing
/// - Zero-copy operations for high-performance I/O
/// - Peek and skip operations for protocol parsing
///
/// The minimum segment size is 64 bytes. Smaller values will be clamped to this minimum.
///
/// @note For optimal performance, this implementation is not thread-safe. If access from
/// multiple threads is required, wrap operations with an appropriate lock.
///
/// @todo This is a generic and expanded version of the streambuf ring buffer implementation
/// that supports indefinite expansion instead of only 2 buffers. The streambuf module should
/// be refactored at some point to use this new implementation.
///
/// Example:
/// @code
///   BufRing ring;
///   bufringInit(&ring, 4096);
///
///   // Write data
///   bufringWrite(&ring, data, dataLen);
///
///   // Read data
///   uint8 buffer[1024];
///   size_t nread = bufringRead(&ring, buffer, sizeof(buffer));
///
///   bufringDestroy(&ring);
/// @endcode

typedef struct BufRingNode BufRingNode;
typedef struct BufRingNode {
    BufRingNode* next;   ///< Next segment for capacity expansion
    buffer buf;          ///< Buffer containing data
    size_t head;         ///< Read position in buf
    size_t tail;         ///< Write position in buf
    bool full;           ///< Distinguish full vs empty when head == tail
} BufRingNode;

typedef struct BufRing {
    BufRingNode* head;   ///< First segment
    BufRingNode* tail;   ///< Last segment
    size_t total;        ///< Total bytes of data available
    size_t segsz;        ///< Size of each buffer segment
} BufRing;

/// Callback for zero-copy buffer reads.
///
/// The callback is invoked for each contiguous segment of data in the ring buffer.
/// It must consistently return the same value (true or false) for all invocations during
/// a single read operation, as the ring buffer only supports all-or-nothing consumption.
///
/// @param buf Pointer to the contiguous data buffer segment
/// @param bytes Number of valid bytes in this buffer segment
/// @param ctx User-defined context pointer
/// @return true to consume all data from the read operation, false to retain all data
typedef bool (*bufringZCCB)(_In_reads_bytes_(bytes) const uint8* buf, size_t bytes,
                            _Pre_opt_valid_ void* ctx);

/// Callback for feeding data into a ring buffer.
///
/// The callback is invoked to write data directly into the ring buffer's internal storage.
/// This enables efficient data transfer without intermediate copying. The callback should
/// write up to maxbytes into the provided buffer and return the actual number of bytes
/// written.
///
/// If the callback returns 0, the feed operation stops. The callback may be invoked multiple
/// times to fill the requested amount.
///
/// @param buf Pointer to the buffer where data should be written
/// @param maxbytes Maximum number of bytes that can be written
/// @param ctx User-defined context pointer
/// @return Number of bytes actually written (0 to stop feeding)
typedef size_t (*bufringFeedCB)(_Out_writes_bytes_(maxbytes) uint8* buf, size_t maxbytes,
                                _Pre_opt_valid_ void* ctx);

/// Initialize an empty ring buffer.
///
/// Creates an empty ring buffer with the specified segment size. Additional capacity will be
/// allocated as needed when data is written. The segment size will be clamped to a minimum
/// of 64 bytes.
///
/// @param ring Pointer to the ring buffer to initialize
/// @param segsz Size of each buffer segment in bytes (minimum 64)
void bufringInit(_Out_ BufRing* ring, size_t segsz);

/// Read data from a ring buffer into a user buffer.
///
/// Reads up to the specified number of bytes from the ring buffer and removes them from
/// the buffer. Data is read sequentially from the head. Exhausted capacity is automatically
/// freed, except the base segment which is retained for reuse.
///
/// @param ring Pointer to the ring buffer to read from
/// @param buf Pointer to the user buffer to read data into
/// @param bytes Maximum number of bytes to read
/// @return Number of bytes actually read (may be less if buffer has insufficient data)
size_t bufringRead(_Inout_ BufRing* ring, _Out_writes_bytes_(bytes) uint8* buf, size_t bytes);

/// Read data from a ring buffer into a buffer object.
///
/// Convenience function that reads data from the ring buffer into an existing buffer object.
/// Updates the buffer's length field to reflect the number of bytes actually read. The maximum
/// bytes read is limited by both maxbytes and the buffer's allocated size.
///
/// @param ring Pointer to the ring buffer to read from
/// @param buf Buffer object to read data into
/// @param maxbytes Maximum number of bytes to read
/// @return Number of bytes actually read (stored in buf->len)
_meta_inline size_t bufringReadBuf(_Inout_ BufRing* ring, _Inout_ buffer buf, size_t maxbytes)
{
    buf->len = bufringRead(ring, buf->data, min(buf->sz, maxbytes));
    return buf->len;
}

/// Peek at data in a ring buffer without removing it.
///
/// Reads data from the ring buffer without consuming it. The data remains in the buffer
/// and the read position is not advanced. This is useful for protocol parsing where you
/// need to examine data before deciding whether to consume it.
///
/// @param ring Pointer to the ring buffer to peek into
/// @param buf Pointer to the user buffer to copy data into
/// @param off Offset within the ring buffer to start peeking from
/// @param bytes Number of bytes to peek at
/// @return Number of bytes actually copied (may be less if buffer has insufficient data)
size_t bufringPeek(_Inout_ BufRing* ring, _Out_writes_bytes_(bytes) uint8* buf, size_t off,
                   size_t bytes);

/// Peek at data in a ring buffer into a buffer object.
///
/// Convenience function that peeks data from the ring buffer into an existing buffer object
/// without consuming it. Updates the buffer's length field to reflect the number of bytes
/// actually copied. The maximum bytes copied is limited by both maxbytes and the buffer's
/// allocated size.
///
/// @param ring Pointer to the ring buffer to peek into
/// @param buf Buffer object to copy data into
/// @param off Offset within the ring buffer to start peeking from
/// @param maxbytes Maximum number of bytes to peek at
/// @return Number of bytes actually copied (stored in buf->len)
_meta_inline size_t bufringPeekBuf(_Inout_ BufRing* ring, _Inout_ buffer buf, size_t off,
                                   size_t maxbytes)
{
    buf->len = bufringPeek(ring, buf->data, off, min(buf->sz, maxbytes));
    return buf->len;
}

/// Skip data in a ring buffer.
///
/// Discards data from the head of the ring buffer without copying it. This is more efficient
/// than reading into a dummy buffer when you need to discard data. Exhausted capacity is
/// automatically freed, except the base segment which is retained for reuse.
///
/// @param ring Pointer to the ring buffer to skip data in
/// @param bytes Number of bytes to skip
/// @return Number of bytes actually skipped (may be less if buffer has insufficient data)
size_t bufringSkip(_Inout_ BufRing* ring, size_t bytes);

/// Read data from a ring buffer in zero-copy mode.
///
/// Reads data from the ring buffer by invoking a callback for each contiguous segment,
/// passing pointers directly to the internal buffer data without copying. This is the most
/// efficient way to process data in the buffer.
///
/// The callback must consistently return the same value (true or false) for all segments
/// in a single read operation. If the callback returns true for all segments, the data is
/// consumed and removed from the buffer. If it returns false for any segment, all data is
/// retained (all-or-nothing semantics).
///
/// @param cring Pointer to the ring buffer to read from
/// @param bytes Maximum number of bytes to read
/// @param cb Callback function to process each buffer segment
/// @param ctx User-defined context pointer passed to the callback
/// @return Number of bytes actually sent to the callback
/// @note The callback must return the same value consistently across all invocations during
/// a single read operation.
size_t bufringReadZC(_Inout_ BufRing* ring, size_t bytes, bufringZCCB cb, _Inout_opt_ void* ctx);

/// Write data from a user buffer into a ring buffer.
///
/// Appends data to the tail of the ring buffer. If the current capacity is insufficient,
/// additional capacity is automatically allocated as needed. Data is copied into the
/// ring buffer.
///
/// @param ring Pointer to the ring buffer to write to
/// @param buf Pointer to the user buffer containing data to write
/// @param bytes Number of bytes to write
void bufringWrite(_Inout_ BufRing* ring, _In_reads_bytes_(bytes) const uint8* buf, size_t bytes);

/// Write data from a buffer object into a ring buffer.
///
/// Convenience function that writes the contents of a buffer object to the ring buffer.
/// The data is copied and the buffer object remains owned by the caller. Uses the buffer's
/// length field to determine how many bytes to write.
///
/// @param ring Pointer to the ring buffer to write to
/// @param buf Buffer object containing data to write
_meta_inline void bufringWriteBuf(_Inout_ BufRing* ring, _In_ buffer buf)
{
    bufringWrite(ring, buf->data, buf->len);
}

/// Append a buffer to a ring buffer for zero-copy mode.
///
/// Appends a pre-allocated buffer object to the tail of the ring buffer without copying. This is
/// the most efficient way to add data to the buffer when you already have it in a buffer object.
/// The ring buffer takes ownership of the buffer object and will destroy it when the data is
/// consumed or when the ring buffer is destroyed. The buffer pointer is set to NULL after
/// transfer.
///
/// The buffer's length field (buf->len) is used to determine how much valid data is in the
/// buffer.
///
/// @param ring Pointer to the existing ring buffer
/// @param buf Pointer to buffer object to append (ownership transferred, set to NULL on return)
/// @note The ring buffer takes ownership of the buffer object and will destroy it. After this
/// call, *buf will be NULL.
_At_(*buf,
     _Pre_notnull_ _Post_null_) void bufringWriteZC(_Inout_ BufRing* ring, _Inout_ buffer* buf);

/// Get available write space in the ring buffer without expansion.
///
/// Returns the number of bytes that can be written to the current tail segment without
/// allocating a new segment. This is useful for determining if a write operation can
/// complete without triggering expansion, or for optimizing feed operations.
///
/// @param ring Pointer to the ring buffer to query
/// @return Number of bytes available for writing in the current tail segment (0 if empty)
size_t bufringWriteSpace(_In_ BufRing* ring);

/// Feed data into a ring buffer using a callback.
///
/// Allows a callback to write data directly into the ring buffer's internal storage, avoiding
/// intermediate copying. The callback is invoked one or more times with pointers to available
/// buffer space. This is particularly useful for reading from files, network sockets, or other
/// data sources directly into the buffer.
///
/// The function attempts to feed up to 'bytes' amount of data. The callback may be invoked
/// multiple times to reach this goal, and can return 0 to stop the operation early. New segments
/// are automatically allocated as needed.
///
/// @param ring Pointer to the ring buffer to feed data into
/// @param feed Callback function that writes data to the buffer
/// @param bytes Maximum number of bytes to feed
/// @param ctx User-defined context pointer passed to the callback
/// @return Total number of bytes actually fed into the buffer
/// @note The callback can return 0 to stop feeding. Writes are automatically split at segment
/// boundaries to maintain ring buffer integrity.
size_t bufringFeed(_Inout_ BufRing* ring, bufringFeedCB feed, size_t bytes, _Inout_opt_ void* ctx);

/// Destroy a ring buffer and free all associated resources.
///
/// Frees all segments and their associated data buffers, then resets the buffer structure
/// to an empty state. After calling this function, the buffer must be reinitialized with
/// bufringInit() before it can be used again.
///
/// @param ring Pointer to the ring buffer to destroy
void bufringDestroy(_Pre_notnull_ _Post_invalid_ BufRing* ring);

/// @}  // end of bufring group
