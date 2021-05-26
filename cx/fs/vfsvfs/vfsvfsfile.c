// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/core/objstdif.h>
#include <cx/debug/assert.h>
#include <cx/container.h>
#include <cx/string.h>
#include "vfsvfsfile.h"
// ==================== Auto-generated section ends ======================

VFSVFSFile *VFSVFSFile_create(VFSFile *f)
{
    VFSVFSFile *ret;
    ret = objInstCreate(VFSVFSFile);
    ret->file = f;
    if (!objInstInit(ret))
        objRelease(&ret);
    return ret;
}

bool VFSVFSFile_close(VFSVFSFile *self)
{
    bool ret = false;
    if (self->file)
        ret = vfsClose(self->file);
    self->file = NULL;
    return ret;
}

bool VFSVFSFile_read(VFSVFSFile *self, void *buf, size_t sz, size_t *bytesread)
{
    if (!self->file)
        return false;
    return vfsRead(self->file, buf, sz, bytesread);
}

bool VFSVFSFile_write(VFSVFSFile *self, void *buf, size_t sz, size_t *byteswritten)
{
    if (!self->file)
        return false;
    return vfsWrite(self->file, buf, sz, byteswritten);
}

int64 VFSVFSFile_tell(VFSVFSFile *self)
{
    if (!self->file)
        return -1;
    return vfsTell(self->file);
}

int64 VFSVFSFile_seek(VFSVFSFile *self, int64 off, int seektype)
{
    if (!self->file)
        return -1;
    return vfsSeek(self->file, off, seektype);
}

bool VFSVFSFile_flush(VFSVFSFile *self)
{
    if (!self->file)
        return false;
    return vfsFlush(self->file);
}

void VFSVFSFile_destroy(VFSVFSFile *self)
{
    VFSVFSFile_close(self);
}

// Autogen begins -----
#include "vfsvfsfile.auto.inc"
// Autogen ends -------
