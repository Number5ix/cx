#include "cx/fs/vfs_private.h"
#include "cx/platform/win.h"

STR_CONST(kRootPath, "/");
STR_CONST(kUNCPath, "unc:/");

_Use_decl_annotations_
bool vfsMountPlatformFS(VFS* vfs)
{
    DWORD ldrives = GetLogicalDrives();
    bool ret      = true;
    string drive  = 0;
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
    ret &= vfsMountFS(vfs, kUNCPath, kUNCPath);

    // drives mapped or plugged in later are mounted on first use
    vfs->flags |= VFS_AutoMountDrives;

    string curdir = 0;
    fsCurDir(&curdir);
    // mount current drive as root
    strSubStr(&drive, _fsCurDir, 0, 3);
    vfsMountFS(vfs, kRootPath, drive);
    vfsSetCurDir(vfs, curdir);
    strDestroy(&curdir);
    strDestroy(&drive);

    return ret;
}

_Use_decl_annotations_
bool _vfsPlatformMountNamespace(VFS* vfs, strref ns)
{
    char letter = strGetChar(ns, 0);
    if (letter >= 'A' && letter <= 'Z')
        letter += 'a' - 'A';
    if (strLen(ns) != 1 || letter < 'a' || letter > 'z')
        return false;

    // Asks about this one root rather than the GetLogicalDrives() mask, which can lag behind a
    // drive mapped while the process is running.
    wchar_t root[4] = { (wchar_t)letter, L':', L'\\', 0 };
    if (GetDriveTypeW(root) <= DRIVE_NO_ROOT_DIR)
        return false;

    char drivestr[4] = { letter, ':', '/', 0 };
    string drive     = 0;
    strCopy(&drive, (string)drivestr);
    bool ret = vfsMountFS(vfs, drive, drive, VFS_MountNewNS);
    strDestroy(&drive);
    return ret;
}

bool _vfsIsPlatformCaseSensitive()
{
    return false;
}
