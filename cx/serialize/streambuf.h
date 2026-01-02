/// @file streambuf.h
/// @brief Core stream buffer implementation for efficient data streaming
///
/// @defgroup serialize_streambuf Stream Buffers
/// @ingroup serialize
/// @{
///
/// Stream buffers provide an efficient producer-consumer model for streaming data with
/// automatic buffering and flow control. They support two primary modes:
///
/// **Push Mode:** Producer writes data via sbufPWrite(), consumer is notified or receives
/// data directly via callbacks.
///
/// **Pull Mode:** Consumer reads data via sbufCRead(), producer's callback is invoked to
/// fill the buffer as needed.
///
/// **Basic Usage (Push Mode):**
/// @code
///   StreamBuffer *sb = sbufCreate(4096);
///
///   // Producer side
///   sbufPRegisterPush(sb, NULL, NULL);
///   sbufPWrite(sb, data, size);
///   sbufPFinish(sb);
///
///   // Consumer side (with notification callback)
///   sbufCRegisterPush(sb, myNotifyCallback, NULL, ctx);
///   // In callback: use sbufCRead() to retrieve data
/// @endcode
///
/// **Basic Usage (Pull Mode):**
/// @code
///   StreamBuffer *sb = sbufCreate(4096);
///
///   // Producer side (with pull callback)
///   sbufPRegisterPull(sb, myPullCallback, NULL, ctx);
///
///   // Consumer side
///   sbufCRegisterPull(sb, NULL, NULL);
///   size_t bytesread;
///   while (sbufCRead(sb, buffer, sizeof(buffer), &bytesread)) {
///       // process buffer
///   }
///   sbufCFinish(sb);
/// @endcode

#pragma once

#include <cx/stype/stype.h>

typedef struct StreamBuffer StreamBuffer;

// Pull callback
// sz is set to the maximum amount of data the may be written to buf.
// The callback should fill buf with up to that amount and return the number
// of bytes that were written.
// The callback may return 0 if no data is currently available but will likely
// be immediately called again, so a performing a blocking wait is advisable.
//
// If the callback needs to write an amount of data that is larger than the
// space available as indicated by sz, it may instead call sbufPWrite with the
// full amount (which will expand the buffer in the process) and return 0.
//
// If sz is 0, check if the consumer is finished and/or for error state.
typedef size_t (*sbufPullCB)(_Pre_valid_ StreamBuffer* sb, _Out_writes_bytes_(sz) uint8* buf,
                             size_t sz, _Pre_opt_valid_ void* ctx);

// Push callback
// When this callback is used (in direct mode), the data is pushed once to the
// callback and MUST all be written in one go or it will be lost.
// If sz is 0, check if the producer is finished and/or for error state.
typedef void (*sbufPushCB)(_Pre_valid_ StreamBuffer* sb, _In_reads_bytes_(sz) const uint8* buf,
                           size_t sz, _Pre_opt_valid_ void* ctx);

// Send callback
// This callback is used with sbufCSend. It may be called multiple times with varying
// offsets. The offset passed is always from the start of the available bytes in the
// buffer.
// If this function returns true, the sent bytes will be consumed and removed from the
// buffer. If it returns false, it behaves like the peek functions and does not remove
// the bytes from the buffer.
// If sz is 0, check if the producer is finished and/or for error state.
typedef bool (*sbufSendCB)(_Pre_valid_ StreamBuffer* sb, _In_reads_bytes_(sz) const uint8* buf,
                           size_t off, size_t sz, _Pre_opt_valid_ void* ctx);

// Notify callback
// Notification to a consumer that data is available. The sbufC* functions may be
// used to read all or part of the available data.
// If sz is 0, check if the producer is finished and/or for error state.
typedef void (*sbufNotifyCB)(_Pre_valid_ StreamBuffer* sb, size_t sz, _Pre_opt_valid_ void* ctx);

// Cleanup callback
// Called just before the structure is deallocated, should perform any needed
// cleanup of the user-supplied ctx.
typedef void (*sbufCleanupCB)(_Pre_opt_valid_ void* ctx);

/// @defgroup serialize_streambuf_core Core Functions
/// @ingroup serialize_streambuf
/// @{
///
/// Core stream buffer lifecycle and state management.
///
/// **Operating Modes:**
///
/// **Push Mode:** Producer repeatedly calls sbufPWrite(). Consumer is either notified
/// that data is available via callback, or in direct mode, data is pushed immediately
/// to the consumer's callback.
///
/// **Pull Mode:** Consumer repeatedly calls sbufCRead() or sbufCSend(). Producer's
/// pull callback is invoked to fill the buffer as needed.

/// @cond IGNORE
enum STREAM_BUFFER_FLAGS_ENUM {
    SBUF_Push                = 0x0001,
    SBUF_Pull                = 0x0002,
    SBUF_Direct              = 0x0010,
    SBUF_Error               = 0x0800,
    SBUF_Producer_Registered = 0x1000,
    SBUF_Consumer_Registered = 0x2000,
    SBUF_Producer_Done       = 0x4000,
    SBUF_Consumer_Done       = 0x8000,
};

/// Stream buffer structure for managing producer-consumer data flow
typedef struct StreamBuffer {
    uint8* buf;                      ///< Main buffer
    size_t bufsz;                    ///< Current buffer size
    size_t targetsz;                 ///< Desired max size (can be exceeded in push mode)

    uint8* overflow;                 ///< Overflow buffer for expanding main buffer
    size_t overflowtail;             ///< Write position in overflow buffer
    size_t overflowsz;               ///< Size of overflow buffer

    size_t head;                     ///< Start of valid data in buffer
    size_t tail;                     ///< End of valid data in buffer

    sbufPullCB producerPull;         ///< Producer pull callback
    sbufCleanupCB producerCleanup;   ///< Producer cleanup callback
    void* producerCtx;               ///< Producer context

    sbufNotifyCB consumerNotify;     ///< Consumer notify callback
    sbufPushCB consumerPush;         ///< Consumer push callback
    sbufCleanupCB consumerCleanup;   ///< Consumer cleanup callback
    void* consumerCtx;               ///< Consumer context

    int refcount;                    ///< Reference count for lifecycle management
    uint32 flags;                    ///< Operating mode and state flags
} StreamBuffer;
/// @endcond

/// StreamBuffer *sbufCreate(size_t targetsz)
///
/// Creates a new stream buffer with the specified target size.
///
/// The buffer will automatically grow as needed but tries to stay near targetsz.
/// Set targetsz to 0 only when using direct push mode (no buffering needed).
///
/// @param targetsz Target buffer size in bytes (0 for direct mode)
/// @return New stream buffer (must be released with sbufRelease)
///
/// Example:
/// @code
///   StreamBuffer *sb = sbufCreate(4096);
///   // ... register producer and consumer ...
///   sbufRelease(&sb);
/// @endcode
_Ret_valid_ StreamBuffer* sbufCreate(size_t targetsz);

/// void sbufRelease(StreamBuffer **sb)
///
/// Releases a reference to a stream buffer.
///
/// Decrements the reference count and destroys the buffer when it reaches zero.
/// Sets the pointer to NULL after release.
///
/// @param sb Pointer to stream buffer pointer
_At_(*sb, _Pre_maybenull_ _Post_null_) void sbufRelease(_Inout_ StreamBuffer** sb);

/// void sbufError(StreamBuffer *sb)
///
/// Puts the stream buffer into error state.
///
/// Processing will be aborted and both producer and consumer should call their
/// respective Finish functions as soon as practical.
///
/// @param sb The stream buffer
void sbufError(_Inout_ StreamBuffer* sb);

/// bool sbufIsPull(StreamBuffer *sb)
///
/// Checks if the stream buffer is in pull mode.
///
/// @param sb The stream buffer
/// @return true if in pull mode
_meta_inline bool sbufIsPull(_In_ StreamBuffer* sb)
{
    return sb->flags & SBUF_Pull;
}

/// bool sbufIsPush(StreamBuffer *sb)
///
/// Checks if the stream buffer is in push mode.
///
/// @param sb The stream buffer
/// @return true if in push mode
_meta_inline bool sbufIsPush(_In_ StreamBuffer* sb)
{
    return sb->flags & SBUF_Push;
}

/// bool sbufIsError(StreamBuffer *sb)
///
/// Checks if the stream buffer is in an error state.
///
/// @param sb The stream buffer
/// @return true if in error state
_meta_inline bool sbufIsError(_In_ StreamBuffer* sb)
{
    return sb->flags & SBUF_Error;
}

/// bool sbufIsPFinished(StreamBuffer *sb)
///
/// Checks if the producer is finished (EOF).
///
/// @param sb The stream buffer
/// @return true if producer has finished or error occurred
_meta_inline bool sbufIsPFinished(_In_ StreamBuffer* sb)
{
    return (sb->flags & SBUF_Producer_Done) || sbufIsError(sb);
}

/// bool sbufIsCFinished(StreamBuffer *sb)
///
/// Checks if the consumer is finished.
///
/// Returns true if consumer has exited early (no longer needs data to be produced).
///
/// @param sb The stream buffer
/// @return true if consumer has finished or error occurred
_meta_inline bool sbufIsCFinished(_In_ StreamBuffer* sb)
{
    return (sb->flags & SBUF_Consumer_Done) || sbufIsError(sb);
}

/// @}  // end of serialize_streambuf_core

/// @defgroup serialize_streambuf_producer Producer Functions
/// @ingroup serialize_streambuf
/// @{
///
/// Functions for the producer side of stream buffer operations.

/// bool sbufPRegisterPull(StreamBuffer *sb, sbufPullCB ppull, sbufCleanupCB pcleanup, void *ctx)
///
/// Registers a producer with the stream buffer in pull mode.
///
/// In pull mode, the consumer pulls data by calling sbufCRead(), which triggers
/// the producer's callback to fill the buffer.
///
/// @param sb The stream buffer
/// @param ppull Pull callback to provide data
/// @param pcleanup Optional cleanup callback for ctx
/// @param ctx Optional user context passed to callbacks
/// @return true on success, false if already registered or invalid mode
_Check_return_ bool sbufPRegisterPull(_Inout_ StreamBuffer* sb, _In_ sbufPullCB ppull,
                                      _In_opt_ sbufCleanupCB pcleanup, _Inout_opt_ void* ctx);

/// bool sbufPRegisterPush(StreamBuffer *sb, sbufCleanupCB pcleanup, void *ctx)
///
/// Registers a producer with the stream buffer in push mode.
///
/// In push mode, the producer pushes data by calling sbufPWrite(), which notifies
/// or directly calls the consumer.
///
/// @param sb The stream buffer
/// @param pcleanup Optional cleanup callback for ctx
/// @param ctx Optional user context passed to cleanup
/// @return true on success, false if already registered or invalid mode
_Check_return_ bool sbufPRegisterPush(_Inout_ StreamBuffer* sb, _In_opt_ sbufCleanupCB pcleanup,
                                      _Inout_opt_ void* ctx);

/// size_t sbufPAvail(StreamBuffer *sb)
///
/// Returns the available space for writing to the buffer.
///
/// @param sb The stream buffer
/// @return Number of bytes available for writing
size_t sbufPAvail(_In_ StreamBuffer* sb);

/// bool sbufPWrite(StreamBuffer *sb, const uint8 *buf, size_t sz)
///
/// Writes data to the buffer.
///
/// This will always succeed unless the system is out of memory. An overflow buffer
/// is used if written data exceeds current buffer size.
///
/// @param sb The stream buffer
/// @param buf Data to write
/// @param sz Number of bytes to write
/// @return true on success, false if consumer finished or error
bool sbufPWrite(_Inout_ StreamBuffer* sb, _In_reads_bytes_(sz) const uint8* buf, size_t sz);

/// bool sbufPWriteStr(StreamBuffer *sb, strref str)
///
/// Writes a string to the buffer.
///
/// @param sb The stream buffer
/// @param str String to write
/// @return true on success, false on error
bool sbufPWriteStr(_Inout_ StreamBuffer* sb, _In_opt_ strref str);

/// bool sbufPWriteLine(StreamBuffer *sb, strref str)
///
/// Writes a string followed by a system-dependent line ending to the buffer.
///
/// Uses `\r\n` on Windows, `\n` on Unix systems.
///
/// @param sb The stream buffer
/// @param str String to write
/// @return true on success, false on error
bool sbufPWriteLine(_Inout_ StreamBuffer* sb, _In_opt_ strref str);

/// bool sbufPWriteEOL(StreamBuffer *sb)
///
/// Writes a system-dependent line ending to the buffer.
///
/// Uses `\r\n` on Windows, `\n` on Unix systems.
///
/// @param sb The stream buffer
/// @return true on success, false on error
bool sbufPWriteEOL(_Inout_ StreamBuffer* sb);

/// void sbufPFinish(StreamBuffer *sb)
///
/// Marks the producer as finished (EOF).
///
/// **CRITICAL:** The producer MUST NOT access the stream buffer after calling this
/// function as it may be immediately deallocated.
///
/// @param sb The stream buffer (invalidated after call)
void sbufPFinish(_Pre_valid_ _Post_invalid_ StreamBuffer* sb);

/// @}  // end of serialize_streambuf_producer

/// @defgroup serialize_streambuf_consumer Consumer Functions
/// @ingroup serialize_streambuf
/// @{
///
/// Functions for the consumer side of stream buffer operations.

/// bool sbufCRegisterPull(StreamBuffer *sb, sbufCleanupCB ccleanup, void *ctx)
///
/// Registers a consumer with the stream buffer in pull mode.
///
/// In pull mode, the consumer explicitly reads data using sbufCRead() or sbufCSend().
///
/// @param sb The stream buffer
/// @param ccleanup Optional cleanup callback for ctx
/// @param ctx Optional user context passed to cleanup
/// @return true on success, false if already registered or invalid mode
_Check_return_ bool sbufCRegisterPull(_Inout_ StreamBuffer* sb, _In_opt_ sbufCleanupCB ccleanup,
                                      _Inout_opt_ void* ctx);

/// bool sbufCRegisterPush(StreamBuffer *sb, sbufNotifyCB cnotify, sbufCleanupCB ccleanup, void
/// *ctx)
///
/// Registers a consumer with the stream buffer in push mode with notifications.
///
/// The notify callback is called when data becomes available. The consumer then uses
/// sbufCRead() or sbufCSend() to retrieve the data.
///
/// @param sb The stream buffer
/// @param cnotify Notification callback
/// @param ccleanup Optional cleanup callback for ctx
/// @param ctx Optional user context passed to callbacks
/// @return true on success, false if already registered or invalid mode
_Check_return_ bool sbufCRegisterPush(_Inout_ StreamBuffer* sb, _In_ sbufNotifyCB cnotify,
                                      _In_opt_ sbufCleanupCB ccleanup, _Inout_opt_ void* ctx);

/// bool sbufCRegisterPushDirect(StreamBuffer *sb, sbufPushCB cpush, sbufCleanupCB ccleanup, void
/// *ctx)
///
/// Registers a consumer with the stream buffer in direct push mode.
///
/// In direct mode, data is pushed immediately to the consumer's callback without buffering.
/// The consumer must process data immediately as it will not be retained.
///
/// @param sb The stream buffer
/// @param cpush Push callback to receive data
/// @param ccleanup Optional cleanup callback for ctx
/// @param ctx Optional user context passed to callbacks
/// @return true on success, false if already registered or invalid mode
_Check_return_ bool sbufCRegisterPushDirect(_Inout_ StreamBuffer* sb, _In_ sbufPushCB cpush,
                                            _In_opt_ sbufCleanupCB ccleanup, _Inout_opt_ void* ctx);

/// size_t sbufCAvail(StreamBuffer *sb)
///
/// Returns how much data is currently buffered and ready to consume.
///
/// @param sb The stream buffer
/// @return Number of bytes available
size_t sbufCAvail(_Inout_ StreamBuffer* sb);

/// bool sbufCRead(StreamBuffer *sb, uint8 *buf, size_t sz, size_t *bytesread)
///
/// Reads data from the stream buffer.
///
/// **Pull mode:** Repeatedly calls the producer's callback to satisfy the request.
/// Only short-reads when producer finishes (EOF).
///
/// **Push mode:** Returns only buffered data. Fails if requesting more than available.
///
/// @param sb The stream buffer
/// @param buf Buffer to read into
/// @param sz Number of bytes to read
/// @param bytesread Output: actual number of bytes read
/// @return true if any data was read, false on error or no data available
_Success_(return) bool
sbufCRead(_Inout_ StreamBuffer* sb, _Out_writes_bytes_to_(sz, *bytesread) uint8* buf, size_t sz,
          _Out_ _Deref_out_range_(0, sz) size_t* bytesread);

/// bool sbufCPeek(StreamBuffer *sb, uint8 *buf, size_t off, size_t sz)
///
/// Peeks at data in the buffer without consuming it.
///
/// **Pull mode:** Only reads unconsumed data already in buffer, does NOT call producer.
///
/// **Push mode:** Reads from buffered data.
///
/// Never short-reads; fails if insufficient data is available (check sbufCAvail first).
///
/// @param sb The stream buffer
/// @param buf Buffer to read into
/// @param off Offset from start of available data
/// @param sz Number of bytes to peek
/// @return true on success, false if not enough data available
_Success_(return > 0) bool
sbufCPeek(_Inout_ StreamBuffer* sb, _Out_writes_bytes_(sz) uint8* buf, size_t off, size_t sz);

/// bool sbufCFeed(StreamBuffer *sb, size_t minsz)
///
/// For pull mode only - feeds the buffer until it has at least the requested bytes.
///
/// Similar to sbufCRead() but doesn't consume the data. Useful for peek-ahead operations.
/// Keeps calling producer until enough data is available or producer finishes.
///
/// @param sb The stream buffer
/// @param minsz Minimum bytes to ensure are buffered
/// @return true if request satisfied, false if producer finished before providing enough
bool sbufCFeed(_Inout_ StreamBuffer* sb, size_t minsz);

/// bool sbufCSend(StreamBuffer *sb, sbufSendCB func, size_t sz)
///
/// Sends data from buffer to callback with zero-copy optimization.
///
/// The callback may be invoked multiple times with different chunks.
///
/// **Push mode:** More efficient than sbufCRead() as it avoids an extra copy by passing
/// pointers to internal buffer directly to the callback.
///
/// **Pull mode:** Functions like sbufCRead(), calling producer to fill buffer before
/// invoking the callback.
///
/// @param sb The stream buffer
/// @param func Send callback to receive data
/// @param sz Maximum bytes to send
/// @return true on success
bool sbufCSend(_Inout_ StreamBuffer* sb, _In_ sbufSendCB func, size_t sz);

/// bool sbufCSkip(StreamBuffer *sb, size_t bytes)
///
/// Skips over bytes in the buffer without reading them.
///
/// Can be used in conjunction with sbufCPeek() to peek ahead and then skip.
///
/// @param sb The stream buffer
/// @param bytes Number of bytes to skip
/// @return true on success, false if not enough data available
bool sbufCSkip(_Inout_ StreamBuffer* sb, size_t bytes);

/// void sbufCFinish(StreamBuffer *sb)
///
/// Marks the consumer as finished.
///
/// **CRITICAL:** The consumer MUST NOT access the stream buffer after calling this
/// function as it may be immediately deallocated.
///
/// @param sb The stream buffer (invalidated after call)
void sbufCFinish(_Pre_valid_ _Post_invalid_ StreamBuffer* sb);

/// @}  // end of serialize_streambuf_consumer
/// @}  // end of serialize_streambuf
