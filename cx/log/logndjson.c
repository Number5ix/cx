// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/format.h>
#include <cx/serialize/jsonout.h>
#include <cx/string.h>

// Newline-delimited JSON, one object per record.
//
// This is the serializer the split exists for. Nothing here knows where the bytes go, so the
// same code produces a rotating .ndjson file, a line on a terminal, or -- eventually -- the body
// of an Elasticsearch _bulk request. Interning is deliberately absent: an NDJSON line has to be
// self-contained, which is a property of this transport-facing format rather than of the record
// model.
//
// It builds the line directly into a string rather than through JSONOut. A log line is a handful
// of scalars, and standing up a stream buffer per record to write them would cost more than the
// line itself; the escaping, which is the part worth sharing, comes from jsonStrEscape().

STR_CONST(kNdOpen, "{\"time\":\"");
STR_CONST(kNdLevel, "\",\"level\":\"");
STR_CONST(kNdSeq, "\",\"seq\":");
STR_CONST(kNdChan, ",\"chan\":\"");
STR_CONST(kNdSample, ",\"sample\":");
STR_CONST(kNdMsg, ",\"msg\":\"");
STR_CONST(kNdColon, "\":");
STR_CONST(kNdNull, "null");
STR_CONST(kNdTrue, "true");
STR_CONST(kNdFalse, "false");
STR_CONST(kNdObjFmt, "${object}");

static void ndQuoted(_Inout_ string* out, _In_opt_ strref val)
{
    strAppendChar(out, '"');
    jsonStrEscape(out, val, 0);
    strAppendChar(out, '"');
}

// Emits one argument as a JSON value. Numbers and booleans go out bare; everything else is
// rendered to text and quoted, which keeps the output valid for any type a caller can log
// without this file having to know about that type.
static void ndValue(_Inout_ string* out, _In_ const stvar* v)
{
    stype st   = stvarType(v);
    uint32 id  = st->id;
    string tmp = 0;
    bool done  = false;

    // The scalar types are keyed off the id's class and subtype rather than enumerated, because
    // intptr/uintptr/size are aliases of whichever fixed-width type matches the platform and
    // would collide in a switch.
    if (STYPE_CLASS(id) == STCLASS_BASIC) {
        switch (STYPE_SUBTYPE(id)) {
        case STST_INT: {
            int64 iv = 0;
            switch (stGetSize(st)) {
            case 1: iv = v->data.st_int8; break;
            case 2: iv = v->data.st_int16; break;
            case 4: iv = v->data.st_int32; break;
            default: iv = v->data.st_int64; break;
            }
            done = strFromInt64(&tmp, iv, 10);
            break;
        }
        case STST_UINT: {
            if (id == STypeId_bool) {
                strAppend(out, v->data.st_bool ? kNdTrue : kNdFalse);
                return;
            }
            uint64 uv = 0;
            switch (stGetSize(st)) {
            case 1: uv = v->data.st_uint8; break;
            case 2: uv = v->data.st_uint16; break;
            case 4: uv = v->data.st_uint32; break;
            default: uv = v->data.st_uint64; break;
            }
            done = strFromUInt64(&tmp, uv, 10);
            break;
        }
        case STST_FLOAT:
            done = strFromFloat64(&tmp,
                                  (stGetSize(st) == 4) ? (float64)v->data.st_float32
                                                       : v->data.st_float64);
            break;
        default:
            break;
        }

        if (done) {
            strAppend(out, tmp);
            strDestroy(&tmp);
            return;
        }
        strDestroy(&tmp);
    }

    switch (id) {
    case STypeId_string:
        ndQuoted(out, v->data.st_string);
        return;
    case STypeId_object:
        // LogSnapshot and anything else Formattable; the value was already rendered at the call
        // site, so this is just reading it back out
        if (_strFormat(&tmp, kNdObjFmt, 1, (stvar*)v))
            ndQuoted(out, tmp);
        else
            strAppend(out, kNdNull);
        strDestroy(&tmp);
        return;
    default:
        break;
    }

    // last resort: anything with a conversion to string
    stgeneric dest = { 0 };
    if (_stConvert(stType(string), &dest, st, v->data, 0)) {
        ndQuoted(out, dest.st_string);
        strDestroy(&dest.st_string);
    } else {
        strAppend(out, kNdNull);
    }
}

static void logNdjsonSerialize(_Inout_ string* out, _In_ const LogRecord* rec,
                               _In_opt_ void* userdata)
{
    LogNdjsonConfig* cfg = (LogNdjsonConfig*)userdata;

    string tmp = 0, msg = 0;

    // a structured record always carries a timestamp; LOG_OmitDate is a text-layout choice and
    // does not get to blank the field out from under a consumer sharing one flags word
    logFormatDate(&tmp, LOG_DateISO, cfg->flags & ~LOG_OmitDate, rec->timestamp);
    strDup(out, kNdOpen);
    jsonStrEscape(out, tmp, 0);

    strAppend(out, kNdLevel);
    jsonStrEscape(out, LogLevelNames[rec->level], 0);

    strAppend(out, kNdSeq);
    strFromUInt64(&tmp, rec->seq, 10);
    strAppend(out, tmp);

    if (rec->chan && !strEmpty(rec->chan->path)) {
        strAppend(out, kNdChan);
        jsonStrEscape(out, rec->chan->path, 0);
        strAppendChar(out, '"');
    }

    // Only when the channel was actually being sampled: a "sample":1 on every record of every
    // unsampled log is noise, and its absence means the same thing.
    if (rec->sample > 1) {
        strAppend(out, kNdSample);
        strFromUInt64(&tmp, rec->sample, 10);
        strAppend(out, tmp);
    }

    // TODO: Add a serializer flag to suppress the message field, for structured-only logs
    logRecordRender(&msg, rec);
    strAppend(out, kNdMsg);
    jsonStrEscape(out, msg, 0);
    strAppendChar(out, '"');

    // Context fields come before the arguments so that an argument sharing a key wins in any
    // parser that takes the last occurrence -- the call site is more specific than the enclosing
    // request.
    for (const LogCtx* c = rec->ctx; c; c = logCtxParent(c)) {
        const stvar* vars = logCtxVars(c);
        uint32 n          = logCtxNumVars(c);

        for (uint32 i = 0; i < n; i++) {
            const char* key = stvarKey(&vars[i]);
            if (!key || logCtxShadowed(rec->ctx, c, i, key))
                continue;

            strAppendChar(out, ',');
            strAppendChar(out, '"');
            jsonStrEscape(out, (strref)key, 0);
            strAppend(out, kNdColon);
            ndValue(out, &vars[i]);
        }
    }

    // Only keyed arguments become fields. An unkeyed argument is positional and belongs to the
    // template, so it is already inside msg; emitting it again under a made-up name would invent
    // a field the call site never declared.
    for (int i = 0; i < rec->nargs; i++) {
        const char* key = stvarKey(&rec->args[i]);
        if (!key)
            continue;

        strAppendChar(out, ',');
        strAppendChar(out, '"');
        jsonStrEscape(out, (strref)key, 0);
        strAppend(out, kNdColon);
        ndValue(out, &rec->args[i]);
    }

    strAppendChar(out, '}');

    strDestroy(&msg);
    strDestroy(&tmp);
}

static void logNdjsonClose(_In_opt_ void* userdata)
{
    xaFree(userdata);
}

_Use_decl_annotations_
LogSerializer* logNdjsonSerializer(LogNdjsonConfig* config)
{
    LogNdjsonConfig* cfg = xaAllocStruct(LogNdjsonConfig, XA_Zero);
    if (config)
        *cfg = *config;

    return logSerializerCreate(logNdjsonSerialize, logNdjsonClose, cfg);
}
