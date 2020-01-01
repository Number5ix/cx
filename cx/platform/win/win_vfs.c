#include "cx/fs/fs_private.h"
#include "cx/fs/vfs.h"

bool _vfsAddPlatformSpecificMounts(VFS *vfs)
{
    DWORD ldrives = GetLogicalDrives();
    bool ret = true;
    string drive = 0;
    char drivestr[4];

    drivestr[1] = ':';
    drivestr[2] = '/';
    drivestr[3] = 0;
    for (char dletter = 'a'; dletter <= 'z'; dletter++) {
        if (ldrives & 1 << (dletter - 'a')) {
            drivestr[0] = dletter;
            strCopy(&drive, drivestr);
            ret &= vfsMountFS(vfs, drive, drive, 0);
        }
    }

    string curdir = 0;
    fsCurDir(&curdir);
    vfsSetCurDir(vfs, curdir);
    strDestroy(&curdir);

    return ret;
}

bool _vfsIsPlatformCaseSensitive()
{
    return false;
}
