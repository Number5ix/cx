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
    flags_t (*flags)(_Inout_ void *self);
    // returns an object that implements VFSFileProvider
    _Ret_opt_valid_ ObjInst *(*open)(_Inout_ void *self, _In_opt_ strref path, flags_t flags);
    FSPathStat (*stat)(_Inout_ void *self, _In_opt_ strref path, _When_(return != FS_Nonexistent, _Out_opt_) FSStat *stat);
    bool (*setTimes)(_Inout_ void *self, _In_opt_ strref path, int64 modified, int64 accessed);
    bool (*createDir)(_Inout_ void *self, _In_opt_ strref path);
    bool (*removeDir)(_Inout_ void *self, _In_opt_ strref path);
    bool (*deleteFile)(_Inout_ void *self, _In_opt_ strref path);
    bool (*rename)(_Inout_ void *self, _In_opt_ strref oldpath, _In_opt_ strref newpath);
    bool (*getFSPath)(_Inout_ void *self, _Inout_ string *out, _In_opt_ strref path);
    bool (*searchInit)(_Inout_ void *self, _Out_ FSSearchIter *iter, _In_opt_ strref path, _In_opt_ strref pattern, bool stat);
    bool (*searchNext)(_Inout_ void *self, _Inout_ FSSearchIter *iter);
    void (*searchFinish)(_Inout_ void *self, _Inout_ FSSearchIter *iter);
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
#define VFSFS(inst) ((VFSFS*)(unused_noeval((inst) && &((inst)->_is_VFSFS)), (inst)))
#define VFSFSNone ((VFSFS*)NULL)

_objfactory_check VFSFS *VFSFS_create(_In_opt_ strref rootpath);
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
// FSPathStat vfsfsStat(VFSFS *self, strref path, FSStat *stat);
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

