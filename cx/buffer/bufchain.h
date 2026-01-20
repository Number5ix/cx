#pragma once

#include <cx/cx.h>
#include "cx/buffer/buffer.h"
#include "cx/utils/compare.h"

/// @file bufchain.h
/// @brief Buffer chain implementation for efficient streaming I/O

/// @defgroup buffer_bufchain Buffer Chain
/// @ingroup buffer
/// @{
///
/// A buffer chain that provides efficient streaming I/O with true zero-copy semantics.
/// Data is read from the head and written to the tail as complete buffer segments, with
/// automatic capacity expansion and cleanup as needed.
///
/// Unlike BufRing, BufChain uses simple sequential buffers rather than ring buffers, making
/// it less efficient for repeated small read/write operations on raw bytes, but more efficient
/// for zero-copy operations since it can transfer buffer ownership directly to consumers.
///
/// As a tradeoff, when performing zero-copy reads, the callback only receives complete buffers
/// rather than a byte range, and cannot operate in peek mode. If a partial read has been
/// performed on the head segment (using bufchainRead), the zero-copy callback will receive
/// an offset indicating where the valid data begins in that buffer.
///
/// Key features:
/// - Chain of simple buffer segments (not ring buffers)
/// - True zero-copy reads by transferring buffer ownership to callbacks
/// - Automatic capacity expansion when space is exhausted
/// - Automatic cleanup when data is consumed to reduce memory usage
/// - Peek and skip operations for protocol parsing
///
/// The minimum segment size is 64 bytes. Smaller values will be clamped to this minimum.
///
/// @note For optimal performance, this implementation is not thread-safe. If access from
/// multiple threads is required, wrap operations with an appropriate lock.
///
/// Example:
/// @code
///   BufChain chain;
///   bufchainInit(&chain, 4096);
///
///   // Write data
///   bufchainWrite(&chain, data, dataLen);
///
///   // Read data
///   uint8 buffer[1024];
///   size_t nread = bufchainRead(&chain, buffer, sizeof(buffer));
///
///   bufchainDestroy(&chain);
/// @endcode

typedef struct BufChainNode BufChainNode;
typedef struct BufChainNode {
    BufChainNode* next;   ///< Next segment in chain
    buffer buf;           ///< Buffer containing data
} BufChainNode;

typedef struct BufChain {
    BufChainNode* head;   ///< First segment
    BufChainNode* tail;   ///< Last segment
    uintptr cursor;       ///< Current read position within head segment
    size_t total;         ///< Total bytes of data available
    size_t segsz;         ///< Size of each buffer segment
} BufChain;

/// Callback for zero-copy buffer reads.
///
/// The callback is invoked for each complete buffer segment in the chain. Unlike BufRing's
/// zero-copy callback, this callback receives ownership of the buffer object and MUST
/// destroy it or otherwise dispose of it. This enables true zero-copy operation.
///
/// The callback is only invoked for complete segments; partial segments are not sent to
/// preserve the ability to transfer ownership. If data has been previously read from the
/// buffer using bufchainRead(), the offset parameter indicates where the valid data starts.
///
/// @param buf Buffer object to process (ownership transferred to callback, must destroy)
/// @param off Offset within buffer where valid data starts (may be nonzero)
/// @param ctx User-defined context pointer
/// @return true to continue reading more segments, false to stop
typedef bool (*bufchainZCCB)(_Inout_ buffer buf, size_t off, _Pre_opt_valid_ void* ctx);

/// Initialize an empty buffer chain.
///
/// Creates an empty buffer chain with the specified segment size. Additional capacity will be
/// allocated as needed when data is written. The segment size will be clamped to a minimum
/// of 64 bytes.
///
/// @param chain Pointer to the buffer chain to initialize
/// @param segsz Size of each buffer segment in bytes (minimum 64)
void bufchainInit(_Out_ BufChain* chain, size_t segsz);

/// Read data from a buffer chain into a user buffer.
///
/// Reads up to the specified number of bytes from the buffer chain and removes them from
/// the chain. Data is read sequentially from the head. Exhausted segments are automatically
/// freed.
///
/// @param chain Pointer to the buffer chain to read from
/// @param buf Pointer to the user buffer to read data into
/// @param bytes Maximum number of bytes to read
/// @return Number of bytes actually read (may be less if chain has insufficient data)
size_t bufchainRead(_Inout_ BufChain* chain, _Out_writes_bytes_(bytes) uint8* buf, size_t bytes);

/// Read data from a buffer chain into a buffer object.
///
/// Convenience function that reads data from the buffer chain into an existing buffer object.
/// Updates the buffer's length field to reflect the number of bytes actually read. The maximum
/// bytes read is limited by both maxbytes and the buffer's allocated size.
///
/// @param chain Pointer to the buffer chain to read from
/// @param buf Buffer object to read data into
/// @param maxbytes Maximum number of bytes to read
/// @return Number of bytes actually read (stored in buf->len)
_meta_inline size_t bufchainReadBuf(_Inout_ BufChain* chain, _Inout_ buffer buf, size_t maxbytes)
{
    buf->len = bufchainRead(chain, buf->data, min(buf->sz, maxbytes));
    return buf->len;
}

/// Peek at data in a buffer chain without removing it.
///
/// Reads data from the buffer chain without consuming it. The data remains in the chain
/// and the read position is not advanced. This is useful for protocol parsing where you
/// need to examine data before deciding whether to consume it.
///
/// @param chain Pointer to the buffer chain to peek into
/// @param buf Pointer to the user buffer to copy data into
/// @param off Offset within the buffer chain to start peeking from
/// @param bytes Number of bytes to peek at
/// @return Number of bytes actually copied (may be less if chain has insufficient data)
size_t bufchainPeek(_Inout_ BufChain* chain, _Out_writes_bytes_(bytes) uint8* buf, size_t off,
                    size_t bytes);

/// Peek at data in a buffer chain into a buffer object.
///
/// Convenience function that peeks data from the buffer chain into an existing buffer object
/// without consuming it. Updates the buffer's length field to reflect the number of bytes
/// actually copied. The maximum bytes copied is limited by both maxbytes and the buffer's
/// allocated size.
///
/// @param chain Pointer to the buffer chain to peek into
/// @param buf Buffer object to copy data into
/// @param off Offset within the buffer chain to start peeking from
/// @param maxbytes Maximum number of bytes to peek at
/// @return Number of bytes actually copied (stored in buf->len)
_meta_inline size_t bufchainPeekBuf(_Inout_ BufChain* chain, _Inout_ buffer buf, size_t off,
                                    size_t maxbytes)
{
    buf->len = bufchainPeek(chain, buf->data, off, min(buf->sz, maxbytes));
    return buf->len;
}

/// Skip data in a buffer chain.
///
/// Discards data from the head of the buffer chain without copying it. This is more efficient
/// than reading into a dummy buffer when you need to discard data. Exhausted segments are
/// automatically freed.
///
/// @param chain Pointer to the buffer chain to skip data in
/// @param bytes Number of bytes to skip
/// @return Number of bytes actually skipped (may be less if chain has insufficient data)
size_t bufchainSkip(_Inout_ BufChain* chain, size_t bytes);

/// Read data from a buffer chain in zero-copy mode.
///
/// Reads data from the buffer chain by invoking a callback for each complete buffer segment,
/// transferring ownership of the buffer object to the callback. This is the most efficient
/// way to process data in the chain as it avoids copying entirely. The callback MUST destroy
/// or otherwise dispose of each buffer it receives.
///
/// Unlike BufRing's zero-copy function, this only sends complete segments to the callback
/// (not partial buffers), as partial reads cannot transfer ownership. The callback can stop
/// reading by returning false.
///
/// @param chain Pointer to the buffer chain to read from
/// @param maxbytes Maximum number of bytes to read (limit, may read less)
/// @param cb Callback function that receives ownership of each buffer segment
/// @param ctx User-defined context pointer passed to the callback
/// @return Number of bytes actually sent to the callback
/// @note The callback receives buffer ownership and MUST destroy each buffer. Only complete
/// segments are sent; partial buffers are not transferred.
size_t bufchainReadZC(_Inout_ BufChain* chain, size_t maxbytes, bufchainZCCB cb,
                      _Inout_opt_ void* ctx);

/// Write data from a user buffer into a buffer chain.
///
/// Appends data to the tail of the buffer chain. If the current capacity is insufficient,
/// additional capacity is automatically allocated as needed. Data is copied into the
/// buffer chain.
///
/// @param chain Pointer to the buffer chain to write to
/// @param buf Pointer to the user buffer containing data to write
/// @param bytes Number of bytes to write
void bufchainWrite(_Inout_ BufChain* chain, _In_reads_bytes_(bytes) const uint8* buf, size_t bytes);

/// Write data from a buffer object into a buffer chain.
///
/// Convenience function that writes the contents of a buffer object to the buffer chain.
/// The data is copied and the buffer object remains owned by the caller. Uses the buffer's
/// length field to determine how many bytes to write.
///
/// @param chain Pointer to the buffer chain to write to
/// @param buf Buffer object containing data to write
_meta_inline void bufchainWriteBuf(_Inout_ BufChain* chain, _In_ buffer buf)
{
    bufchainWrite(chain, buf->data, buf->len);
}

/// Append a buffer to a buffer chain for zero-copy mode.
///
/// Appends a pre-allocated buffer object to the tail of the buffer chain without copying. This is
/// the most efficient way to add data to the chain when you already have it in a buffer object.
/// The buffer chain takes ownership of the buffer object and will destroy it when the data is
/// consumed or when the chain is destroyed. The buffer pointer is set to NULL after transfer.
///
/// The buffer's length field (buf->len) is used to determine how much valid data is in the
/// buffer. The entire buffer is added as a segment.
///
/// @param chain Pointer to the existing buffer chain
/// @param buf Pointer to buffer object to append (ownership transferred, set to NULL on return)
/// @note The buffer chain takes ownership of the buffer object and will destroy it. After this
/// call, *buf will be NULL.
_At_(*buf, _Pre_notnull_ _Post_null_) void bufchainWriteZC(BufChain* chain, _Inout_ buffer* buf);

/// Destroy a buffer chain and free all associated resources.
///
/// Frees all segments and their associated data buffers, then resets the chain structure
/// to an empty state. After calling this function, the chain must be reinitialized with
/// bufchainInit() before it can be used again.
///
/// @param chain Pointer to the buffer chain to destroy
void bufchainDestroy(_Pre_notnull_ _Post_invalid_ BufChain* chain);

/// @}  // end of bufchain group
