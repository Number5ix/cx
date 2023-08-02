// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "unix_thread_threadobj.h"
// ==================== Auto-generated section ends ======================

UnixThread *UnixThread_create()
{
    UnixThread *self;
    self = objInstCreate(UnixThread);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

// Autogen begins -----
#include "unix_thread_threadobj.auto.inc"
// Autogen ends -------
