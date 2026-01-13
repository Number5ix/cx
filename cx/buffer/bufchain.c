#include "bufchain.h"
#include <cx/utils/compare.h>

_Use_decl_annotations_
void bufChainInit(BufChain* chain, size_t segsz)
{
    chain->head  = NULL;
    chain->tail  = NULL;
    chain->total = 0;
    chain->segsz = clamplow(segsz, 64);  // minimum segment size is 64 bytes
}

static _meta_inline size_t nodeReadAvail(BufChainNode* node)
{
    if (node->head <= node->tail && !node->full) {
        return node->tail - node->head;
    } else {
        return node->sz - node->head + node->tail;
    }
}

static _meta_inline size_t nodeWriteAvail(BufChainNode* node)
{
    if (node->head <= node->tail && !node->full) {
        return node->sz - node->tail + node->head;
    } else {
        return node->head - node->tail;
    }
}

static _meta_inline void moveReadHead(BufChain* chain, size_t bytes)
{
    size_t remaining = bytes;

    while (chain->head && remaining > 0 && chain->total > 0) {
        BufChainNode* node = chain->head;
        size_t nodeAvail   = nodeReadAvail(node);
        if (remaining < nodeAvail) {
            node->head = (node->head + remaining) % node->sz;
            node->full = false;
            chain->total -= remaining;
            remaining = 0;
        } else {
            remaining -= nodeAvail;
            chain->total -= nodeAvail;

            // only remove the node if it's not the last in the chain
            // we keep the last node to use for ringbuffer writes
            if (node->next) {
                chain->head = node->next;
                xaFree(node->data);
                xaFree(node);
            } else {
                // If the buffer is empty, reset it to the start of the ring.
                // This helps avoid having to split reads/writes.
                node->head = node->tail = 0;
                node->full              = false;
            }
        }
    }
}

static _meta_inline void readOutputHelper(uint8* in, size_t count, size_t skip, uint8* buf,
                                          uint8** p, bufChainZCCB cb, void* ctx, bool* movehead)
{
    if (count > skip) {
        if (buf) {
            memcpy(*p, in + skip, count - skip);
            *p += count - skip;
        } else if (cb) {
            *movehead &= cb(in + skip, count - skip, ctx);
        }
    }
}

// Force inline to allow compiler to optimize out unused code paths based on hardcoded parameters
// for each use case
static _meta_inline size_t readCommon(BufChain* chain,
                                      _Out_writes_bytes_opt_(skip + bsz) uint8* buf, size_t skip,
                                      size_t bsz, bufChainZCCB cb, void* ctx, bool movehead)
{
    BufChainNode* node = chain->head;
    size_t avail       = chain->total;
    size_t total       = min(skip + bsz, avail);
    size_t nread       = 0;
    uint8* p           = buf;

    while (node && total > 0 && avail > 0) {
        size_t count = total;

        if (node->head <= node->tail && !node->full) {
            // buffer is contiguous, can get it all at once
            count = min(count, node->tail - node->head);
            readOutputHelper(node->data + node->head, count, skip, buf, &p, cb, ctx, &movehead);

            nread += (count > skip) ? (count - skip) : 0;
            skip -= min(count, skip);
            avail -= count;
            total -= count;
        } else {
            // have to split read

            // first read from head to end of buffer
            count = min(count, node->sz - node->head);
            readOutputHelper(node->data + node->head, count, skip, buf, &p, cb, ctx, &movehead);
            nread += (count > skip) ? (count - skip) : 0;
            skip -= min(count, skip);
            avail -= count;
            total -= count;

            // now read from start of buffer to tail
            count = min(total, node->tail);
            readOutputHelper(node->data, count, skip, buf, &p, cb, ctx, &movehead);
            nread += (count > skip) ? (count - skip) : 0;
            skip -= min(count, skip);
            avail -= count;
            total -= count;
        }

        // get remaining data from next node
        node = node->next;
    }

    if (movehead)
        moveReadHead(chain, skip + bsz);

    return nread;
}

_Use_decl_annotations_
size_t bufChainRead(BufChain* chain, uint8* buf, size_t bytes)
{
    return readCommon(chain, buf, 0, bytes, NULL, NULL, true);
}

_Use_decl_annotations_
size_t bufChainPeek(_Inout_ BufChain* chain, uint8* buf, size_t off, size_t bytes)
{
    return readCommon(chain, buf, off, bytes, NULL, NULL, false);
}

_Use_decl_annotations_
size_t bufChainReadZC(_Inout_ BufChain* chain, size_t bytes, bufChainZCCB cb, void* ctx)
{
    return readCommon(chain, NULL, 0, bytes, cb, ctx, true);
}

_Use_decl_annotations_
void bufChainWrite(BufChain* chain, const uint8* buf, size_t bytes)
{
    size_t remaining = bytes;
    const uint8* p   = buf;

    while (remaining > 0) {
        BufChainNode* node = chain->tail;
        if (!node || nodeWriteAvail(node) == 0) {
            // need a new node
            node       = xaAllocStruct(BufChainNode);
            node->next = NULL;
            node->data = xaAlloc(chain->segsz);
            node->sz   = chain->segsz;
            node->head = 0;
            node->tail = 0;
            node->full = false;
            if (chain->tail) {
                chain->tail->next = node;
                chain->tail       = node;
            } else {
                chain->head = chain->tail = node;
            }
        }

        size_t canwrite = nodeWriteAvail(node);
        size_t count    = (remaining < canwrite) ? remaining : canwrite;

        // write the data
        if (node->tail + count <= node->sz) {
            // can write in one go
            memcpy(node->data + node->tail, p, count);
            node->tail = (node->tail + count) % node->sz;
        } else {
            // have to split the write
            size_t firstPart = node->sz - node->tail;
            memcpy(node->data + node->tail, p, firstPart);
            size_t secondPart = count - firstPart;
            memcpy(node->data, p + firstPart, secondPart);
            node->tail = secondPart;
        }

        // we filled up the node
        if (node->head == node->tail)
            node->full = true;

        chain->total += count;
        remaining -= count;
        p += count;
    }
}

_Use_decl_annotations_
void bufChainWriteZC(BufChain* chain, uint8* data, size_t size, size_t bytes)
{
    BufChainNode* node = xaAllocStruct(BufChainNode);
    node->next         = NULL;
    node->data         = data;
    node->sz           = size;
    node->head         = 0;
    node->tail         = bytes % size;
    node->full         = (size == bytes);
    devAssert(bytes <= size);

    if (chain->tail) {
        chain->tail->next = node;
        chain->tail       = node;
    } else {
        chain->head = chain->tail = node;
    }

    chain->total += bytes;
}

_Use_decl_annotations_
size_t bufChainSkip(BufChain* chain, size_t bytes)
{
    size_t toSkip = min(bytes, chain->total);
    moveReadHead(chain, toSkip);
    return toSkip;
}

_Use_decl_annotations_
void bufChainDestroy(BufChain* chain)
{
    BufChainNode* node = chain->head;
    while (node) {
        BufChainNode* next = node->next;
        xaFree(node->data);
        xaFree(node);
        node = next;
    }
    chain->head  = NULL;
    chain->tail  = NULL;
    chain->total = 0;
    chain->segsz = 0;
}