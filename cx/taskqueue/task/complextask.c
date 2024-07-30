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
#include "cx/utils/murmur.h"

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
    objDestroyWeak(&self->lastq);
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

uint32 ComplexTask_hash(_Inout_ ComplexTask* self, uint32 flags)
{
    // just hash the pointer
    return hashMurmur3((uint8*)&self, sizeof(void*));
}

bool ComplexTask_checkDeps(_Inout_ ComplexTask* self, bool updateProgress)
{
    bool ret = true;

    if ((self->flags & TASK_Cancel_Cascade) && taskCancelled(self)) {
        // We need to cascade the cancel at a time it's safe to access the _depends array,
        // here is as good as any.
        for (int i = saSize(self->_depends) - 1; i >= 0; --i) {
            btaskCancel(self->_depends.a[i]);
        }
    }

    for (int i = saSize(self->_depends) - 1; i >= 0; --i) {
        uint32 state = taskState(self->_depends.a[i]);
        if (state == TASK_Succeeded) {
            saRemove(&self->_depends, i);
            self->lastprogress = clockTimer();
        } else if (state == TASK_Failed) {
            saRemove(&self->_depends, i);
            if (!btaskFailed(self)) {
                btask_setState(self, TASK_Failed);
                if (self->oncomplete) {
                    // Normally the completion callback would be called when the task is run.
                    // But since it will never run due to a depedency failing, call it now.
                    cchainCallOnce(&self->oncomplete, stvar(object, self));
                }
                ctaskAdvance(self);
            }
            ret = false;   // don't run now, instead re-process in advanceq
        } else {
            ret = false;
            if (updateProgress) {
                ComplexTask* ctask = objDynCast(ComplexTask, self->_depends.a[i]);
                if (ctask)
                    self->lastprogress = max(self->lastprogress, ctask->lastprogress);
            } else {
                // we don't care about updating progress, so no need to keep checking once one that
                // isn't complete is found
                break;
            }
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

extern bool BasicTask_cancel(_Inout_ BasicTask* self);   // parent
#define parent_cancel() BasicTask_cancel((BasicTask*)(self))
bool ComplexTask_cancel(_Inout_ ComplexTask* self)
{
    // If the cancellation needs to be cascaded, advance the task.
    // Even though it's still deferred, this causes dependencies to be re-checked, which propagates
    // the cancellation.
    if (self->flags & TASK_Cancel_Cascade)
        ctaskAdvance(self);

    return parent_cancel();
}

// Autogen begins -----
#include "complextask.auto.inc"
// Autogen ends -------
