// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "tqtestobj.h"
// ==================== Auto-generated section ends ======================
#include <cx/taskqueue.h>

_objfactory_guaranteed TQTest1 *TQTest1_create(int num1, int num2, Event *notify)
{
    TQTest1 *self;
    self = objInstCreate(TQTest1);

    self->num[0] = num1;
    self->num[1] = num2;
    self->notify = notify;

    objInstInit(self);

    return self;
}

bool TQTest1_run(_Inout_ TQTest1 *self, _In_ TaskQueue *tq, _Inout_ TaskControl *tcon)
{
    self->total = self->num[0] + self->num[1];
    tcon->notifyev = self->notify;

    return true;
}

_objfactory_guaranteed TQTestFail *TQTestFail_create(int n, Event *notify)
{
    TQTestFail *self;
    self = objInstCreate(TQTestFail);

    self->n = n;
    self->notify = notify;

    objInstInit(self);

    return self;
}

bool TQTestFail_run(_Inout_ TQTestFail *self, _In_ TaskQueue *tq, _Inout_ TaskControl *tcon)
{
    tcon->notifyev = self->notify;
    return self->n & 1;     // odd will succeed, even will fail
}

_objfactory_guaranteed TQTestCC1 *TQTestCC1_create(int num1, int num2, TaskQueue *destq, int *accum, int *counter, Event *notify)
{
    TQTestCC1 *self;
    self = objInstCreate(TQTestCC1);

    self->num[0] = num1;
    self->num[1] = num2;
    self->destq = destq;
    self->accum = accum;
    self->counter = counter;
    self->notify = notify;

    objInstInit(self);

    return self;
}

bool TQTestCC1_run(_Inout_ TQTestCC1 *self, _In_ TaskQueue *tq, _Inout_ TaskControl *tcon)
{
    TQTestCC2 *task = tqtestcc2Create(self->num[0] + self->num[1], self->accum, self->counter, self->notify);
    tqRun(self->destq, &task);
    return true;
}

_objfactory_guaranteed TQTestCC2 *TQTestCC2_create(int total, int *accum, int *counter, Event *notify)
{
    TQTestCC2 *self;
    self = objInstCreate(TQTestCC2);

    self->total = total;
    self->accum = accum;
    self->counter = counter;
    self->notify = notify;

    objInstInit(self);

    return self;
}

bool TQTestCC2_run(_Inout_ TQTestCC2 *self, _In_ TaskQueue *tq, _Inout_ TaskControl *tcon)
{
    *self->accum += self->total;
    *self->counter += 1;
    tcon->notifyev = self->notify;
    return true;
}

// Autogen begins -----
#include "tqtestobj.auto.inc"
// Autogen ends -------
