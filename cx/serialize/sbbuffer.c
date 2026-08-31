#include "sbbuffer.h"
#include <cx/debug/assert.h>
#include <cx/utils/compare.h>

typedef struct SbufBufInCtx {
    Buffer buf;
    size_t pos;
    bool own;
} SbufBufInCtx;

static void sbufBufInCleanup(_Pre_opt_valid_ void* ctx)
{
    SbufBufInCtx* sbc = (SbufBufInCtx*)ctx;
    if (!sbc)
        return;

    if (sbc->own)
        bufDestroy(&sbc->buf);
    xaFree(sbc);
}

_Use_decl_annotations_
bool sbufBufIn(StreamBuffer* sb, Buffer buf, bool own)
{
    // This does not return until the source is exhausted, so waiting out the high watermark is the
    // only way to honor flow control. That needs a consumer draining on another thread, which is
    // exactly what SBUF_Locked promises.
    devAssertMsg(sb->high == 0 || sbufIsLocked(sb),
                 "Flow control needs a stream buffer that can block the producer");

    if (!sbufPRegisterPush(sb, NULL, NULL, sbufIsLocked(sb) ? SBUF_PBlock : 0)) {
        if (own)
            bufDestroy(&buf);
        return false;
    }

    size_t len = buf ? buf->len : 0;
    size_t pos = 0;
    bool ret   = true;

    // a buffer created for direct push mode has no target size, so it all goes in one write
    size_t chunksz = sb->targetsz > 0 ? sb->targetsz : len;

    while (pos < len) {
        // push at most the target buffer size
        size_t nbytes = min(len - pos, chunksz);
        if (!sbufPWrite(sb, buf->data + pos, nbytes)) {
            ret = false;
            break;
        }
        pos += nbytes;
    }

    if (own)
        bufDestroy(&buf);

    ret = ret && !sbufIsError(sb);
    sbufPFinish(sb);
    return ret;
}

static size_t sbufBufPullCB(_Pre_valid_ StreamBuffer* sb, _Out_writes_bytes_(sz) uint8* buf,
                            size_t sz, _Pre_opt_valid_ void* ctx)
{
    SbufBufInCtx* sbc = (SbufBufInCtx*)ctx;
    if (!sbc)
        return 0;

    size_t remain = sbc->buf ? sbc->buf->len - sbc->pos : 0;
    size_t nbytes = min(remain, sz);

    if (nbytes > 0) {
        memcpy(buf, sbc->buf->data + sbc->pos, nbytes);
        sbc->pos += nbytes;
        remain -= nbytes;
    }

    if (remain == 0)
        sbufPFinish(sb);

    return nbytes;
}

_Use_decl_annotations_
bool sbufBufPRegisterPull(StreamBuffer* sb, Buffer buf, bool own)
{
    SbufBufInCtx* sbc = xaAllocStruct(SbufBufInCtx);
    sbc->buf          = buf;
    sbc->pos          = 0;
    sbc->own          = own;

    if (!sbufPRegisterPull(sb, sbufBufPullCB, sbufBufInCleanup, sbc)) {
        // registration never ran, so the cleanup callback will not either
        sbufBufInCleanup(sbc);
        return false;
    }

    return true;
}

typedef struct SbufBufOutCtx {
    Buffer* out;
} SbufBufOutCtx;

static void sbufBufOutCleanup(_Pre_opt_valid_ void* ctx)
{
    xaFree(ctx);
}

// Reads straight into the tail of the output buffer, so nothing is copied twice on the way in.
static bool sbufBufDrain(_Pre_valid_ StreamBuffer* sb, _Inout_ Buffer* out, size_t sz,
                         _Out_ size_t* got)
{
    *got = 0;
    if (sz == 0)
        return true;

    uint8* dest = bufReserve(out, sz);
    if (!sbufCRead(sb, dest, sz, got))
        return false;

    (*out)->len += *got;
    return true;
}

// Direct mode hands the producer's bytes straight here, so the only copy anywhere on this path is
// the one into the output buffer. A push consumer has to take everything it is given in one go,
// which a Buffer can always do -- it grows.
static void sbufBufPushCB(_Pre_valid_ StreamBuffer* sb, _In_reads_bytes_(sz) const uint8* buf,
                          size_t sz, _Pre_opt_valid_ void* ctx)
{
    SbufBufOutCtx* sbc = (SbufBufOutCtx*)ctx;
    if (!sbc || sz == 0)
        return;   // sz == 0 is a status query, not data

    bufAppendBytes(sbc->out, buf, sz);
}

_Use_decl_annotations_
bool sbufBufOut(StreamBuffer* sb, Buffer* bufout)
{
    if (!sbufCRegisterPull(sb, NULL, NULL))
        return false;

    size_t got;
    do {
        if (!sbufBufDrain(sb, bufout, sb->targetsz, &got))
            break;
    } while (got > 0 || !sbufIsPFinished(sb));

    bool ret = !sbufIsError(sb);
    sbufCFinish(sb);
    return ret;
}

_Use_decl_annotations_
bool sbufBufCRegisterPush(StreamBuffer* sb, Buffer* bufout)
{
    SbufBufOutCtx* sbc = xaAllocStruct(SbufBufOutCtx);
    sbc->out           = bufout;

    if (!sbufCRegisterPushDirect(sb, sbufBufPushCB, sbufBufOutCleanup, sbc)) {
        // registration never ran, so the cleanup callback will not either
        sbufBufOutCleanup(sbc);
        return false;
    }

    return true;
}

_Use_decl_annotations_
StreamBuffer* sbufBufCreatePush(Buffer* bufout)
{
    // No target size: a direct-mode consumer never uses the stream buffer's own storage, so
    // allocating any would be dead weight.
    StreamBuffer* ret = sbufCreate(0);
    if (!ret)
        return NULL;

    if (!sbufPRegisterPush(ret, NULL, NULL)) {
        sbufRelease(&ret);
        return NULL;
    }

    if (!sbufBufCRegisterPush(ret, bufout)) {
        sbufRelease(&ret);
        return NULL;
    }

    return ret;
}
