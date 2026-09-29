// WebAssembly has no way to be told about changes to the native filesystem.

#include "cx/fs/fswatch_private.h"

bool _fsWatchPlatformInit(void)
{
    return false;
}

_Use_decl_annotations_
bool _fsWatchPlatformAddDir(FSWDir* d)
{
    return false;
}

_Use_decl_annotations_
bool _fsWatchPlatformUpdateDir(FSWDir* d)
{
    return false;
}

_Use_decl_annotations_
void _fsWatchPlatformRemoveDir(FSWDir* d)
{
}

_Use_decl_annotations_
bool _fsWatchPlatformList(strref path, FSWListCB cb, void* ctx)
{
    return false;
}

void _fsWatchPlatformWait(int64 timeout)
{
}

void _fsWatchPlatformWake(void)
{
}
