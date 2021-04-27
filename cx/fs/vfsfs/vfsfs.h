#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/fs/vfsprovider.h>

typedef struct VFSFS VFSFS;

typedef struct VFSFS_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    uint32 (*flags)(void *self);
    ObjInst *(*open)(void *self, strref path, int flags);
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
    VFSFS_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_VFSFS;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    string root;
} VFSFS;
extern ObjClassInfo VFSFS_clsinfo;
#define VFSFS(inst) ((VFSFS*)(&((inst)->_is_VFSFS), (inst)))
#define VFSFSNone ((VFSFS*)NULL)

VFSFS *VFSFS_create(strref rootpath);
#define vfsfsCreate(rootpath) VFSFS_create(rootpath)
#define vfsfsFlags(self) (self)->_->flags(VFSFS(self))
#define vfsfsOpen(self, path, flags) (self)->_->open(VFSFS(self), path, flags)
#define vfsfsStat(self, path, stat) (self)->_->stat(VFSFS(self), path, stat)
#define vfsfsSetTimes(self, path, modified, accessed) (self)->_->setTimes(VFSFS(self), path, modified, accessed)
#define vfsfsCreateDir(self, path) (self)->_->createDir(VFSFS(self), path)
#define vfsfsRemoveDir(self, path) (self)->_->removeDir(VFSFS(self), path)
#define vfsfsDeleteFile(self, path) (self)->_->deleteFile(VFSFS(self), path)
#define vfsfsRename(self, oldpath, newpath) (self)->_->rename(VFSFS(self), oldpath, newpath)
#define vfsfsGetFSPath(self, out, path) (self)->_->getFSPath(VFSFS(self), out, path)
#define vfsfsSearchInit(self, iter, path, pattern, stat) (self)->_->searchInit(VFSFS(self), iter, path, pattern, stat)
#define vfsfsSearchNext(self, iter) (self)->_->searchNext(VFSFS(self), iter)
#define vfsfsSearchFinish(self, iter) (self)->_->searchFinish(VFSFS(self), iter)

