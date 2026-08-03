#include "format_private.h"
#include "cx/format/formattable.h"
#include "cx/obj/objstdif.h"
#include "cx/utils/compare.h"

// Objects render through an interface rather than a builtin formatter, so once the value
// is located it still has to be bound to one. Shared by the keyed and positional paths.
// *ret carries the "a default is available" fallback through an unresolvable object.
static bool fmtBindObjectIf(_Inout_ FMTContext* ctx, _Inout_ bool* ret)
{
    Formattable* fmtif = objInstIf(*(ObjInst**)ctx->v.data, Formattable);
    if (fmtif) {
        ctx->v.fmtdata[0] = (uintptr)fmtif;
        return true;
    }

    // If an object doesn't implement Formattable, it might implement Convertible
    Convertible* cvtif = objInstIf(*(ObjInst**)ctx->v.data, Convertible);
    if (cvtif) {
        ctx->v.fmtdata[1] = (uintptr)cvtif;
        return true;
    }

    ctx->v.vtype = -1;
    ctx->v.data  = NULL;
    return *ret;
}

_Use_decl_annotations_
int32 _fmtFindKeyed(FMTContext* ctx, strref key)
{
    if (strEmpty(key))
        return -1;

    int32 found = -1;
    for (int32 i = 0; i < ctx->nargs; i++) {
        const char* nm = stvarName(&ctx->args[i]);
        // strEq handles the const char* through the C-string cast path, and detects that
        // it has no embedded length so nm is never measured
        if (nm && strEq(key, (strref)nm)) {
            if (found == -1) {
                found = i;
#if DEBUG_LEVEL < 1 && !defined(DIAGNOSTIC)
                break;
#endif
            } else {
                devAssertMsg(false, "duplicate key in strFormat argument list");
                break;
            }
        }
    }
    return found;
}

// Does this argument satisfy the placeholder? When the placeholder subscripts a container
// the check applies to the container's element/value type, not to the argument itself.
// Shared by the positional search (where it selects which instance to take) and the keyed
// path (where the argument is already located and this just validates it).
static bool fmtArgMatches(_In_ stvar* arg, bool isarray, bool ishash, uint32 typeid,
                          uint32 typemask)
{
    // use ID for these to also catch parameterized types that match the base type
    if (isarray) {
        return stvarTypeId(arg) == stTypeId(sarray) &&
               ((saElemType(arg->data.st_sarray))->id & typemask) == typeid;
    }
    if (ishash) {
        return stvarTypeId(arg) == stTypeId(hashtable) &&
               (htKeyType(arg->data.st_hashtable))->id == stTypeId(string) &&
               ((htValType(arg->data.st_hashtable))->id & typemask) == typeid;
    }
    return (stvarTypeId(arg) & typemask) == typeid;
}

// Point ctx->v at the value the placeholder names, applying container subscripting if it
// asked for one. Returns false when the subscript itself fails -- index out of range, or
// a hashtable key that is not present. Assumes fmtArgMatches() already passed.
static bool fmtBindArg(_Inout_ FMTContext* ctx, _In_ stvar* arg, bool isarray, bool ishash)
{
    if (isarray) {
        sa_ref arr = arg->data.st_sarray;
        if (ctx->v.arrayidx >= saSize(arr))
            return false;
        ctx->v.type = saElemType(arr);
        ctx->v.data = (void*)((uintptr)arr.a + (size_t)saElemSize(arr) * ctx->v.arrayidx);
        return true;
    }
    if (ishash) {
        hashtable htbl = arg->data.st_hashtable;
        htelem elem    = htFind(htbl, string, ctx->v.hashkey, none, 0);
        if (!elem)
            return false;
        ctx->v.type = htValType(htbl);
        ctx->v.data = hteValPtr(htbl, opaque, elem);
        return true;
    }
    ctx->v.type = stvarType(arg);
    ctx->v.data = stGenPtr(ctx->v.type, arg->data);
    return true;
}

_Use_decl_annotations_
int _fmtTypeFromKey(FMTContext* ctx, strref key, bool isarray, bool ishash)
{
    int32 idx = _fmtFindKeyed(ctx, key);
    if (idx == -1)
        return -1;

    // For a subscripted container the formatter renders an element, so it is the element
    // type that has to select the formatter -- not sarray/hashtable, which have none.
    stvar* arg = &ctx->args[idx];
    uint32 id;
    if (isarray) {
        if (stvarTypeId(arg) != stTypeId(sarray))
            return -1;
        id = saElemType(arg->data.st_sarray)->id;
    } else if (ishash) {
        if (stvarTypeId(arg) != stTypeId(hashtable) ||
            (htKeyType(arg->data.st_hashtable))->id != stTypeId(string))
            return -1;
        id = htValType(arg->data.st_hashtable)->id;
    } else {
        id = stvarTypeId(arg);
    }

    for (int i = 0; i < FMT_count; i++) {
        if ((id & _fmtTypeIdMask[i][1]) == _fmtTypeIdMask[i][0])
            return i;
    }
    return -1;   // not a type the formatter knows how to render
}

_Use_decl_annotations_
bool _fmtFindData(FMTContext* ctx)
{
    bool ret = !strEmpty(ctx->v.def);

    if (ctx->v.vtype == -1)
        return ret;

    bool isarray    = ctx->v.arrayidx >= 0;
    bool ishash     = !strEmpty(ctx->v.hashkey);
    bool iskeyed    = !strEmpty(ctx->v.key);
    uint32 typeid   = _fmtTypeIdMask[ctx->v.vtype][0];
    uint32 typemask = _fmtTypeIdMask[ctx->v.vtype][1];

    // Keyed lookup short-circuits the positional machinery entirely: it scans the whole
    // argument list by name and leaves every per-type cursor alone, so keyed and
    // positional placeholders can be interleaved without disturbing each other. Container
    // subscripting still applies, so "${int:sizes[2]}" indexes the keyed array.
    if (iskeyed) {
        int32 kidx = _fmtFindKeyed(ctx, ctx->v.key);
        if (kidx == -1 || !fmtArgMatches(&ctx->args[kidx], isarray, ishash, typeid, typemask) ||
            !fmtBindArg(ctx, &ctx->args[kidx], isarray, ishash)) {
            // no such key, wrong type for this placeholder, or the subscript missed
            ctx->v.vtype = -1;
            return ret;
        }

        if (typeid == stTypeId(object))
            return fmtBindObjectIf(ctx, &ret);

        return true;
    }

    bool usestartarg = (ctx->v.idx == -1 && !isarray && !ishash);
    int32 findinst   = clamplow(ctx->v.idx, 1);
    int32 idx        = usestartarg ? ctx->startarg[ctx->v.vtype] : 0;

    for (; findinst > 0; idx++) {
        stvar* arg = &ctx->args[idx];

        if (idx >= ctx->nargs) {
            ctx->v.vtype = -1;   // no data, don't try to format
            return ret;
        }

        // A keyed argument is addressed by name only, never positionally. If positional
        // placeholders could consume keyed arguments, adding a keyed field to a call
        // would silently renumber every same-typed placeholder after it -- which is
        // precisely the fragility keys exist to remove. Skipping them keeps the two
        // addressing modes independent, and a placeholder that finds nothing fails
        // loudly rather than quietly formatting the wrong value.
        if (stvarName(arg))
            continue;

        if (fmtArgMatches(arg, isarray, ishash, typeid, typemask))
            findinst--;
    }

    if (usestartarg)
        ctx->startarg[ctx->v.vtype] = idx;

    if (!fmtBindArg(ctx, &ctx->args[idx - 1], isarray, ishash)) {
        ctx->v.vtype = -1;   // index out of range, or hashtable key not present
        return ret;
    }

    if (typeid == stTypeId(object)) {
        return fmtBindObjectIf(ctx, &ret);
    }
    return true;
}

static inline void fillPad(_Inout_ string* pad, int32 len)
{
    strClear(pad);
    uint8* buf = strBuffer(pad, len);
    for (int32 i = 0; i < len; i++) buf[i] = ' ';
}

static void fmtApplyGenWidth(_Inout_ FMTVar* v, _Inout_ string* vstr, int32 width, uint32 flags)
{
    if (width <= 0 || strLen(*vstr) == width)
        return;

    if ((int32)strLen(*vstr) > width) {
        strSubStr(vstr, *vstr, 0, width);
        return;
    }

    int32 wdiff = width - strLen(*vstr);
    if (flags & FMTVar_Right) {
        fillPad(&v->tmp, wdiff);
        strPrepend(v->tmp, vstr);
    } else if (flags & FMTVar_Center) {
        int32 lpart = wdiff / 2;
        fillPad(&v->tmp, lpart);
        strPrepend(v->tmp, vstr);
        fillPad(&v->tmp, wdiff - lpart);
        strAppend(vstr, v->tmp);
    } else {
        // FMTVar_Left implied
        fillPad(&v->tmp, wdiff);
        strAppend(vstr, v->tmp);
    }
}

static void fmtApplyGenFlags(_Inout_ FMTContext* ctx, _Inout_ string* vstr)
{
    if (!(ctx->v.flags & FMTVar_NoGenCase)) {
        if (ctx->v.flags & FMTVar_Upper)
            strUpper(vstr);
        else if (ctx->v.flags & FMTVar_Lower)
            strLower(vstr);
    }

    if (!(ctx->v.flags & FMTVar_NoGenWidth))
        fmtApplyGenWidth(&ctx->v, vstr, ctx->v.width, ctx->v.flags);
}

_Use_decl_annotations_
void _fmtFormat(FMTContext* ctx)
{
    bool success = false;

    strClear(&ctx->tmp);
    if (ctx->v.vtype != -1 && _fmtTypeFormat[ctx->v.vtype])
        success = _fmtTypeFormat[ctx->v.vtype](&ctx->v, &ctx->tmp);

    if (!success) {
        // use the default value
        strDup(&ctx->tmp, ctx->v.def);
    }

    // apply generic formatting options
    fmtApplyGenFlags(ctx, &ctx->tmp);

    strAppend(ctx->dest, ctx->tmp);
}
