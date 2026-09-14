// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "sys/dynlibobj.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "dynlib_private.h"

_objfactory_guaranteed DynLib* DynLib_create(void* handle)
{
    DynLib* self;
    self = objInstCreate(DynLib);

    self->handle = handle;

    objInstInit(self);

    return self;
}

void DynLib_destroy(_In_ DynLib* self)
{
    if (self->handle) {
        _dynlibPlatformClose(self->handle);
        self->handle = NULL;
    }
}

// Autogen begins -----
// clang-format off
#include "sys/dynlibobj.auto.inc"
// clang-format on
// Autogen ends -------
