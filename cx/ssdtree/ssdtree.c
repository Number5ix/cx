#include "ssdtree.h"
#include <cx/container.h>
#include <cx/string.h>

void ssdLockInit(SSDLock *lock)
{
    lock->init = true;
    lock->rdlock = lock->wrlock = false;
}

bool ssdLockRead(SSDNode *tree, SSDLock *lock)
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

bool ssdLockWrite(SSDNode *tree, SSDLock *lock)
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

SSDNode *_ssdCreateRoot(bool singleval, stvar initval, uint32 flags)
{
    SSDInfo *info = ssdinfoCreate(flags);
    SSDNode *ret = ssdnodeCreate(info);

    if (singleval) {
        ret->singleval = true;
        // don't need to lock anything because no one else can possibly have a
        // pointer to the object yet
        htInsert(&ret->children, strref, _S"", stvar, initval);
    }

    objRelease(&info);
    return ret;
}

static bool ssdResolvePath(SSDNode *tree, strref path, SSDNode **nodeout, string *nameout, bool create, SSDLock *lock)
{
    SSDNode *node = objAcquire(tree);
    string temp = 0;
    bool ret = true;
    int start = 0;

    for (int slash = 0; (slash = strFind(path, start, _S"/")) != -1 ;) {
        // there's a slash, recurse into child node
        strSubStr(&temp, path, start, slash);
        SSDNode *child = ssdnodeGetChild(node, temp, create, lock);
        if (child) {
            objRelease(&node);
            node = child;
            start = slash + 1;
        } else {
            // couldn't find or create child
            ret = false;
            break;
        }
    }

    if (ret) {
        // give caller the temp reference we're holding in code
        *nodeout = node;
        if (start > 0)
            strSubStr(nameout, path, start, strEnd);
        else
            strDup(nameout, path);
    } else {
        objRelease(&node);
    }

    strDestroy(&temp);
    return ret;
}

bool ssdGetValue(SSDNode *tree, strref path, stvar *out, SSDLock *lock)
{
    SSDLock transient_lock = { 0 };
    if (!lock) lock = &transient_lock;

    SSDNode *node = NULL;
    string name = 0;
    bool ret = false;

    if (ssdResolvePath(tree, path, &node, &name, false, lock))
        ret = ssdnodeGetValue(node, name, out, lock);

    if (transient_lock.init)
        ssdLockEnd(tree, &transient_lock);

    objRelease(&node);
    strDestroy(&name);
    return ret;
}

stvar *ssdGetPtr(SSDNode *tree, strref path, SSDLock *lock)
{
    SSDNode *node = NULL;
    string name = 0;
    stvar *ret = NULL;

    devAssert(lock);            // lock parameter is mandatory

    if (ssdResolvePath(tree, path, &node, &name, false, lock))
        ret = ssdnodeGetPtr(node, name, lock);

    objRelease(&node);
    strDestroy(&name);
    return ret;

}

bool ssdSetValue(SSDNode *tree, strref path, stvar val, SSDLock *lock)
{
    SSDLock transient_lock = { 0 };
    if (!lock) lock = &transient_lock;

    // we know we're going to need the write lock for the duration regardless
    ssdLockWrite(tree, lock);

    SSDNode *node = NULL;
    string name = 0;
    bool ret = false;

    if (ssdResolvePath(tree, path, &node, &name, true, lock)) {
        ssdnodeSetValue(node, name, val, lock);
        ret = true;
    }

    if (transient_lock.init)
        ssdLockEnd(tree, &transient_lock);

    objRelease(&node);
    strDestroy(&name);
    return ret;
}

SSDNode *ssdGetSubtree(SSDNode *tree, strref path, bool create, SSDLock *lock)
{
    SSDLock transient_lock = { 0 };
    if (!lock) lock = &transient_lock;

    SSDNode *ret = NULL;
    SSDNode *node = NULL;
    string name = 0;

    if (ssdResolvePath(tree, path, &node, &name, create, lock)) {
        ret = ssdnodeGetChild(node, name, create, lock);
    }

    if (transient_lock.init)
        ssdLockEnd(tree, &transient_lock);

    objRelease(&node);
    strDestroy(&name);
    return ret;
}
