// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "basictask.h"
// ==================== Auto-generated section ends ======================
#include "taskqueue.h"

bool BasicTask_reset(_Inout_ BasicTask* self)
{
    int32 oldval = atomicLoad(int32, &self->state, Relaxed);
    if(oldval != TASK_Succeeded && oldval != TASK_Failed)
        return false;

    while(!atomicCompareExchange(int32, weak, &self->state, &oldval, TASK_Created, AcqRel, Relaxed)) {
        if(oldval != TASK_Succeeded && oldval != TASK_Failed)
            return false;
    }

    return true;
}

// Autogen begins -----
#include "basictask.auto.inc"
// Autogen ends -------
