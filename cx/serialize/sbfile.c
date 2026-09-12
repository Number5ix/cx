#include "sbfile.h"
#include <cx/debug/assert.h>

// direct push mode has no target size of its own, so fall back to a reasonable chunk size
#define SBUF_DEFAULT_CHUNK (64 * 1024)

// A registration outlives the call that made it: its callbacks run whenever the stream buffer says
// so, on whatever thread drives it, until the slot is handed back. So its closure captures the
// File, which takes a reference of its own for that whole time rather than borrowing the caller's
// pointer -- a File whose last reference went away while it was registered would otherwise be read
// or written through here after it was freed.
//
// With `close` set, the caller's reference is handed over as well, and the file is closed when the
// registration goes away even if something else still holds a reference to it, which is what the
// flag promises.
static void sbufFileClose(stvlist* cvars)
{
    fileClose(stvlAtObj(cvars, 0, File));
}

static closure sbufFileClosure(closure cls, File** file, bool close)
{
    if (close) {
        closureSetDestroy(cls, sbufFileClose);
        objRelease(file);   // the closure now holds the only reference this call was given
    }
    return cls;
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

static size_t sbufFilePullCB(stvlist* cvars, _Pre_valid_ StreamBuffer* sb,
                             _Out_writes_bytes_(sz) uint8* buf, size_t sz)
{
    if (sz == 0) {
        // A status check rather than a request for data. Once the stream is over there is nothing
        // left to feed it, so hand the slot back.
        if (sbufIsClosed(sb))
            sbufPUnregister(sb);
        return 0;
    }

    size_t didread = 0;
    if (!fileRead(stvlAtObj(cvars, 0, File), buf, sz, &didread))
        sbufError(sb);

    // end of file: leave the slot open for another producer rather than ending the stream
    if (didread == 0)
        sbufPUnregister(sb);

    return didread;
}

_Use_decl_annotations_
bool _sbufFilePRegisterPull(StreamBuffer* sb, File* file, bool close)
{
    closure cls = closureCreateAs(sbufPullCB, sbufFilePullCB, stvar(object, file));
    return sbufPRegisterPull(sb, sbufFileClosure(cls, &file, close));
}

static bool sbufFileSendCB(_Pre_valid_ StreamBuffer* sb, _In_reads_bytes_(sz) const uint8* buf,
                           size_t off, size_t sz, _Pre_opt_valid_ void* ctx)
{
    size_t didwrite = 0;
    if (!fileWrite((File*)ctx, (void*)buf, sz, &didwrite))
        sbufError(sb);

    return true;
}

static void sbufFileNotifyCB(stvlist* cvars, _Pre_valid_ StreamBuffer* sb, size_t sz)
{
    File* file = stvlAtObj(cvars, 0, File);

    if (sz >= (sb->targetsz >> 1) + (sb->targetsz >> 2)) {
        sbufCSend(sb, sbufFileSendCB, sz, file);
    } else if (sz == 0 || !sbufCMore(sb)) {
        // flush anything that's left in the streambuf
        sbufCSend(sb, sbufFileSendCB, sbufCAvail(sb), file);
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
    closure cls = closureCreateAs(sbufNotifyCB, sbufFileNotifyCB, stvar(object, file));
    return sbufCRegisterPush(sb, sbufFileClosure(cls, &file, close));
}
