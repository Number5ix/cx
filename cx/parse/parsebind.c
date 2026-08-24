#include "parse_private.h"

#include "cx/parse/parsable.h"
#include "cx/stype/stconvert.h"

#define PARSE_NUMFLAGS (STRNUM_NoTrailing | STRNUM_NoWS | STRNUM_NoPrefix)

// Whether convertNum() had anything to say about this destination type at all.
typedef enum { NUM_NotNumeric, NUM_Ok, NUM_Fail } NumResult;

_Use_decl_annotations_
void _parseSpanText(strhandle out, const ParseField* f, strref s, const StrPatSpan* sp)
{
    if (sp->off < 0) {
        // the field did not match, so its default stands in for the text it would have had
        strDup(out, f->def);
        return;
    }

    if (f->quoted) {
        strClear(out);
        bool escaped = false;
        for (int32 i = sp->off; i < sp->off + sp->len; i++) {
            uint8 c = strGetChar(s, i);
            if (!escaped && c == '\\') {
                escaped = true;
                continue;
            }
            escaped = false;
            strAppendBytes(out, &c, 1);
        }
    } else {
        strSubStr(out, s, sp->off, sp->off + sp->len);
    }

    if (f->trim) {
        string t = 0;
        strTrim(&t, *out, NULL);
        strDestroy(out);
        *out = t;
    }
    if (f->upper)
        strUpper(out);
    else if (f->lower)
        strLower(out);
}

// Numbers do not go through stConvert, because stConvert's string path is the permissive
// one -- it accepts a "0x" prefix and leading whitespace. A parser has to be exact about
// what it read, and it also has to honor the base the pattern asked for.
static NumResult convertNum(const ParseField* f, strref text, stype st, stgeneric* g, bool write)
{
    switch (st->id) {
    case stTypeId(int8):
    case stTypeId(int16):
    case stTypeId(int32):
    case stTypeId(int64): {
        // intptr shares int64's id on 64-bit and int32's on 32-bit, so it needs no case of
        // its own -- it always lands on whichever of the two it actually is
        int64 v;
        if (!strToInt64(&v, text, f->base, PARSE_NUMFLAGS))
            return NUM_Fail;

        switch (st->id) {
        case stTypeId(int8):
            if (v < MIN_INT8 || v > MAX_INT8)
                return NUM_Fail;
            if (write)
                g->st_int8 = (int8)v;
            break;
        case stTypeId(int16):
            if (v < MIN_INT16 || v > MAX_INT16)
                return NUM_Fail;
            if (write)
                g->st_int16 = (int16)v;
            break;
        case stTypeId(int32):
            if (v < MIN_INT32 || v > MAX_INT32)
                return NUM_Fail;
            if (write)
                g->st_int32 = (int32)v;
            break;
        default:
            if (write)
                g->st_int64 = v;
            break;
        }
        return NUM_Ok;
    }

    case stTypeId(uint8):
    case stTypeId(uint16):
    case stTypeId(uint32):
    case stTypeId(uint64): {
        uint64 v;
        if (!strToUInt64(&v, text, f->base, PARSE_NUMFLAGS | STRNUM_NoSign))
            return NUM_Fail;

        switch (st->id) {
        case stTypeId(uint8):
            if (v > MAX_UINT8)
                return NUM_Fail;
            if (write)
                g->st_uint8 = (uint8)v;
            break;
        case stTypeId(uint16):
            if (v > MAX_UINT16)
                return NUM_Fail;
            if (write)
                g->st_uint16 = (uint16)v;
            break;
        case stTypeId(uint32):
            if (v > MAX_UINT32)
                return NUM_Fail;
            if (write)
                g->st_uint32 = (uint32)v;
            break;
        default:
            if (write)
                g->st_uint64 = v;
            break;
        }
        return NUM_Ok;
    }

    case stTypeId(float32):
    case stTypeId(float64): {
        float64 d;
        if (!strToFloat64(&d, text, PARSE_NUMFLAGS))
            return NUM_Fail;
        if (write) {
            if (st->id == stTypeId(float32))
                g->st_float32 = (float32)d;
            else
                g->st_float64 = d;
        }
        return NUM_Ok;
    }

    case stTypeId(bool): {
        // Converted into a scratch first so that the rehearsal pass can find out whether
        // the text is a boolean at all without writing anything down
        stgeneric tmp;
        if (!_stConvert(stType(bool), &tmp, stType(string), stArg(string, (string)text), 0))
            return NUM_Fail;
        if (write)
            g->st_bool = tmp.st_bool;
        return NUM_Ok;
    }

    default:
        return NUM_NotNumeric;
    }
}

// Value a group's key field carries: which alternative matched, or 0 for none.
static bool bindGroup(int32 altno, const stvp* d, bool write)
{
    if (d->type->id == stTypeId(bool)) {
        if (write)
            d->ptr->st_bool = (altno != 0);
        return true;
    }

    if (!write) {
        // Every integer width can hold a small alternative number, and anything else is
        // left to stConvert to accept or refuse when it is actually written.
        return true;
    }

    return _stConvert(d->type, d->ptr, stType(int32), stArg(int32, altno), 0);
}

static bool bindOne(const StrPattern* pat, const ParseField* f, strref text, const StrPatSpan* sp,
                    const stvp* d, bool write)
{
    if (f->isgroup)
        return bindGroup(sp->off, d, write);

    switch (convertNum(f, text, d->type, d->ptr, write)) {
    case NUM_Ok:
        return true;
    case NUM_Fail:
        return false;
    default:
        break;
    }

    if (d->type->id == stTypeId(string)) {
        if (write)
            strDup(&d->ptr->st_string, text);
        return true;
    }

    if (d->type->id == stTypeId(object)) {
        // The caller supplies the object already built; the interface fills it in from the
        // text. Unlike every other destination this happens in place, so it is the one
        // conversion that cannot be rehearsed first.
        if (!write)
            return true;

        ObjInst* obj  = d->ptr->st_object;
        Parsable* pif = objInstIf(obj, Parsable);
        if (!pif)
            return false;

        ParseVar pv = { 0 };
        pv.opts     = f->objopts;
        pv.def      = f->def;
        if (pat->flags & STRPAT_CaseI)
            pv.flags |= PARSEVar_CaseInsensitive;
        if (f->trim)
            pv.flags |= PARSEVar_Trim;
        if (f->upper)
            pv.flags |= PARSEVar_Upper;
        if (f->lower)
            pv.flags |= PARSEVar_Lower;

        return pif->parse(obj, text, &pv);
    }

    if (!write)
        return true;

    return _stConvert(d->type, d->ptr, stType(string), stArg(string, (string)text), 0);
}

_Use_decl_annotations_
bool _parseBind(const StrPattern* pat, strref s, const StrPatSpan* spans, int32 nspans, int n,
                stvp* dests)
{
    int32 fbuf[16];
    int32* fmap = fbuf;
    bool ok     = true;

    if (n > (int)(sizeof(fbuf) / sizeof(fbuf[0])))
        fmap = xaAlloc(sizeof(int32) * (size_t)n);

    // Resolve every destination to a field before writing anything. A key that is not in
    // the pattern is a typo rather than a choice, and the same goes for more positional
    // destinations than the pattern has positional placeholders.
    int32 posn = 0;
    for (int i = 0; i < n; i++) {
        fmap[i] = -1;

        if (!dests[i].type)
            continue;   // stvpNone: a placeholder for "nothing to bind"

        if (dests[i].key) {
            for (int32 j = 0; j < saSize(pat->fields); j++) {
                if (pat->fields.a[j].key && strEq(pat->fields.a[j].key, (strref)dests[i].key)) {
                    fmap[i] = j;
                    break;
                }
            }
        } else {
            int32 want = posn++;
            for (int32 j = 0; j < saSize(pat->fields); j++) {
                if (pat->fields.a[j].posidx == want) {
                    fmap[i] = j;
                    break;
                }
            }
        }

        if (fmap[i] < 0) {
            ok = false;
            break;
        }
    }

    // Two passes: rehearse every conversion first, and only write once they all came back
    // clean. Together with the matcher recording spans rather than values, this is what
    // makes a parse that goes wrong leave every destination exactly as it found it.
    //
    // Numbers, booleans and strings -- the destinations a matched value can realistically
    // fail to fit -- are all rehearsed. An object destination is the one that cannot be,
    // since the interface fills the caller's object in place; see bindOne().
    for (int pass = 0; ok && pass < 2; pass++) {
        string text = 0;

        for (int32 si = 0; si < nspans && ok; si++) {
            const ParseField* f = &pat->fields.a[spans[si].field];

            bool needtext = false;
            for (int i = 0; i < n; i++) {
                if (fmap[i] == spans[si].field)
                    needtext = true;
            }
            if (!needtext)
                continue;

            if (!f->isgroup)
                _parseSpanText(&text, f, s, &spans[si]);

            for (int i = 0; i < n && ok; i++) {
                if (fmap[i] != spans[si].field)
                    continue;
                ok = bindOne(pat, f, text, &spans[si], &dests[i], pass == 1);
            }
        }

        strDestroy(&text);
    }

    if (fmap != fbuf)
        xaFree(fmap);

    return ok;
}

_Use_decl_annotations_
bool _strPatternMatch(StrPattern* pat, strref s, int n, stvp* dests)
{
    int32 pos = 0;
    if (!pat)
        return false;

    StrPatSpan sbuf[16];
    StrPatSpan* spans = sbuf;
    if (saSize(pat->fields) > (int32)(sizeof(sbuf) / sizeof(sbuf[0])))
        spans = xaAlloc(sizeof(StrPatSpan) * (size_t)saSize(pat->fields));

    int32 nspans = 0, endpos = 0;
    bool ok = _parseRun(pat, s, pos, true, spans, &nspans, &endpos) &&
        _parseBind(pat, s, spans, nspans, n, dests);

    if (spans != sbuf)
        xaFree(spans);

    return ok;
}

_Use_decl_annotations_
bool _strPatternMatchAt(int32* io_pos, StrPattern* pat, strref s, int n, stvp* dests)
{
    if (!pat)
        return false;

    StrPatSpan sbuf[16];
    StrPatSpan* spans = sbuf;
    if (saSize(pat->fields) > (int32)(sizeof(sbuf) / sizeof(sbuf[0])))
        spans = xaAlloc(sizeof(StrPatSpan) * (size_t)saSize(pat->fields));

    int32 nspans = 0, endpos = 0;
    bool ok = true;

    if (!_parseRun(pat, s, *io_pos, false, spans, &nspans, &endpos)) {
        // On a failed match endpos is how far the best attempt got, which is the most
        // useful thing to call the error position.
        *io_pos = endpos;
        ok      = false;
    } else if (!_parseBind(pat, s, spans, nspans, n, dests)) {
        ok = false;   // the text matched but could not be stored; the position stands
    } else {
        *io_pos = endpos;
    }

    if (spans != sbuf)
        xaFree(spans);

    return ok;
}
