#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/fs/vfsprovider.h>

typedef struct VFSFS VFSFS;
saDeclarePtr(VFSFS);

typedef struct VFSFS_ClassIf {
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
} VFSFS_ClassIf;
extern VFSFS_ClassIf VFSFS_ClassIf_tmpl;

typedef struct VFSFS {
    union {
        VFSFS_ClassIf *_;
        void *_is_VFSFS;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    string root;
} VFSFS;
extern ObjClassInfo VFSFS_clsinfo;
#define VFSFS(inst) ((VFSFS*)(&((inst)->_is_VFSFS)))
#define VFSFSNone ((VFSFS*)NULL)

VFSFS *VFSFS_create(strref rootpath);
// VFSFS *vfsfsCreate(strref rootpath);
#define vfsfsCreate(rootpath) VFSFS_create(rootpath)

// flags_t vfsfsFlags(VFSFS *self);
//
// VFSProviderFlags enforced for this provider
#define vfsfsFlags(self) (self)->_->flags(VFSFS(self))
// ObjInst *vfsfsOpen(VFSFS *self, strref path, flags_t flags);
//
// returns an object that implements VFSFileProvider
#define vfsfsOpen(self, path, flags) (self)->_->open(VFSFS(self), path, flags)
// int vfsfsStat(VFSFS *self, strref path, FSStat *stat);
#define vfsfsStat(self, path, stat) (self)->_->stat(VFSFS(self), path, stat)
// bool vfsfsSetTimes(VFSFS *self, strref path, int64 modified, int64 accessed);
#define vfsfsSetTimes(self, path, modified, accessed) (self)->_->setTimes(VFSFS(self), path, modified, accessed)
// bool vfsfsCreateDir(VFSFS *self, strref path);
#define vfsfsCreateDir(self, path) (self)->_->createDir(VFSFS(self), path)
// bool vfsfsRemoveDir(VFSFS *self, strref path);
#define vfsfsRemoveDir(self, path) (self)->_->removeDir(VFSFS(self), path)
// bool vfsfsDeleteFile(VFSFS *self, strref path);
#define vfsfsDeleteFile(self, path) (self)->_->deleteFile(VFSFS(self), path)
// bool vfsfsRename(VFSFS *self, strref oldpath, strref newpath);
#define vfsfsRename(self, oldpath, newpath) (self)->_->rename(VFSFS(self), oldpath, newpath)
// bool vfsfsGetFSPath(VFSFS *self, string *out, strref path);
#define vfsfsGetFSPath(self, out, path) (self)->_->getFSPath(VFSFS(self), out, path)
// bool vfsfsSearchInit(VFSFS *self, FSSearchIter *iter, strref path, strref pattern, bool stat);
#define vfsfsSearchInit(self, iter, path, pattern, stat) (self)->_->searchInit(VFSFS(self), iter, path, pattern, stat)
// bool vfsfsSearchNext(VFSFS *self, FSSearchIter *iter);
#define vfsfsSearchNext(self, iter) (self)->_->searchNext(VFSFS(self), iter)
// void vfsfsSearchFinish(VFSFS *self, FSSearchIter *iter);
#define vfsfsSearchFinish(self, iter) (self)->_->searchFinish(VFSFS(self), iter)

