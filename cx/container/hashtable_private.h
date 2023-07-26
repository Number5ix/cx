#pragma once

#include "hashtable.h"
#include "cx/utils.h"

// The first slot of each chunk is reserved for the chunk metadata.
// This header must fit within the minimum slot size (128 bits)
typedef struct HTChunkHeader {
    uint8 deleted[HT_SLOTS_PER_CHUNK >> 3];     // deleted slot bitmap
    uint8 nalloc;                               // how many slots have been allocated for this chunk so far
} HTChunkHeader;

_Static_assert((HT_SLOTS_PER_CHUNK >> 8) < sizeof(((HTChunkHeader*)0)->nalloc), "HT_SLOTS_PER_CHUNK too high to fit into nalloc");
_Static_assert(sizeof(HTChunkHeader) <= 16, "Hash chunk header too large!");

#define HT_IDXENT_SZ (sizeof(uint32))
#define HT_SLOT_CHUNK(slot) ((slot) >> HT_CHUNK_SHIFT)
#define HT_SLOT_CHUNK_PTR(hdr, slot) (hdr->storage[HT_SLOT_CHUNK(slot)])
#define HT_SLOT_OFF(slot, elemsz) ((uintptr)((slot) & HT_CHUNK_MASK) * elemsz)
#define HT_SLOT_PTR(hdr, elemsz, slot) ((void*)((uintptr)HT_SLOT_CHUNK_PTR(hdr, slot) + HT_SLOT_OFF(slot, elemsz)))
#define HT_SLOT_VAL_PTR(hdr, elemsz, slot) ((void*)((uintptr)HT_SLOT_CHUNK_PTR(hdr, slot) + HT_SLOT_OFF(slot, elemsz) + stGetSize((hdr)->keytype)))

#define HT_DELETED_IDX(slot) ((slot & HT_CHUNK_MASK) >> 3)
#define HT_DELETED_BIT(slot) (1 << (slot & 7))

#define HT_SMALLHDR_OFFSET (offsetof(HashTableHeader, idxsz))
#define HDRKEYOPS(hdr) ((hdr->flags & HTINT_Extended) ? &hdr->keytypeops : NULL)
#define HDRVALOPS(hdr) ((hdr->flags & HTINT_Extended) ? &hdr->valtypeops : NULL)

#define hashIndexDeleted (0xffffffffUL)
#define hashIndexEmpty   (0UL)

uint32 _htNextSlot(HashTableHeader *hdr, uint32 slot);

static _meta_inline uint32 _htElemSz(HashTableHeader *hdr)
{
    return clamplow(stGetSize(hdr->keytype) + stGetSize(hdr->valtype), 8);
}
