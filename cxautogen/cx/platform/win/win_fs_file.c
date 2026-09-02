// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "win_fs_file.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cx/platform/win.h"
#include "cx/utils/compare.h"
#include "win_fs.h"

// biggest I/O request the OS will let us do
// using a safe value for XP
#define MAX_TRANSFER_SIZE (16 * 1024 * 1024)

_Use_decl_annotations_
FSFile* fsOpen(strref path, flags_t flags)
{
    DWORD access = 0;
    DWORD share  = 0;
    DWORD disp   = 0;

    if (flags & FS_Read)
        access |= GENERIC_READ;
    if (flags & FS_Write)
        access |= GENERIC_WRITE;

    if ((flags & FS_Create) && (flags & FS_Truncate))
        disp = CREATE_ALWAYS;
    else if (flags & FS_Create)
        disp = OPEN_ALWAYS;
    else if (flags & FS_Truncate)
        disp = TRUNCATE_EXISTING;
    else
        disp = OPEN_EXISTING;

    if (flags & FS_Lock)
        share = FILE_SHARE_READ;
    else
        share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

    HANDLE handle = CreateFileW(fsPathToNT(path),
                                access,
                                share,
                                NULL,
                                disp,
                                FILE_ATTRIBUTE_NORMAL,
                                NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        winMapLastError();
        return NULL;
    }

    return File(fsfilewinCreate(handle));
}

_objfactory_guaranteed FSFileWin* FSFileWin_create(HANDLE h)
{
    FSFileWin* self;
    self = objInstCreate(FSFileWin);

    self->h = h;

    objInstInit(self);
    return self;
}

bool FSFileWin_close(_In_ FSFileWin* self)
{
    if (!self->h)
        return true;   // already closed

    bool ret = true;
    if (!CloseHandle(self->h))
        ret = winMapLastError();
    self->h = NULL;
    return ret;
}

bool FSFileWin_read(_In_ FSFileWin* self, _Out_writes_bytes_to_(sz, *bytesread) void* buf,
                    size_t sz, _Out_ _Deref_out_range_(0, sz) size_t* bytesread)
{
    DWORD didread = 0;

    if (!self->h) {
        *bytesread = 0;
        return false;
    }

    if (sz < MAX_TRANSFER_SIZE) {
        // fast path, can do it in a single call
        if (!ReadFile(self->h, buf, (DWORD)sz, &didread, NULL)) {
            *bytesread = 0;
            return winMapLastError();
        }

        *bytesread = didread;
        return true;
    }

    // have to break it up into smaller chunks
    size_t actuallyread = 0;
    uint8* bufp         = (uint8*)buf;
    while (sz > 0) {
        if (!ReadFile(self->h, bufp, (DWORD)clamphigh(sz, MAX_TRANSFER_SIZE), &didread, NULL)) {
            *bytesread = 0;
            return winMapLastError();
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

bool FSFileWin_write(_In_ FSFileWin* self, _In_reads_bytes_(sz) const void* buf, size_t sz,
                     _Out_opt_ _Deref_out_range_(0, sz) size_t* byteswritten)
{
    DWORD didwrite = 0;

    if (!self->h) {
        if (byteswritten)
            *byteswritten = 0;
        return false;
    }

    if (sz < MAX_TRANSFER_SIZE) {
        // fast path, can do it in a single call
        if (!WriteFile(self->h, buf, (DWORD)sz, &didwrite, NULL)) {
            if (byteswritten)
                *byteswritten = 0;
            return winMapLastError();
        }

        if (byteswritten)
            *byteswritten = didwrite;
        return true;
    }

    // have to break it up into smaller chunks
    size_t actuallywrote = 0;
    const uint8* bufp    = (const uint8*)buf;
    while (sz > 0) {
        if (!WriteFile(self->h, bufp, (DWORD)clamphigh(sz, MAX_TRANSFER_SIZE), &didwrite, NULL)) {
            if (byteswritten)
                *byteswritten = 0;
            return winMapLastError();
        }

        bufp += didwrite;
        actuallywrote += didwrite;
        sz -= didwrite;
    }

    if (byteswritten)
        *byteswritten = actuallywrote;
    return true;
}

int64 FSFileWin_tell(_In_ FSFileWin* self)
{
    LARGE_INTEGER zero = { 0 };
    LARGE_INTEGER out;

    if (!self->h)
        return -1;

    if (!SetFilePointerEx(self->h, zero, &out, FILE_CURRENT)) {
        winMapLastError();
        return -1;
    }

    return out.QuadPart;
}

int64 FSFileWin_seek(_In_ FSFileWin* self, int64 off, FSSeekType seektype)
{
    LARGE_INTEGER move;
    LARGE_INTEGER out;
    DWORD method;

    if (!self->h)
        return -1;

    move.QuadPart = off;
    switch (seektype) {
    case FS_Set:
        method = FILE_BEGIN;
        break;
    case FS_Cur:
        method = FILE_CURRENT;
        break;
    case FS_End:
        method = FILE_END;
        break;
    default:
        return -1;
    }

    if (!SetFilePointerEx(self->h, move, &out, method)) {
        winMapLastError();
        return -1;
    }

    return out.QuadPart;
}

bool FSFileWin_flush(_In_ FSFileWin* self)
{
    if (!self->h)
        return false;

    if (!FlushFileBuffers(self->h))
        return winMapLastError();

    return true;
}

void FSFileWin_destroy(_In_ FSFileWin* self)
{
    FSFileWin_close(self);
}

// Autogen begins -----
// clang-format off
#include "win_fs_file.auto.inc"
// clang-format on
// Autogen ends -------
