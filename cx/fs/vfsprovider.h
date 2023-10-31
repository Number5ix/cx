#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/fs/fs.h>


typedef struct VFSFileProvider {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*close)(_Inout_ void *self);
    bool (*read)(_Inout_ void *self, void *buf, size_t sz, size_t *bytesread);
    bool (*write)(_Inout_ void *self, void *buf, size_t sz, size_t *byteswritten);
    int64 (*tell)(_Inout_ void *self);
    int64 (*seek)(_Inout_ void *self, int64 off, FSSeekType seektype);
    bool (*flush)(_Inout_ void *self);
} VFSFileProvider;
extern VFSFileProvider VFSFileProvider_tmpl;

typedef struct VFSProvider {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    // VFSProviderFlags enforced for this provider
    flags_t (*flags)(_Inout_ void *self);
    // returns an object that implements VFSFileProvider
    ObjInst *(*open)(_Inout_ void *self, strref path, flags_t flags);
    int (*stat)(_Inout_ void *self, strref path, FSStat *stat);
    bool (*setTimes)(_Inout_ void *self, strref path, int64 modified, int64 accessed);
    bool (*createDir)(_Inout_ void *self, strref path);
    bool (*removeDir)(_Inout_ void *self, strref path);
    bool (*deleteFile)(_Inout_ void *self, strref path);
    bool (*rename)(_Inout_ void *self, strref oldpath, strref newpath);
    bool (*getFSPath)(_Inout_ void *self, string *out, strref path);
    bool (*searchInit)(_Inout_ void *self, FSSearchIter *iter, strref path, strref pattern, bool stat);
    bool (*searchNext)(_Inout_ void *self, FSSearchIter *iter);
    void (*searchFinish)(_Inout_ void *self, FSSearchIter *iter);
} VFSProvider;
extern VFSProvider VFSProvider_tmpl;

