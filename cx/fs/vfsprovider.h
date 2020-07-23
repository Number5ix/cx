#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/fs/fs.h>


typedef struct VFSFileProvider {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*close)(void *self);
    bool (*read)(void *self, void *buf, size_t sz, size_t *bytesread);
    bool (*write)(void *self, void *buf, size_t sz, size_t *byteswritten);
    int64 (*tell)(void *self);
    int64 (*seek)(void *self, int64 off, int seektype);
    bool (*flush)(void *self);
} VFSFileProvider;
extern VFSFileProvider VFSFileProvider_tmpl;

typedef struct VFSDirSearchProvider {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*close)(void *self);
    FSDirEnt *(*next)(void *self);
} VFSDirSearchProvider;
extern VFSDirSearchProvider VFSDirSearchProvider_tmpl;

typedef struct VFSProvider {
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
    ObjInst *(*searchDir)(void *self, strref path, strref pattern, bool stat);
} VFSProvider;
extern VFSProvider VFSProvider_tmpl;

