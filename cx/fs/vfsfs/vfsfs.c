// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/core/objstdif.h>
#include <cx/debug/assert.h>
#include <cx/container.h>
#include <cx/string.h>
#include "vfsfs.h"
// ==================== Auto-generated section ends ======================
#include "cx/platform/base.h"
#include "cx/fs/path.h"
#include "cx/fs/vfs.h"
#include "vfsfsfile.h"

VFSFS *VFSFS_create(strref rootpath)
{
    VFSFS *ret;

    if (!pathIsAbsolute(rootpath))
        return NULL;

    ret = objInstCreate(VFSFS);

    strDup(&ret->root, rootpath);
    pathNormalize(&ret->root);

    if (!objInstInit(ret))
        objRelease(&ret);
    return ret;
}

flags_t VFSFS_flags(VFSFS *self)
{
#ifdef _PLATFORM_UNIX
    return VFS_CaseSensitive;
#else
    // No special flags here
    return 0;
#endif
}

ObjInst *VFSFS_open(VFSFS *self, strref path, flags_t flags)
{
    string fspath = 0;
    pathJoin(&fspath, self->root, path);

    FSFile *file = fsOpen(fspath, flags);
    strDestroy(&fspath);
    if (!file)
        return NULL;

    VFSFSFile *fileprov = vfsfsfileCreate(file);
    return objInstBase(fileprov);
}

int VFSFS_stat(VFSFS *self, strref path, FSStat *stat)
{
    string fspath = 0;
    pathJoin(&fspath, self->root, path);

    int ret = fsStat(fspath, stat);

    strDestroy(&fspath);
    return ret;
}

bool VFSFS_setTimes(VFSFS *self, strref path, int64 modified, int64 accessed)
{
    string fspath = 0;
    pathJoin(&fspath, self->root, path);

    bool ret = fsSetTimes(fspath, modified, accessed);

    strDestroy(&fspath);
    return ret;

}

bool VFSFS_createDir(VFSFS *self, strref path)
{
    string fspath = 0;
    pathJoin(&fspath, self->root, path);

    bool ret = fsCreateDir(fspath);

    strDestroy(&fspath);
    return ret;
}

bool VFSFS_removeDir(VFSFS *self, strref path)
{
    string fspath = 0;
    pathJoin(&fspath, self->root, path);

    bool ret = fsRemoveDir(fspath);

    strDestroy(&fspath);
    return ret;
}

bool VFSFS_deleteFile(VFSFS *self, strref path)
{
    string fspath = 0;
    pathJoin(&fspath, self->root, path);

    bool ret = fsDelete(fspath);

    strDestroy(&fspath);
    return ret;
}

bool VFSFS_rename(VFSFS *self, strref oldpath, strref newpath)
{
    string fsoldpath = 0, fsnewpath = 0;
    pathJoin(&fsoldpath, self->root, oldpath);
    pathJoin(&fsnewpath, self->root, newpath);

    bool ret = fsRename(fsoldpath, fsnewpath);

    strDestroy(&fsoldpath);
    strDestroy(&fsnewpath);
    return ret;
}

bool VFSFS_searchInit(VFSFS *self, FSSearchIter *iter, strref path, strref pattern, bool stat)
{
    string fspath = 0;

    pathJoin(&fspath, self->root, path);
    bool ret = fsSearchInit(iter, fspath, pattern, stat);
    strDestroy(&fspath);
    return ret;
}

bool VFSFS_searchNext(VFSFS *self, FSSearchIter *iter)
{
    return fsSearchNext(iter);
}

void VFSFS_searchFinish(VFSFS *self, FSSearchIter *iter)
{
    fsSearchFinish(iter);
}

bool VFSFS_getFSPath(VFSFS *self, string *out, strref path)
{
    pathJoin(out, self->root, path);
    return true;
}

void VFSFS_destroy(VFSFS *self)
{
    // Autogen begins -----
    strDestroy(&self->root);
    // Autogen ends -------
}

// Autogen begins -----
#include "vfsfs.auto.inc"
// Autogen ends -------
