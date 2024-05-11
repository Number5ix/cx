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

extern bool BasicTask_reset(_Inout_ BasicTask *self); // parent
#define parent_reset() BasicTask_reset((BasicTask*)(self))
bool Task_reset(_Inout_ Task *self)
{
    int32 oldval = atomicLoad(int32, &self->state, Relaxed);
    if(oldval != TASK_Succeeded && oldval != TASK_Failed)
        return false;

    atomicStore(bool, &self->cancelled, false, Relaxed);
    self->last = 0;
    self->lastprogress = 0;
    self->nextrun = 0;
    cchainDestroy(&self->oncomplete);

    while(!atomicCompareExchange(int32, weak, &self->state, &oldval, TASK_Created, AcqRel, Relaxed)) {
        if(oldval != TASK_Succeeded && oldval != TASK_Failed)
            return false;
    }

    return true;
}

// Autogen begins -----
#include "task.auto.inc"
// Autogen ends -------
