// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "platform/unix/unix_sys_processobj.h"
// clang-format on
// ==================== Auto-generated section ends ======================

#include <unistd.h>

_objfactory_guaranteed UnixProcess* UnixProcess_create()
{
    UnixProcess* self;
    self = objInstCreate(UnixProcess);

    // Zero would be a real file descriptor, so the "no pidfd" value has to be set explicitly
    // before anything can look at it.
    self->waitfd = -1;

    objInstInit(self);

    return self;
}

void UnixProcess_destroy(_In_ UnixProcess* self)
{
    if (self->waitfd != -1) {
        close(self->waitfd);
        self->waitfd = -1;
    }
}

// Autogen begins -----
// clang-format off
#include "platform/unix/unix_sys_processobj.auto.inc"
// clang-format on
// Autogen ends -------
