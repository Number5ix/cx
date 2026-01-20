#include "streambuf.h"
#include <cx/container/foreach.h>
#include <cx/debug/error.h>
#include <cx/meta/block.h>
#include <cx/string/striter.h>
#include <cx/utils/compare.h>

static void sbufPFinishInternal(_Inout_ StreamBuffer *sb);
static void sbufCFinishInternal(_Inout_ StreamBuffer *sb);

_Use_decl_annotations_
StreamBuffer *sbufCreate(size_t targetsz)
{
    StreamBuffer *ret = xaAlloc(sizeof(StreamBuffer), XA_Zero);

    ret->refcount = 1;

    if (targetsz > 0) {
        bufringInit(&ret->buf, targetsz);
        ret->targetsz = targetsz;
    } else {
        // targetsz == 0 is used only for direct mode,
        // go ahead and lock this buffer into push mode
        ret->flags |= SBUF_Push;
    }

    return ret;
}

static void sbufDestroy(_Pre_valid_ _Post_invalid_ StreamBuffer *sb)
{
    if (sb->consumerCleanup)
        sb->consumerCleanup(sb->consumerCtx);
    if (sb->producerCleanup)
        sb->producerCleanup(sb->producerCtx);

    bufringDestroy(&sb->buf);
    xaFree(sb);
}

_Use_decl_annotations_
void sbufRelease(StreamBuffer **sb)
{
    if (*sb) {
        (*sb)->refcount--;
        if ((*sb)->refcount <= 0)
            sbufDestroy(*sb);
        (*sb) = NULL;
    }
}

_Use_decl_annotations_
void sbufError(StreamBuffer *sb)
{
    sb->flags |= SBUF_Error;

    if (!(sb->flags & SBUF_Consumer_Done)) {
        if (sb->consumerNotify)
            sb->consumerNotify(sb, 0, sb->consumerCtx);
        if (sb->consumerPush)
            sb->consumerPush(sb, NULL, 0, sb->consumerCtx);
    }

    if (!(sb->flags & SBUF_Producer_Done) && sb->producerPull)
        sb->producerPull(sb, NULL, 0, sb->producerCtx);
}

_Use_decl_annotations_
bool sbufPRegisterPull(StreamBuffer *sb, sbufPullCB ppull, sbufCleanupCB pcleanup, void *ctx)
{
    // can only register once, if it's not already a push buffer
    if (sbufIsPush(sb) ||
        (sb->flags & SBUF_Producer_Registered) ||
        !ppull) {
        if (pcleanup)
            pcleanup(ctx);

        cxerr = CX_InvalidArgument;
        return false;
    }  

    sb->producerPull = ppull;
    sb->producerCleanup = pcleanup;
    sb->producerCtx = ctx;
    sb->flags |= SBUF_Producer_Registered | SBUF_Pull;
    sb->refcount++;

    return true;
}

_Use_decl_annotations_
bool sbufPRegisterPush(StreamBuffer *sb, sbufCleanupCB pcleanup, void *ctx)
{
    // can only register once, if it's not already a pull buffer
    if (sbufIsPull(sb) ||
        (sb->flags & SBUF_Producer_Registered)) {
        if (pcleanup)
            pcleanup(ctx);

        cxerr = CX_InvalidArgument;
        return false;
    }

    sb->producerCleanup = pcleanup;
    sb->producerCtx = ctx;
    sb->flags |= SBUF_Producer_Registered | SBUF_Push;
    sb->refcount++;

    return true;
}

_Use_decl_annotations_
size_t sbufPAvail(StreamBuffer *sb)
{
    if (sb->flags & SBUF_Direct)
        return 0;   // there is no buffer

    return bufringWriteSpace(&sb->buf);
}

_Use_decl_annotations_
size_t sbufCAvail(StreamBuffer *sb)
{
    if (sb->flags & SBUF_Direct)
        return 0;   // there is no buffer

    return sb->buf.total;
}

_Use_decl_annotations_
bool sbufPWrite(StreamBuffer *sb, const uint8 *buf, size_t sz)
{
    if (sz == 0)
        return true;

    devAssert(sb->flags & SBUF_Producer_Registered);
    devAssert(!(sb->flags & SBUF_Producer_Done));
    devAssert(sb->flags & SBUF_Consumer_Registered);

    if (sbufIsCFinished(sb)) {
        // nobody's listening
        return false;
    }

    if (sb->flags & SBUF_Direct) {
        // for direct mode just call the callback
        sb->consumerPush(sb, buf, sz, sb->consumerCtx);
    } else {
        bufringWrite(&sb->buf, buf, sz);

        // Notify consumer there's data in the buffer, but only in push mode.
        // This function may also be used in pull mode if the callback doesn't have
        // enough buffer space to write what it wants.
        if (sbufIsPush(sb)) {
            sb->consumerNotify(sb, sbufCAvail(sb), sb->consumerCtx);
        }
    }

    return true;
}

_Use_decl_annotations_
bool sbufPWriteStr(StreamBuffer *sb, strref str)
{
    foreach(string, it, str)
    {
        if (!sbufPWrite(sb, it.bytes, it.len))
            return false;
    }
    return true;
}

_Use_decl_annotations_
bool sbufPWriteLine(StreamBuffer *sb, strref str)
{
    foreach (string, it, str) {
        if (!sbufPWrite(sb, it.bytes, it.len))
            return false;
    }
#ifdef _PLATFORM_WIN
    return sbufPWrite(sb, (const uint8*)"\r\n", 2);
#else
    return sbufPWrite(sb, (const uint8*)"\n", 1);
#endif
    return true;
}

_Use_decl_annotations_
bool sbufPWriteEOL(StreamBuffer* sb)
{
#ifdef _PLATFORM_WIN
    return sbufPWrite(sb, (const uint8*)"\r\n", 2);
#else
    return sbufPWrite(sb, (const uint8*)"\n", 1);
#endif
    return true;
}

_Use_decl_annotations_
static void sbufPFinishInternal(StreamBuffer *sb)
{
    if (sb->flags & SBUF_Producer_Done)
        return;

    sb->flags |= SBUF_Producer_Done;

    // notify consumer about EOF
    if (!(sb->flags & SBUF_Consumer_Done)) {
        if (sb->consumerNotify) {
            // notify once for any remaining data in buffer, then again for EOF
            size_t left = sbufCAvail(sb);
            if (left > 0)
                sb->consumerNotify(sb, left, sb->consumerCtx);

            // check flag again in case they finished in the previous callback
            if (!(sb->flags & SBUF_Consumer_Done))
                sb->consumerNotify(sb, 0, sb->consumerCtx);
        } else if (sb->consumerPush)
            sb->consumerPush(sb, NULL, 0, sb->consumerCtx);
    }

    // if buffer is in push mode, consumer has gotten all the callbacks they're going to get
    if (sbufIsPush(sb))
        sbufCFinishInternal(sb);

    sbufRelease(&sb);
}

_Use_decl_annotations_
void sbufPFinish(StreamBuffer *sb)
{
    sbufPFinishInternal(sb);
}

_Use_decl_annotations_
bool sbufCRegisterPull(StreamBuffer *sb, sbufCleanupCB ccleanup, void *ctx)
{
    // can only register once, if it's not already a push buffer
    if (sbufIsPush(sb) ||
        (sb->flags & SBUF_Consumer_Registered)) {
        if (ccleanup)
            ccleanup(ctx);

        cxerr = CX_InvalidArgument;
        return false;
    }

    sb->consumerCleanup = ccleanup;
    sb->consumerCtx = ctx;
    sb->flags |= SBUF_Consumer_Registered | SBUF_Pull;
    sb->refcount++;

    return true;
}

_Use_decl_annotations_
bool sbufCRegisterPush(StreamBuffer *sb, sbufNotifyCB cnotify, sbufCleanupCB ccleanup, void *ctx)
{
    // can only register once, if it's not already a pull buffer, and is not set as a direct buffer
    if (sbufIsPull(sb) || (sb->flags & SBUF_Consumer_Registered) || sb->targetsz == 0 || !cnotify) {
        if (ccleanup)
            ccleanup(ctx);

        cxerr = CX_InvalidArgument;
        return false;
    }

    sb->consumerNotify = cnotify;
    sb->consumerCleanup = ccleanup;
    sb->consumerCtx = ctx;
    sb->flags |= SBUF_Consumer_Registered | SBUF_Push;
    sb->refcount++;

    return true;
}

_Use_decl_annotations_
bool sbufCRegisterPushDirect(StreamBuffer *sb, sbufPushCB cpush, sbufCleanupCB ccleanup, void *ctx)
{
    // can only register once, if it's not already a pull buffer, and is not set as a direct buffer
    if (sbufIsPull(sb) ||
        (sb->flags & SBUF_Consumer_Registered) ||
        !cpush) {
        if (ccleanup)
            ccleanup(ctx);

        cxerr = CX_InvalidArgument;
        return false;
    }

    sb->consumerPush = cpush;
    sb->consumerCleanup = ccleanup;
    sb->consumerCtx = ctx;
    sb->flags |= SBUF_Consumer_Registered | SBUF_Push | SBUF_Direct;
    sb->refcount++;

    return true;
}

typedef struct SbufRingFeedCtx {
    StreamBuffer* sb;
    size_t needed;
} SbufRingFeedCtx;

size_t sbufFeedCB(uint8* buf, size_t maxbytes, void* _ctx)
{
    SbufRingFeedCtx* ctx = (SbufRingFeedCtx*)_ctx;
    size_t toread        = min(ctx->needed, maxbytes);
    size_t read          = ctx->sb->producerPull(ctx->sb, buf, toread, ctx->sb->producerCtx);
    ctx->needed -= read;
    return read;
}

static void feedBuffer(_Inout_ StreamBuffer *sb, size_t want)
{
    SbufRingFeedCtx ctx = { .sb = sb };
    ctx.needed          = want - sbufCAvail(sb);
    bufringFeed(&sb->buf, sbufFeedCB, ctx.needed, &ctx);
}

_Use_decl_annotations_
bool sbufCRead(StreamBuffer *sb, uint8 *buf, size_t sz, size_t *bytesread)
{
    if ((sb->flags & SBUF_Direct) || sbufIsError(sb) || sz == 0) {
        *bytesread = 0;
        return false;               // can't pull in direct mode!
    }

    if (sbufIsPull(sb)) {
        // loop until we have enough data to satisfy the request
        while (!sbufIsPFinished(sb) && sz > sbufCAvail(sb)) {
            feedBuffer(sb, sz);
        }
        // cap sz at actual data available, which happens on EOF or error
        sz = min(sz, sbufCAvail(sb));
    } else if (sz > sbufCAvail(sb)) {
        // we don't have enough!
        return false;
    }

    *bytesread = bufringRead(&sb->buf, buf, sz);

    return (sz > 0);
}

_Use_decl_annotations_
bool sbufCPeek(StreamBuffer *sb, uint8 *buf, size_t off, size_t sz)
{
    if ((sb->flags & SBUF_Direct) || sbufIsError(sb))
        return false;               // can't peek in direct mode!

    if (sz == 0)
        return true;

    if (off + sz > sbufCAvail(sb)) {
        // we don't have enough!
        return false;
    }

    bufringPeek(&sb->buf, buf, off, sz);

    return true;
}

_Use_decl_annotations_
bool sbufCFeed(StreamBuffer *sb, size_t minsz)
{
    if (!sbufIsPull(sb))
        return false;

    // loop until we have enough data to satisfy the request
    while (!sbufIsPFinished(sb) && minsz > sbufCAvail(sb)) {
        feedBuffer(sb, minsz);
    }

    return sbufCAvail(sb) >= minsz;
}

_Use_decl_annotations_
bool sbufCSkip(StreamBuffer *sb, size_t bytes)
{
    if ((sb->flags & SBUF_Direct) || sbufIsError(sb))
        return false;               // can't seek in direct mode!

    if (bytes == 0)
        return true;

    if (bytes > sbufCAvail(sb)) {
        // we don't have enough!
        return false;
    }

    bufringSkip(&sb->buf, bytes);

    return true;
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
bool sbufCSend(StreamBuffer *sb, sbufSendCB func, size_t sz)
{
    if ((sb->flags & SBUF_Direct) || sbufIsError(sb))
        return false;                   // can't pull in direct mode!

    if (sz == 0)
        return true;

    if (sbufIsPull(sb)) {
        // loop until we have enough data to satisfy the request
        while (!sbufIsPFinished(sb) && sz > sbufCAvail(sb)) {
            feedBuffer(sb, sz);
        }
    }

    // cap sz at actual data available
    sz = min(sz, sbufCAvail(sb));

    SbufRingReadCtx ctx = { .sb = sb, .func = func };

    bufringReadZC(&sb->buf, sz, sbufRingRead, &ctx);

    return true;
}

_Use_decl_annotations_
static void sbufCFinishInternal(StreamBuffer *sb)
{
    if (sb->flags & SBUF_Consumer_Done)
        return;

    sb->flags |= SBUF_Consumer_Done;

    // if buffer is in pull mode, producer has gotten all the callbacks they're going to get
    if (sbufIsPull(sb))
        sbufPFinishInternal(sb);

    sbufRelease(&sb);
}

_Use_decl_annotations_
void sbufCFinish(StreamBuffer *sb)
{
    sbufCFinishInternal(sb);
}
