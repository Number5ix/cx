#include "vfs_private.h"

VFSCacheEnt *_vfsCacheEntCreate(VFSMount *m, string opath)
{
    VFSCacheEnt *c = xaAlloc(sizeof(VFSCacheEnt), XA_ZERO);
    c->mount = m;
    strDup(&c->origpath, opath);
    return c;
}

static void _vfsCacheEntDestroy(stype st, void *ptr, uint32 flags)
{
    VFSCacheEnt *e = (VFSCacheEnt*)ptr;
    strDestroy(&e->origpath);
    xaFree(e);
}

STypeOps VFSCacheEnt_ops = {
    .dtor = _vfsCacheEntDestroy
};

VFSDir *_vfsDirCreate(VFS *vfs, VFSDir *parent)
{
    VFSDir *d = xaAlloc(sizeof(VFSDir), XA_ZERO);
    d->parent = parent;             // weak ref
    d->mounts = saCreate(object, 1, 0);
    d->subdirs = htCreate(string, custom(ptr, VFSDir_ops), 8, (vfs->flags & VFS_CaseSensitive) ? 0 : HT_CaseInsensitive);
    d->files = htCreate(string, custom(ptr, VFSCacheEnt_ops), 8, (vfs->flags & VFS_CaseSensitive) ? 0 : HT_CaseInsensitive);
    return d;
}

static void _vfsDirDestroy(stype st, void *ptr, uint32 flags)
{
    VFSDir *d = (VFSDir*)ptr;
    saDestroy(&d->mounts);
    htDestroy(&d->files);
    htDestroy(&d->subdirs);
    strDestroy(&d->name);
    xaFree(d);
}

STypeOps VFSDir_ops = {
    .dtor = _vfsDirDestroy
};
