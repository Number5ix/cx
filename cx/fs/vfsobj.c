// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "vfsobj.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "vfs_private.h"

_objfactory_guaranteed VFS* VFS_create(uint32 flags)
{
    VFS *ret;
    ret = objInstCreate(VFS);
    ret->flags = flags;
    objInstInit(ret);
    return ret;
}

_objinit_guaranteed bool VFS_init(_Inout_ VFS* self)
{
    self->root = _vfsDirCreate(self, NULL);
    strDup(&self->curdir, fsPathSepStr);
    rwlockInit(&self->vfslock);
    rwlockInit(&self->vfsdlock);
    htInit(&self->namespaces, string, custom(ptr, VFSDir_ops), 4, HT_CaseInsensitive);
    // Autogen begins -----
    return true;
    // Autogen ends -------
}

void VFS_destroy(_Inout_ VFS* self)
{
    _stDestroy(stFullType(custom(ptr, VFSDir_ops)), &stgeneric(ptr, self->root), 0);
    rwlockDestroy(&self->vfsdlock);
    rwlockDestroy(&self->vfslock);
    // Autogen begins -----
    htDestroy(&self->namespaces);
    strDestroy(&self->curdir);
    // Autogen ends -------
}

_objfactory_guaranteed VFSMount* VFSMount_create(ObjInst* provider, uint32 flags)
{
    VFSMount *ret;
    ret = objInstCreate(VFSMount);

    ret->provider = objAcquire(provider);
    ret->flags = flags;

    objInstInit(ret);
    return ret;
}

void VFSMount_destroy(_Inout_ VFSMount* self)
{
    // Autogen begins -----
    objRelease(&self->provider);
    // Autogen ends -------
}

_objfactory_check VFS* VFS_createFromFS()
{
    VFS *ret = VFS_create(_vfsIsPlatformCaseSensitive() ? VFS_CaseSensitive : 0);

    if (!_vfsAddPlatformSpecificMounts(ret)) {
        objRelease(&ret);
        return NULL;
    }

    return ret;
}

// Autogen begins -----
#include "vfsobj.auto.inc"
// Autogen ends -------
