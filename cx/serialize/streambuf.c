#include "streambuf.h"
#include <cx/container/foreach.h>
#include <cx/debug/error.h>
#include <cx/meta/block.h>
#include <cx/string/striter.h>
#include <cx/utils/compare.h>

// Thread identity for the recursive lock below. The address of a thread-local is unique per
// thread and never 0, and unlike thrCurrentOSThreadID() it does not pull the thread module into
// the cut-down cx that cxautogen bootstraps with.
static _Thread_local char sbufThreadMarker;
#define sbufSelf() ((intptr)&sbufThreadMarker)

static void sbufDestroy(_Pre_valid_ _Post_invalid_ StreamBuffer* sb);
static bool sbufPFinishLocked(_Inout_ StreamBuffer* sb);
static bool sbufCFinishLocked(_Inout_ StreamBuffer* sb);

// Recursive per-buffer lock, active only when the buffer was created with SBUF_Locked.
//
// The recursion is load-bearing rather than a convenience. sbufPullCB's contract lets a pull
// producer call sbufPWrite() from inside its own callback when it has more data than the slice it
// was asked for, and that callback runs from feedBuffer() on a thread that is already holding the
// lock. cx's Mutex is not recursive, so ownership is tracked here instead.
static void sbufLock(_Inout_ StreamBuffer* sb)
{
    if (!sb->locked)
        return;

    intptr self = sbufSelf();

    if (atomicLoad(intptr, &sb->owner, Relaxed) == self) {
        ++sb->depth;
        return;
    }

    mutexAcquire(&sb->lock);
    atomicStore(intptr, &sb->owner, self, Relaxed);
    sb->depth = 1;
}

static void sbufUnlock(_Inout_ StreamBuffer* sb)
{
    if (!sb->locked)
        return;

    devAssert(sb->depth > 0);
    if (--sb->depth > 0)
        return;

    atomicStore(intptr, &sb->owner, 0, Relaxed);
    mutexRelease(&sb->lock);
}

static uint32 sbufFlags(_In_ StreamBuffer* sb)
{
    return atomicLoad(uint32, &sb->flags, Relaxed);
}

static void sbufSetFlags(_Inout_ StreamBuffer* sb, uint32 set)
{
    atomicFetchOr(uint32, &sb->flags, set, Relaxed);
}

static void sbufClearFlags(_Inout_ StreamBuffer* sb, uint32 clear)
{
    atomicFetchAnd(uint32, &sb->flags, ~clear, Relaxed);
}

static size_t sbufCAvailLocked(_In_ StreamBuffer* sb)
{
    if (sbufFlags(sb) & SBUF_Direct)
        return 0;   // there is no buffer

    return sb->buf.total;
}

// Unparks a producer waiting at the watermark without regard to how full the buffer is. Used when
// the stream ends, because the wait loop rechecks sbufIsCFinished() every time it wakes.
static void sbufWakeProducerLocked(_Inout_ StreamBuffer* sb)
{
    if (sb->locked)
        cvarBroadcast(&sb->drained);
}

// Engages the hold once the buffer fills, and reports whether the producer is currently held.
//
// Push mode only: in pull mode the write is happening inside the producer's own callback, on the
// consumer's thread, so holding it there would stall the very consumer that has to drain it.
static bool sbufHoldProducerLocked(_Inout_ StreamBuffer* sb)
{
    uint32 f = sbufFlags(sb);

    if (sb->high == 0 || !(f & SBUF_Push) || (f & SBUF_Direct))
        return false;

    if (!(f & SBUF_PHeld) && sbufCAvailLocked(sb) >= sb->high) {
        sbufSetFlags(sb, SBUF_PHeld);
        return true;
    }

    return (f & SBUF_PHeld) != 0;
}

// Lets the producer go once the consumer has drained back to the low mark: wakes anything waiting
// inside sbufPWrite() and pays out the resume callback owed to a producer that was refused.
// force ignores the low mark, for when flow control is turned off entirely.
static void sbufReleaseProducerLocked(_Inout_ StreamBuffer* sb, bool force)
{
    uint32 f = sbufFlags(sb);

    if (!(f & (SBUF_PHeld | SBUF_PResumeOwed)))
        return;

    if (!force && sbufCAvailLocked(sb) > sb->low)
        return;

    sbufClearFlags(sb, SBUF_PHeld | SBUF_PResumeOwed);
    sbufWakeProducerLocked(sb);

    // The resume callback is left to sbufUnlockAndResume(), once the lock is gone and this
    // operation has finished. A producer that writes from inside it has to arrive as a fresh
    // top-level write: run inline from here it would look re-entrant, and re-entrant writes skip
    // the watermark check on purpose.
    if (f & SBUF_PResumeOwed)
        sb->resumePending = true;
}

// Tail end of every consumer entry point that can drain the buffer.
static void sbufUnlockAndResume(_Inout_ StreamBuffer* sb)
{
    // when nested, leave it for the outermost frame to pay out
    bool resume = sb->resumePending && (!sb->locked || sb->depth == 1);
    if (resume)
        sb->resumePending = false;

    sbufUnlock(sb);

    if (resume && sb->producerResume)
        sb->producerResume(sb, sb->producerCtx);
}

// Returns true if the producer may go ahead, false if the write must be refused or the stream
// ended while the producer was parked.
static bool sbufWaitDrainLocked(_Inout_ StreamBuffer* sb)
{
    if (!(sbufFlags(sb) & SBUF_PBlock)) {
        // Asynchronous producer: refuse the write now, resume callback when there is room again.
        sbufSetFlags(sb, SBUF_PResumeOwed);
        return false;
    }

    // Registration rejects SBUF_PBlock on an unlocked buffer, and only a top-level sbufPWrite()
    // reaches this, so the wait can safely hand the mutex back.
    devAssert(sb->locked && sb->depth == 1);

    intptr self = sbufSelf();
    while ((sbufFlags(sb) & SBUF_PHeld) && !sbufIsCFinished(sb)) {
        // cvarWait releases the mutex, so ownership has to go with it
        atomicStore(intptr, &sb->owner, 0, Relaxed);
        sb->depth = 0;

        cvarWait(&sb->drained, &sb->lock);

        atomicStore(intptr, &sb->owner, self, Relaxed);
        sb->depth = 1;
    }

    return !sbufIsCFinished(sb);
}

// Drops a reference. Returns true if it was the last one, meaning the caller must destroy the
// buffer once it is no longer holding the lock that lives inside it.
static bool sbufDerefLocked(_Inout_ StreamBuffer* sb)
{
    return --sb->refcount <= 0;
}

static void sbufUnlockAndRelease(_Inout_ StreamBuffer* sb, bool destroy)
{
    sbufUnlock(sb);

    if (destroy) {
        // Dropping the last reference from inside a callback would free the buffer out from under
        // the code still walking it further up the stack.
        devAssert(sb->depth == 0);
        sbufDestroy(sb);
    }
}

_Use_decl_annotations_
StreamBuffer* _sbufCreate(size_t targetsz, flags_t flags)
{
    StreamBuffer* ret = xaAlloc(sizeof(StreamBuffer), XA_Zero);

    ret->refcount = 1;

    if (flags & SBUF_Locked) {
        ret->locked = true;
        sbufSetFlags(ret, SBUF_Locked);
        mutexInit(&ret->lock);
        cvarInit(&ret->drained);
    }

    if (targetsz > 0) {
        bufringInit(&ret->buf, targetsz);
        ret->targetsz = targetsz;
    } else {
        // targetsz == 0 is used only for direct mode,
        // go ahead and lock this buffer into push mode
        sbufSetFlags(ret, SBUF_Push);
    }

    return ret;
}

static void sbufDestroy(_Pre_valid_ _Post_invalid_ StreamBuffer* sb)
{
    if (sb->consumerCleanup)
        sb->consumerCleanup(sb->consumerCtx);
    if (sb->producerCleanup)
        sb->producerCleanup(sb->producerCtx);

    bufringDestroy(&sb->buf);

    if (sb->locked) {
        cvarDestroy(&sb->drained);
        mutexDestroy(&sb->lock);
    }

    xaFree(sb);
}

_Use_decl_annotations_
void sbufRelease(StreamBuffer** sb)
{
    if (*sb) {
        StreamBuffer* b = *sb;
        *sb             = NULL;

        sbufLock(b);
        sbufUnlockAndRelease(b, sbufDerefLocked(b));
    }
}

_Use_decl_annotations_
void sbufError(StreamBuffer* sb)
{
    sbufLock(sb);

    sbufSetFlags(sb, SBUF_Error);

    // a producer parked at the watermark has to come back and find out
    sbufWakeProducerLocked(sb);

    if (!(sbufFlags(sb) & SBUF_Consumer_Done)) {
        if (sb->consumerNotify)
            sb->consumerNotify(sb, 0, sb->consumerCtx);
        if (sb->consumerPush)
            sb->consumerPush(sb, NULL, 0, sb->consumerCtx);
    }

    if (!(sbufFlags(sb) & SBUF_Producer_Done) && sb->producerPull)
        sb->producerPull(sb, NULL, 0, sb->producerCtx);

    sbufUnlock(sb);
}

_Use_decl_annotations_
void sbufSetWatermark(StreamBuffer* sb, size_t high, size_t low)
{
    sbufLock(sb);

    sb->high = high;
    sb->low  = (low > 0 && low < high) ? low : high / 2;

    // moving the marks can strand a producer that was held under the old ones
    sbufReleaseProducerLocked(sb, high == 0);

    sbufUnlockAndResume(sb);
}

_Use_decl_annotations_
bool sbufPIsHeld(StreamBuffer* sb)
{
    sbufLock(sb);
    bool ret = (sbufFlags(sb) & SBUF_PHeld) != 0;
    sbufUnlock(sb);

    return ret;
}

_Use_decl_annotations_
bool sbufPRegisterPull(StreamBuffer* sb, sbufPullCB ppull, sbufCleanupCB pcleanup, void* ctx)
{
    sbufLock(sb);

    // can only register once, if it's not already a push buffer
    if (sbufIsPush(sb) || (sbufFlags(sb) & SBUF_Producer_Registered) || !ppull) {
        sbufUnlock(sb);

        if (pcleanup)
            pcleanup(ctx);

        cxerr = CX_InvalidArgument;
        return false;
    }

    sb->producerPull    = ppull;
    sb->producerCleanup = pcleanup;
    sb->producerCtx     = ctx;
    sbufSetFlags(sb, SBUF_Producer_Registered | SBUF_Pull);
    sb->refcount++;

    sbufUnlock(sb);
    return true;
}

_Use_decl_annotations_
bool _sbufPRegisterPush(StreamBuffer* sb, sbufCleanupCB pcleanup, void* ctx, flags_t flags)
{
    sbufLock(sb);

    // can only register once, if it's not already a pull buffer. SBUF_PBlock parks the producer's
    // thread, which needs a second thread to do the draining, so it needs a locked buffer.
    if (sbufIsPull(sb) || (sbufFlags(sb) & SBUF_Producer_Registered) ||
        ((flags & SBUF_PBlock) && !sb->locked)) {
        sbufUnlock(sb);

        if (pcleanup)
            pcleanup(ctx);

        cxerr = CX_InvalidArgument;
        return false;
    }

    sb->producerCleanup = pcleanup;
    sb->producerCtx     = ctx;
    sbufSetFlags(sb, SBUF_Producer_Registered | SBUF_Push | (flags & SBUF_PBlock));
    sb->refcount++;

    sbufUnlock(sb);
    return true;
}

_Use_decl_annotations_
void sbufPSetResume(StreamBuffer* sb, sbufResumeCB resume)
{
    sbufLock(sb);
    sb->producerResume = resume;
    sbufUnlock(sb);
}

_Use_decl_annotations_
size_t sbufPAvail(StreamBuffer* sb)
{
    if (sbufFlags(sb) & SBUF_Direct)
        return 0;   // there is no buffer

    sbufLock(sb);
    size_t ret = bufringWriteSpace(&sb->buf);
    sbufUnlock(sb);

    return ret;
}

_Use_decl_annotations_
size_t sbufCAvail(StreamBuffer* sb)
{
    sbufLock(sb);
    size_t ret = sbufCAvailLocked(sb);
    sbufUnlock(sb);

    return ret;
}

static bool sbufPWriteLocked(_Inout_ StreamBuffer* sb, _In_reads_bytes_(sz) const uint8* buf,
                             size_t sz)
{
    devAssert(sbufFlags(sb) & SBUF_Producer_Registered);
    devAssert(!(sbufFlags(sb) & SBUF_Producer_Done));
    devAssert(sbufFlags(sb) & SBUF_Consumer_Registered);

    if (sbufIsCFinished(sb)) {
        // nobody's listening
        return false;
    }

    // Flow control, but never on a re-entrant write: that is a pull producer writing from inside
    // its own callback, and the consumer that called it is waiting further up this same stack.
    if (sb->depth <= 1 && sbufHoldProducerLocked(sb) && !sbufWaitDrainLocked(sb))
        return false;

    if (sbufFlags(sb) & SBUF_Direct) {
        // for direct mode just call the callback
        sb->consumerPush(sb, buf, sz, sb->consumerCtx);
    } else {
        bufringWrite(&sb->buf, buf, sz);

        // Notify consumer there's data in the buffer, but only in push mode.
        // This function may also be used in pull mode if the callback doesn't have
        // enough buffer space to write what it wants.
        if (sbufIsPush(sb)) {
            sb->consumerNotify(sb, sbufCAvailLocked(sb), sb->consumerCtx);
        }
    }

    return true;
}

_Use_decl_annotations_
bool sbufPWrite(StreamBuffer* sb, const uint8* buf, size_t sz)
{
    if (sz == 0)
        return true;

    sbufLock(sb);
    bool ret = sbufPWriteLocked(sb, buf, sz);
    sbufUnlock(sb);

    return ret;
}

_Use_decl_annotations_
bool sbufPWriteStr(StreamBuffer* sb, strref str)
{
    bool ret = true;

    sbufLock(sb);
    foreach (string, it, str) {
        if (!sbufPWriteLocked(sb, it.bytes, it.len)) {
            ret = false;
            break;
        }
    }
    sbufUnlock(sb);

    return ret;
}

_Use_decl_annotations_
bool sbufPWriteLine(StreamBuffer* sb, strref str)
{
    bool ret = true;

    sbufLock(sb);
    foreach (string, it, str) {
        if (!sbufPWriteLocked(sb, it.bytes, it.len)) {
            ret = false;
            break;
        }
    }

    if (ret) {
#ifdef _PLATFORM_WIN
        ret = sbufPWriteLocked(sb, (const uint8*)"\r\n", 2);
#else
        ret = sbufPWriteLocked(sb, (const uint8*)"\n", 1);
#endif
    }
    sbufUnlock(sb);

    return ret;
}

_Use_decl_annotations_
bool sbufPWriteEOL(StreamBuffer* sb)
{
#ifdef _PLATFORM_WIN
    return sbufPWrite(sb, (const uint8*)"\r\n", 2);
#else
    return sbufPWrite(sb, (const uint8*)"\n", 1);
#endif
}

static bool sbufPFinishLocked(StreamBuffer* sb)
{
    if (sbufFlags(sb) & SBUF_Producer_Done)
        return false;

    sbufSetFlags(sb, SBUF_Producer_Done);

    // notify consumer about EOF
    if (!(sbufFlags(sb) & SBUF_Consumer_Done)) {
        if (sb->consumerNotify) {
            // notify once for any remaining data in buffer, then again for EOF
            size_t left = sbufCAvailLocked(sb);
            if (left > 0)
                sb->consumerNotify(sb, left, sb->consumerCtx);

            // check flag again in case they finished in the previous callback
            if (!(sbufFlags(sb) & SBUF_Consumer_Done))
                sb->consumerNotify(sb, 0, sb->consumerCtx);
        } else if (sb->consumerPush)
            sb->consumerPush(sb, NULL, 0, sb->consumerCtx);
    }

    // if buffer is in push mode, consumer has gotten all the callbacks they're going to get
    bool last = false;
    if (sbufIsPush(sb))
        last = sbufCFinishLocked(sb);

    return sbufDerefLocked(sb) || last;
}

_Use_decl_annotations_
void sbufPFinish(StreamBuffer* sb)
{
    sbufLock(sb);
    sbufUnlockAndRelease(sb, sbufPFinishLocked(sb));
}

_Use_decl_annotations_
bool sbufCRegisterPull(StreamBuffer* sb, sbufCleanupCB ccleanup, void* ctx)
{
    sbufLock(sb);

    // can only register once, if it's not already a push buffer
    if (sbufIsPush(sb) || (sbufFlags(sb) & SBUF_Consumer_Registered)) {
        sbufUnlock(sb);

        if (ccleanup)
            ccleanup(ctx);

        cxerr = CX_InvalidArgument;
        return false;
    }

    sb->consumerCleanup = ccleanup;
    sb->consumerCtx     = ctx;
    sbufSetFlags(sb, SBUF_Consumer_Registered | SBUF_Pull);
    sb->refcount++;

    sbufUnlock(sb);
    return true;
}

_Use_decl_annotations_
bool sbufCRegisterPush(StreamBuffer* sb, sbufNotifyCB cnotify, sbufCleanupCB ccleanup, void* ctx)
{
    sbufLock(sb);

    // can only register once, if it's not already a pull buffer, and is not set as a direct buffer
    if (sbufIsPull(sb) || (sbufFlags(sb) & SBUF_Consumer_Registered) || sb->targetsz == 0 ||
        !cnotify) {
        sbufUnlock(sb);

        if (ccleanup)
            ccleanup(ctx);

        cxerr = CX_InvalidArgument;
        return false;
    }

    sb->consumerNotify  = cnotify;
    sb->consumerCleanup = ccleanup;
    sb->consumerCtx     = ctx;
    sbufSetFlags(sb, SBUF_Consumer_Registered | SBUF_Push);
    sb->refcount++;

    sbufUnlock(sb);
    return true;
}

_Use_decl_annotations_
bool sbufCRegisterPushDirect(StreamBuffer* sb, sbufPushCB cpush, sbufCleanupCB ccleanup, void* ctx)
{
    sbufLock(sb);

    // can only register once, if it's not already a pull buffer, and is not set as a direct buffer
    if (sbufIsPull(sb) || (sbufFlags(sb) & SBUF_Consumer_Registered) || !cpush) {
        sbufUnlock(sb);

        if (ccleanup)
            ccleanup(ctx);

        cxerr = CX_InvalidArgument;
        return false;
    }

    sb->consumerPush    = cpush;
    sb->consumerCleanup = ccleanup;
    sb->consumerCtx     = ctx;
    sbufSetFlags(sb, SBUF_Consumer_Registered | SBUF_Push | SBUF_Direct);
    sb->refcount++;

    sbufUnlock(sb);
    return true;
}

typedef struct SbufRingFeedCtx {
    StreamBuffer* sb;
    size_t needed;
} SbufRingFeedCtx;

static size_t sbufFeedCB(uint8* buf, size_t maxbytes, void* _ctx)
{
    SbufRingFeedCtx* ctx = (SbufRingFeedCtx*)_ctx;
    size_t toread        = min(ctx->needed, maxbytes);
    size_t read          = ctx->sb->producerPull(ctx->sb, buf, toread, ctx->sb->producerCtx);
    ctx->needed -= read;
    return read;
}

static void feedBuffer(_Inout_ StreamBuffer* sb, size_t want)
{
    SbufRingFeedCtx ctx = { .sb = sb };
    ctx.needed          = want - sbufCAvailLocked(sb);
    bufringFeed(&sb->buf, sbufFeedCB, ctx.needed, &ctx);
}

static bool sbufCReadLocked(_Inout_ StreamBuffer* sb, _Out_writes_bytes_to_(sz, *bytesread) uint8* buf,
                            size_t sz, _Out_ size_t* bytesread)
{
    *bytesread = 0;

    if ((sbufFlags(sb) & SBUF_Direct) || sbufIsError(sb) || sz == 0)
        return false;   // can't pull in direct mode!

    if (sbufIsPull(sb)) {
        // loop until we have enough data to satisfy the request
        while (!sbufIsPFinished(sb) && sz > sbufCAvailLocked(sb)) {
            feedBuffer(sb, sz);
        }
        // cap sz at actual data available, which happens on EOF or error
        sz = min(sz, sbufCAvailLocked(sb));
    } else if (sz > sbufCAvailLocked(sb)) {
        // we don't have enough!
        return false;
    }

    *bytesread = bufringRead(&sb->buf, buf, sz);
    sbufReleaseProducerLocked(sb, false);

    return (sz > 0);
}

_Use_decl_annotations_
bool sbufCRead(StreamBuffer* sb, uint8* buf, size_t sz, size_t* bytesread)
{
    sbufLock(sb);
    bool ret = sbufCReadLocked(sb, buf, sz, bytesread);
    sbufUnlockAndResume(sb);

    return ret;
}

_Use_decl_annotations_
bool sbufCPeek(StreamBuffer* sb, uint8* buf, size_t off, size_t sz)
{
    if ((sbufFlags(sb) & SBUF_Direct) || sbufIsError(sb))
        return false;   // can't peek in direct mode!

    if (sz == 0)
        return true;

    sbufLock(sb);

    // never short-reads; fails outright if there isn't enough
    bool ret = (off + sz <= sbufCAvailLocked(sb));
    if (ret)
        bufringPeek(&sb->buf, buf, off, sz);

    sbufUnlock(sb);
    return ret;
}

_Use_decl_annotations_
bool sbufCFeed(StreamBuffer* sb, size_t minsz)
{
    if (!sbufIsPull(sb))
        return false;

    sbufLock(sb);

    // loop until we have enough data to satisfy the request
    while (!sbufIsPFinished(sb) && minsz > sbufCAvailLocked(sb)) {
        feedBuffer(sb, minsz);
    }

    bool ret = sbufCAvailLocked(sb) >= minsz;
    sbufUnlock(sb);

    return ret;
}

_Use_decl_annotations_
bool sbufCSkip(StreamBuffer* sb, size_t bytes)
{
    if ((sbufFlags(sb) & SBUF_Direct) || sbufIsError(sb))
        return false;   // can't seek in direct mode!

    if (bytes == 0)
        return true;

    sbufLock(sb);

    bool ret = (bytes <= sbufCAvailLocked(sb));
    if (ret) {
        bufringSkip(&sb->buf, bytes);
        sbufReleaseProducerLocked(sb, false);
    }

    sbufUnlockAndResume(sb);
    return ret;
}

typedef struct SbufRingReadCtx {
    StreamBuffer* sb;
    sbufSendCB func;
    size_t off;
} SbufRingReadCtx;

static bool sbufRingRead(const uint8* buf, size_t bytes, void* _ctx)
{
    SbufRingReadCtx* ctx = (SbufRingReadCtx*)_ctx;

    bool ret = ctx->func(ctx->sb, buf, ctx->off, bytes, ctx->sb->consumerCtx);
    ctx->off += bytes;
    return ret;
}

_Use_decl_annotations_
bool sbufCSend(StreamBuffer* sb, sbufSendCB func, size_t sz)
{
    if ((sbufFlags(sb) & SBUF_Direct) || sbufIsError(sb))
        return false;   // can't pull in direct mode!

    if (sz == 0)
        return true;

    sbufLock(sb);

    if (sbufIsPull(sb)) {
        // loop until we have enough data to satisfy the request
        while (!sbufIsPFinished(sb) && sz > sbufCAvailLocked(sb)) {
            feedBuffer(sb, sz);
        }
    }

    // cap sz at actual data available
    sz = min(sz, sbufCAvailLocked(sb));

    SbufRingReadCtx ctx = { .sb = sb, .func = func };
    bufringReadZC(&sb->buf, sz, sbufRingRead, &ctx);

    sbufReleaseProducerLocked(sb, false);

    sbufUnlockAndResume(sb);
    return true;
}

static bool sbufCFinishLocked(StreamBuffer* sb)
{
    if (sbufFlags(sb) & SBUF_Consumer_Done)
        return false;

    sbufSetFlags(sb, SBUF_Consumer_Done);

    // a producer parked at the watermark has nothing left to wait for
    sbufWakeProducerLocked(sb);

    // if buffer is in pull mode, producer has gotten all the callbacks they're going to get
    bool last = false;
    if (sbufIsPull(sb))
        last = sbufPFinishLocked(sb);

    return sbufDerefLocked(sb) || last;
}

_Use_decl_annotations_
void sbufCFinish(StreamBuffer* sb)
{
    sbufLock(sb);
    sbufUnlockAndRelease(sb, sbufCFinishLocked(sb));
}
