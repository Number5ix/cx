#include "hashtable_private.h"
#include "cx/utils/murmur.h"

void stDtor_hashtable(stype st, stgeneric *gen, uint32 flags)
{
    htRelease(&gen->st_hashtable);
}

void stCopy_hashtable(stype st, stgeneric *dest, stgeneric src, uint32 flags)
{
    dest->st_hashtable = htAcquire(src.st_hashtable);
}

uint32 stHash_hashtable(stype st, stgeneric gen, uint32 flags)
{
    hashtable *htbl = &gen.st_hashtable;
    HashTableHeader *hdr = HTABLE_HDR(*htbl);
    uint32 elemsz = _htElemSz(hdr);
    uint32 ret = 0;

    for (int32 i = 0; i < hdr->slots; i++) {
        uint64 *skey = HTKEY(hdr, elemsz, i);
        if (*skey == hashEmpty || *skey == hashDeleted)
            continue;

        ret ^= _stHash(hdr->keytype, HDRKEYOPS(hdr),
                       stStored(hdr->keytype, HTKEY(hdr, elemsz, i)), 0);
        ret ^= _stHash(hdr->valtype, HDRVALOPS(hdr),
                       stStored(hdr->valtype, HTVAL(hdr, elemsz, i)), 0);
    }

    return ret;
}
