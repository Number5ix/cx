// A send buffer that holds data until the peer acknowledges it.
//
// The buffer is a run of bytes plus a list of ranges saying where each part of it is in its life.
// Every byte is covered by exactly one range, the ranges are sorted, and neighbours in the same
// state are joined, so a stream that is sent and acknowledged in order settles on a single range
// and stays there.
//
// CRYPTO frames are the first user; stream data is the second, and works the same way.

#include "conn_private.h"

#undef LOG_CHANNEL
#define LOG_CHANNEL QuicLogChannel

_Use_decl_annotations_
void _quicSendBufInit(QuicSendBuf* sb)
{
    memset(sb, 0, sizeof(*sb));
}

_Use_decl_annotations_
void _quicSendBufDestroy(QuicSendBuf* sb)
{
    bufDestroy(&sb->buf);
    xaFree(sb->chunks);
    memset(sb, 0, sizeof(*sb));
}

// Makes room for one more range. The table doubles rather than being capped, because what bounds
// it is how many packets this endpoint has outstanding -- and refusing to track one more would
// mean refusing to send, with nothing the layer above could do about it.
static void chunkReserve(_Inout_ QuicSendBuf* sb)
{
    if (sb->nchunks < sb->cap)
        return;

    uint32 want = sb->cap ? sb->cap * 2 : QUIC_SEND_INIT_CHUNKS;
    if (sb->chunks)
        xaResize(&sb->chunks, want * sizeof(QuicSendChunk));
    else
        sb->chunks = xaAlloc(want * sizeof(QuicSendChunk), XA_Zero);
    sb->cap = want;
}

static void chunkRemove(_Inout_ QuicSendBuf* sb, uint32 at)
{
    memmove(&sb->chunks[at], &sb->chunks[at + 1],
            (sb->nchunks - at - 1) * sizeof(QuicSendChunk));
    sb->nchunks--;
}

static bool chunkInsert(_Inout_ QuicSendBuf* sb, uint32 at, uint64 off, uint64 end, uint8 state,
                        uint64 pn)
{
    chunkReserve(sb);

    memmove(&sb->chunks[at + 1], &sb->chunks[at], (sb->nchunks - at) * sizeof(QuicSendChunk));
    sb->nchunks++;

    sb->chunks[at].off   = off;
    sb->chunks[at].end   = end;
    sb->chunks[at].state = state;
    sb->chunks[at].pn    = pn;
    return true;
}

// Joins neighbours that would behave identically. Two sent ranges only join if the same packet
// carried both, since an acknowledgement or a loss applies to one packet at a time.
static void chunkMerge(_Inout_ QuicSendBuf* sb)
{
    for (uint32 i = 0; i + 1 < sb->nchunks;) {
        QuicSendChunk* a = &sb->chunks[i];
        QuicSendChunk* b = &sb->chunks[i + 1];

        if (a->end == b->off && a->state == b->state &&
            (a->state != QUIC_CHUNK_SENT || a->pn == b->pn)) {
            a->end = b->end;
            chunkRemove(sb, i + 1);
        } else {
            i++;
        }
    }
}

// Releases the acknowledged bytes at the front, which are the only ones that can be released:
// what is behind them still has to be there in case it has to be sent again.
static void sendBufTrim(_Inout_ QuicSendBuf* sb)
{
    while (sb->nchunks > 0 && sb->chunks[0].state == QUIC_CHUNK_ACKED) {
        size_t n = (size_t)(sb->chunks[0].end - sb->base);

        sb->base = sb->chunks[0].end;
        chunkRemove(sb, 0);

        if (sb->buf) {
            size_t keep = sb->buf->len > n ? sb->buf->len - n : 0;
            if (keep > 0)
                memmove(sb->buf->data, sb->buf->data + n, keep);
            sb->buf->len = keep;
        }
    }
}

_Use_decl_annotations_
bool _quicSendBufAdd(QuicSendBuf* sb, const uint8* data, size_t len)
{
    if (len == 0)
        return true;

    // New bytes either extend a range already waiting to be sent or need one of their own.
    bool extends = sb->nchunks > 0 && sb->chunks[sb->nchunks - 1].state == QUIC_CHUNK_PENDING;

    size_t used = (size_t)(sb->end - sb->base);
    if (!sb->buf || sb->buf->sz < used + len) {
        size_t want = sb->buf ? sb->buf->sz : 1024;
        while (want < used + len)
            want *= 2;
        if (!bufTryResize(&sb->buf, want))
            return false;
    }

    memcpy(sb->buf->data + used, data, len);
    sb->buf->len = used + len;

    uint64 off = sb->end;
    sb->end += len;

    if (extends) {
        sb->chunks[sb->nchunks - 1].end = sb->end;
        return true;
    }

    return chunkInsert(sb, sb->nchunks, off, sb->end, QUIC_CHUNK_PENDING, QUIC_PN_NONE);
}

_Use_decl_annotations_
bool _quicSendBufNext(const QuicSendBuf* sb, size_t max, uint64* off, const uint8** data,
                      size_t* len)
{
    if (max == 0)
        return false;

    for (uint32 i = 0; i < sb->nchunks; i++) {
        const QuicSendChunk* ch = &sb->chunks[i];
        if (ch->state != QUIC_CHUNK_PENDING)
            continue;

        size_t n = (size_t)(ch->end - ch->off);
        if (n > max)
            n = max;

        *off  = ch->off;
        *data = sb->buf->data + (size_t)(ch->off - sb->base);
        *len  = n;
        return true;
    }

    return false;
}

_Use_decl_annotations_
void _quicSendBufSent(QuicSendBuf* sb, uint64 off, size_t len, uint64 pn)
{
    for (uint32 i = 0; i < sb->nchunks; i++) {
        QuicSendChunk* ch = &sb->chunks[i];
        if (ch->state != QUIC_CHUNK_PENDING || ch->off != off)
            continue;

        if ((size_t)(ch->end - ch->off) == len) {
            ch->state = QUIC_CHUNK_SENT;
            ch->pn    = pn;
        } else {
            ch->off = off + len;
            chunkInsert(sb, i, off, off + len, QUIC_CHUNK_SENT, pn);
        }

        chunkMerge(sb);
        return;
    }
}

_Use_decl_annotations_
void _quicSendBufAcked(QuicSendBuf* sb, uint64 pn)
{
    for (uint32 i = 0; i < sb->nchunks; i++) {
        if (sb->chunks[i].state == QUIC_CHUNK_SENT && sb->chunks[i].pn == pn) {
            sb->chunks[i].state = QUIC_CHUNK_ACKED;
            sb->chunks[i].pn    = QUIC_PN_NONE;
        }
    }

    chunkMerge(sb);
    sendBufTrim(sb);
}

_Use_decl_annotations_
void _quicSendBufLost(QuicSendBuf* sb, uint64 pn)
{
    for (uint32 i = 0; i < sb->nchunks; i++) {
        if (sb->chunks[i].state == QUIC_CHUNK_SENT && sb->chunks[i].pn == pn) {
            sb->chunks[i].state = QUIC_CHUNK_PENDING;
            sb->chunks[i].pn    = QUIC_PN_NONE;
        }
    }

    chunkMerge(sb);
}

_Use_decl_annotations_
bool _quicSendBufProbe(const QuicSendBuf* sb, size_t max, uint64* off, const uint8** data,
                       size_t* len)
{
    if (max == 0)
        return false;

    for (uint32 i = 0; i < sb->nchunks; i++) {
        const QuicSendChunk* ch = &sb->chunks[i];
        if (ch->state != QUIC_CHUNK_SENT)
            continue;

        size_t n = (size_t)(ch->end - ch->off);
        if (n > max)
            n = max;

        *off  = ch->off;
        *data = sb->buf->data + (size_t)(ch->off - sb->base);
        *len  = n;
        return true;
    }

    return false;
}

_Use_decl_annotations_
void _quicSendBufReset(QuicSendBuf* sb)
{
    for (uint32 i = 0; i < sb->nchunks; i++) {
        sb->chunks[i].state = QUIC_CHUNK_PENDING;
        sb->chunks[i].pn    = QUIC_PN_NONE;
    }

    chunkMerge(sb);
}

_Use_decl_annotations_
bool _quicSendBufPending(const QuicSendBuf* sb)
{
    for (uint32 i = 0; i < sb->nchunks; i++) {
        if (sb->chunks[i].state == QUIC_CHUNK_PENDING)
            return true;
    }
    return false;
}

_Use_decl_annotations_
uint64 _quicSendBufOutstanding(const QuicSendBuf* sb)
{
    uint64 n = 0;
    for (uint32 i = 0; i < sb->nchunks; i++) {
        if (sb->chunks[i].state != QUIC_CHUNK_ACKED)
            n += sb->chunks[i].end - sb->chunks[i].off;
    }
    return n;
}
