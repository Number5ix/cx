#include "stype_buffer.h"
#include "buffer.h"
#include "cx/utils/compare.h"
#include "cx/utils/murmur.h"

_Use_decl_annotations_
void stDtor_buffer(stype st, stgeneric* stgen, flags_t flags)
{
    bufDestroy(&stgen->st_buffer);
}

_Use_decl_annotations_
void stCopy_buffer(stype st, stgeneric* dest, stgeneric src, flags_t flags)
{
    dest->st_buffer = bufCreate(src.st_buffer->sz);
    memcpy(dest->st_buffer->data, src.st_buffer->data, src.st_buffer->len);
    dest->st_buffer->len = src.st_buffer->len;
}

_Use_decl_annotations_
intptr stCmp_buffer(stype st, stgeneric stgen1, stgeneric stgen2, uint32 flags)
{
    if (flags & ST_Equality && stgen1.st_buffer->len != stgen2.st_buffer->len) {
        // early out on length mismatch
        return 1;
    }

    size_t minlen = min(stgen1.st_buffer->len, stgen2.st_buffer->len);
    return memcmp(stgen1.st_buffer->data, stgen2.st_buffer->data, minlen);
}

_Use_decl_annotations_
uint32 stHash_buffer(stype st, _In_ stgeneric stgen, flags_t flags)
{
    return hashMurmur3(stgen.st_buffer->data, stgen.st_buffer->len);
}
