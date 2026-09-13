// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "platform/wasm/wasm_fs_file.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cx/fs/fs_private.h"
#include "cx/platform/unix.h"
#include "cx/utils/compare.h"

#include <fcntl.h>
#include <unistd.h>

// biggest I/O request the OS will let us do
// BSD doesn't need this but Linux does
#define MAX_TRANSFER_SIZE (1024 * 1024 * 1024)

FSFile* fsOpen(strref path, flags_t flags)
{
    int oflags   = 0;
    string npath = 0;

    strDup(&npath, path);
    pathNormalize(&npath);
    pathToPlatform(&npath, npath);

    if ((flags & FS_Read) && (flags & FS_Write))
        oflags = O_RDWR;
    else if (flags & FS_Read)
        oflags = O_RDONLY;
    else if (flags & FS_Write)
        oflags = O_WRONLY;

    if (flags & FS_Create)
        oflags |= O_CREAT;
    if (flags & FS_Truncate)
        oflags |= O_TRUNC;

    int fd = open(strC(npath), oflags, 0644);
    if (fd < 0) {
        unixMapErrno();
        strDestroy(&npath);
        return NULL;
    }

    FSFileWasm* ret = fsfilewasmCreate(fd);
    strDestroy(&npath);
    return File(ret);
}

_objfactory_guaranteed FSFileWasm* FSFileWasm_create(int fd)
{
    FSFileWasm* self;
    self = objInstCreate(FSFileWasm);

    self->fd = fd;

    objInstInit(self);
    return self;
}

bool FSFileWasm_closeHandle(_In_ FSFileWasm* self)
{
    if (self->fd < 0)
        return true;   // already closed

    bool ret = true;
    if (close(self->fd) != 0)
        ret = unixMapErrno();
    self->fd = -1;
    return ret;
}

bool FSFileWasm_read(_In_ FSFileWasm* self, _Out_writes_bytes_to_(sz, *bytesread) void* buf,
                     size_t sz, _Out_ _Deref_out_range_(0, sz) size_t* bytesread)
{
    ssize_t didread = 0;

    if (self->fd < 0) {
        *bytesread = 0;
        return false;
    }

    if (sz < MAX_TRANSFER_SIZE) {
        // fast path, can do it in a single call
        didread = read(self->fd, buf, sz);
        if (didread < 0) {
            *bytesread = 0;
            return unixMapErrno();
        }

        *bytesread = (size_t)didread;
        return true;
    }

    // have to break it up into smaller chunks
    size_t actuallyread = 0;
    uint8* bufp         = (uint8*)buf;
    while (sz > 0) {
        didread = read(self->fd, bufp, clamphigh(sz, MAX_TRANSFER_SIZE));
        if (didread < 0) {
            *bytesread = 0;
            return unixMapErrno();
        }
        if (didread == 0)   // EOF
            break;

        bufp += didread;
        actuallyread += didread;
        sz -= didread;
    }

    *bytesread = actuallyread;
    return true;
}

bool FSFileWasm_write(_In_ FSFileWasm* self, _In_reads_bytes_(sz) const void* buf, size_t sz,
                      _Out_opt_ _Deref_out_range_(0, sz) size_t* byteswritten)
{
    ssize_t didwrite = 0;

    if (self->fd < 0) {
        if (byteswritten)
            *byteswritten = 0;
        return false;
    }

    if (sz < MAX_TRANSFER_SIZE) {
        // fast path, can do it in a single call
        didwrite = write(self->fd, buf, sz);
        if (didwrite < 0) {
            if (byteswritten)
                *byteswritten = 0;
            return unixMapErrno();
        }

        if (byteswritten)
            *byteswritten = didwrite;
        return true;
    }

    // have to break it up into smaller chunks
    size_t actuallywrote = 0;
    const uint8* bufp    = (const uint8*)buf;
    while (sz > 0) {
        didwrite = write(self->fd, bufp, clamphigh(sz, MAX_TRANSFER_SIZE));
        if (didwrite < 0) {
            if (byteswritten)
                *byteswritten = 0;
            return unixMapErrno();
        }

        bufp += didwrite;
        actuallywrote += didwrite;
        sz -= didwrite;
    }

    if (byteswritten)
        *byteswritten = actuallywrote;
    return true;
}

int64 FSFileWasm_tell(_In_ FSFileWasm* self)
{
    off_t off;

    if (self->fd < 0)
        return -1;

    off = lseek(self->fd, 0, SEEK_CUR);
    if (off < 0) {
        unixMapErrno();
        return -1;
    }

    return off;
}

int64 FSFileWasm_seek(_In_ FSFileWasm* self, int64 off, FSSeekType seektype)
{
    int method;
    off_t out;

    if (self->fd < 0)
        return -1;

    switch (seektype) {
    case FS_Set:
        method = SEEK_SET;
        break;
    case FS_Cur:
        method = SEEK_CUR;
        break;
    case FS_End:
        method = SEEK_END;
        break;
    default:
        return -1;
    }

    out = lseek(self->fd, (off_t)off, method);
    if (out < 0) {
        unixMapErrno();
        return -1;
    }

    return out;
}

bool FSFileWasm_flush(_In_ FSFileWasm* self)
{
    if (self->fd < 0)
        return false;

    if (fsync(self->fd) == -1)
        return unixMapErrno();

    return true;
}

void FSFileWasm_destroy(_In_ FSFileWasm* self)
{
    FSFileWasm_closeHandle(self);
}

// Autogen begins -----
// clang-format off
#include "platform/wasm/wasm_fs_file.auto.inc"
// clang-format on
// Autogen ends -------
