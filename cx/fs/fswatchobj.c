// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "fs/fswatchobj.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "fswatch_private.h"

#include <cx/platform/os.h>
#include <cx/time/time.h>

_objinit_guaranteed bool FSWatch_init(_In_ FSWatch* self)
{
    mutexInit(&self->tlock);
    saInit(&self->tpaths, string, 4);
    _fsWatchDispatchInit();

    // Autogen begins -----
    return true;
    // Autogen ends -------
}

void FSWatch_cancel(_In_ FSWatch* self)
{
    if (!self)
        return;

    // Paired with the dispatcher, which raises busy before it checks cancelled. Both sides are
    // sequentially consistent, so either the dispatcher sees this flag and skips the callback,
    // or the loop below sees its busy count and waits for the callback to return.
    atomicStore(bool, &self->cancelled, true, SeqCst);

    fsWatchStopSources(self);

    // From inside a callback, a callback of this watch may be what called us; waiting for it
    // would be waiting for ourselves. Every callback runs on the dispatch thread, so on any
    // other thread there is nothing to be careful of.
    if (!_fsWatchOnDispatchThread()) {
        while (atomicLoad(int32, &self->busy, SeqCst) > 0) osSleep(timeMS(1));
    }
}

void FSWatch_destroy(_In_ FSWatch* self)
{
    // Autogen begins -----
    closureDestroy(&self->cls);
    mutexDestroy(&self->tlock);
    saDestroy(&self->tpaths);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "fs/fswatchobj.auto.inc"
// clang-format on
// Autogen ends -------
