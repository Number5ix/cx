// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "sys/processobj.h"
// clang-format on
// ==================== Auto-generated section ends ======================

_objinit_guaranteed bool Process_init(_In_ Process* self)
{
    // The lock and the chain are [noinit] in the class definition, so they are set up here.
    // Everything else starts zeroed, which is already the right initial state.
    mutexInit(&self->lock);

    // Autogen begins -----
    return true;
    // Autogen ends -------
}

void Process_destroy(_In_ Process* self)
{
    // Autogen begins -----
    strDestroy(&self->name);
    strDestroy(&self->exepath);
    mutexDestroy(&self->lock);
    cchainDestroy(&self->onexit);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "sys/processobj.auto.inc"
// clang-format on
// Autogen ends -------
