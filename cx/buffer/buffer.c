#include "buffer.h"
#include "cx/debug/assert.h"
#include "cx/utils/compare.h"

#define BUFFER_HEADER_SZ offsetof(struct BufferHeader, data)

_Use_decl_annotations_
Buffer bufCreate(size_t size)
{
    Buffer out = xaAlloc(size + BUFFER_HEADER_SZ);

    out->sz  = size;
    out->len = 0;
    return out;
}

// may fail, uses optional allocation
_Use_decl_annotations_
Buffer bufTryCreate(size_t size)
{
    Buffer out = xaAlloc(size + BUFFER_HEADER_SZ, XA_Opt);
    if (!out)
        return NULL;

    out->sz  = size;
    out->len = 0;
    return out;
}

_Use_decl_annotations_
void bufResize(Buffer* buf, size_t newsize)
{
    if (!(*buf)) {
        *buf = bufCreate(newsize);
        return;
    }

    if (newsize == (*buf)->sz)
        return;

    xaResize(buf, newsize + BUFFER_HEADER_SZ);
    (*buf)->sz  = newsize;
    (*buf)->len = min((*buf)->len, newsize);
}

_Use_decl_annotations_
bool bufTryResize(Buffer* buf, size_t newsize)
{
    if (!(*buf)) {
        *buf = bufTryCreate(newsize);
        return (*buf) != NULL;
    }

    if (newsize == (*buf)->sz)
        return true;

    if (!xaResize(buf, newsize + BUFFER_HEADER_SZ, XA_Opt))
        return false;

    (*buf)->sz  = newsize;
    (*buf)->len = min((*buf)->len, newsize);
    return true;
}

// Grows a buffer geometrically rather than to exactly what is needed, so a run of small appends
// costs a handful of reallocations instead of one per append.
static void bufGrow(_Inout_ Buffer* buf, size_t need)
{
    size_t sz  = *buf ? (*buf)->sz : 0;
    size_t len = *buf ? (*buf)->len : 0;

    if (*buf && sz - len >= need)
        return;

    relAssertMsg(need <= (size_t)-1 - len, "Buffer append overflows");

    size_t want = len + need;
    size_t nsz  = sz ? sz : 32;
    while (nsz < want) {
        // stop doubling rather than wrapping; the exact size still satisfies the request
        if (nsz > ((size_t)-1) / 2) {
            nsz = want;
            break;
        }
        nsz *= 2;
    }

    bufResize(buf, nsz);
}

_Use_decl_annotations_
uint8* bufReserve(Buffer* buf, size_t len)
{
    bufGrow(buf, len);
    return (*buf)->data + (*buf)->len;
}

_Use_decl_annotations_
void bufAppendBytes(Buffer* buf, const void* data, size_t len)
{
    uint8* dest = bufReserve(buf, len);
    if (len > 0 && data)
        memcpy(dest, data, len);
    (*buf)->len += len;
}

_Use_decl_annotations_
void bufAppend(Buffer* buf, Buffer src)
{
    bufAppendBytes(buf, src ? src->data : NULL, src ? src->len : 0);
}

_Use_decl_annotations_
void bufAppendC(Buffer* buf, Buffer* src)
{
    // Handing the memory over is only right when there is nothing in front of it to preserve.
    if (!*buf || (*buf)->len == 0) {
        bufDestroy(buf);
        *buf = *src;
        *src = NULL;
        return;
    }

    bufAppend(buf, *src);
    bufDestroy(src);
}

_Use_decl_annotations_
void bufDestroy(Buffer* buf)
{
    xaDestroy(buf);
}
