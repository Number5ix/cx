#pragma once

#include "cx/utils.h"
#include "hashtable.h"

typedef struct HTChunkInfo {
    uint8 deleted[HT_SLOTS_PER_CHUNK >> 3];   // deleted slot bitmap
    uint8 nalloc;   // how many slots have been allocated for this chunk so far
} HTChunkInfo;

// Enable metadata for tables with at least this many index entries
#define HT_METADATA_THRESHOLD 256

_Static_assert((HT_SLOTS_PER_CHUNK >> 8) < sizeof(((HTChunkInfo*)0)->nalloc),
               "HT_SLOTS_PER_CHUNK too high to fit into nalloc");

#define HT_IDXENT_SZ                     (sizeof(uint32))
#define HT_IDXENTWITHMETA_SZ             (HT_IDXENT_SZ + sizeof(uint8))
#define HT_SLOT_CHUNK(slot)              ((slot) >> HT_CHUNK_SHIFT)
#define HT_SLOT_KEY_CHUNK_PTR(hdr, slot) ((uintptr)hdr->keystorage[HT_SLOT_CHUNK(slot)])
#define HT_SLOT_VAL_CHUNK_PTR(hdr, slot) ((uintptr)hdr->valstorage[HT_SLOT_CHUNK(slot)])
#define HT_SLOT_OFF(slot, elemsz)        ((uintptr)((slot) & HT_CHUNK_MASK) * elemsz)
#define HT_SLOT_KEY_PTR(hdr, slot)              \
    ((void*)(HT_SLOT_KEY_CHUNK_PTR(hdr, slot) + \
             (size_t)((slot) & HT_CHUNK_MASK) * stGetSize(hdr->keytype)))
#define HT_SLOT_VAL_PTR(hdr, slot)              \
    ((void*)(HT_SLOT_VAL_CHUNK_PTR(hdr, slot) + \
             (size_t)((slot) & HT_CHUNK_MASK) * stGetSize(hdr->valtype)))

// Number of storage chunks currently allocated for a table. This is NOT
// ceil(storused / HT_SLOTS_PER_CHUNK): _htNewSlot allocates the next chunk as soon as
// storused lands exactly on a chunk boundary, so the chunk holding slot (storused - 1)
// is never the last one allocated once storused is a multiple of HT_SLOTS_PER_CHUNK.
// Anything walking the chunk arrays to free them must use this, or it silently drops
// the trailing chunk whenever the table happens to end on a boundary.
#define HT_CHUNK_COUNT(hdr) (HT_SLOT_CHUNK((hdr)->storused) + 1)

#define HT_METADATA(hdr) ((uint8*)(&hdr->index[hdr->idxsz]))

#define HT_DELETED_IDX(slot) ((slot & HT_CHUNK_MASK) >> 3)
#define HT_DELETED_BIT(slot) (1 << (slot & 7))

#define hashIndexDeleted (0xffffffffUL)
#define hashIndexEmpty   (0UL)

uint32 _htNextSlot(_Inout_ HashTableHeader* hdr, uint32 slot);
