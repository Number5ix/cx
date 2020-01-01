#include "hashtable_private.h"
#include "cx/utils/murmur.h"

void stDtor_hashtable(stype st, void *ptr, uint32 flags)
{
    htDestroy((hashtable*)ptr);
}

void stCopy_hashtable(stype st, void *dest, const void *src, uint32 flags)
{
    *(hashtable*)dest = _htClone((hashtable*)src, 0, NULL, false);
}

uint32 stHash_hashtable(stype st, const void *ptr, uint32 flags)
{
    hashtable *htbl = (hashtable*)ptr;
    HashTableHeader *hdr = HTABLE_HDR(*htbl);
    uint32 elemsz = _htElemSz(hdr);
    uint32 ret = 0;

    for (int32 i = 0; i < hdr->slots; i++) {
        uint64 *skey = HTKEY(hdr, elemsz, i);
        if (*skey == hashEmpty || *skey == hashDeleted)
            continue;

        ret ^= _stHash(hdr->keytype, HDRKEYOPS(hdr), HTKEY(hdr, elemsz, i), 0);
        ret ^= _stHash(hdr->valtype, HDRVALOPS(hdr), HTVAL(hdr, elemsz, i), 0);
    }

    return ret;
}
