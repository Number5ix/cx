#include "vfs_private.h"
#include "cx/core/error.h"
#include "cx/time/clock.h"
#include "vfsfs/vfsfs.h"
#include "vfsvfs/vfsvfs.h"

static void vfsUnmountAll(VFSDir *dir)
{
    htiter sdi;
    htiCreate(&sdi, &dir->subdirs);
    while (htiNext(&sdi)) {
        vfsUnmountAll((VFSDir*)htiVal(sdi, ptr));
    }
    htiDestroy(&sdi);

    saClear(&dir->mounts);
}

void vfsDestroy(VFS *vfs)
{
    // Unmount all filesystems
    // This is to break a reference loop that can happen in a fairly common case
    // of mounting a VFS backed by a file that is in the same VFS as it's
    // being mounted to.

    rwlockAcquireWrite(vfs->vfslock);
    htiter nsi;
    htiCreate(&nsi, &vfs->namespaces);
    while (htiNext(&nsi)) {
        vfsUnmountAll((VFSDir*)htiVal(nsi, ptr));
    }
    htiDestroy(&nsi);
    vfsUnmountAll(vfs->root);
    rwlockReleaseWrite(vfs->vfslock);

    objRelease(&vfs);
}

static VFSDir *_vfsGetDirInternal(VFS *vfs, VFSDir *root, string *path, int32 plen, bool cache, uint64 now, bool writelockheld)
{
    root->touched = now;

    // if something in the path isn't cachable, the entire path becomes exempt
    if (!cache)
        root->cache = false;

    if (plen == 0)
        return root;

    VFSDir *child = 0;

    // empty path component means this is a root
    if (strEmpty(path[0]))
        child = root;
    else
        htFind(&root->subdirs, string, path[0], ptr, &child);

    if (!child) {
        if (!writelockheld) {
            rwlockReleaseRead(vfs->vfslock);
            rwlockAcquireWrite(vfs->vfslock);
            // try again with the write lock held
            htFind(&root->subdirs, string, path[0], ptr, &child);
        }
        if (!child) {
            child = _vfsDirCreate(vfs, root);
            child->cache = cache;
            strDup(&child->name, path[0]);
            htInsert(&root->subdirs, string, path[0], ptr, child);
        }
        if (!writelockheld) {
            rwlockReleaseWrite(vfs->vfslock);
            rwlockAcquireRead(vfs->vfslock);
        }
    }

    return _vfsGetDirInternal(vfs, child, &path[1], plen - 1, cache, now, writelockheld);
}

VFSDir *_vfsGetDir(VFS *vfs, string path, bool isfile, bool cache, bool writelockheld)
{
    VFSDir *d, *ret = 0;
    string ns = 0;
    string *components = 0;

    pathDecompose(&ns, &components, path);

    if (strEmpty(ns)) {
        d = vfs->root;
    } else if (!htFind(&vfs->namespaces, string, ns, ptr, &d)) {
        cxerr = CX_FileNotFound;
        goto out;
    }

    ret = _vfsGetDirInternal(vfs, d, components, saSize(&components) - (isfile ? 1 : 0), cache, clockTimer(), writelockheld);

out:
    strDestroy(&ns);
    saDestroy(&components);
    return ret;
}

bool _vfsMountProvider(VFS *vfs, ObjInst *provider, string path, uint32 flags)
{
    string ns = 0, rpath = 0;
    VFSProvider *provif;
    bool ret = false;

    // verify that this implements the right interface
    provif = objInstIf(provider, VFSProvider);
    if (!provif)
        return false;

    rwlockAcquireWrite(vfs->vfslock);

    if (!pathIsAbsolute(path))
        goto out;           // must mount with an absolute path

    pathSplitNS(&ns, &rpath, path);
    strDestroy(&rpath);

    if (!strEmpty(ns) && !htHasKey(&vfs->namespaces, string, ns)) {
        // namespace hasn't been added yet, create it now
        htInsert(&vfs->namespaces, string, ns, ptr, _vfsDirCreate(vfs, NULL));
    }

    VFSDir *dir = _vfsGetDir(vfs, path, false, false, true);
    if (!dir)
        goto out;

    // propagate certain flags from the VFS to all mounted providers
    if (vfs->flags & VFS_ReadOnly)
        flags |= VFS_ReadOnly;
    if (vfs->flags & VFS_NoCache)
        flags |= VFS_NoCache;

    VFSMount *nmount = vfsmountCreate(provider, flags | provif->flags(provider));
    saPushC(&dir->mounts, object, &nmount);
    _vfsInvalidateRecursive(vfs, dir, true);
    ret = true;

out:
    rwlockReleaseWrite(vfs->vfslock);
    strDestroy(&ns);
    return ret;
}

// Mounts the built-in OS filesystem provider to the given VFS
bool _vfsMountFS(VFS *vfs, string path, string fsroot, uint32 flags)
{
    VFSFS *fsprovider = vfsfsCreate(fsroot);
    if (!fsprovider)
        return false;

    bool ret = _vfsMountProvider(vfs, objInstBase(fsprovider), path, flags);
    objRelease(&fsprovider);
    return ret;
}

// Mounts one VFS underneath another
bool _vfsMountVFS(VFS *vfs, string path, VFS *vfs2, string vfs2root, uint32 flags)
{
    VFSVFS *vfsprovider = vfsvfsCreate(vfs2, vfs2root);
    if (!vfsprovider)
        return false;

    relAssert(vfs != vfs2);

    bool ret = _vfsMountProvider(vfs, objInstBase(vfsprovider), path, flags);
    objRelease(&vfsprovider);
    return ret;
}

VFSCacheEnt *_vfsGetFile(VFS *vfs, string path, bool writelockheld)
{
    VFSDir *pdir = _vfsGetDir(vfs, path, true, true, writelockheld);
    VFSCacheEnt *ret = 0;
    string fname = 0;

    if (!pdir)
        return NULL;

    pathFilename(&fname, path);

    if (!htFind(&pdir->files, string, fname, ptr, &ret))
        cxerr = CX_FileNotFound;

    strDestroy(&fname);
    return ret;
}

void _vfsInvalidateCache(VFS *vfs, string path)
{
    string abspath = 0;

    rwlockAcquireWrite(vfs->vfslock);
    _vfsAbsPath(vfs, &abspath, path);
    VFSDir *pdir = _vfsGetDir(vfs, abspath, true, true, true);
    string fname = 0;

    if (!pdir) {
        rwlockReleaseWrite(vfs->vfslock);
        strDestroy(&abspath);
        return;
    }

    pathFilename(&fname, abspath);
    htRemove(&pdir->files, string, fname);
    rwlockReleaseWrite(vfs->vfslock);
    strDestroy(&fname);
    strDestroy(&abspath);
}

void _vfsInvalidateRecursive(VFS *vfs, VFSDir *dir, bool havelock)
{
    if (!havelock)
        rwlockAcquireWrite(vfs->vfslock);

    if (dir->cache && dir->parent) {
        // can just remove the whole thing
        htRemove(&dir->parent->subdirs, string, dir->name);
    } else {
        htClear(&dir->files);
        htiter sdi;
        htiCreate(&sdi, &dir->subdirs);
        while (htiNext(&sdi)) {
            _vfsInvalidateRecursive(vfs, (VFSDir*)htiVal(sdi, ptr), true);
        }
        htiDestroy(&sdi);
    }

    if (!havelock)
        rwlockReleaseWrite(vfs->vfslock);
}

void _vfsAbsPath(VFS *vfs, string *out, string path)
{
    if (pathIsAbsolute(path))
        strDup(out, path);
    else
        pathJoin(out, vfs->curdir, path);
}

static int vfsFindCISub(VFSDir *vdir, string *out, string path,
        string *components, int depth, int target,
        VFSMount *mount, VFSProvider *provif)
{
    int ret = FS_Nonexistent;
    string filepath = 0;
    VFSDir *cvdir = vdir;

    // walk backwards up vdir tree to current depth for caching
    for (int i = depth; i < target && cvdir; i++) {
        cvdir = cvdir->parent;
    }

    // get a directory listing from the current depth
    ObjInst *dsprov = provif->searchDir(mount->provider, path, NULL, false);
    VFSDirSearchProvider *dsprovif = objInstIf(dsprov, VFSDirSearchProvider);
    if (!dsprovif)
        return ret;

    FSDirEnt *ent;
    while ((ent = dsprovif->next(dsprov))) {
        pathJoin(&filepath, path, ent->name);

        // if we haven't found it yet (the loop continues to cache even after
        // we do), check to see if this entry matches what we're looking for
        // at the current depth
        if (ret == FS_Nonexistent && strEqi(ent->name, components[depth])) {
            // if we haven't found it yet (the loop continues to cache even
            // after we do)

            if (depth == target) {
                // this is it!
                strDup(out, filepath);
                ret = ent->type;
            } else if (ent->type == FS_Directory) {
                // not at the target depth yet, so recurse into all matching
                // subdirectories (there may be more than one in a case
                // sensitive filesystem!)
                ret = vfsFindCISub(vdir, out, filepath, components,
                        depth + 1, target, mount, provif);
            }
        }

        if (cvdir && ent->type == FS_File && !(mount->flags & VFS_NoCache)) {
            // go ahead and add it to the cache while we're here
            VFSCacheEnt *newent = _vfsCacheEntCreate(mount, filepath);
            htInsertC(&cvdir->files, string, ent->name, ptr, &newent, Ignore);
        }
    }

    objRelease(&dsprov);
    strDestroy(&filepath);
    return ret;
}

int _vfsFindCIHelper(VFS *vfs, VFSDir *vdir, string *out, string *components, VFSMount *mount, VFSProvider *provif)
{
    // This is ugly and slow. The hope is that once a given file is found, the
    // VFS cache helps take the edge off. All these dir searches help populate
    // the cache for neighboring files as well.

    if (saSize(&components) == 0)
        return FS_Nonexistent;

    int ret;
    // grab write lock for this
    rwlockReleaseRead(vfs->vfslock);
    rwlockAcquireWrite(vfs->vfslock);

    ret = vfsFindCISub(vdir, out, NULL, components, 0, saSize(&components) - 1, mount, provif);

    rwlockReleaseWrite(vfs->vfslock);
    rwlockAcquireRead(vfs->vfslock);
    return ret;
}

VFSMount *_vfsFindMount(VFS *vfs, string *rpath, string path, VFSMount **cowmount, string *cowrpath, uint32 flags)
{
    VFSMount *ret = 0;
    string abspath = 0, curpath = 0;

    bool flwrite = flags & VFS_FindWriteFile;
    bool fldelete = flags & VFS_FindDelete;
    bool flcreate = flags & VFS_FindCreate;
    bool flcache = flags & VFS_FindCache;

    // see if we can get this from the file cache
    rwlockAcquireRead(vfs->vfslock);
    _vfsAbsPath(vfs, &abspath, path);
    if (flcache && !flcreate && !fldelete) {
        VFSCacheEnt *ent = _vfsGetFile(vfs, abspath, false);
        // only for simple case, i.e. no need to do COW or find a writable layer
        if (ent && (!flwrite || !(ent->mount->flags & VFS_ReadOnly))) {
            strDup(rpath, ent->origpath);
            strDestroy(&abspath);
            ret = ent->mount;
            rwlockReleaseRead(vfs->vfslock);
            return ret;
        }
    }

    VFSDir *vfsdir = _vfsGetDir(vfs, abspath, true, true, false), *pdir = vfsdir;
    VFSMount *firstwritable = 0;
    string firstwpath = 0;
    string ns = 0, *components = 0, *relcomp = 0;

    pathDecompose(&ns, &components, abspath);

    int32 relstart = saSize(&components) - 1;
    while (pdir) {
        devAssert(relstart >= 0);
        saDestroy(&relcomp);
        relcomp = saSlice(&components, relstart, 0);
        strJoin(&curpath, relcomp, fsPathSepStr);

        // traverse list of registered providers backwards, as providers registered later
        // are "higher" on the stack
        for (int i = saSize(&pdir->mounts) - 1; i >= 0; --i) {
            // save first writable provider we find
            if (!firstwritable && !(pdir->mounts[i]->flags & VFS_ReadOnly)) {
                firstwritable = pdir->mounts[i];
                strDup(&firstwpath, curpath);
            }

            if (cowmount && (pdir->mounts[i]->flags & VFS_AlwaysCOW)) {
                // this provider wants to get COW copies for any write
                *cowmount = pdir->mounts[i];
                strDup(cowrpath, curpath);
                cowmount = NULL;        // don't let anything else set it
            }

            VFSProvider *provif = objInstIf(pdir->mounts[i]->provider, VFSProvider);
            if (!provif)
                continue;

            int stat;
            if (!(vfs->flags & VFS_CaseSensitive) && (pdir->mounts[i]->flags & VFS_CaseSensitive)) {
                // case-sensitive file system on insensitive VFS, find the real underlying path
                stat = _vfsFindCIHelper(vfs, vfsdir, &curpath, relcomp, pdir->mounts[i], provif);
            } else {
                // check if this exists
                stat = provif->stat(pdir->mounts[i]->provider, curpath, NULL);
            }

            if (stat == FS_Directory) {
                // found a directory, don't cache this as a file
                ret = pdir->mounts[i];
                strDup(rpath, curpath);
                flcache = false;
                goto done;
            } else if (stat == FS_File) {
                // found an existing file
                ret = pdir->mounts[i];
                strDup(rpath, curpath);
                goto done;
            }

            // do we capture new files on this layer?
            if (flcreate && (pdir->mounts[i]->flags & VFS_NewFiles)) {
                ret = pdir->mounts[i];
                strDup(rpath, curpath);
                // do not exit, keep searching to see if it exists in a lower layer
            }

            // if this layer is opaque, the buck stops here
            if (pdir->mounts[i]->flags & VFS_Opaque)
                goto done;
        }

        relstart--;
        pdir = pdir->parent;
    }

done:
    rwlockReleaseRead(vfs->vfslock);
    if (ret && flwrite && (ret->flags & VFS_ReadOnly) && cowmount) {
        // let the caller know they should COW to a writable provider
        *cowmount = firstwritable;
        strDup(cowrpath, firstwpath);
    }

    // didn't find a provider? if we're writing, go ahead and create a new file
    if (!ret && (flwrite || flcreate) && !fldelete) {
        ret = firstwritable;
        strDup(rpath, firstwpath);
    }

    if (!ret)
        cxerr = CX_FileNotFound;

    if (ret && flcache && !(ret->flags & VFS_NoCache)) {
        rwlockAcquireWrite(vfs->vfslock);
        // in case another thread purged the cache when we dropped the lock
        vfsdir = _vfsGetDir(vfs, abspath, true, true, true);
        VFSCacheEnt *newent = _vfsCacheEntCreate(ret, *rpath);
        htInsertC(&vfsdir->files, string, components[saSize(&components) - 1], ptr, &newent, Ignore);
        rwlockReleaseWrite(vfs->vfslock);
    }

    strDestroy(&ns);
    strDestroy(&abspath);
    strDestroy(&curpath);
    strDestroy(&firstwpath);
    saDestroy(&relcomp);
    saDestroy(&components);
    return ret;
}

void vfsCurDir(VFS *vfs, string *out)
{
    rwlockAcquireRead(vfs->vfslock);
    strDup(out, vfs->curdir);
    rwlockReleaseRead(vfs->vfslock);
}

bool vfsSetCurDir(VFS *vfs, string cur)
{
    if (!pathIsAbsolute(cur))
        return false;

    rwlockAcquireWrite(vfs->vfslock);
    strDup(&vfs->curdir, cur);
    rwlockReleaseWrite(vfs->vfslock);
    return true;
}