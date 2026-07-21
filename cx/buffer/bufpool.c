#include "bufpool.h"
#include <cx/utils/compare.h>

// The freelist only ever needs to hold buffers that are not checked out, so it is sized from the
// cap rather than grown without bound. When the cap is unlimited there is no meaningful number to
// use, so pick a large-but-sane ceiling; overflowing it is harmless (see bufpoolPut).
#define BUFPOOL_UNCAPPED_FREELIST 65536

_Use_decl_annotations_
void bufpoolInit(BufPool* pool, size_t bufsz, uint32 initial, uint32 max)
{
    devAssert(bufsz > 0);

    pool->bufsz = bufsz;
    pool->max   = max;
    atomicStore(uint32, &pool->count, 0, Relaxed);

    uint32 fmax = max ? max : BUFPOOL_UNCAPPED_FREELIST;
    if (max)
        initial = min(initial, max);

    prqInitDynamic(&pool->freelist,
                   clamplow(initial, 16),
                   clamplow(initial, 16),
                   clamplow(fmax, 16),
                   PRQ_Grow_100,
                   PRQ_Grow_50);

    for (uint32 i = 0; i < initial; ++i) {
        Buffer buf = bufTryCreate(bufsz);
        if (!buf)
            break;   // could not preallocate the full set; the pool still works, just colder

        if (!prqPush(&pool->freelist, buf)) {
            bufDestroy(&buf);
            break;
        }
        atomicFetchAdd(uint32, &pool->count, 1, Relaxed);
    }
}

_Use_decl_annotations_
Buffer bufpoolGet(BufPool* pool)
{
    Buffer buf = (Buffer)prqPop(&pool->freelist);
    if (buf) {
        buf->len = 0;
        return buf;
    }

    // Nothing free. Claim a slot under the cap before allocating, so that concurrent callers
    // cannot collectively overshoot it.
    if (pool->max) {
        uint32 cur = atomicLoad(uint32, &pool->count, Relaxed);
        do {
            if (cur >= pool->max)
                return NULL;
        } while (!atomicCompareExchange(uint32,
                                        weak,
                                        &pool->count,
                                        &cur,
                                        cur + 1,
                                        AcqRel,
                                        Relaxed));
    } else {
        atomicFetchAdd(uint32, &pool->count, 1, Relaxed);
    }

    buf = bufTryCreate(pool->bufsz);
    if (!buf) {
        // Out of memory rather than out of cap, but the caller cannot do anything different
        // about it, so it is reported the same way. Give the slot back.
        atomicFetchSub(uint32, &pool->count, 1, Relaxed);
        return NULL;
    }

    return buf;
}

_Use_decl_annotations_
void bufpoolPut(BufPool* pool, Buffer* buf)
{
    if (!buf || !*buf)
        return;

    devAssertMsg((*buf)->sz == pool->bufsz, "Returning a wrong-sized buffer to a pool");

    (*buf)->len = 0;

    if (!prqPush(&pool->freelist, *buf)) {
        // The freelist is full and cannot grow. Dropping the buffer is always safe -- it just
        // means the pool will allocate again later if it needs to.
        bufDestroy(buf);
        atomicFetchSub(uint32, &pool->count, 1, Relaxed);
    }

    *buf = NULL;
}

_Use_decl_annotations_
uint32 bufpoolInUse(BufPool* pool)
{
    uint32 total = atomicLoad(uint32, &pool->count, Relaxed);
    uint32 free  = prqCount(&pool->freelist);

    // Both values are snapshots taken at different moments, so the subtraction can go negative
    // under concurrency even though the real answer never is.
    return (total > free) ? total - free : 0;
}

_Use_decl_annotations_
void bufpoolDestroy(BufPool* pool)
{
    Buffer buf;
    while ((buf = (Buffer)prqPop(&pool->freelist)) != NULL) {
        bufDestroy(&buf);
        atomicFetchSub(uint32, &pool->count, 1, Relaxed);
    }

    devAssertMsg(atomicLoad(uint32, &pool->count, Relaxed) == 0,
                 "Destroying a buffer pool with buffers still checked out");

    prqDestroy(&pool->freelist);
    pool->bufsz = 0;
    pool->max   = 0;
}
