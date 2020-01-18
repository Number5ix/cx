#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/fs/fs.h>
#include <cx/fs/vfsprovider.h>

typedef struct VFSFS VFSFS;

typedef struct VFSFS_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*getFSPath)(void *self, string *out, string path);
    uint32 (*flags)(void *self);
    ObjInst *(*open)(void *self, string path, int flags);
    int (*stat)(void *self, string path, FSStat *stat);
    bool (*createDir)(void *self, string path);
    bool (*removeDir)(void *self, string path);
    bool (*deleteFile)(void *self, string path);
    bool (*rename)(void *self, string oldpath, string newpath);
    ObjInst *(*searchDir)(void *self, string path, string pattern, bool stat);
} VFSFS_ClassIf;
extern VFSFS_ClassIf VFSFS_ClassIf_tmpl;

typedef struct VFSFS {
    VFSFS_ClassIf *_;
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    string root;
} VFSFS;
extern ObjClassInfo VFSFS_clsinfo;

VFSFS *VFSFS_create(string rootpath);
#define vfsfsCreate(rootpath) VFSFS_create(rootpath)
#define vfsfsGetFSPath(self, out, path) (self)->_->getFSPath(objInstBase(self), out, path)
#define vfsfsFlags(self) (self)->_->flags(objInstBase(self))
#define vfsfsOpen(self, path, flags) (self)->_->open(objInstBase(self), path, flags)
#define vfsfsStat(self, path, stat) (self)->_->stat(objInstBase(self), path, stat)
#define vfsfsCreateDir(self, path) (self)->_->createDir(objInstBase(self), path)
#define vfsfsRemoveDir(self, path) (self)->_->removeDir(objInstBase(self), path)
#define vfsfsDeleteFile(self, path) (self)->_->deleteFile(objInstBase(self), path)
#define vfsfsRename(self, oldpath, newpath) (self)->_->rename(objInstBase(self), oldpath, newpath)
#define vfsfsSearchDir(self, path, pattern, stat) (self)->_->searchDir(objInstBase(self), path, pattern, stat)

