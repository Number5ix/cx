#include "cx/fs/fs_private.h"
#include "cx/fs/vfs.h"

STR_CONST(kRootPath, "/");

bool _vfsAddPlatformSpecificMounts(VFS* vfs)
{
    bool ret = vfsMountFS(vfs, kRootPath, kRootPath, VFS_CaseSensitive);

    string curdir = 0;
    fsCurDir(&curdir);
    vfsSetCurDir(vfs, curdir);
    strDestroy(&curdir);
    return ret;
}

bool _vfsIsPlatformCaseSensitive()
{
    return true;
}
