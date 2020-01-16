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
#include "vfsfsdirsearch.h"

VFSFS *VFSFS_create(string rootpath)
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

uint32 VFSFS_flags(VFSFS *self)
{
#ifdef _PLATFORM_UNIX
    return VFS_CaseSensitive;
#else
    // No special flags here
    return 0;
#endif
}

ObjInst *VFSFS_open(VFSFS *self, string path, int flags)
{
    string fspath = 0;
    pathJoin(&fspath, self->root, path);

    FSFile *file = _fsOpen(fspath, flags);
    strDestroy(&fspath);
    if (!file)
        return NULL;

    VFSFSFile *fileprov = vfsfsfileCreate(file);
    return objInstBase(fileprov);
}

int VFSFS_stat(VFSFS *self, string path, FSStat *stat)
{
    string fspath = 0;
    pathJoin(&fspath, self->root, path);

    int ret = fsStat(fspath, stat);

    strDestroy(&fspath);
    return ret;
}

bool VFSFS_createDir(VFSFS *self, string path)
{
    string fspath = 0;
    pathJoin(&fspath, self->root, path);

    bool ret = fsCreateDir(fspath);

    strDestroy(&fspath);
    return ret;
}

bool VFSFS_removeDir(VFSFS *self, string path)
{
    string fspath = 0;
    pathJoin(&fspath, self->root, path);

    bool ret = fsRemoveDir(fspath);

    strDestroy(&fspath);
    return ret;
}

bool VFSFS_deleteFile(VFSFS *self, string path)
{
    string fspath = 0;
    pathJoin(&fspath, self->root, path);

    bool ret = fsDelete(fspath);

    strDestroy(&fspath);
    return ret;
}

bool VFSFS_rename(VFSFS *self, string oldpath, string newpath)
{
    string fsoldpath = 0, fsnewpath = 0;
    pathJoin(&fsoldpath, self->root, oldpath);
    pathJoin(&fsnewpath, self->root, newpath);

    bool ret = fsRename(fsoldpath, fsnewpath);

    strDestroy(&fsoldpath);
    strDestroy(&fsnewpath);
    return ret;
}

ObjInst *VFSFS_searchDir(VFSFS *self, string path, string pattern, bool stat)
{
    string fspath = 0;
    pathJoin(&fspath, self->root, path);

    FSDirSearch *ds = fsSearchDir(fspath, pattern, stat);
    strDestroy(&fspath);
    if (!ds)
        return NULL;

    VFSFSDirSearch *dsprov = vfsfsdirsearchCreate(ds);
    return objInstBase(dsprov);
}

bool VFSFS_getFSPath(VFSFS *self, string *out, string path)
{
    pathJoin(out, self->root, path);
    return true;
}

void VFSFS_destroy(VFSFS *self)
{
// ==================== Auto-generated section begins ====================
    strDestroy(&self->root);
// ==================== Auto-generated section ends ======================
}

// ==================== Auto-generated section begins ====================
VFSFS_ClassIf VFSFS_ClassIf_tmpl = {
    ._size = sizeof(VFSFS_ClassIf),
};

static VFSProvider _impl_VFSFS_VFSProvider = {
    ._size = sizeof(VFSProvider),
    ._implements = (ObjIface*)&VFSProvider_tmpl,
    .flags = (uint32 (*)(void*))VFSFS_flags,
    .open = (ObjInst *(*)(void*, string, int))VFSFS_open,
    .stat = (int (*)(void*, string, FSStat*))VFSFS_stat,
    .createDir = (bool (*)(void*, string))VFSFS_createDir,
    .removeDir = (bool (*)(void*, string))VFSFS_removeDir,
    .deleteFile = (bool (*)(void*, string))VFSFS_deleteFile,
    .rename = (bool (*)(void*, string, string))VFSFS_rename,
    .searchDir = (ObjInst *(*)(void*, string, string, bool))VFSFS_searchDir,
};

static VFSFS_ClassIf _impl_VFSFS_VFSFS_ClassIf = {
    ._size = sizeof(VFSFS_ClassIf),
    ._implements = (ObjIface*)&VFSFS_ClassIf_tmpl,
    .getFSPath = (bool (*)(void*, string*, string))VFSFS_getFSPath,
    .flags = (uint32 (*)(void*))VFSFS_flags,
    .open = (ObjInst *(*)(void*, string, int))VFSFS_open,
    .stat = (int (*)(void*, string, FSStat*))VFSFS_stat,
    .createDir = (bool (*)(void*, string))VFSFS_createDir,
    .removeDir = (bool (*)(void*, string))VFSFS_removeDir,
    .deleteFile = (bool (*)(void*, string))VFSFS_deleteFile,
    .rename = (bool (*)(void*, string, string))VFSFS_rename,
    .searchDir = (ObjInst *(*)(void*, string, string, bool))VFSFS_searchDir,
};

static ObjIface *_ifimpl_VFSFS[] = {
    (ObjIface*)&_impl_VFSFS_VFSProvider,
    (ObjIface*)&_impl_VFSFS_VFSFS_ClassIf,
    NULL
};

ObjClassInfo VFSFS_clsinfo = {
    .instsize = sizeof(VFSFS),
    .classif = (ObjIface*)&VFSFS_ClassIf_tmpl,
    .destroy = (void(*)(void*))VFSFS_destroy,
    .ifimpl = _ifimpl_VFSFS,
};

// ==================== Auto-generated section ends ======================
