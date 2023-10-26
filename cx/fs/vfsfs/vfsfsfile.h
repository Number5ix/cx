#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include "vfsfs.h"
#include <cx/fs/file.h>

typedef struct VFSFSFile VFSFSFile;
saDeclarePtr(VFSFSFile);

typedef struct VFSFSFile_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*close)(_Inout_ void *self);
    bool (*read)(_Inout_ void *self, void *buf, size_t sz, size_t *bytesread);
    bool (*write)(_Inout_ void *self, void *buf, size_t sz, size_t *byteswritten);
    int64 (*tell)(_Inout_ void *self);
    int64 (*seek)(_Inout_ void *self, int64 off, int seektype);
    bool (*flush)(_Inout_ void *self);
} VFSFSFile_ClassIf;
extern VFSFSFile_ClassIf VFSFSFile_ClassIf_tmpl;

typedef struct VFSFSFile {
    union {
        VFSFSFile_ClassIf *_;
        void *_is_VFSFSFile;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    FSFile *file;
} VFSFSFile;
extern ObjClassInfo VFSFSFile_clsinfo;
#define VFSFSFile(inst) ((VFSFSFile*)(unused_noeval((inst) && &((inst)->_is_VFSFSFile)), (inst)))
#define VFSFSFileNone ((VFSFSFile*)NULL)

_objfactory VFSFSFile *VFSFSFile_create(FSFile *f);
// VFSFSFile *vfsfsfileCreate(FSFile *f);
#define vfsfsfileCreate(f) VFSFSFile_create(f)

// bool vfsfsfileClose(VFSFSFile *self);
#define vfsfsfileClose(self) (self)->_->close(VFSFSFile(self))
// bool vfsfsfileRead(VFSFSFile *self, void *buf, size_t sz, size_t *bytesread);
#define vfsfsfileRead(self, buf, sz, bytesread) (self)->_->read(VFSFSFile(self), buf, sz, bytesread)
// bool vfsfsfileWrite(VFSFSFile *self, void *buf, size_t sz, size_t *byteswritten);
#define vfsfsfileWrite(self, buf, sz, byteswritten) (self)->_->write(VFSFSFile(self), buf, sz, byteswritten)
// int64 vfsfsfileTell(VFSFSFile *self);
#define vfsfsfileTell(self) (self)->_->tell(VFSFSFile(self))
// int64 vfsfsfileSeek(VFSFSFile *self, int64 off, int seektype);
#define vfsfsfileSeek(self, off, seektype) (self)->_->seek(VFSFSFile(self), off, seektype)
// bool vfsfsfileFlush(VFSFSFile *self);
#define vfsfsfileFlush(self) (self)->_->flush(VFSFSFile(self))

