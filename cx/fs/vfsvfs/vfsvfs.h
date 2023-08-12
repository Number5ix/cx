#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/fs/vfsprovider.h>
#include <cx/fs/vfsobj.h>

typedef struct VFSDir VFSDir;
typedef struct VFSVFS VFSVFS;
saDeclarePtr(VFSVFS);

typedef struct VFSVFS_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    // VFSProviderFlags enforced for this provider
    flags_t (*flags)(void *self);
    // returns an object that implements VFSFileProvider
    ObjInst *(*open)(void *self, strref path, flags_t flags);
    int (*stat)(void *self, strref path, FSStat *stat);
    bool (*setTimes)(void *self, strref path, int64 modified, int64 accessed);
    bool (*createDir)(void *self, strref path);
    bool (*removeDir)(void *self, strref path);
    bool (*deleteFile)(void *self, strref path);
    bool (*rename)(void *self, strref oldpath, strref newpath);
    bool (*getFSPath)(void *self, string *out, strref path);
    bool (*searchInit)(void *self, FSSearchIter *iter, strref path, strref pattern, bool stat);
    bool (*searchNext)(void *self, FSSearchIter *iter);
    void (*searchFinish)(void *self, FSSearchIter *iter);
} VFSVFS_ClassIf;
extern VFSVFS_ClassIf VFSVFS_ClassIf_tmpl;

typedef struct VFSVFS {
    union {
        VFSVFS_ClassIf *_;
        void *_is_VFSVFS;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    VFS *vfs;
    string root;
} VFSVFS;
extern ObjClassInfo VFSVFS_clsinfo;
#define VFSVFS(inst) ((VFSVFS*)(&((inst)->_is_VFSVFS)))
#define VFSVFSNone ((VFSVFS*)NULL)

VFSVFS *VFSVFS_create(VFS *vfs, strref rootpath);
// VFSVFS *vfsvfsCreate(VFS *vfs, strref rootpath);
#define vfsvfsCreate(vfs, rootpath) VFSVFS_create(VFS(vfs), rootpath)

// flags_t vfsvfsFlags(VFSVFS *self);
//
// VFSProviderFlags enforced for this provider
#define vfsvfsFlags(self) (self)->_->flags(VFSVFS(self))
// ObjInst *vfsvfsOpen(VFSVFS *self, strref path, flags_t flags);
//
// returns an object that implements VFSFileProvider
#define vfsvfsOpen(self, path, flags) (self)->_->open(VFSVFS(self), path, flags)
// int vfsvfsStat(VFSVFS *self, strref path, FSStat *stat);
#define vfsvfsStat(self, path, stat) (self)->_->stat(VFSVFS(self), path, stat)
// bool vfsvfsSetTimes(VFSVFS *self, strref path, int64 modified, int64 accessed);
#define vfsvfsSetTimes(self, path, modified, accessed) (self)->_->setTimes(VFSVFS(self), path, modified, accessed)
// bool vfsvfsCreateDir(VFSVFS *self, strref path);
#define vfsvfsCreateDir(self, path) (self)->_->createDir(VFSVFS(self), path)
// bool vfsvfsRemoveDir(VFSVFS *self, strref path);
#define vfsvfsRemoveDir(self, path) (self)->_->removeDir(VFSVFS(self), path)
// bool vfsvfsDeleteFile(VFSVFS *self, strref path);
#define vfsvfsDeleteFile(self, path) (self)->_->deleteFile(VFSVFS(self), path)
// bool vfsvfsRename(VFSVFS *self, strref oldpath, strref newpath);
#define vfsvfsRename(self, oldpath, newpath) (self)->_->rename(VFSVFS(self), oldpath, newpath)
// bool vfsvfsGetFSPath(VFSVFS *self, string *out, strref path);
#define vfsvfsGetFSPath(self, out, path) (self)->_->getFSPath(VFSVFS(self), out, path)
// bool vfsvfsSearchInit(VFSVFS *self, FSSearchIter *iter, strref path, strref pattern, bool stat);
#define vfsvfsSearchInit(self, iter, path, pattern, stat) (self)->_->searchInit(VFSVFS(self), iter, path, pattern, stat)
// bool vfsvfsSearchNext(VFSVFS *self, FSSearchIter *iter);
#define vfsvfsSearchNext(self, iter) (self)->_->searchNext(VFSVFS(self), iter)
// void vfsvfsSearchFinish(VFSVFS *self, FSSearchIter *iter);
#define vfsvfsSearchFinish(self, iter) (self)->_->searchFinish(VFSVFS(self), iter)

