// JSON backend.
//
// The first backend that has to lie. SSD stores stvars and round-trips the data model exactly;
// JSON has one number type, no byte string, and string-only object keys, so every place the
// model is wider than JSON needs a defined projection rather than an improvised one. Those are
// the rules in serjson.h, and they are the substance of this file -- the plumbing around them
// is thin, because JSONOut and JSONParseState already speak in events that line up with the
// data model almost node for node.

#include "cx/serialize/serjson.h"

#include "cx/format.h"
#include "cx/serialize/jsonout.h"
#include "cx/serialize/jsonparse.h"
#include "cx/string.h"
#include "cx/xalloc/xalloc.h"

#include <math.h>

// The range a JSON consumer that only has doubles can hold exactly. Past it an integer goes out
// as a decimal string: cx would read its own output back either way, but silently rounding
// somebody else's parse is exactly the kind of loss a defined projection exists to prevent.
#define JSON_INT_EXACT ((int64)1 << 53)

// ---------------------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------------------

// Wrappers are the one place JSON needs the writer to remember anything. A type tag and a
// reference definition are both *prefix* nodes in the data model, but `{"$type":…,"$value":…}` is
// a container around the value, so the closing brace can only be emitted once the value it wraps
// is complete -- which means tracking how deep the value went. `depth` counts open containers and
// `wrapdepth` holds the depth each pending wrapper is waiting to see again.
typedef struct SerJsonWriter {
    SerWriter w;   // must be first
    JSONOut* jo;
    int32 depth;
    sa_int32 wrapdepth;

    // A reference definition waits here for the value it names. Section 11 puts `"$id": N` inside
    // that value's object rather than in a wrapper around it, which reads far better and is what
    // makes a `$ref` point at something a consumer can see -- but it means the id cannot be
    // emitted until the value has begun and turned out to be an object.
    bool haveid;
    uint32 pendingid;
} SerJsonWriter;

static flags_t jsonOutOptions(flags_t flags)
{
    if (flags & SER_JSON_Pretty)
        return JSON_Pretty;
    if (flags & SER_JSON_Compact)
        return JSON_Minimal;
    return JSON_Single_Line;
}

static bool jwEmit(_Inout_ SerJsonWriter* jw, _In_ JSONParseEvent* ev)
{
    if (!jw->jo)
        return false;   // the create failure is already recorded

    if (!jsonOut(jw->jo, ev))
        return serWriterFail(&jw->w, SER_Err_Backend, _SL("JSON output failed"));

    return true;
}

static bool jwSimple(_Inout_ SerJsonWriter* jw, JsonEventType t)
{
    JSONParseEvent ev = { .etype = t };
    return jwEmit(jw, &ev);
}

static bool jwStr(_Inout_ SerJsonWriter* jw, JsonEventType t, _In_opt_ strref s)
{
    JSONParseEvent ev = { .etype = t, .edata.strData = (string)s };
    return jwEmit(jw, &ev);
}

static bool jwInt(_Inout_ SerJsonWriter* jw, int64 v)
{
    JSONParseEvent ev = { .etype = JSON_Int, .edata.intData = v };
    return jwEmit(jw, &ev);
}

static bool jwReal(_Inout_ SerJsonWriter* jw, float64 v)
{
    JSONParseEvent ev = { .etype = JSON_Float, .edata.floatData = v };
    return jwEmit(jw, &ev);
}

// Closes out every wrapper whose value has just finished. A wrapper opened at depth D is done
// when the writer is back at depth D with a value emitted, and closing it drops the depth again
// -- which is why this loops rather than closing one.
static bool jwValueDone(_Inout_ SerJsonWriter* jw)
{
    while (saSize(jw->wrapdepth) > 0 && jw->wrapdepth.a[saSize(jw->wrapdepth) - 1] == jw->depth) {
        saSetSize(&jw->wrapdepth, saSize(jw->wrapdepth) - 1);
        jw->depth--;
        if (!jwSimple(jw, JSON_Object_End))
            return false;
    }
    return true;
}

// Opens a wrapper object around the value about to be written, and records the depth its closing
// brace is owed at. Shared by `$type` and by the `$id` form that could not become a key.
static bool jwWrapBegin(_Inout_ SerJsonWriter* jw, _In_ strref key, _In_ JSONParseEvent* val)
{
    jw->depth++;
    if (!(jwSimple(jw, JSON_Object_Begin) && jwStr(jw, JSON_Object_Key, key) && jwEmit(jw, val) &&
          jwStr(jw, JSON_Object_Key, _S "$value")))
        return false;

    saPush(&jw->wrapdepth, int32, jw->depth);
    return true;
}

// Discharges a pending `$id` at the front of a value that is about to be written and is not an
// object -- a `Serializable` class that writes an array, say. There is nowhere to put a key, so
// the value takes the same wrapper a type tag would give it.
static bool jwPendingId(_Inout_ SerJsonWriter* jw)
{
    if (!jw->haveid)
        return true;
    jw->haveid = false;

    JSONParseEvent ev = { .etype = JSON_Int, .edata.intData = (int64)jw->pendingid };
    return jwWrapBegin(jw, _S "$id", &ev);
}

// A complete scalar: emit it, then see whether it finished a wrapper.
static bool jwScalarDone(_Inout_ SerJsonWriter* jw, bool ok) { return ok && jwValueDone(jw); }

static bool jsonWNull(SerWriter* w)
{
    SerJsonWriter* jw = (SerJsonWriter*)w;
    return jwPendingId(jw) && jwScalarDone(jw, jwSimple(jw, JSON_Null));
}

static bool jsonWBool(SerWriter* w, bool v)
{
    SerJsonWriter* jw = (SerJsonWriter*)w;
    return jwPendingId(jw) && jwScalarDone(jw, jwSimple(jw, v ? JSON_True : JSON_False));
}

static bool jsonWInt(SerWriter* w, int64 v, stype declared)
{
    SerJsonWriter* jw = (SerJsonWriter*)w;

    if (!jwPendingId(jw))
        return false;

    if (v <= -JSON_INT_EXACT || v >= JSON_INT_EXACT) {
        string tmp = 0;
        strFromInt64(&tmp, v, 10);
        bool ret = jwStr(jw, JSON_String, tmp);
        strDestroy(&tmp);
        return jwScalarDone(jw, ret);
    }

    return jwScalarDone(jw, jwInt(jw, v));
}

static bool jsonWUint(SerWriter* w, uint64 v, stype declared)
{
    SerJsonWriter* jw = (SerJsonWriter*)w;

    if (!jwPendingId(jw))
        return false;

    if (v >= (uint64)JSON_INT_EXACT) {
        string tmp = 0;
        strFromUInt64(&tmp, v, 10);
        bool ret = jwStr(jw, JSON_String, tmp);
        strDestroy(&tmp);
        return jwScalarDone(jw, ret);
    }

    return jwScalarDone(jw, jwInt(jw, (int64)v));
}

static bool jsonWReal(SerWriter* w, float64 v, stype declared)
{
    SerJsonWriter* jw = (SerJsonWriter*)w;

    if (!jwPendingId(jw))
        return false;

    if (isnan(v) || isinf(v)) {
        if (w->flags & SER_Strict)
            return serWriterFail(w, SER_Err_Unsupported, _SL("JSON cannot represent NaN or Inf"));
        return jwScalarDone(jw, jwSimple(jw, JSON_Null));
    }

    // A float32 widened to double prints as the double it became -- 0.1f reads out as
    // 0.10000000149011612, which round-trips but is noise. Take the shortest decimal that reads
    // back as the same float32 and hand the double nearest to *that* to the formatter: it
    // prints as the short form, and narrowing it on the way back in lands on the same float32.
    if (declared && stGetSize(declared) == 4) {
        string tmp = 0;
        float64 shortest;
        if (strFromFloat32(&tmp, (float32)v) && strToFloat64(&shortest, tmp, STRNUM_NoTrailing))
            v = shortest;
        strDestroy(&tmp);
    }

    return jwScalarDone(jw, jwReal(jw, v));
}

static bool jsonWStr(SerWriter* w, strref v)
{
    SerJsonWriter* jw = (SerJsonWriter*)w;
    return jwPendingId(jw) && jwScalarDone(jw, jwStr(jw, JSON_String, v));
}

static bool jsonWBytes(SerWriter* w, const void* p, size_t n)
{
    if (n > UINT32_MAX)
        return serWriterFail(w, SER_Err_Unsupported, _SL("byte run is too large to encode"));

    SerJsonWriter* jw = (SerJsonWriter*)w;
    if (!jwPendingId(jw))
        return false;

    string b64 = 0;
    strB64Encode(&b64, (const uint8*)p, (uint32)n, false);
    bool ret = jwStr(jw, JSON_String, b64);
    strDestroy(&b64);
    return jwScalarDone(jw, ret);
}

static bool jsonWArrBegin(SerWriter* w, int32 count)
{
    SerJsonWriter* jw = (SerJsonWriter*)w;
    if (!jwPendingId(jw))
        return false;
    jw->depth++;
    return jwSimple(jw, JSON_Array_Begin);
}

static bool jsonWArrEnd(SerWriter* w)
{
    SerJsonWriter* jw = (SerJsonWriter*)w;
    jw->depth--;
    return jwScalarDone(jw, jwSimple(jw, JSON_Array_End));
}

static bool jsonWMapBegin(SerWriter* w, int32 count)
{
    SerJsonWriter* jw = (SerJsonWriter*)w;

    jw->depth++;
    if (!jwSimple(jw, JSON_Object_Begin))
        return false;

    // The case a pending id was waiting for: an object has somewhere to put one, so the id
    // becomes its first key and costs no nesting at all.
    if (jw->haveid) {
        jw->haveid = false;
        if (!(jwStr(jw, JSON_Object_Key, _S "$id") && jwInt(jw, (int64)jw->pendingid)))
            return false;
    }

    return true;
}

static bool jsonWMapKey(SerWriter* w, strref key)
{
    return jwStr((SerJsonWriter*)w, JSON_Object_Key, key);
}

static bool jsonWMapKeyTyped(SerWriter* w, const STypeInfoExt* kt, stgeneric key)
{
    // Not advertised, so the traverser projects non-string keys to pair arrays instead.
    return serWriterFail(w, SER_Err_Unsupported, _SL("JSON object keys are strings"));
}

static bool jsonWMapEnd(SerWriter* w)
{
    SerJsonWriter* jw = (SerJsonWriter*)w;
    jw->depth--;
    return jwScalarDone(jw, jwSimple(jw, JSON_Object_End));
}

static bool jsonWTypeTag(SerWriter* w, const STypeInfoExt* st)
{
    SerJsonWriter* jw = (SerJsonWriter*)w;

    if (!st->name)
        return serWriterFail(w, SER_Err_Type, _SL("cannot tag a value with an unnamed type"));

    // The value that follows closes this out; see jwValueDone.
    JSONParseEvent ev = { .etype = JSON_String, .edata.strData = (string)st->name };
    return jwWrapBegin(jw, _S "$type", &ev);
}

// Nothing goes out yet. Where `"$id": N` belongs depends on what the value turns out to be, and
// the value has not been written; jsonWMapBegin and jwPendingId are the two places that resolve
// it. The traverser emits a definition immediately before the value it names, so there is never
// more than one waiting.
static bool jsonWRefDef(SerWriter* w, uint32 id)
{
    SerJsonWriter* jw = (SerJsonWriter*)w;
    jw->haveid        = true;
    jw->pendingid     = id;
    return true;
}

static bool jsonWRefUse(SerWriter* w, uint32 id)
{
    SerJsonWriter* jw = (SerJsonWriter*)w;

    // A use is a complete value in itself, so it never has a pending id of its own -- the
    // traverser writes a definition or a use, never both.
    return jwScalarDone(jw,
                        jwSimple(jw, JSON_Object_Begin) &&
                            jwStr(jw, JSON_Object_Key, _S "$ref") && jwInt(jw, (int64)id) &&
                            jwSimple(jw, JSON_Object_End));
}

static bool jsonWFinish(SerWriter* w)
{
    SerJsonWriter* jw = (SerJsonWriter*)w;
    if (jw->jo)
        jsonOutEnd(&jw->jo);
    return true;
}

static void jsonWDestroy(SerWriter* w)
{
    SerJsonWriter* jw = (SerJsonWriter*)w;

    // A writer destroyed without being finished still has to close out the JSONOut, even though
    // the document it produced is incomplete.
    if (jw->jo)
        jsonOutEnd(&jw->jo);

    saDestroy(&jw->wrapdepth);
    xaFree(jw);
}

static const SerWriterOps jsonWriterOps = {
    .writeNull   = jsonWNull,
    .writeBool   = jsonWBool,
    .writeInt    = jsonWInt,
    .writeUint   = jsonWUint,
    .writeReal   = jsonWReal,
    .writeStr    = jsonWStr,
    .writeBytes  = jsonWBytes,
    .arrBegin    = jsonWArrBegin,
    .arrEnd      = jsonWArrEnd,
    .mapBegin    = jsonWMapBegin,
    .mapKey      = jsonWMapKey,
    .mapKeyTyped = jsonWMapKeyTyped,
    .mapEnd      = jsonWMapEnd,
    .typeTag     = jsonWTypeTag,
    .refDef      = jsonWRefDef,
    .refUse      = jsonWRefUse,
    .finish      = jsonWFinish,
    .destroy     = jsonWDestroy,
};

_Use_decl_annotations_
SerWriter* serJsonWriterCreate(StreamBuffer* sb, flags_t flags)
{
    // No SER_Cap_Sizes: JSON writes no container counts, and no SER_Cap_ExactInt, because the
    // declared width of a number does not survive the trip.
    SerJsonWriter* jw = (SerJsonWriter*)_serWriterAlloc(sizeof(SerJsonWriter),
                                                        &jsonWriterOps,
                                                        SER_Cap_TypeTags | SER_Cap_Refs,
                                                        flags);
    saInit(&jw->wrapdepth, int32, 4);
    jw->jo = jsonOutBegin(sb, jsonOutOptions(flags));

    return &jw->w;
}

// ---------------------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------------------

// One event of lookahead, which is all the directed-read model needs: peek() answers from it and
// every read consumes it. The parser owns its event's string only until the next pull, so the
// lookahead keeps its own copy.
typedef struct SerJsonReader {
    SerReader r;   // must be first
    JSONParseState ps;
    bool have;
    JsonEventType etype;
    int64 intData;
    float64 floatData;
    string strData;

    // What the object in the lookahead actually is -- a plain map, a `$type` wrapper, a reference
    // definition or a reference use. Answering that needs a second token, which the parser's own
    // push-back queue supplies; the answer is cached because peek is cheap by contract and gets
    // called repeatedly at the same position.
    bool objchecked;
    SerNodeKind objkind;

    // Mirrors the writer: the closing brace of a wrapper is not part of the value the traverser
    // asked for, so the reader has to know when the value has finished and swallow it.
    int32 depth;
    sa_int32 wrapdepth;

    // A `"$id"` read as the first key of the object it names leaves the reader standing inside a
    // map the traverser has not opened yet. Set by jsonRRef and consumed by the mapBegin that
    // immediately follows it.
    bool inrefmap;
} SerJsonReader;

static JsonEventType jrPeek(_Inout_ SerJsonReader* jr)
{
    if (jr->have)
        return jr->etype;

    JSONParseEvent* ev = jsonParseNext(&jr->ps);
    strClear(&jr->strData);

    if (!ev) {
        jr->etype = JSON_End;
    } else {
        jr->etype = ev->etype;
        switch (ev->etype) {
        case JSON_Int:
            jr->intData = ev->edata.intData;
            break;
        case JSON_Float:
            jr->floatData = ev->edata.floatData;
            break;
        case JSON_String:
        case JSON_Object_Key:
        case JSON_Error:
            strDup(&jr->strData, ev->edata.strData);
            break;
        default:
            break;
        }
    }

    jr->have = true;
    return jr->etype;
}

static void jrTake(_Inout_ SerJsonReader* jr)
{
    jr->have       = false;
    jr->objchecked = false;
}

// Two tokens of lookahead, and the only place the reader needs them: `{` alone does not say
// whether an object is a value, a `$type` wrapper around one, an object carrying a reference id,
// or a reference standing in for one. Its first key does, and the parser's event queue takes that
// second token back, so nothing here has to reimplement buffering.
static SerNodeKind jrObjKind(_Inout_ SerJsonReader* jr)
{
    if (jr->objchecked)
        return jr->objkind;

    jr->objchecked = true;
    jr->objkind    = SER_MapBegin;

    JSONParseEvent* ev = jsonParseNext(&jr->ps);
    if (ev) {
        if (ev->etype == JSON_Object_Key) {
            if (strEq(ev->edata.strData, _S "$type"))
                jr->objkind = SER_TypeTag;
            else if (strEq(ev->edata.strData, _S "$id"))
                jr->objkind = SER_RefDef;
            else if (strEq(ev->edata.strData, _S "$ref"))
                jr->objkind = SER_RefUse;
        }
        jsonParsePush(&jr->ps, ev);
    }

    return jr->objkind;
}

// The parser reports a syntax error as an event rather than a return code, so surfacing its
// message here is the difference between a location and a usable diagnostic.
static bool jrUnexpected(_Inout_ SerJsonReader* jr, _In_ strref want)
{
    string msg = 0;

    if (jr->etype == JSON_Error) {
        strFormat(&msg,
                  _SL("JSON parse error on line ${int}: ${string}"),
                  stvar(int32, jr->ps.line),
                  stvar(string, jr->strData));
    } else if (jr->etype == JSON_End) {
        strFormat(&msg, _SL("expected ${string}, found end of document"), stvar(string, (string)want));
    } else {
        strFormat(&msg,
                  _SL("expected ${string} on line ${int}"),
                  stvar(string, (string)want),
                  stvar(int32, jr->ps.line));
    }

    bool ret = serReaderFail(&jr->r, SER_Err_Data, msg);
    strDestroy(&msg);
    return ret;
}

// Consumes one event of the given type, or fails describing what was wanted instead.
static bool jrExpect(_Inout_ SerJsonReader* jr, JsonEventType t, _In_ strref want)
{
    if (jrPeek(jr) != t)
        return jrUnexpected(jr, want);
    jrTake(jr);
    return true;
}

static SerNodeKind jsonRPeek(SerReader* r)
{
    switch (jrPeek((SerJsonReader*)r)) {
    case JSON_Null:
        return SER_Null;
    case JSON_True:
    case JSON_False:
        return SER_Bool;
    case JSON_Int:
        return SER_Int;
    case JSON_Float:
        return SER_Real;
    case JSON_String:
        return SER_Str;
    case JSON_Object_Begin:
        return jrObjKind((SerJsonReader*)r);
    case JSON_Object_Key:
        return SER_MapKey;
    case JSON_Object_End:
        return SER_MapEnd;
    case JSON_Array_Begin:
        return SER_ArrayBegin;
    case JSON_Array_End:
        return SER_ArrayEnd;
    case JSON_End:
        return SER_EOF;
    default:
        return SER_Invalid;
    }
}

// The read counterpart of jwValueDone: consumes the closing brace of every wrapper whose value
// has just finished, which is what keeps the wrapper invisible to the traverser.
static bool jrValueDone(_Inout_ SerJsonReader* jr)
{
    while (saSize(jr->wrapdepth) > 0 && jr->wrapdepth.a[saSize(jr->wrapdepth) - 1] == jr->depth) {
        saSetSize(&jr->wrapdepth, saSize(jr->wrapdepth) - 1);
        jr->depth--;
        if (!jrExpect(jr, JSON_Object_End, _S "the end of a $value wrapper"))
            return false;
    }
    return true;
}

static bool jrScalarDone(_Inout_ SerJsonReader* jr, bool ok) { return ok && jrValueDone(jr); }

static bool jsonRNull(SerReader* r)
{
    SerJsonReader* jr = (SerJsonReader*)r;
    return jrScalarDone(jr, jrExpect(jr, JSON_Null, _S "null"));
}

static bool jsonRBool(SerReader* r, bool* out)
{
    SerJsonReader* jr = (SerJsonReader*)r;
    JsonEventType t   = jrPeek(jr);

    if (t != JSON_True && t != JSON_False)
        return jrUnexpected(jr, _S "a boolean");

    *out = (t == JSON_True);
    jrTake(jr);
    return jrValueDone(jr);
}

static bool jsonRInt(SerReader* r, int64* out, stype declared)
{
    SerJsonReader* jr = (SerJsonReader*)r;

    switch (jrPeek(jr)) {
    case JSON_Int:
        *out = jr->intData;
        break;
    case JSON_String:
        // the big-integer projection: anything outside +/-2^53 went out as a decimal string
        if (!strToInt64(out, jr->strData, 10, STRNUM_NoTrailing))
            return jrUnexpected(jr, _S "an integer");
        break;
    case JSON_Float:
        // JSON has one number type, so 1e3 arrives as a float; a value with a fractional part
        // is a genuine mismatch rather than something to truncate silently.
        if (jr->floatData != floor(jr->floatData) || fabs(jr->floatData) > (float64)INT64_MAX)
            return jrUnexpected(jr, _S "an integer");
        *out = (int64)jr->floatData;
        break;
    default:
        return jrUnexpected(jr, _S "an integer");
    }

    jrTake(jr);
    return jrValueDone(jr);
}

static bool jsonRUint(SerReader* r, uint64* out, stype declared)
{
    SerJsonReader* jr = (SerJsonReader*)r;

    switch (jrPeek(jr)) {
    case JSON_Int:
        if (jr->intData < 0)
            return jrUnexpected(jr, _S "an unsigned integer");
        *out = (uint64)jr->intData;
        break;
    case JSON_String:
        if (!strToUInt64(out, jr->strData, 10, STRNUM_NoTrailing))
            return jrUnexpected(jr, _S "an unsigned integer");
        break;
    case JSON_Float:
        if (jr->floatData != floor(jr->floatData) || jr->floatData < 0 ||
            jr->floatData > (float64)UINT64_MAX)
            return jrUnexpected(jr, _S "an unsigned integer");
        *out = (uint64)jr->floatData;
        break;
    default:
        return jrUnexpected(jr, _S "an unsigned integer");
    }

    jrTake(jr);
    return jrValueDone(jr);
}

static bool jsonRReal(SerReader* r, float64* out, stype declared)
{
    SerJsonReader* jr = (SerJsonReader*)r;

    switch (jrPeek(jr)) {
    case JSON_Float:
        *out = jr->floatData;
        break;
    case JSON_Int:
        *out = (float64)jr->intData;
        break;
    case JSON_String:
        if (!strToFloat64(out, jr->strData, STRNUM_NoTrailing))
            return jrUnexpected(jr, _S "a number");
        break;
    case JSON_Null:
        // The inverse of the write rule: JSON has no spelling for NaN or Inf, so both went out
        // as null. Which of the three it was is not recoverable, and NaN is the one that keeps
        // "not a number" true.
        *out = NAN;
        break;
    default:
        return jrUnexpected(jr, _S "a number");
    }

    jrTake(jr);
    return jrValueDone(jr);
}

static bool jsonRStr(SerReader* r, string* out)
{
    SerJsonReader* jr = (SerJsonReader*)r;

    if (jrPeek(jr) != JSON_String)
        return jrUnexpected(jr, _S "a string");

    strDup(out, jr->strData);
    jrTake(jr);
    return jrValueDone(jr);
}

static bool jsonRBytes(SerReader* r, Buffer* out)
{
    SerJsonReader* jr = (SerJsonReader*)r;

    if (jrPeek(jr) != JSON_String)
        return jrUnexpected(jr, _S "a base64 string");

    uint32 sz = strB64Decode(jr->strData, NULL, 0);
    bufDestroy(out);
    *out = bufCreate(sz);
    if (sz > 0 && strB64Decode(jr->strData, (*out)->data, sz) != sz)
        return serReaderFail(r, SER_Err_Data, _SL("value is not valid base64"));
    (*out)->len = sz;

    jrTake(jr);
    return jrValueDone(jr);
}

static bool jsonRArrBegin(SerReader* r, int32* count)
{
    // JSON writes no counts, and the traverser is required to cope with that rather than
    // trusting one; -1 is how it is told.
    SerJsonReader* jr = (SerJsonReader*)r;
    *count            = -1;
    jr->depth++;
    return jrExpect(jr, JSON_Array_Begin, _S "an array");
}

static bool jsonRArrNext(SerReader* r)
{
    // The end token is left in place for arrEnd to consume, so that a false here means "no more
    // elements" and never "something went wrong".
    return jrPeek((SerJsonReader*)r) != JSON_Array_End;
}

static bool jsonRArrEnd(SerReader* r)
{
    SerJsonReader* jr = (SerJsonReader*)r;
    jr->depth--;
    return jrScalarDone(jr, jrExpect(jr, JSON_Array_End, _S "the end of an array"));
}

static bool jsonRMapBegin(SerReader* r, int32* count)
{
    SerJsonReader* jr = (SerJsonReader*)r;
    *count            = -1;

    // A reference definition opened this object already, to take the `"$id"` out of it; the
    // depth was counted there too. See jsonRRef.
    if (jr->inrefmap) {
        jr->inrefmap = false;
        return true;
    }

    jr->depth++;
    return jrExpect(jr, JSON_Object_Begin, _S "an object");
}

static bool jsonRMapNext(SerReader* r, string* key)
{
    SerJsonReader* jr = (SerJsonReader*)r;

    if (jrPeek(jr) != JSON_Object_Key)
        return false;

    strDup(key, jr->strData);
    jrTake(jr);
    return true;
}

static bool jsonRMapEnd(SerReader* r)
{
    SerJsonReader* jr = (SerJsonReader*)r;
    jr->depth--;
    return jrScalarDone(jr, jrExpect(jr, JSON_Object_End, _S "the end of an object"));
}

static bool jsonRTypeTag(SerReader* r, string* name)
{
    SerJsonReader* jr = (SerJsonReader*)r;

    jr->depth++;
    if (!jrExpect(jr, JSON_Object_Begin, _S "a $type wrapper"))
        return false;
    if (!jrExpect(jr, JSON_Object_Key, _S "$type"))
        return false;
    if (jrPeek(jr) != JSON_String)
        return jrUnexpected(jr, _S "a type name");
    strDup(name, jr->strData);
    jrTake(jr);

    if (jrPeek(jr) != JSON_Object_Key || !strEq(jr->strData, _S "$value"))
        return jrUnexpected(jr, _S "$value");
    jrTake(jr);

    // The closing brace belongs to the value that follows, not to this node; jrValueDone
    // swallows it once that value is complete.
    saPush(&jr->wrapdepth, int32, jr->depth);
    return true;
}

static bool jsonRRef(SerReader* r, uint32* id)
{
    SerJsonReader* jr = (SerJsonReader*)r;

    SerNodeKind kind = jrObjKind(jr);
    if (jrPeek(jr) != JSON_Object_Begin || (kind != SER_RefDef && kind != SER_RefUse))
        return jrUnexpected(jr, _S "a reference");
    bool isdef = kind == SER_RefDef;

    jr->depth++;
    if (!jrExpect(jr, JSON_Object_Begin, _S "a reference"))
        return false;
    // jrObjKind already matched the key by name, so only its form is left to check.
    if (!jrExpect(jr, JSON_Object_Key, isdef ? _S "$id" : _S "$ref"))
        return false;
    if (jrPeek(jr) != JSON_Int || jr->intData < 0 || jr->intData > UINT32_MAX)
        return jrUnexpected(jr, _S "a reference id");
    *id = (uint32)jr->intData;
    jrTake(jr);

    if (!isdef) {
        // A use is the whole object: `{"$ref": N}` and nothing more.
        jr->depth--;
        return jrScalarDone(jr, jrExpect(jr, JSON_Object_End, _S "the end of a $ref"));
    }

    // A definition is a prefix, and the writer put it in one of two places. `"$value"` next means
    // the value could not carry a key and took a wrapper, which unwinds exactly like a `$type`
    // one. Anything else means the id was the first key of the value's own object -- so the
    // reader is now standing inside a map the traverser has yet to open.
    if (jrPeek(jr) == JSON_Object_Key && strEq(jr->strData, _S "$value")) {
        jrTake(jr);
        saPush(&jr->wrapdepth, int32, jr->depth);
        return true;
    }

    jr->inrefmap = true;
    return true;
}

// Any reference definition inside the skipped value goes with it -- there is no object to record,
// because nothing was constructed. A later use of one of those ids fails where it is used rather
// than resolving to the wrong thing, which is what the reader's sparse ref map is for.
static bool jsonRSkip(SerReader* r)
{
    SerJsonReader* jr = (SerJsonReader*)r;
    int depth         = 0;

    for (;;) {
        JsonEventType t = jrPeek(jr);
        if (t == JSON_End || t == JSON_Error || t == JSON_Parse_Unknown)
            return jrUnexpected(jr, _S "a value to skip over");

        jrTake(jr);

        switch (t) {
        case JSON_Object_Begin:
        case JSON_Array_Begin:
            depth++;
            break;
        case JSON_Object_End:
        case JSON_Array_End:
            if (--depth == 0)
                return jrValueDone(jr);
            break;
        case JSON_Object_Key:
            break;   // a key is not a value; its value follows
        default:
            if (depth == 0)
                return jrValueDone(jr);   // a bare scalar
            break;
        }
    }
}

static void jsonRDestroy(SerReader* r)
{
    SerJsonReader* jr = (SerJsonReader*)r;

    strDestroy(&jr->strData);
    saDestroy(&jr->wrapdepth);
    jsonParseDestroy(&jr->ps);
    xaFree(jr);
}

static const SerReaderOps jsonReaderOps = {
    .peek        = jsonRPeek,
    .readNull    = jsonRNull,
    .readBool    = jsonRBool,
    .readInt     = jsonRInt,
    .readUint    = jsonRUint,
    .readReal    = jsonRReal,
    .readStr     = jsonRStr,
    .readBytes   = jsonRBytes,
    .arrBegin    = jsonRArrBegin,
    .arrNext     = jsonRArrNext,
    .arrEnd      = jsonRArrEnd,
    .mapBegin    = jsonRMapBegin,
    .mapNext     = jsonRMapNext,
    .mapEnd      = jsonRMapEnd,
    .readTypeTag = jsonRTypeTag,
    .readRef     = jsonRRef,
    .skip        = jsonRSkip,
    .destroy     = jsonRDestroy,
};

_Use_decl_annotations_
SerReader* serJsonReaderCreate(StreamBuffer* sb, flags_t flags)
{
    SerJsonReader* jr = (SerJsonReader*)_serReaderAlloc(sizeof(SerJsonReader),
                                                        &jsonReaderOps,
                                                        SER_Cap_Skip | SER_Cap_TypeTags |
                                                            SER_Cap_Refs,
                                                        flags);
    saInit(&jr->wrapdepth, int32, 4);
    if (!jsonParseInit(&jr->ps, sb))
        serReaderFail(&jr->r, SER_Err_Backend, _SL("could not begin JSON parsing"));

    return &jr->r;
}
