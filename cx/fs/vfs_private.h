#pragma once

#include "vfs.h"
#include "fs_private.h"
#include "cx/container.h"
#include "cx/string.h"

typedef struct VFSFile {
    VFS *vfs;

    ObjInst *fileprov;      // VFSFileProvider
    VFSFileProvider *fileprovif;

    // for copy-on-write files
    ObjInst *cowprov;       // VFSProvider
    string cowpath;         // absolute path to COW file (for cache invalidation)
    string cowrpath;        // relative path for COW file for provider
} VFSFile;

typedef struct VFSDirEnt {
    string name;
    int type;
    FSStat stat;
} VFSDirEnt;
saDeclare(VFSDirEnt);

typedef struct VFSSearch {
    VFS *vfs;

    sa_VFSDirEnt ents;
    int32 idx;
} VFSSearch;

// object-like structures for VFS
// these use custom type ops instead of the object framework so that
// they can be tightly packed into arrays/hashtables

typedef struct VFSMount VFSMount;
typedef struct VFSCacheEnt {
    VFSMount *mount;        // which VFS mount this file belongs to
    string origpath;        // original path (relative to provider)
} VFSCacheEnt;
VFSCacheEnt *_vfsCacheEntCreate(VFSMount *m, strref opath);
extern STypeOps VFSCacheEnt_ops;

typedef struct VFSDir VFSDir;
typedef struct VFSDir {
    string name;
    VFSDir *parent;
    sa_VFSMount mounts;     // VFS providers mounted in this directory

    hashtable subdirs;      // hashtable of string/VFSDir*

    // CACHE
    hashtable files;        // hashtable of string/VFSCacheEnt*
    uint64 touched;         // timestamp of last use
    bool cache;             // only exists to cache directory entries, can be discarded
} VFSDir;
VFSDir *_vfsDirCreate(VFS *vfs, VFSDir *parent);
extern STypeOps VFSDir_ops;

// gets (and creates) path in VFS cache
// must be called with vfslock read (or write) lock held!
VFSDir *_vfsGetDir(VFS *vfs, strref path, bool isfile, bool cache, bool writelockheld);
// gets a file from VFS cache if it exists
// must be called with vfslock read (or write) lock held!
VFSCacheEnt *_vfsGetFile(VFS *vfs, strref path, bool writelockheld);
// finds a suitable provider for a particular file
enum VFS_FIND_PROVIDER_ENUM {
    VFS_FindWriteFile = 0x0100,
    VFS_FindCreate =    0x0200,
    VFS_FindDelete =    0x0400,
    VFS_FindCache =     0x1000,
};
VFSMount *_vfsFindMount(VFS *vfs, string *rpath, strref path, VFSMount **cowmount, string *cowrpath, flags_t flags);
void _vfsInvalidateCache(VFS *vfs, strref path);
void _vfsInvalidateRecursive(VFS *vfs, VFSDir *dir, bool havelock);
void _vfsAbsPath(VFS *vfs, string *out, strref path);
int _vfsFindCIHelper(VFS *vfs, VFSDir *vdir, string *out, sa_string components, VFSMount *mount, VFSProvider *provif);

bool _vfsAddPlatformSpecificMounts(VFS *vfs);
bool _vfsIsPlatformCaseSensitive();
