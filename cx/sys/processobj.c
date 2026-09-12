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
#include "process_private.h"

_objinit_guaranteed bool Process_init(_In_ Process* self)
{
    // The lock and the subscriber list are [noinit] in the class definition, so they are set up
    // here. Everything else starts zeroed, which is already the right initial state.
    mutexInit(&self->lock);
    saInit(&self->onexit, closure, 2);

    // Autogen begins -----
    return true;
    // Autogen ends -------
}

void Process_destroy(_In_ Process* self)
{
    // Nothing may still be about to call into this object once it is gone.
    procNotifyCancel(self);

    // Releasing one handle is a good moment to collect any other child that has finished, since
    // a program that stops caring about one process is often done with several.
    _procReapPending();

    // Autogen begins -----
    strDestroy(&self->name);
    strDestroy(&self->exepath);
    mutexDestroy(&self->lock);
    saDestroy(&self->onexit);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "sys/processobj.auto.inc"
// clang-format on
// Autogen ends -------
