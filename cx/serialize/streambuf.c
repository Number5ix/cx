#include "streambuf.h"
#include <cx/core/error.h>
#include <cx/meta/block.h>

static _meta_inline void sbLock(StreamBuffer *sb, int *rlevel)
{
    if ((sb->flags & SBUF_Thread_Safe) && (*rlevel == 0)) {
        mutexAcquire(&sb->lock);
    }
    (*rlevel)++;
}

static _meta_inline void sbUnlock(StreamBuffer *sb, int *rlevel)
{
    if ((sb->flags & SBUF_Thread_Safe) && (*rlevel == 1)) {
        mutexRelease(&sb->lock);
    }
    (*rlevel)--;
}

#define withSBLock(sb) blkWrap(sbLock(sb, &_streambuf_lock_recursion_level), sbUnlock(sb, &_streambuf_lock_recursion_level))

StreamBuffer *sbufCreate(size_t targetsz, bool threadsafe)
{
    StreamBuffer *ret = xaAlloc(sizeof(StreamBuffer), XA_Zero);

    if (threadsafe) {
        ret->flags |= SBUF_Thread_Safe;
        mutexInit(&ret->lock);
    }

    if (targetsz > 0) {
        // minimum is just to keep the math from blowing up, it's very low for unit tests
        ret->targetsz = max(targetsz, 8);
        ret->bufsz = ret->targetsz / 4;
        ret->buf = xaAlloc(ret->bufsz);
    } else {
        // targetsz == 0 is used only for direct mode,
        // go ahead and lock this buffer into push mode
        ret->flags |= SBUF_Push;
    }

    return ret;
}

static void sbufDestroy(StreamBuffer *sb)
{
    // this does NOT lock the mutex in threadsafe mode.
    // It can only be called if both producer and consumer have finished,
    // after which they are not allowed to touch it anymore.

    if (sb->consumerCleanup)
        sb->consumerCleanup(sb->consumerCtx);
    if (sb->producerCleanup)
        sb->producerCleanup(sb->producerCtx);

    xaFree(sb->buf);
    xaFree(sb->overflow);

    if (sb->flags & SBUF_Thread_Safe)
        mutexDestroy(&sb->lock);

    xaFree(sb);
}

static _meta_inline void checkDestroy(StreamBuffer *sb, int _streambuf_lock_recursion_level)
{
    if (_streambuf_lock_recursion_level == 0 &&
        (sb->flags & SBUF_Consumer_Done) &&
        (sb->flags & SBUF_Producer_Done))
        sbufDestroy(sb);
}

void _sbufError(StreamBuffer *sb, int _streambuf_lock_recursion_level)
{
    withSBLock(sb)
    {
        sb->flags |= SBUF_Error;

        if (!(sb->flags & SBUF_Consumer_Done)) {
            if (sb->consumerNotify)
                sb->consumerNotify(sb, 0, sb->consumerCtx, _streambuf_lock_recursion_level);
            if (sb->consumerPush)
                sb->consumerPush(sb, NULL, 0, sb->consumerCtx, _streambuf_lock_recursion_level);
        }

        if (!(sb->flags & SBUF_Producer_Done) && sb->producerPull)
            sb->producerPull(sb, NULL, 0, sb->producerCtx, _streambuf_lock_recursion_level);
    }
}

bool sbufPRegisterPull(StreamBuffer *sb, sbufPullCB ppull, sbufCleanupCB pcleanup, void *ctx)
{
    bool ret = true;
    int _streambuf_lock_recursion_level = 0;

    withSBLock(sb)
    {
        // can only register once, if it's not already a push buffer
        if (sbufIsPush(sb) ||
            (sb->flags & SBUF_Producer_Registered) ||
            !ppull) {
            cxerr = CX_InvalidArgument;
            ret = false;
            break;
        }

        sb->producerPull = ppull;
        sb->producerCleanup = pcleanup;
        sb->producerCtx = ctx;
        sb->flags |= SBUF_Producer_Registered | SBUF_Pull;
    }

    return ret;
}

bool sbufPRegisterPush(StreamBuffer *sb, sbufCleanupCB pcleanup, void *ctx)
{
    bool ret = true;
    int _streambuf_lock_recursion_level = 0;

    withSBLock(sb)
    {
        // can only register once, if it's not already a pull buffer
        if (sbufIsPull(sb) ||
            (sb->flags & SBUF_Producer_Registered)) {
            cxerr = CX_InvalidArgument;
            ret = false;
            break;
        }

        sb->producerCleanup = pcleanup;
        sb->producerCtx = ctx;
        sb->flags |= SBUF_Producer_Registered | SBUF_Push;
    }

    return ret;
}

static size_t sbufPAvailLocked(StreamBuffer *sb)
{
    if (sb->overflowsz > 0) {
        // we're writing to the overflow buffer
        return sb->overflowsz - sb->overflowtail;
    } else if (sb->head <= sb->tail) {
        // simple buffer
        // remaining space is the space left to the end of the buffer, plus any gap at the beginning,
        // less 1 because head == tail is empty not full
        return sb->bufsz - sb->tail + sb->head - 1;
    } else {
        // wraparound buffer
        // remaining space is the gap between the tail and the head,
        // less 1 because head == tail is empty not full
        return sb->head - sb->tail - 1;
    }
}

static size_t sbufCAvailLocked(StreamBuffer *sb)
{
    if (sb->head <= sb->tail) {
        // simple buffer
        // available data is anything between head and tail, plus whatever's in overflow
        return sb->tail - sb->head + sb->overflowtail;
    } else {
        // wraparound buffer
        // available data is data from head to end, then to tail, plus whatever's in overflow
        // less 1 because head == tail is empty not full
        return sb->bufsz - sb->head + sb->tail + sb->overflowtail;
    }
}

size_t _sbufPAvail(StreamBuffer *sb, int _streambuf_lock_recursion_level)
{
    size_t ret = 0;

    withSBLock(sb)
    {
        ret = sbufPAvailLocked(sb);
    }

    return ret;
}

static size_t adjustSize(size_t startsz, size_t needed, size_t targetsz)
{
    while (startsz < needed) {
        startsz += startsz >> 1;
    }

    // try not to go over target if possible
    if (startsz > targetsz && needed <= targetsz)
        startsz = targetsz;

    return startsz;
}

static void pushMakeBufSpace(StreamBuffer *sb, size_t sz)
{
    // check if there's space
    if (sb->overflowsz > 0) {
        // if we already have an overflow buffer, increase it if necessary
        if (sz > sb->overflowsz - sb->overflowtail) {
            sb->overflowsz = adjustSize(sb->overflowsz, sb->overflowtail + sz, sb->targetsz);
            sb->overflow = xaResize(sb->overflow, sb->overflowsz);
        }
    } else {
        // no overflow, check regular buffer size
        if (sbufPAvailLocked(sb) < sz) {
            // not enough room, make some
            if (sb->head == sb->tail) {
                // buffer is empty, just reallocate it
                xaFree(sb->buf);
                sb->bufsz = adjustSize(sb->bufsz, sz, sb->targetsz);
                sb->buf = xaAlloc(sb->bufsz);
            } else {
                // there's data in the buffer, need to go to overflow
                // grow buffer if below target since it will replace the current buffer
                sb->overflowsz = adjustSize(sb->bufsz + (sb->bufsz >> 1), sz, sb->targetsz);
                sb->overflowtail = 0;
                sb->overflow = xaAlloc(sb->overflowsz);
            }
        }
    }
}

bool _sbufPWrite(StreamBuffer *sb, const char *buf, size_t sz, int _streambuf_lock_recursion_level)
{
    if (sz == 0)
        return true;

    bool ret = true;
    withSBLock(sb)
    {
        devAssert(sbufIsPush(sb));
        devAssert(sb->flags & SBUF_Producer_Registered);
        devAssert(!(sb->flags & SBUF_Producer_Done));
        devAssert(sb->flags & SBUF_Consumer_Registered);

        if (sbufIsCFinished(sb)) {
            // nobody's listening
            ret = false;
            break;
        }

        if (sb->flags & SBUF_Direct) {
            // for direct mode just call the callback
            sb->consumerPush(sb, buf, sz, sb->consumerCtx, _streambuf_lock_recursion_level);
        } else {
            // normal (notify) mode
            pushMakeBufSpace(sb, sz);

            if (sb->overflowsz == 0) {
                // writing to normal buffer
                if (sb->tail + sz < sb->bufsz) {
                    // simple write
                    memcpy(sb->buf + sb->tail, buf, sz);
                    sb->tail = (sb->tail + sz) % sb->bufsz;     // catch edge case where we end right at bufsz
                } else {
                    // wraparound write
                    size_t segment = sb->bufsz - sb->tail;
                    memcpy(sb->buf + sb->tail, buf, segment);
                    memcpy(sb->buf, buf + segment, sz - segment);
                    sb->tail = (sz - segment) % sb->bufsz;
                }
            } else {
                // writing to overflow buffer
                devAssert(sb->overflowtail + sz <= sb->overflowsz);
                memcpy(sb->overflow + sb->overflowtail, buf, sz);
                sb->overflowtail += sz;
            }

            // notify consumer there's data in the buffer
            sb->consumerNotify(sb, sbufCAvailLocked(sb), sb->consumerCtx, _streambuf_lock_recursion_level);
        }
    }

    return ret;
}

void _sbufPFinish(StreamBuffer *sb, int _streambuf_lock_recursion_level)
{
    withSBLock(sb)
    {
        sb->flags |= SBUF_Producer_Done;

        // notify consumer about EOF
        if (!(sb->flags & SBUF_Consumer_Done)) {
            if (sb->consumerNotify) {
                // notify once for any remaining data in buffer, then again for EOF
                size_t left = sbufCAvailLocked(sb);
                if (left > 0)
                    sb->consumerNotify(sb, left, sb->consumerCtx, _streambuf_lock_recursion_level);

                // check flag again in case they finished in the previous callback
                if (!(sb->flags & SBUF_Consumer_Done))
                    sb->consumerNotify(sb, 0, sb->consumerCtx, _streambuf_lock_recursion_level);
            } else if (sb->consumerPush)
                sb->consumerPush(sb, NULL, 0, sb->consumerCtx, _streambuf_lock_recursion_level);
        }
    }

    checkDestroy(sb, _streambuf_lock_recursion_level);
}

bool sbufCRegisterPull(StreamBuffer *sb, sbufCleanupCB ccleanup, void *ctx)
{
    bool ret = true;
    int _streambuf_lock_recursion_level = 0;

    withSBLock(sb)
    {
        // can only register once, if it's not already a push buffer
        if (sbufIsPush(sb) ||
            (sb->flags & SBUF_Consumer_Registered)) {
            cxerr = CX_InvalidArgument;
            ret = false;
            break;
        }

        sb->consumerCleanup = ccleanup;
        sb->consumerCtx = ctx;
        sb->flags |= SBUF_Consumer_Registered | SBUF_Pull;
    }

    return ret;
}

bool sbufCRegisterPush(StreamBuffer *sb, sbufNotifyCB cnotify, sbufCleanupCB ccleanup, void *ctx)
{
    bool ret = true;
    int _streambuf_lock_recursion_level = 0;

    withSBLock(sb)
    {
        // can only register once, if it's not already a pull buffer, and is not set as a direct buffer
        if (sbufIsPull(sb) ||
            (sb->flags & SBUF_Consumer_Registered) ||
            sb->bufsz == 0 ||
            !cnotify) {
            cxerr = CX_InvalidArgument;
ret = false;
break;
        }

        sb->consumerNotify = cnotify;
        sb->consumerCleanup = ccleanup;
        sb->consumerCtx = ctx;
        sb->flags |= SBUF_Consumer_Registered | SBUF_Push;
    }

    return ret;
}

bool sbufCRegisterPushDirect(StreamBuffer *sb, sbufPushCB cpush, sbufCleanupCB ccleanup, void *ctx)
{
    bool ret = true;
    int _streambuf_lock_recursion_level = 0;

    withSBLock(sb)
    {
        // can only register once, if it's not already a pull buffer, and is not set as a direct buffer
        if (sbufIsPull(sb) ||
            (sb->flags & SBUF_Consumer_Registered) ||
            !cpush) {
            cxerr = CX_InvalidArgument;
            ret = false;
            break;
        }

        sb->consumerPush = cpush;
        sb->consumerCleanup = ccleanup;
        sb->consumerCtx = ctx;
        sb->flags |= SBUF_Consumer_Registered | SBUF_Push | SBUF_Direct;
    }

    return ret;
}

size_t _sbufCAvail(StreamBuffer *sb, int _streambuf_lock_recursion_level)
{
    size_t ret = 0;

    withSBLock(sb)
    {
        ret = sbufCAvailLocked(sb);
    }

    return ret;
}

static inline void _readWriteBuf(StreamBuffer *sb, char *out, size_t p, const char *in, size_t sz, bool movehead, sbufPushCB pushcb, int _streambuf_lock_recursion_level)
{
    if (out) {
        memcpy(out + p, in, sz);
        if (movehead)
            sb->head = (sb->head + sz) % sb->bufsz;
    } else if (pushcb) {
        if (pushcb(sb, in, sz, sb->consumerCtx, _streambuf_lock_recursion_level))
            sb->head = (sb->head + sz) % sb->bufsz;
    }
}
#define readWriteBuf(out, p, in, sz) _readWriteBuf(sb, out, p, in, sz, movehead, pushcb, _streambuf_lock_recursion_level)

// caller must verify there's enough data in the buffer first!!!!
static void readCommon(StreamBuffer *sb, char *buf, size_t sz, bool movehead, sbufPushCB pushcb, int _streambuf_lock_recursion_level)
{
    size_t count;
    size_t p = 0;

    if (sb->head <= sb->tail) {
        count = min(sb->tail - sb->head, sz);
        readWriteBuf(buf, p, sb->buf + sb->head, count);
        sz -= count;
        p += count;
    } else {
        // wraparound buffer
        count = min(sb->bufsz - sb->head, sz);
        readWriteBuf(buf, p, sb->buf + sb->head, count);
        sz -= count;
        p += count;

        count = min(sb->tail, sz);
        if (count > 0) {
            readWriteBuf(buf, p, sb->buf, count);
            sz -= count;
            p += count;
        }
    }

    // get the rest from overflow
    if (sb->overflowsz > 0) {
        if (sz > 0) {
            if (movehead)
                devAssert(sb->head == sb->tail);

            count = min(sb->overflowsz, sz);
            if (buf) {
                memcpy(buf + p, sb->overflow, count);
                // don't advance the head, that happens when the buffer swaps
            } else if (pushcb) {
                // if push callback returns false, it doesn't want to consume the overflow data, so set
                // count to 0. If the buffer rotation happens (because all non-overflow data was consumed),
                // this ensures that the new head points to the start of the overflow.
                if (!pushcb(sb, sb->overflow, count, sb->consumerCtx, _streambuf_lock_recursion_level))
                    count = 0;
            }
        } else {
            count = 0;
        }

        if (sb->head == sb->tail) {
            // overflow buffer becomes main buffer
            xaFree(sb->buf);
            sb->buf = sb->overflow;
            sb->bufsz = sb->overflowsz;
            sb->head = count % sb->bufsz;
            sb->tail = sb->overflowtail % sb->bufsz;

            sb->overflow = NULL;
            sb->overflowsz = 0;
            sb->overflowtail = 0;
        }
        sz -= count;
        p += count;
    }

    devAssert(sz == 0);
}

static void feedBuffer(StreamBuffer *sb, size_t want, int _streambuf_lock_recursion_level)
{
    size_t needed = want - sbufCAvailLocked(sb);
    size_t count;
    if (sb->head <= sb->tail && sb->tail < sb->bufsz) {
        // writing at the end of a buffer
        count = sb->producerPull(sb, sb->buf + sb->tail,
                                 sb->bufsz - sb->tail - (sb->head == 0 ? 1 : 0),    // fill as much of the buffer as we can
                                 sb->producerCtx, _streambuf_lock_recursion_level);
        sb->tail = (sb->tail + count) % sb->bufsz;
        return;             // ensure we only ever do one thing per cycle
    } else if (sb->head > sb->tail && sb->tail < sb->head - 1) {
        // writing to the start of the buffer
        count = sb->producerPull(sb, sb->buf + sb->tail,
                                 sb->head - sb->tail - 1,       // fill as much of the buffer as we can
                                 sb->producerCtx, _streambuf_lock_recursion_level);
        sb->tail = (sb->tail + count) % sb->bufsz;
        return;
    } else {
        // off to overflow!
        if (sb->overflowsz < needed) {
            if (sb->overflowsz == 0) {
                sb->overflowsz = adjustSize(sb->bufsz + (sb->bufsz >> 1), needed, sb->targetsz);
                sb->overflowtail = 0;
                sb->overflow = xaAlloc(sb->overflowsz);
            } else {
                sb->overflowsz = adjustSize(sb->overflowsz, sb->overflowtail + needed, sb->targetsz);
                sb->overflow = xaResize(sb->overflow, sb->overflowsz);
            }
        }
        count = sb->producerPull(sb, sb->overflow + sb->overflowtail,
                                 sb->overflowsz - sb->overflowtail, // fill as much of the buffer as we can
                                 sb->producerCtx, _streambuf_lock_recursion_level);
        sb->overflowtail += count;
    }
}

size_t _sbufCRead(StreamBuffer *sb, char *buf, size_t sz,int _streambuf_lock_recursion_level)
{
    if ((sb->flags & SBUF_Direct) || sbufIsError(sb))
        return 0;                   // can't pull in direct mode!

    if (sz == 0)
        return 0;

    size_t ret = 0;
    withSBLock(sb)
    {
        if (sbufIsPull(sb)) {
            // loop until we have enough data to satisfy the request
            while (!sbufIsPFinished(sb) && sz > sbufCAvailLocked(sb)) {
                feedBuffer(sb, sz, _streambuf_lock_recursion_level);
            }
            // cap sz at actual data available, which happens on EOF or error
            sz = min(sz, sbufCAvailLocked(sb));
        } else if (sz > sbufCAvailLocked(sb)) {
            // we don't have enough!
            ret = 0;
            break;
        }

        readCommon(sb, buf, sz, true, NULL, _streambuf_lock_recursion_level);
        ret = sz;
    }

    return ret;
}

bool _sbufCPeek(StreamBuffer *sb, char *buf, size_t sz, int _streambuf_lock_recursion_level)
{
    if ((sb->flags & SBUF_Direct) || sbufIsError(sb))
        return false;               // can't peek in direct mode!

    if (sz == 0)
        return true;

    bool ret = true;
    withSBLock(sb)
    {
        if (sz > sbufCAvailLocked(sb)) {
            // we don't have enough!
            ret = false;
            break;
        }

        readCommon(sb, buf, sz, false, NULL, _streambuf_lock_recursion_level);
    }

    return ret;
}

bool _sbufCSkip(StreamBuffer *sb, size_t bytes, int _streambuf_lock_recursion_level)
{
    if ((sb->flags & SBUF_Direct) || sbufIsError(sb))
        return false;               // can't seek in direct mode!

    if (bytes == 0)
        return true;

    bool ret = true;
    size_t count;
    withSBLock(sb)
    {
        if (bytes > sbufCAvailLocked(sb)) {
            // we don't have enough!
            ret = false;
            break;
        }

        // simplified version of readCommon that just advances the head
        if (sb->head <= sb->tail) {
            count = min(sb->tail - sb->head, bytes);
            sb->head += count;
            bytes -= count;
        } else {
            // wraparound buffer
            count = min(sb->bufsz - sb->head, bytes);
            sb->head = (sb->head + count) % sb->bufsz;
            bytes -= count;

            count = min(sb->tail, bytes);
            if (count > 0) {
                sb->head = count % sb->bufsz;
                bytes -= count;
            }
        }

        // get the rest from overflow
        if (sb->overflowsz > 0 && bytes > 0) {
            devAssert(sb->head == sb->tail);

            count = min(sb->overflowsz, bytes);

            // overflow buffer becomes main buffer
            xaFree(sb->buf);
            sb->buf = sb->overflow;
            sb->bufsz = sb->overflowsz;
            sb->head = count % sb->bufsz;
            sb->tail = sb->overflowtail % sb->bufsz;

            sb->overflow = NULL;
            sb->overflowsz = 0;
            sb->overflowtail = 0;
            bytes -= count;
        }

        devAssert(bytes == 0);
    }

    return ret;
}

bool _sbufCSend(StreamBuffer *sb, sbufPushCB func, size_t sz, int _streambuf_lock_recursion_level)
{
    if ((sb->flags & SBUF_Direct) || sbufIsError(sb))
        return false;                   // can't pull in direct mode!

    if (sz == 0)
        return true;

    bool ret = true;
    withSBLock(sb)
    {
        if (sbufIsPull(sb)) {
            // loop until we have enough data to satisfy the request
            while (!sbufIsPFinished(sb) && sz > sbufCAvailLocked(sb)) {
                feedBuffer(sb, sz, _streambuf_lock_recursion_level);
            }
        }

        // cap sz at actual data available
        sz = min(sz, sbufCAvailLocked(sb));

        readCommon(sb, NULL, sz, true, func, _streambuf_lock_recursion_level);
    }

    return ret;
}

void _sbufCFinish(StreamBuffer *sb, int _streambuf_lock_recursion_level)
{
    withSBLock(sb)
    {
        sb->flags |= SBUF_Consumer_Done;
    }

    checkDestroy(sb, _streambuf_lock_recursion_level);
}
