#include "bufring.h"
#include <cx/utils/compare.h>

_Use_decl_annotations_
void bufringInit(BufRing* ring, size_t segsz)
{
    ring->head     = NULL;
    ring->tail     = NULL;
    ring->total    = 0;
    ring->segsz    = clamplow(segsz, 64);   // minimum segment size is 64 bytes
    ring->reserved = 0;
}

// Allocates a new segment of at least segsz bytes and appends it to the tail of the ring.
static BufRingNode* appendNode(BufRing* ring, size_t segsz)
{
    BufRingNode* node = xaAllocStruct(BufRingNode);
    node->next        = NULL;
    node->buf         = bufCreate(segsz);
    node->head        = 0;
    node->tail        = 0;
    node->full        = false;

    if (ring->tail) {
        ring->tail->next = node;
        ring->tail       = node;
    } else {
        ring->head = ring->tail = node;
    }

    return node;
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
                if (ring->reserved == 0) {
                    // If the buffer is empty, reset it to the start of the ring.
                    // This helps avoid having to split reads/writes.
                    node->head = node->tail = 0;
                } else {
                    // With a reservation outstanding the node must NOT be rewound: the reserved
                    // region begins at node->tail and an asynchronous operation may already be
                    // writing into it. Just catch the read cursor up to the write cursor, which
                    // is still a correct empty state; it only gives up the alignment
                    // optimization until the reservation is committed.
                    node->head = node->tail;
                }
                node->full = false;
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

    devAssertMsg(ring->reserved == 0, "Cannot write to a bufring with an outstanding reservation");

    while (remaining > 0) {
        BufRingNode* node = ring->tail;
        if (!node || nodeWriteAvail(node) == 0) {
            // need a new node
            node = appendNode(ring, ring->segsz);
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
void bufringWriteZC(BufRing* ring, Buffer* buf)
{
    devAssertMsg(ring->reserved == 0, "Cannot write to a bufring with an outstanding reservation");

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
size_t bufringWriteSpace(BufRing* ring)
{
    return ring->tail ? nodeWriteAvail(ring->tail) : 0;
}

_Use_decl_annotations_
size_t bufringFeed(BufRing* ring, bufringFeedCB feed, size_t bytes, void* ctx)
{
    size_t remaining = bytes;
    size_t nfeed     = 0;

    devAssertMsg(ring->reserved == 0, "Cannot write to a bufring with an outstanding reservation");

    while (remaining > 0) {
        BufRingNode* node = ring->tail;
        if (!node || nodeWriteAvail(node) == 0) {
            // need a new node
            node = appendNode(ring, ring->segsz);
        }

        size_t canwrite = nodeWriteAvail(node);
        size_t count    = (remaining < canwrite) ? remaining : canwrite;
        // make sure we don't go past the end, this automatically splits writes
        count           = min(count, node->buf->sz - node->tail);

        nfeed = feed(node->buf->data + node->tail, count, ctx);
        devAssert(nfeed <= count);
        node->tail = (node->tail + nfeed) % node->buf->sz;

        // we filled up the node
        if (node->head == node->tail)
            node->full = true;

        ring->total += nfeed;
        remaining -= nfeed;

        if (nfeed == 0)
            break;   // callback returned 0, nothing else we can do
    }

    return bytes - remaining;   // how much was actually fed into the buffer
}

// Returns how much can be written to a node without wrapping around the end of its buffer.
// Reservations must be contiguous, so this -- not nodeWriteAvail -- is what bounds them.
static _meta_inline size_t nodeContigWriteAvail(BufRingNode* node)
{
    if (node->full)
        return 0;

    // head <= tail: free space runs from tail to the end of the buffer, then wraps to head.
    // head > tail:  free space runs from tail up to head and does not wrap.
    return (node->head <= node->tail) ? node->buf->sz - node->tail : node->head - node->tail;
}

_Use_decl_annotations_
void bufringReserve(BufRing* ring, size_t minbytes, uint8** ptr, size_t* len)
{
    devAssertMsg(ring->reserved == 0, "Only one bufring reservation may be outstanding at a time");
    devAssert(minbytes > 0);

    BufRingNode* node = ring->tail;

    // If the tail node is empty but its cursors have wandered into the middle of the buffer,
    // rewind them. There is no data to preserve, and it may turn a split into a contiguous run
    // large enough to satisfy the request without allocating.
    if (node && !node->full && node->head == node->tail && node->head != 0)
        node->head = node->tail = 0;

    if (!node || nodeContigWriteAvail(node) < minbytes) {
        // Nothing contiguous enough at the tail. Allocate a segment big enough to hold the
        // whole reservation; any unused space in the old tail is simply left behind, exactly as
        // the normal write path leaves it.
        node = appendNode(ring, max(ring->segsz, minbytes));
    }

    *len           = nodeContigWriteAvail(node);
    *ptr           = node->buf->data + node->tail;
    ring->reserved = *len;

    devAssert(*len >= minbytes);
}

_Use_decl_annotations_
void bufringCommit(BufRing* ring, size_t bytes)
{
    devAssertMsg(ring->reserved > 0, "bufringCommit without an outstanding reservation");
    devAssertMsg(bytes <= ring->reserved, "bufringCommit of more bytes than were reserved");

    BufRingNode* node = ring->tail;
    bytes             = min(bytes, ring->reserved);

    // The reservation was contiguous and started at tail, so this cannot overrun the buffer;
    // it can only land exactly on the end, which wraps to zero.
    node->tail = (node->tail + bytes) % node->buf->sz;

    // we filled up the node
    if (node->head == node->tail && bytes > 0)
        node->full = true;

    ring->total += bytes;

    // Whatever was not committed goes back to being ordinary free space.
    ring->reserved = 0;
}

_Use_decl_annotations_
size_t bufringSkip(BufRing* ring, size_t bytes)
{
    size_t toSkip = min(bytes, ring->total);
    moveReadHead(ring, toSkip);
    return toSkip;
}

size_t _bufringReadContigAvail(_In_ BufRing* ring)
{
    if (!ring->head || ring->total == 0)
        return 0;

    BufRingNode* node = ring->head;
    return nodeReadAvail(node);
}

Buffer _bufringStealHead(_Inout_ BufRing* ring)
{
    if (!ring->head || ring->total == 0)
        return NULL;

    BufRingNode* node = ring->head;
    if (node->head != 0)
        return NULL;   // not aligned

    Buffer buf = node->buf;

    // remove the node from the ring
    ring->head = node->next;
    if (ring->head == NULL)
        ring->tail = NULL;

    ring->total -= node->tail;

    xaFree(node);
    return buf;
}

_Use_decl_annotations_
void bufringDestroy(BufRing* ring)
{
    // An outstanding reservation means someone may still be about to write into this memory.
    devAssertMsg(ring->reserved == 0, "Destroying a bufring with an outstanding reservation");

    BufRingNode* node = ring->head;
    while (node) {
        BufRingNode* next = node->next;
        bufDestroy(&node->buf);
        xaFree(node);
        node = next;
    }
    ring->head     = NULL;
    ring->tail     = NULL;
    ring->total    = 0;
    ring->segsz    = 0;
    ring->reserved = 0;
}
