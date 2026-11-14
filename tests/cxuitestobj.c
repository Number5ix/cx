// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "cxuitestobj.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include <cx/format.h>

_objfactory_guaranteed UiTestTree* UiTestTree_create()
{
    UiTestTree* self;
    self = objInstCreate(UiTestTree);

    // Insert any pre-initialization object construction here

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

_objinit_guaranteed bool UiTestTree_init(_In_ UiTestTree* self)
{
    // Autogen begins -----
    saInit(&self->order, uint64, 1);
    saInit(&self->parents, uint64, 1);
    return true;
    // Autogen ends -------
}

CxUiNodeId UiTestTree_add(_In_ UiTestTree* self, CxUiNodeId parent)
{
    saPush(&self->parents, uint64, parent);
    CxUiNodeId n = saSize(self->parents);
    saPush(&self->order, uint64, n);
    return n;
}

CxUiNodeId UiTestTree_firstChild(_In_ UiTestTree* self, CxUiNodeId parent)
{
    for (int32 i = 0; i < saSize(self->order); i++) {
        if (self->parents.a[self->order.a[i] - 1] == parent)
            return self->order.a[i];
    }
    return 0;
}

CxUiNodeId UiTestTree_nextSibling(_In_ UiTestTree* self, CxUiNodeId node)
{
    int32 at = saFind(self->order, uint64, node);
    if (at < 0)
        return 0;
    CxUiNodeId parent = self->parents.a[node - 1];
    for (int32 i = at + 1; i < saSize(self->order); i++) {
        if (self->parents.a[self->order.a[i] - 1] == parent)
            return self->order.a[i];
    }
    return 0;
}

CxUiNodeId UiTestTree_parentOf(_In_ UiTestTree* self, CxUiNodeId node)
{
    return node >= 1 && node <= (CxUiNodeId)saSize(self->parents) ? self->parents.a[node - 1] : 0;
}

bool UiTestTree_hasChildren(_In_ UiTestTree* self, CxUiNodeId node)
{
    return UiTestTree_firstChild(self, node) != 0;
}

bool UiTestTree_nodeCell(_In_ UiTestTree* self, _Out_ CxUiCell* out, CxUiNodeId node, int32 col)
{
    if (col != 0 || saFind(self->order, uint64, node) < 0)
        return false;
    strFormat(&out->text, _S"t${uint}", stvar(uint64, node));
    return true;
}

void UiTestTree_destroy(_In_ UiTestTree* self)
{
    // Autogen begins -----
    saDestroy(&self->order);
    saDestroy(&self->parents);
    strDestroy(&self->editedText);
    // Autogen ends -------
}

bool UiTestTree_setCellText(_In_ UiTestTree* self, uint64 item, int32 col, _In_ strref text)
{
    self->edits++;
    self->editedNode = item;
    strDup(&self->editedText, text);
    return true;
}

bool UiTestTree_setCellCheck(_In_ UiTestTree* self, uint64 item, int32 col, uint32 state)
{
    return false;
}

// Autogen begins -----
// clang-format off
#include "cxuitestobj.auto.inc"
// clang-format on
// Autogen ends -------
