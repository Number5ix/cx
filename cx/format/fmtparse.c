#include "format_private.h"

// try to parse the next variable
_Use_decl_annotations_
bool _fmtExtractVar(FMTContext* ctx)
{
    int32 eatchar = 0;

    // "loop" to handle escaped start sequences
retry_start:
    ctx->vstart = strFind(ctx->fmt,
                          ctx->vend,
                          (strref) "\xE1\xC1\x02"
                                   "${");
    if (ctx->vstart == -1) {   // no more vars
        return true;
    }

    if (ctx->vstart > ctx->vend && strGetChar(ctx->fmt, ctx->vstart - 1) == '`') {
        // it's escaped, keep searching
        if (!(ctx->vstart > ctx->vend && strGetChar(ctx->fmt, ctx->vstart - 2) == '`')) {
            // skip over the backtick
            strSubStr(&ctx->tmp, ctx->fmt, ctx->vend, ctx->vstart - 1);
            strAppend(ctx->dest, ctx->tmp);
            strAppend(ctx->dest,
                      (strref) "\xE1\xC1\x02"
                               "${");
            ctx->vend = (ctx->vstart += 2);
            goto retry_start;
        }
        // unless it isn't, but then we have to eat one of the backticks...
        eatchar = 1;
    }

    // add any text between last variable (or start) and this one
    strSubStr(&ctx->tmp, ctx->fmt, ctx->vend, ctx->vstart - eatchar);
    strAppend(ctx->dest, ctx->tmp);

    // now find the end
    eatchar   = 0;
    ctx->vend = ctx->vstart + 1;

retry_end:
    ctx->vend = strFind(ctx->fmt,
                        ctx->vend,
                        (strref) "\xE1\xC1\x01"
                                 "}");
    if (ctx->vend == -1) {   // broken format string
        return false;
    }

    if (strGetChar(ctx->fmt, ctx->vend - 1) == '`') {
        // it's escaped
        if (strGetChar(ctx->fmt, ctx->vend - 2) != '`') {
            // skip over the backtick
            strSubStr(&ctx->tmp, ctx->fmt, ctx->vstart + 2, ctx->vend - 1);
            strAppend(&ctx->v.var, ctx->tmp);
            ctx->vstart = (ctx->vend++) - 2;
            goto retry_end;
        }
        // not really escaped, have to eat the backtick
        eatchar = 1;
    }

    strSubStr(&ctx->tmp, ctx->fmt, ctx->vstart + 2, ctx->vend - eatchar);
    strAppend(&ctx->v.var, ctx->tmp);
    ctx->vend++;

    return true;
}

static bool fmtParseOpt(_Inout_ FMTContext* ctx, _In_ strref opt, int32 vtype)
{
    if (strEq(opt,
              (strref) "\xE1\xC1\x04"
                       "left"))
        ctx->v.flags |= FMTVar_Left;
    else if (strEq(opt,
                   (strref) "\xE1\xC1\x06"
                            "center"))
        ctx->v.flags |= FMTVar_Center;
    else if (strEq(opt,
                   (strref) "\xE1\xC1\x05"
                            "right"))
        ctx->v.flags |= FMTVar_Right;
    else if (strEq(opt,
                   (strref) "\xE1\xC1\x05"
                            "upper"))
        ctx->v.flags |= FMTVar_Upper;
    else if (strEq(opt,
                   (strref) "\xE1\xC1\x05"
                            "lower"))
        ctx->v.flags |= FMTVar_Lower;
    else if (_fmtTypeParseOpt[vtype])
        return _fmtTypeParseOpt[vtype](&ctx->v, opt);
    return false;
}

// Close the type-name, instance-number and key spans when a terminator is reached. Any
// that are still open end at this position.
//
// These three use -1 rather than 0 for "still open", because 0 is a legitimate end
// position: "${:host}" has an empty type name that closes at index 0.
#define FMT_CLOSESPANS(pos)                \
    do {                                   \
        if (vtend < 0)                     \
            vtend = (pos);                 \
        if (vnend < 0)                     \
            vnend = (pos);                 \
        if (keystart > 0 && keyend < 0)    \
            keyend = (pos);                \
    } while (0)

_Use_decl_annotations_
bool _fmtParseVar(FMTContext* ctx)
{
    int32 vtstart = 0, vtend = -1, vnend = -1, fostart = 0, foend = 0, xstart = 0, xend = 0,
          keystart = 0, keyend = -1, defstart = 0;
    enum { X_None, X_Array, X_Hash } xtype = X_None;
    int phase                              = 0;
    int vtype                              = -1;

    int32 len = strLen(ctx->v.var);
    for (int32 i = 0; i < len; i++) {
        uint8 ch = strGetChar(ctx->v.var, i);
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
            } else if (isdigit(ch) && vtend < 0 && !foend && keystart == 0) {
                vtend = i;
            } else if (ch == ':') {
                // keyed argument. Stays in phase 0 so that (width,fmtopts), an extra
                // field and a default all remain reachable after the key.
                if (keystart > 0)
                    return false;   // only one key per variable
                FMT_CLOSESPANS(i);
                keystart = i + 1;
            } else if (ch == '(') {
                FMT_CLOSESPANS(i);
                fostart = i + 1;
                phase   = 1;
            } else if (ch == ';') {
                FMT_CLOSESPANS(i);
                defstart = i + 1;
                phase    = 3;
            } else if (ch == ')' || ch == ',') {
                return false;
            } else if (ch == '[') {
                // The extra field subscripts the argument, so it binds tighter than the
                // formatting applied to the result and is written first: "${int:sizes[2](6)}"
                // indexes, then pads to a width of 6.
                if (xstart > 0 || foend > 0)
                    return false;   // one subscript, and it precedes (width,fmtopts)
                FMT_CLOSESPANS(i);
                xstart = i + 1;
                phase  = 2;
            } else if (foend > 0 || xend > 0) {
                return false;   // trailing junk after a closed group
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
                xend  = i;
                phase = 0;   // (width,fmtopts) and ;default may follow
            } else if (ch == ';') {
                if (xend == 0)
                    xend = i;
                defstart = i + 1;
                phase    = 3;
            }
            break;
        }
    }

    if (phase == 1)
        return false;

    if (xstart > 0 && xend == 0)
        xend = len;
    FMT_CLOSESPANS(len);

    // check if we have default first, because it will be a fallback in case of parse error
    if (defstart > 0) {
        strSubStr(&ctx->v.def, ctx->v.var, defstart, len);
    }
    bool ret = !strEmpty(ctx->v.def);

    // extract the key, if this is a keyed lookup
    if (keystart > 0) {
        strSubStr(&ctx->v.key, ctx->v.var, keystart, keyend);
        if (strEmpty(ctx->v.key))
            goto out;   // "${string:}" is not meaningful
    }

    // Handle the 'idx' -- container subscripting. This runs before the type is resolved
    // because a typeless keyed placeholder such as "${:sizes[2]}" has to take its type
    // from the array's *element* type, which means knowing that it subscripts an array.
    // Nothing here depends on the type, so the order is free.
    //
    // Array index vs hashtable key is decided from the bracket contents alone, never from
    // which arguments happen to be present: a format string's meaning must not change
    // because a call gained an sarray argument. A leading backtick forces a hash key,
    // using the same escape character the rest of the grammar already uses.
    if (xstart > 0) {
        if (xstart == xend) {
            xtype = X_Array;   // "[]" advances the internal array counter
        } else if (strGetChar(ctx->v.var, xstart) == '`') {
            xtype = X_Hash;
            xstart++;   // eat the escape
        } else {
            strSubStr(&ctx->tmp, ctx->v.var, xstart, xend);
            xtype = strToInt32(&ctx->v.arrayidx, ctx->tmp, 10, true) ? X_Array : X_Hash;
        }
    }

    if (xtype == X_Array) {
        // these can be the same for [], which is legal
        if (xstart != xend) {
            strSubStr(&ctx->tmp, ctx->v.var, xstart, xend);
            if (!strToInt32(&ctx->v.arrayidx, ctx->tmp, 10, true))
                goto out;
        } else {
            ctx->v.arrayidx = ctx->arrayidx++;
        }
    } else if (xtype == X_Hash) {
        strSubStr(&ctx->v.hashkey, ctx->v.var, xstart, xend);
    }

    // now extract the type and check it
    strSubStr(&ctx->tmp, ctx->v.var, vtstart, vtend);
    for (int i = 0; i < FMT_count; i++) {
        if (strEq(ctx->tmp, _fmtTypeNames[i])) {
            vtype = i;
            break;
        }
    }

    // A keyed lookup may omit the type entirely -- "${:host}" -- in which case the
    // argument's own runtime type selects the formatter (or its element type, when the
    // placeholder subscripts a container). Resolve it here rather than deferring, so that
    // everything downstream -- type-specific option parsing, finalize, formatting -- sees
    // a concrete type exactly as it always has.
    if (vtype == -1 && keystart > 0 && strEmpty(ctx->tmp))
        vtype = _fmtTypeFromKey(ctx, ctx->v.key, xtype == X_Array, xtype == X_Hash);

    // didn't find a matching type name
    if (vtype == -1)
        goto out;

    if (vtend != vnend) {
        // have a number after the type name
        strSubStr(&ctx->tmp, ctx->v.var, vtend, vnend);
        if (!strToInt32(&ctx->v.idx, ctx->tmp, 10, true))
            goto out;
    }

    // format options?
    if (fostart > 0) {
        int32 ostart = fostart;
        int32 i, w;
        while (ostart < foend) {
            i = strFind(ctx->v.var,
                        ostart,
                        (strref) "\xE1\xC1\x01"
                                 ",");
            if (i == -1)
                i = foend;
            else if (i > foend)
                break;

            strSubStr(&ctx->tmp, ctx->v.var, ostart, i);

            // look for all-numeric width
            if (strToInt32(&w, ctx->tmp, 10, true)) {
                if (ctx->v.width != -1)
                    goto out;   // already have one!
                ctx->v.width = w;
            } else {
                fmtParseOpt(ctx, ctx->tmp, vtype);
            }

            ostart = i + 1;
        }
    }

    // the 'idx' field was resolved before the type, above

    if (_fmtTypeParseFinalize[vtype] && !_fmtTypeParseFinalize[vtype](&ctx->v))
        goto out;

    // vtype being set means parsing succeeded, otherwise use default
    ctx->v.vtype = vtype;
    ret          = true;

out:
    return ret;
}
