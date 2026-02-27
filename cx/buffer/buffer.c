#include "buffer.h"
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

_Use_decl_annotations_
void bufDestroy(Buffer* buf)
{
    xaDestroy(buf);
}
