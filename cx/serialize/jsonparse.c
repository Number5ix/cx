#include "jsonparse.h"

#include <cx/format.h>
#include <cx/string.h>
#include <cx/string/string_private_utf8.h>
#include <cx/utils/compare.h>

// string constants
STR_CONST(kJPErrInvalidChar, "Invalid character code 0x${uint(hex)} in string");
STR_CONST(kJPErrInvalidHex, "Invalid hex digit '${int(utfchar)}");
STR_CONST(kJPErrSurrogateFirst, "Invalid first half of surrogate pair");
STR_CONST(kJPErrSurrogateSecond, "Invalid second half of surrogate pair");
STR_CONST(kJPErrSurrogateMissing, "Surrogate pair missing second half");
STR_CONST(kJPErrInvalidEscape, "Invalid escape sequence '\\${int(utfchar)}");
STR_CONST(kJPErrUnterminatedStr, "Unterminated string");
STR_CONST(kJPErrExpectedNumber, "Found '${int(utfchar)}' where a number was expected");
STR_CONST(kJPErrRecursionLimit, "Exceeded recursion limit");
STR_CONST(kJPErrExpectedCommaObj,
          "Expected ',' after member \"${string}\" of object, got '${int(utfchar)}' instead");
STR_CONST(kJPErrExpectedKey, "Expected '\"' to begin object key, got '${int(utfchar)}' instead");
STR_CONST(kJPErrExpectedColon,
          "Expected ':' in member \"${string}\" of object, got '${int(utfchar)}' instead");
STR_CONST(kJPErrExpectedCommaArr,
          "Expected ',' after index ${int} of array, got '${int(utfchar)} instead");
STR_CONST(kJPErrUnterminatedArr, "Unterminated array");
STR_CONST(kJPErrFmt, "JSON Parse Error: ${string} on line ${int}");
STR_CONST(kJPErrSyntaxFmt, "JSON Parse Error: Invalid syntax on line ${int}");

#define MAX_PARSE_DEPTH 1000

// State machine phases stored in JSONParseContext.phase
typedef enum JSONParsePhase {
    JP_Start,            // Initial state
    JP_Top_Value,        // Expecting top-level value
    JP_Done,             // Value parsed, deliver End next
    JP_Error,            // Error delivered, deliver End next
    JP_End,              // End delivered, next call returns false

    JP_Obj_FirstOrEnd,   // After '{': expecting key or '}'
    JP_Obj_Key,          // Parse key string
    JP_Obj_Colon,        // Expecting ':'
    JP_Obj_Value,        // Parse value for current key
    JP_Obj_NextOrEnd,    // Expecting ',' or '}'

    JP_Arr_FirstOrEnd,   // After '[': expecting value or ']'
    JP_Arr_Value,        // Parse array element value
    JP_Arr_NextOrEnd,    // Expecting ',' or ']'
} JSONParsePhase;

static void jsonCtxDestroy(_Pre_valid_ _Post_invalid_ JSONParseContext* ctx)
{
    if (ctx->ctype == JSON_Object) {
        strDestroy(&ctx->cdata.curKey);
    }

    xaFree(ctx);
}

static void jsonClearEvent(_Inout_ JSONParseEvent* ev)
{
    if (ev->etype == JSON_Object_Key || ev->etype == JSON_String || ev->etype == JSON_Error) {
        strDestroy(&ev->edata.strData);
    }
    memset(&ev->edata, 0, sizeof(ev->edata));
    ev->etype = JSON_Parse_Unknown;
    ev->ctx   = NULL;
}

#define SCRATCH_SZ 32
#define outAppend(ch)                    \
    scratch[scratchp++] = ch;            \
    if (scratchp == SCRATCH_SZ) {        \
        strAppend(out, (string)scratch); \
        scratchp = 0;                    \
    }

static bool parseString(_Inout_ StreamBuffer* sb, _Inout_ string* out, _Inout_ JSONParseState* ps)
{
    bool ret                     = false;
    char scratch[SCRATCH_SZ + 1] = { 0 };
    int scratchp                 = 0;

    bool escape = false;
    int useq    = 0;     // digits reamining in unicode hex escape sequence
    int32 ucp;           // unicode code point being built
    int32 lastucp = 0;   // for surrogate pairs
    unsigned char ch;

    strClear(out);

    size_t didread;
    while (sbufCRead(sb, &ch, 1, &didread)) {
        if (ch < 0x20) {
            strFormat(&ps->errmsg, kJPErrInvalidChar, stvar(uint8, ch));
            break;
        }

        if (useq > 0) {
            // Unicode character hex escape sequence in progress
            int32 hexval = 0;
            uint8 utf[5];
            int utflen;

            useq--;
            if (ch >= '0' && ch <= '9')
                hexval = ch - '0';
            else if (ch >= 'a' && ch <= 'f')
                hexval = 10 + ch - 'a';
            else if (ch >= 'A' && ch <= 'F')
                hexval = 10 + ch - 'A';
            else {
                strFormat(&ps->errmsg, kJPErrInvalidHex, stvar(int32, ch));
                break;
            }

            ucp |= hexval << (useq * 4);

            if (useq == 0 && lastucp == 0) {
                if (ucp <= 0xd7ff || ucp >= 0xe000) {
                    // BMP
                    utflen = _strUTF8Encode(utf, ucp);
                    for (int i = 0; i < utflen; i++) {
                        outAppend(utf[i]);
                    }
                } else if (ucp >= 0xd800 && ucp <= 0xdbff) {
                    // first half of a surrogate pair
                    lastucp = (ucp & 0x3ff) << 10;
                    ucp     = 0;
                } else {
                    // these are supposed to be for the second half!
                    strDup(&ps->errmsg, kJPErrSurrogateFirst);
                    break;
                }
            } else if (useq == 0) {
                if (ucp >= 0xdc00 && ucp <= 0xdfff) {
                    // second half of a surrogate pair
                    ucp    = lastucp | (ucp & 0x3ff) | 0x10000;
                    utflen = _strUTF8Encode(utf, ucp);
                    for (int i = 0; i < utflen; i++) {
                        outAppend(utf[i]);
                    }
                    lastucp = 0;
                } else {
                    strDup(&ps->errmsg, kJPErrSurrogateSecond);
                    break;
                }
            }
        } else if (escape) {
            escape = false;

            if (lastucp && ch != 'u') {
                // first half of surrogate pair is REQUIRED to be immediately followed up with a
                // second \uXXXX
                strDup(&ps->errmsg, kJPErrSurrogateMissing);
                break;
            }

            switch (ch) {
            case '"':
                outAppend('"');
                break;
            case '\\':
                outAppend('\\');
                break;
            case '/':
                outAppend('/');
                break;
            case 'b':
                outAppend('\b');
                break;
            case 'f':
                outAppend('\f');
                break;
            case 'n':
                outAppend('\n');
                break;
            case 'r':
                outAppend('\r');
                break;
            case 't':
                outAppend('\t');
                break;
            case 'u':
                ucp  = 0;
                useq = 4;
                break;
            default:
                strFormat(&ps->errmsg, kJPErrInvalidEscape, stvar(int32, ch));
                break;
            }
        } else if (ch == '"') {
            ret = true;
            // end of the string
            break;
        } else {
            if (ch == '\\') {
                escape = true;
            } else {
                if (lastucp) {
                    // first half of surrogate pair is REQUIRED to be immediately followed up with a
                    // second \uXXXX
                    strDup(&ps->errmsg, kJPErrSurrogateMissing);
                    break;
                }

                outAppend(ch);
            }
        }
    }

    if (!ret && sbufCAvail(sb) == 0) {
        strDup(&ps->errmsg, kJPErrUnterminatedStr);
    }

    // copy any leftover output
    if (scratchp > 0) {
        scratch[scratchp] = '\0';
        strAppend(out, (string)scratch);
    }
    return ret;
}

_Success_(return) static bool parseNumInt(_Inout_ StreamBuffer* sb, bool neg, size_t intoff, size_t intlen,
                                   _Out_ int64* out, _Inout_ JSONParseState* ps)
{
    int64 res  = 0;
    int64 mult = 1;
    unsigned char ch;

    if (intlen > 19)
        return false;   // can't possibly fit in an int64

    for (size_t i = 0; i < intlen; ++i) {
        // should be impossible to fail since we've already read it once
        sbufCPeek(sb, &ch, intoff + i, 1);
        ch -= '0';

        if (i == intlen - 1) {
            // overflow check
            if (!neg && (res > 922337203685477580 || (res == 922337203685477580 && ch > 7)))
                return false;
            if (neg && (res < -922337203685477580 || (res == -922337203685477580 && ch > 8)))
                return false;
        }

        res *= 10;
        if (!neg)
            res += ch * mult;
        else
            res -= ch * mult;
    }

    *out = res;
    return true;
}

_Success_(return) static bool
parseNumFloat(_Inout_ StreamBuffer* sb, bool neg, size_t intoff, size_t intlen, size_t fracoff,
              size_t fraclen, bool expneg, size_t expoff, size_t explen, _Out_ double* out,
              _Inout_ JSONParseState* ps)
{
    size_t total = max(max(intoff + intlen, fracoff + fraclen), expoff + explen);
    char* buf    = xaAlloc(total + 1);
    buf[total]   = '\0';
    if (!sbufCPeek(sb, (uint8*)buf, 0, total))
        return false;
    bool ret = strToFloat64(out, (strref)buf, true);
    xaFree(buf);
    return ret;
}

static bool parseNumber(_Inout_ StreamBuffer* sb, _Inout_ JSONParseEvent* ev,
                        _Inout_ JSONParseState* ps)
{
    unsigned char ch;
    int phase      = 0;
    size_t off     = 0;
    size_t intoff  = 0;
    size_t intlen  = 0;
    size_t fracoff = 0;
    size_t fraclen = 0;
    size_t expoff  = 0;
    size_t explen  = 0;
    bool neg = false, expneg = false;

    // phases:
    // 0 negative or initial digit
    // 1 integer digits
    // 2 fractional digits
    // 3 exponent sign or first digit
    // 4 exponent digits

    // first, split out the components into separate pieces
    while (sbufCFeed(sb, off + 1) && sbufCPeek(sb, &ch, off++, 1)) {
        if (phase > 0 && (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t' || ch == ',')) {
            // all done
            // don't want to actually consume this character
            --off;
            break;
        }

        if (phase == 0 && ch == '-') {
            neg    = true;
            phase  = 1;
            intoff = 1;
        } else if ((phase == 0 || phase == 1) && ch >= '0' && ch <= '9') {
            intlen++;
            phase = 1;
        } else if (phase == 1 && ch == '.') {
            phase = 2;
        } else if (phase == 2 && ch >= '0' && ch <= '9') {
            if (fracoff == 0)
                fracoff = off - 1;
            fraclen++;
        } else if ((phase == 1 || phase == 2) && (ch == 'e' || ch == 'E')) {
            phase = 3;
        } else if (phase == 3 && ch == '+') {
            // positive exponent is the default, ignore
            phase = 4;
        } else if (phase == 3 && ch == '-') {
            expneg = true;
            phase  = 4;
        } else if ((phase == 3 || phase == 4) && ch >= '0' && ch <= '9') {
            if (expoff == 0)
                expoff = off - 1;
            explen++;
            phase = 4;
        } else {
            strFormat(&ps->errmsg, kJPErrExpectedNumber, stvar(int32, ch));
            return false;
        }
    }

    // now actually parse the numbers
    // see if we can parse it as a simple integer
    if (fraclen == 0 && explen == 0 &&
        parseNumInt(sb, neg, intoff, intlen, &ev->edata.intData, ps)) {
        ev->etype = JSON_Int;
        sbufCSkip(sb, off);
        return true;
    } else if (parseNumFloat(sb,
                             neg,
                             intoff,
                             intlen,
                             fracoff,
                             fraclen,
                             expneg,
                             expoff,
                             explen,
                             &ev->edata.floatData,
                             ps)) {
        ev->etype = JSON_Float;
        sbufCSkip(sb, off);
        return true;
    }

    return false;
}

static void skipWS(_Inout_ StreamBuffer* sb, _Inout_ JSONParseState* ps)
{
    unsigned char ch;

    while (sbufCFeed(sb, 1) && sbufCPeek(sb, &ch, 0, 1)) {
        switch (ch) {
        case '\n':
            ps->line++;
            // FALLTHROUGH
        case '\r':
        case '\t':
        case ' ':
            // whitespace, ignore
            sbufCSkip(sb, 1);
            break;
        default:
            return;
        }
    }
}

static void pushCtx(_Inout_ JSONParseState* ps, int newctype, int newphase)
{
    JSONParseContext* ctx = xaAlloc(sizeof(JSONParseContext), XA_Zero);
    ctx->ctype            = newctype;
    ctx->phase            = newphase;
    ctx->parent           = ps->ctx;
    ps->ctx               = ctx;
    ps->depth++;
}

static void popCtx(_Inout_ JSONParseState* ps)
{
    JSONParseContext* ctx = ps->ctx;
    ps->ctx               = ps->ctx->parent;
    ps->depth--;

    jsonCtxDestroy(ctx);
}

// After finishing a value, set the parent context's phase to its "after value" state
static void advanceAfterValue(_Inout_ JSONParseState* ps)
{
    JSONParseContext* ctx = ps->ctx;
    switch (ctx->ctype) {
    case JSON_Top:
        ctx->phase = JP_Done;
        break;
    case JSON_Object:
        ctx->phase = JP_Obj_NextOrEnd;
        break;
    case JSON_Array:
        ctx->phase = JP_Arr_NextOrEnd;
        break;
    }
}

// Pop the current container context and emit an End event for it.
// ev->ctx is set to the parent context (the one we're returning to).
static JSONParseEvent* finishContainer(_Inout_ JSONParseState* ps, JsonEventType etype)
{
    jsonClearEvent(&ps->ev);
    ps->ev.etype = etype;
    popCtx(ps);
    ps->ev.ctx = ps->ctx;
    skipWS(ps->sb, ps);
    advanceAfterValue(ps);
    return &ps->ev;
}

// Parse the next value token and set up the event.
// For containers, pushes a new context and yields Begin.
// For scalars, parses completely and yields the value event.
// Returns true if an event is ready, false on error.
static bool stepValue(_Inout_ JSONParseState* ps)
{
    StreamBuffer* sb = ps->sb;
    uint8 tmp[4];
    unsigned char ch;

    jsonClearEvent(&ps->ev);
    skipWS(sb, ps);

    if (!(sbufCFeed(sb, 1) && sbufCPeek(sb, &ch, 0, 1)))
        return false;

    switch (ch) {
    case '{':
        sbufCSkip(sb, 1);
        ps->ev.etype = JSON_Object_Begin;
        ps->ev.ctx   = ps->ctx;

        if (ps->depth >= MAX_PARSE_DEPTH) {
            strDup(&ps->errmsg, kJPErrRecursionLimit);
            return false;
        }

        pushCtx(ps, JSON_Object, JP_Obj_FirstOrEnd);
        return true;
    case '[':
        sbufCSkip(sb, 1);
        ps->ev.etype = JSON_Array_Begin;
        ps->ev.ctx   = ps->ctx;

        if (ps->depth >= MAX_PARSE_DEPTH) {
            strDup(&ps->errmsg, kJPErrRecursionLimit);
            return false;
        }

        pushCtx(ps, JSON_Array, JP_Arr_FirstOrEnd);
        return true;
    case '"':
        sbufCSkip(sb, 1);
        if (!parseString(sb, &ps->ev.edata.strData, ps))
            return false;
        ps->ev.etype = JSON_String;
        ps->ev.ctx   = ps->ctx;
        skipWS(sb, ps);
        advanceAfterValue(ps);
        return true;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '-':
        if (!parseNumber(sb, &ps->ev, ps))
            return false;
        ps->ev.ctx = ps->ctx;
        skipWS(sb, ps);
        advanceAfterValue(ps);
        return true;
    case 't':
        if (!sbufCFeed(sb, 4) || !sbufCPeek(sb, tmp, 1, 3) || memcmp(tmp, "rue", 3) != 0)
            return false;
        sbufCSkip(sb, 4);
        ps->ev.etype = JSON_True;
        ps->ev.ctx   = ps->ctx;
        skipWS(sb, ps);
        advanceAfterValue(ps);
        return true;
    case 'f':
        if (!sbufCFeed(sb, 5) || !sbufCPeek(sb, tmp, 1, 4) || memcmp(tmp, "alse", 4) != 0)
            return false;
        sbufCSkip(sb, 5);
        ps->ev.etype = JSON_False;
        ps->ev.ctx   = ps->ctx;
        skipWS(sb, ps);
        advanceAfterValue(ps);
        return true;
    case 'n':
        if (!sbufCFeed(sb, 4) || !sbufCPeek(sb, tmp, 1, 3) || memcmp(tmp, "ull", 3) != 0)
            return false;
        sbufCSkip(sb, 4);
        ps->ev.etype = JSON_Null;
        ps->ev.ctx   = ps->ctx;
        skipWS(sb, ps);
        advanceAfterValue(ps);
        return true;
    default:
        return false;
    }
}

_Use_decl_annotations_
bool jsonParseInit(JSONParseState* state, StreamBuffer* sb)
{
    memset(state, 0, sizeof(*state));
    state->sb   = sb;
    state->line = 1;

    if (!sbufCRegisterPull(sb, NULL, NULL))
        return false;

    state->ctx        = xaAlloc(sizeof(JSONParseContext), XA_Zero);
    state->ctx->ctype = JSON_Top;
    state->ctx->phase = JP_Top_Value;
    return true;
}

_Use_decl_annotations_
JSONParseEvent* jsonParseNext(JSONParseState* state)
{
    StreamBuffer* sb = state->sb;
    unsigned char ch;
    size_t didread;

    for (;;) {
        JSONParseContext* ctx = state->ctx;

        switch (ctx->phase) {
        case JP_Top_Value:
            if (!stepValue(state))
                goto set_error;

            // stepValue set up the event; yield it
            return &state->ev;

        case JP_Done:
        case JP_Error:
            // Deliver End event
            jsonClearEvent(&state->ev);
            state->ev.etype = JSON_End;
            state->ev.ctx   = state->ctx;
            state->success  = (ctx->phase == JP_Done);
            ctx->phase      = JP_End;
            return &state->ev;

        case JP_End:
            return NULL;

        case JP_Obj_FirstOrEnd:
            skipWS(sb, state);
            if (!(sbufCFeed(sb, 1) && sbufCPeek(sb, &ch, 0, 1)))
                goto set_error;

            if (ch == '}') {
                sbufCSkip(sb, 1);
                return finishContainer(state, JSON_Object_End);
            }
            ctx->phase = JP_Obj_Key;
            continue;

        case JP_Obj_Key:
            skipWS(sb, state);
            if (!sbufCRead(sb, &ch, 1, &didread) || ch != '"') {
                strFormat(&state->errmsg, kJPErrExpectedKey, stvar(int32, ch));
                goto set_error;
            }

            if (!parseString(sb, &ctx->cdata.curKey, state))
                goto set_error;

            jsonClearEvent(&state->ev);
            state->ev.etype = JSON_Object_Key;
            strDup(&state->ev.edata.strData, ctx->cdata.curKey);
            state->ev.ctx = ctx;
            ctx->phase    = JP_Obj_Colon;
            return &state->ev;

        case JP_Obj_Colon:
            skipWS(sb, state);
            if (!sbufCRead(sb, &ch, 1, &didread) || ch != ':') {
                strFormat(&state->errmsg,
                          kJPErrExpectedColon,
                          stvar(strref, ctx->cdata.curKey),
                          stvar(int32, ch));
                goto set_error;
            }
            ctx->phase = JP_Obj_Value;
            continue;

        case JP_Obj_Value:
            if (!stepValue(state))
                goto set_error;

            return &state->ev;

        case JP_Obj_NextOrEnd:
            skipWS(sb, state);
            if (!(sbufCFeed(sb, 1) && sbufCPeek(sb, &ch, 0, 1)))
                goto set_error;

            if (ch == '}') {
                sbufCSkip(sb, 1);
                return finishContainer(state, JSON_Object_End);
            } else if (ch == ',') {
                sbufCSkip(sb, 1);
                ctx->phase = JP_Obj_Key;
                continue;
            } else {
                strFormat(&state->errmsg,
                          kJPErrExpectedCommaObj,
                          stvar(strref, ctx->cdata.curKey),
                          stvar(int32, ch));
                goto set_error;
            }

        case JP_Arr_FirstOrEnd:
            skipWS(sb, state);
            if (!(sbufCFeed(sb, 1) && sbufCPeek(sb, &ch, 0, 1))) {
                strDup(&state->errmsg, kJPErrUnterminatedArr);
                goto set_error;
            }

            if (ch == ']') {
                sbufCSkip(sb, 1);
                return finishContainer(state, JSON_Array_End);
            }
            ctx->phase = JP_Arr_Value;
            continue;

        case JP_Arr_Value:
            if (!stepValue(state))
                goto set_error;

            return &state->ev;

        case JP_Arr_NextOrEnd:
            skipWS(sb, state);
            if (!(sbufCFeed(sb, 1) && sbufCPeek(sb, &ch, 0, 1))) {
                strDup(&state->errmsg, kJPErrUnterminatedArr);
                goto set_error;
            }

            if (ch == ']') {
                sbufCSkip(sb, 1);
                return finishContainer(state, JSON_Array_End);
            } else if (ch == ',') {
                sbufCSkip(sb, 1);
                ctx->cdata.curIdx++;
                ctx->phase = JP_Arr_Value;
                continue;
            } else {
                strFormat(&state->errmsg,
                          kJPErrExpectedCommaArr,
                          stvar(int32, ctx->cdata.curIdx),
                          stvar(int32, ch));
                goto set_error;
            }

        default:
            return false;
        }
    }

set_error:
    // Unwind to root context for error delivery
    while (state->ctx->parent) popCtx(state);

    // Deliver the error event immediately
    jsonClearEvent(&state->ev);
    state->ev.etype = JSON_Error;
    state->ev.ctx   = state->ctx;
    if (!strEmpty(state->errmsg)) {
        strFormat(&state->ev.edata.strData,
                  kJPErrFmt,
                  stvar(strref, state->errmsg),
                  stvar(int32, state->line));
    } else {
        strFormat(&state->ev.edata.strData, kJPErrSyntaxFmt, stvar(int32, state->line));
    }
    state->ctx->phase = JP_Error;
    return &state->ev;
}

_Use_decl_annotations_
void jsonParseDestroy(JSONParseState* state)
{
    jsonClearEvent(&state->ev);

    // Clean up remaining context stack
    while (state->ctx) {
        JSONParseContext* parent = state->ctx->parent;
        jsonCtxDestroy(state->ctx);
        state->ctx = parent;
    }

    strDestroy(&state->errmsg);
    if (state->sb)
        sbufCFinish(state->sb);
}

_Use_decl_annotations_
bool jsonParse(StreamBuffer* sb, jsonParseCB callback, void* userdata)
{
    if (!callback)
        return false;

    JSONParseState state;
    if (!jsonParseInit(&state, sb))
        return false;

    while (jsonParseNext(&state)) {
        callback(&state.ev, userdata);
    }

    bool ret = state.success;
    jsonParseDestroy(&state);
    return ret;
}
