#include "cx/fs/vfs_private.h"

STR_CONST(kRootPath, "/");

_Use_decl_annotations_
bool vfsMountPlatformFS(VFS* vfs)
{
    bool ret = vfsMountFS(vfs, kRootPath, kRootPath, VFS_CaseSensitive);

    string curdir = 0;
    fsCurDir(&curdir);
    vfsSetCurDir(vfs, curdir);
    strDestroy(&curdir);
    return ret;
}

_Use_decl_annotations_
bool _vfsPlatformMountNamespace(VFS* vfs, strref ns)
{
    // a single root; nothing appears later
    return false;
}

bool _vfsIsPlatformCaseSensitive()
{
    return true;
}
