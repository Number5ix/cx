// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "ssdtreeobj.h"
// ==================== Auto-generated section ends ======================
#include "ssdnodes.h"

SSDTree *SSDTree_create(uint32 flags)
{
    SSDTree *self;
    self = objInstCreate(SSDTree);

    self->flags = flags;

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

bool SSDTree_init(SSDTree *self)
{
    // default factories
    if (!self->factories[SSD_Create_Hashtable])
        self->factories[SSD_Create_Hashtable] = (SSDNodeFactory)SSDHashNode__create;
    if (!self->factories[SSD_Create_Array])
        self->factories[SSD_Create_Array] = (SSDNodeFactory)SSDArrayNode__create;
    if (!self->factories[SSD_Create_Single])
        self->factories[SSD_Create_Single] = (SSDNodeFactory)SSDSingleNode__create;

    rwlockInit(&self->lock);
    // Autogen begins -----
    return true;
    // Autogen ends -------
}

SSDNode *SSDTree_createNode(SSDTree *self, int crtype)
{
    devAssert(crtype >= 0 && crtype < SSD_Create_Count);
    if (!(crtype >= 0 && crtype < SSD_Create_Count) || !self->factories[crtype])
        return NULL;

    return self->factories[crtype](self);
}

void SSDTree_destroy(SSDTree *self)
{
    rwlockDestroy(&self->lock);
}

// Autogen begins -----
#include "ssdtreeobj.auto.inc"
// Autogen ends -------