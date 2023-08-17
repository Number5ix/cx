// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "ssdhashnode.h"
// ==================== Auto-generated section ends ======================
#include "../ssdtree.h"

SSDHashNode *SSDHashNode__create(SSDTree *tree)
{
    SSDHashNode *self;
    self = objInstCreate(SSDHashNode);

    self->tree = objAcquire(tree);
    ssdnodeUpdateModified(self);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

bool SSDHashNode_init(SSDHashNode *self)
{
    flags_t htflags = 0;
    if (self->tree->flags & SSD_CaseInsensitive)
        htflags = HT_CaseInsensitive;
    htInit(&self->storage, string, stvar, 16, htflags);

    // Autogen begins -----
    return true;
    // Autogen ends -------
}

bool SSDHashNode_get(SSDHashNode *self, int32 idx, strref name, stvar *out, SSDLockState *_ssdCurrentLockState)
{
    bool ret = false;

    if (idx != SSD_ByName)
        goto out;

    ssdLockRead(self);
    ret = htFind(self->storage, strref, name, stvar, out);

out:
    if (!ret)
        *out = stvNone;
    return ret;
}

stvar *SSDHashNode_ptr(SSDHashNode *self, int32 idx, strref name, SSDLockState *_ssdCurrentLockState)
{
    if (idx != SSD_ByName)
        return NULL;

    ssdLockRead(self);

    htelem elem = htFind(self->storage, strref, name, none, NULL);
    if (elem)
        return hteValPtr(self->storage, elem, stvar);

    return NULL;
}

bool SSDHashNode_set(SSDHashNode *self, int32 idx, strref name, stvar val, SSDLockState *_ssdCurrentLockState)
{
    if (idx != SSD_ByName)
        return false;

    ssdLockWrite(self);
    htInsert(&self->storage, strref, name, stvar, val);
    ssdnodeUpdateModified(self);
    return true;
}

bool SSDHashNode_setC(SSDHashNode *self, int32 idx, strref name, stvar *val, SSDLockState *_ssdCurrentLockState)
{
    if (idx != SSD_ByName) {
        stvarDestroy(val);
        return false;
    }

    ssdLockWrite(self);
    htInsertC(&self->storage, strref, name, stvar, val);
    *val = stvNone;
    ssdnodeUpdateModified(self);
    return true;
}

bool SSDHashNode_remove(SSDHashNode *self, int32 idx, strref name, SSDLockState *_ssdCurrentLockState)
{
    if (idx != SSD_ByName)
        return false;

    ssdLockWrite(self);
    bool ret = htRemove(&self->storage, strref, name);
    if (ret)
        ssdnodeUpdateModified(self);
    return ret;
}

SSDIterator *SSDHashNode_iter(SSDHashNode *self)
{
    SSDHashIter *ret = ssdhashiterCreate(self, NULL);
    return SSDIterator(ret);
}

SSDIterator *SSDHashNode__iterLocked(SSDHashNode *self, SSDLockState *_ssdCurrentLockState)
{
    SSDHashIter *ret = ssdhashiterCreate(self, _ssdCurrentLockState);
    return SSDIterator(ret);
}

int32 SSDHashNode_count(SSDHashNode *self, SSDLockState *_ssdCurrentLockState)
{
    ssdLockRead(self);
    return htSize(self->storage);
}

extern bool SSDNode_isHashtable(SSDNode *self); // parent
#define parent_isHashtable() SSDNode_isHashtable((SSDNode*)(self))
bool SSDHashNode_isHashtable(SSDHashNode *self)
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

SSDHashIter *SSDHashIter_create(SSDHashNode *node, SSDLockState *lstate)
{
    SSDHashIter *self;
    self = objInstCreate(SSDHashIter);

    self->node = (SSDNode*)objAcquire(node);
    self->lstate = lstate;

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    // grab iterator after object init because base SSDIterator will lock the tree
    // for read access during the above call to objInstInit
    htiInit(&self->iter, ((SSDHashNode*)self->node)->storage);

    return self;
}

bool SSDHashIter_valid(SSDHashIter *self)
{
    _ssdManualLockRead(self->node, self->lstate);
    bool ret = htiValid(&self->iter);
    _ssdUnlock(self->node, &self->transient_lock_state);
    return ret;
}

bool SSDHashIter_next(SSDHashIter *self)
{
    _ssdManualLockRead(self->node, self->lstate);
    bool ret = htiNext(&self->iter);
    _ssdUnlock(self->node, &self->transient_lock_state);
    return ret;
}

stvar *SSDHashIter_ptr(SSDHashIter *self)
{
    _ssdManualLockRead(self->node, self->lstate);
    // this does not unlock the transient lock!
    // you really shouldn't be using ptr() with the transient lock anyway...
    devAssert(!self->transient_lock_state.init);

    if (!htiValid(&self->iter))
        return NULL;

    return htiValPtr(self->iter, stvar);
}

bool SSDHashIter_get(SSDHashIter *self, stvar *out)
{
    bool ret = false;
    _ssdManualLockRead(self->node, self->lstate);

    if (htiValid(&self->iter)) {
        stvarCopy(out, htiVal(self->iter, stvar));
        ret = true;
    }

    _ssdUnlock(self->node, &self->transient_lock_state);
    return ret;
}

int32 SSDHashIter_idx(SSDHashIter *self)
{
    return SSD_ByName;
}

strref SSDHashIter_name(SSDHashIter *self)
{
    strref ret = NULL;
    _ssdManualLockRead(self->node, self->lstate);

    if (htiValid(&self->iter)) {
        // Have to duplicate this into the iterator because it would be dangerous to
        // return the reference directly to the hash key without the lock held,
        // which is the case if the transient lock is being used. Thankfully refcounted
        // strings make this fairly cheap.
        strDup(&self->lastName, htiKey(self->iter, strref));
        ret = self->lastName;
    }

    _ssdUnlock(self->node, &self->transient_lock_state);
    return ret;
}

bool SSDHashIter_iterOut(SSDHashIter *self, int32 *idx, strref *name, stvar **val)
{
    _ssdManualLockRead(self->node, self->lstate);
    // this shouldn't be used in transient lock mode
    devAssert(!self->transient_lock_state.init);

    if (!htiValid(&self->iter))
        return false;

    *idx = SSD_ByName;
    *name = htiKey(self->iter, strref);
    *val = htiValPtr(self->iter, stvar);

    return true;
}

void SSDHashIter_destroy(SSDHashIter *self)
{
    htiFinish(&self->iter);
    // Autogen begins -----
    strDestroy(&self->lastName);
    // Autogen ends -------
}

extern bool SSDIterator_isHashtable(SSDIterator *self); // parent
#undef parent_isHashtable
#define parent_isHashtable() SSDIterator_isHashtable((SSDIterator*)(self))
bool SSDHashIter_isHashtable(SSDHashIter *self)
{
    return true;
}

// Autogen begins -----
#include "ssdhashnode.auto.inc"
// Autogen ends -------
