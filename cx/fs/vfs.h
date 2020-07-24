#pragma once

#include <cx/fs/file.h>
#include <cx/fs/fs.h>
#include <cx/fs/vfsprovider.h>
#include <cx/fs/vfsobj.h>

typedef struct VFS VFS;
typedef struct VFSFile VFSFile;

CX_C_BEGIN

// vfsCreate() is in vfsobj.h
void vfsDestroy(VFS *vfs);

bool vfsUnmount(VFS *vfs, strref path);

bool _vfsMountProvider(VFS *vfs, ObjInst *provider, strref path, uint32 flags);
#define vfsMountProvider(vfs, provider, path, ...) _vfsMountProvider(vfs, objInstBase(provider), path, func_flags(VFS, __VA_ARGS__))

// Mounts the built-in OS filesystem provider to the given VFS
bool _vfsMountFS(VFS *vfs, strref path, strref fsroot, uint32 flags);
#define vfsMountFS(vfs, path, fsroot, ...) _vfsMountFS(vfs, path, fsroot, func_flags(VFS, __VA_ARGS__))

// Mounts one VFS under another (loopback mode)
bool _vfsMountVFS(VFS *vfs, strref path, VFS *vfs2, strref vfs2root, uint32 flags);
#define vfsMountVFS(vfs, path, vfs2, vfs2root, ...) _vfsMountVFS(vfs, path, vfs2, vfs2root, func_flags(VFS, __VA_ARGS__))

// Get / set current directory
void vfsCurDir(VFS *vfs, string *out);
bool vfsSetCurDir(VFS *vfs, strref cur);

int vfsStat(VFS *vfs, strref path, FSStat *stat);

_meta_inline bool vfsExist(VFS *vfs, strref path)
{
    return vfsStat(vfs, path, NULL) != FS_Nonexistent;
}
_meta_inline bool vfsIsDir(VFS *vfs, strref path)
{
    return vfsStat(vfs, path, NULL) == FS_Directory;
}
_meta_inline bool vfsIsFile(VFS *vfs, strref path)
{
    return vfsStat(vfs, path, NULL) == FS_File;
}

bool vfsSetTimes(VFS *vfs, strref path, int64 modified, int64 accessed);

bool vfsCreateDir(VFS *vfs, strref path);
bool vfsCreateAll(VFS *vfs, strref path);
bool vfsRemoveDir(VFS *vfs, strref path);
bool vfsDelete(VFS *vfs, strref path);
bool vfsCopy(VFS *vfs, strref from, strref to);
bool vfsRename(VFS *vfs, strref from, strref to);

// Special case to get the actual underlying path from VFSFS, works *only* if the path
// in question is backed by a VFSFS provider.
bool vfsGetFSPath(string *out, VFS *vfs, strref path);

typedef struct FSSearchIter FSSearchIter;
bool vfsSearchInit(FSSearchIter *iter, VFS *vfs, strref path, strref pattern, int typefilter, bool stat);
bool vfsSearchNext(FSSearchIter *iter);
void vfsSearchFinish(FSSearchIter *iter);
_meta_inline bool vfsSearchValid(FSSearchIter *iter) { return iter->_search; }

VFSFile *_vfsOpen(VFS *vfs, strref path, int flags);
#define vfsOpen(vfs, path, ...) _vfsOpen(vfs, path, func_flags(FS, __VA_ARGS__))
bool vfsClose(VFSFile *file);
bool vfsRead(VFSFile *file, void *buf, size_t sz, size_t *bytesread);
bool vfsWrite(VFSFile *file, void *buf, size_t sz, size_t *byteswritten);
bool vfsWriteString(VFSFile *file, strref str, size_t *byteswritten);
int64 vfsTell(VFSFile *file);
int64 vfsSeek(VFSFile *file, int64 off, int seektype);

bool vfsFlush(VFSFile *file);

enum VFSFlags {
    VFS_              = 0x00000000,
    // Flags that are valid for both VFS creation and VFS providers
    VFS_ReadOnly      = 0x00000001,     // write operations not allowed on this layer
    VFS_CaseSensitive = 0x00000002,     // filesystem or VFS is case-sensitive
    VFS_NoCache       = 0x00000004,     // do not populate file cache

    // Flags that are valid for VFS providers only (mount options)
    VFS_NewFiles      = 0x00000008,     // new file creations go to this VFS layer
    VFS_AlwaysCOW     = 0x00000010,     // write operations always create a copy in this layer,
                                        // even if a lower layer is writable
    VFS_Opaque        = 0x00000020,     // hides lower layers entirely
};

CX_C_END
