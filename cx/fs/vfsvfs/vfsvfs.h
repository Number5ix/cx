#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/fs/vfsprovider.h>
#include <cx/fs/vfsobj.h>

typedef struct VFSDir VFSDir;
typedef struct VFSVFS VFSVFS;

typedef struct VFSVFS_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    uint32 (*flags)(void *self);
    ObjInst *(*open)(void *self, string path, int flags);
    int (*stat)(void *self, string path, FSStat *stat);
    bool (*createDir)(void *self, string path);
    bool (*removeDir)(void *self, string path);
    bool (*deleteFile)(void *self, string path);
    bool (*rename)(void *self, string oldpath, string newpath);
    bool (*getFSPath)(void *self, string *out, string path);
    ObjInst *(*searchDir)(void *self, string path, string pattern, bool stat);
} VFSVFS_ClassIf;
extern VFSVFS_ClassIf VFSVFS_ClassIf_tmpl;

typedef struct VFSVFS {
    VFSVFS_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_VFSVFS;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    VFS *vfs;
    string root;
} VFSVFS;
extern ObjClassInfo VFSVFS_clsinfo;
#define VFSVFS(inst) ((VFSVFS*)(&((inst)->_is_VFSVFS), (inst)))

VFSVFS *VFSVFS_create(VFS *vfs, string rootpath);
#define vfsvfsCreate(vfs, rootpath) VFSVFS_create(VFS(vfs), rootpath)
#define vfsvfsFlags(self) (self)->_->flags(VFSVFS(self))
#define vfsvfsOpen(self, path, flags) (self)->_->open(VFSVFS(self), path, flags)
#define vfsvfsStat(self, path, stat) (self)->_->stat(VFSVFS(self), path, stat)
#define vfsvfsCreateDir(self, path) (self)->_->createDir(VFSVFS(self), path)
#define vfsvfsRemoveDir(self, path) (self)->_->removeDir(VFSVFS(self), path)
#define vfsvfsDeleteFile(self, path) (self)->_->deleteFile(VFSVFS(self), path)
#define vfsvfsRename(self, oldpath, newpath) (self)->_->rename(VFSVFS(self), oldpath, newpath)
#define vfsvfsGetFSPath(self, out, path) (self)->_->getFSPath(VFSVFS(self), out, path)
#define vfsvfsSearchDir(self, path, pattern, stat) (self)->_->searchDir(VFSVFS(self), path, pattern, stat)

