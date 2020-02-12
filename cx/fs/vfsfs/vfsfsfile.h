#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include "vfsfs.h"
#include <cx/fs/file.h>

typedef struct VFSFSFile VFSFSFile;

typedef struct VFSFSFile_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*close)(void *self);
    bool (*read)(void *self, void *buf, size_t sz, size_t *bytesread);
    bool (*write)(void *self, void *buf, size_t sz, size_t *byteswritten);
    int64 (*tell)(void *self);
    int64 (*seek)(void *self, int64 off, int seektype);
    bool (*flush)(void *self);
} VFSFSFile_ClassIf;
extern VFSFSFile_ClassIf VFSFSFile_ClassIf_tmpl;

typedef struct VFSFSFile {
    VFSFSFile_ClassIf *_;
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    FSFile *file;
} VFSFSFile;
extern ObjClassInfo VFSFSFile_clsinfo;

VFSFSFile *VFSFSFile_create(FSFile *f);
#define vfsfsfileCreate(f) VFSFSFile_create(f)
#define vfsfsfileClose(self) (self)->_->close(objInstBase(self))
#define vfsfsfileRead(self, buf, sz, bytesread) (self)->_->read(objInstBase(self), buf, sz, bytesread)
#define vfsfsfileWrite(self, buf, sz, byteswritten) (self)->_->write(objInstBase(self), buf, sz, byteswritten)
#define vfsfsfileTell(self) (self)->_->tell(objInstBase(self))
#define vfsfsfileSeek(self, off, seektype) (self)->_->seek(objInstBase(self), off, seektype)
#define vfsfsfileFlush(self) (self)->_->flush(objInstBase(self))

