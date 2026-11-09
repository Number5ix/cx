#include "objprop.h"
#include <cx/string.h>

static const ObjPropDesc* findInClass(ObjClassInfo* cls, strref name)
{
    int32 lo = 0, hi = cls->nprops - 1;
    while (lo <= hi) {
        int32 mid = lo + (hi - lo) / 2;
        int32 c   = strCmp(name, cls->props[mid].name);
        if (c == 0)
            return &cls->props[mid];
        if (c < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return NULL;
}

_Use_decl_annotations_
const ObjPropDesc* objPropFind(ObjClassInfo* cls, strref name)
{
    for (; cls; cls = cls->parent) {
        const ObjPropDesc* pd = findInClass(cls, name);
        if (pd)
            return pd;
    }
    return NULL;
}

_Use_decl_annotations_
bool objPropEnum(ObjClassInfo* cls, ObjPropEnumFn fn, void* ctx)
{
    for (ObjClassInfo* cur = cls; cur; cur = cur->parent) {
        for (int32 i = 0; i < cur->nprops; i++) {
            const ObjPropDesc* pd = &cur->props[i];

            // skip a parent's entry that a subclass hides
            bool hidden = false;
            for (ObjClassInfo* sub = cls; sub != cur && !hidden; sub = sub->parent)
                hidden = findInClass(sub, pd->name) != NULL;

            if (!hidden && !fn(pd, ctx))
                return false;
        }
    }
    return true;
}

_Use_decl_annotations_
bool _objPropGetD(ObjInst* obj, const ObjPropDesc* pd, stvar* out)
{
    if (pd->get(obj, out))
        return true;

    *out = stvNone;
    return false;
}

_Use_decl_annotations_
bool _objPropSetD(ObjInst* obj, const ObjPropDesc* pd, stvar val)
{
    // stConvert would turn an empty value into zero
    if (!pd->set || !val._type || val._type->id == stTypeId(none))
        return false;

    if (stEq(val._type, pd->type))
        return pd->set(obj, val);

    // bool takes integers but nothing else; stConvert would also turn "false" into a value
    if (stEq(pd->type, stType(bool))) {
        uint32 id = val._type->id;
        if (STYPE_CLASS(id) != STCLASS_BASIC ||
            (STYPE_SUBTYPE(id) != STST_INT && STYPE_SUBTYPE(id) != STST_UINT))
            return false;
    }

    stgeneric tmp = { 0 };
    if (!_stConvert(pd->type, &tmp, val._type, val.data, 0))
        return false;

    bool ret = pd->set(obj, (stvar) { .data = tmp, ._type = pd->type });
    _stDestroy(pd->type, &tmp, 0);
    return ret;
}

_Use_decl_annotations_
bool _objPropGet(ObjInst* obj, strref name, stvar* out)
{
    const ObjPropDesc* pd = objPropFind(objClsInfo(obj), name);
    if (!pd) {
        *out = stvNone;
        return false;
    }
    return _objPropGetD(obj, pd, out);
}

_Use_decl_annotations_
bool _objPropSet(ObjInst* obj, strref name, stvar val)
{
    const ObjPropDesc* pd = objPropFind(objClsInfo(obj), name);
    return pd && _objPropSetD(obj, pd, val);
}
