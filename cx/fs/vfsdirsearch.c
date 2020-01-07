#include "vfs_private.h"

static intptr dirEntCmp(stype st, const void *ptr1, const void *ptr2, uint32 flags)
{
    FSDirEnt *ent1 = (FSDirEnt*)ptr1;
    FSDirEnt *ent2 = (FSDirEnt*)ptr2;

    return strCmpi(ent1->name, ent2->name);
}

static intptr dirEntCmpCaseSensitive(stype st, const void *ptr1, const void *ptr2, uint32 flags)
{
    FSDirEnt *ent1 = (FSDirEnt*)ptr1;
    FSDirEnt *ent2 = (FSDirEnt*)ptr2;

    return strCmp(ent1->name, ent2->name);
}

static void dirEntCopy(stype st, void *ptr, const void *ptrsrc, uint32 flags)
{
    FSDirEnt *ent = (FSDirEnt*)ptr;
    FSDirEnt *src = (FSDirEnt*)ptrsrc;

    ent->name = 0;
    strDup(&ent->name, src->name);
    ent->type = src->type;
    ent->stat = src->stat;
}

static void dirEntDestroy(stype st, void *ptr, uint32 flags)
{
    FSDirEnt *ent = (FSDirEnt*)ptr;
    strDestroy(&ent->name);
}

static STypeOps FSDirEnt_ops = {
    .cmp = dirEntCmp,
    .copy = dirEntCopy,
    .dtor = dirEntDestroy,
};

static STypeOps FSDirEnt_ops_cs = {
    .cmp = dirEntCmpCaseSensitive,
    .copy = dirEntCopy,
    .dtor = dirEntDestroy,
};

VFSDirSearch *vfsSearchDir(VFS *vfs, string path, string pattern, int typefilter, bool stat)
{
    string abspath = 0, curpath = 0, filepath = 0;
    hashtable names;
    int32 idx;

    if ((vfs->flags & VFS_CaseSensitive))
        names = htCreate(string, intptr, 8, RefKeys, Grow(MaxSpeed));
    else
        names = htCreate(string, intptr, 8, CaseInsensitive, RefKeys, Grow(MaxSpeed));

    // just hold the write lock for this since we'll be adding entries to the cache throughout
    rwlockAcquireWrite(vfs->vfslock);

    _vfsAbsPath(vfs, &abspath, path);

    VFSDir *vfsdir = _vfsGetDir(vfs, abspath, false, false, true), *pdir = vfsdir;
    string ns = 0, *components = 0, *relcomp = 0;

    if (!vfsdir)
        return NULL;

    pathDecompose(&ns, &components, abspath);

    VFSDirSearch *ret = xaAlloc(sizeof(VFSDirSearch), XA_ZERO);
    ret->vfs = objAcquire(vfs);
    ret->idx = 0;
    STypeOps direntops = (vfs->flags & VFS_CaseSensitive) ? FSDirEnt_ops_cs : FSDirEnt_ops;
    ret->ents = saCreate(custom(opaque(FSDirEnt), direntops), 16, Grow(Aggressive));

    // add child mount points as subdirectories
    htiter sdi;
    htiCreate(&sdi, &vfsdir->subdirs);
    while (htiNext(&sdi)) {
        VFSDir *sd = (VFSDir*)htiVal(sdi, ptr);
        if (saSize(&sd->mounts) > 0) {
            FSDirEnt ent = { 0 };
            strDup(&ent.name, sd->name);
            ent.type = FS_Directory;
            saPushC(&ret->ents, opaque, &ent);
            htInsert(&names, string, sd->name, intptr, 1);
        }
    }
    htiDestroy(&sdi);

    // start at the target directory and recurse upwards to see if any providers know about
    // this directory
    int32 relstart = saSize(&components);
    while (pdir) {
        devAssert(relstart >= 0);
        saDestroy(&relcomp);
        relcomp = saSlice(&components, relstart, 0);
        strJoin(&curpath, relcomp, fsPathSepStr);

        // traverse list of registered providers backwards, as providers registered later
        // are "higher" on the stack
        for (int i = saSize(&pdir->mounts) - 1; i >= 0; --i) {
            VFSProvider *provif = objInstIf(pdir->mounts[i]->provider, VFSProvider);
            if (!provif)
                continue;

            if (!(vfs->flags & VFS_CaseSensitive) && (pdir->mounts[i]->flags & VFS_CaseSensitive)) {
                // case-sensitive file system on insensitive VFS, find the real underlying path
                _vfsFindCIHelper(vfs, vfsdir, &curpath, relcomp, pdir->mounts[i], provif);
            }

            // see if we can get a directory listing out of it
            ObjInst *dsprov = provif->searchDir(pdir->mounts[i]->provider, curpath, pattern, stat);
            VFSDirSearchProvider *dsprovif = objInstIf(dsprov, VFSDirSearchProvider);
            if (!dsprovif)
                continue;

            // we did! so gather up all the files
            FSDirEnt *ent = dsprovif->next(dsprov);
            while (ent) {
                // have we seen this file already on a higher layer?
                if ((!typefilter || (ent->type & typefilter) == typefilter) &&
                    !htHasKey(&names, string, ent->name)) {
                    // add to list and hash table of seen files
                    idx = saPush(&ret->ents, opaque, *ent);
                    htInsert(&names, string, ret->ents[idx].name, intptr, 1);

                    if (ent->type == FS_File && !(pdir->mounts[i]->flags & VFS_NoCache)) {
                        // go ahead and add it to the cache while we're here
                        pathJoin(&filepath, curpath, ent->name);
                        VFSCacheEnt *newent = _vfsCacheEntCreate(pdir->mounts[i], filepath);
                        htInsertC(&vfsdir->files, string, ent->name, ptr, &newent, Ignore);
                    }
                }
                ent = dsprovif->next(dsprov);
            }
            objRelease(dsprov);

            // if this layer is opaque, the buck stops here
            if (pdir->mounts[i]->flags & VFS_Opaque)
                goto done;
        }

        relstart--;
        pdir = pdir->parent;
    }

done:
    rwlockReleaseWrite(vfs->vfslock);
    saSort(&ret->ents, true);

    strDestroy(&ns);
    strDestroy(&abspath);
    strDestroy(&curpath);
    strDestroy(&filepath);
    saDestroy(&relcomp);
    saDestroy(&components);
    htDestroy(&names);
    return ret;
}

FSDirEnt *vfsSearchNext(VFSDirSearch *search)
{
    if (search->idx >= saSize(&search->ents))
        return NULL;

    return &search->ents[search->idx++];
}

void vfsSearchClose(VFSDirSearch *search)
{
    saDestroy(&search->ents);
    objRelease(search->vfs);
    xaFree(search);
}
