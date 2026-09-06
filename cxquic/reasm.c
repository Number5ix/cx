// Two pieces of range bookkeeping: reassembling a byte stream that arrives out of order, and
// remembering which packet numbers have been received so they can be acknowledged.
//
// They are the same shape -- a small sorted list of non-overlapping, non-touching ranges that new
// arrivals are merged into -- and both are deliberately capped. The peer decides how fragmented
// its sending is, so an uncapped list of ranges is a memory cost the peer controls; dropping the
// oldest information costs a retransmission and nothing else.

#include "conn_private.h"

#undef LOG_CHANNEL
#define LOG_CHANNEL QuicLogChannel

// ---------------------------------------------------------------------------------------------
// Stream reassembly
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
void _quicReasmInit(QuicReasm* r, uint64 limit)
{
    memset(r, 0, sizeof(*r));
    r->limit = limit;
}

_Use_decl_annotations_
void _quicReasmDestroy(QuicReasm* r)
{
    bufDestroy(&r->buf);
    memset(r, 0, sizeof(*r));
}

_Use_decl_annotations_
bool _quicReasmAdd(QuicReasm* r, uint64 off, const uint8* data, size_t len)
{
    if (len == 0)
        return true;

    uint64 end = off + len;
    if (end < off || end > r->limit)
        return false;

    // Already handed to the reader: the peer retransmitted something that was acknowledged, or
    // the same data arrived twice.
    if (end <= r->base)
        return true;

    if (off < r->base) {
        data += (size_t)(r->base - off);
        len -= (size_t)(r->base - off);
        off = r->base;
    }

    // Work out what the range list would look like with this range merged in, before touching the
    // buffer. A range that would not fit is dropped whole rather than half-recorded.
    uint64 mlo = off, mhi = end;
    uint32 i = 0;
    while (i < r->nranges && r->ranges[i].end < mlo)
        i++;

    uint32 j = i;
    while (j < r->nranges && r->ranges[j].off <= mhi) {
        if (r->ranges[j].off < mlo)
            mlo = r->ranges[j].off;
        if (r->ranges[j].end > mhi)
            mhi = r->ranges[j].end;
        j++;
    }

    if (j == i && r->nranges >= QUIC_REASM_MAX_RANGES)
        return true;

    size_t need = (size_t)(mhi - r->base);
    if (!r->buf || r->buf->sz < need) {
        // Grow in powers of two so a stream arriving in small pieces does not reallocate on every
        // one of them.
        size_t want = r->buf ? r->buf->sz : 1024;
        while (want < need)
            want *= 2;
        if (!bufTryResize(&r->buf, want))
            return false;
    }

    memcpy(r->buf->data + (size_t)(off - r->base), data, len);
    r->buf->len = need > r->buf->len ? need : r->buf->len;

    if (j > i + 1)
        memmove(&r->ranges[i + 1], &r->ranges[j], (r->nranges - j) * sizeof(QuicRange));
    else if (j == i)
        memmove(&r->ranges[i + 1], &r->ranges[i], (r->nranges - i) * sizeof(QuicRange));

    r->nranges -= (j - i);
    r->nranges++;
    r->ranges[i].off = mlo;
    r->ranges[i].end = mhi;

    return true;
}

_Use_decl_annotations_
size_t _quicReasmReadable(const QuicReasm* r)
{
    if (r->nranges == 0 || r->ranges[0].off != r->base)
        return 0;
    return (size_t)(r->ranges[0].end - r->base);
}

_Use_decl_annotations_
const uint8* _quicReasmData(const QuicReasm* r)
{
    return r->buf ? r->buf->data : NULL;
}

_Use_decl_annotations_
void _quicReasmConsume(QuicReasm* r, size_t len)
{
    devAssert(len <= _quicReasmReadable(r));
    if (len == 0)
        return;

    r->base += len;

    if (r->ranges[0].end == r->base) {
        memmove(&r->ranges[0], &r->ranges[1], (r->nranges - 1) * sizeof(QuicRange));
        r->nranges--;
    } else {
        r->ranges[0].off = r->base;
    }

    // Slide the window down. The buffer keeps its allocation, so a stream that is read as fast as
    // it arrives settles on one buffer and stops growing.
    if (r->buf) {
        size_t keep = r->buf->len > len ? r->buf->len - len : 0;
        if (keep > 0)
            memmove(r->buf->data, r->buf->data + len, keep);
        r->buf->len = keep;
    }
}

// ---------------------------------------------------------------------------------------------
// Received packet numbers
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
bool _quicAckStateAdd(QuicAckState* as, uint64 pn, int64 now, bool ackEliciting)
{
    // Find where this packet number belongs. The list runs largest first, so the first range whose
    // largest is below `pn` is the insertion point.
    uint32 i = 0;
    while (i < as->nranges && as->ranges[i].largest >= pn) {
        if (pn >= as->ranges[i].smallest)
            return false;   // duplicate
        i++;
    }

    bool joinAbove = i > 0 && as->ranges[i - 1].smallest == pn + 1;
    bool joinBelow = i < as->nranges && as->ranges[i].largest + 1 == pn;

    if (joinAbove && joinBelow) {
        as->ranges[i - 1].smallest = as->ranges[i].smallest;
        memmove(&as->ranges[i], &as->ranges[i + 1], (as->nranges - i - 1) * sizeof(QuicAckRange));
        as->nranges--;
    } else if (joinAbove) {
        as->ranges[i - 1].smallest = pn;
    } else if (joinBelow) {
        as->ranges[i].largest = pn;
    } else if (as->nranges < QUIC_MAX_ACK_RANGES) {
        memmove(&as->ranges[i + 1], &as->ranges[i], (as->nranges - i) * sizeof(QuicAckRange));
        as->nranges++;
        as->ranges[i].largest  = pn;
        as->ranges[i].smallest = pn;
    } else if (i < QUIC_MAX_ACK_RANGES) {
        // The list is full and this packet is not the oldest thing in it, so the smallest range
        // is dropped to make room. The peer sees those packet numbers go unacknowledged and
        // retransmits whatever was in them.
        memmove(&as->ranges[i + 1], &as->ranges[i],
                (QUIC_MAX_ACK_RANGES - i - 1) * sizeof(QuicAckRange));
        as->ranges[i].largest  = pn;
        as->ranges[i].smallest = pn;
    } else {
        return true;   // older than everything tracked; nothing to record, but not a duplicate
    }

    if (as->largest == QUIC_PN_NONE || pn > as->largest) {
        as->largest     = pn;
        as->largestTime = now;
    }

    if (ackEliciting) {
        as->elicitCount++;
        as->pending = true;
    }

    return true;
}

_Use_decl_annotations_
void _quicAckStateEcn(QuicAckState* as, uint8 ecn)
{
    // The three counts are ECT(0), ECT(1) and CE in that order, which is the order the ACK frame
    // carries them. An unmarked packet belongs to none of them.
    switch (ecn) {
    case NET_ECN_Ect0: as->ecn[0]++; break;
    case NET_ECN_Ect1: as->ecn[1]++; break;
    case NET_ECN_CE:   as->ecn[2]++; break;
    default:           return;
    }

    // One marked packet is enough to make every acknowledgement in this space carry the counts:
    // the peer needs them to tell a path that carries its marks from one that does not.
    as->ecnSeen = true;
}
