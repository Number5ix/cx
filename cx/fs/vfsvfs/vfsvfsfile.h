#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include "vfsvfs.h"
#include <cx/fs/vfs.h>

typedef struct VFSDir VFSDir;
typedef struct VFSVFSFile VFSVFSFile;
saDeclarePtr(VFSVFSFile);

typedef struct VFSVFSFile_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*close)(_Inout_ void *self);
    bool (*read)(_Inout_ void *self, _Out_writes_bytes_to_(sz, *bytesread) void *buf, size_t sz, _Out_ _Deref_out_range_(0, sz) size_t *bytesread);
    bool (*write)(_Inout_ void *self, _In_reads_bytes_(sz) void *buf, size_t sz, _Out_opt_ _Deref_out_range_(0, sz) size_t *byteswritten);
    int64 (*tell)(_Inout_ void *self);
    int64 (*seek)(_Inout_ void *self, int64 off, FSSeekType seektype);
    bool (*flush)(_Inout_ void *self);
} VFSVFSFile_ClassIf;
extern VFSVFSFile_ClassIf VFSVFSFile_ClassIf_tmpl;

typedef struct VFSVFSFile {
    union {
        VFSVFSFile_ClassIf *_;
        void *_is_VFSVFSFile;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    VFSFile *file;
} VFSVFSFile;
extern ObjClassInfo VFSVFSFile_clsinfo;
#define VFSVFSFile(inst) ((VFSVFSFile*)(unused_noeval((inst) && &((inst)->_is_VFSVFSFile)), (inst)))
#define VFSVFSFileNone ((VFSVFSFile*)NULL)

_objfactory_guaranteed VFSVFSFile *VFSVFSFile_create(VFSFile *f);
// VFSVFSFile *vfsvfsfileCreate(VFSFile *f);
#define vfsvfsfileCreate(f) VFSVFSFile_create(f)

// bool vfsvfsfileClose(VFSVFSFile *self);
#define vfsvfsfileClose(self) (self)->_->close(VFSVFSFile(self))
// bool vfsvfsfileRead(VFSVFSFile *self, void *buf, size_t sz, size_t *bytesread);
#define vfsvfsfileRead(self, buf, sz, bytesread) (self)->_->read(VFSVFSFile(self), buf, sz, bytesread)
// bool vfsvfsfileWrite(VFSVFSFile *self, void *buf, size_t sz, size_t *byteswritten);
#define vfsvfsfileWrite(self, buf, sz, byteswritten) (self)->_->write(VFSVFSFile(self), buf, sz, byteswritten)
// int64 vfsvfsfileTell(VFSVFSFile *self);
#define vfsvfsfileTell(self) (self)->_->tell(VFSVFSFile(self))
// int64 vfsvfsfileSeek(VFSVFSFile *self, int64 off, FSSeekType seektype);
#define vfsvfsfileSeek(self, off, seektype) (self)->_->seek(VFSVFSFile(self), off, seektype)
// bool vfsvfsfileFlush(VFSVFSFile *self);
#define vfsvfsfileFlush(self) (self)->_->flush(VFSVFSFile(self))

