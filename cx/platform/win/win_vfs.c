#include "cx/fs/fs_private.h"
#include "cx/fs/vfs.h"

bool _vfsAddPlatformSpecificMounts(VFS *vfs)
{
    DWORD ldrives = GetLogicalDrives();
    bool ret = true;
    string(drive);
    char drivestr[4];

    drivestr[1] = ':';
    drivestr[2] = '/';
    drivestr[3] = 0;
    for (char dletter = 'a'; dletter <= 'z'; dletter++) {
        if (ldrives & 1 << (dletter - 'a')) {
            drivestr[0] = dletter;
            strCopy(&drive, (string)drivestr);
            ret &= vfsMountFS(vfs, drive, drive);
        }
    }

    string(curdir);
    fsCurDir(&curdir);
    // mount current drive as root
    strSubStr(&drive, _fsCurDir, 0, 3);
    vfsMountFS(vfs, _S"/", drive);
    vfsSetCurDir(vfs, curdir);
    strDestroy(&curdir);
    strDestroy(&drive);

    return ret;
}

bool _vfsIsPlatformCaseSensitive()
{
    return false;
}
