#include "ssdtree.h"
#include <cx/container.h>
#include <cx/string.h>
#include "node/ssdarraynode.h"
#include "node/ssdhashnode.h"
#include "node/ssdsinglenode.h"

void ssdLockInit(SSDLock *lock)
{
    lock->init = true;
    lock->rdlock = lock->wrlock = false;
}

bool _ssdLockRead(SSDNode *tree, SSDLock *lock)
{
    if (!tree || !lock)
        return false;
    if (!lock->init)
        ssdLockInit(lock);

    if (lock->rdlock || lock->wrlock)
        return true;

    lock->rdlock = true;
    rwlockAcquireRead(&tree->info->lock);

    return true;
}

bool _ssdLockWrite(SSDNode *tree, SSDLock *lock)
{
    if (!tree || !lock)
        return false;
    if (!lock->init)
        ssdLockInit(lock);

    if (lock->wrlock)
        return true;

    if (lock->rdlock) {
        lock->rdlock = false;
        rwlockReleaseRead(&tree->info->lock);
    }

    rwlockAcquireWrite(&tree->info->lock);
    lock->wrlock = true;

    return true;
}

bool ssdLockEnd(SSDNode *tree, SSDLock *lock)
{
    if (!tree || !lock || !lock->init)
        return false;

    if (lock->wrlock)
        rwlockReleaseWrite(&tree->info->lock);
    else if (lock->rdlock)
        rwlockReleaseRead(&tree->info->lock);

    lock->wrlock = lock->rdlock = false;

    return true;
}

SSDNode *_ssdCreateRoot(int crtype, stvar initval, uint32 flags)
{
    SSDInfo *info = ssdinfoCreate(flags);
    SSDNode *ret = NULL;

    switch (crtype) {
    case SSD_Create_Object:
        ret = (SSDNode*)ssdhashnodeCreate(info);
        break;
    case SSD_Create_Array:
        ret = (SSDNode*)ssdarraynodeCreate(info);
        break;
    case SSD_Create_Single:
        ret = (SSDNode*)ssdsinglenodeCreate(info, initval);
        break;
    }

    objRelease(&info);
    return ret;
}

static SSDNode *getChild(SSDNode *node, strref name, int checktype, int create, SSDLock *lock)
{

    ssdLockRead(node, lock);
    bool havewrite;
    SSDNode *ret = NULL;

    do {
        havewrite = lock->wrlock;

        stvar *val = ssdnodePtr(node, SSD_ByName, name, lock);
        SSDNode *child = val ? (stEq(val->type, stType(object)) ? objDynCast(val->data.st_object, SSDNode) : NULL) : NULL;

        // check if existing child is the correct type for the context
        if (child && checktype == SSD_Create_Object && !ssdnodeIsObject(child))
            child = NULL;
        if (child && checktype == SSD_Create_Array && !ssdnodeIsArray(child))
            child = NULL;

        // there's a valid child there with the correct type, just return it
        if (child)
            return objAcquire(child);

        // didn't exist (or was the wrong type), so create it
        if (create != SSD_Create_None) {
            ssdLockWrite(node, lock);

            // check again with write lock held, since we do drop the lock
            // briefly, another thread may have created it
            if (!havewrite)
                continue;

            switch (create) {
            case SSD_Create_Object:
                ret = (SSDNode*)ssdhashnodeCreate(node->info);
                break;
            case SSD_Create_Array:
                ret = (SSDNode*)ssdarraynodeCreate(node->info);
                break;
            default:
                // invalid create type
                return NULL;
            }

            // create or overwrite
            ssdnodeSet(node, SSD_ByName, name, stvar(object, ret), lock);
            return ret;
        }
    } while ((create != SSD_Create_None) && !havewrite);    // loop if we're in create mode but didn't have the write lock

    return ret;
}

static bool ssdResolvePath(SSDNode *tree, strref path, SSDNode **nodeout, string *nameout, bool create, SSDLock *lock)
{
    SSDNode *node = objAcquire(tree);
    string name = 0;
    int arrstate = 0;
    bool ret = false;
    bool first = true;

    foreach(string, it, path) {
        for (uint32 i = 0; i < it.len; ++i) {
            uint8 ch = it.bytes[i];
            if (arrstate == 1 && !((ch >= '0' && ch <= '9') || ch == ']'))
                // array index parse error
                goto out;

            // name can start with [#] for index of root
            if (!first && (ch == '/' || ch == '[')) {
                // hit a parse point, we need to move down a level

                // check if existing child is the correct type for the context
                int childtype = 0;
                if (ch == '/')
                    childtype = SSD_Create_Object;
                if (ch == '[')
                    childtype = SSD_Create_Array;

                SSDNode *child = getChild(node, name, childtype, create ? childtype : SSD_Create_None, lock);
                if (!child) {
                    // failed to recurse, did not create
                    goto out;
                }

                // rotate child into 'current' node
                objRelease(&node);
                node = child;
                strClear(&name);
            }

            if (ch == '/') {
                arrstate = 0;
            } else if (ch == '[') {
                arrstate = 1;
            } else if (ch == ']') {
                if (arrstate != 1)
                    goto out;               // parse error!
                arrstate = 2;
            } else {
                if (arrstate == 2)
                    goto out;               // parse error, raw text can't come directly after ']'!
                strSetChar(&name, strEnd, ch);
            }

            first = false;
        }
    }

    ret = true;

out:
    if (ret) {
        // give caller the temp reference we're holding in node
        *nodeout = node;
        strDup(nameout, name);
    } else {
        objRelease(&node);
    }

    strDestroy(&name);
    return ret;
}

bool ssdGet(SSDNode *tree, strref path, stvar *out, SSDLock *lock)
{
    SSDLock transient_lock = { 0 };
    if (!lock) lock = &transient_lock;

    SSDNode *node = NULL;
    string name = 0;
    bool ret = false;

    if (ssdResolvePath(tree, path, &node, &name, false, lock))
        ret = ssdnodeGet(node, SSD_ByName, name, out, lock);

    if (transient_lock.init)
        ssdLockEnd(tree, &transient_lock);

    objRelease(&node);
    strDestroy(&name);
    return ret;
}

stvar *ssdPtr(SSDNode *tree, strref path, SSDLock *lock)
{
    SSDNode *node = NULL;
    string name = 0;
    stvar *ret = NULL;

    devAssert(lock);            // lock parameter is mandatory

    if (ssdResolvePath(tree, path, &node, &name, false, lock))
        ret = ssdnodePtr(node, SSD_ByName, name, lock);

    objRelease(&node);
    strDestroy(&name);
    return ret;
}

bool ssdSet(SSDNode *tree, strref path, bool createpath, stvar val, SSDLock *lock)
{
    SSDLock transient_lock = { 0 };
    if (!lock) lock = &transient_lock;

    // we know we're going to need the write lock for the duration regardless
    ssdLockWrite(tree, lock);

    SSDNode *node = NULL;
    string name = 0;
    bool ret = false;

    if (ssdResolvePath(tree, path, &node, &name, createpath, lock)) {
        ssdnodeSet(node, SSD_ByName, name, val, lock);
        ret = true;
    }

    if (transient_lock.init)
        ssdLockEnd(tree, &transient_lock);

    objRelease(&node);
    strDestroy(&name);
    return ret;
}

bool ssdRemove(SSDNode *tree, strref path, SSDLock *lock)
{
    SSDLock transient_lock = { 0 };
    if (!lock) lock = &transient_lock;

    // this is likely going to need the write lock, even if it technically
    // doesn't 100% of the time (i.e. if the key doesn't exist)
    ssdLockWrite(tree, lock);

    SSDNode *node = NULL;
    string name = 0;
    bool ret = false;

    if (ssdResolvePath(tree, path, &node, &name, false, lock)) {
        ret = ssdnodeRemove(node, SSD_ByName, name, lock);
    }

    if (transient_lock.init)
        ssdLockEnd(tree, &transient_lock);

    objRelease(&node);
    strDestroy(&name);
    return ret;
}

SSDNode *ssdSubtree(SSDNode *tree, strref path, int create, SSDLock *lock)
{
    SSDLock transient_lock = { 0 };
    if (!lock) lock = &transient_lock;

    SSDNode *ret = NULL;
    SSDNode *node = NULL;
    string name = 0;

    if (ssdResolvePath(tree, path, &node, &name, create != SSD_Create_None, lock)) {
        ret = getChild(node, name, create, create, lock);
    }

    if (transient_lock.init)
        ssdLockEnd(tree, &transient_lock);

    objRelease(&node);
    strDestroy(&name);
    return ret;
}
