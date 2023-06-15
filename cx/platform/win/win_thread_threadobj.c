// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/core/objstdif.h>
#include <cx/debug/assert.h>
#include <cx/container.h>
#include <cx/string.h>
#include "win_thread_threadobj.h"
// ==================== Auto-generated section ends ======================

WinThread *WinThread_create()
{
    WinThread *self;
    self = objInstCreate(WinThread);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

void WinThread_destroy(WinThread *self)
{
     CloseHandle(self->handle);
}

// Autogen begins -----
#include "win_thread_threadobj.auto.inc"
// Autogen ends -------
