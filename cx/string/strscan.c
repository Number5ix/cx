#include "string_private.h"

#include "cx/stype/stconvert.h"

STR_CONST(kTrue, "true");
STR_CONST(kFalse, "false");
STR_CONST(kYes, "yes");
STR_CONST(kNo, "no");
STR_CONST(kOne, "1");
STR_CONST(kZero, "0");
STR_CONST(kInf, "inf");
STR_CONST(kNan, "nan");
STR_CONST(kWS, " \t\r\n\v\f");

// Mark the scan as failed here. The first failure is the one that gets recorded: once ok
// is false every entry point returns immediately, so a later call cannot overwrite the
// position the grammar actually went wrong at.
static bool scFail(strscan* sc)
{
    if (sc->ok) {
        sc->ok     = false;
        sc->errpos = sc->pos;
    }
    return false;
}

// Record [off, end) as the span the most recent extraction produced, hand it to the
// caller if they asked for a string, and move the cursor to `next`. `next` is separate
// from `end` because a terminator is often consumed without being part of the result.
static bool scTake(strscan* sc, string* out, int32 off, int32 end, int32 next)
{
    sc->_spanoff = off;
    sc->_spanlen = end - off;

    if (out)
        strSubStr(out, sc->s, off, end);

    sc->pos = next;
    return true;
}

_Use_decl_annotations_
void _strscInit(strscan* sc, strref s, flags_t flags)
{
    sc->s        = s;
    sc->pos      = 0;
    sc->errpos   = -1;
    sc->ok       = true;
    sc->flags    = flags;
    sc->_len     = (int32)strLen(s);
    sc->_spanoff = 0;
    sc->_spanlen = 0;
}

_Use_decl_annotations_
bool strscFinish(strscan* sc)
{
    bool ok = sc->ok;

    sc->s    = NULL;
    sc->_len = 0;

    return ok;
}

_Use_decl_annotations_
void strscRewind(strscan* sc, int32 mark)
{
    sc->pos    = clamp(mark, 0, sc->_len);
    sc->ok     = true;
    sc->errpos = -1;
}

_Use_decl_annotations_
bool strscSeek(strscan* sc, int32 pos)
{
    if (!sc->ok)
        return false;

    if (pos < 0 || pos > sc->_len)
        return scFail(sc);

    sc->pos = pos;
    return true;
}

_Use_decl_annotations_
void strscFail(strscan* sc)
{
    scFail(sc);
}

// literals and characters ---------------------------------------------------------------

_Use_decl_annotations_
bool strscTry(strscan* sc, strref lit)
{
    if (!sc->ok)
        return false;

    uint32 n = strLen(lit);
    if (n == 0)
        return true;   // an empty literal is already matched
    if (sc->pos + (int32)n > sc->_len)
        return false;

    bool match = (sc->flags & STRSC_CaseInsensitive) ?
        strRangeEqi(sc->s, lit, sc->pos, n) :
        strRangeEq(sc->s, lit, sc->pos, n);
    if (!match)
        return false;

    sc->pos += (int32)n;
    return true;
}

_Use_decl_annotations_
bool strscLit(strscan* sc, strref lit)
{
    if (!sc->ok)
        return false;

    return strscTry(sc, lit) ? true : scFail(sc);
}

_Use_decl_annotations_
bool strscTryChar(strscan* sc, char ch)
{
    if (!sc->ok || sc->pos >= sc->_len)
        return false;

    uint8 c = strGetChar(sc->s, sc->pos);
    if (sc->flags & STRSC_CaseInsensitive) {
        if (tolower(c) != tolower((uint8)ch))
            return false;
    } else if (c != (uint8)ch) {
        return false;
    }

    sc->pos++;
    return true;
}

_Use_decl_annotations_
bool strscChar(strscan* sc, char ch)
{
    if (!sc->ok)
        return false;

    return strscTryChar(sc, ch) ? true : scFail(sc);
}

_Use_decl_annotations_
bool strscPeek(strscan* sc, strref lit)
{
    if (!sc->ok)
        return false;

    uint32 n = strLen(lit);
    if (n == 0)
        return true;
    if (sc->pos + (int32)n > sc->_len)
        return false;

    return (sc->flags & STRSC_CaseInsensitive) ?
        strRangeEqi(sc->s, lit, sc->pos, n) :
        strRangeEq(sc->s, lit, sc->pos, n);
}

_Use_decl_annotations_
uint8 strscPeekChar(strscan* sc)
{
    if (!sc->ok || sc->pos >= sc->_len)
        return 0;

    return strGetChar(sc->s, sc->pos);
}

_Use_decl_annotations_
bool strscWS(strscan* sc)
{
    if (!sc->ok)
        return false;

    int32 end = strFindNotAny(sc->s, sc->pos, kWS);
    sc->pos   = (end == -1) ? sc->_len : end;
    return true;
}

_Use_decl_annotations_
bool strscWS1(strscan* sc)
{
    if (!sc->ok)
        return false;

    int32 start = sc->pos;
    strscWS(sc);
    return (sc->pos > start) ? true : scFail(sc);
}

// extraction ----------------------------------------------------------------------------

_Use_decl_annotations_
bool strscToken(strscan* sc, string* out, strref delims)
{
    if (!sc->ok)
        return false;

    int32 end = (sc->flags & STRSC_CaseInsensitive) ?
        strFindAnyi(sc->s, sc->pos, delims) :
        strFindAny(sc->s, sc->pos, delims);
    if (end == -1)
        end = sc->_len;

    if (end == sc->pos)
        return scFail(sc);   // a token has to have something in it

    return scTake(sc, out, sc->pos, end, end);
}

_Use_decl_annotations_
bool strscUntil(strscan* sc, string* out, strref text)
{
    if (!sc->ok)
        return false;

    int32 end = (sc->flags & STRSC_CaseInsensitive) ?
        strFindi(sc->s, sc->pos, text) :
        strFind(sc->s, sc->pos, text);
    if (end == -1)
        return scFail(sc);

    return scTake(sc, out, sc->pos, end, end);
}

_Use_decl_annotations_
bool strscWhile(strscan* sc, string* out, strref chars)
{
    if (!sc->ok)
        return false;

    int32 end = (sc->flags & STRSC_CaseInsensitive) ?
        strFindNotAnyi(sc->s, sc->pos, chars) :
        strFindNotAny(sc->s, sc->pos, chars);
    if (end == -1)
        end = sc->_len;

    if (end == sc->pos)
        return scFail(sc);

    return scTake(sc, out, sc->pos, end, end);
}

_Use_decl_annotations_
bool strscQuoted(strscan* sc, string* out)
{
    if (!sc->ok)
        return false;

    int32 start = sc->pos;
    if (strGetChar(sc->s, sc->pos) != '"')
        return scFail(sc);

    // Two passes rather than one: find the closing quote first, so a run with no
    // terminator leaves the cursor where it started instead of half-consumed.
    int32 i        = sc->pos + 1;
    bool escaped   = false;
    int32 closing  = -1;
    bool anyescape = false;

    for (; i < sc->_len; i++) {
        uint8 c = strGetChar(sc->s, i);
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped   = true;
            anyescape = true;
            continue;
        }
        if (c == '"') {
            closing = i;
            break;
        }
    }

    if (closing == -1) {
        sc->pos = start;
        return scFail(sc);
    }

    sc->_spanoff = start + 1;
    sc->_spanlen = closing - (start + 1);
    sc->pos      = closing + 1;

    if (!out)
        return true;

    if (!anyescape) {
        strSubStr(out, sc->s, start + 1, closing);
        return true;
    }

    strClear(out);
    escaped = false;
    for (i = start + 1; i < closing; i++) {
        uint8 c = strGetChar(sc->s, i);
        if (!escaped && c == '\\') {
            escaped = true;
            continue;
        }
        escaped = false;
        strAppendBytes(out, &c, 1);
    }

    return true;
}

_Use_decl_annotations_
bool strscLine(strscan* sc, string* out)
{
    if (!sc->ok)
        return false;

    if (sc->pos >= sc->_len)
        return scFail(sc);   // nothing left, not even an empty line

    int32 lf = strFindChar(sc->s, sc->pos, '\n');
    if (lf == -1)
        return scTake(sc, out, sc->pos, sc->_len, sc->_len);

    int32 end = lf;
    if (end > sc->pos && strGetChar(sc->s, end - 1) == '\r')
        end--;

    return scTake(sc, out, sc->pos, end, lf + 1);
}

_Use_decl_annotations_
bool strscRest(strscan* sc, string* out)
{
    if (!sc->ok)
        return false;

    return scTake(sc, out, sc->pos, sc->_len, sc->_len);
}

_Use_decl_annotations_
void strscSpan(strscan* sc, int32* off, int32* len)
{
    *off = sc->_spanoff;
    *len = sc->_spanlen;
}

// typed values --------------------------------------------------------------------------

// Consume a run of digits in `base` at the cursor, accumulating into *out. Returns how
// many digits were consumed, and reports separately whether the value outran 64 bits --
// the digits are eaten either way, so the caller sees the same cursor position whether
// the number was merely too big or perfectly fine.
static int32 scanDigits(strscan* sc, int base, uint64* out, bool* ovf)
{
    uint64 acc = 0;
    int32 n    = 0;
    *ovf       = false;

    striter it;
    striBorrow(&it, sc->s);
    striSeek(&it, sc->pos, STRI_BYTE, STRI_SET);

    uint8 c;
    while (striPeekChar(&it, &c)) {
        int d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'A' && c <= 'Z')
            d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'z')
            d = c - 'a' + 10;
        else
            break;

        if (d >= base)
            break;

        if (acc > (MAX_UINT64 - (uint64)d) / (uint64)base)
            *ovf = true;
        else
            acc = acc * (uint64)base + (uint64)d;

        n++;
        it.cursor++;
    }

    sc->pos += n;
    *out = acc;
    return n;
}

// Common body for the signed and unsigned readers. `limit` is the largest magnitude the
// destination can hold, which for a signed type depends on the sign that was just read.
static bool scanInt(strscan* sc, int base, bool allowsign, bool* neg, uint64* out, uint64 poslimit,
                    uint64 neglimit)
{
    if (!sc->ok)
        return false;

    if (base < 2 || base > 36)
        return scFail(sc);

    int32 start = sc->pos;
    *neg        = false;

    if (allowsign) {
        uint8 c = strscPeekChar(sc);
        if (c == '-' || c == '+') {
            *neg = (c == '-');
            sc->pos++;
        }
    }

    bool ovf;
    if (scanDigits(sc, base, out, &ovf) == 0 || ovf || *out > (*neg ? neglimit : poslimit)) {
        sc->pos = start;
        return scFail(sc);
    }

    sc->_spanoff = start;
    sc->_spanlen = sc->pos - start;
    return true;
}

_Use_decl_annotations_
bool strscInt64(strscan* sc, int64* out, int base)
{
    bool neg;
    uint64 acc;

    if (!scanInt(sc, base, true, &neg, &acc, (uint64)MAX_INT64, (uint64)MAX_INT64 + 1))
        return false;

    // Negate as unsigned so the most negative value has a well-defined arithmetic path
    *out = (int64)(neg ? (uint64)0 - acc : acc);
    return true;
}

_Use_decl_annotations_
bool strscInt32(strscan* sc, int32* out, int base)
{
    bool neg;
    uint64 acc;

    if (!scanInt(sc, base, true, &neg, &acc, (uint64)MAX_INT32, (uint64)MAX_INT32 + 1))
        return false;

    *out = (int32)(uint32)(neg ? (uint64)0 - acc : acc);
    return true;
}

_Use_decl_annotations_
bool strscUInt64(strscan* sc, uint64* out, int base)
{
    bool neg;
    return scanInt(sc, base, false, &neg, out, MAX_UINT64, MAX_UINT64);
}

_Use_decl_annotations_
bool strscUInt32(strscan* sc, uint32* out, int base)
{
    bool neg;
    uint64 acc;

    if (!scanInt(sc, base, false, &neg, &acc, MAX_UINT32, MAX_UINT32))
        return false;

    *out = (uint32)acc;
    return true;
}

// Delimit a floating point number at the cursor: an optional sign, then either one of the
// special words or digits with an optional fraction and exponent. Returns the offset just
// past the number, or -1 if there is not one here. Nothing is consumed.
static int32 delimitFloat(strscan* sc)
{
    int32 i = sc->pos;

    if (i < sc->_len && (strGetChar(sc->s, i) == '-' || strGetChar(sc->s, i) == '+'))
        i++;

    if (strRangeEqi(sc->s, kInf, i, 3) || strRangeEqi(sc->s, kNan, i, 3))
        return i + 3;

    int32 digits = 0;
    while (i < sc->_len && isdigit(strGetChar(sc->s, i))) {
        i++;
        digits++;
    }

    if (i < sc->_len && strGetChar(sc->s, i) == '.') {
        i++;
        while (i < sc->_len && isdigit(strGetChar(sc->s, i))) {
            i++;
            digits++;
        }
    }

    if (digits == 0)
        return -1;

    // An exponent only counts if it actually has digits; "1e" is the number 1 followed by
    // a letter, not a malformed float.
    if (i < sc->_len && (strGetChar(sc->s, i) == 'e' || strGetChar(sc->s, i) == 'E')) {
        int32 e = i + 1;
        if (e < sc->_len && (strGetChar(sc->s, e) == '-' || strGetChar(sc->s, e) == '+'))
            e++;
        if (e < sc->_len && isdigit(strGetChar(sc->s, e))) {
            while (e < sc->_len && isdigit(strGetChar(sc->s, e))) e++;
            i = e;
        }
    }

    return i;
}

_Use_decl_annotations_
bool strscFloat64(strscan* sc, float64* out)
{
    if (!sc->ok)
        return false;

    int32 end = delimitFloat(sc);
    if (end == -1)
        return scFail(sc);

    string tmp = 0;
    strSubStr(&tmp, sc->s, sc->pos, end);
    bool ok = strToFloat64(out, tmp, STRNUM_NoTrailing | STRNUM_NoWS);
    strDestroy(&tmp);

    if (!ok)
        return scFail(sc);

    sc->_spanoff = sc->pos;
    sc->_spanlen = end - sc->pos;
    sc->pos      = end;
    return true;
}

_Use_decl_annotations_
bool strscFloat32(strscan* sc, float32* out)
{
    float64 d;
    if (!strscFloat64(sc, &d))
        return false;

    *out = (float32)d;
    return true;
}

_Use_decl_annotations_
bool strscBool(strscan* sc, bool* out)
{
    if (!sc->ok)
        return false;

    int32 start = sc->pos;
    int32 end   = start;
    while (end < sc->_len && isalnum(strGetChar(sc->s, end))) end++;

    if (end == start)
        return scFail(sc);

    uint32 n = (uint32)(end - start);
    if (strRangeEqi(sc->s, kTrue, start, n) || strRangeEqi(sc->s, kYes, start, n) ||
        strRangeEq(sc->s, kOne, start, n)) {
        *out = true;
    } else if (strRangeEqi(sc->s, kFalse, start, n) || strRangeEqi(sc->s, kNo, start, n) ||
               strRangeEq(sc->s, kZero, start, n)) {
        *out = false;
    } else {
        return scFail(sc);
    }

    sc->_spanoff = start;
    sc->_spanlen = (int32)n;
    sc->pos      = end;
    return true;
}

_Use_decl_annotations_
bool _strscVal(strscan* sc, stype st, stgeneric* out)
{
    if (!sc->ok)
        return false;

    if (!st)
        return scFail(sc);

    switch (st->id) {
    case stTypeId(int8):
    case stTypeId(int16):
    case stTypeId(int32): {
        int32 v;
        if (!strscInt32(sc, &v, 10))
            return false;
        if (st->id == stTypeId(int8)) {
            if (v < MIN_INT8 || v > MAX_INT8)
                return scFail(sc);
            out->st_int8 = (int8)v;
        } else if (st->id == stTypeId(int16)) {
            if (v < MIN_INT16 || v > MAX_INT16)
                return scFail(sc);
            out->st_int16 = (int16)v;
        } else {
            out->st_int32 = v;
        }
        return true;
    }
    case stTypeId(uint8):
    case stTypeId(uint16):
    case stTypeId(uint32): {
        uint32 v;
        if (!strscUInt32(sc, &v, 10))
            return false;
        if (st->id == stTypeId(uint8)) {
            if (v > MAX_UINT8)
                return scFail(sc);
            out->st_uint8 = (uint8)v;
        } else if (st->id == stTypeId(uint16)) {
            if (v > MAX_UINT16)
                return scFail(sc);
            out->st_uint16 = (uint16)v;
        } else {
            out->st_uint32 = v;
        }
        return true;
    }
    case stTypeId(int64):
        return strscInt64(sc, &out->st_int64, 10);
    case stTypeId(uint64):
        return strscUInt64(sc, &out->st_uint64, 10);
    case stTypeId(float32):
        return strscFloat32(sc, &out->st_float32);
    case stTypeId(float64):
        return strscFloat64(sc, &out->st_float64);
    case stTypeId(bool):
        return strscBool(sc, &out->st_bool);
    default:
        break;
    }

    // Anything else reads a word and lets the type system make sense of it, which is how
    // the scanner picks up SUIDs, strings and every custom type with a string conversion.
    string tok = 0;
    if (!strscToken(sc, &tok, kWS)) {
        strDestroy(&tok);
        return false;
    }

    bool ok;
    if (st->id == stTypeId(string)) {
        strDup(&out->st_string, tok);
        ok = true;
    } else {
        ok = _stConvert(st, out, stType(string), stArg(string, tok), 0);
    }

    strDestroy(&tok);
    return ok ? true : scFail(sc);
}
