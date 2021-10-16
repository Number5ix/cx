#pragma once

#include "hashtable.h"
#include "cx/utils.h"

#define HTDATA(hdr) ((void*)((uintptr)(&(hdr)->data[0]) + sizeof(HashTableHeader)))
#define HTKEY(hdr, elemsz, slot) ((void*)((uintptr)(&(hdr)->data[0]) + slot*elemsz))
#define HTVAL(hdr, elemsz, slot) ((void*)((uintptr)(&(hdr)->data[0]) + slot*elemsz + stGetSize((hdr)->keytype)))
#define HT_SMALLHDR_OFFSET (offsetof(HashTableHeader, slots))

#define HDRKEYOPS(hdr) ((hdr->flags & HTINT_Extended) ? &hdr->keytypeops : NULL)
#define HDRVALOPS(hdr) ((hdr->flags & HTINT_Extended) ? &hdr->valtypeops : NULL)

// sentinel values that are extremely unlikely to occur in a key/data pair
#define hashDeleted (0xdeadfacebeeff00dULL)
#define hashEmpty (0xc0ded00d4badfadeULL)

static _meta_inline uint32 _htElemSz(HashTableHeader *hdr)
{
    return clamplow(stGetSize(hdr->keytype) + stGetSize(hdr->valtype), 8);
}

hashtable _htClone(hashtable htbl, int32 minsz, int32 *origslot, bool move);
