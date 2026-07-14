#include "vfs_private.h"

VFSCacheEnt* _vfsCacheEntCreate(VFSMount* m, strref opath)
{
    VFSCacheEnt* c = xaAlloc(sizeof(VFSCacheEnt), XA_Zero);
    c->mount       = m;
    strDup(&c->origpath, opath);
    return c;
}

static void _vfsCacheEntDestroy(stype st, stgeneric* g, uint32 flags)
{
    VFSCacheEnt* e = (VFSCacheEnt*)g->st_ptr;
    strDestroy(&e->origpath);
    xaFree(e);
}

STR_CONSTR(VFSCacheEnt, "VFSCacheEnt");
stDefine(VFSCacheEnt) { .id   = stTypeId(ptr),
                        .size = sizeof(VFSCacheEnt*),
                        .name = _SR(VFSCacheEnt),
                        .ops  = { .dtor = _vfsCacheEntDestroy } };

_Use_decl_annotations_
VFSDir* _vfsDirCreate(VFS* vfs, VFSDir* parent)
{
    VFSDir* d = xaAlloc(sizeof(VFSDir), XA_Zero);
    d->parent = parent;   // weak ref
    saInit(&d->mounts, object, 1);
    if (vfs->flags & VFS_CaseSensitive) {
        htInit(&d->subdirs, string, VFSDir, 8);
        htInit(&d->files, string, VFSCacheEnt, 8);
    } else {
        htInit(&d->subdirs, string, VFSDir, 8, HT_CaseInsensitive);
        htInit(&d->files, string, VFSCacheEnt, 8, HT_CaseInsensitive);
    }
    return d;
}

static void _vfsDirDestroy(stype st, stgeneric* g, uint32 flags)
{
    VFSDir* d = (VFSDir*)g->st_ptr;
    saDestroy(&d->mounts);
    htDestroy(&d->files);
    htDestroy(&d->subdirs);
    strDestroy(&d->name);
    xaFree(d);
}

STR_CONSTR(VFSDir, "VFSDir");
stDefine(VFSDir) { .id   = stTypeId(ptr),
                   .size = sizeof(VFSDir*),
                   .name = _SR(VFSDir),
                   .ops  = { .dtor = _vfsDirDestroy } };
