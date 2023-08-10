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

bool _ssdLockRead(SSDNode *root, SSDLock *lock)
{
    if (!root || !lock)
        return false;
    if (!lock->init)
        ssdLockInit(lock);

    if (lock->rdlock || lock->wrlock)
        return true;

    lock->rdlock = true;
    rwlockAcquireRead(&root->tree->lock);

    return true;
}

bool _ssdLockWrite(SSDNode *root, SSDLock *lock)
{
    if (!root || !lock)
        return false;
    if (!lock->init)
        ssdLockInit(lock);

    if (lock->wrlock)
        return true;

    if (lock->rdlock) {
        lock->rdlock = false;
        rwlockReleaseRead(&root->tree->lock);
    }

    rwlockAcquireWrite(&root->tree->lock);
    lock->wrlock = true;

    return true;
}

bool _ssdLockEnd(SSDNode *root, SSDLock *lock)
{
    if (!root || !lock || !lock->init)
        return false;

    if (lock->wrlock)
        rwlockReleaseWrite(&root->tree->lock);
    else if (lock->rdlock)
        rwlockReleaseRead(&root->tree->lock);

    lock->wrlock = lock->rdlock = false;

    return true;
}

SSDNode *_ssdCreateRoot(int crtype, SSDTree *tree, uint32 flags)
{
    bool created = false;

    if (!tree) {
        created = true;
        tree = ssdtreeCreate(flags);
    }

    SSDNode *ret = ssdtreeCreateNode(tree, crtype);

    if (created)
        objRelease(&tree);

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
        if (child && checktype == SSD_Create_Hashtable && !ssdnodeIsHashtable(child))
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

            ret = ssdtreeCreateNode(node->tree, create);
            if (!ret)
                return NULL;

            // create or overwrite
            ssdnodeSet(node, SSD_ByName, name, stvar(object, ret), lock);
            return ret;
        }
    } while ((create != SSD_Create_None) && !havewrite);    // loop if we're in create mode but didn't have the write lock

    return ret;
}

static bool ssdResolvePath(SSDNode *root, strref path, SSDNode **nodeout, string *nameout, bool create, SSDLock *lock)
{
    SSDNode *node = objAcquire(root);
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
                    childtype = SSD_Create_Hashtable;
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

bool ssdGet(SSDNode *root, strref path, stvar *out, SSDLock *lock)
{
    SSDLock transient_lock = { 0 };
    if (!lock) lock = &transient_lock;

    SSDNode *node = NULL;
    string name = 0;
    bool ret = false;

    if (ssdResolvePath(root, path, &node, &name, false, lock))
        ret = ssdnodeGet(node, SSD_ByName, name, out, lock);

    if (transient_lock.init)
        ssdLockEnd(root, &transient_lock);

    objRelease(&node);
    strDestroy(&name);
    return ret;
}

stvar *ssdPtr(SSDNode *root, strref path, SSDLock *lock)
{
    SSDNode *node = NULL;
    string name = 0;
    stvar *ret = NULL;

    devAssert(lock);            // lock parameter is mandatory

    if (ssdResolvePath(root, path, &node, &name, false, lock))
        ret = ssdnodePtr(node, SSD_ByName, name, lock);

    objRelease(&node);
    strDestroy(&name);
    return ret;
}

bool ssdSet(SSDNode *root, strref path, bool createpath, stvar val, SSDLock *lock)
{
    SSDLock transient_lock = { 0 };
    if (!lock) lock = &transient_lock;

    // we know we're going to need the write lock for the duration regardless
    ssdLockWrite(root, lock);

    SSDNode *node = NULL;
    string name = 0;
    bool ret = false;

    if (ssdResolvePath(root, path, &node, &name, createpath, lock)) {
        ssdnodeSet(node, SSD_ByName, name, val, lock);
        ret = true;
    }

    if (transient_lock.init)
        ssdLockEnd(root, &transient_lock);

    objRelease(&node);
    strDestroy(&name);
    return ret;
}

bool ssdRemove(SSDNode *root, strref path, SSDLock *lock)
{
    SSDLock transient_lock = { 0 };
    if (!lock) lock = &transient_lock;

    // this is likely going to need the write lock, even if it technically
    // doesn't 100% of the time (i.e. if the key doesn't exist)
    ssdLockWrite(root, lock);

    SSDNode *node = NULL;
    string name = 0;
    bool ret = false;

    if (ssdResolvePath(root, path, &node, &name, false, lock)) {
        ret = ssdnodeRemove(node, SSD_ByName, name, lock);
    }

    if (transient_lock.init)
        ssdLockEnd(root, &transient_lock);

    objRelease(&node);
    strDestroy(&name);
    return ret;
}

SSDNode *ssdSubtree(SSDNode *root, strref path, int create, SSDLock *lock)
{
    SSDLock transient_lock = { 0 };
    if (!lock) lock = &transient_lock;

    SSDNode *ret = NULL;
    SSDNode *node = NULL;
    string name = 0;

    if (ssdResolvePath(root, path, &node, &name, create != SSD_Create_None, lock)) {
        ret = getChild(node, name, create, create, lock);
    }

    if (transient_lock.init)
        ssdLockEnd(root, &transient_lock);

    objRelease(&node);
    strDestroy(&name);
    return ret;
}

SSDNode *ssdSubtreeB(SSDNode *root, strref path, SSDLock *lock)
{
    devAssert(lock);
    if (!lock)
        return NULL;

    SSDNode *ret = NULL;
    SSDNode *node = NULL;
    string name = 0;

    ssdLockRead(root, lock);
    if (ssdResolvePath(root, path, &node, &name, false, lock)) {
        stvar *val = ssdnodePtr(node, SSD_ByName, name, lock);
        ret = val ? (stEq(val->type, stType(object)) ? objDynCast(val->data.st_object, SSDNode) : NULL) : NULL;
    }

    objRelease(&node);
    strDestroy(&name);
    return ret;
}

bool _ssdCopyOut(SSDNode *root, strref path, stype valtype, stgeneric *val, SSDLock *lock)
{
    SSDLock transient_lock = { 0 };
    if (!lock) lock = &transient_lock;

    stvar *temp = NULL;
    SSDNode *node = NULL;
    string name = 0;
    bool ret = false;

    if (ssdResolvePath(root, path, &node, &name, false, lock))
        temp = ssdnodePtr(node, SSD_ByName, name, lock);

    if (temp)
        ret = _stConvert(valtype, val, temp->type, NULL, temp->data, 0);

    if (transient_lock.init)
        ssdLockEnd(root, &transient_lock);

    objRelease(&node);
    strDestroy(&name);

    return ret;
}

bool _ssdCopyOutD(SSDNode *root, strref path, stype valtype, stgeneric *val, stgeneric def, SSDLock *lock)
{
    SSDLock transient_lock = { 0 };
    if (!lock) lock = &transient_lock;

    stvar *temp = NULL;
    SSDNode *node = NULL;
    string name = 0;
    bool ret = false;

    if (ssdResolvePath(root, path, &node, &name, false, lock))
        temp = ssdnodePtr(node, SSD_ByName, name, lock);

    if (temp)
        ret = _stConvert(valtype, val, temp->type, NULL, temp->data, 0);

    if (transient_lock.init)
        ssdLockEnd(root, &transient_lock);

    objRelease(&node);
    strDestroy(&name);

    if (!ret)
        _stCopy(valtype, NULL, val, def, 0);

    return ret;
}

bool ssdExportArray(SSDNode *root, strref path, sa_stvar *out, SSDLock *lock)
{
    SSDLock transient_lock = { 0 };
    if (!lock) lock = &transient_lock;

    bool ret = false;

    ssdLockRead(root, lock);
    SSDArrayNode *node = ssdObjPtr(root, path, SSDArrayNode, lock);
    if (node) {
        saDestroy(out);
        saClone(out, node->storage);
        ret = true;
    }

    if (transient_lock.init)
        ssdLockEnd(root, &transient_lock);

    return ret;
}

bool _ssdExportTypedArray(SSDNode *root, strref path, stype elemtype, sahandle out, bool strict, SSDLock *lock)
{
    SSDLock transient_lock = { 0 };
    if (!lock) lock = &transient_lock;

    bool ret = false;

    ssdLockRead(root, lock);
    SSDArrayNode *node = ssdObjPtr(root, path, SSDArrayNode, lock);
    if (node) {
        saClear(out);
        for (int i = 0, sz = saSize(node->storage); i < sz; ++i) {
            if (!stEq(node->storage.a[i].type, elemtype)) {
                // is the same type as the output array?
                if (strict)
                    goto out;
                continue;               // just skip over
            }

            _saPushPtr(out, elemtype, stStoredPtr(elemtype, &node->storage.a[i].data), 0);
        }
        ret = true;
    }

out:
    if (transient_lock.init)
        ssdLockEnd(root, &transient_lock);

    if (!ret)
        saClear(out);

    return ret;
}

bool ssdImportArray(SSDNode *root, strref path, sa_stvar arr, SSDLock *lock)
{
    devAssert(stEq(saElemType(arr), stType(stvar)));            // double check this is the right kind of array

    SSDLock transient_lock = { 0 };
    if (!lock) lock = &transient_lock;

    bool ret = false;

    ssdLockWrite(root, lock);
    SSDNode *stree = ssdSubtree(root, path, SSD_Create_Array, lock);
    SSDArrayNode *node = objDynCast(stree, SSDArrayNode);
    if (node) {
        saDestroy(&node->storage);
        saClone(&node->storage, arr);
        ssdnodeUpdateModified(node);
        ret = true;
    }
    objRelease(&stree);

    if (transient_lock.init)
        ssdLockEnd(root, &transient_lock);

    return ret;
}

bool _ssdImportTypedArray(SSDNode *root, strref path, stype elemtype, sa_ref arr, SSDLock *lock)
{
    devAssert(stEq(saElemType(arr), elemtype));             // double check this is the right kind of array

    SSDLock transient_lock = { 0 };
    if (!lock) lock = &transient_lock;

    bool ret = false;
    stvar val = { .type = elemtype };

    ssdLockWrite(root, lock);
    SSDNode *stree = ssdSubtree(root, path, SSD_Create_Array, lock);
    SSDArrayNode *node = objDynCast(stree, SSDArrayNode);
    if (node) {
        saClear(&node->storage);
        for (int i = 0, sz = saSize(arr); i < sz; ++i) {
            val.data = stStored(elemtype, (void*)((uintptr)arr.a + i * saElemSize(arr)));       // low-budget ELEMPTR
            saPush(&node->storage, stvar, val);
        }
        ssdnodeUpdateModified(node);
        ret = true;
    }
    objRelease(&stree);

    if (transient_lock.init)
        ssdLockEnd(root, &transient_lock);

    return ret;
}

static SSDNode *ssdCloneNode(SSDNode *src, SSDTree *desttree, SSDLock *destlock);

static SSDNode *ssdCloneHashtable(SSDNode *src, SSDTree *desttree, SSDLock *destlock)
{
    SSDNode *dnode = ssdtreeCreateNode(desttree, SSD_Create_Hashtable);
    string srcname = 0;

    foreach(object, iter, SSDIterator, src)
    {
        stvar *val = ssditeratorPtr(iter);
        if (!val || !ssditeratorName(iter, &srcname)) {
            objRelease(&dnode);
            goto out;
        }

        SSDNode *child = stvarObj(val, SSDNode);
        if (child) {
            // if this is a node, clone it first
            val->data.st_object = (ObjInst*)ssdCloneNode(child, desttree, destlock);
        }

        ssdnodeSet(dnode, SSD_ByName, srcname, *val, destlock);
    }

out:
    strDestroy(&srcname);
    return dnode;
}

static SSDNode *ssdCloneArray(SSDNode *src, SSDTree *desttree, SSDLock *destlock)
{
    SSDNode *dnode = ssdtreeCreateNode(desttree, SSD_Create_Array);

    int srcidx = 0;
    foreach(object, iter, SSDIterator, src)
    {
        stvar *val = ssditeratorPtr(iter);
        if (!val) {
            objRelease(&dnode);
            return NULL;
        }

        SSDNode *child = stvarObj(val, SSDNode);
        if (child) {
            // if this is a node, clone it first
            val->data.st_object = (ObjInst*)ssdCloneNode(child, desttree, destlock);
        }

        ssdnodeSet(dnode, srcidx++, NULL, *val, destlock);
    }

    return dnode;
}

static SSDNode *ssdCloneNode(SSDNode *src, SSDTree *desttree, SSDLock *destlock)
{
    if (ssdnodeIsHashtable(src))
        return ssdCloneHashtable(src, desttree, destlock);
    else if (ssdnodeIsArray(src))
        return ssdCloneArray(src, desttree, destlock);
    else
        return NULL;
}

SSDNode *ssdClone(SSDNode *root, SSDTree *desttree, SSDLock *lock_opt)
{
    SSDLock transient_lock = { 0 };
    if (!lock_opt) lock_opt = &transient_lock;

    bool createdtree = false;
    if (!desttree) {
        desttree = ssdtreeCreate(0);
        createdtree = true;
    }

    // lock source tree while we're working on it
    ssdLockRead(root, lock_opt);

    SSDLock destlock;
    ssdLockInit(&destlock);
    SSDNode *ret = ssdCloneNode(root, desttree, &destlock);
    if (ret)
        ssdLockEnd(ret, &destlock);

    if (transient_lock.init)
        ssdLockEnd(root, &transient_lock);
    if (createdtree) {
        objRelease(&desttree);
    }

    return ret;
}

bool ssdGraft(SSDNode *dest, strref destpath, SSDLock *dest_lock_opt,
              SSDNode *src, strref srcpath, SSDLock *src_lock_opt)
{
    SSDLock transient_lock_dest = { 0 };
    if (!dest_lock_opt) dest_lock_opt = &transient_lock_dest;
    SSDLock transient_lock_src = { 0 };
    if (!src_lock_opt) src_lock_opt = &transient_lock_src;

    bool ret = false;

    ssdLockRead(src, src_lock_opt);
    ssdLockWrite(dest, dest_lock_opt);

    SSDNode *stree;
    SSDNode *dpnode = NULL;
    string dname = 0;

    if (!strEmpty(srcpath))
        stree = ssdSubtreeB(src, srcpath, src_lock_opt);
    else
        stree = src;

    if (!stree)
        goto out;

    if (!ssdResolvePath(dest, destpath, &dpnode, &dname, true, dest_lock_opt))
        goto out;

    SSDNode *dnode = ssdCloneNode(stree, dest->tree, dest_lock_opt);
    if (dnode) {
        ret = ssdnodeSet(dpnode, SSD_ByName, dname, stvar(object, dnode), dest_lock_opt);
        objRelease(&dnode);
    }

out:
    objRelease(&dpnode);
    strDestroy(&dname);
    if (transient_lock_src.init)
        ssdLockEnd(src, &transient_lock_src);
    if (transient_lock_dest.init)
        ssdLockEnd(dest, &transient_lock_dest);

    return ret;
}
