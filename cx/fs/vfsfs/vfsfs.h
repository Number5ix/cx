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
    ObjInst *(*open)(void *self, string path, int flags);
    int (*stat)(void *self, string path, FSStat *stat);
    bool (*setTimes)(void *self, string path, int64 modified, int64 accessed);
    bool (*createDir)(void *self, string path);
    bool (*removeDir)(void *self, string path);
    bool (*deleteFile)(void *self, string path);
    bool (*rename)(void *self, string oldpath, string newpath);
    bool (*getFSPath)(void *self, string *out, string path);
    ObjInst *(*searchDir)(void *self, string path, string pattern, bool stat);
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

VFSFS *VFSFS_create(string rootpath);
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
#define vfsfsSearchDir(self, path, pattern, stat) (self)->_->searchDir(VFSFS(self), path, pattern, stat)

