#include "sbfile.h"

typedef struct SbufFileCtx {
    VFSFile *file;
    bool close;
} SbufFileCtx;

static void sbufFileCleanup(void *ctx)
{
    SbufFileCtx *sbc = (SbufFileCtx *)ctx;
    if (sbc->close)
        vfsClose(sbc->file);
    xaFree(sbc);
}

bool sbufFileIn(StreamBuffer *sb, VFSFile *file, bool close)
{
    if (!sbufPRegisterPush(sb, NULL, NULL))
        return false;

    uint8 *buf = xaAlloc(sb->targetsz);
    size_t didread = 0;
    for(;;) {
        if (!vfsRead(file, buf, sb->targetsz, &didread)) {
            sbufError(sb);
            break;
        }

        if (didread == 0)           // EOF
            break;

        sbufPWrite(sb, buf, didread);
    }
    xaFree(buf);

    if (close)
        vfsClose(file);

    bool ret = !sbufIsError(sb);
    sbufPFinish(sb);
    return ret;
}

static size_t sbufFilePullCB(StreamBuffer *sb, uint8 *buf, size_t sz, void *ctx)
{
    SbufFileCtx *sbc = (SbufFileCtx *)ctx;

    size_t didread = 0;
    if (!vfsRead(sbc->file, buf, sz, &didread))
        sbufError(sb);

    if (didread == 0)
        sbufPFinish(sb);

    return didread;
}

bool sbufFilePRegisterPull(StreamBuffer *sb, VFSFile *file, bool close)
{
    SbufFileCtx *sbc = xaAlloc(sizeof(SbufFileCtx));
    sbc->file = file;
    sbc->close = close;

    if (!sbufPRegisterPull(sb, sbufFilePullCB, sbufFileCleanup, sbc)) {
        sbufFileCleanup(sbc);
        return false;
    }

    return true;
}

static bool sbufFileSendCB(StreamBuffer *sb, const uint8 *buf, size_t off, size_t sz, void *ctx)
{
    SbufFileCtx *sbc = (SbufFileCtx *)ctx;

    size_t didwrite = 0;
    if (!vfsWrite(sbc->file, (void*)buf, sz, &didwrite))
        sbufError(sb);

    return true;
}

static void sbufFileNotifyCB(StreamBuffer *sb, size_t sz, void *ctx)
{
    if (sz >= sb->targetsz) {
        sbufCSend(sb, sbufFileSendCB, sz);
    } else if (sz == 0 || sbufIsPFinished(sb)) {
        // flush anything that's left in the streambuf
        sbufCSend(sb, sbufFileSendCB, sbufCAvail(sb));
    }

    if (sbufIsPFinished(sb))
        sbufCFinish(sb);
}

bool sbufFileOut(StreamBuffer *sb, VFSFile *file, bool close)
{
    if (!sbufCRegisterPull(sb, NULL, NULL))
        return false;

    uint8 *buf = xaAlloc(sb->targetsz);
    size_t sz;
    do {
        // grab targetsz at a time from the buffer
        sz = sbufCRead(sb, buf, sb->targetsz);
        if (sz > 0) {
            size_t didwrite;
            if (!vfsWrite(file, buf, sz, &didwrite)) {
                sbufError(sb);
                break;
            }
        }
    } while (sz > 0 || !sbufIsPFinished(sb));
    xaFree(buf);

    if (close)
        vfsClose(file);

    bool ret = !sbufIsError(sb);
    sbufCFinish(sb);
    return ret;
}

bool sbufFileCRegisterPush(StreamBuffer *sb, VFSFile *file, bool close)
{
    SbufFileCtx *sbc = xaAlloc(sizeof(SbufFileCtx));
    sbc->file = file;
    sbc->close = close;

    if (!sbufCRegisterPush(sb, sbufFileNotifyCB, sbufFileCleanup, sbc)) {
        sbufFileCleanup(sbc);
        return false;
    }

    return true;
}
