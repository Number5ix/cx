#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/fs/fs.h>
#include <cx/fs/vfsprovider.h>
#include <cx/thread/rwlock.h>
#include <cx/fs/vfsobj.h>
#include "vfsvfs.h"
#include <cx/fs/vfs.h>

typedef struct VFSDir VFSDir;
typedef struct VFSVFSDirSearch VFSVFSDirSearch;

typedef struct VFSVFSDirSearch_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*close)(void *self);
    FSDirEnt *(*next)(void *self);
} VFSVFSDirSearch_ClassIf;
extern VFSVFSDirSearch_ClassIf VFSVFSDirSearch_ClassIf_tmpl;

typedef struct VFSVFSDirSearch {
    VFSVFSDirSearch_ClassIf *_;
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    VFSDirSearch *dirsearch;
} VFSVFSDirSearch;
extern ObjClassInfo VFSVFSDirSearch_clsinfo;

VFSVFSDirSearch *VFSVFSDirSearch_create(VFSDirSearch *ds);
#define vfsvfsdirsearchCreate(ds) VFSVFSDirSearch_create(ds)
#define vfsvfsdirsearchClose(self) (self)->_->close(objInstBase(self))
#define vfsvfsdirsearchNext(self) (self)->_->next(objInstBase(self))

