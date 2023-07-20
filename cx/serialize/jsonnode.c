// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/core/objstdif.h>
#include <cx/debug/assert.h>
#include <cx/container.h>
#include <cx/string.h>
#include "jsonnode.h"
// ==================== Auto-generated section ends ======================

extern SSDNode *SSDNode_createLike(SSDNode *self, SSDInfo *info); // parent
#define parent_createLike(info) SSDNode_createLike((SSDNode*)(self), info)
SSDNode *JSONNode_createLike(JSONNode *self, SSDInfo *info)
{
    JSONNode *ret = jsonnodeCreate(info);
    return SSDNode(ret);
}

JSONNode *JSONNode_create(SSDInfo *info)
{
    JSONNode *self;
    self = objInstCreate(JSONNode);

    self->info = objAcquire(info);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

bool JSONNode_init(JSONNode *self)
{
    // Autogen begins -----
    saInit(&self->keyorder, string, 1);
    return true;
    // Autogen ends -------
}

void JSONNode_destroy(JSONNode *self)
{
    // Autogen begins -----
    saDestroy(&self->keyorder);
    // Autogen ends -------
}

// Autogen begins -----
#include "jsonnode.auto.inc"
// Autogen ends -------
