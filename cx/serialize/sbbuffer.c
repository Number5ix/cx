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
