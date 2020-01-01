#pragma once

#include <cx/fs/file.h>
#include <cx/fs/fs.h>
#include <cx/fs/vfsprovider.h>
#include <cx/fs/vfsobj.h>

typedef struct VFS VFS;
typedef struct VFSFile VFSFile;

_EXTERN_C_BEGIN

// vfsCreate() is in vfsobj.h
void vfsDestroy(VFS *vfs);
bool _vfsMountProvider(VFS *vfs, ObjInst *provider, string path, uint32 flags);
#define vfsMountProvider(vfs, provider, path, flags) _vfsMountProvider(vfs, objInstBase(provider), path, flags)

// Mounts the built-in OS filesystem provider to the given VFS
bool vfsMountFS(VFS *vfs, string path, string fsroot, uint32 flags);

// Mounts one VFS under another (loopback mode)
bool vfsMountVFS(VFS *vfs, string path, VFS *vfs2, string vfs2root, uint32 flags);

// Get / set current directory
void vfsCurDir(VFS *vfs, string *out);
bool vfsSetCurDir(VFS *vfs, string cur);

int vfsStat(VFS *vfs, string path, FSStat *stat);

_meta_inline bool vfsExist(VFS *vfs, string path)
{
    return vfsStat(vfs, path, NULL) != FS_Nonexistent;
}
_meta_inline bool vfsIsDir(VFS *vfs, string path)
{
    return vfsStat(vfs, path, NULL) == FS_Directory;
}
_meta_inline bool vfsIsFile(VFS *vfs, string path)
{
    return vfsStat(vfs, path, NULL) == FS_File;
}

bool vfsCreateDir(VFS *vfs, string path);
bool vfsCreateAll(VFS *vfs, string path);
bool vfsRemoveDir(VFS *vfs, string path);
bool vfsDelete(VFS *vfs, string path);
bool vfsCopy(VFS *vfs, string from, string to);
bool vfsRename(VFS *vfs, string from, string to);

// Special case to get the actual underlying path from VFSFS, works *only* if the path
// in question is backed by a VFSFS provider.
bool vfsGetFSPath(string *out, VFS *vfs, string path);

typedef struct VFSDirSearch VFSDirSearch;
VFSDirSearch *vfsSearchDir(VFS *vfs, string path, string pattern, int typefilter, bool stat);
FSDirEnt *vfsSearchNext(VFSDirSearch *search);
void vfsSearchClose(VFSDirSearch *search);

VFSFile *vfsOpen(VFS *vfs, string path, int flags);
bool vfsClose(VFSFile *file);
bool vfsRead(VFSFile *file, void *buf, size_t sz, size_t *bytesread);
bool vfsWrite(VFSFile *file, void *buf, size_t sz, size_t *byteswritten);
int64 vfsTell(VFSFile *file);
int64 vfsSeek(VFSFile *file, int64 off, int seektype);

bool vfsFlush(VFSFile *file);

enum VFSFlags {
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

_EXTERN_C_END
