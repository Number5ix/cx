#include "quic_private.h"

#include <string.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL QuicLogChannel

// Stream and CRYPTO offsets share the 62-bit ceiling packet numbers use, and the check is always
// against the end of the range rather than its start.
static bool frameOffsetOk(uint64 offset, uint64 len)
{
    return offset < QUIC_MAX_PACKET_NUMBER && len <= QUIC_MAX_PACKET_NUMBER - offset;
}

// A stream count is capped lower than everything else: stream ids are built by shifting the count
// left two bits, and the result still has to fit in 62.
#define QUIC_MAX_STREAMS (UINT64_C(1) << 60)

static bool frameDecodeAck(QuicFrame* f, QuicRd* rd)
{
    f->ack.largest = _quicRdVarint(rd);
    f->ack.delay = _quicRdVarint(rd);
    f->ack.rangeCount = _quicRdVarint(rd);
    f->ack.firstRange = _quicRdVarint(rd);
    if (rd->bad || f->ack.firstRange > f->ack.largest)
        return false;

    // The gap and length pairs are kept as raw bytes and walked later by QuicAckIter. They are
    // only scanned here, to find where they end and to reject a truncated list.
    const uint8* start = rd->p;
    for (uint64 i = 0; i < f->ack.rangeCount; i++) {
        _quicRdVarint(rd);
        _quicRdVarint(rd);
        if (rd->bad)
            return false;
    }
    f->ack.rangeData = start;
    f->ack.rangeLen = (size_t)(rd->p - start);

    if (f->type == QUIC_FRAME_ACK_ECN) {
        f->ack.ect0 = _quicRdVarint(rd);
        f->ack.ect1 = _quicRdVarint(rd);
        f->ack.ecnce = _quicRdVarint(rd);
    }

    return !rd->bad;
}

static bool frameDecodeStream(QuicFrame* f, QuicRd* rd)
{
    f->stream.id = _quicRdVarint(rd);
    if (f->type & QUIC_STREAM_OFF)
        f->stream.offset = _quicRdVarint(rd);
    if (rd->bad)
        return false;

    size_t len;
    if (f->type & QUIC_STREAM_LEN) {
        f->stream.data = _quicRdVarintBytes(rd, &len);
    } else {
        // With no length field the frame runs to the end of the packet, which is why a STREAM
        // frame in that form has to be the last one.
        len = _quicRdLeft(rd);
        f->stream.data = _quicRdBytes(rd, len);
    }
    if (rd->bad)
        return false;

    f->stream.len = len;
    return frameOffsetOk(f->stream.offset, f->stream.len);
}

static bool frameDecodeNewConnId(QuicFrame* f, QuicRd* rd)
{
    f->newConnId.seq = _quicRdVarint(rd);
    f->newConnId.retirePrior = _quicRdVarint(rd);
    uint8 n = _quicRd8(rd);
    if (rd->bad || n == 0 || n > QUIC_MAX_CID || f->newConnId.retirePrior > f->newConnId.seq)
        return false;

    const uint8* cid = _quicRdBytes(rd, n);
    const uint8* token = _quicRdBytes(rd, QUIC_RESET_TOKEN_LEN);
    if (rd->bad)
        return false;

    f->newConnId.cid.len = n;
    memcpy(f->newConnId.cid.id, cid, n);
    memcpy(f->newConnId.token, token, QUIC_RESET_TOKEN_LEN);
    return true;
}

_Use_decl_annotations_
bool _quicFrameDecode(QuicFrame* f, QuicRd* rd)
{
    memset(f, 0, sizeof(*f));

    if (rd->bad || _quicRdLeft(rd) == 0)
        return false;

    f->type = _quicRdVarint(rd);
    if (rd->bad)
        return false;

    switch (f->type) {
    case QUIC_FRAME_PADDING: {
        // A run of padding is reported as one frame. Splitting it into thousands of one-byte
        // frames would be faithful to the wire and useless to every caller.
        uint64 n = 1;
        while (_quicRdLeft(rd) > 0 && rd->p[0] == QUIC_FRAME_PADDING) {
            rd->p++;
            n++;
        }
        f->padding = n;
        return true;
    }

    case QUIC_FRAME_PING:
    case QUIC_FRAME_HANDSHAKE_DONE:
        return true;

    case QUIC_FRAME_ACK:
    case QUIC_FRAME_ACK_ECN:
        return frameDecodeAck(f, rd);

    case QUIC_FRAME_RESET_STREAM:
        f->resetStream.id = _quicRdVarint(rd);
        f->resetStream.error = _quicRdVarint(rd);
        f->resetStream.finalSize = _quicRdVarint(rd);
        return !rd->bad;

    case QUIC_FRAME_STOP_SENDING:
        f->stopSending.id = _quicRdVarint(rd);
        f->stopSending.error = _quicRdVarint(rd);
        return !rd->bad;

    case QUIC_FRAME_CRYPTO: {
        size_t len;
        f->crypto.offset = _quicRdVarint(rd);
        f->crypto.data = _quicRdVarintBytes(rd, &len);
        if (rd->bad)
            return false;
        f->crypto.len = len;
        return frameOffsetOk(f->crypto.offset, f->crypto.len);
    }

    case QUIC_FRAME_NEW_TOKEN: {
        size_t len;
        f->newToken.token = _quicRdVarintBytes(rd, &len);
        f->newToken.len = len;
        return !rd->bad && len > 0;
    }

    case QUIC_FRAME_MAX_DATA:
        f->maxData.max = _quicRdVarint(rd);
        return !rd->bad;

    case QUIC_FRAME_MAX_STREAM_DATA:
        f->maxStreamData.id = _quicRdVarint(rd);
        f->maxStreamData.max = _quicRdVarint(rd);
        return !rd->bad;

    case QUIC_FRAME_MAX_STREAMS_BIDI:
    case QUIC_FRAME_MAX_STREAMS_UNI:
        f->maxStreams.max = _quicRdVarint(rd);
        return !rd->bad && f->maxStreams.max <= QUIC_MAX_STREAMS;

    case QUIC_FRAME_DATA_BLOCKED:
        f->dataBlocked.limit = _quicRdVarint(rd);
        return !rd->bad;

    case QUIC_FRAME_STREAM_DATA_BLOCKED:
        f->streamDataBlocked.id = _quicRdVarint(rd);
        f->streamDataBlocked.limit = _quicRdVarint(rd);
        return !rd->bad;

    case QUIC_FRAME_STREAMS_BLOCKED_BIDI:
    case QUIC_FRAME_STREAMS_BLOCKED_UNI:
        f->streamsBlocked.limit = _quicRdVarint(rd);
        return !rd->bad && f->streamsBlocked.limit <= QUIC_MAX_STREAMS;

    case QUIC_FRAME_NEW_CONNECTION_ID:
        return frameDecodeNewConnId(f, rd);

    case QUIC_FRAME_RETIRE_CONNECTION_ID:
        f->retireConnId.seq = _quicRdVarint(rd);
        return !rd->bad;

    case QUIC_FRAME_PATH_CHALLENGE:
    case QUIC_FRAME_PATH_RESPONSE: {
        const uint8* p = _quicRdBytes(rd, QUIC_PATH_DATA_LEN);
        if (!p)
            return false;
        memcpy(f->path.data, p, QUIC_PATH_DATA_LEN);
        return true;
    }

    case QUIC_FRAME_CONNECTION_CLOSE:
    case QUIC_FRAME_CONNECTION_CLOSE_APP: {
        size_t len;
        f->connClose.error = _quicRdVarint(rd);
        if (f->type == QUIC_FRAME_CONNECTION_CLOSE)
            f->connClose.frameType = _quicRdVarint(rd);
        f->connClose.reason = _quicRdVarintBytes(rd, &len);
        f->connClose.reasonLen = len;
        return !rd->bad;
    }

    default:
        if (f->type >= QUIC_FRAME_STREAM && f->type <= (QUIC_FRAME_STREAM | 0x07))
            return frameDecodeStream(f, rd);

        // An unrecognized frame type cannot be skipped, because nothing says how long it is.
        return false;
    }
}

_Use_decl_annotations_
size_t _quicFrameSize(const QuicFrame* f)
{
    uint8 t = _quicVarintSize(f->type);
    if (t == 0)
        return 0;

    switch (f->type) {
    case QUIC_FRAME_PADDING:
        return (size_t)f->padding;

    case QUIC_FRAME_PING:
    case QUIC_FRAME_HANDSHAKE_DONE:
        return t;

    case QUIC_FRAME_ACK:
    case QUIC_FRAME_ACK_ECN: {
        size_t sz = t + _quicVarintSize(f->ack.largest) + _quicVarintSize(f->ack.delay) +
                    _quicVarintSize(f->ack.rangeCount) + _quicVarintSize(f->ack.firstRange) +
                    f->ack.rangeLen;
        if (f->type == QUIC_FRAME_ACK_ECN)
            sz += _quicVarintSize(f->ack.ect0) + _quicVarintSize(f->ack.ect1) +
                  _quicVarintSize(f->ack.ecnce);
        return sz;
    }

    case QUIC_FRAME_RESET_STREAM:
        return t + _quicVarintSize(f->resetStream.id) + _quicVarintSize(f->resetStream.error) +
               _quicVarintSize(f->resetStream.finalSize);

    case QUIC_FRAME_STOP_SENDING:
        return t + _quicVarintSize(f->stopSending.id) + _quicVarintSize(f->stopSending.error);

    case QUIC_FRAME_CRYPTO:
        return t + _quicVarintSize(f->crypto.offset) + _quicVarintSize(f->crypto.len) +
               (size_t)f->crypto.len;

    case QUIC_FRAME_NEW_TOKEN:
        return t + _quicVarintSize(f->newToken.len) + (size_t)f->newToken.len;

    case QUIC_FRAME_MAX_DATA:
        return t + _quicVarintSize(f->maxData.max);

    case QUIC_FRAME_MAX_STREAM_DATA:
        return t + _quicVarintSize(f->maxStreamData.id) + _quicVarintSize(f->maxStreamData.max);

    case QUIC_FRAME_MAX_STREAMS_BIDI:
    case QUIC_FRAME_MAX_STREAMS_UNI:
        return t + _quicVarintSize(f->maxStreams.max);

    case QUIC_FRAME_DATA_BLOCKED:
        return t + _quicVarintSize(f->dataBlocked.limit);

    case QUIC_FRAME_STREAM_DATA_BLOCKED:
        return t + _quicVarintSize(f->streamDataBlocked.id) +
               _quicVarintSize(f->streamDataBlocked.limit);

    case QUIC_FRAME_STREAMS_BLOCKED_BIDI:
    case QUIC_FRAME_STREAMS_BLOCKED_UNI:
        return t + _quicVarintSize(f->streamsBlocked.limit);

    case QUIC_FRAME_NEW_CONNECTION_ID:
        return t + _quicVarintSize(f->newConnId.seq) + _quicVarintSize(f->newConnId.retirePrior) +
               1 + f->newConnId.cid.len + QUIC_RESET_TOKEN_LEN;

    case QUIC_FRAME_RETIRE_CONNECTION_ID:
        return t + _quicVarintSize(f->retireConnId.seq);

    case QUIC_FRAME_PATH_CHALLENGE:
    case QUIC_FRAME_PATH_RESPONSE:
        return t + QUIC_PATH_DATA_LEN;

    case QUIC_FRAME_CONNECTION_CLOSE:
    case QUIC_FRAME_CONNECTION_CLOSE_APP: {
        size_t sz = t + _quicVarintSize(f->connClose.error) +
                    _quicVarintSize(f->connClose.reasonLen) + (size_t)f->connClose.reasonLen;
        if (f->type == QUIC_FRAME_CONNECTION_CLOSE)
            sz += _quicVarintSize(f->connClose.frameType);
        return sz;
    }

    default:
        if (f->type >= QUIC_FRAME_STREAM && f->type <= (QUIC_FRAME_STREAM | 0x07)) {
            size_t sz = t + _quicVarintSize(f->stream.id) + (size_t)f->stream.len;
            if (f->type & QUIC_STREAM_OFF)
                sz += _quicVarintSize(f->stream.offset);
            if (f->type & QUIC_STREAM_LEN)
                sz += _quicVarintSize(f->stream.len);
            return sz;
        }
        return 0;
    }
}

_Use_decl_annotations_
bool _quicFrameEncode(QuicWr* wr, const QuicFrame* f)
{
    if (_quicFrameSize(f) == 0 && f->type != QUIC_FRAME_PADDING)
        return false;

    if (f->type == QUIC_FRAME_PADDING) {
        if (_quicWrLeft(wr) < (size_t)f->padding) {
            wr->bad = true;
            return false;
        }
        memset(wr->p, 0, (size_t)f->padding);
        wr->p += (size_t)f->padding;
        return true;
    }

    _quicWrVarint(wr, f->type);

    switch (f->type) {
    case QUIC_FRAME_PING:
    case QUIC_FRAME_HANDSHAKE_DONE:
        break;

    case QUIC_FRAME_ACK:
    case QUIC_FRAME_ACK_ECN:
        _quicWrVarint(wr, f->ack.largest);
        _quicWrVarint(wr, f->ack.delay);
        _quicWrVarint(wr, f->ack.rangeCount);
        _quicWrVarint(wr, f->ack.firstRange);
        _quicWrBytes(wr, f->ack.rangeData, f->ack.rangeLen);
        if (f->type == QUIC_FRAME_ACK_ECN) {
            _quicWrVarint(wr, f->ack.ect0);
            _quicWrVarint(wr, f->ack.ect1);
            _quicWrVarint(wr, f->ack.ecnce);
        }
        break;

    case QUIC_FRAME_RESET_STREAM:
        _quicWrVarint(wr, f->resetStream.id);
        _quicWrVarint(wr, f->resetStream.error);
        _quicWrVarint(wr, f->resetStream.finalSize);
        break;

    case QUIC_FRAME_STOP_SENDING:
        _quicWrVarint(wr, f->stopSending.id);
        _quicWrVarint(wr, f->stopSending.error);
        break;

    case QUIC_FRAME_CRYPTO:
        _quicWrVarint(wr, f->crypto.offset);
        _quicWrVarintBytes(wr, f->crypto.data, (size_t)f->crypto.len);
        break;

    case QUIC_FRAME_NEW_TOKEN:
        _quicWrVarintBytes(wr, f->newToken.token, (size_t)f->newToken.len);
        break;

    case QUIC_FRAME_MAX_DATA:
        _quicWrVarint(wr, f->maxData.max);
        break;

    case QUIC_FRAME_MAX_STREAM_DATA:
        _quicWrVarint(wr, f->maxStreamData.id);
        _quicWrVarint(wr, f->maxStreamData.max);
        break;

    case QUIC_FRAME_MAX_STREAMS_BIDI:
    case QUIC_FRAME_MAX_STREAMS_UNI:
        _quicWrVarint(wr, f->maxStreams.max);
        break;

    case QUIC_FRAME_DATA_BLOCKED:
        _quicWrVarint(wr, f->dataBlocked.limit);
        break;

    case QUIC_FRAME_STREAM_DATA_BLOCKED:
        _quicWrVarint(wr, f->streamDataBlocked.id);
        _quicWrVarint(wr, f->streamDataBlocked.limit);
        break;

    case QUIC_FRAME_STREAMS_BLOCKED_BIDI:
    case QUIC_FRAME_STREAMS_BLOCKED_UNI:
        _quicWrVarint(wr, f->streamsBlocked.limit);
        break;

    case QUIC_FRAME_NEW_CONNECTION_ID:
        if (f->newConnId.cid.len == 0 || f->newConnId.cid.len > QUIC_MAX_CID)
            return false;
        _quicWrVarint(wr, f->newConnId.seq);
        _quicWrVarint(wr, f->newConnId.retirePrior);
        _quicWr8(wr, f->newConnId.cid.len);
        _quicWrBytes(wr, f->newConnId.cid.id, f->newConnId.cid.len);
        _quicWrBytes(wr, f->newConnId.token, QUIC_RESET_TOKEN_LEN);
        break;

    case QUIC_FRAME_RETIRE_CONNECTION_ID:
        _quicWrVarint(wr, f->retireConnId.seq);
        break;

    case QUIC_FRAME_PATH_CHALLENGE:
    case QUIC_FRAME_PATH_RESPONSE:
        _quicWrBytes(wr, f->path.data, QUIC_PATH_DATA_LEN);
        break;

    case QUIC_FRAME_CONNECTION_CLOSE:
    case QUIC_FRAME_CONNECTION_CLOSE_APP:
        _quicWrVarint(wr, f->connClose.error);
        if (f->type == QUIC_FRAME_CONNECTION_CLOSE)
            _quicWrVarint(wr, f->connClose.frameType);
        _quicWrVarintBytes(wr, f->connClose.reason, (size_t)f->connClose.reasonLen);
        break;

    default:
        _quicWrVarint(wr, f->stream.id);
        if (f->type & QUIC_STREAM_OFF)
            _quicWrVarint(wr, f->stream.offset);
        if (f->type & QUIC_STREAM_LEN)
            _quicWrVarint(wr, f->stream.len);
        _quicWrBytes(wr, f->stream.data, (size_t)f->stream.len);
        break;
    }

    return !wr->bad;
}

// ---------------------------------------------------------------------------------------------
// ACK ranges
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
bool _quicAckIterInit(QuicAckIter* it, const QuicFrame* f)
{
    memset(it, 0, sizeof(*it));

    if (f->type != QUIC_FRAME_ACK && f->type != QUIC_FRAME_ACK_ECN)
        return false;
    if (f->ack.firstRange > f->ack.largest)
        return false;

    _quicRdInit(&it->rd, f->ack.rangeData, f->ack.rangeLen);
    it->remaining = f->ack.rangeCount;
    it->largest = f->ack.largest;
    it->firstRange = f->ack.firstRange;
    it->first = true;
    return true;
}

_Use_decl_annotations_
bool _quicAckIterNext(QuicAckIter* it, QuicAckRange* out)
{
    if (it->bad)
        return false;

    if (it->first) {
        it->first = false;
        out->largest = it->largest;
        out->smallest = it->largest - it->firstRange;
        it->next = out->smallest;
        return true;
    }

    if (it->remaining == 0)
        return false;
    it->remaining--;

    uint64 gap = _quicRdVarint(&it->rd);
    uint64 len = _quicRdVarint(&it->rd);
    if (it->rd.bad) {
        it->bad = true;
        return false;
    }

    // Ranges descend and never touch, so there is always at least one unacknowledged number
    // between them; that is why the gap is biased by two rather than one.
    if (it->next < gap + 2) {
        it->bad = true;
        return false;
    }
    out->largest = it->next - gap - 2;

    if (out->largest < len) {
        it->bad = true;
        return false;
    }
    out->smallest = out->largest - len;

    it->next = out->smallest;
    return true;
}

_Use_decl_annotations_
bool _quicAckBuild(QuicFrame* f, uint64 delay, const QuicAckRange* ranges, size_t nranges,
                   const uint64 ecn[3], uint8* rangeBuf, size_t bufsz)
{
    memset(f, 0, sizeof(*f));

    if (nranges == 0 || ranges[0].smallest > ranges[0].largest)
        return false;

    QuicWr wr;
    _quicWrInit(&wr, rangeBuf, bufsz);

    for (size_t i = 1; i < nranges; i++) {
        if (ranges[i].smallest > ranges[i].largest)
            return false;
        // Adjacent ranges would have to be one range, so the previous smallest must be at least
        // two above this largest.
        if (ranges[i].largest + 2 > ranges[i - 1].smallest)
            return false;

        _quicWrVarint(&wr, ranges[i - 1].smallest - ranges[i].largest - 2);
        _quicWrVarint(&wr, ranges[i].largest - ranges[i].smallest);
    }

    if (wr.bad)
        return false;

    f->type = ecn ? QUIC_FRAME_ACK_ECN : QUIC_FRAME_ACK;
    f->ack.largest = ranges[0].largest;
    f->ack.delay = delay;
    f->ack.firstRange = ranges[0].largest - ranges[0].smallest;
    f->ack.rangeCount = nranges - 1;
    f->ack.rangeData = rangeBuf;
    f->ack.rangeLen = _quicWrLen(&wr);

    if (ecn) {
        f->ack.ect0 = ecn[0];
        f->ack.ect1 = ecn[1];
        f->ack.ecnce = ecn[2];
    }

    return true;
}
