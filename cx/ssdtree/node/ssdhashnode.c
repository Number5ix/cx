// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/core/objstdif.h>
#include <cx/debug/assert.h>
#include <cx/container.h>
#include <cx/string.h>
#include "ssdhashnode.h"
// ==================== Auto-generated section ends ======================
#include "../ssdtree.h"

SSDHashNode *SSDHashNode_create(SSDInfo *info)
{
    SSDHashNode *self;
    self = objInstCreate(SSDHashNode);

    self->info = objAcquire(info);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

bool SSDHashNode_init(SSDHashNode *self)
{
    flags_t htflags = 0;
    if (self->info->flags & SSD_CaseInsensitive)
        htflags = HT_CaseInsensitive;
    htInit(&self->storage, string, stvar, 16, htflags);

    // Autogen begins -----
    return true;
    // Autogen ends -------
}

bool SSDHashNode_get(SSDHashNode *self, int32 idx, strref name, stvar *out, SSDLock *lock)
{
    bool ret = false;

    if (idx != SSD_ByName)
        goto out;

    ssdLockRead(self, lock);
    ret = htFind(self->storage, strref, name, stvar, out);

out:
    if (!ret)
        *out = stvNone;
    return ret;
}

stvar *SSDHashNode_ptr(SSDHashNode *self, int32 idx, strref name, SSDLock *lock)
{
    if (idx != SSD_ByName)
        return NULL;

    ssdLockRead(self, lock);

    htelem elem = htFind(self->storage, strref, name, none, NULL);
    if (elem)
        return hteValPtr(self->storage, elem, stvar);

    return NULL;
}

bool SSDHashNode_set(SSDHashNode *self, int32 idx, strref name, stvar val, SSDLock *lock)
{
    if (idx != SSD_ByName)
        return false;

    ssdLockWrite(self, lock);
    htInsert(&self->storage, strref, name, stvar, val);
    return true;
}

bool SSDHashNode_setC(SSDHashNode *self, int32 idx, strref name, stvar *val, SSDLock *lock)
{
    if (idx != SSD_ByName) {
        stvarDestroy(val);
        return false;
    }

    ssdLockWrite(self, lock);
    htInsertC(&self->storage, strref, name, stvar, val);
    return true;
}

bool SSDHashNode_remove(SSDHashNode *self, int32 idx, strref name, SSDLock *lock)
{
    if (idx != SSD_ByName)
        return false;

    ssdLockWrite(self, lock);
    return htRemove(&self->storage, strref, name);
}

SSDIterator *SSDHashNode_iter(SSDHashNode *self)
{
    SSDHashIter *ret = ssdhashiterCreate(self);
    return SSDIterator(ret);
}

int32 SSDHashNode_count(SSDHashNode *self, SSDLock *lock)
{
    ssdLockRead(self, lock);
    return htSize(self->storage);
}

extern bool SSDNode_isObject(SSDNode *self); // parent
#define parent_isObject() SSDNode_isObject((SSDNode*)(self))
bool SSDHashNode_isObject(SSDHashNode *self)
{
    return true;
}

void SSDHashNode_destroy(SSDHashNode *self)
{
    // Autogen begins -----
    htDestroy(&self->storage);
    // Autogen ends -------
}

// -------------------------------- ITERATOR --------------------------------

SSDHashIter *SSDHashIter_create(SSDHashNode *node)
{
    SSDHashIter *self;
    self = objInstCreate(SSDHashIter);

    self->node = objAcquire(node);
    htiInit(&self->iter, self->node->storage);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

bool SSDHashIter_valid(SSDHashIter *self)
{
    return htiValid(&self->iter);
}

bool SSDHashIter_next(SSDHashIter *self)
{
    return htiNext(&self->iter);
}

stvar *SSDHashIter_ptr(SSDHashIter *self)
{
    if (!htiValid(&self->iter))
        return NULL;

    return htiValPtr(self->iter, stvar);
}

bool SSDHashIter_get(SSDHashIter *self, stvar *out)
{
    if (!htiValid(&self->iter))
        return false;

    stvarCopy(out, htiVal(self->iter, stvar));
    return true;
}

int32 SSDHashIter_idx(SSDHashIter *self)
{
    return SSD_ByName;
}

bool SSDHashIter_name(SSDHashIter *self, string *out)
{
    if (!htiValid(&self->iter))
        return false;

    strDup(out, htiKey(self->iter, strref));
    return true;
}

void SSDHashIter_destroy(SSDHashIter *self)
{
    htiFinish(&self->iter);

    // Autogen begins -----
    objRelease(&self->node);
    // Autogen ends -------
}

// Autogen begins -----
#include "ssdhashnode.auto.inc"
// Autogen ends -------
