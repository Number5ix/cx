#include "bufring.h"
#include <cx/utils/compare.h>

_Use_decl_annotations_
void bufringInit(BufRing* ring, size_t segsz)
{
    ring->head  = NULL;
    ring->tail  = NULL;
    ring->total = 0;
    ring->segsz = clamplow(segsz, 64);   // minimum segment size is 64 bytes
}

static _meta_inline size_t nodeReadAvail(BufRingNode* node)
{
    if (node->head <= node->tail && !node->full) {
        return node->tail - node->head;
    } else {
        return node->buf->sz - node->head + node->tail;
    }
}

static _meta_inline size_t nodeWriteAvail(BufRingNode* node)
{
    if (node->head <= node->tail && !node->full) {
        return node->buf->sz - node->tail + node->head;
    } else {
        return node->head - node->tail;
    }
}

static _meta_inline void moveReadHead(BufRing* ring, size_t bytes)
{
    size_t remaining = bytes;

    while (ring->head && remaining > 0 && ring->total > 0) {
        BufRingNode* node = ring->head;
        size_t nodeAvail  = nodeReadAvail(node);
        if (remaining < nodeAvail) {
            node->head = (node->head + remaining) % node->buf->sz;
            node->full = false;
            ring->total -= remaining;
            remaining = 0;
        } else {
            remaining -= nodeAvail;
            ring->total -= nodeAvail;

            // only remove the node if it's not the last in the chain
            // we keep the last node to use for ringbuffer writes
            if (node->next) {
                ring->head = node->next;
                bufDestroy(&node->buf);
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
                                          uint8** p, bufringZCCB cb, void* ctx, bool* movehead)
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
static _meta_inline size_t readCommon(BufRing* ring, _Out_writes_bytes_opt_(skip + bsz) uint8* buf,
                                      size_t skip, size_t bsz, bufringZCCB cb, void* ctx,
                                      bool movehead)
{
    BufRingNode* node = ring->head;
    size_t avail      = ring->total;
    size_t total      = min(skip + bsz, avail);
    size_t nread      = 0;
    uint8* p          = buf;

    while (node && total > 0 && avail > 0) {
        size_t count = total;

        if (node->head <= node->tail && !node->full) {
            // buffer is contiguous, can get it all at once
            count = min(count, node->tail - node->head);
            readOutputHelper(node->buf->data + node->head,
                             count,
                             skip,
                             buf,
                             &p,
                             cb,
                             ctx,
                             &movehead);

            nread += (count > skip) ? (count - skip) : 0;
            skip -= min(count, skip);
            avail -= count;
            total -= count;
        } else {
            // have to split read

            // first read from head to end of buffer
            count = min(count, node->buf->sz - node->head);
            readOutputHelper(node->buf->data + node->head,
                             count,
                             skip,
                             buf,
                             &p,
                             cb,
                             ctx,
                             &movehead);
            nread += (count > skip) ? (count - skip) : 0;
            skip -= min(count, skip);
            avail -= count;
            total -= count;

            // now read from start of buffer to tail
            count = min(total, node->tail);
            readOutputHelper(node->buf->data, count, skip, buf, &p, cb, ctx, &movehead);
            nread += (count > skip) ? (count - skip) : 0;
            skip -= min(count, skip);
            avail -= count;
            total -= count;
        }

        // get remaining data from next node
        node = node->next;
    }

    if (movehead)
        moveReadHead(ring, skip + bsz);

    return nread;
}

_Use_decl_annotations_
size_t bufringRead(BufRing* ring, uint8* buf, size_t bytes)
{
    return readCommon(ring, buf, 0, bytes, NULL, NULL, true);
}

_Use_decl_annotations_
size_t bufringPeek(_Inout_ BufRing* ring, uint8* buf, size_t off, size_t bytes)
{
    return readCommon(ring, buf, off, bytes, NULL, NULL, false);
}

_Use_decl_annotations_
size_t bufringReadZC(_Inout_ BufRing* ring, size_t bytes, bufringZCCB cb, void* ctx)
{
    return readCommon(ring, NULL, 0, bytes, cb, ctx, true);
}

_Use_decl_annotations_
void bufringWrite(BufRing* ring, const uint8* buf, size_t bytes)
{
    size_t remaining = bytes;
    const uint8* p   = buf;

    while (remaining > 0) {
        BufRingNode* node = ring->tail;
        if (!node || nodeWriteAvail(node) == 0) {
            // need a new node
            node       = xaAllocStruct(BufRingNode);
            node->next = NULL;
            node->buf  = bufCreate(ring->segsz);
            node->head = 0;
            node->tail = 0;
            node->full = false;
            if (ring->tail) {
                ring->tail->next = node;
                ring->tail       = node;
            } else {
                ring->head = ring->tail = node;
            }
        }

        size_t canwrite = nodeWriteAvail(node);
        size_t count    = (remaining < canwrite) ? remaining : canwrite;

        // write the data
        if (node->tail + count <= node->buf->sz) {
            // can write in one go
            memcpy(node->buf->data + node->tail, p, count);
            node->tail = (node->tail + count) % node->buf->sz;
        } else {
            // have to split the write
            size_t firstPart = node->buf->sz - node->tail;
            memcpy(node->buf->data + node->tail, p, firstPart);
            size_t secondPart = count - firstPart;
            memcpy(node->buf->data, p + firstPart, secondPart);
            node->tail = secondPart;
        }

        // we filled up the node
        if (node->head == node->tail)
            node->full = true;

        ring->total += count;
        remaining -= count;
        p += count;
    }
}

_Use_decl_annotations_
void bufringWriteZC(BufRing* ring, buffer* buf)
{
    if (!buf || (*buf)->sz == 0)
        return;

    BufRingNode* node = xaAllocStruct(BufRingNode);
    node->next        = NULL;
    node->buf         = *buf;
    node->head        = 0;
    node->tail        = (*buf)->len % (*buf)->sz;
    node->full        = (*buf)->len == (*buf)->sz;
    devAssert((*buf)->len <= (*buf)->sz);

    if (ring->tail) {
        ring->tail->next = node;
        ring->tail       = node;
    } else {
        ring->head = ring->tail = node;
    }

    ring->total += (*buf)->len;
    *buf = NULL;
}

_Use_decl_annotations_
size_t bufringSkip(BufRing* ring, size_t bytes)
{
    size_t toSkip = min(bytes, ring->total);
    moveReadHead(ring, toSkip);
    return toSkip;
}

_Use_decl_annotations_
void bufringDestroy(BufRing* ring)
{
    BufRingNode* node = ring->head;
    while (node) {
        BufRingNode* next = node->next;
        bufDestroy(&node->buf);
        xaFree(node);
        node = next;
    }
    ring->head  = NULL;
    ring->tail  = NULL;
    ring->total = 0;
    ring->segsz = 0;
}
