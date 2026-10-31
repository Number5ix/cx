#include "cx/fs/vfs_private.h"

#include <unistd.h>

STR_CONST(kRootPath, "/");

_Use_decl_annotations_
bool vfsMountPlatformFS(VFS* vfs)
{
    flags_t flags = VFS_MountNoListings;
    if (_vfsIsPlatformCaseSensitive())
        flags |= VFS_CaseSensitive;
    bool ret = vfsMountFS(vfs, kRootPath, kRootPath, flags);

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
#if defined(_PLATFORM_MACOS)
    // APFS is case-insensitive by default, but can be formatted case-sensitive.
    return pathconf("/", _PC_CASE_SENSITIVE) == 1;
#else
    return true;
#endif
}
