// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/core/objstdif.h>
#include <cx/debug/assert.h>
#include <cx/container.h>
#include <cx/string.h>
#include "threadobj.h"
// ==================== Auto-generated section ends ======================
#include "thread_private.h"

Thread *Thread_create(threadFunc func, strref name, int n, stvar args[], bool ui)
{
    Thread *self;
    self = _thrPlatformCreate();
    //self = objInstCreate(Thread);

    strDup(&self->name, name);
    self->entry = func;
    saInit(&self->_argsa, stvar, 1);
    for (int i = 0; i < n; i++) {
        saPush(&self->_argsa, stvar, args[i]);
    }

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

bool Thread_init(Thread *self)
{
    stvlInitSA(&self->args, self->_argsa);
    eventInit(&self->notify);

    // Autogen begins -----
    return true;
    // Autogen ends -------
}

void Thread_destroy(Thread *self)
{
    eventDestroy(&self->notify);
    // Autogen begins -----
    strDestroy(&self->name);
    saDestroy(&self->_argsa);
    // Autogen ends -------
}

// Autogen begins -----
#include "threadobj.auto.inc"
// Autogen ends -------