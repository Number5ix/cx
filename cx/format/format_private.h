#pragma once

#include "format.h"
#include "cx/container.h"
#include "cx/string.h"

enum FORMAT_TYPES {
    FMT_string,
    FMT_int,
    FMT_uint,
    FMT_float,
    FMT_ptr,
    FMT_suid,
    FMT_object,
    FMT_count
};
extern string _fmtTypeNames[FMT_count];
extern uint8 _fmtTypeIdMask[FMT_count][2];
extern bool(*_fmtTypeParseOpt[FMT_count])(FMTVar *v, string opt);
extern bool(*_fmtTypeParseFinalize[FMT_count])(FMTVar *v);
extern bool(*_fmtTypeFormat[FMT_count])(FMTVar *v, string *out);

typedef struct FMTContext {
    int32 nargs;
    stvariant *args;
    int32 startarg[FMT_count];      // where to start searching for each type
    int32 arrayidx;                 // arrary index counter

    string dest;                    // output string so far

    string fmt;                     // format string parse state
    int32 vstart;
    int32 vend;
    int32 flen;

    FMTVar v;                       // current variable
} FMTContext;

bool _fmtExtractVar(FMTContext *ctx);
bool _fmtParseVar(FMTContext *ctx);
bool _fmtFindData(FMTContext *ctx);
void _fmtFormat(FMTContext *ctx);

// type-specific formatters
bool _fmtParseStringOpt(FMTVar *v, string opt);
bool _fmtString(FMTVar *v, string *out);
bool _fmtParseIntOpt(FMTVar *v, string opt);
bool _fmtParseIntFinalize(FMTVar *v);
bool _fmtInt(FMTVar *v, string *out);
bool _fmtParseFloatOpt(FMTVar *v, string opt);
bool _fmtParseFloatFinalize(FMTVar *v);
bool _fmtFloat(FMTVar *v, string *out);
bool _fmtParsePtrOpt(FMTVar *v, string opt);
bool _fmtParsePtrFinalize(FMTVar *v);
bool _fmtPtr(FMTVar *v, string *out);
bool _fmtSUID(FMTVar *v, string *out);
bool _fmtObject(FMTVar *v, string *out);
