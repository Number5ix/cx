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
// the call through, so this is the only thing that catches it. sbufError() is the exception: it
// touches flags and waiters only, and reporting a failed send from inside the callback is the
// whole point of the return value being all-or-nothing.
#define SBUF_WALK_MSG \
    "An sbufSendCB must not call any sbuf function except sbufError() on the buffer it was " \
    "invoked from"

// Recursive per-buffer lock, active only when the buffer was created with SBUF_Locked. The
// recursion depth is tracked either way, since it is also what tells an entry point whether it is
// the outermost one and therefore the one that pays out deferred callbacks.
//
// sbufPullCB's contract lets a pullproducer call sbufPWrite() from inside its own callback when it has more data than the slice it
// was asked for, and that callback runs from feedBuffer() on a thread that is already holding the
// lock. cx's Mutex is not recursive, so ownership is tracked here instead.
// ringsafe marks the entry points that never touch the ring and so stay legal from inside a send
// callback.
static void sbufLockEx(_Inout_ StreamBuffer* sb, bool ringsafe)
{
    if (!sb->locked) {
        // an unlocked buffer only ever has the one thread, so any arrival here is a re-entry
        devAssertMsg(ringsafe || !sb->walking, SBUF_WALK_MSG);
        ++sb->depth;
        return;
    }

    intptr self = sbufSelf();

    if (atomicLoad(intptr, &sb->owner, Relaxed) == self) {
        devAssertMsg(ringsafe || !sb->walking, SBUF_WALK_MSG);
        ++sb->depth;
        return;
    }

    mutexAcquire(&sb->lock);
    atomicStore(intptr, &sb->owner, self, Relaxed);
    sb->depth = 1;
}

static void sbufLock(_Inout_ StreamBuffer* sb)
{
    sbufLockEx(sb, false);
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

// The resume closure is the one callback that runs after the lock is released, so on a locked
// buffer another thread can replace it or unregister the producer while it is still running, and
// more than one draining thread can be running it at once. The closure therefore lives in a holder
// that counts the calls in progress: taking it off the buffer while any are running only marks it
// detached, and the last of those calls to return is what destroys it.
typedef struct SbufResume {
    closure cls;
    int calls;       // calls currently running outside the lock; guarded by the buffer's lock
    bool detached;   // no longer on the buffer; destroy once calls reaches 0
} SbufResume;

// Takes the resume closure off the buffer. Returns the holder if nothing is running it, for the
// caller to free once the lock is gone; otherwise the call still running frees it.
static SbufResume* sbufDetachResumeLocked(_Inout_ StreamBuffer* sb)
{
    SbufResume* r      = sb->producerResume;
    sb->producerResume = NULL;

    if (r && r->calls > 0) {
        r->detached = true;
        return NULL;
    }
    return r;
}

static void sbufResumeFree(_In_opt_ SbufResume* r)
{
    if (!r)
        return;

    closureDestroy(&r->cls);
    xaFree(r);
}

// Everything an operation can end up owing once it is finished with the buffer: the resume
// callback for a producer that was refused at the watermark, the closures of roles that
// unregistered, and the final destroy. None of it may run under the lock or with an outer frame
// still inside the buffer, so it is collected here and paid afterwards.
typedef struct SbufPayout {
    SbufResume* resume;
    closure pdead;
    closure cdead;
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
            if (pay.resume)
                pay.resume->calls++;
        }

        pay.pdead   = sb->pendingP;
        pay.cdead   = sb->pendingC;
        pay.destroy = sb->destroyPending;

        sb->pendingP       = NULL;
        sb->pendingC       = NULL;
        sb->destroyPending = false;
    }

    sbufUnlock(sb);

    // Resume first, since it may write, and destroying the other closures may free state it is
    // about to use.
    if (pay.resume) {
        closureCallAs(sbufResumeCB, pay.resume->cls, sb);

        sbufLock(sb);
        bool dead = --pay.resume->calls == 0 && pay.resume->detached;
        sbufUnlock(sb);

        if (dead)
            sbufResumeFree(pay.resume);
    }

    closureDestroy(&pay.pdead);
    closureDestroy(&pay.cdead);

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
    // whatever is still registered never got an unregister, so its closure is destroyed here instead
    closureDestroy(&sb->consumerNotify);
    closureDestroy(&sb->consumerPush);
    closureDestroy(&sb->producerPull);
    closureDestroy(&sb->pendingC);
    closureDestroy(&sb->pendingP);

    // Nothing can be running the resume closure: a call in progress is inside an entry point whose
    // caller still holds a reference.
    sbufResumeFree(sbufDetachResumeLocked(sb));

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
        // unregister them, so the buffer can never be freed. Closing detaches whatever is still
        // registered; failing the stream leaves that to the driving side.
        devAssertMsg(destroy || b->refcount > sbufRegCountLocked(b) ||
                         (sbufFlags(b) & SBUF_Error),
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
    if (!sb)
        return;

    sbufLock(sb);

    if (!sbufIsClosed(sb)) {
        sbufSetFlags(sb, SBUF_Closed);

        // anything parked at the watermark or waiting on a flush has to come back and find out
        sbufWakeProducerLocked(sb);

        // Give the registered side its last callback. A notify consumer sees whatever is still
        // buffered first, then the sz == 0 that says nothing more is coming.
        if (sb->consumerNotify) {
            size_t left = sbufCAvailLocked(sb);
            if (left > 0)
                closureCallAs(sbufNotifyCB, sb->consumerNotify, sb, left);

            // check again in case the consumer unregistered in the previous callback
            if (sb->consumerNotify)
                closureCallAs(sbufNotifyCB, sb->consumerNotify, sb, 0);
        } else if (sb->consumerPush) {
            closureCallAs(sbufPushCB, sb->consumerPush, sb, NULL, 0);
        }

        if (sb->producerPull)
            closureCallAs(sbufPullCB, sb->producerPull, sb, NULL, 0);

        // Nothing can reach a registered side again once the stream is over: writes are refused
        // and a pull read stops at sbufCMore(), so a slot the final callback left filled would
        // never hear from the buffer again while its reference kept the buffer alive forever.
        // Whatever is still attached is therefore detached here. These do nothing for the usual
        // case where the callback above already unregistered.
        sbufCUnregister(sb);
        sbufPUnregister(sb);

        // The registration references just given back cannot be the ones keeping the buffer
        // alive, because this call is still using it.
        devAssertMsg(!sb->destroyPending,
                     "Stream buffer closed by a caller that holds no reference to it");
    }

    sbufUnlockAndPay(sb);
}

_Use_decl_annotations_
void sbufFinish(StreamBuffer** sb)
{
    if (!*sb)
        return;

    sbufClose(*sb);
    sbufRelease(sb);
}

_Use_decl_annotations_
void sbufError(StreamBuffer* sb)
{
    // Legal from inside an sbufSendCB, which is where a sink that failed mid-send reports it.
    sbufLockEx(sb, true);

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
bool sbufPRegisterPull(StreamBuffer* sb, closure ppull)
{
    sbufLock(sb);

    // one producer at a time, on a stream that is still running and not already pushing
    if (!ppull || sb->producerPull || sbufIsPush(sb) || sbufIsClosed(sb)) {
        sbufUnlockAndPay(sb);

        closureDestroy(&ppull);

        cxerr = CX_InvalidArgument;
        return false;
    }

    sb->producerPull = ppull;
    sbufSetFlags(sb, SBUF_Pull);
    sb->refcount++;

    sbufUnlockAndPay(sb);
    return true;
}

_Use_decl_annotations_
void sbufPUnregister(StreamBuffer* sb)
{
    closure displaced      = NULL;
    SbufResume* resumeDead = NULL;

    sbufLock(sb);

    if (sb->producerPull) {
        // The slot has to read as empty immediately, even when this is the producer's own callback
        // unregistering itself: the read loop that called it is one frame up and tests the slot on
        // every pass, so leaving it filled until the stack unwinds would call the exhausted
        // producer forever.
        closure pull     = sb->producerPull;
        sb->producerPull = NULL;
        resumeDead       = sbufDetachResumeLocked(sb);

        // Only reachable if a replacement producer registered and left again without the stack
        // ever getting back out of the buffer, so the displaced closure cannot be the one the
        // current callback is standing on and is safe to destroy as soon as the lock is gone.
        displaced = sb->pendingP;

        // Destroying the closure may free state the callback is standing on, so it waits for the
        // stack to unwind. Moving it off the slot also keeps sbufDestroy() from destroying it twice.
        sb->pendingP = pull;

        if (sbufDerefLocked(sb))
            sb->destroyPending = true;
    }

    sbufUnlockAndPay(sb);

    closureDestroy(&displaced);
    sbufResumeFree(resumeDead);
}

_Use_decl_annotations_
void sbufPSetResume(StreamBuffer* sb, closure resume)
{
    SbufResume* r = NULL;
    if (resume) {
        r      = xaAllocStruct(SbufResume, XA_Zero);
        r->cls = resume;
    }

    sbufLock(sb);
    SbufResume* old    = sbufDetachResumeLocked(sb);
    sb->producerResume = r;
    sbufUnlock(sb);

    sbufResumeFree(old);
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

        closureCallAs(sbufPushCB, sb->consumerPush, sb, buf, sz);
    } else {
        bufringWrite(&sb->buf, buf, sz);

        // With no consumer attached the data just piles up, and whoever registers next is handed
        // all of it. In pull mode there is never a notify consumer: this is the producer writing
        // more than the slice it was asked for, from inside its own callback.
        if (sb->consumerNotify)
            closureCallAs(sbufNotifyCB, sb->consumerNotify, sb, sbufCAvailLocked(sb));
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
        closureCallAs(sbufNotifyCB, sb->consumerNotify, sb, sbufCAvailLocked(sb));

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
bool sbufCRegisterPush(StreamBuffer* sb, closure cnotify)
{
    sbufLock(sb);

    // one consumer at a time, on a stream that is still running, not already pulling, and with a
    // buffer to read from
    if (!cnotify || sb->consumerNotify || sb->consumerPush || sbufIsPull(sb) ||
        (sbufFlags(sb) & SBUF_Direct) || sb->targetsz == 0 || sbufIsClosed(sb)) {
        sbufUnlockAndPay(sb);

        closureDestroy(&cnotify);

        cxerr = CX_InvalidArgument;
        return false;
    }

    sb->consumerNotify = cnotify;
    sbufSetFlags(sb, SBUF_Push);
    sb->refcount++;

    // An arriving consumer releases a producer that parked before there was one to drain for it.
    sbufWakeProducerLocked(sb);

    // Hand over anything the producer wrote while the slot was empty.
    size_t waiting = sbufCAvailLocked(sb);
    if (waiting > 0)
        closureCallAs(sbufNotifyCB, cnotify, sb, waiting);

    sbufUnlockAndPay(sb);
    return true;
}

_Use_decl_annotations_
bool sbufCRegisterPushDirect(StreamBuffer* sb, closure cpush)
{
    sbufLock(sb);

    // one consumer at a time, on a stream that is still running and not already pulling
    if (!cpush || sb->consumerNotify || sb->consumerPush || sbufIsPull(sb) || sbufIsClosed(sb)) {
        sbufUnlockAndPay(sb);

        closureDestroy(&cpush);

        cxerr = CX_InvalidArgument;
        return false;
    }

    sb->consumerPush = cpush;
    sbufSetFlags(sb, SBUF_Push | SBUF_Direct);
    sb->refcount++;

    sbufWakeProducerLocked(sb);

    sbufUnlockAndPay(sb);
    return true;
}

_Use_decl_annotations_
void sbufCUnregister(StreamBuffer* sb)
{
    closure displaced = NULL;

    sbufLock(sb);

    if (sb->consumerNotify || sb->consumerPush) {
        // Empty the slot immediately, for the same reason the producer side does; see
        // sbufPUnregister(). Only one of the two is ever set.
        closure cons       = sb->consumerNotify ? sb->consumerNotify : sb->consumerPush;
        sb->consumerNotify = NULL;
        sb->consumerPush   = NULL;

        displaced    = sb->pendingC;
        sb->pendingC = cons;

        if (sbufDerefLocked(sb))
            sb->destroyPending = true;
    }

    sbufUnlockAndPay(sb);

    closureDestroy(&displaced);
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
    size_t read   = closureCallAs(sbufPullCB, ctx->sb->producerPull, ctx->sb, buf, toread);
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
