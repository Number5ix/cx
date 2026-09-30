#include "vfs_private.h"
#include "fswatch_private.h"
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

// Tell VFS watches the providers behind path changed. Called with no VFS lock held: they call
// straight back into the VFS, and into providers, to work out what to watch now.
static void vfsNotifyMountChange(_Inout_ VFS* vfs, _In_opt_ strref path)
{
    cchainCall(&vfs->onmountchange, stvar(strref, path));
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
    saClear(&vfs->root->listings);   // they hold mounts too
    rwlockReleaseWrite(&vfs->vfsdlock);

    // Watches let go of the providers too, which may be what was keeping a loop alive.
    vfsNotifyMountChange(vfs, NULL);

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

// Changes reported by a VFS_CacheListings mount's own watch. Forgets whatever the cache knew
// about what changed.
static void vfsCacheWatchCB(stvlist* cvars, FSWatch* w, FSWatchEvent* ev)
{
    ObjInst_WeakRef *vref = NULL, *mref = NULL;
    if (!stvlNext(cvars, weakref, &vref) || !stvlNext(cvars, weakref, &mref))
        return;

    VFS* vfs    = objAcquireFromWeakDyn(VFS, vref);
    VFSMount* m = objAcquireFromWeakDyn(VFSMount, mref);
    string vpath = 0, vold = 0;
    if (!vfs || !m)
        goto out;

    pathJoin(&vpath, m->path, ev->path);
    switch (ev->kind) {
    case FSWE_Modified:
    case FSWE_Attributes:
        // only what the directory holding it says about it
        _vfsInvalidateCache(vfs, vpath);
        break;
    case FSWE_Renamed:
        if (ev->oldpath) {
            pathJoin(&vold, m->path, ev->oldpath);
            _vfsInvalidateCache(vfs, vold);
            _vfsInvalidatePath(vfs, vold, true);
        }
        // fall through
    case FSWE_Created:
    case FSWE_Removed:
    case FSWE_Rescan:
        // Below it too: a directory that came or went takes everything under it along, including
        // listings that say it has nothing.
        _vfsInvalidateCache(vfs, vpath);
        _vfsInvalidatePath(vfs, vpath, true);
        break;
    case FSWE_Stopped:
        // Nothing keeps listings from this mount honest any more.
        atomicStore(bool, &m->listable, false, Release);
        _vfsInvalidateCache(vfs, m->path);
        _vfsInvalidatePath(vfs, m->path, true);
        break;
    }

out:
    strDestroy(&vpath);
    strDestroy(&vold);
    objRelease(&m);
    objRelease(&vfs);
}

_Use_decl_annotations_
void _vfsMountArmCache(VFS* vfs, VFSMount* m)
{
    if (m->flags & VFS_NoCache)
        return;

    if (m->flags & VFS_Immutable) {
        atomicStore(bool, &m->listable, true, Release);
        return;
    }

    // A VFS mounted inside a VFS has listings of its own, and watching it through another VFS
    // can loop back on this one.
    VFSWatchable* wif = objInstIf(m->provider, VFSWatchable);
    if (!(m->flags & VFS_CacheListings) || !wif || objDynCast(VFSVFS, m->provider))
        return;

    Weak(VFS)* vref      = objGetWeak(VFS, vfs);
    Weak(VFSMount)* mref = objGetWeak(VFSMount, m);
    closure cls          = closureCreateAs(FSWatchCB,
                                  vfsCacheWatchCB,
                                  stvar(weakref, objWeakRefBase(vref)),
                                  stvar(weakref, objWeakRefBase(mref)));
    objDestroyWeak(&vref);
    objDestroyWeak(&mref);

    // Without a watch there is nothing to keep listings current, so the mount just goes
    // uncached.
    FSWatch* w = wif->createWatch(m->provider, cls);
    if (!w)
        return;
    if (!fsWatchAdd(w, NULL, FSW_Subtree | FSW_Everything)) {
        _fsWatchStop(w);
        objRelease(&w);
        return;
    }

    m->cachewatch = w;
    atomicStore(bool, &m->listable, true, Release);
}

_Use_decl_annotations_
bool _vfsMountProvider(VFS* vfs, ObjInst* provider, strref path, flags_t flags)
{
    string ns = 0, rpath = 0, mpath = 0;
    VFSProvider* provif;
    VFSMount* nmount = NULL;
    bool ret         = false;

    // verify that this implements the right interface
    provif = objInstIf(provider, VFSProvider);
    if (!provif)
        return false;

    if (!pathIsAbsolute(path))
        return false;   // must mount with an absolute path

    // propagate certain flags from the VFS to all mounted providers
    bool newns = flags & VFS_MountNewNS;
    flags |= vfs->flags & (VFS_ReadOnly | VFS_NoCache);
    if (!(flags & VFS_MountNoListings))
        flags |= vfs->flags & VFS_CacheListings;
    flags &= ~(VFS_MountNewNS | VFS_MountNoListings);

    // The mount is set up before it is visible, so that its watch is running before anything
    // can list through it.
    strDup(&mpath, path);
    pathNormalize(&mpath);
    nmount = vfsmountCreate(provider, flags | provif->flags(provider), mpath);
    _vfsMountArmCache(vfs, nmount);

    rwlockAcquireWrite(&vfs->vfsdlock);

    pathSplitNS(&ns, &rpath, path);
    strDestroy(&rpath);

    if (newns && (strEmpty(ns) || htHasKey(vfs->namespaces, string, ns))) {
        // someone else got there first
        ret = true;
        rwlockReleaseWrite(&vfs->vfsdlock);
        goto done;
    }

    if (!strEmpty(ns) && !htHasKey(vfs->namespaces, string, ns)) {
        // namespace hasn't been added yet, create it now
        htInsert(&vfs->namespaces, string, ns, VFSDir, _vfsDirCreate(vfs, NULL));
    }

    VFSDir* dir = _vfsGetDir(vfs, path, false, false, true);
    if (!dir)
        goto out;

    saPushC(&dir->mounts, object, &nmount);
    nmount = NULL;   // the array has it now
    _vfsInvalidateRecursive(vfs, dir, true);
    vfs->mountgen++;
    ret = true;

out:
    rwlockReleaseWrite(&vfs->vfsdlock);
    if (ret)
        vfsNotifyMountChange(vfs, path);
done:
    objRelease(&nmount);
    strDestroy(&ns);
    strDestroy(&mpath);
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
    if (ret)
        vfsNotifyMountChange(vfs, path);
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
        saClear(&pdir->listings);
    }
    atomicFetchAdd(uint32, &vfs->cachegen, 1, Relaxed);

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

    atomicFetchAdd(uint32, &vfs->cachegen, 1, Relaxed);

    if (dir->cache && dir->parent) {
        // can just remove the whole thing
        htRemove(&dir->parent->subdirs, string, dir->name);
    } else {
        htClear(&dir->files);
        saClear(&dir->listings);

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

_Use_decl_annotations_
void _vfsInvalidatePath(VFS* vfs, strref abspath, bool recursive)
{
    string ns = 0;
    sa_string components;
    saInit(&components, string, 8, SA_Grow(Aggressive));
    pathDecompose(&ns, &components, abspath);

    rwlockAcquireWrite(&vfs->vfsdlock);

    // Walk down without creating anything: a directory the cache never saw has nothing to forget.
    VFSDir* dir = NULL;
    if (strEmpty(ns))
        dir = vfs->root;
    else if (!htFind(vfs->namespaces, string, ns, VFSDir, &dir))
        dir = NULL;

    for (int32 i = 0, n = saSize(components); dir && i < n; i++) {
        if (strEmpty(components.a[i]))
            continue;
        if (!htFind(dir->subdirs, string, components.a[i], VFSDir, &dir))
            dir = NULL;
    }

    if (dir) {
        if (recursive) {
            _vfsInvalidateRecursive(vfs, dir, true);
        } else {
            htClear(&dir->files);
            saClear(&dir->listings);
        }
    }
    atomicFetchAdd(uint32, &vfs->cachegen, 1, Relaxed);

    rwlockReleaseWrite(&vfs->vfsdlock);

    strDestroy(&ns);
    saDestroy(&components);
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

    // the directory itself has to stay, but what it remembers does not
    htClear(&dir->files);
    saClear(&dir->listings);
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
    atomicFetchAdd(uint32, &vfs->cachegen, 1, Relaxed);
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
void _vfsSnapshot(VFS* vfs, sa_VFSCand* out, strref abspath, bool isfile, VFSDir** dir)
{
    string ns = 0, curpath = 0, mountpath = 0;
    sa_string components, relcomp = saInitNone, mountcomp = saInitNone;

    saInit(&components, string, 8, SA_Grow(Aggressive));
    pathDecompose(&ns, &components, abspath);

    VFSDir* pdir   = _vfsGetDir(vfs, abspath, isfile, true, false);
    int32 relstart = saSize(components) - (isfile ? 1 : 0);
    if (dir)
        *dir = pdir;

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
            cand.ldepth  = -1;
            cand.mount   = objAcquire(pdir->mounts.a[i]);
            strDup(&cand.mountpath, mountpath);
            strDup(&cand.relpath, curpath);
            saSlice(&cand.relcomp, components, relstart, 0);
            saPushC(out, VFSCand, &cand);

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
VFSListing* _vfsListingFor(VFSDir* dir, VFSMount* m)
{
    for (int32 i = 0, n = saSize(dir->listings); i < n; i++) {
        if (dir->listings.a[i].mount == m)
            return &dir->listings.a[i];
    }
    return NULL;
}

_Use_decl_annotations_
bool _vfsListDir(VFSListing* out, VFS* vfs, VFSMount* m, VFSProvider* provif, strref relpath)
{
    _vfsListingInit(out, vfs, m, relpath);

    FSSearchIter iter;
    if (!provif->searchInit(m->provider, &iter, relpath, NULL, true)) {
        provif->searchFinish(m->provider, &iter);
        // An empty directory cannot be told apart from one that could not be read, so only a
        // directory that is not there at all gets a listing.
        if (provif->stat(m->provider, relpath, NULL) == FS_Directory) {
            _vfsListingDestroy(out);
            return false;
        }
        return true;
    }

    out->exists = true;
    while (provif->searchValid(m->provider, &iter)) {
        VFSListEnt ent = { .type = iter.type, .stat = iter.stat };
        // Two names differing only in case on a case-insensitive VFS: the first one wins, as
        // it would with no listing.
        htInsert(&out->ents, string, iter.name, VFSListEnt, ent, HT_Ignore);
        provif->searchNext(m->provider, &iter);
    }
    provif->searchFinish(m->provider, &iter);
    return true;
}

_Use_decl_annotations_
void _vfsStoreListings(VFS* vfs, sa_VFSPendList* lists)
{
    for (int32 i = 0, n = saSize(*lists); i < n; i++) {
        VFSPendList* pl = &lists->a[i];
        if (!_vfsMountListable(pl->l.mount))
            continue;

        VFSDir* d = _vfsGetDir(vfs, pl->dirpath, false, true, true);
        if (!d || _vfsListingFor(d, pl->l.mount))
            continue;
        saPushC(&d->listings, VFSListing, &pl->l);
    }
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
            saPushC(pending, VFSPendEnt, &pe);
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

_Use_decl_annotations_
void _vfsEnsureNamespace(VFS* vfs, strref path)
{
    if (!(vfs->flags & VFS_AutoMountDrives))
        return;

    // Only a drive-letter path can name a namespace that might appear later.
    if (strLen(path) < 2 || strGetChar(path, 1) != ':')
        return;
    char letter = strGetChar(path, 0);
    if (!((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z')))
        return;

    string ns = 0;
    strSubStr(&ns, path, 0, 1);

    rwlockAcquireRead(&vfs->vfsdlock);
    bool have = htHasKey(vfs->namespaces, string, ns);
    rwlockReleaseRead(&vfs->vfsdlock);

    if (!have)
        _vfsPlatformMountNamespace(vfs, ns);

    strDestroy(&ns);
}

// Unlike _vfsGetDir, never creates a VFSDir node to avoid polluting the tree with failed lookups.
_Use_decl_annotations_
VFSMount* _vfsFindSelfMount(VFS* vfs, strref abspath)
{
    VFSMount* ret = NULL;
    VFSDir* dir   = NULL;
    string ns     = 0;
    sa_string components;

    _vfsEnsureNamespace(vfs, abspath);

    saInit(&components, string, 8, SA_Grow(Aggressive));
    pathDecompose(&ns, &components, abspath);

    rwlockAcquireRead(&vfs->vfsdlock);
    rwlockAcquireRead(&vfs->vfslock);

    if (strEmpty(ns)) {
        dir = vfs->root;
    } else if (!htFind(vfs->namespaces, string, ns, VFSDir, &dir)) {
        dir = NULL;
    }

    for (int32 i = 0, n = saSize(components); dir && i < n; i++) {
        if (strEmpty(components.a[i]))
            continue;   // leading separator -> root, already resolved above
        if (!htFind(dir->subdirs, string, components.a[i], VFSDir, &dir))
            dir = NULL;
    }

    if (dir && saSize(dir->mounts) > 0)
        ret = objAcquire(dir->mounts.a[saSize(dir->mounts) - 1]);

    rwlockReleaseRead(&vfs->vfslock);
    rwlockReleaseRead(&vfs->vfsdlock);

    strDestroy(&ns);
    saDestroy(&components);
    return ret;
}

_Use_decl_annotations_
void _vfsCandLocateDir(VFSCand* c, VFSDir* dir, int32 dirdepth)
{
    VFSMount* m = c->mount;
    if (dirdepth <= 0) {
        strClear(&c->ldir);
        c->ldepth = 0;
        return;
    }

    VFSDir* child = dir;
    VFSDir* anc   = dir->parent;
    for (int32 depth = dirdepth - 1; anc && depth >= 0; depth--) {
        VFSListing* al = _vfsListingFor(anc, m);
        if (al) {
            htelem e = al->exists ? htFind(al->ents, string, child->name, none, NULL) : 0;
            if (e && hteValPtr(al->ents, VFSListEnt, e)->type == FS_Directory) {
                pathJoin(&c->ldir, al->relpath, hteKey(al->ents, string, e));
                c->ldepth = depth + 1;
            } else {
                c->lnodir = true;
            }
            return;
        }
        child = anc;
        anc   = anc->parent;
    }
}

// What the cached listings say about the entry fname for candidate c, whose directory node is
// pdir. VFS locks must be held.
static void candFromListings(_Inout_ VFSCand* c, _In_ VFSDir* pdir, _In_ strref fname)
{
    VFSMount* m = c->mount;
    if (!_vfsMountListable(m))
        return;

    int32 dirdepth = saSize(c->relcomp) - 1;
    VFSListing* l  = _vfsListingFor(pdir, m);
    if (l) {
        strDup(&c->ldir, l->relpath);
        c->ldepth = dirdepth;
        htelem e  = l->exists ? htFind(l->ents, strref, fname, none, NULL) : 0;
        if (e) {
            VFSListEnt* le = hteValPtr(l->ents, VFSListEnt, e);
            c->lstate      = VFS_LHit;
            c->ltype       = le->type;
            c->lstat       = le->stat;
            strDup(&c->lname, hteKey(l->ents, string, e));
        } else {
            c->lstate = VFS_LMiss;
        }
        return;
    }

    // No listing of the directory yet. The nearest ancestor that has one says where the
    // directory really is, or that it is not there at all.
    _vfsCandLocateDir(c, pdir, dirdepth);
    if (c->lnodir)
        c->lstate = VFS_LMiss;
}

_Use_decl_annotations_
VFSListing* _vfsCandListDir(VFS* vfs, VFSCand* c, VFSProvider* provif, sa_VFSPendList* lists,
                            strref dirpath, int32 dirdepth)
{
    VFSMount* m     = c->mount;
    bool casefix    = !(vfs->flags & VFS_CaseSensitive) && (m->flags & VFS_CaseSensitive);
    bool exists     = true;
    string real = 0, vdir = 0;
    VFSListing* ret = NULL;
    int32 depth;

    if (c->ldepth >= 0) {
        strDup(&real, c->ldir);
        depth = c->ldepth;
    } else if (!casefix) {
        // the VFS's spelling is good enough for the provider
        strJoin(&real, c->relcomp, fsPathSepStr);
        if (dirdepth < saSize(c->relcomp) && !pathParent(&real, real))
            strClear(&real);
        depth = dirdepth;
    } else {
        depth = 0;
    }

    if (depth < dirdepth) {
        strDup(&vdir, c->mountpath);
        for (int32 i = 0; i < depth; i++) pathJoin(&vdir, vdir, c->relcomp.a[i]);
    }

    while (depth < dirdepth && exists) {
        VFSPendList pl = { 0 };
        if (!_vfsListDir(&pl.l, vfs, m, provif, real))
            goto out;
        strDup(&pl.dirpath, vdir);

        htelem e = pl.l.exists ? htFind(pl.l.ents, string, c->relcomp.a[depth], none, NULL) : 0;
        if (e && hteValPtr(pl.l.ents, VFSListEnt, e)->type == FS_Directory)
            pathJoin(&real, real, hteKey(pl.l.ents, string, e));
        else
            exists = false;

        saPushC(lists, VFSPendList, &pl);
        pathJoin(&vdir, vdir, c->relcomp.a[depth]);
        depth++;
    }

    VFSPendList pl = { 0 };
    if (exists) {
        if (!_vfsListDir(&pl.l, vfs, m, provif, real))
            goto out;
    } else {
        // somewhere above it is missing, so it is not there either
        _vfsListingInit(&pl.l, vfs, m, real);
    }
    strDup(&pl.dirpath, dirpath);
    int32 idx = saPushC(lists, VFSPendList, &pl);
    ret       = &lists->a[idx].l;

out:
    strDestroy(&real);
    strDestroy(&vdir);
    return ret;
}

// A plain lookup answered from the listings alone, with nothing allocated for layers that are
// not needed. Walks the same layers in the same order as _vfsSnapshot. Returns the mount on a
// hit, with a reference; NULL with *known set for a definite miss; NULL with *known clear if some
// layer has no listing to go by. VFS locks must be held.
static VFSMount* listingsLookup(_In_ VFSDir* pdir, _In_ strref fname, _Inout_ string* rpath,
                                _Out_ VFSFound* found, _Out_ bool* known)
{
    *known = false;
    for (VFSDir* d = pdir; d; d = d->parent) {
        for (int32 i = saSize(d->mounts) - 1; i >= 0; --i) {
            VFSMount* m = d->mounts.a[i];
            if (!_vfsMountListable(m))
                return NULL;
            VFSListing* l = _vfsListingFor(pdir, m);
            if (!l)
                return NULL;

            htelem e = l->exists ? htFind(l->ents, strref, fname, none, NULL) : 0;
            if (e) {
                VFSListEnt* le = hteValPtr(l->ents, VFSListEnt, e);
                found->type    = le->type;
                found->stat    = le->stat;
                found->valid   = true;
                pathJoin(rpath, l->relpath, hteKey(l->ents, string, e));
                *known = true;
                return objAcquire(m);
            }
            if (m->flags & VFS_Opaque)
                goto miss;
        }
    }

miss:
    *known          = true;
    VFSDir* self    = NULL;
    found->nomount  = !htFind(pdir->subdirs, strref, fname, VFSDir, &self) ||
                     saSize(self->mounts) == 0;
    return NULL;
}

// This function does all the heavy lifting of the VFS system.
//
// It runs in three phases, and the split is the point: the middle phase calls into providers,
// and a provider can be another VFS pointing back at this one, so it must run with no VFS lock
// held. Phase one gathers everything a provider call needs into a snapshot, phase two does the
// calls, and phase three puts the results into the cache.
//
// A layer whose listing of the directory is cached is answered from it in phase one, and never
// asked. A listable layer with no listing yet is asked for the whole directory instead of just
// the one entry, and phase three keeps the listing.
_Use_decl_annotations_
VFSMount* _vfsFindMount(VFS* vfs, string* rpath, strref path, VFSMount** cowmount, string* cowrpath,
                        uint32 flags, VFSFound* found)
{
    VFSMount* ret           = 0;
    VFSMount* firstwritable = 0;
    VFSMount* alwayscow     = 0;
    string abspath = 0, curpath = 0, firstwpath = 0, dirpath = 0, fname = 0;
    sa_VFSCand cands      = saInitNone;
    sa_VFSPendEnt pending = saInitNone;
    sa_VFSPendList lists  = saInitNone;
    VFSDir* pdir          = NULL;
    uint32 gen, cgen;

    if (cowmount)
        *cowmount = NULL;
    if (found)
        memset(found, 0, sizeof(VFSFound));

    if (!vfs)
        return NULL;

    _vfsMaybeEvict(vfs);
    _vfsEnsureNamespace(vfs, path);

    bool flwrite  = flags & VFS_FindWriteFile;
    bool fldelete = flags & VFS_FindDelete;
    bool flcreate = flags & VFS_FindCreate;
    bool flcache  = flags & VFS_FindCache;

    // ---- phase 1: everything that needs a lock, and nothing that calls a provider
    rwlockAcquireRead(&vfs->vfsdlock);
    rwlockAcquireRead(&vfs->vfslock);

    _vfsAbsPath(vfs, &abspath, path);
    pathFilename(&fname, abspath);

    // see if we can get this from the file cache
    if (flcache && !flcreate && !fldelete) {
        pdir             = _vfsGetDir(vfs, abspath, true, true, false);
        VFSCacheEnt* ent = NULL;
        if (pdir)
            htFind(pdir->files, string, fname, VFSCacheEnt, &ent);
        // only for simple case, i.e. no need to do COW or find a writable layer
        if (ent && (!flwrite || !(ent->mount->flags & VFS_ReadOnly))) {
            strDup(rpath, ent->origpath);
            ret = objAcquire(ent->mount);

            // the mount's listing of the directory, if it has one, has the rest
            VFSListing* l = (found && _vfsMountListable(ret)) ? _vfsListingFor(pdir, ret) : NULL;
            htelem e      = (l && l->exists) ? htFind(l->ents, strref, fname, none, NULL) : 0;
            if (e) {
                VFSListEnt* le = hteValPtr(l->ents, VFSListEnt, e);
                found->type    = le->type;
                found->stat    = le->stat;
                found->valid   = true;
            }

            rwlockReleaseRead(&vfs->vfslock);
            rwlockReleaseRead(&vfs->vfsdlock);
            strDestroy(&abspath);
            strDestroy(&fname);
            return ret;
        }

        // a plain lookup the listings can answer on their own
        bool known = false;
        if (found && !flwrite && pdir && !strEmpty(fname))
            ret = listingsLookup(pdir, fname, rpath, found, &known);
        if (known) {
            rwlockReleaseRead(&vfs->vfslock);
            rwlockReleaseRead(&vfs->vfsdlock);
            if (!ret)
                cxerr = CX_FileNotFound;
            strDestroy(&abspath);
            strDestroy(&fname);
            return ret;
        }
    }

    saInit(&cands, VFSCand, 8);
    _vfsSnapshot(vfs, &cands, abspath, true, &pdir);
    if (pdir && !strEmpty(fname)) {
        for (int32 i = 0, n = saSize(cands); i < n; i++) candFromListings(&cands.a[i], pdir, fname);

        VFSDir* self = NULL;
        if (found)
            found->nomount = !htFind(pdir->subdirs, strref, fname, VFSDir, &self) ||
                             saSize(self->mounts) == 0;
    }
    gen  = vfs->mountgen;
    cgen = atomicLoad(uint32, &vfs->cachegen, Relaxed);

    rwlockReleaseRead(&vfs->vfslock);
    rwlockReleaseRead(&vfs->vfsdlock);

    // ---- phase 2: ask the providers, with no lock held
    saInit(&pending, VFSPendEnt, 8);
    if (!pathParent(&dirpath, abspath))
        strDup(&dirpath, abspath);

    for (int32 i = 0, n = saSize(cands); i < n; i++) {
        VFSCand* c  = &cands.a[i];
        VFSMount* m = c->mount;

        // save first writable provider we find
        if (!firstwritable && !(m->flags & VFS_ReadOnly)) {
            firstwritable = m;
            strDup(&firstwpath, c->relpath);
        }

        if (cowmount && !alwayscow && (m->flags & VFS_AlwaysCOW)) {
            // this provider wants to get COW copies for any write
            alwayscow = m;
            *cowmount = objAcquire(m);
            strDup(cowrpath, c->relpath);
        }

        VFSProvider* provif = objInstIf(m->provider, VFSProvider);
        if (!provif)
            continue;

        // Start from this mount's own path every time. The case-insensitive helper rewrites it
        // with the provider's real casing, which must not carry over to the next provider.
        strDup(&curpath, c->relpath);

        int stat       = FS_Nonexistent;
        FSStat st      = { 0 };
        bool stvalid   = false;
        VFSListing* dl = NULL;

        if (c->lstate == VFS_LHit) {
            stat    = c->ltype;
            st      = c->lstat;
            stvalid = true;
            pathJoin(&curpath, c->ldir, c->lname);
        } else if (c->lstate == VFS_LMiss) {
            // anything created here goes in the directory's real path, when it is known
            if (c->ldepth == saSize(c->relcomp) - 1)
                pathJoin(&curpath, c->ldir, fname);
        } else if (_vfsMountListable(m) && !strEmpty(fname) &&
                   (dl = _vfsCandListDir(vfs, c, provif, &lists, dirpath, saSize(c->relcomp) - 1))) {
            htelem e = dl->exists ? htFind(dl->ents, string, fname, none, NULL) : 0;
            if (e) {
                VFSListEnt* le = hteValPtr(dl->ents, VFSListEnt, e);
                stat           = le->type;
                st             = le->stat;
                stvalid        = true;
                pathJoin(&curpath, dl->relpath, hteKey(dl->ents, string, e));
            } else {
                pathJoin(&curpath, dl->relpath, fname);
            }
        } else if (!(vfs->flags & VFS_CaseSensitive) && (m->flags & VFS_CaseSensitive)) {
            // case-sensitive file system on insensitive VFS, find the real underlying path
            stat = _vfsFindCIHelper(&curpath, c->mountpath, c->relcomp, m, provif, &pending);
        } else {
            stat    = provif->stat(m->provider, curpath, &st);
            stvalid = stat != FS_Nonexistent;
        }

        if (stat == FS_Directory || stat == FS_File) {
            ret = m;
            strDup(rpath, curpath);
            if (found) {
                found->type  = stat;
                found->stat  = st;
                found->valid = stvalid;
            }
            // a directory is not cached as a file
            if (stat == FS_Directory)
                flcache = false;
            break;
        }

        // do we capture new files on this layer?
        if (flcreate && (m->flags & VFS_NewFiles)) {
            ret = m;
            strDup(rpath, curpath);
            // do not exit, keep searching to see if it exists in a lower layer
        }
    }

    if (ret && flwrite && (ret->flags & VFS_ReadOnly) && cowmount && !*cowmount) {
        // let the caller know they should COW to a writable provider
        *cowmount = objAcquire(firstwritable);
        strDup(cowrpath, firstwpath);
    }

    // didn't find a provider? if we're writing, go ahead and create a new file
    if (!ret && (flwrite || flcreate) && !fldelete) {
        ret = firstwritable;
        strDup(rpath, firstwpath);
    }

    // The file already lives on (or is being created on) the AlwaysCOW layer, so there is
    // nothing to copy up.
    if (alwayscow && ret == alwayscow) {
        objRelease(cowmount);
        strDestroy(cowrpath);
    }

    if (!ret)
        cxerr = CX_FileNotFound;

    if (ret && flcache && !(ret->flags & VFS_NoCache)) {
        VFSPendEnt pe = { 0 };
        pe.mount      = objAcquire(ret);
        strDup(&pe.dirpath, dirpath);
        strDup(&pe.name, fname);
        strDup(&pe.origpath, *rpath);
        saPushC(&pending, VFSPendEnt, &pe);
    }

    objAcquire(ret);

    // ---- phase 3: write what the providers told us into the cache
    if (saSize(pending) > 0 || saSize(lists) > 0) {
        rwlockAcquireRead(&vfs->vfsdlock);
        rwlockAcquireWrite(&vfs->vfslock);
        // A mount or unmount in the meantime means these entries may describe a tree that no
        // longer exists, and an invalidation means a listing may be missing a change that was
        // already reported, so drop them rather than cache something stale.
        if (gen == vfs->mountgen) {
            _vfsFlushPending(vfs, &pending);
            if (cgen == atomicLoad(uint32, &vfs->cachegen, Relaxed))
                _vfsStoreListings(vfs, &lists);
        }
        rwlockReleaseWrite(&vfs->vfslock);
        rwlockReleaseRead(&vfs->vfsdlock);
    }

    strDestroy(&abspath);
    strDestroy(&curpath);
    strDestroy(&firstwpath);
    strDestroy(&dirpath);
    strDestroy(&fname);
    saDestroy(&pending);
    saDestroy(&lists);
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
