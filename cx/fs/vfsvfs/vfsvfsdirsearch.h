#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
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
    union {
        ObjClassInfo *_clsinfo;
        void *_is_VFSVFSDirSearch;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    VFSDirSearch *dirsearch;
} VFSVFSDirSearch;
extern ObjClassInfo VFSVFSDirSearch_clsinfo;
#define VFSVFSDirSearch(inst) ((VFSVFSDirSearch*)(&((inst)->_is_VFSVFSDirSearch), (inst)))

VFSVFSDirSearch *VFSVFSDirSearch_create(VFSDirSearch *ds);
#define vfsvfsdirsearchCreate(ds) VFSVFSDirSearch_create(ds)
#define vfsvfsdirsearchClose(self) (self)->_->close(VFSVFSDirSearch(self))
#define vfsvfsdirsearchNext(self) (self)->_->next(VFSVFSDirSearch(self))

