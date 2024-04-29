// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "task.h"
// ==================== Auto-generated section ends ======================

void Task_destroy(_Inout_ Task *self)
{
    // Autogen begins -----
    strDestroy(&self->name);
    // Autogen ends -------
}

_objinit_guaranteed bool Task_init(_Inout_ Task *self)
{
    if(!self->name)
        self->name = _S"Task";
    // Autogen begins -----
    return true;
    // Autogen ends -------
}

// Autogen begins -----
#include "task.auto.inc"
// Autogen ends -------
