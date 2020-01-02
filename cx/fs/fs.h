#pragma once

#include <cx/cx.h>

// ---------- Platform-specific filesystem operations ----------

CX_C_BEGIN

// Convert a platform-specific path into a CX path
void pathFromPlatform(string *out, string platformpath);
// Convert a CX normalized path into a platform-specific path
void pathToPlatform(string *out, string path);

// Get / set current directory
void fsCurDir(string *out);
bool fsSetCurDir(string cur);

// Make a relative path into an absolute one, using the OS's current directory
bool pathMakeAbsolute(string *out, string path);

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
    uint64 created;
    uint64 modified;
    uint64 accessed;
} FSStat;

int fsStat(string path, FSStat *stat);

_meta_inline bool fsExist(string path)
{
    return fsStat(path, NULL) != FS_Nonexistent;
}
_meta_inline bool fsIsDir(string path)
{
    return fsStat(path, NULL) == FS_Directory;
}
_meta_inline bool fsIsFile(string path)
{
    return fsStat(path, NULL) == FS_File;
}

bool fsCreateDir(string path);
bool fsCreateAll(string path);
bool fsRemoveDir(string path);
bool fsDelete(string path);
bool fsRename(string from, string to);

typedef struct FSDirSearch FSDirSearch;
typedef struct FSDirEnt {
    string name;
    int type;
    FSStat stat;
} FSDirEnt;

FSDirSearch *fsSearchDir(string path, string pattern, bool stat);
FSDirEnt *fsSearchNext(FSDirSearch *search);
void fsSearchClose(FSDirSearch *search);

CX_C_END
