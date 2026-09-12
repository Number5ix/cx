// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "taskqueue/userfunctask.h"
// clang-format on
// ==================== Auto-generated section ends ======================

_objfactory_guaranteed UserFuncTask* UserFuncTask_create(closure cls)
{
    UserFuncTask* self;
    self = objInstCreate(UserFuncTask);

    // Owned from here on, including if this task is cancelled or the queue shuts down without
    // ever running it -- the generated destructor is what closes that path, and is the reason
    // this holds a closure rather than a function pointer and a context to free by hand.
    self->cls = cls;

    objInstInit(self);

    return self;
}

uint32 UserFuncTask_run(_In_ UserFuncTask* self, _In_ TaskQueue* tq, _In_ TQWorker* worker,
                        _Inout_ TaskControl* tcon)
{
    return (self->cls && closureCall(self->cls, stvar(object, tq))) ? TASK_Result_Success
                                                                    : TASK_Result_Failure;
}

void UserFuncTask_destroy(_In_ UserFuncTask* self)
{
    // Autogen begins -----
    closureDestroy(&self->cls);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "taskqueue/userfunctask.auto.inc"
// clang-format on
// Autogen ends -------
