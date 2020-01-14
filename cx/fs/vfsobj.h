#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>

typedef struct VFSDir VFSDir;
typedef struct RWLock RWLock;
typedef struct VFS VFS;
typedef struct VFSMount VFSMount;

typedef struct VFS {
    ObjIface *_;
    ObjClassInfo *_clsinfo;
    atomic_intptr _ref;

    VFSDir *root;
    hashtable namespaces;
    string curdir;
    RWLock *vfslock;
    uint32 flags;
} VFS;
extern ObjClassInfo VFS_clsinfo;

VFS *VFS_create(uint32 flags);
#define vfsCreate(flags) VFS_create(flags)
VFS *VFS_createFromFS();
#define vfsCreateFromFS() VFS_createFromFS()

typedef struct VFSMount {
    ObjIface *_;
    ObjClassInfo *_clsinfo;
    atomic_intptr _ref;

    ObjInst *provider;
    uint32 flags;
} VFSMount;
extern ObjClassInfo VFSMount_clsinfo;

VFSMount *VFSMount_create(ObjInst *provider, uint32 flags);
#define vfsmountCreate(provider, flags) VFSMount_create(provider, flags)

