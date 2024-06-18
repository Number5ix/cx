// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "complextask.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include <cx/taskqueue.h>
#include <cx/taskqueue/queue/tqcomplex.h>

bool ComplexTask_advance(_Inout_ ComplexTask* self)
{
    // If the task was previously in a queue, it will keep a weak reference to it.
    ComplexTaskQueue* tq = objAcquireFromWeak(ComplexTaskQueue, self->lastq);
    bool ret             = false;

    if (tq) {
        ret = ctaskqueueAdvance(tq, self);
        objRelease(&tq);
    }

    return ret;
}

extern bool Task_reset(_Inout_ Task* self);   // parent
#define parent_reset() Task_reset((Task*)(self))
bool ComplexTask_reset(_Inout_ ComplexTask* self)
{
    if (!parent_reset())
        return false;

    self->lastprogress = 0;
    self->nextrun      = 0;
    saClear(&self->_depends);

    return true;
}

void ComplexTask_destroy(_Inout_ ComplexTask* self)
{
    // Autogen begins -----
    objDestroyWeak(&self->lastq);
    saDestroy(&self->_depends);
    // Autogen ends -------
}

intptr ComplexTask_cmp(_Inout_ ComplexTask* self, ComplexTask* other, uint32 flags)
{
    intptr nrcmp = stCmp(int64, self->nextrun, other->nextrun);
    if (nrcmp != 0)
        return nrcmp;

    return stCmp(ptr, self, other);
}

bool ComplexTask_checkDeps(_Inout_ ComplexTask* self)
{
    bool ret = true;

    for (int i = saSize(self->_depends) - 1; i >= 0; --i) {
        uint32 state = taskState(self->_depends.a[i]);
        if (state == TASK_Succeeded) {
            saRemove(&self->_depends, i);
            self->lastprogress = clockTimer();
        } else if (state == TASK_Failed) {
            btask_setState(self, TASK_Failed);
            saRemove(&self->_depends, i);
        } else {
            ret                = false;
            ComplexTask* ctask = objDynCast(self->_depends.a[i], ComplexTask);
            if (ctask)
                self->lastprogress = max(self->lastprogress, ctask->lastprogress);
        }
    }

    return ret;
}

static bool taskDepCallback(stvlist* cvars, stvlist* args)
{
    Weak(ComplexTask)* wref = NULL;
    if (!stvlNext(cvars, weakref, &wref))
        return false;

    ComplexTask* ctask = objAcquireFromWeak(ComplexTask, wref);
    if (ctask) {
        // advance the task so that it can re-check its dependencies
        ctaskAdvance(ctask);
        objRelease(&ctask);
    }

    return true;
}

void ComplexTask_dependOn(_Inout_ ComplexTask* self, _In_ Task* dep)
{
    saPush(&self->_depends, object, dep);

    // When the depended-on task completes, it will call a callback that advances this task out of
    // the defer queue. A weak reference is used in case this task is discarded before the callback
    // is called.
    Weak(ComplexTask)* wref = objGetWeak(ComplexTask, self);
    cchainAttach(&dep->oncomplete, taskDepCallback, stvar(weakref, wref));
    objDestroyWeak(&wref);
}

// Autogen begins -----
#include "complextask.auto.inc"
// Autogen ends -------
