#include "format_private.h"

// try to parse the next variable
bool _fmtExtractVar(FMTContext *ctx)
{
    int32 eatchar = 0;
    string(frag);

    // "loop" to handle escaped start sequences
retry_start:
    ctx->vstart = strFind(ctx->fmt, ctx->vend, _S"${");
    if (ctx->vstart == -1) {        // no more vars
        strDestroy(&frag);
        return true;
    }

    if (ctx->vstart > ctx->vend && strGetChar(ctx->fmt, ctx->vstart - 1) == '`') {
        // it's escaped, keep searching
        if (!(ctx->vstart > ctx->vend && strGetChar(ctx->fmt, ctx->vstart - 2) == '`')) {
            // skip over the backtick
            strSubStr(&frag, ctx->fmt, ctx->vend, ctx->vstart - 1);
            strAppend(&ctx->dest, frag);
            strAppend(&ctx->dest, _S"${");
            ctx->vend = (ctx->vstart += 2);
            goto retry_start;
        }
        // unless it isn't, but then we have to eat one of the backticks...
        eatchar = 1;
    }

    // add any text between last variable (or start) and this one
    strSubStr(&frag, ctx->fmt, ctx->vend, ctx->vstart - eatchar);
    strAppend(&ctx->dest, frag);

    // now find the end
    eatchar = 0;
    ctx->vend = ctx->vstart + 1;

retry_end:
    ctx->vend = strFind(ctx->fmt, ctx->vend, _S"}");
    if (ctx->vend == -1) {          // broken format string
        strDestroy(&frag);
        return false;
    }

    if (strGetChar(ctx->fmt, ctx->vend - 1) == '`') {
        // it's escaped
        if (strGetChar(ctx->fmt, ctx->vend - 2) != '`') {
            // skip over the backtick
            strSubStr(&frag, ctx->fmt, ctx->vstart + 2, ctx->vend - 1);
            strAppend(&ctx->v.var, frag);
            ctx->vstart = (ctx->vend++) - 2;
            goto retry_end;
        }
        // not really escaped, have to eat the backtick
        eatchar = 1;
    }

    strSubStr(&frag, ctx->fmt, ctx->vstart + 2, ctx->vend - eatchar);
    strAppend(&ctx->v.var, frag);
    ctx->vend++;

    strDestroy(&frag);
    return true;
}

static bool fmtParseOpt(FMTContext *ctx, strref opt, int32 vtype)
{
    if (strEq(opt, _S"left"))
        ctx->v.flags |= FMTVar_Left;
    else if (strEq(opt, _S"center"))
        ctx->v.flags |= FMTVar_Center;
    else if (strEq(opt, _S"right"))
        ctx->v.flags |= FMTVar_Right;
    else if (strEq(opt, _S"upper"))
        ctx->v.flags |= FMTVar_Upper;
    else if (strEq(opt, _S"lower"))
        ctx->v.flags |= FMTVar_Lower;
    else if (_fmtTypeParseOpt[vtype])
        return _fmtTypeParseOpt[vtype](&ctx->v, opt);
    return false;
}

bool _fmtParseVar(FMTContext *ctx)
{
    int32 vtstart = 0, vtend = 0, vnend = 0, fostart = 0, foend = 0, xstart = 0, xend = 0, defstart = 0;
    enum { X_None, X_Array, X_Hash } xtype = X_None;
    int phase = 0;
    int vtype = -1;

    int32 len = strLen(ctx->v.var);
    for (int32 i = 0; i < len; i++) {
        char ch = strGetChar(ctx->v.var, i);
        switch (phase) {
        case 0:
            if (i == 0 && ch == '0') {
                ctx->v.flags |= FMTVar_LeadingZeros;
                vtstart = 1;
            } else if (i == 0 && ch == '-') {
                ctx->v.flags |= FMTVar_SignPrefix;
                vtstart = 1;
            } else if (i == 0 && ch == '+') {
                ctx->v.flags |= FMTVar_SignAlways;
                vtstart = 1;
            } else if (isdigit(ch) && vtend == 0 && !foend) {
                vtend = i;
            } else if (ch == '(') {
                if (vtend == 0)
                    vtend = i;
                vnend = i;
                fostart = i + 1;
                phase = 1;
            } else if (ch == ';') {
                if (vtend == 0)
                    vtend = i;
                if (vnend == 0)
                    vnend = i;
                defstart = i + 1;
                phase = 3;
            } else if (ch == ')' || ch == ',') {
                return false;
            } else if (ch == '[' || ch == ':') {
                if (vtend == 0)
                    vtend = i;
                if (vnend == 0)
                    vnend = i;
                xstart = i + 1;
                xtype = (ch == '[') ? X_Array : X_Hash;
                phase = 2;
            } else if (foend > 0) {
                return false;
            }
            break;
        case 1:
            if (ch == ')') {
                foend = i;
                phase = 0;
            }
            if (ch == ';')
                return false;
            break;
        case 2:
            if (ch == ']') {
                xend = i;
            } else if (ch == ';') {
                if (xend == 0)
                    xend = i;
                defstart = i + 1;
                phase = 3;
            } else if (xend > 0)
                return false;
            break;
        }
    }

    if (phase == 1)
        return false;

    if (xstart > 0 && xend == 0)
        xend = len;
    if (vtend == 0)
        vtend = len;
    if (vnend == 0)
        vnend = len;

    string(frag);

    // check if we have default first, because it will be a fallback in case of parse error
    if (defstart > 0) {
        strSubStr(&ctx->v.def, ctx->v.var, defstart, len);
    }
    bool ret = !strEmpty(ctx->v.def);

    // first extract the type and check it
    strSubStr(&frag, ctx->v.var, vtstart, vtend);
    for (int i = 0; i < FMT_count; i++) {
        if (strEq(frag, _fmtTypeNames[i])) {
            vtype = i;
            break;
        }
    }

    // didn't find a matching type name
    if (vtype == -1)
        goto out;

    if (vtend != vnend) {
        // have a number after the type name
        strSubStr(&frag, ctx->v.var, vtend, vnend);
        if (!strToInt32(&ctx->v.idx, frag, 10, true))
            goto out;
    }

    // format options?
    if (fostart > 0) {
        strSubStr(&frag, ctx->v.var, fostart, foend);
        strSplit(&ctx->v.fmtopts, frag, _S",", false);
        // look for all-numeric width
        for (int32 i = saSize(&ctx->v.fmtopts) - 1; i >= 0; --i) {
            int32 w;
            if (strToInt32(&w, ctx->v.fmtopts[i], 10, true)) {
                if (ctx->v.width != -1)
                    goto out;           // already have one!
                ctx->v.width = w;
                saRemove(&ctx->v.fmtopts, i);
            } else {
                fmtParseOpt(ctx, ctx->v.fmtopts[i], vtype);
            }
        }
    }

    // handle the 'extra' (array index and/or hash key)
    if (xstart > 0 && xend == 0)
        xend = len;
    if (xtype == X_Array) {
        // these can be the same for [], which is legal
        if (xstart != xend) {
            strSubStr(&frag, ctx->v.var, xstart, xend);
            if (!strToInt32(&ctx->v.arrayidx, frag, 10, true))
                goto out;
        } else {
            ctx->v.arrayidx = ctx->arrayidx++;
        }
    } else if (xtype == X_Hash) {
        strSubStr(&ctx->v.hashkey, ctx->v.var, xstart, xend);
    }

    if (_fmtTypeParseFinalize[vtype] &&
        !_fmtTypeParseFinalize[vtype](&ctx->v))
        goto out;

    // vtype being set means parsing succeeded, otherwise use default
    ctx->v.vtype = vtype;
    ret = true;

out:
    strDestroy(&frag);
    return ret;
}
