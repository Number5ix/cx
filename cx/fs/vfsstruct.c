#include "vfs_private.h"

VFSCacheEnt *_vfsCacheEntCreate(VFSMount *m, strref opath)
{
    VFSCacheEnt *c = xaAlloc(sizeof(VFSCacheEnt), Zero);
    c->mount = m;
    strDup(&c->origpath, opath);
    return c;
}

static void _vfsCacheEntDestroy(stype st, stgeneric *g, uint32 flags)
{
    VFSCacheEnt *e = (VFSCacheEnt*)g->st_ptr;
    strDestroy(&e->origpath);
    xaFree(e);
}

STypeOps VFSCacheEnt_ops = {
    .dtor = _vfsCacheEntDestroy
};

VFSDir *_vfsDirCreate(VFS *vfs, VFSDir *parent)
{
    VFSDir *d = xaAlloc(sizeof(VFSDir), Zero);
    d->parent = parent;             // weak ref
    saInit(&d->mounts, object, 1);
    if (vfs->flags & VFS_CaseSensitive) {
        htInit(&d->subdirs, string, custom(ptr, VFSDir_ops), 8);
        htInit(&d->files, string, custom(ptr, VFSCacheEnt_ops), 8);
    } else {
        htInit(&d->subdirs, string, custom(ptr, VFSDir_ops), 8, CaseInsensitive);
        htInit(&d->files, string, custom(ptr, VFSCacheEnt_ops), 8, CaseInsensitive);
    }
    return d;
}

static void _vfsDirDestroy(stype st, stgeneric *g, uint32 flags)
{
    VFSDir *d = (VFSDir*)g->st_ptr;
    saRelease(&d->mounts);
    htRelease(&d->files);
    htRelease(&d->subdirs);
    strDestroy(&d->name);
    xaFree(d);
}

STypeOps VFSDir_ops = {
    .dtor = _vfsDirDestroy
};
