#include "sbbuffer.h"
#include <cx/debug/assert.h>
#include <cx/utils/compare.h>

// A pull producer walks through the buffer as it is asked for data, so the position is state that
// changes on every call. That lives in a context the closure points at, and the closure's destroy
// function frees it -- along with the buffer itself, when the caller handed that over.
typedef struct SbufBufInCtx {
    Buffer buf;
    size_t pos;
    bool own;
} SbufBufInCtx;

static void sbufBufInDestroy(stvlist* cvars)
{
    SbufBufInCtx* sbc = stvlAtPtr(cvars, 0);
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

    size_t len = buf ? buf->len : 0;
    size_t pos = 0;
    bool ret   = true;

    // a buffer created for direct push mode has no target size, so it all goes in one write
    size_t chunksz = sb->targetsz > 0 ? sb->targetsz : len;

    while (pos < len) {
        // push at most the target buffer size
        size_t nbytes = min(len - pos, chunksz);
        if (!sbufPWrite(sb, buf->data + pos, nbytes, SBUF_Wait)) {
            ret = false;
            break;
        }
        pos += nbytes;
    }

    if (own)
        bufDestroy(&buf);

    return ret && !sbufIsError(sb);
}

static size_t sbufBufPullCB(stvlist* cvars, _Pre_valid_ StreamBuffer* sb,
                            _Out_writes_bytes_(sz) uint8* buf, size_t sz)
{
    SbufBufInCtx* sbc = stvlAtPtr(cvars, 0);

    if (sz == 0) {
        // A status check rather than a request for data. Once the stream is over there is nothing
        // left to feed it, so hand the slot back.
        if (sbufIsClosed(sb))
            sbufPUnregister(sb);
        return 0;
    }

    size_t remain = sbc->buf ? sbc->buf->len - sbc->pos : 0;
    size_t nbytes = min(remain, sz);

    if (nbytes > 0) {
        memcpy(buf, sbc->buf->data + sbc->pos, nbytes);
        sbc->pos += nbytes;
        remain -= nbytes;
    }

    // out of buffer: leave the slot open for another producer rather than ending the stream
    if (remain == 0)
        sbufPUnregister(sb);

    return nbytes;
}

_Use_decl_annotations_
bool sbufBufPRegisterPull(StreamBuffer* sb, Buffer buf, bool own)
{
    SbufBufInCtx* sbc = xaAllocStruct(SbufBufInCtx);
    sbc->buf          = buf;
    sbc->pos          = 0;
    sbc->own          = own;

    closure cls = closureCreateAs(sbufPullCB, sbufBufPullCB, stvar(ptr, sbc));
    closureSetDestroy(cls, sbufBufInDestroy);
    return sbufPRegisterPull(sb, cls);
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
static void sbufBufPushCB(stvlist* cvars, _Pre_valid_ StreamBuffer* sb,
                          _In_reads_bytes_(sz) const uint8* buf, size_t sz)
{
    if (sz > 0)
        bufAppendBytes((Buffer*)stvlAtPtr(cvars, 0), buf, sz);

    // nothing more is coming, so hand the slot back
    if (sbufIsClosed(sb))
        sbufCUnregister(sb);
}

_Use_decl_annotations_
bool sbufBufOut(StreamBuffer* sb, Buffer* bufout)
{
    size_t got;
    do {
        if (!sbufBufDrain(sb, bufout, sb->targetsz, &got))
            break;
    } while (got > 0 || sbufCMore(sb));

    return !sbufIsError(sb);
}

_Use_decl_annotations_
bool sbufBufCRegisterPush(StreamBuffer* sb, Buffer* bufout)
{
    return sbufCRegisterPushDirect(sb,
                                   closureCreateAs(sbufPushCB, sbufBufPushCB, stvar(ptr, bufout)));
}

_Use_decl_annotations_
StreamBuffer* sbufBufCreatePush(Buffer* bufout)
{
    // No target size: a direct-mode consumer never uses the stream buffer's own storage, so
    // allocating any would be dead weight.
    StreamBuffer* ret = sbufCreate(0);
    if (!ret)
        return NULL;

    if (!sbufBufCRegisterPush(ret, bufout)) {
        sbufRelease(&ret);
        return NULL;
    }

    return ret;
}
