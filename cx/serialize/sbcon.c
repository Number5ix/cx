#include "sbcon.h"

typedef struct SbufConCtx {
    ConStream* con;
} SbufConCtx;

static void sbufConCleanup(_Pre_opt_valid_ void* ctx)
{
    xaFree(ctx);
}

static bool sbufConSendCB(_Pre_valid_ StreamBuffer* sb, _In_reads_bytes_(sz) const uint8* buf,
                          size_t off, size_t sz, _Pre_opt_valid_ void* ctx)
{
    SbufConCtx* sbc = (SbufConCtx*)ctx;
    if (!sbc)
        return false;

    if (!conWrite(sbc->con, buf, sz))
        sbufError(sb);

    return true;
}

static void sbufConNotifyCB(_Pre_valid_ StreamBuffer* sb, size_t sz, _Pre_opt_valid_ void* ctx)
{
    if (sz >= (sb->targetsz >> 1) + (sb->targetsz >> 2)) {
        sbufCSend(sb, sbufConSendCB, sz, ctx);
    } else if (sz == 0 || !sbufCMore(sb)) {
        // flush anything that's left in the streambuf
        sbufCSend(sb, sbufConSendCB, sbufCAvail(sb), ctx);
    }

    // nothing more is coming, so hand the slot back
    if (sbufIsClosed(sb))
        sbufCUnregister(sb);
}

_Use_decl_annotations_
bool sbufConOut(StreamBuffer* sb, ConStream* con)
{
    uint8* buf = xaAlloc(sb->targetsz);
    size_t sz;
    do {
        // grab targetsz at a time from the buffer
        if (sbufCRead(sb, buf, sb->targetsz, &sz)) {
            if (!conWrite(con, buf, sz)) {
                sbufError(sb);
                break;
            }
        }
    } while (sz > 0 || sbufCMore(sb));
    xaFree(buf);

    return !sbufIsError(sb);
}

_Use_decl_annotations_
bool sbufConCRegisterPush(StreamBuffer* sb, ConStream* con)
{
    SbufConCtx* sbc = xaAlloc(sizeof(SbufConCtx));
    sbc->con        = con;

    if (!sbufCRegisterPush(sb, sbufConNotifyCB, sbufConCleanup, sbc)) {
        sbufConCleanup(sbc);
        return false;
    }

    return true;
}
