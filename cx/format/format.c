#include "format_private.h"

// literal strings with embedded length for maximum efficiency
string _fmtTypeNames[FMT_count] = {
    "\xE1\xC1\x06""string",
    "\xE1\xC1\x03""int",
    "\xE1\xC1\x04""uint",
    "\xE1\xC1\x05""float",
    "\xE1\xC1\x03""ptr",
    "\xE1\xC1\x04""suid",
    "\xE1\xC1\x06""object",
};

uint8 _fmtTypeIdMask[FMT_count][2] = {
    { 0xe0, 0xff },     // string
    { 0x10, 0xf0 },     // int
    { 0x20, 0xf0 },     // uint
    { 0x30, 0xf0 },     // float
    { 0x40, 0xf0 },     // ptr
    { 0xe2, 0xff },     // suid
    { 0xe1, 0xff },     // object
};

bool(*_fmtTypeParseOpt[FMT_count])(FMTVar *v, string opt) = {
    _fmtParseStringOpt,
    _fmtParseIntOpt,
    _fmtParseIntOpt,
    _fmtParseFloatOpt,
    _fmtParsePtrOpt,
    0,          // suid
    0,          // object
};

bool(*_fmtTypeParseFinalize[FMT_count])(FMTVar *v) = {
    0,          // string
    _fmtParseIntFinalize,
    _fmtParseIntFinalize,
    _fmtParseFloatFinalize,
    _fmtParsePtrFinalize,
    0,          // suid
    0,          // object
};

bool(*_fmtTypeFormat[FMT_count])(FMTVar *v, string *out) = {
    _fmtString,
    _fmtInt,
    _fmtInt,
    _fmtFloat,
    _fmtPtr,
    _fmtSUID,
    _fmtObject,
};

static void fmtVarReset(FMTVar *v)
{
    v->vtype = -1;
    v->idx = -1;
    v->width = -1;
    v->arrayidx = -1;
    v->flags = 0;
    v->data = 0;
    v->type = 0;
    memset(&v->fmtdata, 0, sizeof(v->fmtdata));
    saClear(&v->fmtopts);
    strClear(&v->var);
    strClear(&v->def);
    strClear(&v->hashkey);
}

static void fmtVarCreate(FMTVar *v)
{
    v->fmtopts = saCreate(string, 4);
}

static void fmtVarDestroy(FMTVar *v)
{
    saDestroy(&v->fmtopts);
    strDestroy(&v->var);
    strDestroy(&v->def);
    strDestroy(&v->hashkey);
}

bool _strFormat(string *out, string fmt, int n, stvariant *args)
{
    if (!(out && fmt))
        return false;

    FMTContext ctx = { 0 };
    bool ret = false;
    ctx.fmt = fmt;              // borrows caller's ref, do not modify!
    ctx.flen = strLen(fmt);
    ctx.nargs = n;
    ctx.args = args;
    fmtVarCreate(&ctx.v);

    string frag = 0;

    // main format string scan loop
    for (;;) {
        fmtVarReset(&ctx.v);
        if (!_fmtExtractVar(&ctx))
            goto out;
        if (strEmpty(ctx.v.var))
            break;

        if (!_fmtParseVar(&ctx))
            goto out;

        if (!_fmtFindData(&ctx))
            goto out;

        // everything checks out, so do it!
        _fmtFormat(&ctx);
    }

    // add any text after the last variable
    if (ctx.vend < ctx.flen) {
        strSubStr(&frag, fmt, ctx.vend, 0);
        strAppend(&ctx.dest, frag);
    }

    ret = true;

out:
    if (ret) {
        strDestroy(out);
        *out = ctx.dest;
    } else {
        strDestroy(&ctx.dest);
    }
    strDestroy(&frag);
    fmtVarDestroy(&ctx.v);
    return ret;
}
