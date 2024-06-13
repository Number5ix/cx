// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "tqobject.h"
// ==================== Auto-generated section ends ======================
#include "taskqueue_private.h"

static intptr deferTaskCmp(stype st, stgeneric a, stgeneric b, uint32 flags)
{
    Task *t1 = (Task *)a.st_ptr;
    Task *t2 = (Task *)b.st_ptr;

    intptr nrcmp = stCmp(int64, t1->nextrun, t2->nextrun);
    if(nrcmp != 0)
        return nrcmp;

    return stCmp(ptr, t1, t2);
}

static STypeOps deferTaskOps = {
    .cmp = deferTaskCmp,
};

_objinit_guaranteed bool TaskQueue_init(_Inout_ TaskQueue* self)
{
    eventInit(&self->workev);
    eventInit(&self->shutdownev);
    saInit(&self->deferred, custom(ptr, deferTaskOps), 10, SA_Sorted);

    int qsz = self->tqconfig.wBusy * 4;
    prqInitDynamic(&self->runq, qsz, qsz * 2, 0, PRQ_Grow_100, PRQ_Grow_25);
    prqInitDynamic(&self->doneq, qsz, qsz * 2, 0, PRQ_Grow_100, PRQ_Grow_25);
    prqInitDynamic(&self->advanceq, 4, 16, 0, PRQ_Grow_100, PRQ_Grow_100);
    self->runq.shrinkms = 5;
    self->doneq.shrinkms = 5;
    self->advanceq.shrinkms = 5;

    // Autogen begins -----
    saInit(&self->workers, object, 1);
    return true;
    // Autogen ends -------
}

void TaskQueue_destroy(_Inout_ TaskQueue* self)
{
    eventDestroy(&self->workev);
    eventDestroy(&self->shutdownev);
    prqDestroy(&self->advanceq);
    prqDestroy(&self->runq);
    prqDestroy(&self->doneq);
    // Autogen begins -----
    strDestroy(&self->name);
    objRelease(&self->manager);
    saDestroy(&self->workers);
    saDestroy(&self->deferred);
    // Autogen ends -------
}

_objfactory_guaranteed TaskQueue* TaskQueue_create(_In_opt_ strref name, _In_ TaskQueueConfig* tqconfig)
{
    TaskQueue *self;
    self = objInstCreate(TaskQueue);

    strDup(&self->name, name);
    self->tqconfig = *tqconfig;
    self->state = TQState_Init;

    objInstInit(self);

    return self;
}

bool TaskQueue_start(_Inout_ TaskQueue* self, Event* notify)
{
    if(!(self->state == TQState_Init || self->state == TQState_Shutdown))
        return false;

    self->state = TQState_Starting;

    // Acquire an extra reference for the manager thread to own. This makes sure that
    // it can't disappear out from under the mananger.
    objAcquire(self);

    string thrname = 0;
    strNConcat(&thrname, self->name, _S" Manager");
    // start manager thread, it will handle starting the workers and completing the startup
    self->manager = thrCreate(tqManagerThread, thrname, stvar(ptr, self), stvar(ptr, notify));

    return self->manager;
}

_objfactory_guaranteed TaskQueueWorker* TaskQueue_createWorker(_Inout_ TaskQueue* self, int32 num)
{
    return taskqueueworkerCreate(num);
}

// Autogen begins -----
#include "tqobject.auto.inc"
// Autogen ends -------
