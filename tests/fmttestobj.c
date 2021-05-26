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
    // Autogen begins -----
    strDestroy(&self->sv);
    // Autogen ends -------
}

// Autogen begins -----
#include "fmttestobj.auto.inc"
// Autogen ends -------
