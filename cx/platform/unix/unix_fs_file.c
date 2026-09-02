// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "platform/unix/unix_fs_file.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cx/fs/fs_private.h"
#include "cx/debug/error.h"
#include "cx/platform/unix.h"
#include "cx/utils/compare.h"

#include <sys/file.h>
#include <errno.h>
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

    int lockop = 0;
    if (flags & FS_Lock)
        lockop = LOCK_EX;   // for locking the file we need exclusive access
    else if (flags & FS_Write)
        lockop = LOCK_SH;   // for writing, need a shared lock

    // do we need a lock
    if (lockop != 0) {
        if (flock(fd, lockop | LOCK_NB) == EWOULDBLOCK) {
            // could not get the lock
            cxerr = CX_AccessDenied;
            close(fd);
        }
        // other types of failures like locking not supported on the
        // filesystem are silently ignored; not a great way to handle it
        // but the alternative is to just not work at all in those cases
    }

    FSFileUnix* ret = fsfileunixCreate(fd, lockop != 0);
    strDestroy(&npath);
    return File(ret);
}

_objfactory_guaranteed FSFileUnix* FSFileUnix_create(int fd, bool locked)
{
    FSFileUnix* self;
    self = objInstCreate(FSFileUnix);

    self->fd     = fd;
    self->locked = locked;

    objInstInit(self);
    return self;
}

bool FSFileUnix_close(_In_ FSFileUnix* self)
{
    if (self->fd < 0)
        return true;   // already closed

    bool ret = true;
    if (self->locked)
        flock(self->fd, LOCK_UN);
    if (close(self->fd) != 0)
        ret = unixMapErrno();
    self->fd = -1;
    return ret;
}

bool FSFileUnix_read(_In_ FSFileUnix* self, _Out_writes_bytes_to_(sz, *bytesread) void* buf,
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

bool FSFileUnix_write(_In_ FSFileUnix* self, _In_reads_bytes_(sz) const void* buf, size_t sz,
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

int64 FSFileUnix_tell(_In_ FSFileUnix* self)
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

int64 FSFileUnix_seek(_In_ FSFileUnix* self, int64 off, FSSeekType seektype)
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

bool FSFileUnix_flush(_In_ FSFileUnix* self)
{
    if (self->fd < 0)
        return false;

    if (fsync(self->fd) == -1)
        return unixMapErrno();

    return true;
}

void FSFileUnix_destroy(_In_ FSFileUnix* self)
{
    FSFileUnix_close(self);
}

// Autogen begins -----
// clang-format off
#include "platform/unix/unix_fs_file.auto.inc"
// clang-format on
// Autogen ends -------
