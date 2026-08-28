#include "vfs_private.h"
#include "cx/debug/error.h"
#include "cx/fs/vfsfs/vfsfs.h"
#include "cx/fs/vfsvfs/vfsvfs.h"
#include "cx/time/clock.h"

static void vfsUnmountAll(_Inout_ VFSDir* dir)
{
    foreach (hashtable, sdi, dir->subdirs) {
        vfsUnmountAll(htiVal(VFSDir, sdi));
    }
    saClear(&dir->mounts);
}

_Use_decl_annotations_
void vfsDestroy(VFS** pvfs)
{
    if (!(pvfs && *pvfs))
        return;

    // Unmount all filesystems
    // This is to break a reference loop that can happen in a fairly common case
    // of mounting a VFS backed by a file that is in the same VFS as it's
    // being mounted to.

    VFS* vfs = *pvfs;
    rwlockAcquireWrite(&vfs->vfsdlock);
    foreach (hashtable, nsi, vfs->namespaces) {
        vfsUnmountAll((VFSDir*)htiVal(ptr, nsi));
    }
    vfsUnmountAll(vfs->root);
    vfs->mountgen++;
    htClear(&vfs->namespaces);
    htClear(&vfs->root->subdirs);
    htClear(&vfs->root->files);
    rwlockReleaseWrite(&vfs->vfsdlock);

    objRelease(pvfs);
}

_When_(!exclusive, _Requires_shared_lock_held_(vfs->vfslock)) static _Ret_valid_ VFSDir*
_vfsGetDirInternal(_Inout_ VFS* vfs, _Inout_ VFSDir* root, _In_reads_(plen) string* path,
                   int32 plen, bool cache, uint64 now, bool exclusive)
{
    atomicStore(uint64, &root->touched, now, Relaxed);

    // if something in the path isn't cachable, the entire path becomes exempt
    if (!cache)
        root->cache = false;

    if (plen == 0)
        return root;

    VFSDir* child = 0;

    // empty path component means this is a root
    if (strEmpty(path[0]))
        child = root;
    else
        htFind(root->subdirs, string, path[0], VFSDir, &child);

    if (!child) {
        if (!exclusive) {
            rwlockReleaseRead(&vfs->vfslock);
            rwlockAcquireWrite(&vfs->vfslock);
            // try again with the write lock held
            htFind(root->subdirs, string, path[0], VFSDir, &child);
        }
        if (!child) {
            child        = _vfsDirCreate(vfs, root);
            child->cache = cache;
            strDup(&child->name, path[0]);
            htInsert(&root->subdirs, string, path[0], VFSDir, child);
        }
        if (!exclusive) {
            rwlockDowngradeWrite(&vfs->vfslock);
        }
    }

    return _vfsGetDirInternal(vfs, child, &path[1], plen - 1, cache, now, exclusive);
}

_Use_decl_annotations_
VFSDir* _vfsGetDir(VFS* vfs, strref path, bool isfile, bool cache, bool exclusive)
{
    VFSDir *d, *ret = 0;
    string ns = 0;
    sa_string components;

    saInit(&components, string, 8, SA_Grow(Aggressive));
    pathDecompose(&ns, &components, path);

    if (strEmpty(ns)) {
        d = vfs->root;
    } else if (!htFind(vfs->namespaces, string, ns, VFSDir, &d)) {
        cxerr = CX_FileNotFound;
        goto out;
    }

    ret = _vfsGetDirInternal(vfs,
                             d,
                             components.a,
                             saSize(components) - (isfile ? 1 : 0),
                             cache,
                             clockTimer(),
                             exclusive);

out:
    strDestroy(&ns);
    saDestroy(&components);
    return ret;
}

_Use_decl_annotations_
bool _vfsMountProvider(VFS* vfs, ObjInst* provider, strref path, flags_t flags)
{
    string ns = 0, rpath = 0;
    VFSProvider* provif;
    bool ret = false;

    // verify that this implements the right interface
    provif = objInstIf(provider, VFSProvider);
    if (!provif)
        return false;

    rwlockAcquireWrite(&vfs->vfsdlock);

    if (!pathIsAbsolute(path))
        goto out;   // must mount with an absolute path

    pathSplitNS(&ns, &rpath, path);
    strDestroy(&rpath);

    if (!strEmpty(ns) && !htHasKey(vfs->namespaces, string, ns)) {
        // namespace hasn't been added yet, create it now
        htInsert(&vfs->namespaces, string, ns, VFSDir, _vfsDirCreate(vfs, NULL));
    }

    VFSDir* dir = _vfsGetDir(vfs, path, false, false, true);
    if (!dir)
        goto out;

    // propagate certain flags from the VFS to all mounted providers
    if (vfs->flags & VFS_ReadOnly)
        flags |= VFS_ReadOnly;
    if (vfs->flags & VFS_NoCache)
        flags |= VFS_NoCache;

    VFSMount* nmount = vfsmountCreate(provider, flags | provif->flags(provider));
    saPushC(&dir->mounts, object, &nmount);
    _vfsInvalidateRecursive(vfs, dir, true);
    vfs->mountgen++;
    ret = true;

out:
    rwlockReleaseWrite(&vfs->vfsdlock);
    strDestroy(&ns);
    return ret;
}

_Use_decl_annotations_
bool vfsUnmount(VFS* vfs, strref path)
{
    string ns = 0, rpath = 0;
    bool ret = false;

    rwlockAcquireWrite(&vfs->vfsdlock);

    if (!pathIsAbsolute(path))
        goto out;   // must unmount with an absolute path

    pathSplitNS(&ns, &rpath, path);
    strDestroy(&rpath);

    VFSDir* dir = _vfsGetDir(vfs, path, false, true, true);
    if (!dir)
        goto out;

    vfsUnmountAll(dir);

    if (dir->parent) {
        // remove this dir from the tree; it'll be recached if a parent provider
        // still has it
        htRemove(&dir->parent->subdirs, string, dir->name);
    } else {
        // this is the root of something
        if (!strEmpty(ns)) {
            // it's a namespace, nuke it
            htRemove(&vfs->namespaces, string, ns);
        } else {
            // the root namespace should never be removed...
            // but invalidate the cache
            _vfsInvalidateRecursive(vfs, dir, true);
        }
    }
    vfs->mountgen++;
    ret = true;

out:
    rwlockReleaseWrite(&vfs->vfsdlock);
    strDestroy(&ns);
    return ret;
}

// Mounts the built-in OS filesystem provider to the given VFS
_Use_decl_annotations_
bool _vfsMountFS(VFS* vfs, strref path, strref fsroot, flags_t flags)
{
    VFSFS* fsprovider = vfsfsCreate(fsroot);
    if (!fsprovider)
        return false;

    bool ret = _vfsMountProvider(vfs, objInstBase(fsprovider), path, flags);
    objRelease(&fsprovider);
    return ret;
}

// Mounts one VFS underneath another
_Use_decl_annotations_
bool _vfsMountVFS(VFS* vfs, strref path, VFS* vfs2, strref vfs2root, flags_t flags)
{
    VFSVFS* vfsprovider = vfsvfsCreate(vfs2, vfs2root);
    if (!vfsprovider)
        return false;

    bool ret = _vfsMountProvider(vfs, objInstBase(vfsprovider), path, flags);
    objRelease(&vfsprovider);
    return ret;
}

_Use_decl_annotations_
VFSCacheEnt* _vfsGetFile(VFS* vfs, strref path, bool exclusive)
{
    VFSDir* pdir     = _vfsGetDir(vfs, path, true, true, exclusive);
    VFSCacheEnt* ret = 0;
    string fname     = 0;

    if (!pdir)
        return NULL;

    pathFilename(&fname, path);
    htFind(pdir->files, string, fname, VFSCacheEnt, &ret);

    strDestroy(&fname);
    return ret;
}

_Use_decl_annotations_
void _vfsInvalidateCache(VFS* vfs, strref path)
{
    string abspath = 0, fname = 0;

    // This drops a single entry from one directory's file cache, which is exactly what vfslock
    // guards -- taking vfsdlock exclusively here would serialize the whole VFS behind every
    // failed open and every stat of a path that does not exist.
    rwlockAcquireRead(&vfs->vfsdlock);
    rwlockAcquireWrite(&vfs->vfslock);

    _vfsAbsPath(vfs, &abspath, path);
    VFSDir* pdir = _vfsGetDir(vfs, abspath, true, true, true);

    if (pdir) {
        pathFilename(&fname, abspath);
        htRemove(&pdir->files, string, fname);
    }

    rwlockReleaseWrite(&vfs->vfslock);
    rwlockReleaseRead(&vfs->vfsdlock);

    strDestroy(&fname);
    strDestroy(&abspath);
}

_Use_decl_annotations_
void _vfsInvalidateRecursive(VFS* vfs, VFSDir* dir, bool havelock)
{
    if (!havelock)
        rwlockAcquireWrite(&vfs->vfsdlock);

    if (dir->cache && dir->parent) {
        // can just remove the whole thing
        htRemove(&dir->parent->subdirs, string, dir->name);
    } else {
        htClear(&dir->files);

        // Collect first, act second: the recursive call removes the child it was just handed
        // from this very hashtable, and htRemove is not iteration-safe
        sa_ptr children;
        saInit(&children, ptr, 8);
        foreach (hashtable, sdi, dir->subdirs) {
            saPush(&children, ptr, htiVal(VFSDir, sdi));
        }

        foreach (sarray, idx, VFSDir*, sd, children) {
            _vfsInvalidateRecursive(vfs, sd, true);
        }
        saDestroy(&children);
    }

    if (!havelock)
        rwlockReleaseWrite(&vfs->vfsdlock);
}

// Depth first, because a directory can only go once nothing is left under it.
static void _vfsPruneDir(_Inout_ VFSDir* dir, int64 cutoff)
{
    // Collect first, act second, for the same reason _vfsInvalidateRecursive does: the
    // recursive call removes the child it was handed from this very hashtable.
    sa_ptr children;
    saInit(&children, ptr, 8);
    foreach (hashtable, sdi, dir->subdirs) {
        saPush(&children, ptr, htiVal(VFSDir, sdi));
    }

    foreach (sarray, idx, VFSDir*, sd, children) {
        _vfsPruneDir(sd, cutoff);
    }
    saDestroy(&children);

    if ((int64)atomicLoad(uint64, &dir->touched, Relaxed) >= cutoff)
        return;   // used recently, keep it and everything it remembers

    if (dir->cache && dir->parent && saSize(dir->mounts) == 0 && htSize(dir->subdirs) == 0) {
        // exists only to cache, and nothing is left below it
        htRemove(&dir->parent->subdirs, string, dir->name);
        return;
    }

    // the directory itself has to stay, but the files it remembers do not
    htClear(&dir->files);
}

_Use_decl_annotations_
void vfsPruneCache(VFS* vfs)
{
    int64 now    = clockTimer();
    int64 cutoff = now - vfs->dcache.ttl;

    rwlockAcquireWrite(&vfs->vfsdlock);
    foreach (hashtable, nsi, vfs->namespaces) {
        _vfsPruneDir((VFSDir*)htiVal(ptr, nsi), cutoff);
    }
    _vfsPruneDir(vfs->root, cutoff);
    rwlockReleaseWrite(&vfs->vfsdlock);

    atomicStore(int64, &vfs->dcache.lastprune, now, Relaxed);
}

_Use_decl_annotations_
void vfsSetCacheLimits(VFS* vfs, uint32 maxdirs, int64 ttl)
{
    vfs->dcache.maxdirs = maxdirs ? maxdirs : VFS_CACHE_MAXDIRS;
    vfs->dcache.ttl     = ttl ? ttl : VFS_CACHE_TTL;
}

// Don't sweep the whole tree on every lookup once it is over the limit; a tree that stays over
// it would otherwise pay for a full walk per operation.
#define VFS_PRUNE_INTERVAL timeS(1)

_Use_decl_annotations_
void _vfsMaybeEvict(VFS* vfs)
{
    if (atomicLoad(uint32, &vfs->dcache.dircount, Relaxed) <= vfs->dcache.maxdirs)
        return;

    int64 now  = clockTimer();
    int64 last = atomicLoad(int64, &vfs->dcache.lastprune, Relaxed);
    if (now - last < VFS_PRUNE_INTERVAL)
        return;

    // whoever wins the exchange does the walk; everyone else carries on
    if (!atomicCompareExchange(int64, strong, &vfs->dcache.lastprune, &last, now, AcqRel, Relaxed))
        return;

    vfsPruneCache(vfs);
}

_Use_decl_annotations_
void _vfsAbsPath(VFS* vfs, string* out, strref path)
{
    if (pathIsAbsolute(path))
        strDup(out, path);
    else
        pathJoin(out, vfs->curdir, path);
}

_Use_decl_annotations_
void vfsAbsolutePath(VFS* vfs, string* out, strref path)
{
    rwlockAcquireRead(&vfs->vfslock);
    _vfsAbsPath(vfs, out, path);
    rwlockReleaseRead(&vfs->vfslock);
}

_Use_decl_annotations_
void _vfsSnapshot(VFS* vfs, sa_VFSCand* out, strref abspath, bool isfile)
{
    string ns = 0, curpath = 0, mountpath = 0;
    sa_string components, relcomp = saInitNone, mountcomp = saInitNone;

    saInit(&components, string, 8, SA_Grow(Aggressive));
    pathDecompose(&ns, &components, abspath);

    VFSDir* pdir   = _vfsGetDir(vfs, abspath, isfile, true, false);
    int32 relstart = saSize(components) - (isfile ? 1 : 0);

    while (pdir) {
        devAssert(relstart >= 0);

        // the path this level's providers are asked about, and the VFS path of the level itself
        saDestroy(&relcomp);
        saSlice(&relcomp, components, relstart, 0);
        strJoin(&curpath, relcomp, fsPathSepStr);

        saDestroy(&mountcomp);
        saSlice(&mountcomp, components, 0, relstart);
        pathCompose(&mountpath, ns, mountcomp);

        // traverse list of registered providers backwards, as providers registered later
        // are "higher" on the stack
        for (int i = saSize(pdir->mounts) - 1; i >= 0; --i) {
            VFSCand cand = { 0 };
            cand.mount   = objAcquire(pdir->mounts.a[i]);
            strDup(&cand.mountpath, mountpath);
            strDup(&cand.relpath, curpath);
            saSlice(&cand.relcomp, components, relstart, 0);
            _saPushPtr(SAHANDLE(out), stType(VFSCand), &stgeneric(opaque, &cand), SAINT_Consume);

            // if this layer is opaque, the buck stops here
            if (pdir->mounts.a[i]->flags & VFS_Opaque)
                goto done;
        }

        relstart--;
        pdir = pdir->parent;
    }

done:
    strDestroy(&ns);
    strDestroy(&curpath);
    strDestroy(&mountpath);
    saDestroy(&relcomp);
    saDestroy(&mountcomp);
    saDestroy(&components);
}

_Use_decl_annotations_
void _vfsFlushPending(VFS* vfs, sa_VFSPendEnt* pending)
{
    for (int32 i = 0, n = saSize(*pending); i < n; i++) {
        VFSPendEnt* pe = &pending->a[i];

        VFSDir* d = _vfsGetDir(vfs, pe->dirpath, false, true, true);
        if (!d)
            continue;

        VFSCacheEnt* newent = _vfsCacheEntCreate(pe->mount, pe->origpath);
        htInsertC(&d->files, string, pe->name, VFSCacheEnt, &newent, HT_Ignore);
    }
}

// dirpath is the absolute VFS path of the directory whose listing this level is walking, which
// is where any files found in it belong in the cache.
static int vfsFindCISub(_Inout_ string* out, _In_opt_ strref path, _In_opt_ strref dirpath,
                        _In_reads_(target + 1) string* components, int depth, int target,
                        _Inout_ VFSMount* mount, _Inout_ VFSProvider* provif,
                        _Inout_ sa_VFSPendEnt* pending)
{
    int ret         = FS_Nonexistent;
    string filepath = 0, subdirpath = 0;

    // get a directory listing from the current depth
    FSSearchIter dsiter;
    if (!provif->searchInit(mount->provider, &dsiter, path, NULL, false)) {
        provif->searchFinish(mount->provider, &dsiter);
        return ret;
    }

    do {
        pathJoin(&filepath, path, dsiter.name);

        // if we haven't found it yet (the loop continues to cache even after
        // we do), check to see if this entry matches what we're looking for
        // at the current depth
        if (ret == FS_Nonexistent && strEqi(dsiter.name, components[depth])) {
            if (depth == target) {
                // this is it!
                strDup(out, filepath);
                ret = dsiter.type;
            } else if (dsiter.type == FS_Directory) {
                // not at the target depth yet, so recurse into all matching
                // subdirectories (there may be more than one in a case
                // sensitive filesystem!)
                pathJoin(&subdirpath, dirpath, dsiter.name);
                ret = vfsFindCISub(out,
                                   filepath,
                                   subdirpath,
                                   components,
                                   depth + 1,
                                   target,
                                   mount,
                                   provif,
                                   pending);
            }
        }

        if (dsiter.type == FS_File && !(mount->flags & VFS_NoCache)) {
            // remember it for the cache while we're here; it belongs to the directory being
            // listed right now, whatever depth the search itself has reached
            VFSPendEnt pe = { 0 };
            pe.mount      = objAcquire(mount);
            strDup(&pe.dirpath, dirpath);
            strDup(&pe.name, dsiter.name);
            strDup(&pe.origpath, filepath);
            _saPushPtr(SAHANDLE(pending),
                       stType(VFSPendEnt),
                       &stgeneric(opaque, &pe),
                       SAINT_Consume);
        }
    } while (provif->searchNext(mount->provider, &dsiter));
    provif->searchFinish(mount->provider, &dsiter);

    strDestroy(&filepath);
    strDestroy(&subdirpath);
    return ret;
}

_Use_decl_annotations_
int _vfsFindCIHelper(string* out, strref mountpath, sa_string components, VFSMount* mount,
                     VFSProvider* provif, sa_VFSPendEnt* pending)
{
    // This is ugly and slow. The hope is that once a given file is found, the
    // VFS cache helps take the edge off. All these dir searches help populate
    // the cache for neighboring files as well.

    if (saSize(components) == 0)
        return FS_Nonexistent;

    return vfsFindCISub(out,
                        NULL,
                        mountpath,
                        components.a,
                        0,
                        saSize(components) - 1,
                        mount,
                        provif,
                        pending);
}

// This function does all the heavy lifting of the VFS system.
//
// It runs in three phases, and the split is the point: the middle phase calls into providers,
// and a provider can be another VFS pointing back at this one, so it must run with no VFS lock
// held. Phase one gathers everything a provider call needs into a snapshot, phase two does the
// calls, and phase three puts the results into the cache.
_Use_decl_annotations_
VFSMount* _vfsFindMount(VFS* vfs, string* rpath, strref path, VFSMount** cowmount, string* cowrpath,
                        uint32 flags)
{
    VFSMount* ret           = 0;
    VFSMount* firstwritable = 0;
    string abspath = 0, curpath = 0, firstwpath = 0, cachedir = 0, cachename = 0;
    sa_VFSCand cands      = saInitNone;
    sa_VFSPendEnt pending = saInitNone;
    uint32 gen;

    if (cowmount)
        *cowmount = NULL;

    if (!vfs)
        return NULL;

    _vfsMaybeEvict(vfs);

    bool flwrite  = flags & VFS_FindWriteFile;
    bool fldelete = flags & VFS_FindDelete;
    bool flcreate = flags & VFS_FindCreate;
    bool flcache  = flags & VFS_FindCache;

    // ---- phase 1: everything that needs a lock, and nothing that calls a provider
    rwlockAcquireRead(&vfs->vfsdlock);
    rwlockAcquireRead(&vfs->vfslock);

    // see if we can get this from the file cache
    _vfsAbsPath(vfs, &abspath, path);
    if (flcache && !flcreate && !fldelete) {
        VFSCacheEnt* ent = _vfsGetFile(vfs, abspath, false);
        // only for simple case, i.e. no need to do COW or find a writable layer
        if (ent && (!flwrite || !(ent->mount->flags & VFS_ReadOnly))) {
            strDup(rpath, ent->origpath);
            ret = objAcquire(ent->mount);
            rwlockReleaseRead(&vfs->vfslock);
            rwlockReleaseRead(&vfs->vfsdlock);
            strDestroy(&abspath);
            return ret;
        }
    }

    saInit(&cands, VFSCand, 8);
    _vfsSnapshot(vfs, &cands, abspath, true);
    gen = vfs->mountgen;

    rwlockReleaseRead(&vfs->vfslock);
    rwlockReleaseRead(&vfs->vfsdlock);

    // ---- phase 2: ask the providers, with no lock held
    saInit(&pending, VFSPendEnt, 8);

    for (int32 i = 0, n = saSize(cands); i < n; i++) {
        VFSMount* m = cands.a[i].mount;

        // save first writable provider we find
        if (!firstwritable && !(m->flags & VFS_ReadOnly)) {
            firstwritable = m;
            strDup(&firstwpath, cands.a[i].relpath);
        }

        if (cowmount && (m->flags & VFS_AlwaysCOW)) {
            // this provider wants to get COW copies for any write
            *cowmount = objAcquire(m);
            strDup(cowrpath, cands.a[i].relpath);
            cowmount = NULL;   // don't let anything else set it
        }

        VFSProvider* provif = objInstIf(m->provider, VFSProvider);
        if (!provif)
            continue;

        // Start from this mount's own path every time. The case-insensitive helper rewrites it
        // with the provider's real casing, which must not carry over to the next provider.
        strDup(&curpath, cands.a[i].relpath);

        int stat;
        if (!(vfs->flags & VFS_CaseSensitive) && (m->flags & VFS_CaseSensitive)) {
            // case-sensitive file system on insensitive VFS, find the real underlying path
            stat = _vfsFindCIHelper(&curpath,
                                    cands.a[i].mountpath,
                                    cands.a[i].relcomp,
                                    m,
                                    provif,
                                    &pending);
        } else {
            stat = provif->stat(m->provider, curpath, NULL);
        }

        if (stat == FS_Directory) {
            // found a directory, don't cache this as a file
            ret = m;
            strDup(rpath, curpath);
            flcache = false;
            break;
        } else if (stat == FS_File) {
            // found an existing file
            ret = m;
            strDup(rpath, curpath);
            break;
        }

        // do we capture new files on this layer?
        if (flcreate && (m->flags & VFS_NewFiles)) {
            ret = m;
            strDup(rpath, curpath);
            // do not exit, keep searching to see if it exists in a lower layer
        }
    }

    if (ret && flwrite && (ret->flags & VFS_ReadOnly) && cowmount) {
        // let the caller know they should COW to a writable provider
        *cowmount = objAcquire(firstwritable);
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
        VFSPendEnt pe = { 0 };
        pe.mount      = objAcquire(ret);
        pathParent(&cachedir, abspath);
        pathFilename(&cachename, abspath);
        strDup(&pe.dirpath, cachedir);
        strDup(&pe.name, cachename);
        strDup(&pe.origpath, *rpath);
        _saPushPtr(SAHANDLE(&pending), stType(VFSPendEnt), &stgeneric(opaque, &pe), SAINT_Consume);
    }

    objAcquire(ret);

    // ---- phase 3: write what the providers told us into the cache
    if (saSize(pending) > 0) {
        rwlockAcquireRead(&vfs->vfsdlock);
        rwlockAcquireWrite(&vfs->vfslock);
        // A mount or unmount in the meantime means these entries may describe a tree that no
        // longer exists, so drop them rather than cache something stale.
        if (gen == vfs->mountgen)
            _vfsFlushPending(vfs, &pending);
        rwlockReleaseWrite(&vfs->vfslock);
        rwlockReleaseRead(&vfs->vfsdlock);
    }

    strDestroy(&abspath);
    strDestroy(&curpath);
    strDestroy(&firstwpath);
    strDestroy(&cachedir);
    strDestroy(&cachename);
    saDestroy(&pending);
    saDestroy(&cands);
    return ret;
}

_Use_decl_annotations_
void vfsCurDir(VFS* vfs, string* out)
{
    rwlockAcquireRead(&vfs->vfslock);
    strDup(out, vfs->curdir);
    rwlockReleaseRead(&vfs->vfslock);
}

_Use_decl_annotations_
bool vfsSetCurDir(VFS* vfs, strref cur)
{
    if (!pathIsAbsolute(cur))
        return false;

    rwlockAcquireWrite(&vfs->vfslock);
    strDup(&vfs->curdir, cur);
    rwlockReleaseWrite(&vfs->vfslock);
    return true;
}
