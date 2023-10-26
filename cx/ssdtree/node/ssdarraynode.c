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

_objfactory SSDArrayNode *SSDArrayNode__create(SSDTree *tree)
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

bool SSDArrayNode_init(_Inout_ SSDArrayNode *self)
{
    // Autogen begins -----
    saInit(&self->storage, stvar, 1);
    return true;
    // Autogen ends -------
}

bool SSDArrayNode_get(_Inout_ SSDArrayNode *self, int32 idx, strref name, stvar *out, SSDLockState *_ssdCurrentLockState)
{
    if (idx == SSD_ByName && !strToInt32(&idx, name, 0, true))
        return false;

    ssdLockRead(self);

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

stvar *SSDArrayNode_ptr(_Inout_ SSDArrayNode *self, int32 idx, strref name, SSDLockState *_ssdCurrentLockState)
{
    if (idx == SSD_ByName && !strToInt32(&idx, name, 0, true))
        return NULL;

    ssdLockRead(self);
    return ptrInternal(self, idx);
}

bool SSDArrayNode_set(_Inout_ SSDArrayNode *self, int32 idx, strref name, stvar val, SSDLockState *_ssdCurrentLockState)
{
    if (idx == SSD_ByName && !strToInt32(&idx, name, 0, true))
        return false;

    if (idx < 0)
        return false;

    ssdLockWrite(self);

    if (idx >= saSize(self->storage))
        saSetSize(&self->storage, idx + 1);

    stvarDestroy(&self->storage.a[idx]);
    stvarCopy(&self->storage.a[idx], val);
    ssdnodeUpdateModified(self);
    return true;
}

bool SSDArrayNode_setC(_Inout_ SSDArrayNode *self, int32 idx, strref name, stvar *val, SSDLockState *_ssdCurrentLockState)
{
    bool ret = false;

    if (idx == SSD_ByName && !strToInt32(&idx, name, 0, true))
        goto out;

    if (idx < 0)
        goto out;

    ssdLockWrite(self);

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

bool SSDArrayNode_append(_Inout_ SSDArrayNode *self, stvar val, SSDLockState *_ssdCurrentLockState)
{
    ssdLockWrite(self);
    saPush(&self->storage, stvar, val);
    ssdnodeUpdateModified(self);
    return true;
}

bool SSDArrayNode_remove(_Inout_ SSDArrayNode *self, int32 idx, strref name, SSDLockState *_ssdCurrentLockState)
{
    if (idx == SSD_ByName && !strToInt32(&idx, name, 0, true))
        return false;

    ssdLockWrite(self);

    // upper bounds check for idx happens in saRemove
    bool ret = saRemove(&self->storage, idx);
    if (ret)
        ssdnodeUpdateModified(self);
    return ret;
}

SSDIterator *SSDArrayNode_iter(_Inout_ SSDArrayNode *self)
{
    SSDArrayIter *ret = ssdarrayiterCreate(self, NULL);
    return SSDIterator(ret);
}

SSDIterator *SSDArrayNode__iterLocked(_Inout_ SSDArrayNode *self, SSDLockState *_ssdCurrentLockState)
{
    SSDArrayIter *ret = ssdarrayiterCreate(self, _ssdCurrentLockState);
    return SSDIterator(ret);
}

int32 SSDArrayNode_count(_Inout_ SSDArrayNode *self, SSDLockState *_ssdCurrentLockState)
{
    ssdLockRead(self);
    return saSize(self->storage);
}

extern bool SSDNode_isArray(_Inout_ SSDNode *self); // parent
#define parent_isArray() SSDNode_isArray((SSDNode*)(self))
bool SSDArrayNode_isArray(_Inout_ SSDArrayNode *self)
{
    return true;
}

void SSDArrayNode_destroy(_Inout_ SSDArrayNode *self)
{
    // Autogen begins -----
    saDestroy(&self->storage);
    // Autogen ends -------
}

// -------------------------------- ITERATOR --------------------------------

_objfactory SSDArrayIter *SSDArrayIter_create(SSDArrayNode *node, SSDLockState *lstate)
{
    SSDArrayIter *self;
    self = objInstCreate(SSDArrayIter);

    self->node = (SSDNode*)objAcquire(node);
    self->lstate = lstate;

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

bool SSDArrayIter_valid(_Inout_ SSDArrayIter *self)
{
    _ssdManualLockRead(self->node, self->lstate);
    bool ret = self->idx >= 0 && self->idx < saSize(((SSDArrayNode *)self->node)->storage);
    _ssdUnlock(self->node, &self->transient_lock_state);
    return ret;
}

bool SSDArrayIter_next(_Inout_ SSDArrayIter *self)
{
    bool ret = false;
    _ssdManualLockRead(self->node, self->lstate);
    if (self->idx >= 0 && self->idx < saSize(((SSDArrayNode*)self->node)->storage) - 1) {
        self->idx++;
        ret = true;
    } else {
        self->idx = -1;
    }
    _ssdUnlock(self->node, &self->transient_lock_state);
    return ret;
}

stvar *SSDArrayIter_ptr(_Inout_ SSDArrayIter *self)
{
    _ssdManualLockRead(self->node, self->lstate);
    // this does not unlock the transient lock!
    // you really shouldn't be using ptr() with the transient lock anyway...
    devAssert(!self->transient_lock_state.init);
    return ptrInternal((SSDArrayNode*)self->node, self->idx);
}

bool SSDArrayIter_get(_Inout_ SSDArrayIter *self, stvar *out)
{
    _ssdManualLockRead(self->node, self->lstate);
    stvar *ptr = ptrInternal((SSDArrayNode*)self->node, self->idx);
    bool ret = false;
    if (ptr) {
        stvarCopy(out, *ptr);
        ret = true;
    }

    _ssdUnlock(self->node, &self->transient_lock_state);
    return ret;
}

int32 SSDArrayIter_idx(_Inout_ SSDArrayIter *self)
{
    return self->idx;
}

strref SSDArrayIter_name(_Inout_ SSDArrayIter *self)
{
    strFromInt32(&self->lastName, self->idx, 0);
    return self->lastName;
}

bool SSDArrayIter_iterOut(_Inout_ SSDArrayIter *self, int32 *idx, strref *name, stvar **val)
{
    _ssdManualLockRead(self->node, self->lstate);
    // see comments in SSDArrayIter_ptr about the transient lock
    devAssert(!self->transient_lock_state.init);

    if (self->idx < 0 || self->idx >= saSize(((SSDArrayNode *)self->node)->storage))
        return false;

    *idx = self->idx;
    strFromInt32(&self->lastName, self->idx, 0);
    *name = self->lastName;
    *val = ptrInternal((SSDArrayNode *)self->node, self->idx);
    return true;
}

extern bool SSDIterator_isArray(_Inout_ SSDIterator *self); // parent
#undef parent_isArray
#define parent_isArray() SSDIterator_isArray((SSDIterator*)(self))
bool SSDArrayIter_isArray(_Inout_ SSDArrayIter *self)
{
    return true;
}

void SSDArrayIter_destroy(_Inout_ SSDArrayIter *self)
{
    // Autogen begins -----
    strDestroy(&self->lastName);
    // Autogen ends -------
}

// Autogen begins -----
#include "ssdarraynode.auto.inc"
// Autogen ends -------
