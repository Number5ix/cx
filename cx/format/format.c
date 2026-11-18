#include "cx/string/string_private.h"
#include "format_private.h"

// literal strings with embedded length for maximum efficiency
STR_CONSTR(kFmtString, "string");
STR_CONSTR(kFmtBool, "bool");
STR_CONSTR(kFmtInt, "int");
STR_CONSTR(kFmtUint, "uint");
STR_CONSTR(kFmtFloat, "float");
STR_CONSTR(kFmtPtr, "ptr");
STR_CONSTR(kFmtSuid, "suid");
STR_CONSTR(kFmtObject, "object");

strref _fmtTypeNames[FMT_count] = { _SR(kFmtString), _SR(kFmtBool), _SR(kFmtInt),
                                    _SR(kFmtUint),   _SR(kFmtFloat), _SR(kFmtPtr),
                                    _SR(kFmtSuid),   _SR(kFmtObject) };

uint32 _fmtTypeIdMask[FMT_count][2] = {
    { stTypeId(string),           0xffffffff }, // string
    { stTypeId(bool),             0xffffffff }, // bool
    { STCLASS_BASIC | STST_INT,   0xffffff00 }, // int
    { STCLASS_BASIC | STST_UINT,  0xffffff00 }, // uint
    { STCLASS_BASIC | STST_FLOAT, 0xffffff00 }, // float
    { STCLASS_BASIC | STST_PTR,   0xffffff00 }, // ptr
    { stTypeId(suid),             0xffffffff }, // suid
    { stTypeId(object),           0xffffffff }, // object
};

bool (*_fmtTypeParseOpt[FMT_count])(FMTVar* v, strref opt) = {
    _fmtParseStringOpt,
    0,   // bool
    _fmtParseIntOpt,
    _fmtParseIntOpt,
    _fmtParseFloatOpt,
    _fmtParsePtrOpt,
    0,   // suid
    _fmtParseObjectOpt,
};

bool (*_fmtTypeParseFinalize[FMT_count])(FMTVar* v) = {
    0,   // string
    0,   // bool
    _fmtParseIntFinalize,
    _fmtParseIntFinalize,
    _fmtParseFloatFinalize,
    _fmtParsePtrFinalize,
    0,   // suid
    0,   // object
};

bool (*_fmtTypeFormat[FMT_count])(FMTVar* v, string* out) = {
    _fmtString, _fmtBool, _fmtInt, _fmtInt, _fmtFloat, _fmtPtr, _fmtSUID, _fmtObject,
};

static void fmtVarReset(FMTVar* v)
{
    v->vtype    = -1;
    v->idx      = -1;
    v->width    = -1;
    v->arrayidx = -1;
    v->flags    = 0;
    v->data     = 0;
    v->type     = 0;
    memset(&v->fmtdata, 0, sizeof(v->fmtdata));
    saClear(&v->fmtopts);
    strClear(&v->var);
    strClear(&v->def);
    strClear(&v->hashkey);
    strClear(&v->key);
}

static void fmtVarCreate(FMTVar* v) {}

static void fmtVarDestroy(FMTVar* v)
{
    saDestroy(&v->fmtopts);
    strDestroy(&v->tmp);
    strDestroy(&v->var);
    strDestroy(&v->def);
    strDestroy(&v->hashkey);
    strDestroy(&v->key);
}

_Use_decl_annotations_
bool _strFormat(string* out, strref fmt, int n, stvar* args)
{
    if (!(out && fmt))
        return false;

    // pre-allocate space in output string
    // this is a ROUGH GUESS and string growth may still be necessary
    uint32 fmtlen = strLen(fmt);
    _strReset(out, fmtlen + (fmtlen >> 3));

    FMTContext ctx = { 0 };
    bool ret       = true;
    ctx.flen       = strLen(fmt);
    ctx.nargs      = n;
    ctx.args       = args;
    ctx.dest       = out;
    fmtVarCreate(&ctx.v);

    // Crazy optimizations -- in performance profiling, strFormat spends a lot of time in
    // cStrLen. This is because most format strings are literals that don't have an
    // embedded length, so strFind ends up counting the characters every time it needs
    // to check the length.
    if (fmtlen < 64) {
        // for small ones we just copy it to the stack
        strTemp(&ctx.fmt, fmtlen);
        strDup(&ctx.fmt, fmt);
    } else {
        // For longer strings, we create a fake rope structure on the stack that wraps the
        // literal, so the string API knows the length up front. Kids don't try this at home!

        uint8 newhdr         = STR_CX | STR_ALLOC | STR_STACK | STR_ROPE | STR_LEN32;
        ctx.fmt              = (string)stackAlloc(_strOffStr(newhdr) + sizeof(str_ropedata));
        *(uint8*)ctx.fmt     = newhdr;
        ((uint8*)ctx.fmt)[1] = 0xc1;   // magic string header
        _strSetLen(ctx.fmt, fmtlen);
        _strInitRef(ctx.fmt);
        str_ropedata* ropedata = _strRopeData(ctx.fmt);
        ropedata->depth        = 0;
        ropedata->left.str     = (string)fmt;
        ropedata->left.len     = fmtlen;
        ropedata->left.off     = 0;
        ropedata->right.str    = NULL;
        ropedata->right.len    = 0;
        ropedata->right.off    = 0;
    }

    // For performance, strFormat tries very hard to avoid allocating a bunch of strings
    // on the heap. These temporary strings live in the stack of the strFormat() function
    // and passed down to the various parsing and formatting functions through the
    // context structure. They are reused for each variable that is parsed.
    strTemp(&ctx.v.var, 64);
    strTemp(&ctx.v.def, 64);
    strTemp(&ctx.v.hashkey, 32);
    strTemp(&ctx.v.key, 32);
    strTemp(&ctx.v.tmp, 128);
    strTemp(&ctx.tmp, 128);

    // main format string scan loop
    for (;;) {
        fmtVarReset(&ctx.v);
        ctx.hasdef = false;
        if (!_fmtExtractVar(&ctx)) {
            ret = false;   // unterminated variable swallows the rest of the string
            break;
        }
        if (strEmpty(ctx.v.var))
            break;

        if (!_fmtParseVar(&ctx) || !_fmtFindData(&ctx))
            ret = false;

        _fmtFormat(&ctx);
    }

    // add any text after the last variable
    if (ctx.vend < ctx.flen) {
        strSubStr(&ctx.v.tmp, fmt, ctx.vend, strEnd);
        strAppend(ctx.dest, ctx.v.tmp);
    }

    fmtVarDestroy(&ctx.v);
    strDestroy(&ctx.tmp);
    return ret;
}
