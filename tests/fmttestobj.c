// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "fmttestobj.h"
// ==================== Auto-generated section ends ======================

_objfactory_guaranteed FmtTestClass *FmtTestClass_create(int32 ival, string sval)
{
    FmtTestClass *ret;
    ret = objInstCreate(FmtTestClass);

    ret->iv = ival;
    strDup(&ret->sv, sval);

    if (!objInstInit(ret))
        objRelease(&ret);
    return ret;
}

bool FmtTestClass_format(_Inout_ FmtTestClass *self, FMTVar *v, string *out)
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

void FmtTestClass_destroy(_Inout_ FmtTestClass *self)
{
    // Autogen begins -----
    strDestroy(&self->sv);
    // Autogen ends -------
}

_objfactory_guaranteed FmtTestClass2 *FmtTestClass2_create(int32 ival, string sval)
{
    FmtTestClass2 *self;
    self = objInstCreate(FmtTestClass2);

    self->iv = ival;
    strDup(&self->sv, sval);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

bool FmtTestClass2_convert(_Inout_ FmtTestClass2 *self, stype st, stgeneric *dest, uint32 flags)
{
    if (!stEq(st, stType(string)))
        return false;

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

    strNConcat(&dest->st_string, _S"Object(", self->sv, _S":", snum, _S")");
    return true;
}

void FmtTestClass2_destroy(_Inout_ FmtTestClass2 *self)
{
    // Autogen begins -----
    strDestroy(&self->sv);
    // Autogen ends -------
}

// Autogen begins -----
#include "fmttestobj.auto.inc"
// Autogen ends -------
