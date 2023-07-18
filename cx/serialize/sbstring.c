#include "sbstring.h"
#include <cx/string.h>
#include <cx/utils/compare.h>

typedef struct SbufStrInCtx {
    striter iter;
} SbufStrInCtx;

static void sbufStrInCleanup(void *ctx)
{
    SbufStrInCtx *sbc = (SbufStrInCtx *)ctx;
    striFinish(&sbc->iter);
    xaFree(sbc);
}

bool sbufStrIn(StreamBuffer *sb, strref str)
{
    striter si;

    if (!sbufPRegisterPush(sb, NULL, NULL))
        return false;

    striBorrow(&si, str);

    // iterate entire string
    while (si.len > 0) {
        while (si.cursor < si.len) {
            // push at most the target buffer size
            uint32 nbytes = min(si.len - si.cursor, (uint32)sb->targetsz);
            sbufPWrite(sb, si.bytes + si.cursor, nbytes);
            si.cursor += nbytes;
        }
        striNext(&si);
    }

    striFinish(&si);
    bool ret = !sbufIsError(sb);
    sbufPFinish(sb);
    return ret;
}

static size_t sbufStrPullCB(StreamBuffer *sb, char *buf, size_t sz, void *ctx)
{
    SbufStrInCtx *sbc = (SbufStrInCtx *)ctx;

    uint32 nbytes = min(sbc->iter.len - sbc->iter.cursor, (uint32)sz);
    if (nbytes > 0) {
        memcpy(buf, sbc->iter.bytes + sbc->iter.cursor, nbytes);
        striAdvance(&sbc->iter, nbytes);
    }

    if (sbc->iter.len == 0)
        sbufPFinish(sb);

    return nbytes;
}

bool sbufStrPRegisterPull(StreamBuffer *sb, strref str)
{
    SbufStrInCtx *sbc = xaAlloc(sizeof(SbufStrInCtx));

    striInit(&sbc->iter, str);

    if (!sbufPRegisterPull(sb, sbufStrPullCB, sbufStrInCleanup, sbc))
        return false;

    return true;
}

typedef struct SbufStrOutCtx {
    string *out;
} SbufStrOutCtx;

static void sbufStrOutCleanup(void *ctx)
{
    SbufStrOutCtx *sbc = (SbufStrOutCtx *)ctx;
    xaFree(sbc);
}

static void sbufStrNotifyCB(StreamBuffer *sb, size_t sz, void *ctx)
{
    SbufStrOutCtx *sbc = (SbufStrOutCtx *)ctx;

    string temp = 0;
    char *tbuf = strBuffer(&temp, (uint32)sz);
    sz = sbufCRead(sb, tbuf, sz);
    if (sz > 0) {
        strSetLen(&temp, (uint32)sz);
        strAppend(sbc->out, temp);
    }
    strDestroy(&temp);

    if (sbufIsPFinished(sb)) {
        devAssert(sbufCAvail(sb) == 0);
        sbufCFinish(sb);
    }
}

bool sbufStrOut(StreamBuffer *sb, string *strout)
{
    if (!sbufCRegisterPull(sb, NULL, NULL))
        return false;

    string temp = 0;
    uint32 sz;
    do {
        // grab targetsz at a time from the buffer
        strClear(&temp);
        char *tbuf = strBuffer(&temp, (uint32)sb->targetsz);

        sz = (uint32)sbufCRead(sb, tbuf, sb->targetsz);
        if (sz > 0) {
            strSetLen(&temp, sz);
            strAppend(strout, temp);
        }
    } while (sz > 0 || !sbufIsPFinished(sb));

    strDestroy(&temp);

    bool ret = !sbufIsError(sb);
    sbufCFinish(sb);
    return ret;
}

bool sbufStrCRegisterPush(StreamBuffer *sb, string *strout)
{
    SbufStrOutCtx *sbc = xaAlloc(sizeof(SbufStrOutCtx));
    sbc->out = strout;

    if (!sbufCRegisterPush(sb, sbufStrNotifyCB, sbufStrOutCleanup, sbc))
        return false;

    return true;
}
