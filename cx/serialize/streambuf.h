/// @file streambuf.h
/// @brief Core stream buffer implementation for efficient data streaming
///
/// @defgroup serialize_streambuf Stream Buffers
/// @ingroup serialize
/// @{
///
/// A stream buffer carries bytes from a producer to a consumer. One side drives and the other is
/// called back; which is which is the buffer's mode:
///
/// **Push mode:** the producer drives. It calls sbufPWrite() whenever it has data, and the
/// consumer's callback runs to take it.
///
/// **Pull mode:** the consumer drives. It calls sbufCRead() whenever it wants data, and the
/// producer's callback runs to supply it.
///
/// Only the side that gets called back registers, and that registration picks the mode: registering
/// a consumer callback makes the buffer push mode, registering a producer callback makes it pull
/// mode. The driving side has nothing to register -- it just calls the read or write functions.
///
/// Those functions are the same in both modes. sbufCRead() hands back buffered data in push mode
/// and calls the producer in pull mode, so code that only reads, or only writes, does not have to
/// know which mode it is in.
///
/// **Push mode:**
/// @code
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufCRegisterPush(sb, myNotifyCallback, NULL, ctx);   // consumer is called back
///
///   sbufPWrite(sb, data, size);                           // producer drives
///   sbufClose(sb);
///   sbufRelease(&sb);
/// @endcode
///
/// **Pull mode:**
/// @code
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufPRegisterPull(sb, myPullCallback, NULL, ctx);     // producer is called back
///
///   size_t bytesread;
///   while (sbufCRead(sb, buffer, sizeof(buffer), &bytesread)) {   // consumer drives
///       // process buffer
///   }
///   sbufClose(sb);
///   sbufRelease(&sb);
/// @endcode
///
/// **Lifetime:** stream buffers are reference counted. sbufCreate() returns one reference,
/// sbufAcquire() takes another, and sbufRelease() gives one back. Releasing the last reference
/// frees the buffer, and is the only thing that can. Registering takes a reference of its own, so
/// hold your own reference -- acquire one if you did not create the buffer -- for as long as you
/// keep the pointer.
///
/// **Closing a stream:** the driving side calls sbufClose() once the stream is over: the producer
/// in push mode, the consumer in pull mode. That says something about the stream rather than about
/// either party, so there is only one such call and only the driving side makes it. Writes stop
/// working, a consumer may still drain whatever is already buffered, and the registered side gets
/// one last callback with sz == 0 so it can unregister itself.
///
/// **Leaving a role:** a registered party that is simply done calls sbufPUnregister() or
/// sbufCUnregister() instead. That empties the slot without ending the stream, so a replacement can
/// register and carry on -- a pull producer that ran out of bytes, or a log file rotated out from
/// under a writer. Until someone new attaches, a reader gets short reads and a writer's bytes pile
/// up in the buffer. Unregistering runs that registration's cleanup callback and gives back the
/// reference the registration took.
///
/// Whoever holds a reference may unregister a slot, not only the party that filled it, which is
/// what lets an application swap a sink out from under a stream. Flush first with sbufPFlush() so
/// the outgoing sink gets the bytes that were meant for it.
///
/// **Errors:** sbufError() reports that something went wrong. Reads and writes fail while the error
/// stands, so a failure cannot be quietly written over, but the stream is not over. The driving
/// side finds out on its next call and decides: give up with sbufClose(), or unregister whoever
/// failed, call sbufClearError() and attach a replacement.
///
/// **Threads:** a stream buffer is single-threaded by default. The producer and the consumer are
/// expected to run on the same thread, taking turns through the callbacks. Pass SBUF_Locked to
/// sbufCreate() when they must live on different threads; that adds a lock around every operation
/// and is the only supported way to share a stream buffer.
///
/// **Flow control:** by default the buffer grows without limit, so a producer that outruns its
/// consumer uses as much memory as it writes. Call sbufSetWatermark() to cap it. Once the buffered
/// data reaches the high mark the producer is held until the consumer drains it back to the low
/// mark. A producer that passes SBUF_Wait waits inside sbufPWrite() until that happens; otherwise
/// sbufPWrite() returns false right away and the resume callback set with sbufPSetResume() says
/// when to try again.
///
/// @code
///   StreamBuffer *sb = sbufCreate(4096, SBUF_Locked);
///   sbufSetWatermark(sb, 65536, 16384);
///   sbufPWrite(sb, data, size, SBUF_Wait);   // waits at the mark rather than failing
/// @endcode

#pragma once

#include <cx/buffer/bufring.h>
#include <cx/stype/stype.h>
#include <cx/thread/condvar.h>
#include <cx/thread/mutex.h>

CX_C_BEGIN

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
// A producer that has run out of data calls sbufPUnregister(), which leaves the stream open for
// another producer. If sz is 0 this is a status check rather than a request for data: the stream
// has closed or failed, and a producer that sees sbufIsClosed() must unregister itself.
typedef size_t (*sbufPullCB)(_Pre_valid_ StreamBuffer* sb, _Out_writes_bytes_(sz) uint8* buf,
                             size_t sz, _Pre_opt_valid_ void* ctx);

// Push callback
// When this callback is used (in direct mode), the data is pushed once to the
// callback and MUST all be written in one go or it will be lost.
// If sz is 0, check whether the stream has closed or failed.
typedef void (*sbufPushCB)(_Pre_valid_ StreamBuffer* sb, _In_reads_bytes_(sz) const uint8* buf,
                           size_t sz, _Pre_opt_valid_ void* ctx);

// Send callback
// This callback is used with sbufCSend. It may be called multiple times with varying
// offsets. The offset passed is always from the start of the available bytes in the
// buffer.
// Consumption is all-or-nothing across the whole sbufCSend call: if every invocation
// returns true the bytes are consumed and removed from the buffer, and if any one of them
// returns false the buffer keeps all of them, like the peek functions. A callback that can
// accept some of the data but not the rest should therefore return false every time and
// have the caller sbufCSkip() exactly what it took.
// This callback MUST NOT call any sbuf function on the buffer it was passed.
typedef bool (*sbufSendCB)(_Pre_valid_ StreamBuffer* sb, _In_reads_bytes_(sz) const uint8* buf,
                           size_t off, size_t sz, _Pre_opt_valid_ void* ctx);

// Notify callback
// Notification to a consumer that data is available. The sbufC* functions may be
// used to read all or part of the available data.
// A consumer that no longer wants the stream calls sbufCUnregister(), which leaves the stream open
// for another consumer. If sz is 0 this is a status check rather than an offer of data: the stream
// has closed or failed, and a consumer that sees sbufIsClosed() must unregister itself.
typedef void (*sbufNotifyCB)(_Pre_valid_ StreamBuffer* sb, size_t sz, _Pre_opt_valid_ void* ctx);

// Resume callback
// Tells a producer that was refused at the high watermark that the buffer has drained back to
// the low mark and writing may continue. Called on whichever thread drained the buffer, with the
// buffer's lock held, so it may call sbufPWrite() but must not block.
typedef void (*sbufResumeCB)(_Pre_valid_ StreamBuffer* sb, _Pre_opt_valid_ void* ctx);

// Cleanup callback
// Called when the registration it belongs to goes away, either from an explicit unregister or
// because the buffer was destroyed with the registration still in place. Should perform any needed
// cleanup of the user-supplied ctx.
typedef void (*sbufCleanupCB)(_Pre_opt_valid_ void* ctx);

/// @defgroup serialize_streambuf_core Core Functions
/// @ingroup serialize_streambuf
/// @{
///
/// Stream buffer lifetime, mode and state.

/// @cond IGNORE
enum STREAM_BUFFER_FLAGS_ENUM {
    SBUF_Push   = 0x0001,
    SBUF_Pull   = 0x0002,
    SBUF_Direct = 0x0010,
    SBUF_Error  = 0x0800,
    SBUF_Closed = 0x1000,
};
/// @endcond

/// Optional flags for sbufCreate() and the sbufPWrite() family
enum STREAM_BUFFER_OPT_FLAGS {
    /// sbufCreate(): guard the buffer with a lock so the producer and the consumer may run on
    /// different threads. Without it a stream buffer must only ever be touched by one thread.
    SBUF_Locked = 0x0004,

    /// sbufPWrite() and friends: wait for the buffer to drain when it is full instead of returning
    /// false. Requires SBUF_Locked, since the thread that has to drain the buffer cannot be the
    /// one that is waiting on it.
    SBUF_Wait = 0x0008,
};

/// @cond IGNORE
// Internal state, not for callers.
enum STREAM_BUFFER_STATE_ENUM {
    SBUF_PHeld       = 0x0020,   // producer is held at the high watermark
    SBUF_PResumeOwed = 0x0040,   // a write was refused; owe the producer a resume callback
};

/// Stream buffer structure for managing producer-consumer data flow
typedef struct StreamBuffer {
    BufRing buf;                     ///< Underlying buffer ring for data storage
    size_t targetsz;                 ///< Buffer size that producers should aim for

    sbufPullCB producerPull;         ///< Producer pull callback; non-NULL means pull mode
    sbufCleanupCB producerCleanup;   ///< Producer cleanup callback
    void* producerCtx;               ///< Producer context

    sbufResumeCB producerResume;     ///< Producer resume callback
    void* producerResumeCtx;         ///< Context for the resume callback

    sbufNotifyCB consumerNotify;     ///< Consumer notify callback; non-NULL means push mode
    sbufPushCB consumerPush;         ///< Consumer push callback; non-NULL means direct push mode
    sbufCleanupCB consumerCleanup;   ///< Consumer cleanup callback
    void* consumerCtx;               ///< Consumer context

    // Cleanup callbacks owed to a role that unregistered from inside a callback, paid out once the
    // stack has unwound back out of the buffer.
    sbufCleanupCB pendingPCleanup;
    void* pendingPCleanupCtx;
    sbufCleanupCB pendingCCleanup;
    void* pendingCCleanupCtx;

    size_t high;                     ///< Hold the producer at this much buffered data (0 = never)
    size_t low;                      ///< Release the producer once drained back to this much

    Mutex lock;                      ///< Guards everything here; only used when SBUF_Locked
    CondVar ready;                   ///< Parks a producer that is waiting out the high watermark
    CondVar flushed;                 ///< Parks a producer waiting in sbufPFlush() for an empty ring
    atomic(intptr) owner;            ///< Thread identity of the lock holder; 0 == free
    uint32 depth;                    ///< Recursion depth of the entry points, 0 when not inside one

    int refcount;                    ///< Reference count for lifecycle management
    bool locked;                     ///< Copy of SBUF_Locked; never changes after creation
    bool resumePending;              ///< Producer resume callback is owed once the lock is gone
    bool destroyPending;             ///< Last reference went away inside a callback
    bool walking;                    ///< Inside a zero-copy ring walk; nothing may touch the ring
    atomic(uint32) flags;            ///< Operating mode and state flags
} StreamBuffer;
/// @endcond

// Internal function - use sbufCreate() macro instead
_Ret_valid_ StreamBuffer* _sbufCreate(size_t targetsz, flags_t flags);

/// StreamBuffer *sbufCreate(size_t targetsz, [flags])
///
/// Creates a new stream buffer with the specified target size.
///
/// The buffer will automatically grow as needed but tries to stay near targetsz.
/// Set targetsz to 0 only when using direct push mode (no buffering needed).
///
/// @param targetsz Target buffer size in bytes (0 for direct mode)
/// @param ... (flags) Pass SBUF_Locked if the producer and consumer run on different threads
/// @return New stream buffer (must be released with sbufRelease)
///
/// Example:
/// @code
///   StreamBuffer *sb = sbufCreate(4096);
///   // ... register the callback side and run the stream ...
///   sbufRelease(&sb);
/// @endcode
#define sbufCreate(targetsz, ...) _sbufCreate(targetsz, opt_flags(__VA_ARGS__))

/// Takes another reference to a stream buffer.
///
/// Acquire one whenever you store the pointer somewhere that outlives the call you got it from.
///
/// @param sb The stream buffer
/// @return The same stream buffer
///
/// Example:
/// @code
///   self->stream = sbufAcquire(sb);
/// @endcode
_Ret_valid_ StreamBuffer* sbufAcquire(_Inout_ StreamBuffer* sb);

/// void sbufRelease(StreamBuffer **sb)
///
/// Releases a reference to a stream buffer.
///
/// Decrements the reference count and destroys the buffer when it reaches zero.
/// Sets the pointer to NULL after release.
///
/// @param sb Pointer to stream buffer pointer
_At_(*sb, _Pre_maybenull_ _Post_null_) void sbufRelease(_Inout_ StreamBuffer** sb);

/// Closes the stream to traffic.
///
/// Called by whichever side is driving: the producer in push mode, the consumer in pull mode. No
/// more data will be written, but a consumer may still drain what is already buffered. The
/// registered side gets one final callback with sz == 0 and is expected to unregister itself.
///
/// This does not release any references. Both sides still have to release theirs.
///
/// @param sb The stream buffer
void sbufClose(_Inout_ StreamBuffer* sb);

/// void sbufError(StreamBuffer *sb)
///
/// Reports that something went wrong with the stream.
///
/// Reads and writes fail while the error stands, so the driving side finds out on its next call.
/// The stream is not over: the driving side may end it, or unregister whoever failed, call
/// sbufClearError() and attach a replacement.
///
/// @param sb The stream buffer
void sbufError(_Inout_ StreamBuffer* sb);

/// void sbufClearError(StreamBuffer *sb)
///
/// Clears the error state so the stream can be used again.
///
/// For the driving side, once it has dealt with whatever failed. Any data still buffered is kept;
/// call sbufCSkip() to throw away a partial record.
///
/// @param sb The stream buffer
void sbufClearError(_Inout_ StreamBuffer* sb);

/// Sets the flow control watermarks.
///
/// Once the amount of buffered data reaches high, the producer is held until the consumer drains
/// it back down to low. Held means either waiting inside sbufPWrite() or being refused by it,
/// depending on whether the write passed SBUF_Wait. Both marks default to 0, which lets the buffer
/// grow without limit.
///
/// @param sb The stream buffer
/// @param high Amount of buffered data that holds the producer (0 turns flow control off)
/// @param low Amount to drain back down to before releasing it (0 uses half of high)
///
/// Example:
/// @code
///   sbufSetWatermark(sb, 65536, 16384);
/// @endcode
void sbufSetWatermark(_Inout_ StreamBuffer* sb, size_t high, size_t low);

/// bool sbufIsLocked(StreamBuffer *sb)
///
/// Checks whether the buffer was created with SBUF_Locked.
///
/// @param sb The stream buffer
/// @return true if the buffer may be used from more than one thread
_meta_inline bool sbufIsLocked(_In_ StreamBuffer* sb)
{
    return sb->locked;
}

/// bool sbufIsPull(StreamBuffer *sb)
///
/// Checks if the stream buffer is in pull mode.
///
/// @param sb The stream buffer
/// @return true if in pull mode
_meta_inline bool sbufIsPull(_In_ StreamBuffer* sb)
{
    return (atomicLoad(uint32, &sb->flags, Relaxed) & SBUF_Pull) != 0;
}

/// bool sbufIsPush(StreamBuffer *sb)
///
/// Checks if the stream buffer is in push mode.
///
/// @param sb The stream buffer
/// @return true if in push mode
_meta_inline bool sbufIsPush(_In_ StreamBuffer* sb)
{
    return (atomicLoad(uint32, &sb->flags, Relaxed) & SBUF_Push) != 0;
}

/// bool sbufIsError(StreamBuffer *sb)
///
/// Checks if the stream buffer is in an error state.
///
/// @param sb The stream buffer
/// @return true if in error state
_meta_inline bool sbufIsError(_In_ StreamBuffer* sb)
{
    return (atomicLoad(uint32, &sb->flags, Relaxed) & SBUF_Error) != 0;
}

/// bool sbufIsClosed(StreamBuffer *sb)
///
/// Checks whether the stream is closed.
///
/// Data already buffered may still be read; this only says that no more is coming.
///
/// @param sb The stream buffer
/// @return true if sbufClose() has been called
_meta_inline bool sbufIsClosed(_In_ StreamBuffer* sb)
{
    return (atomicLoad(uint32, &sb->flags, Relaxed) & SBUF_Closed) != 0;
}

/// Checks whether more data may still arrive.
///
/// False once the stream has closed, once it has failed, or while no producer is attached to a pull
/// stream. This is the test a drain loop wants:
///
/// @code
///   while (sz > 0 || sbufCMore(sb)) { ... }
/// @endcode
///
/// @param sb The stream buffer
/// @return true if it is worth asking for more data
bool sbufCMore(_Inout_ StreamBuffer* sb);

/// @}  // end of serialize_streambuf_core

/// @defgroup serialize_streambuf_producer Producer Functions
/// @ingroup serialize_streambuf
/// @{
///
/// Functions for the producer side of stream buffer operations.

/// bool sbufPRegisterPull(StreamBuffer *sb, sbufPullCB ppull, sbufCleanupCB pcleanup, void *ctx)
///
/// Registers a producer callback, putting the buffer in pull mode.
///
/// The consumer drives from here on: every sbufCRead() calls this callback to fill the buffer.
/// Registration takes its own reference to the buffer and gives it back on unregister, so keep
/// your own as well.
///
/// @param sb The stream buffer
/// @param ppull Pull callback to provide data
/// @param pcleanup Optional cleanup callback for ctx
/// @param ctx Optional user context passed to callbacks
/// @return true on success, false if a producer is already attached, the stream closed, or the
///         buffer is already in push mode
_Check_return_ bool sbufPRegisterPull(_Inout_ StreamBuffer* sb, _In_ sbufPullCB ppull,
                                      _In_opt_ sbufCleanupCB pcleanup, _Inout_opt_ void* ctx);

/// Detaches the producer.
///
/// Empties the producer slot, runs its cleanup callback and gives back the reference the
/// registration took. The stream is not closed: another producer may register, and until one does a
/// consumer gets short reads. A pull producer that has run out of data calls this on itself.
///
/// Safe to call from inside the producer's own callback. Does nothing if no producer is attached.
///
/// @param sb The stream buffer
void sbufPUnregister(_Inout_ StreamBuffer* sb);

/// Checks whether a producer callback is currently attached.
///
/// Only pull streams have one; this is always false in push mode, where the producer drives.
///
/// @param sb The stream buffer
/// @return true if a pull producer is registered
bool sbufPAttached(_Inout_ StreamBuffer* sb);

/// Sets the callback that tells the producer it may write again.
///
/// Only useful for a producer that does not pass SBUF_Wait. When sbufPWrite() refuses a write
/// because the buffer is full, this callback fires once the consumer has drained it back to the
/// low mark.
///
/// @param sb The stream buffer
/// @param resume Callback to invoke when writing may continue (NULL to remove)
/// @param ctx Optional user context passed to the callback
///
/// Example:
/// @code
///   sbufPSetResume(sb, myResumeCallback, self);
/// @endcode
void sbufPSetResume(_Inout_ StreamBuffer* sb, _In_opt_ sbufResumeCB resume, _Inout_opt_ void* ctx);

/// Checks whether the producer is currently held at the high watermark.
///
/// Use this to tell why sbufPWrite() returned false: true here means the buffer is full and a
/// resume callback is coming, false means the stream closed or failed.
///
/// @param sb The stream buffer
/// @return true if the buffer is full and writes are being refused
bool sbufPIsHeld(_Inout_ StreamBuffer* sb);

/// size_t sbufPAvail(StreamBuffer *sb)
///
/// Returns the available space for writing to the buffer.
///
/// @param sb The stream buffer
/// @return Number of bytes available for writing
size_t sbufPAvail(_Inout_ StreamBuffer* sb);

// Internal function - use sbufPWrite() macro instead
bool _sbufPWrite(_Inout_ StreamBuffer* sb, _In_reads_bytes_(sz) const uint8* buf, size_t sz,
                 flags_t flags);

/// bool sbufPWrite(StreamBuffer *sb, const uint8 *buf, size_t sz, [flags])
///
/// Writes data to the buffer.
///
/// This always succeeds unless the stream has closed or failed, the buffer is full, or the system
/// is out of memory. The buffer grows past its target size rather than short-writing.
///
/// With no consumer attached the data simply accumulates, and the next consumer to register is
/// handed all of it.
///
/// @param sb The stream buffer
/// @param buf Data to write
/// @param sz Number of bytes to write
/// @param ... (flags) Pass SBUF_Wait to wait at the watermark instead of being refused
/// @return true on success, false if the stream closed or failed, or the buffer is full
#define sbufPWrite(sb, buf, sz, ...) _sbufPWrite(sb, buf, sz, opt_flags(__VA_ARGS__))

// Internal function - use sbufPWriteStr() macro instead
bool _sbufPWriteStr(_Inout_ StreamBuffer* sb, _In_opt_ strref str, flags_t flags);

/// bool sbufPWriteStr(StreamBuffer *sb, strref str, [flags])
///
/// Writes a string to the buffer.
///
/// @param sb The stream buffer
/// @param str String to write
/// @param ... (flags) Pass SBUF_Wait to wait at the watermark instead of being refused
/// @return true on success, false on error
#define sbufPWriteStr(sb, str, ...) _sbufPWriteStr(sb, str, opt_flags(__VA_ARGS__))

// Internal function - use sbufPWriteLine() macro instead
bool _sbufPWriteLine(_Inout_ StreamBuffer* sb, _In_opt_ strref str, flags_t flags);

/// bool sbufPWriteLine(StreamBuffer *sb, strref str, [flags])
///
/// Writes a string followed by a system-dependent line ending to the buffer.
///
/// Uses `\r\n` on Windows, `\n` on Unix systems.
///
/// @param sb The stream buffer
/// @param str String to write
/// @param ... (flags) Pass SBUF_Wait to wait at the watermark instead of being refused
/// @return true on success, false on error
#define sbufPWriteLine(sb, str, ...) _sbufPWriteLine(sb, str, opt_flags(__VA_ARGS__))

// Internal function - use sbufPWriteEOL() macro instead
bool _sbufPWriteEOL(_Inout_ StreamBuffer* sb, flags_t flags);

/// bool sbufPWriteEOL(StreamBuffer *sb, [flags])
///
/// Writes a system-dependent line ending to the buffer.
///
/// Uses `\r\n` on Windows, `\n` on Unix systems.
///
/// @param sb The stream buffer
/// @param ... (flags) Pass SBUF_Wait to wait at the watermark instead of being refused
/// @return true on success, false on error
#define sbufPWriteEOL(sb, ...) _sbufPWriteEOL(sb, opt_flags(__VA_ARGS__))

/// Hands the consumer everything written so far and reports whether it took all of it.
///
/// Use this mid-stream, whenever the producer needs to know its bytes have landed -- at the end of
/// a frame, or before swapping the consumer out. It says nothing about the stream being over.
///
/// On an unlocked buffer this notifies the consumer and returns right away. On a locked buffer it
/// also waits for the consumer's thread to finish draining.
///
/// Not valid in pull mode, where the buffer only fills on demand. Direct push mode never buffers,
/// so there is nothing to catch up on and this always succeeds. Must not be called from inside a
/// stream buffer callback.
///
/// @param sb The stream buffer
/// @return true if the buffer is now empty, false if the consumer left data behind or the stream
///         closed or failed while waiting
bool sbufPFlush(_Inout_ StreamBuffer* sb);

/// @}  // end of serialize_streambuf_producer

/// @defgroup serialize_streambuf_consumer Consumer Functions
/// @ingroup serialize_streambuf
/// @{
///
/// Functions for the consumer side of stream buffer operations.

/// bool sbufCRegisterPush(StreamBuffer *sb, sbufNotifyCB cnotify, sbufCleanupCB ccleanup, void
/// *ctx)
///
/// Registers a consumer notification callback, putting the buffer in push mode.
///
/// The producer drives from here on: the callback runs whenever data is available, and the consumer
/// uses sbufCRead() or sbufCSend() to take as much of it as it wants. If the producer has already
/// written something, the callback fires once immediately with the backlog.
///
/// Registration takes its own reference to the buffer and gives it back on unregister, so keep your
/// own as well.
///
/// @param sb The stream buffer
/// @param cnotify Notification callback
/// @param ccleanup Optional cleanup callback for ctx
/// @param ctx Optional user context passed to callbacks
/// @return true on success, false if a consumer is already attached, the stream has closed, or the
///         buffer is already in pull or direct mode
_Check_return_ bool sbufCRegisterPush(_Inout_ StreamBuffer* sb, _In_ sbufNotifyCB cnotify,
                                      _In_opt_ sbufCleanupCB ccleanup, _Inout_opt_ void* ctx);

/// bool sbufCRegisterPushDirect(StreamBuffer *sb, sbufPushCB cpush, sbufCleanupCB ccleanup, void
/// *ctx)
///
/// Registers a consumer in direct push mode.
///
/// Data is handed to the callback as it is written and never buffered, so the consumer must take
/// all of it every time. A direct buffer has no storage of its own: with no consumer attached
/// there is nowhere for a write to go and it fails.
///
/// @param sb The stream buffer
/// @param cpush Push callback to receive data
/// @param ccleanup Optional cleanup callback for ctx
/// @param ctx Optional user context passed to callbacks
/// @return true on success, false if a consumer is already attached, the stream has closed, or the
///         buffer is already in pull mode
_Check_return_ bool sbufCRegisterPushDirect(_Inout_ StreamBuffer* sb, _In_ sbufPushCB cpush,
                                            _In_opt_ sbufCleanupCB ccleanup, _Inout_opt_ void* ctx);

/// Detaches the consumer.
///
/// Empties the consumer slot, runs its cleanup callback and gives back the reference the
/// registration took. The stream is not closed: another consumer may register and will be handed
/// everything that piled up in the meantime.
///
/// Call sbufPFlush() first when swapping consumers, so the bytes already written reach the one that
/// is leaving rather than the one arriving.
///
/// Safe to call from inside the consumer's own callback. Does nothing if no consumer is attached.
///
/// @param sb The stream buffer
void sbufCUnregister(_Inout_ StreamBuffer* sb);

/// Checks whether a consumer callback is currently attached.
///
/// Only push streams have one; this is always false in pull mode, where the consumer drives.
///
/// @param sb The stream buffer
/// @return true if a push consumer is registered
bool sbufCAttached(_Inout_ StreamBuffer* sb);

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
/// **Pull mode:** Repeatedly calls the producer's callback to satisfy the request. Short-reads once
/// the stream ends or the producer unregisters.
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
/// Only looks at data already in the buffer and never calls the producer; use sbufCFeed() first in
/// pull mode.
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
/// Keeps calling the producer until enough data is available, the stream ends, or the producer
/// unregisters.
///
/// @param sb The stream buffer
/// @param minsz Minimum bytes to ensure are buffered
/// @return true if request satisfied, false if the data ran out first
bool sbufCFeed(_Inout_ StreamBuffer* sb, size_t minsz);

/// bool sbufCSend(StreamBuffer *sb, sbufSendCB func, size_t sz, void *ctx)
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
/// @param ctx Optional user context passed to the callback
/// @return true on success
bool sbufCSend(_Inout_ StreamBuffer* sb, _In_ sbufSendCB func, size_t sz, _Inout_opt_ void* ctx);

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

/// @}  // end of serialize_streambuf_consumer
/// @}  // end of serialize_streambuf

CX_C_END
