// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/format.h>
#include <cx/string.h>

// The serializer half of a destination. This file is only the plumbing -- the serializers
// themselves are in logtext.c and logndjson.c.

_Use_decl_annotations_
LogSerializer* logSerializerCreate(LogSerializeFunc serialize, LogSerializerClose close,
                                   void* userdata)
{
    LogSerializer* ser = xaAllocStruct(LogSerializer, XA_Zero);
    ser->serialize     = serialize;
    ser->close         = close;
    ser->userdata      = userdata;
    return ser;
}

_Use_decl_annotations_
void logSerializerDestroy(LogSerializer** ser)
{
    if (!*ser)
        return;

    if ((*ser)->close)
        (*ser)->close((*ser)->userdata);
    xaDestroy(ser);
}

_Use_decl_annotations_
void logSerialize(string* out, LogSerializer* ser, const LogRecord* rec)
{
    // A transport that was given no serializer still has to produce something, and the record's
    // own rendering is the only answer that needs no configuration.
    if (!ser || !ser->serialize) {
        logRecordRender(out, rec);
        return;
    }

    ser->serialize(out, rec, ser->userdata);
}

STR_CONST(kLogVarObjFmt, "${object}");

_Use_decl_annotations_
void logVarText(string* out, const stvar* v)
{
    stype st = stvarType(v);

    // Objects go through the formatter because that is where Formattable lives -- and by the time
    // a serializer sees one it is a LogSnapshot anyway, already rendered at the call site.
    // Everything else has a conversion to string or has no text form at all.
    if (stEq(st, stType(object))) {
        if (!_strFormat(out, kLogVarObjFmt, 1, (stvar*)v))
            strClear(out);
        return;
    }

    stgeneric dest = { 0 };
    if (_stConvert(stType(string), &dest, st, v->data, 0)) {
        strDup(out, dest.st_string);
        strDestroy(&dest.st_string);
    } else {
        strClear(out);
    }
}
