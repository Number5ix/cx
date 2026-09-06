// Transport parameters (RFC 9000 section 18).
//
// The parameter set is a list of id/length/value triples that each endpoint sends inside its
// handshake, in an extension the TLS engine carries as opaque bytes. It is the only place an
// endpoint states its flow control limits, its idle timeout, and which connection IDs it used
// before the handshake was authenticated.

#include "conn_private.h"

#undef LOG_CHANNEL
#define LOG_CHANNEL QuicLogChannel

// Values the RFC assigns to parameters that were not sent.
#define QUIC_TP_DEF_MAX_UDP_PAYLOAD 65527
#define QUIC_TP_DEF_ACK_DELAY_EXP   3
#define QUIC_TP_DEF_MAX_ACK_DELAY   25
#define QUIC_TP_DEF_ACTIVE_CID      2

// Bounds the RFC puts on parameters that have them.
#define QUIC_TP_MIN_UDP_PAYLOAD     1200
#define QUIC_TP_MAX_ACK_DELAY_EXP   20
#define QUIC_TP_MAX_ACK_DELAY_MS    16384
#define QUIC_TP_MAX_STREAMS         (UINT64_C(1) << 60)

_Use_decl_annotations_
void _quicTpDefaults(QuicTransportParams* tp)
{
    memset(tp, 0, sizeof(*tp));
    tp->maxUdpPayload   = QUIC_TP_DEF_MAX_UDP_PAYLOAD;
    tp->ackDelayExponent = QUIC_TP_DEF_ACK_DELAY_EXP;
    tp->maxAckDelay     = QUIC_TP_DEF_MAX_ACK_DELAY;
    tp->activeCidLimit  = QUIC_TP_DEF_ACTIVE_CID;
}

static void tpInt(_Inout_ QuicWr* wr, uint64 id, uint64 val)
{
    _quicWrVarint(wr, id);
    _quicWrVarint(wr, _quicVarintSize(val));
    _quicWrVarint(wr, val);
}

static void tpCid(_Inout_ QuicWr* wr, uint64 id, _In_ const QuicCid* cid)
{
    _quicWrVarint(wr, id);
    _quicWrVarintBytes(wr, cid->id, cid->len);
}

_Use_decl_annotations_
bool _quicTpEncode(QuicWr* wr, const QuicTransportParams* tp, bool server)
{
    if (tp->haveInitScid)
        tpCid(wr, QUIC_TP_INIT_SCID, &tp->initScid);

    // The three parameters below are how a client checks that nothing rewrote the connection IDs
    // in the packets that went out before either end had authenticated keys. Only a server has
    // anything to put in them.
    if (server) {
        if (tp->haveOrigDcid)
            tpCid(wr, QUIC_TP_ORIG_DCID, &tp->origDcid);
        if (tp->haveRetryScid)
            tpCid(wr, QUIC_TP_RETRY_SCID, &tp->retryScid);
        if (tp->haveResetToken) {
            _quicWrVarint(wr, QUIC_TP_STATELESS_RESET);
            _quicWrVarintBytes(wr, tp->resetToken, QUIC_RESET_TOKEN_LEN);
        }
    }

    if (tp->maxIdleTimeout != 0)
        tpInt(wr, QUIC_TP_MAX_IDLE_TIMEOUT, tp->maxIdleTimeout);
    if (tp->maxUdpPayload != QUIC_TP_DEF_MAX_UDP_PAYLOAD)
        tpInt(wr, QUIC_TP_MAX_UDP_PAYLOAD, tp->maxUdpPayload);
    if (tp->initMaxData != 0)
        tpInt(wr, QUIC_TP_INIT_MAX_DATA, tp->initMaxData);
    if (tp->initMaxSdBidiLocal != 0)
        tpInt(wr, QUIC_TP_INIT_MAX_SD_BIDI_L, tp->initMaxSdBidiLocal);
    if (tp->initMaxSdBidiRemote != 0)
        tpInt(wr, QUIC_TP_INIT_MAX_SD_BIDI_R, tp->initMaxSdBidiRemote);
    if (tp->initMaxSdUni != 0)
        tpInt(wr, QUIC_TP_INIT_MAX_SD_UNI, tp->initMaxSdUni);
    if (tp->initMaxStreamsBidi != 0)
        tpInt(wr, QUIC_TP_INIT_MAX_STR_BIDI, tp->initMaxStreamsBidi);
    if (tp->initMaxStreamsUni != 0)
        tpInt(wr, QUIC_TP_INIT_MAX_STR_UNI, tp->initMaxStreamsUni);
    if (tp->ackDelayExponent != QUIC_TP_DEF_ACK_DELAY_EXP)
        tpInt(wr, QUIC_TP_ACK_DELAY_EXPONENT, tp->ackDelayExponent);
    if (tp->maxAckDelay != QUIC_TP_DEF_MAX_ACK_DELAY)
        tpInt(wr, QUIC_TP_MAX_ACK_DELAY, tp->maxAckDelay);
    if (tp->activeCidLimit != QUIC_TP_DEF_ACTIVE_CID)
        tpInt(wr, QUIC_TP_ACTIVE_CID_LIMIT, tp->activeCidLimit);

    if (tp->disableMigration) {
        _quicWrVarint(wr, QUIC_TP_DISABLE_MIGRATION);
        _quicWrVarint(wr, 0);
    }

    return !wr->bad;
}

// Reads a parameter whose value is a single varint, rejecting a value that does not fill the
// declared length exactly. A short or long encoding is a malformed parameter rather than
// something to accept quietly, since the length is what the next parameter starts after.
static bool tpRdInt(_In_reads_(len) const uint8* val, size_t len, _Out_ uint64* out)
{
    QuicRd rd;
    _quicRdInit(&rd, val, len);
    *out = _quicRdVarint(&rd);
    return !rd.bad && _quicRdLeft(&rd) == 0;
}

static bool tpRdCid(_In_reads_(len) const uint8* val, size_t len, _Out_ QuicCid* out)
{
    memset(out, 0, sizeof(*out));
    if (len > QUIC_MAX_CID)
        return false;

    out->len = (uint8)len;
    memcpy(out->id, val, len);
    return true;
}

_Use_decl_annotations_
bool _quicTpDecode(QuicTransportParams* tp, const uint8* data, size_t len, bool fromServer)
{
    _quicTpDefaults(tp);

    QuicRd rd;
    _quicRdInit(&rd, data, len);

    // Every id the RFC defines fits in the low bits of a mask, which is all duplicate detection
    // needs: ids past those are unknown to this version and are skipped without being recorded.
    uint32 seen = 0;

    while (_quicRdLeft(&rd) > 0) {
        uint64 id = _quicRdVarint(&rd);

        size_t vlen      = 0;
        const uint8* val = _quicRdVarintBytes(&rd, &vlen);
        if (!val)
            return false;

        if (id > QUIC_TP_RETRY_SCID)
            continue;   // a parameter from a later version, or GREASE

        if (seen & (1u << id))
            return false;
        seen |= 1u << id;

        // A client has no legitimate value for any of these, and accepting one would let it
        // dictate the connection IDs a server believes it used.
        if (!fromServer && (id == QUIC_TP_ORIG_DCID || id == QUIC_TP_RETRY_SCID ||
                            id == QUIC_TP_STATELESS_RESET || id == QUIC_TP_PREFERRED_ADDRESS))
            return false;

        uint64 v = 0;

        switch (id) {
        case QUIC_TP_ORIG_DCID:
            if (!tpRdCid(val, vlen, &tp->origDcid))
                return false;
            tp->haveOrigDcid = true;
            break;

        case QUIC_TP_INIT_SCID:
            if (!tpRdCid(val, vlen, &tp->initScid))
                return false;
            tp->haveInitScid = true;
            break;

        case QUIC_TP_RETRY_SCID:
            if (!tpRdCid(val, vlen, &tp->retryScid))
                return false;
            tp->haveRetryScid = true;
            break;

        case QUIC_TP_STATELESS_RESET:
            if (vlen != QUIC_RESET_TOKEN_LEN)
                return false;
            memcpy(tp->resetToken, val, QUIC_RESET_TOKEN_LEN);
            tp->haveResetToken = true;
            break;

        case QUIC_TP_DISABLE_MIGRATION:
            if (vlen != 0)
                return false;
            tp->disableMigration = true;
            break;

        // Carries a preferred address and a connection ID to use on it. cx does not migrate to
        // one, but the parameter still has to parse: a malformed value is an error even when the
        // contents are not going to be used.
        case QUIC_TP_PREFERRED_ADDRESS:
            if (vlen < 4 + 2 + 16 + 2 + 1)
                return false;
            if (vlen != (size_t)(4 + 2 + 16 + 2 + 1) + val[4 + 2 + 16 + 2] + QUIC_RESET_TOKEN_LEN)
                return false;
            if (val[4 + 2 + 16 + 2] > QUIC_MAX_CID)
                return false;
            break;

        default:
            if (!tpRdInt(val, vlen, &v))
                return false;
            break;
        }

        switch (id) {
        case QUIC_TP_MAX_IDLE_TIMEOUT:   tp->maxIdleTimeout      = v; break;
        case QUIC_TP_INIT_MAX_DATA:      tp->initMaxData         = v; break;
        case QUIC_TP_INIT_MAX_SD_BIDI_L: tp->initMaxSdBidiLocal  = v; break;
        case QUIC_TP_INIT_MAX_SD_BIDI_R: tp->initMaxSdBidiRemote = v; break;
        case QUIC_TP_INIT_MAX_SD_UNI:    tp->initMaxSdUni        = v; break;

        case QUIC_TP_MAX_UDP_PAYLOAD:
            if (v < QUIC_TP_MIN_UDP_PAYLOAD)
                return false;
            tp->maxUdpPayload = v;
            break;

        case QUIC_TP_INIT_MAX_STR_BIDI:
        case QUIC_TP_INIT_MAX_STR_UNI:
            if (v > QUIC_TP_MAX_STREAMS)
                return false;
            if (id == QUIC_TP_INIT_MAX_STR_BIDI)
                tp->initMaxStreamsBidi = v;
            else
                tp->initMaxStreamsUni = v;
            break;

        case QUIC_TP_ACK_DELAY_EXPONENT:
            if (v > QUIC_TP_MAX_ACK_DELAY_EXP)
                return false;
            tp->ackDelayExponent = v;
            break;

        case QUIC_TP_MAX_ACK_DELAY:
            if (v >= QUIC_TP_MAX_ACK_DELAY_MS)
                return false;
            tp->maxAckDelay = v;
            break;

        case QUIC_TP_ACTIVE_CID_LIMIT:
            if (v < QUIC_TP_DEF_ACTIVE_CID)
                return false;
            tp->activeCidLimit = v;
            break;

        default:
            break;
        }
    }

    // An endpoint always states the connection ID it put in its own first packet, and a client
    // additionally needs the server's original destination connection ID to check against the one
    // it chose. Without them there is nothing to compare and the handshake is unauthenticated in
    // exactly the way these parameters exist to prevent.
    if (!tp->haveInitScid)
        return false;
    if (fromServer && !tp->haveOrigDcid)
        return false;

    return !rd.bad;
}
