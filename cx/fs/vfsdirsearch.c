#include "vfs_private.h"
#include <cx/debug/error.h>

static intptr dirEntCmp(stype st, stgeneric g1, stgeneric g2, uint32 flags)
{
    VFSDirEnt* ent1 = (VFSDirEnt*)g1.st_opaque;
    VFSDirEnt* ent2 = (VFSDirEnt*)g2.st_opaque;

    if (flags & ST_CaseInsensitive)
        return strCmpi(ent1->name, ent2->name);

    return strCmp(ent1->name, ent2->name);
}

// saSortCustom() is the only sort that hands flags to the comparator, which is how a VFS's case
// sensitivity reaches dirEntCmp.
static intptr dirEntSortCmp(stype st, stgeneric g1, stgeneric g2, flags_t flags, void* ctx)
{
    return dirEntCmp(st, g1, g2, flags);
}

static void dirEntCopy(stype st, stgeneric* gdest, stgeneric gsrc, uint32 flags)
{
    VFSDirEnt* ent = (VFSDirEnt*)gdest->st_opaque;
    VFSDirEnt* src = (VFSDirEnt*)gsrc.st_opaque;

    ent->name = 0;
    strDup(&ent->name, src->name);
    ent->type = src->type;
    ent->stat = src->stat;
}

static void dirEntDestroy(stype st, stgeneric* g, uint32 flags)
{
    VFSDirEnt* ent = (VFSDirEnt*)g->st_opaque;
    strDestroy(&ent->name);
}

static stDefine(VFSDirEnt) {
    .id    = stTypeId(opaque),
    .size  = sizeof(VFSDirEnt),
    .flags = stFlag(PassPtr),
    .ops   = { .cmp = dirEntCmp, .copy = dirEntCopy, .dtor = dirEntDestroy }
};

#define SType_VFSDirEnt                         VFSDirEnt*
#define STStorageType_VFSDirEnt                 VFSDirEnt
#define STypeArg_VFSDirEnt(type, val)           stgeneric(opaque, &(val))
#define STypeArgPtr_VFSDirEnt(type, val)        &stgeneric(opaque, (val))
#define STypeCheckedArg_VFSDirEnt(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_VFSDirEnt(type, val) stType(type), stArgPtr(type, val)

// Like _vfsFindMount, this runs in three phases so that no provider is ever called with a VFS
// lock held -- a provider here can be a VFSVFS pointing back at this same VFS.
_Use_decl_annotations_
bool vfsSearchInit(FSSearchIter* iter, VFS* vfs, strref path, strref pattern, int typefilter,
                   bool stat)
{
    string abspath = 0, curpath = 0, filepath = 0;
    hashtable names;
    sa_string mountpoints = saInitNone;
    sa_VFSMount mountmnts = saInitNone;
    sa_VFSCand cands      = saInitNone;
    sa_VFSPendEnt pending = saInitNone;
    uint32 gen  = 0;
    int32 idx;
    bool exists = false;

    cxerr = CX_Success;
    memset(iter, 0, sizeof(FSSearchIter));

    _vfsMaybeEvict(vfs);

    if ((vfs->flags & VFS_CaseSensitive))
        htInit(&names, string, intptr, 8, HT_RefKeys | HT_Grow(MaxSpeed));
    else
        htInit(&names, string, intptr, 8, HT_CaseInsensitive | HT_RefKeys | HT_Grow(MaxSpeed));

    saInit(&mountpoints, string, 8);
    saInit(&mountmnts, object, 8);
    saInit(&cands, VFSCand, 8);
    saInit(&pending, VFSPendEnt, 8);

    VFSSearch* search = xaAlloc(sizeof(VFSSearch), XA_Zero);
    iter->_search     = search;
    search->vfs       = objAcquire(vfs);
    search->idx       = 0;
    saInit(&search->ents, VFSDirEnt, 16, SA_Grow(Aggressive));

    // ---- phase 1: gather what the providers will be asked about
    rwlockAcquireRead(&vfs->vfsdlock);
    rwlockAcquireRead(&vfs->vfslock);

    _vfsAbsPath(vfs, &abspath, path);
    VFSDir* vfsdir = _vfsGetDir(vfs, abspath, false, false, false);

    if (!vfsdir) {
        rwlockReleaseRead(&vfs->vfslock);
        rwlockReleaseRead(&vfs->vfsdlock);
        cxerr = CX_InvalidArgument;
        goto done;
    }

    // child mount points appear as subdirectories of this one
    foreach (hashtable, sdi, vfsdir->subdirs) {
        VFSDir* sd = htiVal(VFSDir, sdi);
        if (saSize(sd->mounts) > 0) {
            saPush(&mountpoints, string, sd->name);
            // keep the topmost provider mounted there, so its metadata can be fetched later
            saPush(&mountmnts, object, sd->mounts.a[saSize(sd->mounts) - 1]);
        }
    }

    _vfsSnapshot(vfs, &cands, abspath, false);
    gen = vfs->mountgen;

    rwlockReleaseRead(&vfs->vfslock);
    rwlockReleaseRead(&vfs->vfsdlock);

    // ---- phase 2: collect entries, with no lock held

    // Mount points go in first so they take priority over a provider entry of the same name.
    // They are filtered exactly like provider entries -- a mount point is a directory that
    // happens to be produced by the VFS rather than by a provider, not an exception to the
    // caller's pattern and type filter.
    for (int32 i = 0, n = saSize(mountpoints); i < n; i++) {
        if (typefilter && (FS_Directory & typefilter) != typefilter)
            continue;
        if (!strEmpty(pattern) && !pathMatch(mountpoints.a[i], pattern, 0))
            continue;

        VFSDirEnt ent = { 0 };
        strDup(&ent.name, mountpoints.a[i]);
        ent.type = FS_Directory;
        if (stat) {
            // ask the mount's own provider about its root, which is what this entry names
            VFSProvider* mprovif = objInstIf(mountmnts.a[i]->provider, VFSProvider);
            if (mprovif)
                mprovif->stat(mountmnts.a[i]->provider, NULL, &ent.stat);
        }
        idx = saPushC(&search->ents, VFSDirEnt, &ent);
        htInsert(&names, string, search->ents.a[idx].name, intptr, 1);

        // the directory exists as far as a caller is concerned, even if no provider knows it
        exists = true;
    }

    // start at the target directory and recurse upwards to see if any providers know about
    // this directory
    for (int32 i = 0, n = saSize(cands); i < n; i++) {
        VFSMount* m         = cands.a[i].mount;
        VFSProvider* provif = objInstIf(m->provider, VFSProvider);
        if (!provif)
            continue;

        // Start from this mount's own path every time. The case-insensitive helper rewrites it
        // with the provider's real casing, which must not carry over to the next provider.
        strDup(&curpath, cands.a[i].relpath);

        if (!(vfs->flags & VFS_CaseSensitive) && (m->flags & VFS_CaseSensitive)) {
            // case-sensitive file system on insensitive VFS, find the real underlying path
            _vfsFindCIHelper(&curpath, cands.a[i].mountpath, cands.a[i].relcomp, m, provif,
                             &pending);
        }

        // see if we can get a directory listing out of it
        FSSearchIter dsiter;
        if (!provif->searchInit(m->provider, &dsiter, curpath, pattern, stat)) {
            provif->searchFinish(m->provider, &dsiter);
            continue;
        }

        // we did! so gather up all the files
        exists = true;
        while (provif->searchValid(m->provider, &dsiter)) {
            // have we seen this file already on a higher layer?
            if ((!typefilter || (dsiter.type & typefilter) == typefilter) &&
                !htHasKey(names, string, dsiter.name)) {
                // add to list and hash table of seen files
                VFSDirEnt ent = { .name = dsiter.name,   // borrowed ref!
                                  .type = dsiter.type,
                                  .stat = dsiter.stat };
                idx = saPush(&search->ents, VFSDirEnt, ent);
                htInsert(&names, string, search->ents.a[idx].name, intptr, 1);

                if (dsiter.type == FS_File && !(m->flags & VFS_NoCache)) {
                    // remember it for the cache while we're here
                    pathJoin(&filepath, curpath, dsiter.name);
                    VFSPendEnt pe = { 0 };
                    pe.mount      = objAcquire(m);
                    strDup(&pe.dirpath, abspath);
                    strDup(&pe.name, dsiter.name);
                    strDup(&pe.origpath, filepath);
                    saPushC(&pending, VFSPendEnt, &pe);
                }
            }
            provif->searchNext(m->provider, &dsiter);
        }
        provif->searchFinish(m->provider, &dsiter);
    }

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

done:
    saSortCustom(&search->ents, dirEntSortCmp, NULL,
                 (vfs->flags & VFS_CaseSensitive) ? 0 : ST_CaseInsensitive);

    strDestroy(&abspath);
    strDestroy(&curpath);
    strDestroy(&filepath);
    saDestroy(&pending);
    saDestroy(&cands);
    saDestroy(&mountpoints);
    saDestroy(&mountmnts);
    htDestroy(&names);

    // did the path exist somewhere in the VFS?
    if (exists) {
        vfsSearchNext(iter);
        return true;
    } else {
        vfsSearchFinish(iter);
        if (cxerr == CX_Success)
            cxerr = CX_FileNotFound;
        return false;
    }
}

_Use_decl_annotations_
bool vfsSearchNext(FSSearchIter* iter)
{
    VFSSearch* search = (VFSSearch*)iter->_search;

    if (!search)
        return false;

    if (search->idx >= saSize(search->ents)) {
        vfsSearchFinish(iter);
        return false;
    }

    _Analysis_assume_(search->ents.a != NULL);   // because saSize returned > 0
    VFSDirEnt* ent = &search->ents.a[search->idx];
    strDup(&iter->name, ent->name);
    iter->type = ent->type;
    iter->stat = ent->stat;
    search->idx++;

    return true;
}

_Use_decl_annotations_
void vfsSearchFinish(FSSearchIter* iter)
{
    VFSSearch* search = (VFSSearch*)iter->_search;
    if (!search)
        return;

    strDestroy(&iter->name);

    saDestroy(&search->ents);
    objRelease(&search->vfs);
    xaDestroy(&iter->_search);
}
