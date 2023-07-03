#pragma once

#include <cx/core/stype.h>
#include <cx/thread/mutex.h>

typedef struct StreamBuffer StreamBuffer;

// Thread-safety note:
// For correct function in threadsafe mode, the last parameter of the callbacks MUST be an
// integer named _streambuf_lock_recursion_level. There are some macros that include this
// as a hidden parameter in API calls in order to track if the current thread has already
// acquired the streambuf mutex.

// This enum acts as a default for the hidden parameter. It's hidden by the actual variable
// inside the callback functions.
enum {
    _streambuf_lock_recursion_level = 0
};

// Pull callback
// sz is set to the maximum amount of data the may be written to buf.
// The callback should fill buf with up to that amount and return the number
// of bytes that were written.
// The callback may return 0 if no data is currently available but will likely
// be immediately called again, so a performing a blocking wait is advisable.
// If sz is 0, check if the consumer is finished and/or for error state.
typedef size_t(*sbufPullCB)(StreamBuffer *sb, char *buf, size_t sz, void *ctx, int _streambuf_lock_recursion_level);

// Push callback
// When this callback is used (in direct mode), the data is pushed once to the
// callback and MUST all be written in one go or it will be lost.
// If sz is 0, check if the producer is finished and/or for error state.
typedef bool(*sbufPushCB)(StreamBuffer *sb, const char *buf, size_t sz, void *ctx, int _streambuf_lock_recursion_level);

// Notify callback
// Notification to a consumer that data is available. The sbufC* functions may be
// used to read all or part of the available data.
// If sz is 0, check if the producer is finished and/or for error state.
typedef void(*sbufNotifyCB)(StreamBuffer *sb, size_t sz, void *ctx, int _streambuf_lock_recursion_level);

// Cleanup callback
// Called just before the structure is deallocated, should perform any needed
// cleanup of the user-supplied ctx.
typedef void(*sbufCleanupCB)(void *ctx);

// Push mode:   Producer repeatedly calls sbufPWrite.
//              Consumer is either notified that data is available via callback,
//              or if consumer is in direct mode, available data is pushed to its callback.
// Pull mode:   Consumer repeatedly calls sbufCRead or sbufCSend.
//              Producer's pull callback is called in order to fill the buffer.

enum STREAM_BUFFER_FLAGS_ENUM {
    SBUF_Push =                 0x0001,
    SBUF_Pull =                 0x0002,
    SBUF_Direct =               0x0010,
    SBUF_Thread_Safe =          0x0100,
    SBUF_Defer_Destroy =        0x0200,
    SBUF_Error =                0x0800,
    SBUF_Producer_Registered =  0x1000,
    SBUF_Consumer_Registered =  0x2000,
    SBUF_Producer_Done =        0x4000,
    SBUF_Consumer_Done =        0x8000,
};

typedef struct StreamBuffer {
    char *buf;
    size_t bufsz;               // current buffer size
    size_t targetsz;            // desired max size (can be exceeded in push mode)

    char *overflow;             // for buffering writes when expanding the main buffer
    size_t overflowtail;        // where in the overflow buffer to start writing
    size_t overflowsz;          // size of overflow buffer, >0 serves as flag that we're swapping buffers

    size_t head;                // start of valid data in buffer
    size_t tail;                // end of valid data in buffer
    // Note that head == tail means an empty buffer, so the maximum amount of data is bufsz - 1.
    // This wastes 1 byte but is worth it to make the logic much simpler.

    sbufPullCB producerPull;
    sbufCleanupCB producerCleanup;
    void *producerCtx;

    sbufNotifyCB consumerNotify;
    sbufPushCB consumerPush;
    sbufCleanupCB consumerCleanup;
    void *consumerCtx;

    Mutex lock;
    uint32 flags;
} StreamBuffer;

// Create a stream buffer
// Set threadsafe to true if the stream buffer may be accessed from multiple threads,
// i.e. a producer in a thread pool in push mode. Note that consumer callbacks will be
// called in whatever thread the producer pushes from!
// Otherwise leave it false for best performance in a single thread, which is the
// typical use case.
// targetsz may be set to 0 if direct mode is going to be used.
StreamBuffer *sbufCreate(size_t targetsz, bool threadsafe);

// Puts a stream buffer into error state. Processing will be aborted and both consumers
// and producers should call the Finish function as soon as is practical.
#define sbufError(sb) _sbufError(sb, _streambuf_lock_recursion_level)
void _sbufError(StreamBuffer *sb, int _streambuf_lock_recursion_level);

_meta_inline bool sbufIsPull(StreamBuffer *sb)
{
    return sb->flags & SBUF_Pull;
}

_meta_inline bool sbufIsPush(StreamBuffer *sb)
{
    return sb->flags & SBUF_Push;
}

// Checks if the stream buffer is in an error state
_meta_inline bool sbufIsError(StreamBuffer *sb)
{
    return sb->flags & SBUF_Error;
}

// Returns true if the producer is finished (EOF)
_meta_inline bool sbufIsPFinished(StreamBuffer *sb)
{
    return (sb->flags & SBUF_Producer_Done) || sbufIsError(sb);
}

// Returns true if the consumer is finished (early out, data no longer need be produced)
_meta_inline bool sbufIsCFinished(StreamBuffer *sb)
{
    return (sb->flags & SBUF_Consumer_Done) || sbufIsError(sb);
}

// ---------------------------------------------------------------------------- Producer Functions

// Registers a producer with the stream buffer in pull mode
bool sbufPRegisterPull(StreamBuffer *sb, sbufPullCB ppull, sbufCleanupCB pcleanup, void *ctx);
// Registers a producer with the stream buffer in push mode
bool sbufPRegisterPush(StreamBuffer *sb, sbufCleanupCB pcleanup, void *ctx);

// Returns the available space for writing to the buffer
#define sbufPAvail(sb) _sbufPAvail(sb, _streambuf_lock_recursion_level)
size_t _sbufPAvail(StreamBuffer *sb, int _streambuf_lock_recursion_level);
// Writes data to the buffer. This will always succeed unless the system is out of memory.
// Overflow buffer is used if more written data exceeds current buffer size.
// For use in push mode only!
#define sbufPWrite(sb, buf, sz) _sbufPWrite(sb, buf, sz, _streambuf_lock_recursion_level)
bool _sbufPWrite(StreamBuffer *sb, const char *buf, size_t sz, int _streambuf_lock_recursion_level);
// Mark the producer as finished. The producer MUST NOT touch the stream buffer after
// call as it maybe immediately deallocated.
#define sbufPFinish(sb) _sbufPFinish(sb, _streambuf_lock_recursion_level)
void _sbufPFinish(StreamBuffer *sb, int _streambuf_lock_recursion_level);

// ---------------------------------------------------------------------------- Consumer Functions

// Registers a consumer with the stream buffer in pull mode
bool sbufCRegisterPull(StreamBuffer *sb, sbufCleanupCB ccleanup, void *ctx);
// Registers a consumer with the stream buffer in push mode
bool sbufCRegisterPush(StreamBuffer *sb, sbufNotifyCB cnotify, sbufCleanupCB ccleanup, void *ctx);
// Registers a consumer with the stream buffer in direct push mode
bool sbufCRegisterPushDirect(StreamBuffer *sb, sbufPushCB cpush, sbufCleanupCB ccleanup, void *ctx);

// Returns how much data is currently buffered waiting to be consumed.
#define sbufCAvail(sb) _sbufCAvail(sb, _streambuf_lock_recursion_level)
size_t _sbufCAvail(StreamBuffer *sb, int _streambuf_lock_recursion_level);
// Reads sz bytes from the producer.
// If in pull mode, this will repeatedly call the producer's callback in order to completely
// satisfy the request. It will short read only when the producer is finished (EOF).
// If in push mode, this will fail if more data is requested than is available.
#define sbufCRead(sb, buf, sz) _sbufCRead(sb, buf, sz, _streambuf_lock_recursion_level)
size_t _sbufCRead(StreamBuffer *sb, char *buf, size_t sz, int _streambuf_lock_recursion_level);

// Peeks at data in the buffer without consuming it.
// In pull mode this will NOT call the callback and only read unconsumed data left in
// the buffer.
// Will never short read, will fail if there is not enough in the buffer (check sbufCAvail).
#define sbufCPeek(sb, buf, sz) _sbufCPeek(sb, buf, sz, _streambuf_lock_recursion_level)
bool _sbufCPeek(StreamBuffer *sb, char *buf, size_t sz, int _streambuf_lock_recursion_level);

// Sends at most sz bytes from the buffer to a callback function.
// The callback may be called multiple times in order to send the requested data, it is not
// guaranteed that it will arrive in a single chunk.
// This can be used for more efficient operation in push mode. In the notify callback, call
// sbufCSend with the desired number of bytes. The difference is that sbufCRead will copy the
// data to another buffer, while sbufCSend will invoke the callback with a pointer to the
// streambuf's internal buffer, which avoids making another copy.
// If used in pull mode, it functions like sbufCRead and will try to fill the buffer with
// the requested amount of data before calling the callback.
// The callback should return true if it consumed the data, or false if it is only peeking.
#define sbufCSend(sb, func, sz) _sbufCSend(sb, func, sz, _streambuf_lock_recursion_level)
bool _sbufCSend(StreamBuffer *sb, sbufPushCB func, size_t sz, int _streambuf_lock_recursion_level);

// Skip over bytes in the buffer, can be used in conjunction with sbufCPeek.
#define sbufCSkip(sb, bytes) _sbufCSkip(sb, bytes, _streambuf_lock_recursion_level)
bool _sbufCSkip(StreamBuffer *sb, size_t bytes, int _streambuf_lock_recursion_level);

// Mark the consumer as finished. The consumer MUST NOT touch the stream buffer after
// call as it maybe immediately deallocated.
#define sbufCFinish(sb) _sbufCFinish(sb, _streambuf_lock_recursion_level)
void _sbufCFinish(StreamBuffer *sb, int _streambuf_lock_recursion_level);
