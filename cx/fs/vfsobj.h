#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/thread/rwlock.h>

typedef struct VFSDir VFSDir;
typedef struct VFS VFS;
typedef struct VFSMount VFSMount;

typedef struct VFS {
    ObjIface *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_VFS;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    VFSDir *root;
    hashtable namespaces;
    string curdir;
    RWLock vfslock;
    RWLock vfsdlock;
    uint32 flags;
} VFS;
extern ObjClassInfo VFS_clsinfo;
#define VFS(inst) ((VFS*)(&((inst)->_is_VFS), (inst)))

VFS *VFS_create(uint32 flags);
#define vfsCreate(flags) VFS_create(flags)
VFS *VFS_createFromFS();
#define vfsCreateFromFS() VFS_createFromFS()

typedef struct VFSMount {
    ObjIface *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_VFSMount;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    ObjInst *provider;
    uint32 flags;
} VFSMount;
extern ObjClassInfo VFSMount_clsinfo;
#define VFSMount(inst) ((VFSMount*)(&((inst)->_is_VFSMount), (inst)))

VFSMount *VFSMount_create(ObjInst *provider, uint32 flags);
#define vfsmountCreate(provider, flags) VFSMount_create(ObjInst(provider), flags)

