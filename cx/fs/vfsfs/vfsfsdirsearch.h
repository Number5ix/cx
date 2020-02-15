#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include "vfsfs.h"

typedef struct VFSFSDirSearch VFSFSDirSearch;

typedef struct VFSFSDirSearch_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*close)(void *self);
    FSDirEnt *(*next)(void *self);
} VFSFSDirSearch_ClassIf;
extern VFSFSDirSearch_ClassIf VFSFSDirSearch_ClassIf_tmpl;

typedef struct VFSFSDirSearch {
    VFSFSDirSearch_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_VFSFSDirSearch;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    FSDirSearch *dirsearch;
} VFSFSDirSearch;
extern ObjClassInfo VFSFSDirSearch_clsinfo;
#define VFSFSDirSearch(inst) ((VFSFSDirSearch*)(&((inst)->_is_VFSFSDirSearch), (inst)))

VFSFSDirSearch *VFSFSDirSearch_create(FSDirSearch *ds);
#define vfsfsdirsearchCreate(ds) VFSFSDirSearch_create(ds)
#define vfsfsdirsearchClose(self) (self)->_->close(VFSFSDirSearch(self))
#define vfsfsdirsearchNext(self) (self)->_->next(VFSFSDirSearch(self))

