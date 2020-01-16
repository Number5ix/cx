// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/core/objstdif.h>
#include <cx/debug/assert.h>
#include <cx/container.h>
#include <cx/string.h>
#include "vfsfsfile.h"
// ==================== Auto-generated section ends ======================

VFSFSFile *VFSFSFile_create(FSFile *f)
{
    VFSFSFile *ret;
    ret = objInstCreate(VFSFSFile);
    ret->file = f;
    if (!objInstInit(ret))
        objRelease(&ret);
    return ret;
}

bool VFSFSFile_close(VFSFSFile *self)
{
    bool ret = false;
    if (self->file)
        ret = fsClose(self->file);
    self->file = NULL;
    return ret;
}

bool VFSFSFile_read(VFSFSFile *self, void *buf, size_t sz, size_t *bytesread)
{
    if (!self->file)
        return false;
    return fsRead(self->file, buf, sz, bytesread);
}

bool VFSFSFile_write(VFSFSFile *self, void *buf, size_t sz, size_t *byteswritten)
{
    if (!self->file)
        return false;
    return fsWrite(self->file, buf, sz, byteswritten);
}

int64 VFSFSFile_tell(VFSFSFile *self)
{
    if (!self->file)
        return -1;
    return fsTell(self->file);
}

int64 VFSFSFile_seek(VFSFSFile *self, int64 off, int seektype)
{
    if (!self->file)
        return -1;
    return fsSeek(self->file, off, seektype);
}

bool VFSFSFile_flush(VFSFSFile *self)
{
    if (!self->file)
        return false;
    return fsFlush(self->file);
}

void VFSFSFile_destroy(VFSFSFile *self)
{
    VFSFSFile_close(self);
}

// ==================== Auto-generated section begins ====================
VFSFSFile_ClassIf VFSFSFile_ClassIf_tmpl = {
    ._size = sizeof(VFSFSFile_ClassIf),
};

static VFSFileProvider _impl_VFSFSFile_VFSFileProvider = {
    ._size = sizeof(VFSFileProvider),
    ._implements = (ObjIface*)&VFSFileProvider_tmpl,
    .close = (bool (*)(void*))VFSFSFile_close,
    .read = (bool (*)(void*, void*, size_t, size_t*))VFSFSFile_read,
    .write = (bool (*)(void*, void*, size_t, size_t*))VFSFSFile_write,
    .tell = (int64 (*)(void*))VFSFSFile_tell,
    .seek = (int64 (*)(void*, int64, int))VFSFSFile_seek,
    .flush = (bool (*)(void*))VFSFSFile_flush,
};

static VFSFSFile_ClassIf _impl_VFSFSFile_VFSFSFile_ClassIf = {
    ._size = sizeof(VFSFSFile_ClassIf),
    ._implements = (ObjIface*)&VFSFSFile_ClassIf_tmpl,
    .close = (bool (*)(void*))VFSFSFile_close,
    .read = (bool (*)(void*, void*, size_t, size_t*))VFSFSFile_read,
    .write = (bool (*)(void*, void*, size_t, size_t*))VFSFSFile_write,
    .tell = (int64 (*)(void*))VFSFSFile_tell,
    .seek = (int64 (*)(void*, int64, int))VFSFSFile_seek,
    .flush = (bool (*)(void*))VFSFSFile_flush,
};

static ObjIface *_ifimpl_VFSFSFile[] = {
    (ObjIface*)&_impl_VFSFSFile_VFSFileProvider,
    (ObjIface*)&_impl_VFSFSFile_VFSFSFile_ClassIf,
    NULL
};

ObjClassInfo VFSFSFile_clsinfo = {
    .instsize = sizeof(VFSFSFile),
    .classif = (ObjIface*)&VFSFSFile_ClassIf_tmpl,
    .destroy = (void(*)(void*))VFSFSFile_destroy,
    .ifimpl = _ifimpl_VFSFSFile,
};

// ==================== Auto-generated section ends ======================
