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

SSDSingleNode *SSDSingleNode_create(SSDInfo *info, stvar initval)
{
    SSDSingleNode *self;
    self = objInstCreate(SSDSingleNode);

    self->info = objAcquire(info);
    stvarCopy(&self->storage, initval);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

bool SSDSingleNode_get(SSDSingleNode *self, int32 idx, strref name, stvar *out, SSDLock *lock)
{
    ssdLockRead(self, lock);
    stvarCopy(out, self->storage);
    return true;
}

stvar *SSDSingleNode_ptr(SSDSingleNode *self, int32 idx, strref name, SSDLock *lock)
{
    ssdLockRead(self, lock);
    return &self->storage;
}

bool SSDSingleNode_set(SSDSingleNode *self, int32 idx, strref name, stvar val, SSDLock *lock)
{
    ssdLockWrite(self, lock);
    stvarDestroy(&self->storage);
    stvarCopy(&self->storage, val);
    return true;
}

bool SSDSingleNode_setC(SSDSingleNode *self, int32 idx, strref name, stvar *val, SSDLock *lock)
{
    ssdLockWrite(self, lock);
    stvarDestroy(&self->storage);
    self->storage = *val;
    *val = stvNone;
    return true;
}

bool SSDSingleNode_remove(SSDSingleNode *self, int32 idx, strref name, SSDLock *lock)
{
    // can't remove a single node!
    return false;
}

SSDIterator *SSDSingleNode_iter(SSDSingleNode *self)
{
    SSDSingleIter *iter = ssdsingleiterCreate(self);
    return SSDIterator(iter);
}

int32 SSDSingleNode_count(SSDSingleNode *self, SSDLock *lock)
{
    return 1;
}


void SSDSingleNode_destroy(SSDSingleNode *self)
{
    // Autogen begins -----
    _stDestroy(self->storage.type, NULL, &self->storage.data, 0);
    // Autogen ends -------
}

// -------------------------------- ITERATOR --------------------------------

SSDSingleIter *SSDSingleIter_create(SSDSingleNode *node)
{
    SSDSingleIter *self;
    self = objInstCreate(SSDSingleIter);

    self->node = objAcquire(node);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

bool SSDSingleIter_valid(SSDSingleIter *self)
{
    return !self->done;
}

bool SSDSingleIter_next(SSDSingleIter *self)
{
    self->done = true;
    return true;
}

stvar *SSDSingleIter_ptr(SSDSingleIter *self)
{
    if (self->done)
        return NULL;

    return &self->node->storage;
}

bool SSDSingleIter_get(SSDSingleIter *self, stvar *out)
{
    if (self->done)
        return false;

    stvarCopy(out, self->node->storage);
    return true;
}

int32 SSDSingleIter_idx(SSDSingleIter *self)
{
    return 0;
}

bool SSDSingleIter_name(SSDSingleIter *self, string *out)
{
    return false;
}

void SSDSingleIter_destroy(SSDSingleIter *self)
{
    // Autogen begins -----
    objRelease(&self->node);
    // Autogen ends -------
}

// Autogen begins -----
#include "ssdsinglenode.auto.inc"
// Autogen ends -------
