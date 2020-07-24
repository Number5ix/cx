#include "hashtable_private.h"
#include "cx/debug/assert.h"

static int32 _htInsertInternal(hashtable *htbl, stgeneric key, stgeneric *val, uint32 flags);

static int npow2(int val)
{
    int i;

    for(i = 0; i < 32; i++)
    {
        if ((1 << i) >= val)
            return 1 << i;
    }
    return 16;
}

hashtable _htCreate(stype keytype, STypeOps *keyops, stype valtype, STypeOps *valops, int32 initsz, uint32 flags)
{
    HashTableHeader *ret;
    uint32 elemsz = clamplow(stGetSize(keytype) + stGetSize(valtype), 8);

    relAssert(stGetSize(keytype) > 0);
    relAssert(stGetSize(valtype) > 0);

    if (initsz == 0)
        initsz = 16;
    else
        initsz = clamplow(initsz, 1);

    // If we are not using custom ops and this is a POD type, skip all of the copy/destructor
    // logic and just use straight memory copies for speed.
    if (!keyops && !(stHasFlag(keytype, Object)))
        flags |= HT_RefKeys;
    if (!valops && !(stHasFlag(valtype, Object)))
        flags |= HT_Ref;

    if ((HT_GET_GROW(flags) & HT_GROW_AT_MASK) != HT_GROW_At50) {
        // At an average table load of 0.5, linear probing is better due to cache.
        // For everything else use quadratic to avoid clustering.
        flags |= HTINT_Quadratic;
    }

    if ((HT_GET_GROW(flags) & HT_GROW_BY_MASK) == HT_GROW_By100 ||
            (HT_GET_GROW(flags) & HT_GROW_BY_MASK) == HT_GROW_By300) {
        // 100% (2x) and 300% (4x) grow settings are special
        // If we set the initial table size to a power of 2, it will always be a
        // power-of-2 sized table even after growing, so we can use fast indexing
        // with masking rather than modulo.
        flags |= HTINT_Pow2;
        initsz = npow2(initsz);
    }

    if (keyops || valops) {
        // need the extended header
        flags |= HTINT_Extended;
        ret = xaAlloc(sizeof(HashTableHeader) + elemsz * initsz);
        memset(&ret->keytypeops, 0, sizeof(ret->keytypeops));
        memset(&ret->valtypeops, 0, sizeof(ret->valtypeops));
    } else {
        // revel in the evil
        ret = (HashTableHeader*)((uintptr)xaAlloc((sizeof(HashTableHeader) + elemsz * initsz) - HT_SMALLHDR_OFFSET) - HT_SMALLHDR_OFFSET);
    }

    ret->slots = initsz;
    ret->used = ret->valid = 0;

    ret->keytype = keytype;
    ret->valtype = valtype;
    ret->flags = flags;

    if (keyops) {
        ret->keytypeops = *keyops;
    }
    if (valops) {
        ret->valtypeops = *valops;
    }

    // fill table with empty sentinel
    for (int32 i = 0; i < initsz; i++) {
        *(uint64*)HTKEY(ret, elemsz, i) = hashEmpty;
    }

    devAssert(_htElemSz(ret) == elemsz);

    return (hashtable)&ret->data[0];
}

static _meta_inline uint32 clampHash(HashTableHeader *hdr, uint32 hash)
{
    if (hdr->flags & HTINT_Pow2)
        return hash & (hdr->slots - 1);
    else
        return hash % hdr->slots;
}

hashtable _htClone(hashtable *htbl, int32 minsz, int32 *origslot, bool move)
{
    HashTableHeader *hdr = HTABLE_HDR(*htbl);
    HashTableHeader *nhdr;
    hashtable ntbl;
    uint32 elemsz = _htElemSz(hdr);

    if (minsz == 0)
        minsz = hdr->slots;

    int32 newsz = clamplow(minsz, hdr->valid + 1);
    if (hdr->flags & HTINT_Pow2)
        newsz = npow2(newsz);

    // make a new table to copy stuff into
    if (hdr->flags & HTINT_Extended) {
        // need the extended header
        nhdr = xaAlloc(sizeof(HashTableHeader) + elemsz * newsz);
    } else {
        nhdr = (HashTableHeader*)((uintptr)xaAlloc((sizeof(HashTableHeader) + elemsz * newsz) - HT_SMALLHDR_OFFSET) - HT_SMALLHDR_OFFSET);
    }

    nhdr->keytype = hdr->keytype;
    nhdr->valtype = hdr->valtype;
    nhdr->slots = newsz;
    nhdr->used = nhdr->valid = 0;

    uint32 origflags = hdr->flags;
    if (move) {
        // neither table should own anything during the copy
        // that way everything is done with straight memcpy
        // this permanently changes the flags on the source table!
        hdr->flags |= HT_Ref | HT_RefKeys;
        nhdr->flags = hdr->flags;
    } else {
        nhdr->flags = hdr->flags;
    }

    if (hdr->flags & HTINT_Extended) {
        nhdr->keytypeops = hdr->keytypeops;
        nhdr->valtypeops = hdr->valtypeops;
    }

    // initialize new table
    for (int32 i = 0; i < newsz; i++) {
        *(uint64*)HTKEY(nhdr, elemsz, i) = hashEmpty;
    }

    ntbl = (hashtable)&nhdr->data[0];
    int32 nslot = -1;
    // copy the stuff
    for (int32 i = 0; i < hdr->slots; i++) {
        uint64 *skey = HTKEY(hdr, elemsz, i);
        if (*skey == hashEmpty || *skey == hashDeleted)
            continue;
        int32 insslot = _htInsertInternal(&ntbl, stStored(hdr->keytype, skey),
                                          stStoredPtr(hdr->valtype, HTVAL(hdr, elemsz, i)), 0);
        if (origslot && *origslot == i)
            nslot = insslot;
    }

    // keep track of a particular slot number if it moved
    if (origslot && nslot != -1)
        *origslot = nslot;

    nhdr = HTABLE_HDR(ntbl);
    if (move) {
        // restore ownership for new table
        nhdr->flags = origflags;
    }

    devAssert(nhdr->valid == hdr->valid);

    return ntbl;
}

static void htResizeTable(hashtable *htbl, int32 newsz, int32 *origslot)
{
    HashTableHeader *hdr = HTABLE_HDR(*htbl);

    *htbl = _htClone(htbl, newsz, origslot, true);

    // free old table
    if (hdr->flags & HTINT_Extended) {
        xaFree(hdr);
    } else {
        void *smbase = (void*)((uintptr_t)hdr + HT_SMALLHDR_OFFSET);
        xaFree(smbase);
    }
}

static void htGrowTable(hashtable *htbl, int32 *origslot)
{
    HashTableHeader *hdr = HTABLE_HDR(*htbl);
    int32 newsz = hdr->slots;

    if (hdr->flags & HTINT_Pow2)
        newsz = npow2(newsz);

    switch (HT_GET_GROW(hdr->flags) & HT_GROW_BY_MASK) {
    case HT_GROW_By100:
        newsz = newsz << 1;
        break;
    case HT_GROW_By200:
        newsz = newsz * 3;
        break;
    case HT_GROW_By300:
        newsz = newsz << 2;
        break;
    case HT_GROW_By50:
        newsz += newsz >> 1;
        break;
    default:
        devFatalError("Invalid hashtable grow amount");
        return;
    }

    htResizeTable(htbl, newsz, origslot);
}

static bool htFindInternal(HashTableHeader *hdr, stgeneric key, int32 *indexOut, int32 *deletedOut)
{
    uint32 elemsz = _htElemSz(hdr);
    uint32 opsflags = (hdr->flags & HT_CaseInsensitive) ? STOPS_CaseInsensitive : 0;
    uint32 probes = 1;

    if (deletedOut)
        *deletedOut = -1;

    uint32 hash = _stHash(hdr->keytype, HDRKEYOPS(hdr), key, opsflags);

    for(;;) {
        hash = clampHash(hdr, hash);
        uint64 *skey = HTKEY(hdr, elemsz, hash);

        if (*skey == hashEmpty) {
            // nothing in this hash slot, just return it
            if (indexOut)
                *indexOut = hash;
            return false;
        }

        if (*skey != hashDeleted) {
            // not deleted, so check the key
            if (_stCmp(hdr->keytype, HDRKEYOPS(hdr), key,
                       stStored(hdr->keytype, skey), opsflags) == 0) {
                // found it!
                if (indexOut)
                    *indexOut = hash;
                return true;
            }
        } else {
            // this is a deleted key, keep searching
            // and cache it if it's the first one
            if (deletedOut && *deletedOut == -1)
                *deletedOut = hash;
        }

        // keep searching
        if (hdr->flags & HTINT_Quadratic) {
            hash += probes * probes;
            probes++;
        } else {
            // linear probing
            hash++;
        }
    }
}

//static _meta_inline void htSetValueInternal(HashTableHeader *hdr, int32 slot, void *val, bool consume)
static void htSetValueInternal(HashTableHeader *hdr, int32 slot, stgeneric *val, bool consume)
{
    if (consume) {
        // special case: if we're consuming, just steal the element instead of deep copying it,
        // even if we're the owner
        memcpy(HTVAL(hdr, _htElemSz(hdr), slot), stGenPtr(hdr->valtype, *val), stGetSize(hdr->valtype));

        // destroy source
        if (hdr->flags & HT_Ref)        // this combination doesn't make much sense, but should respect it
            _stDestroy(hdr->valtype, HDRVALOPS(hdr), val, 0);
        else if (stGetSize(hdr->valtype) == sizeof(void*))
            val->st_ptr = 0;            // if this is a pointer-sized element, clear it out
        return;
    }

    if (!(hdr->flags & HT_Ref))
        _stCopy(hdr->valtype, HDRVALOPS(hdr),
                stStoredPtr(hdr->valtype, HTVAL(hdr, _htElemSz(hdr), slot)), *val, 0);
    else
        memcpy(HTVAL(hdr, _htElemSz(hdr), slot), stGenPtr(hdr->valtype, *val),
               stGetSize(hdr->valtype));
}

static int32 _htInsertInternal(hashtable *htbl, stgeneric key, stgeneric *val, uint32 flags)
{
    HashTableHeader *hdr = HTABLE_HDR(*htbl);
    int32 slot, deleted;
    bool found;

    found = htFindInternal(hdr, key, &slot, &deleted);

    if (found) {
        if (flags & HTFUNC_Ignore) {
            // already exists and set to ignore, so do not set value
            if (flags & HTFUNCINT_Consume)
                _stDestroy(hdr->valtype, HDRVALOPS(hdr), val, 0);
            return slot;
        }

        // replacing existing value, destroy it first if necessary
        if (!(hdr->flags & HT_Ref))
            _stDestroy(hdr->valtype, HDRVALOPS(hdr),
                       stStoredPtr(hdr->valtype, HTVAL(hdr, _htElemSz(hdr), slot)), 0);

        htSetValueInternal(hdr, slot, val, flags & HTFUNCINT_Consume);
        return slot;
    }

    // didn't find it, so pick a new slot
    if (deleted > -1)
        slot = deleted;         // reuse deleted slot
    else
        hdr->used++;

    // set key
    if (!(hdr->flags & HT_RefKeys))
        _stCopy(hdr->keytype, HDRKEYOPS(hdr),
                stStoredPtr(hdr->keytype, HTKEY(hdr, _htElemSz(hdr), slot)), key, 0);
    else
        memcpy(HTKEY(hdr, _htElemSz(hdr), slot), stGenPtr(hdr->keytype, key), stGetSize(hdr->keytype));

    htSetValueInternal(hdr, slot, val, flags & HTFUNCINT_Consume);

    hdr->valid++;

    // check to see if table needs to be grown
    int32 growsz;
    switch (HT_GET_GROW(hdr->flags) & HT_GROW_AT_MASK) {
    case HT_GROW_At50:
        growsz = hdr->slots >> 1;
        break;
    case HT_GROW_At75:
        growsz = (hdr->slots >> 1) + (hdr->slots >> 2);
        break;
    case HT_GROW_At90:
        growsz = hdr->slots * 9 / 10;
        break;
    default:
        devFatalError("Invalid hashtable grow threshold");
        return false;
    }

    if (hdr->used >= growsz)
        htGrowTable(htbl, &slot);

    return slot;
}

htelem _htInsertPtr(hashtable *htbl, stgeneric key, stgeneric *val, uint32 flags)
{
    int32 slot = _htInsertInternal(htbl, key, val, flags);
    if (slot == -1)
        return NULL;

    HashTableHeader *hdr = HTABLE_HDR(*htbl);
    return HTKEY(hdr, _htElemSz(hdr), slot);
}

htelem _htInsert(hashtable *htbl, stgeneric key, stgeneric val, uint32 flags)
{
    return _htInsertPtr(htbl, key, &val, flags);
}

void htClear(hashtable *htbl)
{
    if (!(htbl && *htbl))
        return;

    HashTableHeader *hdr = HTABLE_HDR(*htbl);
    uint32 elemsz = _htElemSz(hdr);

    if (!((hdr->flags & HT_Ref) && (hdr->flags & HT_RefKeys))) {
        for (int32 i = 0; i < hdr->slots; i++) {
            uint64 *skey = HTKEY(hdr, elemsz, i);
            if (*skey != hashEmpty && *skey != hashDeleted) {
                if (!(hdr->flags & HT_RefKeys))
                    _stDestroy(hdr->keytype, HDRKEYOPS(hdr),
                               stStoredPtr(hdr->keytype, HTKEY(hdr, elemsz, i)), 0);
                if (!(hdr->flags & HT_Ref))
                    _stDestroy(hdr->valtype, HDRVALOPS(hdr),
                               stStoredPtr(hdr->valtype, HTVAL(hdr, elemsz, i)), 0);
            }
            *skey = hashEmpty;
        }
    } else {
        for (int32 i = 0; i < hdr->slots; i++) {
            *(uint64*)HTKEY(hdr, elemsz, i) = hashEmpty;
        }
    }
    hdr->valid = 0;
}

void htDestroy(hashtable *htbl)
{
    if (!(htbl && *htbl))
        return;

    htClear(htbl);

    HashTableHeader *hdr = HTABLE_HDR(*htbl);
    if (hdr->flags & HTINT_Extended) {
        xaFree(hdr);
    } else {
        void *smbase = (void*)((uintptr_t)hdr + HT_SMALLHDR_OFFSET);
        xaFree(smbase);
    }
}

void htSetSize(hashtable *htbl, int32 newsz)
{
    HashTableHeader *hdr = HTABLE_HDR(*htbl);

    if (hdr->flags & HTINT_Pow2)
        newsz = npow2(newsz);
    if (newsz < hdr->slots)
        htResizeTable(htbl, newsz, NULL);
}

bool _htFind(hashtable *htbl, stgeneric key, stgeneric *val, uint32 flags)
{
    HashTableHeader *hdr = HTABLE_HDR(*htbl);
    uint32 elemsz = _htElemSz(hdr);
    int32 slot;
    bool found = htFindInternal(hdr, key, &slot, NULL);

    if (found) {
        if (val)
            memcpy(stGenPtr(hdr->valtype, *val), HTVAL(hdr, elemsz, slot), stGetSize(hdr->valtype));
        if ((flags & HTFUNC_Destroy) && !(hdr->flags & HT_Ref))
            _stDestroy(hdr->valtype, HDRVALOPS(hdr),
                       stStoredPtr(hdr->valtype, HTVAL(hdr, elemsz, slot)), 0);
        if (flags & (HTFUNC_Destroy | HTFUNC_RemoveOnly)) {
            if (!(hdr->flags & HT_RefKeys))
                _stDestroy(hdr->keytype, HDRKEYOPS(hdr),
                           stStoredPtr(hdr->keytype, HTKEY(hdr, elemsz, slot)), 0);
            *(uint64*)HTKEY(hdr, elemsz, slot) = hashDeleted;
            hdr->valid--;
        }
    }

    return found;
}

bool _htRemove(hashtable *htbl, stgeneric key)
{
    HashTableHeader *hdr = HTABLE_HDR(*htbl);
    uint32 elemsz = _htElemSz(hdr);
    int32 slot;
    bool found = htFindInternal(hdr, key, &slot, NULL);

    if (found) {
        if (!(hdr->flags & HT_Ref))
            _stDestroy(hdr->valtype, HDRVALOPS(hdr),
                       stStoredPtr(hdr->valtype, HTVAL(hdr, elemsz, slot)), 0);
        if (!(hdr->flags & HT_RefKeys))
            _stDestroy(hdr->keytype, HDRKEYOPS(hdr),
                       stStoredPtr(hdr->keytype, HTKEY(hdr, elemsz, slot)), 0);
        *(uint64*)HTKEY(hdr, elemsz, slot) = hashDeleted;
        hdr->valid--;
    }

    return found;
}

htelem _htFindElem(hashtable *htbl, stgeneric key)
{
    HashTableHeader *hdr = HTABLE_HDR(*htbl);
    uint32 elemsz = _htElemSz(hdr);
    int32 slot;
    bool found = htFindInternal(hdr, key, &slot, NULL);

    if (found)
        return HTKEY(hdr, elemsz, slot);

    return NULL;
}

bool htiInit(htiter *iter, hashtable *htbl)
{
    if (!iter || !htbl)
        return false;

    iter->hdr = HTABLE_HDR(*htbl);
    iter->slot = -1;
    iter->elem = 0;
    return htiNext(iter);
}

bool htiNext(htiter *iter)
{
    if (!(iter && iter->hdr))
        return false;

    HashTableHeader *hdr = iter->hdr;
    uint32 elemsz = _htElemSz(hdr);
    int32 i;

    // find next non-empty and non-deleted slot
    for (i = iter->slot + 1; i < hdr->slots; i++) {
        uint64 *skey = HTKEY(hdr, elemsz, i);

        if (*skey != hashEmpty && *skey != hashDeleted) {
            iter->slot = i;
            iter->elem = (htelem)skey;
            return true;
        }
    }

    iter->slot = hdr->slots;
    iter->elem = NULL;
    return false;
}

void htiFinish(htiter *iter)
{
    memset(iter, 0, sizeof(htiter));
}
