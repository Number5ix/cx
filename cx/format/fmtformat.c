#include "format_private.h"
#include "cx/utils/compare.h"
#include "formattable.h"

bool _fmtFindData(FMTContext *ctx)
{
    bool ret = !strEmpty(ctx->v.def);

    if (ctx->v.vtype == -1)
        return ret;

    bool isarray = ctx->v.arrayidx >= 0;
    bool ishash = !strEmpty(ctx->v.hashkey);
    bool usestartarg = (ctx->v.idx == -1 && !isarray && !ishash);
    int32 findinst = clamplow(ctx->v.idx, 1);
    int32 idx = usestartarg ? ctx->startarg[ctx->v.vtype] : 0;
    uint8 typeid = _fmtTypeIdMask[ctx->v.vtype][0];
    uint8 typemask = _fmtTypeIdMask[ctx->v.vtype][1];

    for (; findinst > 0; idx++) {
        stvar *arg = &ctx->args[idx];

        if (idx >= ctx->nargs) {
            ctx->v.vtype = -1;      // no data, don't try to format
            return ret;
        }

        if (isarray) {
            if (stGetId(arg->type) == stTypeId(sarray) &&
                (stGetId(saElemType(&arg->data.st_sarray)) & typemask) == typeid)
                findinst--;
        } else if (ishash) {
            if (stGetId(arg->type) == stTypeId(hashtable) &&
                stGetId(htKeyType(&arg->data.st_hashtable)) == stTypeId(string) &&
                (stGetId(htValType(&arg->data.st_hashtable)) & typemask) == typeid)
                findinst--;
        } else if ((stGetId(arg->type) & typemask) == typeid) {
            findinst--;
        }
    }

    if (usestartarg)
        ctx->startarg[ctx->v.vtype] = idx;

    if (isarray) {
        sa_gen *arr = &ctx->args[idx - 1].data.st_sarray;
        if (ctx->v.arrayidx >= saSize(arr)) {
            ctx->v.vtype = -1;
            return ret;
        }

        ctx->v.type = saElemType(arr);
        ctx->v.data = (void*)((uintptr)arr->a + saElemSize(arr)*ctx->v.arrayidx);
    } else if (ishash) {
        hashtable *htbl = &ctx->args[idx - 1].data.st_hashtable;
        htelem elem = htFindElem(htbl, string, ctx->v.hashkey);
        if (!elem) {
            ctx->v.vtype = -1;
            return ret;
        }
        ctx->v.type = htValType(htbl);
        ctx->v.data = hteValPtr(htbl, elem, opaque);
    } else {
        ctx->v.type = ctx->args[idx - 1].type;
        ctx->v.data = stGenPtr(ctx->v.type, ctx->args[idx - 1].data);
    }

    if (typeid == stTypeId(object)) {
        // special handling for objects to ensure they have the right interface
        Formattable *fmtif = objInstIf(*(ObjInst**)ctx->v.data, Formattable);
        if (fmtif) {
            ctx->v.fmtdata[0] = (uintptr)fmtif;
        } else {
            ctx->v.vtype = -1;
            ctx->v.data = NULL;
            return ret;
        }
    }
    return true;
}

static inline void fillPad(string *pad, int32 len)
{
    strClear(pad);
    char *buf = strBuffer(pad, len);
    for (int32 i = 0; i < len; i++)
        buf[i] = ' ';
}

static void fmtApplyGenWidth(string *vstr, int32 width, uint32 flags)
{
    if (width <= 0 || strLen(*vstr) == width)
        return;

    if ((int32)strLen(*vstr) > width) {
        strSubStr(vstr, *vstr, 0, width);
        return;
    }

    string(pad);
    int32 wdiff = width - strLen(*vstr);
    if (flags & FMTVar_Right) {
        fillPad(&pad, wdiff);
        strPrepend(pad, vstr);
    } else if (flags & FMTVar_Center) {
        int32 lpart = wdiff / 2;
        fillPad(&pad, lpart);
        strPrepend(pad, vstr);
        fillPad(&pad, wdiff - lpart);
        strAppend(vstr, pad);
    } else {
        // FMTVar_Left implied
        fillPad(&pad, wdiff);
        strAppend(vstr, pad);
    }
    strDestroy(&pad);
}

static void fmtApplyGenFlags(FMTContext *ctx, string *vstr)
{
    if (!(ctx->v.flags & FMTVar_NoGenCase)) {
        if (ctx->v.flags & FMTVar_Upper)
            strUpper(vstr);
        else if (ctx->v.flags & FMTVar_Lower)
            strLower(vstr);
    }

    if (!(ctx->v.flags & FMTVar_NoGenWidth))
        fmtApplyGenWidth(vstr, ctx->v.width, ctx->v.flags);
}

void _fmtFormat(FMTContext *ctx)
{
    string(vstr);
    bool success = false;

    if (_fmtTypeFormat[ctx->v.vtype])
        success = _fmtTypeFormat[ctx->v.vtype](&ctx->v, &vstr);

    if (!success) {
        // use the default value
        strDup(&vstr, ctx->v.def);
    }

    // apply generic formatting options
    fmtApplyGenFlags(ctx, &vstr);

    strAppend(&ctx->dest, vstr);
    strDestroy(&vstr);
}
