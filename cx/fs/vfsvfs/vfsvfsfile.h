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

    bool (*close)(void *self);
    bool (*read)(void *self, void *buf, size_t sz, size_t *bytesread);
    bool (*write)(void *self, void *buf, size_t sz, size_t *byteswritten);
    int64 (*tell)(void *self);
    int64 (*seek)(void *self, int64 off, int seektype);
    bool (*flush)(void *self);
} VFSVFSFile_ClassIf;
extern VFSVFSFile_ClassIf VFSVFSFile_ClassIf_tmpl;

typedef struct VFSVFSFile {
    VFSVFSFile_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_VFSVFSFile;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    VFSFile *file;
} VFSVFSFile;
extern ObjClassInfo VFSVFSFile_clsinfo;
#define VFSVFSFile(inst) ((VFSVFSFile*)((void)((inst) && &((inst)->_is_VFSVFSFile)), (inst)))
#define VFSVFSFileNone ((VFSVFSFile*)NULL)

VFSVFSFile *VFSVFSFile_create(VFSFile *f);
#define vfsvfsfileCreate(f) VFSVFSFile_create(f)
#define vfsvfsfileClose(self) (self)->_->close(VFSVFSFile(self))
#define vfsvfsfileRead(self, buf, sz, bytesread) (self)->_->read(VFSVFSFile(self), buf, sz, bytesread)
#define vfsvfsfileWrite(self, buf, sz, byteswritten) (self)->_->write(VFSVFSFile(self), buf, sz, byteswritten)
#define vfsvfsfileTell(self) (self)->_->tell(VFSVFSFile(self))
#define vfsvfsfileSeek(self, off, seektype) (self)->_->seek(VFSVFSFile(self), off, seektype)
#define vfsvfsfileFlush(self) (self)->_->flush(VFSVFSFile(self))

