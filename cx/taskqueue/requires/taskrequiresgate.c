// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "taskrequiresgate.h"
// clang-format on
// ==================== Auto-generated section ends ======================

_objfactory_guaranteed TRGate* TRGate_create(_In_opt_ strref name)
{
    TRGate* self;
    self = objInstCreate(TRGate);

    strDup(&self->name, name);
    objInstInit(self);
    return self;
}

void TRGate_open(_Inout_ TRGate* self)
{
    atomicStore(bool, &self->isopen, true, Release);

    withMutex(&self->_wlmtx) {
        foreach(sarray, idx, ComplexTask*, task, self->_waitlist) {
            ctaskAdvance(task);
        }
        saClear(&self->_waitlist);
    }
}

void TRGate_progress(_Inout_ TRGate* self)
{
    int64 now = clockTimer();
    withMutex(&self->_wlmtx) {
        self->lastprogress = now;
    }
}

bool TRGate_registerTask(_Inout_ TRGate* self, ComplexTask* task)
{
    // if the gate is open, no sense allowing new registrations
    if (atomicLoad(bool, &self->isopen, Acquire))
        return false;

    withMutex(&self->_wlmtx) {
        saPush(&self->_waitlist, object, task);
    }

    return true;
}

_objinit_guaranteed bool TRGate_init(_Inout_ TRGate* self)
{
    // Autogen begins -----
    saInit(&self->_waitlist, object, 1);
    return true;
    // Autogen ends -------
}

void TRGate_destroy(_Inout_ TRGate* self)
{
    // Autogen begins -----
    strDestroy(&self->name);
    saDestroy(&self->_waitlist);
    // Autogen ends -------
}

// ---------------- TaskRequiresGate ----------------

_objfactory_guaranteed TaskRequiresGate* TaskRequiresGate_create(_In_ TRGate* gate)
{
    TaskRequiresGate* self;
    self = objInstCreate(TaskRequiresGate);

    self->gate = objAcquire(gate);

    objInstInit(self);
    return self;
}

uint32 TaskRequiresGate_state(_Inout_ TaskRequiresGate* self, ComplexTask* task)
{
    return atomicLoad(bool, &self->gate->isopen, Relaxed) ?
        TASK_Requires_Ok_Permanent :
        TASK_Requires_Wait;
}

int64 TaskRequiresGate_progress(_Inout_ TaskRequiresGate* self)
{
    int64 ret = -1;
    withMutex(&self->gate->_wlmtx) {
        ret = self->gate->lastprogress;
    }

    return ret;
}

bool TaskRequiresGate_tryAcquire(_Inout_ TaskRequiresGate* self, ComplexTask* task)
{
    return false;
}

bool TaskRequiresGate_release(_Inout_ TaskRequiresGate* self, ComplexTask* task)
{
    return false;
}

void TaskRequiresGate_cancel(_Inout_ TaskRequiresGate* self)
{
}

bool TaskRequiresGate_registerTask(_Inout_ TaskRequiresGate* self, _In_ ComplexTask* task)
{
    return trgateRegisterTask(self->gate, task);
}

void TaskRequiresGate_destroy(_Inout_ TaskRequiresGate* self)
{
    // Autogen begins -----
    objRelease(&self->gate);
    // Autogen ends -------
}

// Autogen begins -----
#include "taskrequiresgate.auto.inc"
// Autogen ends -------
