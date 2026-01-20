#include "bufchain.h"
#include <cx/utils/compare.h>

_Use_decl_annotations_
void bufchainInit(BufChain* chain, size_t segsz)
{
    chain->head   = NULL;
    chain->tail   = NULL;
    chain->cursor = 0;
    chain->total  = 0;
    chain->segsz  = clamplow(segsz, 64);   // minimum segment size is 64 bytes
}

_Use_decl_annotations_
size_t bufchainRead(BufChain* chain, uint8* buf, size_t bytes)
{
    size_t bytesread = 0;

    while (bytes > 0 && chain->head) {
        BufChainNode* node = chain->head;
        size_t toread      = min(bytes, node->buf->len - chain->cursor);

        memcpy(buf + bytesread, node->buf->data + chain->cursor, toread);

        bytesread += toread;
        bytes -= toread;
        chain->cursor += toread;
        chain->total -= toread;

        // did we exhaust this node?
        if (chain->cursor >= node->buf->len) {
            // move to next node
            chain->head = node->next;
            if (chain->head == NULL)
                chain->tail = NULL;   // chain is now empty
            bufDestroy(&node->buf);
            xaFree(node);
            chain->cursor = 0;
        }
    }

    return bytesread;
}

_Use_decl_annotations_
size_t bufchainPeek(_Inout_ BufChain* chain, uint8* buf, size_t off, size_t bytes)
{
    size_t bytesread = 0;

    BufChainNode* node = chain->head;
    size_t cursor      = chain->cursor;   // local copy so we don't modify it

    // seek to the starting offset
    while (node && off > 0) {
        size_t toskip = min(off, node->buf->len - cursor);
        off -= toskip;
        cursor += toskip;
        if (cursor >= node->buf->len) {
            node   = node->next;
            cursor = 0;
        }
    }

    // now read the data
    while (node && bytes > 0) {
        size_t toread = min(bytes, node->buf->len - cursor);

        memcpy(buf + bytesread, node->buf->data + cursor, toread);

        bytesread += toread;
        bytes -= toread;
        cursor += toread;

        if (cursor >= node->buf->len) {
            node   = node->next;
            cursor = 0;
        }
    }

    return bytesread;
}

_Use_decl_annotations_
size_t bufchainReadZC(BufChain* chain, size_t maxbytes, bufchainZCCB cb, void* ctx)
{
    size_t bytesread = 0;
    bool done        = false;

    while (!done && chain->head && bytesread < maxbytes) {
        BufChainNode* node = chain->head;
        size_t toread      = node->buf->len - chain->cursor;

        // only send complete buffers
        if (bytesread + toread > maxbytes)
            break;

        // don't continue onto next node if callback returns false
        done = !cb(node->buf, chain->cursor, ctx);

        bytesread += toread;
        chain->total -= toread;

        // move to next node
        chain->head = node->next;
        if (chain->head == NULL)
            chain->tail = NULL;   // chain is now empty
        // do not destroy buffer; it's owned by the callback now
        xaFree(node);
        chain->cursor = 0;
    }

    return bytesread;
}

_Use_decl_annotations_
void bufchainWrite(BufChain* chain, const uint8* buf, size_t bytes)
{
    size_t remaining = bytes;
    const uint8* p   = buf;

    while (remaining > 0) {
        BufChainNode* node = chain->tail;
        if (!node || node->buf->len >= node->buf->sz) {
            // need a new node
            node       = xaAllocStruct(BufChainNode);
            node->next = NULL;
            node->buf  = bufCreate(chain->segsz);
            if (chain->tail) {
                chain->tail->next = node;
                chain->tail       = node;
            } else {
                chain->head = chain->tail = node;
            }
        }

        size_t count = min(remaining, node->buf->sz - node->buf->len);

        // write the data
        memcpy(node->buf->data + node->buf->len, p, count);
        node->buf->len += count;
        devAssert(node->buf->len <= node->buf->sz);

        chain->total += count;
        remaining -= count;
        p += count;
    }
}

_Use_decl_annotations_
void bufchainWriteZC(BufChain* chain, buffer* buf)
{
    if (!buf || (*buf)->sz == 0)
        return;

    BufChainNode* node = xaAllocStruct(BufChainNode);
    node->next         = NULL;
    node->buf          = *buf;
    devAssert((*buf)->len <= (*buf)->sz);

    if (chain->tail) {
        chain->tail->next = node;
        chain->tail       = node;
    } else {
        chain->head = chain->tail = node;
    }

    chain->total += (*buf)->len;
    *buf = NULL;
}

_Use_decl_annotations_
size_t bufchainSkip(BufChain* chain, size_t bytes)
{
    size_t skipped = 0;

    while (bytes > 0 && chain->head) {
        BufChainNode* node = chain->head;
        size_t toskip      = min(bytes, node->buf->len - chain->cursor);

        skipped += toskip;
        bytes -= toskip;
        chain->cursor += toskip;
        chain->total -= toskip;

        // did we exhaust this node?
        if (chain->cursor >= node->buf->len) {
            // move to next node
            chain->head = node->next;
            if (chain->head == NULL)
                chain->tail = NULL;   // chain is now empty
            bufDestroy(&node->buf);
            xaFree(node);
            chain->cursor = 0;
        }
    }

    return skipped;
}

_Use_decl_annotations_
void bufchainDestroy(BufChain* chain)
{
    BufChainNode* node = chain->head;
    while (node) {
        BufChainNode* next = node->next;
        bufDestroy(&node->buf);
        xaFree(node);
        node = next;
    }
    chain->head  = NULL;
    chain->tail  = NULL;
    chain->cursor = 0;
    chain->total = 0;
    chain->segsz = 0;
}
