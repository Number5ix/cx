// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "ssdsinglenode.h"
// ==================== Auto-generated section ends ======================
#include "../ssdtree.h"

_objfactory_guaranteed SSDSingleNode *SSDSingleNode__create(SSDTree *tree)
{
    SSDSingleNode *self;
    self = objInstCreate(SSDSingleNode);

    self->tree = objAcquire(tree);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

bool SSDSingleNode_get(_Inout_ SSDSingleNode *self, int32 idx, _In_opt_ strref name, stvar *out, SSDLockState *_ssdCurrentLockState)
{
    ssdLockRead(self);
    stvarCopy(out, self->storage);
    return true;
}

stvar *SSDSingleNode_ptr(_Inout_ SSDSingleNode *self, int32 idx, _In_opt_ strref name, SSDLockState *_ssdCurrentLockState)
{
    ssdLockRead(self);
    return &self->storage;
}

bool SSDSingleNode_set(_Inout_ SSDSingleNode *self, int32 idx, _In_opt_ strref name, stvar val, SSDLockState *_ssdCurrentLockState)
{
    ssdLockWrite(self);
    stvarDestroy(&self->storage);
    stvarCopy(&self->storage, val);
    ssdnodeUpdateModified(self);
    return true;
}

bool SSDSingleNode_setC(_Inout_ SSDSingleNode *self, int32 idx, _In_opt_ strref name, stvar *val, SSDLockState *_ssdCurrentLockState)
{
    ssdLockWrite(self);
    stvarDestroy(&self->storage);
    self->storage = *val;
    *val = stvNone;
    ssdnodeUpdateModified(self);
    return true;
}

bool SSDSingleNode_remove(_Inout_ SSDSingleNode *self, int32 idx, _In_opt_ strref name, SSDLockState *_ssdCurrentLockState)
{
    // can't remove a single node!
    return false;
}

SSDIterator *SSDSingleNode_iter(_Inout_ SSDSingleNode *self)
{
    SSDSingleIter *iter = ssdsingleiterCreate(self, NULL);
    return SSDIterator(iter);
}

SSDIterator *SSDSingleNode__iterLocked(_Inout_ SSDSingleNode *self, SSDLockState *_ssdCurrentLockState)
{
    SSDSingleIter *iter = ssdsingleiterCreate(self, _ssdCurrentLockState);
    return SSDIterator(iter);
}

int32 SSDSingleNode_count(_Inout_ SSDSingleNode *self, SSDLockState *_ssdCurrentLockState)
{
    return 1;
}

void SSDSingleNode_destroy(_Inout_ SSDSingleNode *self)
{
    // Autogen begins -----
    _stDestroy(self->storage.type, NULL, &self->storage.data, 0);
    // Autogen ends -------
}

// -------------------------------- ITERATOR --------------------------------

_objfactory_guaranteed SSDSingleIter *SSDSingleIter_create(SSDSingleNode *node, SSDLockState *lstate)
{
    SSDSingleIter *self;
    self = objInstCreate(SSDSingleIter);

    self->node = (SSDNode*)objAcquire(node);
    self->lstate = lstate;

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

bool SSDSingleIter_valid(_Inout_ SSDSingleIter *self)
{
    return !self->done;
}

bool SSDSingleIter_next(_Inout_ SSDSingleIter *self)
{
    self->done = true;
    return true;
}

stvar *SSDSingleIter_ptr(_Inout_ SSDSingleIter *self)
{
    if (self->done)
        return NULL;

    _ssdManualLockRead(self->node, self->lstate);
    // this shouldn't be used in transient lock mode
    devAssert(!self->transient_lock_state.init);

    return &((SSDSingleNode*)self->node)->storage;
}

bool SSDSingleIter_get(_Inout_ SSDSingleIter *self, stvar *out)
{
    if (self->done)
        return false;

    _ssdManualLockRead(self->node, self->lstate);
    stvarCopy(out, ((SSDSingleNode *)self->node)->storage);

    _ssdUnlock(self->node, &self->transient_lock_state);
    return true;
}

int32 SSDSingleIter_idx(_Inout_ SSDSingleIter *self)
{
    return 0;
}

strref SSDSingleIter_name(_Inout_ SSDSingleIter *self)
{
    return _S"0";
}

bool SSDSingleIter_iterOut(_Inout_ SSDSingleIter *self, int32 *idx, strref *name, stvar **val)
{
    if (self->done)
        return false;

    _ssdManualLockRead(self->node, self->lstate);
    // this shouldn't be used in transient lock mode
    devAssert(!self->transient_lock_state.init);

    *idx = 0;
    *name = _S"0";
    *val = &((SSDSingleNode *)self->node)->storage;
    return true;
}

// Autogen begins -----
#include "ssdsinglenode.auto.inc"
// Autogen ends -------
