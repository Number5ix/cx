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

stDefine(VFSCacheEnt) { .id   = stTypeId(ptr),
                        .size = sizeof(VFSCacheEnt*),
                        .ops  = { .dtor = _vfsCacheEntDestroy } };

static void _vfsCandDestroy(stype st, stgeneric* g, uint32 flags)
{
    VFSCand* c = (VFSCand*)g->st_opaque;
    objRelease(&c->mount);
    strDestroy(&c->mountpath);
    strDestroy(&c->relpath);
    saDestroy(&c->relcomp);
    strDestroy(&c->lname);
    strDestroy(&c->ldir);
}

// Snapshot entries are only ever moved into an array and destroyed with it, never copied, so
// there is no copy op here on purpose -- push them with saPushC.
stDefine(VFSCand) { .id    = stTypeId(opaque),
                    .size  = sizeof(VFSCand),
                    .flags = stFlag(PassPtr),
                    .ops   = { .dtor = _vfsCandDestroy } };

static void _vfsPendEntDestroy(stype st, stgeneric* g, uint32 flags)
{
    VFSPendEnt* pe = (VFSPendEnt*)g->st_opaque;
    objRelease(&pe->mount);
    strDestroy(&pe->dirpath);
    strDestroy(&pe->name);
    strDestroy(&pe->origpath);
}

stDefine(VFSPendEnt) { .id    = stTypeId(opaque),
                       .size  = sizeof(VFSPendEnt),
                       .flags = stFlag(PassPtr),
                       .ops   = { .dtor = _vfsPendEntDestroy } };

// Plain data, so the default copy is right.
stDefine(VFSListEnt) { .id    = stTypeId(opaque),
                       .size  = sizeof(VFSListEnt),
                       .flags = stFlag(PassPtr) };

_Use_decl_annotations_
void _vfsListingDestroy(VFSListing* l)
{
    objRelease(&l->mount);
    strDestroy(&l->relpath);
    htDestroy(&l->ents);
}

static void _vfsListingDtor(stype st, stgeneric* g, uint32 flags)
{
    _vfsListingDestroy((VFSListing*)g->st_opaque);
}

// Moved into place with saPushC, never copied.
stDefine(VFSListing) { .id    = stTypeId(opaque),
                       .size  = sizeof(VFSListing),
                       .flags = stFlag(PassPtr),
                       .ops   = { .dtor = _vfsListingDtor } };

static void _vfsPendListDestroy(stype st, stgeneric* g, uint32 flags)
{
    VFSPendList* pl = (VFSPendList*)g->st_opaque;
    strDestroy(&pl->dirpath);
    _vfsListingDestroy(&pl->l);
}

stDefine(VFSPendList) { .id    = stTypeId(opaque),
                        .size  = sizeof(VFSPendList),
                        .flags = stFlag(PassPtr),
                        .ops   = { .dtor = _vfsPendListDestroy } };

_Use_decl_annotations_
void _vfsListingInit(VFSListing* l, VFS* vfs, VFSMount* m, strref relpath)
{
    memset(l, 0, sizeof(VFSListing));
    l->mount = objAcquire(m);
    strDup(&l->relpath, relpath);
    if (vfs->flags & VFS_CaseSensitive)
        htInit(&l->ents, string, VFSListEnt, 16, HT_Grow(MaxSpeed));
    else
        htInit(&l->ents, string, VFSListEnt, 16, HT_CaseInsensitive | HT_Grow(MaxSpeed));
}

_Use_decl_annotations_
VFSDir* _vfsDirCreate(VFS* vfs, VFSDir* parent)
{
    VFSDir* d = xaAlloc(sizeof(VFSDir), XA_Zero);
    d->parent = parent;   // weak ref
    d->vfs    = vfs;      // weak ref
    atomicFetchAdd(uint32, &vfs->dcache.dircount, 1, Relaxed);
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
    atomicFetchSub(uint32, &d->vfs->dcache.dircount, 1, Relaxed);
    saDestroy(&d->mounts);
    saDestroy(&d->listings);
    htDestroy(&d->files);
    htDestroy(&d->subdirs);
    strDestroy(&d->name);
    xaFree(d);
}

stDefine(VFSDir) { .id   = stTypeId(ptr),
                   .size = sizeof(VFSDir*),
                   .ops  = { .dtor = _vfsDirDestroy } };
