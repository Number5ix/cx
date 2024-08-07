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
#include <cx/taskqueue/requires/taskrequirestask.h>
#include <cx/taskqueue/requires/taskrequiresgate.h>
#include <cx/taskqueue/requires/taskrequiresresource.h>
#include "cx/utils/murmur.h"

bool ComplexTask_advance(_Inout_ ComplexTask* self)
{
    // If the task was previously in a queue, it will keep a weak reference to it.
    ComplexTaskQueue* tq = objAcquireFromWeak(ComplexTaskQueue, self->lastq);
    bool ret             = false;

    if (tq) {
        atomicFetchAdd(uint32, &self->_advcount, 1, Relaxed);
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
    self->_intflags    = 0;
    objDestroyWeak(&self->lastq);
    saClear(&self->_requires);

    return true;
}

void ComplexTask_destroy(_Inout_ ComplexTask* self)
{
    // failsafe in case any resources were acquired in retain mode and we got deferred,
    // then failed a dependency check or something

    ctaskReleaseRequires(self, self->_requires);

    // Autogen begins -----
    objDestroyWeak(&self->lastq);
    saDestroy(&self->_requires);
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

bool ComplexTask_checkRequires(_Inout_ ComplexTask* self, bool updateProgress)
{
    int64 now     = clockTimer();
    bool ret      = true;
    bool done     = false;

    if ((self->flags & TASK_Cancel_Cascade) && taskCancelled(self)) {
        // We need to cascade the cancel at a time it's safe to access the _depends array,
        // here is as good as any.
        for (int i = saSize(self->_requires) - 1; i >= 0; --i) {
            taskrequiresCancel(self->_requires.a[i]);
        }
    }

    for (int i = saSize(self->_requires) - 1; i >= 0 && !done; --i) {
        TaskRequires* req = self->_requires.a[i];
        uint32 state = taskrequiresState(req, self);
        switch (state) {
        case TASK_Requires_Ok:
            self->lastprogress = now;
            break;

        case TASK_Requires_Ok_Permanent:
            self->lastprogress = now;
            saRemove(&self->_requires, i);
            break;

        case TASK_Requires_Fail_Permanent:
            saRemove(&self->_requires, i);

            if (self->flags & TASK_Soft_Requires) {
                // just flag it as failed but otherwise treat as permanent Ok
                self->flags |= TASK_Require_Failed;
            } else {
                ret = false;

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
            }
            break;

        case TASK_Requires_Wait:
            ret = false;
            if (updateProgress) {
                int64 progress = taskrequiresProgress(req);
                if (progress > 0)
                    self->lastprogress = max(self->lastprogress, progress);
            } else {
                // we don't care about updating progress, so no need to keep checking once one that
                // isn't complete is found
                done = true;
            }
            break;

        case TASK_Requires_Acquire:
            // resources need to be acquired, but we prefer to try to do that right before the task is run
            self->_intflags |= TASK_INTERNAL_Needs_Resources;
            break;
        }
    }

    return ret;
}

bool ComplexTask_acquireRequires(_Inout_ ComplexTask* self, sa_TaskRequires* acquired)
{
    for (int i = saSize(self->_requires) - 1; i >= 0; --i) {
        TaskRequires* req = self->_requires.a[i];
        uint32 state      = taskrequiresState(req, self);
        if (state == TASK_Requires_Acquire) {
            if (taskrequiresTryAcquire(req, self)) {
                saPush(acquired, object, req);
                self->_intflags |= TASK_INTERNAL_Owns_Resources;
            } else {
                return false;
            }
        } else if (state != TASK_Requires_Ok && state != TASK_Requires_Ok_Permanent) {
            // We have some other dependency that is no longer satisfied, need to go back into defer queue
            return false;
        }
    }

    self->_intflags &= ~TASK_INTERNAL_Needs_Resources;

    return true;
}

bool ComplexTask_releaseRequires(_Inout_ ComplexTask* self, sa_TaskRequires resources)
{
    bool ret = true;
    for (int i = saSize(resources) - 1; i >= 0; --i) {
        TaskRequires* req = resources.a[i];
        if (!taskrequiresRelease(req, self))
            ret = false;
    }

    self->_intflags &= ~TASK_INTERNAL_Owns_Resources;

    return ret;
}

bool ComplexTask_advanceCallback(stvlist* cvars, stvlist* args)
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

void ComplexTask_require(_Inout_ ComplexTask* self, _In_ TaskRequires* req)
{
    taskrequiresRegisterTask(req, self);
    saPush(&self->_requires, object, req);
}

void ComplexTask_requireTask(_Inout_ ComplexTask* self, _In_ Task* dep)
{
    TaskRequiresTask* trt = taskrequirestaskCreate(dep);
    taskrequirestaskRegisterTask(trt, self);
    saPushC(&self->_requires, object, &trt);
}

void ComplexTask_requireResource(_Inout_ ComplexTask* self, _In_ TaskResource* res)
{
    TaskRequiresResource* trr = taskrequiresresourceCreate(res);
    taskrequiresresourceRegisterTask(trr, self);
    saPushC(&self->_requires, object, &trr);
}

void ComplexTask_requireGate(_Inout_ ComplexTask* self, _In_ TRGate* gate)
{
    TaskRequiresGate* trg = taskrequiresgateCreate(gate);
    taskrequiresgateRegisterTask(trg, self);
    saPushC(&self->_requires, object, &trg);
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
