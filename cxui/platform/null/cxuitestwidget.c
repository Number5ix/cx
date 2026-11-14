// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "platform/null/cxuitestwidget.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "../../core/cxui_private.h"

_objfactory_guaranteed CxUiTestWidget* CxUiTestWidget_create(uint32 events)
{
    CxUiTestWidget* self;
    self = objInstCreate(CxUiTestWidget);
    self->events = events;

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

_objinit_guaranteed bool CxUiTestWidget_init(_In_ CxUiTestWidget* self)
{
    saInit(&self->children, object, 4);
    // Autogen begins -----
    return true;
    // Autogen ends -------
}

extern uint32 CxUiWidget_eventMask(_In_ CxUiWidget* self);   // parent
#define parent_eventMask() CxUiWidget_eventMask((CxUiWidget*)(self))
uint32 CxUiTestWidget_eventMask(_In_ CxUiTestWidget* self)
{
    return parent_eventMask() | self->events;
}

static bool testWidgetInsert(CxUiTestWidget* self, CxUiWidget* child, int32 at)
{
    if (!_cxuiLive(CxUiWidget(self)) || !_cxuiLive(child) || child->parent)
        return false;
    at = clamp(at, 0, saSize(self->children));
    saInsert(&self->children, at, object, child);
    child->parent = CxUiWidget(self);
    _cxuiLayoutAttached(CxUiWidget(self), child);
    return true;
}

bool CxUiTestWidget_append(_In_ CxUiTestWidget* self, _In_ CxUiWidget* child)
{
    return testWidgetInsert(self, child, saSize(self->children));
}

bool CxUiTestWidget_insert(_In_ CxUiTestWidget* self, _In_ CxUiWidget* child, int32 at)
{
    return testWidgetInsert(self, child, at);
}

bool CxUiTestWidget_detach(_In_ CxUiTestWidget* self, _In_ CxUiWidget* child)
{
    int32 i = saFind(self->children, object, child);
    if (i < 0)
        return false;
    objAcquire(child);
    saRemove(&self->children, i);
    child->parent = NULL;
    _cxuiLayoutDetached(CxUiWidget(self), child);
    objRelease(&child);
    return true;
}

int32 CxUiTestWidget_childCount(_In_ CxUiTestWidget* self)
{
    return saSize(self->children);
}

_Ret_opt_valid_ CxUiWidget* CxUiTestWidget_child(_In_ CxUiTestWidget* self, int32 i)
{
    return (i >= 0 && i < saSize(self->children)) ? self->children.a[i] : NULL;
}

void CxUiTestWidget_destroy(_In_ CxUiTestWidget* self)
{
    // Autogen begins -----
    saDestroy(&self->children);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "platform/null/cxuitestwidget.auto.inc"
// clang-format on
// Autogen ends -------
