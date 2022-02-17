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

    flags_t (*flags)(void *self);
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
#define VFSVFS(inst) ((VFSVFS*)((void)((inst) && &((inst)->_is_VFSVFS)), (inst)))
#define VFSVFSNone ((VFSVFS*)NULL)

VFSVFS *VFSVFS_create(VFS *vfs, strref rootpath);
#define vfsvfsCreate(vfs, rootpath) VFSVFS_create(VFS(vfs), rootpath)
#define vfsvfsFlags(self) (self)->_->flags(VFSVFS(self))
#define vfsvfsOpen(self, path, flags) (self)->_->open(VFSVFS(self), path, flags)
#define vfsvfsStat(self, path, stat) (self)->_->stat(VFSVFS(self), path, stat)
#define vfsvfsSetTimes(self, path, modified, accessed) (self)->_->setTimes(VFSVFS(self), path, modified, accessed)
#define vfsvfsCreateDir(self, path) (self)->_->createDir(VFSVFS(self), path)
#define vfsvfsRemoveDir(self, path) (self)->_->removeDir(VFSVFS(self), path)
#define vfsvfsDeleteFile(self, path) (self)->_->deleteFile(VFSVFS(self), path)
#define vfsvfsRename(self, oldpath, newpath) (self)->_->rename(VFSVFS(self), oldpath, newpath)
#define vfsvfsGetFSPath(self, out, path) (self)->_->getFSPath(VFSVFS(self), out, path)
#define vfsvfsSearchInit(self, iter, path, pattern, stat) (self)->_->searchInit(VFSVFS(self), iter, path, pattern, stat)
#define vfsvfsSearchNext(self, iter) (self)->_->searchNext(VFSVFS(self), iter)
#define vfsvfsSearchFinish(self, iter) (self)->_->searchFinish(VFSVFS(self), iter)

