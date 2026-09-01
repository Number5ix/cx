#include "lineparse.h"
#include <cx/cx.h>
#include <cx/string.h>
#include <cx/utils/compare.h>

#define LPCHUNK 128

typedef struct LineParser {
    StreamBuffer* sb;   // our own reference, held for as long as the parser exists
    uint32 flags;

    size_t checked;   // buffer offset that has already been checked for EOL
    string out;       // cached output string

    lparseLineCB lineCB;   // push mode only; NULL means nothing was registered
    void* userCtx;
    sbufCleanupCB userCleanupCB;
} LineParser;

typedef struct EOLFindInfo {
    size_t off;
    int len;
} EOLFindInfo;

static bool findEOLLF(_Inout_ EOLFindInfo* ei, _In_reads_bytes_(sz) const uint8* buf, size_t sz,
                      _Inout_ LineParser* lpc)
{
    for (size_t i = 0; i < sz; i++) {
        if (buf[i] == '\n') {
            ei->off = i;
            ei->len = 1;
            return true;
        }
    }
    return false;
}

static bool findEOLCRLF(_Inout_ EOLFindInfo* ei, _In_reads_bytes_(sz) const uint8* buf, size_t sz,
                        _Inout_ LineParser* lpc)
{
    for (size_t i = 0; i + 1 < sz; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n') {
            ei->off = i;
            ei->len = 2;
            return true;
        }
    }
    return false;
}

static bool findEOLMixed(_Inout_ EOLFindInfo* ei, _In_reads_bytes_(sz) const uint8* buf, size_t sz,
                         _Inout_ LineParser* lpc)
{
    for (size_t i = 0; i < sz; i++) {
        if (i + 1 < sz && buf[i] == '\r' && buf[i + 1] == '\n') {
            ei->off = i;
            ei->len = 2;
            return true;
        } else if (buf[i] == '\n') {
            ei->off = i;
            ei->len = 1;
            return true;
        }
    }
    return false;
}

static bool findEOLAuto(_Inout_ EOLFindInfo* ei, _In_reads_bytes_(sz) const uint8* buf, size_t sz,
                        _Inout_ LineParser* lpc)
{
    for (size_t i = 0; i < sz; i++) {
        if (i + 1 < sz && buf[i] == '\r' && buf[i + 1] == '\n') {
            ei->off = i;
            ei->len = 2;
            lpc->flags |= LPARSE_CRLF;
            return true;
        } else if (buf[i] == '\n') {
            ei->off = i;
            ei->len = 1;
            lpc->flags |= LPARSE_LF;
            return true;
        }
    }
    return false;
}

// these must be kept in the same order as the flags 0-3 in LINEPARSER_FLAGS_ENUM!
static bool (*eolfuncs[])(_Inout_ EOLFindInfo* ei, _In_reads_bytes_(sz) const uint8* buf, size_t sz,
                          _Inout_ LineParser* lpc) = {
    findEOLAuto,
    findEOLCRLF,
    findEOLLF,
    findEOLMixed
};

_Static_assert((sizeof(eolfuncs) / sizeof(eolfuncs[0]) == LPARSE_EOL_COUNT),
               "Wrong number of EOL functions");

_Use_decl_annotations_
LineParser* _lparseCreatePull(StreamBuffer* sb, flags_t flags)
{
    LineParser* lpc = xaAlloc(sizeof(LineParser), XA_Zero);

    lpc->sb    = sbufAcquire(sb);
    lpc->flags = flags;

    return lpc;
}

_Use_decl_annotations_
void lparseDestroy(LineParser** lp)
{
    LineParser* lpc = *lp;
    if (!lpc)
        return;
    *lp = NULL;

    // only a push parser ever took a consumer slot
    if (lpc->lineCB)
        sbufCUnregister(lpc->sb);

    if (lpc->userCleanupCB)
        lpc->userCleanupCB(lpc->userCtx);

    strDestroy(&lpc->out);
    sbufRelease(&lpc->sb);

    xaFree(lpc);
}

// Returns false when there are no more lines.
_Use_decl_annotations_
bool lparseLine(LineParser* lpc, string* out)
{
    StreamBuffer* sb = lpc->sb;
    EOLFindInfo ei;
    uint8 buf[LPCHUNK];
    uint8* outbuf;
    size_t didread;

    strClear(out);

    for (;;) {
        // feed buffer if we have already checked everything
        if (sbufCAvail(sb) - lpc->checked <= 1) {
            // check for <= 1 because of the 1-byte overlap mentioned below
            sbufCFeed(sb, lpc->checked + LPCHUNK);
        }

        size_t tocheck = min(sbufCAvail(sb) - lpc->checked, LPCHUNK);
        if (!sbufCPeek(sb, buf, lpc->checked, tocheck))
            break;

        if (eolfuncs[lpc->flags & LPARSE_EOL_MASK](&ei, buf, tocheck, lpc)) {
            // adjust offset since we only started looking at lpc->checked
            ei.off += lpc->checked;

            // found one!
            if (!(lpc->flags & LPARSE_IncludeEOL)) {
                // EOL in string (default)
                outbuf = strBuffer(out, (uint32)ei.off);
                sbufCRead(sb, outbuf, (uint32)ei.off, &didread);
                devAssert(didread == ei.off);
                sbufCSkip(sb, ei.len);
            } else {
                // include EOL character(s) in string
                outbuf = strBuffer(out, (uint32)ei.off + ei.len);
                sbufCRead(sb, outbuf, ei.off + ei.len, &didread);
                devAssert(didread == ei.off + ei.len);
            }
            // buffer has been advanced to right after the EOL
            lpc->checked = 0;
            return true;
        }

        if (!sbufCMore(sb)) {
            // no EOL but we have everything we're going to get
            if (sbufCAvail(sb) > 0 && !(lpc->flags & LPARSE_NoIncomplete)) {
                outbuf = strBuffer(out, (uint32)sbufCAvail(sb));
                sbufCRead(sb, outbuf, sbufCAvail(sb), &didread);
                return true;
            }

            return false;
        }

        // Mark that we've already checked this part,
        // but offset it so there's a 1-byte overlap.
        // This is to catch CRLF pairs that get split across buffer boundaries.
        lpc->checked += max(tocheck, 1) - 1;
    }

    return false;
}

// -------- Push mode --------

static void lpcNotify(_Pre_valid_ StreamBuffer* sb, size_t sz, _Pre_opt_valid_ void* ctx)
{
    LineParser* lpc = (LineParser*)ctx;
    EOLFindInfo ei;
    uint8 buf[LPCHUNK];
    uint8* outbuf;
    size_t didread;

    // keep looking for lines as long as there's data in the buffer
    while (sbufCAvail(sb) > 0 && lpc->checked < sbufCAvail(sb) - (sbufCMore(sb) ? 1 : 0)) {
        size_t tocheck = min(sbufCAvail(sb) - lpc->checked, LPCHUNK);
        if (!sbufCPeek(sb, buf, lpc->checked, tocheck))
            break;

        if (eolfuncs[lpc->flags & LPARSE_EOL_MASK](&ei, buf, tocheck, lpc)) {
            // adjust offset since we only started looking at lpc->checked
            ei.off += lpc->checked;

            // found one!
            strClear(&lpc->out);
            if (!(lpc->flags & LPARSE_IncludeEOL)) {
                // EOL in string (default)
                outbuf = strBuffer(&lpc->out, (uint32)ei.off);
                sbufCRead(sb, outbuf, (uint32)ei.off, &didread);
                devAssert(didread == ei.off);
                sbufCSkip(sb, ei.len);
            } else {
                // include EOL character(s) in string
                outbuf = strBuffer(&lpc->out, (uint32)ei.off + ei.len);
                sbufCRead(sb, outbuf, ei.off + ei.len, &didread);
                devAssert(didread == ei.off + ei.len);
            }
            // buffer has been advanced to right after the EOL
            lpc->checked = 0;

            if (!lpc->lineCB(lpc->out, lpc->userCtx)) {
                // the callback wants no more of this stream, which is a hangup rather than just
                // this parser stepping aside
                sbufClose(sb);
                return;
            }
        } else {
            lpc->checked += tocheck - (sbufCMore(sb) ? 1 : 0);
        }
    }

    if (!sbufCMore(sb)) {
        if (sbufCAvail(sb) > 0 && !(lpc->flags & LPARSE_NoIncomplete)) {
            // no EOL but we have everything we're going to get
            strClear(&lpc->out);
            outbuf = strBuffer(&lpc->out, (uint32)sbufCAvail(sb));
            sbufCRead(sb, outbuf, sbufCAvail(sb), &didread);
            lpc->lineCB(lpc->out, lpc->userCtx);
        }
    }
}

_Use_decl_annotations_
LineParser* _lparseCreatePush(StreamBuffer* sb, lparseLineCB pline, sbufCleanupCB pcleanup,
                              void* ctx, flags_t flags)
{
    LineParser* lpc = xaAlloc(sizeof(LineParser), XA_Zero);

    lpc->sb            = sbufAcquire(sb);
    lpc->flags         = flags;
    lpc->userCtx       = ctx;
    lpc->userCleanupCB = pcleanup;

    // Installed before registering, since a buffer that already has data waiting notifies from
    // inside the registration call.
    lpc->lineCB = pline;

    // The parser owns itself rather than the registration owning it, so no cleanup goes with the
    // registration; lparseDestroy() is what tears both down.
    if (!pline || !sbufCRegisterPush(sb, lpcNotify, NULL, lpc)) {
        lpc->lineCB = NULL;   // nothing was registered, so there is nothing to unregister
        lparseDestroy(&lpc);
        return NULL;
    }

    return lpc;
}
