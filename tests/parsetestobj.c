// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "parsetestobj.h"
// clang-format on
// ==================== Auto-generated section ends ======================

_objfactory_guaranteed ParseTestClass* ParseTestClass_create()
{
    ParseTestClass* self;
    self = objInstCreate(ParseTestClass);

    // Insert any pre-initialization object construction here

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

bool ParseTestClass_parse(_In_ ParseTestClass* self, _In_opt_ strref text, ParseVar* v)
{
    // "name:number", where the number is spelled out. Options the pattern did not
    // understand arrive verbatim, and "words" is one this object does understand.
    bool words = false;
    for (int32 i = 0; i < saSize(v->opts); i++) {
        if (strEq(v->opts.a[i], _S"words"))
            words = true;
    }

    int32 colon = strFindChar(text, 0, ':');
    if (colon < 0)
        return false;

    strDestroy(&self->sv);
    strSubStr(&self->sv, text, 0, colon);

    string num = 0;
    strSubStr(&num, text, colon + 1, strEnd);

    bool ok = true;
    if (words) {
        if (strEqi(num, _S"one"))
            self->iv = 1;
        else if (strEqi(num, _S"two"))
            self->iv = 2;
        else if (strEqi(num, _S"three"))
            self->iv = 3;
        else
            ok = false;
    } else {
        ok = strToInt32(&self->iv, num, 10, STRNUM_NoTrailing | STRNUM_NoWS);
    }

    strDestroy(&num);
    return ok;
}

void ParseTestClass_destroy(_In_ ParseTestClass* self)
{
    // Autogen begins -----
    strDestroy(&self->sv);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "parsetestobj.auto.inc"
// clang-format on
// Autogen ends -------
