// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "task.h"
// clang-format on
// ==================== Auto-generated section ends ======================

_objinit_guaranteed bool Task_init(_Inout_ Task* self)
{
    if (!self->name)
        self->name = _S"Task";
    // Autogen begins -----
    return true;
    // Autogen ends -------
}

void Task_destroy(_Inout_ Task* self)
{
    // Autogen begins -----
    strDestroy(&self->name);
    cchainDestroy(&self->oncomplete);
    // Autogen ends -------
}

extern bool BasicTask_reset(_Inout_ BasicTask* self);   // parent
#define parent_reset() BasicTask_reset((BasicTask*)(self))
bool Task_reset(_Inout_ Task* self)
{
    if (!parent_reset())
        return false;

    self->last = 0;
    cchainDestroy(&self->oncomplete);

    return true;
}

// Autogen begins -----
#include "task.auto.inc"
// Autogen ends -------