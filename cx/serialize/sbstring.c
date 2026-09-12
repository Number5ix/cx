#include "sbstring.h"
#include <cx/debug/assert.h>
#include <cx/string.h>
#include <cx/utils/compare.h>

// A pull producer walks through the string as it is asked for data, so the iterator is state that
// changes on every call. That lives in a context the closure points at, and the closure's destroy
// function finishes the iterator and frees it.
typedef struct SbufProviderCtx {
    striter iter;
} SbufProviderCtx;

static void sbufProviderDestroy(stvlist* cvars)
{
    SbufProviderCtx* sbc = stvlAtPtr(cvars, 0);
    striFinish(&sbc->iter);
    xaFree(sbc);
}

_Use_decl_annotations_
bool sbufStrIn(StreamBuffer* sb, strref str)
{
    striter si;

    // This does not return until the source is exhausted, so waiting out the high watermark is the
    // only way to honor flow control.
    devAssertMsg(sb->high == 0 || sbufIsLocked(sb),
                 "Flow control needs a stream buffer that can block the producer");

    striBorrow(&si, str);

    // a buffer created for direct push mode has no target size, so it all goes in one write
    size_t chunksz = sb->targetsz > 0 ? sb->targetsz : (size_t)-1;
    bool ok        = true;

    // iterate entire string
    while (ok && si.len > 0) {
        while (ok && si.cursor < si.len) {
            // push at most the target buffer size
            uint32 nbytes = (uint32)min((size_t)(si.len - si.cursor), chunksz);
            ok            = sbufPWrite(sb, si.bytes + si.cursor, nbytes, SBUF_Wait);
            if (ok)
                si.cursor += nbytes;
        }
        if (ok)
            striNext(&si);
    }

    striFinish(&si);
    return !sbufIsError(sb);
}

static size_t sbufStrPullCB(stvlist* cvars, _Pre_valid_ StreamBuffer* sb,
                            _Out_writes_bytes_(sz) uint8* buf, size_t sz)
{
    SbufProviderCtx* sbc = stvlAtPtr(cvars, 0);

    if (sz == 0) {
        // A status check rather than a request for data. Once the stream is over there is nothing
        // left to feed it, so hand the slot back.
        if (sbufIsClosed(sb))
            sbufPUnregister(sb);
        return 0;
    }

    uint32 nbytes = min(sbc->iter.len - sbc->iter.cursor, (uint32)sz);
    if (nbytes > 0) {
        memcpy(buf, sbc->iter.bytes + sbc->iter.cursor, nbytes);
        striAdvance(&sbc->iter, nbytes);
    }

    // out of string: leave the slot open for another producer rather than ending the stream
    if (sbc->iter.len == 0)
        sbufPUnregister(sb);

    return nbytes;
}

_Use_decl_annotations_
bool sbufStrPRegisterPull(StreamBuffer* sb, strref str)
{
    SbufProviderCtx* sbc = xaAllocStruct(SbufProviderCtx);
    striInit(&sbc->iter, str);

    closure cls = closureCreateAs(sbufPullCB, sbufStrPullCB, stvar(ptr, sbc));
    closureSetDestroy(cls, sbufProviderDestroy);
    return sbufPRegisterPull(sb, cls);
}

static void sbufStrNotifyCB(stvlist* cvars, _Pre_valid_ StreamBuffer* sb, size_t sz)
{
    if (sz > 0) {
        string temp   = 0;
        size_t didread;
        uint8* tbuf   = strBuffer(&temp, (uint32)sz);
        if (sbufCRead(sb, tbuf, sz, &didread)) {
            strSetLen(&temp, (uint32)didread);
            strAppend((string*)stvlAtPtr(cvars, 0), temp);
        }
        strDestroy(&temp);
    }

    // nothing more is coming, so hand the slot back
    if (sbufIsClosed(sb))
        sbufCUnregister(sb);
}

_Use_decl_annotations_
bool sbufStrOut(StreamBuffer* sb, string* strout)
{
    string temp = 0;
    size_t sz;
    do {
        // grab targetsz at a time from the buffer
        strClear(&temp);
        uint8* tbuf = strBuffer(&temp, (uint32)sb->targetsz);

        if (sbufCRead(sb, tbuf, sb->targetsz, &sz)) {
            strSetLen(&temp, (uint32)sz);
            strAppend(strout, temp);
        }
    } while (sz > 0 || sbufCMore(sb));

    strDestroy(&temp);

    return !sbufIsError(sb);
}

_Use_decl_annotations_
bool sbufStrCRegisterPush(StreamBuffer* sb, string* strout)
{
    return sbufCRegisterPush(sb,
                             closureCreateAs(sbufNotifyCB, sbufStrNotifyCB, stvar(ptr, strout)));
}

_Use_decl_annotations_
StreamBuffer* sbufStrCreatePush(string* strout, size_t targetsz)
{
    StreamBuffer* ret = sbufCreate(targetsz);
    if (!ret)
        return NULL;

    if (!sbufStrCRegisterPush(ret, strout)) {
        sbufRelease(&ret);
        return NULL;
    }

    return ret;
}
