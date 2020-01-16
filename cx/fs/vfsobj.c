// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/core/objstdif.h>
#include <cx/debug/assert.h>
#include <cx/container.h>
#include <cx/string.h>
#include "vfsobj.h"
// ==================== Auto-generated section ends ======================
#include "vfs_private.h"

VFS *VFS_create(uint32 flags)
{
    VFS *ret;
    ret = objInstCreate(VFS);
    ret->flags = flags;
    if (!objInstInit(ret))
        objRelease(&ret);
    return ret;
}

bool VFS_init(VFS *self)
{
    self->root = _vfsDirCreate(self, NULL);
    strDup(&self->curdir, fsPathSepStr);
    self->vfslock = rwlockCreate();
// ==================== Auto-generated section begins ====================
    self->namespaces = htCreate(string, custom(ptr, VFSDir_ops), 4, CaseInsensitive);
    return true;
// ==================== Auto-generated section ends ======================
}

void VFS_destroy(VFS *self)
{
    _stDestroy(stFullType(custom(ptr, VFSDir_ops)), &stgeneric(object, self->root), 0);
    rwlockDestroy(self->vfslock);
// ==================== Auto-generated section begins ====================
    htDestroy(&self->namespaces);
    strDestroy(&self->curdir);
// ==================== Auto-generated section ends ======================
}

VFSMount *VFSMount_create(ObjInst *provider, uint32 flags)
{
    VFSMount *ret;
    ret = objInstCreate(VFSMount);

    ret->provider = objAcquire(provider);
    ret->flags = flags;

    if (!objInstInit(ret))
        objRelease(&ret);
    return ret;
}

void VFSMount_destroy(VFSMount *self)
{
// ==================== Auto-generated section begins ====================
    objRelease(&self->provider);
// ==================== Auto-generated section ends ======================
}

VFS *VFS_createFromFS()
{
    VFS *ret = VFS_create(_vfsIsPlatformCaseSensitive() ? VFS_CaseSensitive : 0);
    if (!ret)
        return NULL;

    if (!_vfsAddPlatformSpecificMounts(ret)) {
        objRelease(&ret);
        return NULL;
    }

    return ret;
}

// ==================== Auto-generated section begins ====================
static ObjIface *_ifimpl_VFS[] = {
    NULL
};

ObjClassInfo VFS_clsinfo = {
    .instsize = sizeof(VFS),
    .init = (bool(*)(void*))VFS_init,
    .destroy = (void(*)(void*))VFS_destroy,
    .ifimpl = _ifimpl_VFS,
};

static ObjIface *_ifimpl_VFSMount[] = {
    NULL
};

ObjClassInfo VFSMount_clsinfo = {
    .instsize = sizeof(VFSMount),
    .destroy = (void(*)(void*))VFSMount_destroy,
    .ifimpl = _ifimpl_VFSMount,
};

// ==================== Auto-generated section ends ======================
