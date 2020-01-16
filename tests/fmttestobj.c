// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/core/objstdif.h>
#include <cx/debug/assert.h>
#include <cx/container.h>
#include <cx/string.h>
#include "fmttestobj.h"
// ==================== Auto-generated section ends ======================

FmtTestClass *FmtTestClass_create(int32 ival, string sval)
{
    FmtTestClass *ret;
    ret = objInstCreate(FmtTestClass);

    ret->iv = ival;
    strDup(&ret->sv, sval);

    if (!objInstInit(ret))
        objRelease(&ret);
    return ret;
}

bool FmtTestClass_format(FmtTestClass *self, FMTVar *v, string *out)
{
    string snum;
    switch (self->iv) {
    case 1:
        snum = _S"One";
        break;
    case 2:
        snum = _S"Two";
        break;
    case 3:
        snum = _S"Three";
        break;
    case 4:
        snum = _S"Four";
        break;
    case 5:
        snum = _S"Five";
        break;
    default:
        return false;
    }

    strNConcat(out, _S"Object(", self->sv, _S":", snum, _S")");

    return true;
}

void FmtTestClass_destroy(FmtTestClass *self)
{
// ==================== Auto-generated section begins ====================
    strDestroy(&self->sv);
// ==================== Auto-generated section ends ======================
}

// ==================== Auto-generated section begins ====================
FmtTestClass_ClassIf FmtTestClass_ClassIf_tmpl = {
    ._size = sizeof(FmtTestClass_ClassIf),
};

static Formattable _impl_FmtTestClass_Formattable = {
    ._size = sizeof(Formattable),
    ._implements = (ObjIface*)&Formattable_tmpl,
    .format = (bool (*)(void*, FMTVar*, string*))FmtTestClass_format,
};

static FmtTestClass_ClassIf _impl_FmtTestClass_FmtTestClass_ClassIf = {
    ._size = sizeof(FmtTestClass_ClassIf),
    ._implements = (ObjIface*)&FmtTestClass_ClassIf_tmpl,
    .format = (bool (*)(void*, FMTVar*, string*))FmtTestClass_format,
};

static ObjIface *_ifimpl_FmtTestClass[] = {
    (ObjIface*)&_impl_FmtTestClass_Formattable,
    (ObjIface*)&_impl_FmtTestClass_FmtTestClass_ClassIf,
    NULL
};

ObjClassInfo FmtTestClass_clsinfo = {
    .instsize = sizeof(FmtTestClass),
    .classif = (ObjIface*)&FmtTestClass_ClassIf_tmpl,
    .destroy = (void(*)(void*))FmtTestClass_destroy,
    .ifimpl = _ifimpl_FmtTestClass,
};

// ==================== Auto-generated section ends ======================
