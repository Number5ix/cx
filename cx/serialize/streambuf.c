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

// An sbufSendCB runs in the middle of a ring walk that is still holding pointers into nodes it
// has not finished with, and the walk consumes what it read only after the last callback returns.
// Anything the callback does that touches the ring -- a write, a read, a skip, a flush -- either
// corrupts the walk or has its effect thrown away, quietly in both cases. The recursive lock lets
// the call through, so this is the only thing that catches it.
#define SBUF_WALK_MSG \
    "An sbufSendCB must not call any sbuf function on the buffer it was invoked from"

// Recursive per-buffer lock, active only when the buffer was created with SBUF_Locked. The
// recursion depth is tracked either way, since it is also what tells an entry point whether it is
// the outermost one and therefore the one that pays out deferred callbacks.
//
// sbufPullCB's contract lets a pullproducer call sbufPWrite() from inside its own callback when it has more data than the slice it
// was asked for, and that callback runs from feedBuffer() on a thread that is already holding the
// lock. cx's Mutex is not recursive, so ownership is tracked here instead.
static void sbufLock(_Inout_ StreamBuffer* sb)
{
    if (!sb->locked) {
        // an unlocked buffer only ever has the one thread, so any arrival here is a re-entry
        devAssertMsg(!sb->walking, SBUF_WALK_MSG);
        ++sb->depth;
        return;
    }

    intptr self = sbufSelf();

    if (atomicLoad(intptr, &sb->owner, Relaxed) == self) {
        devAssertMsg(!sb->walking, SBUF_WALK_MSG);
        ++sb->depth;
        return;
    }

    mutexAcquire(&sb->lock);
    atomicStore(intptr, &sb->owner, self, Relaxed);
    sb->depth = 1;
}

static void sbufUnlock(_Inout_ StreamBuffer* sb)
{
    devAssert(sb->depth > 0);
    if (--sb->depth > 0 || !sb->locked)
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

// How many of the outstanding references belong to registrations rather than to a real holder.
static int sbufRegCountLocked(_In_ StreamBuffer* sb)
{
    return (sb->producerPull ? 1 : 0) + ((sb->consumerNotify || sb->consumerPush) ? 1 : 0);
}

static bool sbufCMoreLocked(_In_ StreamBuffer* sb)
{
    if (sbufFlags(sb) & (SBUF_Closed | SBUF_Error))
        return false;

    // A push stream has no producer registration, so there is no empty slot to stop for.
    return !sbufIsPull(sb) || sb->producerPull != NULL;
}

// Unparks a producer waiting at the watermark or inside sbufPFlush() without regard to how full
// the buffer is. Used when the stream ends or fails, because both wait loops recheck for that
// every time they wake.
static void sbufWakeProducerLocked(_Inout_ StreamBuffer* sb)
{
    if (sb->locked) {
        cvarBroadcast(&sb->ready);
        cvarBroadcast(&sb->flushed);
    }
}

// Releases sbufPFlush() once the consumer has taken the last buffered byte.
static void sbufSignalFlushedLocked(_Inout_ StreamBuffer* sb)
{
    if (sb->locked && sbufCAvailLocked(sb) == 0)
        cvarBroadcast(&sb->flushed);
}

// Engages the hold once the buffer fills, and reports whether the producer is currently held.
//
// Push mode only: in pull mode the write is happening inside the producer's own callback, on the
// consumer's thread, so holding it there would stall the very consumer that has to drain it.
static bool sbufHoldProducerLocked(_Inout_ StreamBuffer* sb)
{
    uint32 f = sbufFlags(sb);

    if (sb->high == 0 || (f & SBUF_Pull) || (f & SBUF_Direct))
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

    // The resume callback is left to sbufUnlockAndPay(), once the lock is gone and this operation
    // has finished. A producer that writes from inside it has to arrive as a fresh top-level
    // write: run inline from here it would look re-entrant, and re-entrant writes skip the
    // watermark check on purpose.
    if (f & SBUF_PResumeOwed)
        sb->resumePending = true;
}

// Everything an operation can end up owing once it is finished with the buffer: the resume
// callback for a producer that was refused at the watermark, the cleanup callbacks of roles that
// unregistered, and the final destroy. None of it may run under the lock or with an outer frame
// still inside the buffer, so it is collected here and paid afterwards.
typedef struct SbufPayout {
    sbufResumeCB resume;
    void* resumeCtx;
    sbufCleanupCB pcleanup;
    void* pcleanupCtx;
    sbufCleanupCB ccleanup;
    void* ccleanupCtx;
    bool destroy;
} SbufPayout;

// Tail end of every entry point that can run a callback.
static void sbufUnlockAndPay(_Inout_ StreamBuffer* sb)
{
    SbufPayout pay = { 0 };

    // when nested, leave it for the outermost frame to pay out
    if (sb->depth == 1) {
        if (sb->resumePending) {
            sb->resumePending = false;
            pay.resume        = sb->producerResume;
            pay.resumeCtx     = sb->producerResumeCtx;
        }

        pay.pcleanup    = sb->pendingPCleanup;
        pay.pcleanupCtx = sb->pendingPCleanupCtx;
        pay.ccleanup    = sb->pendingCCleanup;
        pay.ccleanupCtx = sb->pendingCCleanupCtx;
        pay.destroy     = sb->destroyPending;

        sb->pendingPCleanup    = NULL;
        sb->pendingPCleanupCtx = NULL;
        sb->pendingCCleanup    = NULL;
        sb->pendingCCleanupCtx = NULL;
        sb->destroyPending     = false;
    }

    sbufUnlock(sb);

    // Resume first, since it may write, and the cleanups may free contexts it is about to use.
    if (pay.resume)
        pay.resume(sb, pay.resumeCtx);
    if (pay.pcleanup)
        pay.pcleanup(pay.pcleanupCtx);
    if (pay.ccleanup)
        pay.ccleanup(pay.ccleanupCtx);
    if (pay.destroy)
        sbufDestroy(sb);
}

// Returns true if the producer may go ahead, false if the write must be refused or the stream
// ended while the producer was parked.
static bool sbufWaitDrainLocked(_Inout_ StreamBuffer* sb, flags_t flags)
{
    // SBUF_Wait parks this thread, so the draining has to happen on a different one.
    devAssertMsg(!(flags & SBUF_Wait) || sb->locked,
                 "SBUF_Wait needs a stream buffer created with SBUF_Locked");

    if (!(flags & SBUF_Wait) || !sb->locked) {
        // Asynchronous producer: refuse the write now, resume callback when there is room again.
        sbufSetFlags(sb, SBUF_PResumeOwed);
        return false;
    }

    // only a top-level sbufPWrite() reaches this, so the wait can safely hand the mutex back
    devAssert(sb->depth == 1);

    intptr self = sbufSelf();
    while ((sbufFlags(sb) & SBUF_PHeld) && !(sbufFlags(sb) & (SBUF_Closed | SBUF_Error))) {
        // cvarWait releases the mutex, so ownership has to go with it
        atomicStore(intptr, &sb->owner, 0, Relaxed);
        sb->depth = 0;

        cvarWait(&sb->ready, &sb->lock);

        atomicStore(intptr, &sb->owner, self, Relaxed);
        sb->depth = 1;
    }

    return !(sbufFlags(sb) & (SBUF_Closed | SBUF_Error));
}

// Drops a reference. Returns true if it was the last one, meaning the caller must destroy the
// buffer once it is no longer holding the lock that lives inside it.
static bool sbufDerefLocked(_Inout_ StreamBuffer* sb)
{
    return --sb->refcount <= 0;
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
        cvarInit(&ret->ready);
        cvarInit(&ret->flushed);
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
    // whatever is still registered never got an unregister, so its cleanup runs here instead
    if (sb->consumerCleanup)
        sb->consumerCleanup(sb->consumerCtx);
    if (sb->producerCleanup)
        sb->producerCleanup(sb->producerCtx);

    bufringDestroy(&sb->buf);

    if (sb->locked) {
        cvarDestroy(&sb->ready);
        cvarDestroy(&sb->flushed);
        mutexDestroy(&sb->lock);
    }

    xaFree(sb);
}

_Use_decl_annotations_
StreamBuffer* sbufAcquire(StreamBuffer* sb)
{
    sbufLock(sb);
    sb->refcount++;
    sbufUnlock(sb);

    return sb;
}

_Use_decl_annotations_
void sbufRelease(StreamBuffer** sb)
{
    if (*sb) {
        StreamBuffer* b = *sb;
        *sb             = NULL;

        sbufLock(b);
        bool destroy = sbufDerefLocked(b);

        // A stream that still has registrations but no other holder has nobody left who could
        // unregister them, so the buffer can never be freed. Ending or failing the stream first is
        // what tells the registered side to let go.
        devAssertMsg(destroy || b->refcount > sbufRegCountLocked(b) ||
                         (sbufFlags(b) & (SBUF_Closed | SBUF_Error)),
                     "Stream buffer abandoned with a registration still attached");

        sbufUnlock(b);

        if (destroy) {
            // Dropping the last reference from inside a callback would free the buffer out from
            // under the code still walking it further up the stack.
            devAssert(b->depth == 0);
            sbufDestroy(b);
        }
    }
}

_Use_decl_annotations_
void sbufClose(StreamBuffer* sb)
{
    sbufLock(sb);

    if (!sbufIsClosed(sb)) {
        sbufSetFlags(sb, SBUF_Closed);

        // anything parked at the watermark or waiting on a flush has to come back and find out
        sbufWakeProducerLocked(sb);

        // Give the registered side its last callback. A notify consumer sees whatever is still
        // buffered first, then the sz == 0 that says nothing more is coming. Both sides are
        // expected to unregister themselves when they see the stream has ended.
        if (sb->consumerNotify) {
            size_t left = sbufCAvailLocked(sb);
            if (left > 0)
                sb->consumerNotify(sb, left, sb->consumerCtx);

            // check again in case the consumer unregistered in the previous callback
            if (sb->consumerNotify)
                sb->consumerNotify(sb, 0, sb->consumerCtx);
        } else if (sb->consumerPush) {
            sb->consumerPush(sb, NULL, 0, sb->consumerCtx);
        }

        if (sb->producerPull)
            sb->producerPull(sb, NULL, 0, sb->producerCtx);
    }

    sbufUnlockAndPay(sb);
}

_Use_decl_annotations_
void sbufError(StreamBuffer* sb)
{
    sbufLock(sb);

    sbufSetFlags(sb, SBUF_Error);

    // Nothing is called back from here. An error is usually reported by the registered side from
    // inside its own callback, and the driving side is the one that has to act on it -- which it
    // does on its next read or write, since both fail while the error stands. The one thing that
    // cannot wait is a producer parked on a drain that is no longer coming.
    sbufWakeProducerLocked(sb);

    sbufUnlock(sb);
}

_Use_decl_annotations_
void sbufClearError(StreamBuffer* sb)
{
    sbufLock(sb);
    sbufClearFlags(sb, SBUF_Error);
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

    sbufUnlockAndPay(sb);
}

_Use_decl_annotations_
bool sbufCMore(StreamBuffer* sb)
{
    sbufLock(sb);
    bool ret = sbufCMoreLocked(sb);
    sbufUnlock(sb);

    return ret;
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
bool sbufPAttached(StreamBuffer* sb)
{
    sbufLock(sb);
    bool ret = sb->producerPull != NULL;
    sbufUnlock(sb);

    return ret;
}

_Use_decl_annotations_
bool sbufCAttached(StreamBuffer* sb)
{
    sbufLock(sb);
    bool ret = sb->consumerNotify != NULL || sb->consumerPush != NULL;
    sbufUnlock(sb);

    return ret;
}

_Use_decl_annotations_
bool sbufPRegisterPull(StreamBuffer* sb, sbufPullCB ppull, sbufCleanupCB pcleanup, void* ctx)
{
    sbufLock(sb);

    // one producer at a time, on a stream that is still running and not already pushing
    if (!ppull || sb->producerPull || sbufIsPush(sb) || sbufIsClosed(sb)) {
        sbufUnlockAndPay(sb);

        if (pcleanup)
            pcleanup(ctx);

        cxerr = CX_InvalidArgument;
        return false;
    }

    sb->producerPull    = ppull;
    sb->producerCleanup = pcleanup;
    sb->producerCtx     = ctx;
    sbufSetFlags(sb, SBUF_Pull);
    sb->refcount++;

    sbufUnlockAndPay(sb);
    return true;
}

_Use_decl_annotations_
void sbufPUnregister(StreamBuffer* sb)
{
    sbufCleanupCB displaced = NULL;
    void* displacedCtx      = NULL;

    sbufLock(sb);

    if (sb->producerPull) {
        // The slot has to read as empty immediately, even when this is the producer's own callback
        // unregistering itself: the read loop that called it is one frame up and tests the slot on
        // every pass, so leaving it filled until the stack unwinds would call the exhausted
        // producer forever.
        sb->producerPull      = NULL;
        sb->producerResume    = NULL;
        sb->producerResumeCtx = NULL;

        // Only reachable if a replacement producer registered and left again without the stack
        // ever getting back out of the buffer, so the displaced context cannot be the one the
        // current callback is standing on and is safe to pay out as soon as the lock is gone.
        displaced    = sb->pendingPCleanup;
        displacedCtx = sb->pendingPCleanupCtx;

        // The cleanup may free the context the callback is standing on, so it waits for the stack
        // to unwind. Clearing the fields also keeps sbufDestroy() from running it a second time.
        sb->pendingPCleanup    = sb->producerCleanup;
        sb->pendingPCleanupCtx = sb->producerCtx;
        sb->producerCleanup    = NULL;
        sb->producerCtx        = NULL;

        if (sbufDerefLocked(sb))
            sb->destroyPending = true;
    }

    sbufUnlockAndPay(sb);

    if (displaced)
        displaced(displacedCtx);
}

_Use_decl_annotations_
void sbufPSetResume(StreamBuffer* sb, sbufResumeCB resume, void* ctx)
{
    sbufLock(sb);
    sb->producerResume    = resume;
    sb->producerResumeCtx = ctx;
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
                             size_t sz, flags_t flags)
{
    // A write into a stream that is over, or one that has failed and not been cleared, is refused
    // rather than asserted: the return value is how a producer finds out about either.
    if (sbufFlags(sb) & (SBUF_Closed | SBUF_Error))
        return false;

    // Flow control, but never on a re-entrant write: that is a pull producer writing from inside
    // its own callback, and the consumer that called it is waiting further up this same stack.
    if (sb->depth == 1 && sbufHoldProducerLocked(sb) && !sbufWaitDrainLocked(sb, flags))
        return false;

    if (sbufFlags(sb) & SBUF_Direct) {
        // Direct mode keeps no storage of its own, so with nobody attached there is nowhere for
        // the bytes to go and the write has to fail rather than lose them.
        if (!sb->consumerPush)
            return false;

        sb->consumerPush(sb, buf, sz, sb->consumerCtx);
    } else {
        bufringWrite(&sb->buf, buf, sz);

        // With no consumer attached the data just piles up, and whoever registers next is handed
        // all of it. In pull mode there is never a notify consumer: this is the producer writing
        // more than the slice it was asked for, from inside its own callback.
        if (sb->consumerNotify)
            sb->consumerNotify(sb, sbufCAvailLocked(sb), sb->consumerCtx);
    }

    return true;
}

_Use_decl_annotations_
bool _sbufPWrite(StreamBuffer* sb, const uint8* buf, size_t sz, flags_t flags)
{
    if (sz == 0)
        return true;

    sbufLock(sb);
    bool ret = sbufPWriteLocked(sb, buf, sz, flags);
    sbufUnlockAndPay(sb);

    return ret;
}

_Use_decl_annotations_
bool _sbufPWriteStr(StreamBuffer* sb, strref str, flags_t flags)
{
    bool ret = true;

    sbufLock(sb);
    foreach (string, it, str) {
        if (!sbufPWriteLocked(sb, it.bytes, it.len, flags)) {
            ret = false;
            break;
        }
    }
    sbufUnlockAndPay(sb);

    return ret;
}

_Use_decl_annotations_
bool _sbufPWriteLine(StreamBuffer* sb, strref str, flags_t flags)
{
    bool ret = true;

    sbufLock(sb);
    foreach (string, it, str) {
        if (!sbufPWriteLocked(sb, it.bytes, it.len, flags)) {
            ret = false;
            break;
        }
    }

    if (ret) {
#ifdef _PLATFORM_WIN
        ret = sbufPWriteLocked(sb, (const uint8*)"\r\n", 2, flags);
#else
        ret = sbufPWriteLocked(sb, (const uint8*)"\n", 1, flags);
#endif
    }
    sbufUnlockAndPay(sb);

    return ret;
}

_Use_decl_annotations_
bool _sbufPWriteEOL(StreamBuffer* sb, flags_t flags)
{
#ifdef _PLATFORM_WIN
    return _sbufPWrite(sb, (const uint8*)"\r\n", 2, flags);
#else
    return _sbufPWrite(sb, (const uint8*)"\n", 1, flags);
#endif
}

_Use_decl_annotations_
bool sbufPFlush(StreamBuffer* sb)
{
    // In pull mode the buffer only fills on demand and any write is happening on the consumer's
    // own stack, so there is nothing to catch up on and nobody to wait for.
    if (sbufIsPull(sb))
        return false;

    // Direct mode never buffers, so everything written has already been delivered.
    if (sbufFlags(sb) & SBUF_Direct)
        return !sbufIsError(sb);

    sbufLock(sb);

    devAssertMsg(sb->depth == 1,
                 "sbufPFlush() must not be called from inside a stream buffer callback");

    // Offer the consumer everything that is waiting; it may take all of it, some, or none.
    if (sb->consumerNotify && sbufCAvailLocked(sb) > 0)
        sb->consumerNotify(sb, sbufCAvailLocked(sb), sb->consumerCtx);

    // On a locked buffer the consumer drains on its own thread, so wait for it to catch up. An
    // unlocked buffer has nobody else to wait for and just reports what the notify achieved.
    if (sb->locked) {
        intptr self = sbufSelf();
        while (sbufCAvailLocked(sb) > 0 && !(sbufFlags(sb) & (SBUF_Closed | SBUF_Error))) {
            atomicStore(intptr, &sb->owner, 0, Relaxed);
            sb->depth = 0;

            cvarWait(&sb->flushed, &sb->lock);

            atomicStore(intptr, &sb->owner, self, Relaxed);
            sb->depth = 1;
        }
    }

    bool ret = sbufCAvailLocked(sb) == 0 && !sbufIsError(sb);
    sbufUnlockAndPay(sb);

    return ret;
}

_Use_decl_annotations_
bool sbufCRegisterPush(StreamBuffer* sb, sbufNotifyCB cnotify, sbufCleanupCB ccleanup, void* ctx)
{
    sbufLock(sb);

    // one consumer at a time, on a stream that is still running, not already pulling, and with a
    // buffer to read from
    if (!cnotify || sb->consumerNotify || sb->consumerPush || sbufIsPull(sb) ||
        (sbufFlags(sb) & SBUF_Direct) || sb->targetsz == 0 || sbufIsClosed(sb)) {
        sbufUnlockAndPay(sb);

        if (ccleanup)
            ccleanup(ctx);

        cxerr = CX_InvalidArgument;
        return false;
    }

    sb->consumerNotify  = cnotify;
    sb->consumerCleanup = ccleanup;
    sb->consumerCtx     = ctx;
    sbufSetFlags(sb, SBUF_Push);
    sb->refcount++;

    // An arriving consumer releases a producer that parked before there was one to drain for it.
    sbufWakeProducerLocked(sb);

    // Hand over anything the producer wrote while the slot was empty.
    size_t waiting = sbufCAvailLocked(sb);
    if (waiting > 0)
        cnotify(sb, waiting, ctx);

    sbufUnlockAndPay(sb);
    return true;
}

_Use_decl_annotations_
bool sbufCRegisterPushDirect(StreamBuffer* sb, sbufPushCB cpush, sbufCleanupCB ccleanup, void* ctx)
{
    sbufLock(sb);

    // one consumer at a time, on a stream that is still running and not already pulling
    if (!cpush || sb->consumerNotify || sb->consumerPush || sbufIsPull(sb) || sbufIsClosed(sb)) {
        sbufUnlockAndPay(sb);

        if (ccleanup)
            ccleanup(ctx);

        cxerr = CX_InvalidArgument;
        return false;
    }

    sb->consumerPush    = cpush;
    sb->consumerCleanup = ccleanup;
    sb->consumerCtx     = ctx;
    sbufSetFlags(sb, SBUF_Push | SBUF_Direct);
    sb->refcount++;

    sbufWakeProducerLocked(sb);

    sbufUnlockAndPay(sb);
    return true;
}

_Use_decl_annotations_
void sbufCUnregister(StreamBuffer* sb)
{
    sbufCleanupCB displaced = NULL;
    void* displacedCtx      = NULL;

    sbufLock(sb);

    if (sb->consumerNotify || sb->consumerPush) {
        // Empty the slot immediately, for the same reason the producer side does; see
        // sbufPUnregister().
        sb->consumerNotify = NULL;
        sb->consumerPush   = NULL;

        displaced    = sb->pendingCCleanup;
        displacedCtx = sb->pendingCCleanupCtx;

        sb->pendingCCleanup    = sb->consumerCleanup;
        sb->pendingCCleanupCtx = sb->consumerCtx;
        sb->consumerCleanup    = NULL;
        sb->consumerCtx        = NULL;

        if (sbufDerefLocked(sb))
            sb->destroyPending = true;
    }

    sbufUnlockAndPay(sb);

    if (displaced)
        displaced(displacedCtx);
}

typedef struct SbufRingFeedCtx {
    StreamBuffer* sb;
    size_t needed;
} SbufRingFeedCtx;

static size_t sbufFeedCB(uint8* buf, size_t maxbytes, void* _ctx)
{
    SbufRingFeedCtx* ctx = (SbufRingFeedCtx*)_ctx;

    // The producer may unregister from inside its own callback; returning 0 ends the feed cleanly
    // rather than calling through a slot that is now empty.
    if (!ctx->sb->producerPull)
        return 0;

    size_t toread = min(ctx->needed, maxbytes);
    size_t read   = ctx->sb->producerPull(ctx->sb, buf, toread, ctx->sb->producerCtx);
    ctx->needed -= read;
    return read;
}

static void feedBuffer(_Inout_ StreamBuffer* sb, size_t want)
{
    SbufRingFeedCtx ctx = { .sb = sb };
    ctx.needed          = want - sbufCAvailLocked(sb);
    bufringFeed(&sb->buf, sbufFeedCB, ctx.needed, &ctx);
}

static bool sbufCReadLocked(_Inout_ StreamBuffer* sb,
                            _Out_writes_bytes_to_(sz, *bytesread) uint8* buf, size_t sz,
                            _Out_ size_t* bytesread)
{
    *bytesread = 0;

    if ((sbufFlags(sb) & SBUF_Direct) || sbufIsError(sb) || sz == 0)
        return false;   // can't pull in direct mode!

    if (sbufIsPull(sb)) {
        // Loop until we have enough data to satisfy the request. It short-reads instead when the
        // stream ends, when it fails, or when the producer detaches.
        while (sbufCMoreLocked(sb) && sz > sbufCAvailLocked(sb)) {
            feedBuffer(sb, sz);
        }
        sz = min(sz, sbufCAvailLocked(sb));
    } else if (sz > sbufCAvailLocked(sb)) {
        // we don't have enough!
        return false;
    }

    *bytesread = bufringRead(&sb->buf, buf, sz);
    sbufReleaseProducerLocked(sb, false);
    sbufSignalFlushedLocked(sb);

    return (sz > 0);
}

_Use_decl_annotations_
bool sbufCRead(StreamBuffer* sb, uint8* buf, size_t sz, size_t* bytesread)
{
    sbufLock(sb);
    bool ret = sbufCReadLocked(sb, buf, sz, bytesread);
    sbufUnlockAndPay(sb);

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
    while (sbufCMoreLocked(sb) && minsz > sbufCAvailLocked(sb)) {
        feedBuffer(sb, minsz);
    }

    bool ret = sbufCAvailLocked(sb) >= minsz;
    sbufUnlockAndPay(sb);

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
        sbufSignalFlushedLocked(sb);
    }

    sbufUnlockAndPay(sb);
    return ret;
}

typedef struct SbufRingReadCtx {
    StreamBuffer* sb;
    sbufSendCB func;
    void* ctx;
    size_t off;
} SbufRingReadCtx;

static bool sbufRingRead(const uint8* buf, size_t bytes, void* _ctx)
{
    SbufRingReadCtx* ctx = (SbufRingReadCtx*)_ctx;

    bool ret = ctx->func(ctx->sb, buf, ctx->off, bytes, ctx->ctx);
    ctx->off += bytes;
    return ret;
}

_Use_decl_annotations_
bool sbufCSend(StreamBuffer* sb, sbufSendCB func, size_t sz, void* ctx)
{
    if ((sbufFlags(sb) & SBUF_Direct) || sbufIsError(sb))
        return false;   // can't pull in direct mode!

    if (sz == 0)
        return true;

    sbufLock(sb);

    if (sbufIsPull(sb)) {
        // loop until we have enough data to satisfy the request
        while (sbufCMoreLocked(sb) && sz > sbufCAvailLocked(sb)) {
            feedBuffer(sb, sz);
        }
    }

    // cap sz at actual data available
    sz = min(sz, sbufCAvailLocked(sb));

    SbufRingReadCtx rctx = { .sb = sb, .func = func, .ctx = ctx };
    sb->walking          = true;
    bufringReadZC(&sb->buf, sz, sbufRingRead, &rctx);
    sb->walking = false;

    sbufReleaseProducerLocked(sb, false);
    sbufSignalFlushedLocked(sb);

    sbufUnlockAndPay(sb);
    return true;
}
