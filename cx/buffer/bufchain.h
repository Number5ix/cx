#pragma once

#include <cx/cx.h>

/// @file bufchain.h
/// @brief Chained ring buffer implementation for efficient streaming I/O

/// @defgroup bufchain Buffer Chain
/// @ingroup buffer
/// @{
///
/// A buffer chain is a linked list of ring buffers that provides efficient streaming I/O
/// with minimal memory allocation overhead. Data is read from the head and written to the
/// tail, with automatic node allocation and cleanup as the chain grows and shrinks.
///
/// Key features:
/// - Ring buffer nodes for efficient wraparound without data movement
/// - Automatic node allocation when existing nodes fill up
/// - Last node is retained and reused when emptied to reduce allocation thrashing
/// - Zero-copy operations for high-performance I/O
/// - Peek and skip operations for protocol parsing
///
/// The minimum segment size is 64 bytes. Smaller values will be clamped to this minimum.
///
/// @note For optimal performance, this implementation is not thread-safe. If access from
/// multiple threads is required, wrap operations with an appropriate lock.
///
/// @todo This is a generic and expanded version of the streambuf ring buffer implementation
/// that supports indefinite chaining instead of only 2 buffers. The streambuf module should
/// be refactored at some point to use this new implementation.
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
    BufChainNode* next;   ///< Next buffer in chain
    uint8* data;          ///< Pointer to data
    size_t sz;            ///< Size of buffer in bytes
    size_t head;          ///< Read position in data
    size_t tail;          ///< Write position in data
    bool full;            ///< Distinguish full vs empty when head == tail
} BufChainNode;

typedef struct BufChain {
    BufChainNode* head;   ///< First buffer in chain
    BufChainNode* tail;   ///< Last buffer in chain
    size_t total;         ///< Total size of data in chain
    size_t segsz;         ///< Size of each buffer segment
} BufChain;

/// Callback for zero-copy buffer reads.
///
/// The callback is invoked for each contiguous segment of data in the buffer chain.
/// It must consistently return the same value (true or false) for all invocations during
/// a single read operation, as the buffer chain only supports all-or-nothing consumption.
///
/// @param buf Pointer to the contiguous data buffer segment
/// @param bytes Number of valid bytes in this buffer segment
/// @param ctx User-defined context pointer
/// @return true to consume all data from the read operation, false to retain all data
typedef bool (*bufchainZCCB)(_In_reads_bytes_(bytes) const uint8* buf, size_t bytes,
                             _Pre_opt_valid_ void* ctx);

/// Initialize an empty buffer chain.
///
/// Creates an empty buffer chain with the specified segment size. Buffer nodes will be
/// allocated as needed when data is written. The segment size will be clamped to a minimum
/// of 64 bytes.
///
/// @param chain Pointer to the buffer chain to initialize
/// @param segsz Size of each buffer segment in bytes (minimum 64)
void bufchainInit(_Out_ BufChain* chain, size_t segsz);

/// Read data from a buffer chain into a user buffer.
///
/// Reads up to the specified number of bytes from the buffer chain and removes them from
/// the chain. Data is read sequentially from the head of the chain. Empty buffer nodes are
/// freed, except the last node which is retained for reuse.
///
/// @param chain Pointer to the buffer chain to read from
/// @param buf Pointer to the user buffer to read data into
/// @param bytes Maximum number of bytes to read
/// @return Number of bytes actually read (may be less if chain has insufficient data)
size_t bufchainRead(_Inout_ BufChain* chain, _Out_writes_bytes_(bytes) uint8* buf, size_t bytes);

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

/// Skip data in a buffer chain.
///
/// Discards data from the head of the buffer chain without copying it. This is more efficient
/// than reading into a dummy buffer when you need to discard data. Empty buffer nodes are
/// freed, except the last node which is retained for reuse.
///
/// @param chain Pointer to the buffer chain to skip data in
/// @param bytes Number of bytes to skip
/// @return Number of bytes actually skipped (may be less if chain has insufficient data)
size_t bufchainSkip(_Inout_ BufChain* chain, size_t bytes);

/// Read data from a buffer chain in zero-copy mode.
///
/// Reads data from the buffer chain by invoking a callback for each contiguous segment,
/// passing pointers directly to the internal buffer data without copying. This is the most
/// efficient way to process data in the chain.
///
/// The callback must consistently return the same value (true or false) for all segments
/// in a single read operation. If the callback returns true for all segments, the data is
/// consumed and removed from the chain. If it returns false for any segment, all data is
/// retained (all-or-nothing semantics).
///
/// @param chain Pointer to the buffer chain to read from
/// @param bytes Maximum number of bytes to read
/// @param cb Callback function to process each buffer segment
/// @param ctx User-defined context pointer passed to the callback
/// @return Number of bytes actually sent to the callback
/// @note The callback must return the same value consistently across all invocations during
/// a single read operation.
size_t bufchainReadZC(_Inout_ BufChain* chain, size_t bytes, bufchainZCCB cb,
                      _Inout_opt_ void* ctx);

/// Write data from a user buffer into a buffer chain.
///
/// Appends data to the tail of the buffer chain. If the current tail node has insufficient
/// space, additional nodes are automatically allocated as needed. Data is copied into the
/// buffer chain.
///
/// @param chain Pointer to the buffer chain to write to
/// @param buf Pointer to the user buffer containing data to write
/// @param bytes Number of bytes to write
void bufchainWrite(_Inout_ BufChain* chain, _In_reads_bytes_(bytes) const uint8* buf, size_t bytes);

/// Append a buffer to a chain for zero-copy mode.
///
/// Appends a pre-allocated buffer to the tail of the buffer chain without copying. This is
/// the most efficient way to add data to the chain when you already have it in a buffer.
/// The buffer chain takes ownership of the data pointer and will free it when the node is
/// consumed or when the chain is destroyed.
///
/// @param chain Pointer to the existing buffer chain
/// @param data Pointer to the data buffer to append (ownership transferred to chain)
/// @param size Total size of the data buffer in bytes
/// @param bytes Number of valid bytes in the buffer (may be less than size)
/// @note The buffer chain takes ownership of the data pointer and will free it when destroyed.
/// It must be a valid pointer that was allocated with xaAlloc!
void bufchainWriteZC(BufChain* chain, _Pre_notnull_ _Post_invalid_ uint8* data, size_t size,
                     size_t bytes);

/// Destroy a buffer chain and free all associated resources.
///
/// Frees all buffer nodes and their associated data buffers, then resets the chain structure
/// to an empty state. After calling this function, the chain must be reinitialized with
/// bufchainInit() before it can be used again.
///
/// @param chain Pointer to the buffer chain to destroy
void bufchainDestroy(_Pre_notnull_ _Post_invalid_ BufChain* chain);

/// @}  // end of bufchain group
