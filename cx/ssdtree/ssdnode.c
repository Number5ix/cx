// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/core/objstdif.h>
#include <cx/debug/assert.h>
#include <cx/container.h>
#include <cx/string.h>
#include "ssdnode.h"
// ==================== Auto-generated section ends ======================
#include "ssdtree.h"

SSDNode *SSDNode_create(SSDInfo *info)
{
    SSDNode *self;
    self = objInstCreate(SSDNode);

    self->info = objAcquire(info);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

bool SSDNode_init(SSDNode *self)
{
    // Autogen begins -----
    htInit(&self->children, string, stvar, 16);
    return true;
    // Autogen ends -------
}

void SSDNode_destroy(SSDNode *self)
{
    // Autogen begins -----
    objRelease(&self->info);
    htDestroy(&self->children);
    // Autogen ends -------
}

SSDNode *SSDNode_getChild(SSDNode *self, strref name, bool create, SSDLock *lock)
{
    if (self->singleval)
        return NULL;

    ssdLockRead(self, lock);
    bool havewrite;

    SSDNode *ret = NULL;

    do {
        havewrite = lock->wrlock;;
        htelem elem = htFind(self->children, strref, name, none, NULL);

        if (elem) {
            stvar *val = hteValPtr(self->children, elem, stvar);
            if (stEq(val->type, stType(object)) &&
                (ret = objDynCast(val->data.st_object, SSDNode))) {
                // the hashtable entry is an SSDNode instance, can just return it as-is
                return objAcquire(ret);
            }
        }

        // didn't exist (or was the wrong type), so create it
        if (create) {
            ssdLockWrite(self, lock);

            // check again with write lock held, since we do drop the lock
            // briefly, another thread may have created it
            if (!havewrite)
                continue;

            // share our info and locks with the child
            ret = SSDNode_create(self->info);
            htInsert(&self->children, strref, name, stvar, stvar(object, ret));
            return ret;
        }
    } while (create && !havewrite);         // loop if we're in create mode but didn't have the write lock

    return ret;
}

bool SSDNode_getValue(SSDNode *self, strref name, stvar *out, SSDLock *lock)
{
    bool ret = false;

    ssdLockRead(self, lock);

    if (htFind(self->children, strref, name, stvar, out))
        ret = true;
    else
        *out = stvNone;
    return ret;
}

stvar *SSDNode_getPtr(SSDNode *self, strref name, SSDLock *lock)
{
    ssdLockRead(self, lock);

    htelem elem = htFind(self->children, strref, name, none, NULL);
    if (elem)
        return hteValPtr(self->children, elem, stvar);

    return NULL;
}


void SSDNode_setValue(SSDNode *self, strref name, stvar val, SSDLock *lock)
{
    ssdLockWrite(self, lock);
    htInsert(&self->children, strref, name, stvar, val);
}

void SSDNode_setValueC(SSDNode *self, strref name, stvar *val, SSDLock *lock)
{
    ssdLockWrite(self, lock);
    htInsertC(&self->children, strref, name, stvar, val);
}

bool SSDNode_removeValue(SSDNode *self, strref name, SSDLock *lock)
{
    ssdLockWrite(self, lock);
    return htRemove(&self->children, strref, name);
}

// Autogen begins -----
#include "ssdnode.auto.inc"
// Autogen ends -------
