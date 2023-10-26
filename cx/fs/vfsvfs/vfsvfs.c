// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "vfsvfs.h"
// ==================== Auto-generated section ends ======================
#include "vfsvfsfile.h"
#include "cx/fs/path.h"

_objfactory VFSVFS *VFSVFS_create(VFS *vfs, strref rootpath)
{
    VFSVFS *ret;
    ret = objInstCreate(VFSVFS);

    ret->vfs = objAcquire(vfs);
    strDup(&ret->root, rootpath);

    if (!objInstInit(ret))
        objRelease(&ret);
    return ret;
}

flags_t VFSVFS_flags(_Inout_ VFSVFS *self)
{
    return 0;
}

ObjInst *VFSVFS_open(_Inout_ VFSVFS *self, strref path, flags_t flags)
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

int VFSVFS_stat(_Inout_ VFSVFS *self, strref path, FSStat *stat)
{
    string vfspath = 0;
    pathJoin(&vfspath, self->root, path);

    int ret = vfsStat(self->vfs, vfspath, stat);

    strDestroy(&vfspath);
    return ret;
}

bool VFSVFS_setTimes(_Inout_ VFSVFS *self, strref path, int64 modified, int64 accessed)
{
    string vfspath = 0;
    pathJoin(&vfspath, self->root, path);

    bool ret = vfsSetTimes(self->vfs, vfspath, modified, accessed);

    strDestroy(&vfspath);
    return ret;
}

bool VFSVFS_createDir(_Inout_ VFSVFS *self, strref path)
{
    string vfspath = 0;
    pathJoin(&vfspath, self->root, path);

    bool ret = vfsCreateDir(self->vfs, vfspath);

    strDestroy(&vfspath);
    return ret;
}

bool VFSVFS_removeDir(_Inout_ VFSVFS *self, strref path)
{
    string vfspath = 0;
    pathJoin(&vfspath, self->root, path);

    bool ret = vfsRemoveDir(self->vfs, vfspath);

    strDestroy(&vfspath);
    return ret;
}

bool VFSVFS_deleteFile(_Inout_ VFSVFS *self, strref path)
{
    string vfspath = 0;
    pathJoin(&vfspath, self->root, path);

    bool ret = vfsDelete(self->vfs, vfspath);

    strDestroy(&vfspath);
    return ret;
}

bool VFSVFS_rename(_Inout_ VFSVFS *self, strref oldpath, strref newpath)
{
    string vfsoldpath = 0, vfsnewpath = 0;
    pathJoin(&vfsoldpath, self->root, oldpath);
    pathJoin(&vfsnewpath, self->root, newpath);

    bool ret = vfsRename(self->vfs, vfsoldpath, vfsnewpath);

    strDestroy(&vfsoldpath);
    strDestroy(&vfsnewpath);
    return ret;
}

void VFSVFS_destroy(_Inout_ VFSVFS *self)
{
    // Autogen begins -----
    objRelease(&self->vfs);
    strDestroy(&self->root);
    // Autogen ends -------
}

bool VFSVFS_searchInit(_Inout_ VFSVFS *self, FSSearchIter *iter, strref path, strref pattern, bool stat)
{
    string vfspath = 0;

    pathJoin(&vfspath, self->root, path);
    bool ret = vfsSearchInit(iter, self->vfs, vfspath, pattern, 0, stat);
    strDestroy(&vfspath);
    return ret;
}

bool VFSVFS_searchNext(_Inout_ VFSVFS *self, FSSearchIter *iter)
{
    return vfsSearchNext(iter);
}

void VFSVFS_searchFinish(_Inout_ VFSVFS *self, FSSearchIter *iter)
{
    vfsSearchFinish(iter);
}

bool VFSVFS_getFSPath(_Inout_ VFSVFS *self, string *out, strref path)
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
