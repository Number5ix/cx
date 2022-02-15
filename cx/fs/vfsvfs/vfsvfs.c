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
#include "cx/fs/path.h"

VFSVFS *VFSVFS_create(VFS *vfs, strref rootpath)
{
    VFSVFS *ret;
    ret = objInstCreate(VFSVFS);

    ret->vfs = objAcquire(vfs);
    strDup(&ret->root, rootpath);

    if (!objInstInit(ret))
        objRelease(&ret);
    return ret;
}

uint32 VFSVFS_flags(VFSVFS *self)
{
    return 0;
}

ObjInst *VFSVFS_open(VFSVFS *self, strref path, uint32 flags)
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

int VFSVFS_stat(VFSVFS *self, strref path, FSStat *stat)
{
    string vfspath = 0;
    pathJoin(&vfspath, self->root, path);

    int ret = vfsStat(self->vfs, vfspath, stat);

    strDestroy(&vfspath);
    return ret;
}

bool VFSVFS_setTimes(VFSVFS *self, strref path, int64 modified, int64 accessed)
{
    string vfspath = 0;
    pathJoin(&vfspath, self->root, path);

    bool ret = vfsSetTimes(self->vfs, vfspath, modified, accessed);

    strDestroy(&vfspath);
    return ret;
}

bool VFSVFS_createDir(VFSVFS *self, strref path)
{
    string vfspath = 0;
    pathJoin(&vfspath, self->root, path);

    bool ret = vfsCreateDir(self->vfs, vfspath);

    strDestroy(&vfspath);
    return ret;
}

bool VFSVFS_removeDir(VFSVFS *self, strref path)
{
    string vfspath = 0;
    pathJoin(&vfspath, self->root, path);

    bool ret = vfsRemoveDir(self->vfs, vfspath);

    strDestroy(&vfspath);
    return ret;
}

bool VFSVFS_deleteFile(VFSVFS *self, strref path)
{
    string vfspath = 0;
    pathJoin(&vfspath, self->root, path);

    bool ret = vfsDelete(self->vfs, vfspath);

    strDestroy(&vfspath);
    return ret;
}

bool VFSVFS_rename(VFSVFS *self, strref oldpath, strref newpath)
{
    string vfsoldpath = 0, vfsnewpath = 0;
    pathJoin(&vfsoldpath, self->root, oldpath);
    pathJoin(&vfsnewpath, self->root, newpath);

    bool ret = vfsRename(self->vfs, vfsoldpath, vfsnewpath);

    strDestroy(&vfsoldpath);
    strDestroy(&vfsnewpath);
    return ret;
}

void VFSVFS_destroy(VFSVFS *self)
{
    // Autogen begins -----
    objRelease(&self->vfs);
    strDestroy(&self->root);
    // Autogen ends -------
}

bool VFSVFS_searchInit(VFSVFS *self, FSSearchIter *iter, strref path, strref pattern, bool stat)
{
    string vfspath = 0;

    pathJoin(&vfspath, self->root, path);
    bool ret = vfsSearchInit(iter, self->vfs, vfspath, pattern, 0, stat);
    strDestroy(&vfspath);
    return ret;
}

bool VFSVFS_searchNext(VFSVFS *self, FSSearchIter *iter)
{
    return vfsSearchNext(iter);
}

void VFSVFS_searchFinish(VFSVFS *self, FSSearchIter *iter)
{
    vfsSearchFinish(iter);
}

bool VFSVFS_getFSPath(VFSVFS *self, string *out, strref path)
{
    string vfspath = 0;
    pathJoin(&vfspath, self->root, path);

    bool ret = vfsGetFSPath(out, self->vfs, vfspath);

    strDestroy(&vfspath);
    return ret;
}

// Autogen begins -----
#include "vfsvfs.auto.inc"
// Autogen ends -------
