// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "task.h"
// ==================== Auto-generated section ends ======================
#include "taskqueue_private.h"

void Task_destroy(_Inout_ Task *self)
{
    // Autogen begins -----
    strDestroy(&self->name);
    objDestroyWeak(&self->lastq);
    cchainDestroy(&self->oncomplete);
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

bool Task_advance(_Inout_ Task *self)
{
    TaskQueue *tq = objAcquireFromWeak(TaskQueue, self->lastq);
    bool ret = false;

    if(tq) {
        ret = _tqAdvanceTask(tq, self);
        objRelease(&tq);
    }

    return ret;
}

// Autogen begins -----
#include "task.auto.inc"
// Autogen ends -------
