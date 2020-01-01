// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/core/objstdif.h>
#include <cx/debug/assert.h>
#include <cx/container.h>
#include <cx/string.h>
#include "vfsvfs.h"
// ==================== Auto-generated section ends ======================
#include "vfsvfsfile.h"
#include "vfsvfsdirsearch.h"
#include "cx/fs/path.h"

VFSVFS *VFSVFS_create(VFS *vfs, string rootpath)
{
    VFSVFS *ret;
    ret = objInstCreate(VFSVFS);

    ret->vfs = objAcquire(vfs);
    strDup(&ret->root, rootpath);

    if (!objInstInit(ret))
        objRelease(ret);
    return ret;
}

uint32 VFSVFS_flags(VFSVFS *self)
{
    return 0;
}

ObjInst *VFSVFS_open(VFSVFS *self, string path, int flags)
{
    string vfspath = 0;
    pathJoin(&vfspath, self->root, path);

    VFSFile *file = vfsOpen(self->vfs, vfspath, flags);
    strDestroy(&vfspath);
    if (!file)
        return NULL;

    VFSVFSFile *fileprov = vfsvfsfileCreate(file);
    return objInstBase(fileprov);
}

int VFSVFS_stat(VFSVFS *self, string path, FSStat *stat)
{
    string vfspath = 0;
    pathJoin(&vfspath, self->root, path);

    int ret = vfsStat(self->vfs, vfspath, stat);

    strDestroy(&vfspath);
    return ret;
}

bool VFSVFS_createDir(VFSVFS *self, string path)
{
    string vfspath = 0;
    pathJoin(&vfspath, self->root, path);

    bool ret = vfsCreateDir(self->vfs, vfspath);

    strDestroy(&vfspath);
    return ret;
}

bool VFSVFS_removeDir(VFSVFS *self, string path)
{
    string vfspath = 0;
    pathJoin(&vfspath, self->root, path);

    bool ret = vfsRemoveDir(self->vfs, vfspath);

    strDestroy(&vfspath);
    return ret;
}

bool VFSVFS_deleteFile(VFSVFS *self, string path)
{
    string vfspath = 0;
    pathJoin(&vfspath, self->root, path);

    bool ret = vfsDelete(self->vfs, vfspath);

    strDestroy(&vfspath);
    return ret;
}

bool VFSVFS_rename(VFSVFS *self, string oldpath, string newpath)
{
    string vfsoldpath = 0, vfsnewpath;
    pathJoin(&vfsoldpath, self->root, oldpath);
    pathJoin(&vfsnewpath, self->root, newpath);

    bool ret = vfsRename(self->vfs, vfsoldpath, vfsnewpath);

    strDestroy(&vfsoldpath);
    strDestroy(&vfsnewpath);
    return ret;
}

void VFSVFS_destroy(VFSVFS *self)
{
// ==================== Auto-generated section begins ====================
    objRelease(self->vfs);
    strDestroy(&self->root);
// ==================== Auto-generated section ends ======================
}

ObjInst *VFSVFS_searchDir(VFSVFS *self, string path, string pattern, bool stat)
{
    string vfspath = 0;
    pathJoin(&vfspath, self->root, path);

    VFSDirSearch *ds = vfsSearchDir(self->vfs, vfspath, pattern, 0, stat);
    strDestroy(&vfspath);
    if (!ds)
        return NULL;

    VFSVFSDirSearch *dsprov = vfsvfsdirsearchCreate(ds);
    return objInstBase(dsprov);
}

// ==================== Auto-generated section begins ====================
VFSVFS_ClassIf VFSVFS_ClassIf_tmpl = {
    ._size = sizeof(VFSVFS_ClassIf),
};

static VFSProvider _impl_VFSVFS_VFSProvider = {
    ._size = sizeof(VFSProvider),
    ._implements = (ObjIface*)&VFSProvider_tmpl,
    .flags = (uint32 (*)(void*))VFSVFS_flags,
    .open = (ObjInst *(*)(void*, string, int))VFSVFS_open,
    .stat = (int (*)(void*, string, FSStat*))VFSVFS_stat,
    .createDir = (bool (*)(void*, string))VFSVFS_createDir,
    .removeDir = (bool (*)(void*, string))VFSVFS_removeDir,
    .deleteFile = (bool (*)(void*, string))VFSVFS_deleteFile,
    .rename = (bool (*)(void*, string, string))VFSVFS_rename,
    .searchDir = (ObjInst *(*)(void*, string, string, bool))VFSVFS_searchDir,
};

static VFSVFS_ClassIf _impl_VFSVFS_VFSVFS_ClassIf = {
    ._size = sizeof(VFSVFS_ClassIf),
    ._implements = (ObjIface*)&VFSVFS_ClassIf_tmpl,
    .flags = (uint32 (*)(void*))VFSVFS_flags,
    .open = (ObjInst *(*)(void*, string, int))VFSVFS_open,
    .stat = (int (*)(void*, string, FSStat*))VFSVFS_stat,
    .createDir = (bool (*)(void*, string))VFSVFS_createDir,
    .removeDir = (bool (*)(void*, string))VFSVFS_removeDir,
    .deleteFile = (bool (*)(void*, string))VFSVFS_deleteFile,
    .rename = (bool (*)(void*, string, string))VFSVFS_rename,
    .searchDir = (ObjInst *(*)(void*, string, string, bool))VFSVFS_searchDir,
};

static ObjIface *_ifimpl_VFSVFS[] = {
    (ObjIface*)&_impl_VFSVFS_VFSProvider,
    (ObjIface*)&_impl_VFSVFS_VFSVFS_ClassIf,
    NULL
};

ObjClassInfo VFSVFS_clsinfo = {
    .instsize = sizeof(VFSVFS),
    .classif = (ObjIface*)&VFSVFS_ClassIf_tmpl,
    .destroy = (void(*)(void*))VFSVFS_destroy,
    .ifimpl = _ifimpl_VFSVFS,
};

// ==================== Auto-generated section ends ======================
