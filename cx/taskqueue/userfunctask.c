// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "userfunctask.h"
// ==================== Auto-generated section ends ======================

_objfactory_guaranteed UserFuncTask *UserFuncTask_create(UserTaskCB func, void *udata)
{
    UserFuncTask *self;
    self = objInstCreate(UserFuncTask);

    self->func = func;
    self->udata = udata;

    objInstInit(self);

    return self;
}

bool UserFuncTask_run(_Inout_ UserFuncTask *self, _In_ TaskQueue *tq, _In_ TaskQueueWorker *worker, _Inout_ TaskControl *tcon)
{
    return self->func && self->func(tq, self->udata);
}

// Autogen begins -----
#include "userfunctask.auto.inc"
// Autogen ends -------