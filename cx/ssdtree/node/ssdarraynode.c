// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "ssdarraynode.h"
// ==================== Auto-generated section ends ======================
#include "../ssdtree.h"

SSDArrayNode *SSDArrayNode__create(SSDTree *tree)
{
    SSDArrayNode *self;
    self = objInstCreate(SSDArrayNode);

    self->tree = objAcquire(tree);
    ssdnodeUpdateModified(self);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

bool SSDArrayNode_init(SSDArrayNode *self)
{
    // Autogen begins -----
    saInit(&self->storage, stvar, 1);
    return true;
    // Autogen ends -------
}

bool SSDArrayNode_get(SSDArrayNode *self, int32 idx, strref name, stvar *out, SSDLock *lock)
{
    if (idx == SSD_ByName && !strToInt32(&idx, name, 0, true))
        return false;

    ssdLockRead(self, lock);

    if (idx < 0 || idx >= saSize(self->storage))
        return false;

    stvarCopy(out, self->storage.a[idx]);
    return true;
}

static stvar *ptrInternal(SSDArrayNode *self, int32 idx)
{
    if (idx < 0 || idx >= saSize(self->storage))
        return NULL;
    return &self->storage.a[idx];
}

stvar *SSDArrayNode_ptr(SSDArrayNode *self, int32 idx, strref name, SSDLock *lock)
{
    if (idx == SSD_ByName && !strToInt32(&idx, name, 0, true))
        return NULL;

    ssdLockRead(self, lock);
    return ptrInternal(self, idx);
}

bool SSDArrayNode_set(SSDArrayNode *self, int32 idx, strref name, stvar val, SSDLock *lock)
{
    if (idx == SSD_ByName && !strToInt32(&idx, name, 0, true))
        return false;

    if (idx < 0)
        return false;

    ssdLockWrite(self, lock);

    if (idx >= saSize(self->storage))
        saSetSize(&self->storage, idx + 1);

    stvarDestroy(&self->storage.a[idx]);
    stvarCopy(&self->storage.a[idx], val);
    ssdnodeUpdateModified(self);
    return true;
}

bool SSDArrayNode_setC(SSDArrayNode *self, int32 idx, strref name, stvar *val, SSDLock *lock)
{
    bool ret = false;

    if (idx == SSD_ByName && !strToInt32(&idx, name, 0, true))
        goto out;

    if (idx < 0)
        goto out;

    ssdLockWrite(self, lock);

    if (idx >= saSize(self->storage))
        saSetSize(&self->storage, idx + 1);

    stvarDestroy(&self->storage.a[idx]);
    self->storage.a[idx] = *val;
    *val = stvNone;
    ssdnodeUpdateModified(self);
    ret = true;

out:
    if (ret)
        *val = stvNone;
    else
        stvarDestroy(val);

    return ret;
}

bool SSDArrayNode_remove(SSDArrayNode *self, int32 idx, strref name, SSDLock *lock)
{
    if (idx == SSD_ByName && !strToInt32(&idx, name, 0, true))
        return false;

    ssdLockWrite(self, lock);

    // upper bounds check for idx happens in saRemove
    bool ret = saRemove(&self->storage, idx);
    if (ret)
        ssdnodeUpdateModified(self);
    return ret;
}

SSDIterator *SSDArrayNode_iter(SSDArrayNode *self)
{
    SSDArrayIter *ret = ssdarrayiterCreate(self, NULL);
    return SSDIterator(ret);
}

SSDIterator *SSDArrayNode_iterLocked(SSDArrayNode *self, SSDLock *lock)
{
    SSDArrayIter *ret = ssdarrayiterCreate(self, lock);
    return SSDIterator(ret);
}

int32 SSDArrayNode_count(SSDArrayNode *self, SSDLock *lock)
{
    ssdLockRead(self, lock);
    return saSize(self->storage);
}

extern bool SSDNode_isArray(SSDNode *self); // parent
#define parent_isArray() SSDNode_isArray((SSDNode*)(self))
bool SSDArrayNode_isArray(SSDArrayNode *self)
{
    return true;
}

void SSDArrayNode_destroy(SSDArrayNode *self)
{
    // Autogen begins -----
    saDestroy(&self->storage);
    // Autogen ends -------
}

// -------------------------------- ITERATOR --------------------------------

SSDArrayIter *SSDArrayIter_create(SSDArrayNode *node, SSDLock *lock)
{
    SSDArrayIter *self;
    self = objInstCreate(SSDArrayIter);

    self->node = (SSDNode*)objAcquire(node);
    self->lock = lock;

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

bool SSDArrayIter_valid(SSDArrayIter *self)
{
    return self->idx >= 0 && self->idx < saSize(((SSDArrayNode*)self->node)->storage);
}

bool SSDArrayIter_next(SSDArrayIter *self)
{
    if (self->idx >= 0 && self->idx < saSize(((SSDArrayNode*)self->node)->storage) - 1) {
        self->idx++;
        return true;
    } else {
        self->idx = -1;
        return false;
    }
}

stvar *SSDArrayIter_ptr(SSDArrayIter *self)
{
    return ptrInternal((SSDArrayNode*)self->node, self->idx);
}

bool SSDArrayIter_get(SSDArrayIter *self, stvar *out)
{
    stvar *ptr = ptrInternal((SSDArrayNode*)self->node, self->idx);
    if (ptr) {
        stvarCopy(out, *ptr);
        return true;
    }
    return false;
}

int32 SSDArrayIter_idx(SSDArrayIter *self)
{
    return self->idx;
}

strref SSDArrayIter_name(SSDArrayIter *self)
{
    strFromInt32(&self->lastName, self->idx, 0);
    return self->lastName;
}

bool SSDArrayIter_iterOut(SSDArrayIter *self, int32 *idx, strref *name, stvar **val)
{
    if (self->idx < 0 || self->idx >= saSize(((SSDArrayNode *)self->node)->storage))
        return false;

    *idx = self->idx;
    strFromInt32(&self->lastName, self->idx, 0);
    *name = self->lastName;
    *val = ptrInternal((SSDArrayNode *)self->node, self->idx);
    return true;
}

extern bool SSDIterator_isArray(SSDIterator *self); // parent
#undef parent_isArray
#define parent_isArray() SSDIterator_isArray((SSDIterator*)(self))
bool SSDArrayIter_isArray(SSDArrayIter *self)
{
    return true;
}

void SSDArrayIter_destroy(SSDArrayIter *self)
{
    // Autogen begins -----
    strDestroy(&self->lastName);
    // Autogen ends -------
}

// Autogen begins -----
#include "ssdarraynode.auto.inc"
// Autogen ends -------
