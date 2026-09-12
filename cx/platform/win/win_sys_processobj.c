// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "platform/win/win_sys_processobj.h"
// clang-format on
// ==================== Auto-generated section ends ======================

_objfactory_guaranteed WinProcess* WinProcess_create()
{
    WinProcess* self;
    self = objInstCreate(WinProcess);

    objInstInit(self);

    return self;
}

void WinProcess_destroy(_In_ WinProcess* self)
{
    if (self->h) {
        CloseHandle(self->h);
        self->h = NULL;
    }
}

// Autogen begins -----
// clang-format off
#include "platform/win/win_sys_processobj.auto.inc"
// clang-format on
// Autogen ends -------
