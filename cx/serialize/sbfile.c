#include "sbfile.h"
#include <cx/debug/assert.h>

// direct push mode has no target size of its own, so fall back to a reasonable chunk size
#define SBUF_DEFAULT_CHUNK (64 * 1024)

typedef struct SbufFileCtx {
    File* file;
    bool close;
} SbufFileCtx;

static void sbufFileCleanup(_Pre_valid_ void* ctx)
{
    SbufFileCtx* sbc = (SbufFileCtx*)ctx;
    if (sbc->close)
        fsClose(sbc->file);
    xaFree(sbc);
}

_Use_decl_annotations_
bool _sbufFileIn(StreamBuffer* sb, File* file, bool close)
{
    // This does not return until the source is exhausted, so waiting out the high watermark is the
    // only way to honor flow control.
    devAssertMsg(sb->high == 0 || sbufIsLocked(sb),
                 "Flow control needs a stream buffer that can block the producer");

    size_t chunksz = sb->targetsz > 0 ? sb->targetsz : SBUF_DEFAULT_CHUNK;

    uint8* buf     = xaAlloc(chunksz);
    size_t didread = 0;
    for (;;) {
        if (!fileRead(file, buf, chunksz, &didread)) {
            sbufError(sb);
            break;
        }

        if (didread == 0)   // EOF
            break;

        if (!sbufPWrite(sb, buf, didread, SBUF_Wait))
            break;
    }
    xaFree(buf);

    if (close)
        fsClose(file);

    return !sbufIsError(sb);
}

static size_t sbufFilePullCB(_Pre_valid_ StreamBuffer* sb, _Out_writes_bytes_(sz) uint8* buf,
                             size_t sz, _Pre_opt_valid_ void* ctx)
{
    SbufFileCtx* sbc = (SbufFileCtx*)ctx;
    if (!sbc)
        return 0;

    if (sz == 0) {
        // A status check rather than a request for data. Once the stream is over there is nothing
        // left to feed it, so hand the slot back.
        if (sbufIsClosed(sb))
            sbufPUnregister(sb);
        return 0;
    }

    size_t didread = 0;
    if (!fileRead(sbc->file, buf, sz, &didread))
        sbufError(sb);

    // end of file: leave the slot open for another producer rather than ending the stream
    if (didread == 0)
        sbufPUnregister(sb);

    return didread;
}

_Use_decl_annotations_
bool _sbufFilePRegisterPull(StreamBuffer* sb, File* file, bool close)
{
    SbufFileCtx* sbc = xaAlloc(sizeof(SbufFileCtx));
    sbc->file        = file;
    sbc->close       = close;

    if (!sbufPRegisterPull(sb, sbufFilePullCB, sbufFileCleanup, sbc)) {
        sbufFileCleanup(sbc);
        return false;
    }

    return true;
}

static bool sbufFileSendCB(_Pre_valid_ StreamBuffer* sb, _In_reads_bytes_(sz) const uint8* buf,
                           size_t off, size_t sz, _Pre_opt_valid_ void* ctx)
{
    SbufFileCtx* sbc = (SbufFileCtx*)ctx;
    if (!sbc)
        return false;

    size_t didwrite = 0;
    if (!fileWrite(sbc->file, (void*)buf, sz, &didwrite))
        sbufError(sb);

    return true;
}

static void sbufFileNotifyCB(_Pre_valid_ StreamBuffer* sb, size_t sz, _Pre_opt_valid_ void* ctx)
{
    if (sz >= (sb->targetsz >> 1) + (sb->targetsz >> 2)) {
        sbufCSend(sb, sbufFileSendCB, sz, ctx);
    } else if (sz == 0 || !sbufCMore(sb)) {
        // flush anything that's left in the streambuf
        sbufCSend(sb, sbufFileSendCB, sbufCAvail(sb), ctx);
    }

    // nothing more is coming, so hand the slot back; that closes the file if we own it
    if (sbufIsClosed(sb))
        sbufCUnregister(sb);
}

_Use_decl_annotations_
bool _sbufFileOut(StreamBuffer* sb, File* file, bool close)
{
    uint8* buf = xaAlloc(sb->targetsz);
    size_t sz;
    do {
        // grab targetsz at a time from the buffer
        if (sbufCRead(sb, buf, sb->targetsz, &sz)) {
            size_t didwrite;
            if (!fileWrite(file, buf, sz, &didwrite)) {
                sbufError(sb);
                break;
            }
        }
    } while (sz > 0 || sbufCMore(sb));
    xaFree(buf);

    if (close)
        fsClose(file);

    return !sbufIsError(sb);
}

_Use_decl_annotations_
bool _sbufFileCRegisterPush(StreamBuffer* sb, File* file, bool close)
{
    SbufFileCtx* sbc = xaAlloc(sizeof(SbufFileCtx));
    sbc->file        = file;
    sbc->close       = close;

    if (!sbufCRegisterPush(sb, sbufFileNotifyCB, sbufFileCleanup, sbc)) {
        sbufFileCleanup(sbc);
        return false;
    }

    return true;
}
