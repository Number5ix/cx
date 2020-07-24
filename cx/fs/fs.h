#pragma once

#include <cx/cx.h>

// ---------- Platform-specific filesystem operations ----------

CX_C_BEGIN

// Convert a platform-specific path into a CX path
void pathFromPlatform(string *out, strref platformpath);
// Convert a CX normalized path into a platform-specific path
void pathToPlatform(string *out, strref path);

// Get / set current directory
void fsCurDir(string *out);
bool fsSetCurDir(strref cur);

// Make a relative path into an absolute one, using the OS's current directory
bool pathMakeAbsolute(string *out, strref path);

// Get running executable name
void fsExe(string *out);
void fsExeDir(string *out);

enum FSPathStat {
    FS_Nonexistent,
    FS_Directory,
    FS_File
};

typedef struct FSStat {
    uint64 size;
    int64 created;
    int64 modified;
    int64 accessed;
} FSStat;

int fsStat(strref path, FSStat *stat);

_meta_inline bool fsExist(strref path)
{
    return fsStat(path, NULL) != FS_Nonexistent;
}
_meta_inline bool fsIsDir(strref path)
{
    return fsStat(path, NULL) == FS_Directory;
}
_meta_inline bool fsIsFile(strref path)
{
    return fsStat(path, NULL) == FS_File;
}

bool fsSetTimes(strref path, int64 modified, int64 accessed);

bool fsCreateDir(strref path);
bool fsCreateAll(strref path);
bool fsRemoveDir(strref path);
bool fsDelete(strref path);
bool fsRename(strref from, strref to);

typedef struct FSSearch FSSearch;
typedef struct FSSearchIter {
    string name;
    int type;
    FSStat stat;
    // private
    void *_search;
} FSSearchIter;

bool fsSearchInit(FSSearchIter *iter, strref path, strref pattern, bool stat);
bool fsSearchNext(FSSearchIter *iter);
void fsSearchFinish(FSSearchIter *iter);
_meta_inline bool fsSearchValid(FSSearchIter *iter) { return iter->name; }

CX_C_END
