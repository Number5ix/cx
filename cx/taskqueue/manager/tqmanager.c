// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "tqmanager.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cx/taskqueue/taskqueue_private.h"

void TQManager_destroy(_Inout_ TQManager* self)
{
    // Autogen begins -----
    objRelease(&self->tq);
    // Autogen ends -------
}

bool TQManager_start(_Inout_ TQManager* self, _In_ TaskQueue* tq)
{
    self->tq = objAcquire(tq);
    return true;
}

bool TQManager_stop(_Inout_ TQManager* self)
{
    objRelease(&self->tq);
    return true;
}

int64 TQManager_tick(_Inout_ TQManager* self)
{
    return timeForever;
}

void TQManager_notify(_Inout_ TQManager* self, bool wakeup)
{
}

void TQManager_pretask(_Inout_ TQManager* self)
{
}

// Autogen begins -----
#include "tqmanager.auto.inc"
// Autogen ends -------
