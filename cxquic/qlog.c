#include "qlog_private.h"

#include <cx/fs/file.h>
#include <cx/fs/path.h>
#include <cx/serialize/jsonout.h>
#include <cx/string.h>
#include <cx/time/clock.h>
#include <cx/time/time.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL QuicLogChannel

// JSON-SEQ framing: every record is preceded by a record separator and followed by a newline,
// which is what lets a reader recover from a partial write at the end of a crashed process.
#define QLOG_RS 0x1e

// How much is held before it goes to the file. Writing every event as it happens would make the
// log part of what it is measuring: a syscall per packet is enough to change the timing of the
// connection being logged, which is exactly the timing a stall has to be read out of.
#define QLOG_BUFSZ (64 * 1024)

struct QuicQlog {
    FSFile* file;
    int64 t0;        // clockTimer() when the connection started; every event time is relative
    string line;     // one event, rebuilt in place rather than reallocated per event
    string pending;  // events written but not yet handed to the file
};

static string qlogDir;

_Use_decl_annotations_
void _quicQlogDir(strref dir)
{
    strDup(&qlogDir, dir);
}

_Use_decl_annotations_
bool _quicQlogEnabled(void)
{
    return strLen(qlogDir) > 0;
}

// ---------------------------------------------------------------------------------------------
// Building one event
// ---------------------------------------------------------------------------------------------

static void put(_Inout_ QuicQlog* ql, _In_opt_ strref s)
{
    strAppend(&ql->line, s);
}

static void putU(_Inout_ QuicQlog* ql, uint64 v)
{
    string tmp = 0;
    strFromUInt64(&tmp, v, 10);
    strAppend(&ql->line, tmp);
    strDestroy(&tmp);
}

// Names and quotes a string field. Everything written through here is produced by cxquic itself
// -- event names, frame names, hexadecimal -- except the close reason, which is escaped.
static void putKeyStr(_Inout_ QuicQlog* ql, _In_ strref key, _In_opt_ strref val)
{
    put(ql, key);
    put(ql, _S "\":\"");
    put(ql, val);
    put(ql, _S "\"");
}

static void putKeyU(_Inout_ QuicQlog* ql, _In_ strref key, uint64 val)
{
    put(ql, key);
    put(ql, _S "\":");
    putU(ql, val);
}

static void putHex(_Inout_ QuicQlog* ql, _In_reads_bytes_(len) const uint8* data, size_t len)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        strAppendChar(&ql->line, (uint8)digits[data[i] >> 4]);
        strAppendChar(&ql->line, (uint8)digits[data[i] & 0xf]);
    }
}

// Starts an event: the separator, the time relative to the connection, and the name.
static void evStart(_Inout_ QuicQlog* ql, _In_ strref name)
{
    strClear(&ql->line);
    strAppendChar(&ql->line, (char)QLOG_RS);

    // Milliseconds with the microseconds after the point, which is the resolution every clock
    // here has and the resolution the qlog tools expect.
    int64 us = clockTimer() - ql->t0;
    if (us < 0)
        us = 0;

    put(ql, _S "{\"time\":");
    putU(ql, (uint64)(us / 1000));
    put(ql, _S ".");

    string frac = 0;
    strFromUInt64(&frac, (uint64)(us % 1000), 10);
    while (strLen(frac) < 3)
        strPrepend(_S "0", &frac);
    put(ql, frac);
    strDestroy(&frac);

    put(ql, _S ",\"name\":\"");
    put(ql, name);
    put(ql, _S "\",\"data\":{");
}

static void qlogFlush(_Inout_ QuicQlog* ql)
{
    if (strLen(ql->pending) == 0)
        return;

    fsWriteString(ql->file, ql->pending, NULL);
    strClear(&ql->pending);
}

static void evEnd(_Inout_ QuicQlog* ql)
{
    put(ql, _S "}}\n");
    strAppend(&ql->pending, ql->line);

    if (strLen(ql->pending) >= QLOG_BUFSZ)
        qlogFlush(ql);
}

// ---------------------------------------------------------------------------------------------
// The file
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
QuicQlog* _quicQlogCreate(const QuicCid* odcid, bool server)
{
    if (!_quicQlogEnabled())
        return NULL;

    QuicQlog* ql = xaAllocStruct(QuicQlog, XA_Zero);
    ql->t0       = clockTimer();

    string name = 0;
    putHex(ql, odcid->id, odcid->len);   // ql->line is the scratch here, before any event uses it
    strDup(&name, ql->line);
    strClear(&ql->line);

    strAppend(&name, server ? _S "_server.sqlog" : _S "_client.sqlog");

    string path = 0;
    pathJoin(&path, qlogDir, name);
    ql->file = fsOpen(path, FS_Overwrite);
    strDestroy(&path);
    strDestroy(&name);

    if (!ql->file) {
        logFmt(Warn, _SL("QUIC qlog file could not be opened in ${string}"),
               stvar(strref, qlogDir));
        xaFree(ql);
        return NULL;
    }

    // The header, which is a record like any other but carries the trace's own metadata rather
    // than an event.
    strClear(&ql->line);
    strAppendChar(&ql->line, (char)QLOG_RS);
    put(ql, _S "{\"qlog_format\":\"JSON-SEQ\",\"qlog_version\":\"0.3\",\"title\":\"cxquic\",");
    put(ql, _S "\"trace\":{\"vantage_point\":{\"type\":\"");
    put(ql, server ? _S "server" : _S "client");
    put(ql, _S "\"},\"common_fields\":{\"ODCID\":\"");
    putHex(ql, odcid->id, odcid->len);
    put(ql, _S "\",\"time_format\":\"relative\",\"reference_time\":0}}}\n");
    strAppend(&ql->pending, ql->line);

    return ql;
}

_Use_decl_annotations_
void _quicQlogDestroy(QuicQlog** ql)
{
    if (!*ql)
        return;

    qlogFlush(*ql);
    fsClose((*ql)->file);
    strDestroy(&(*ql)->line);
    strDestroy(&(*ql)->pending);
    xaFree(*ql);
    *ql = NULL;
}

// ---------------------------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
void _quicQlogStarted(QuicQlog* ql, const NetAddr* peer)
{
    if (!ql)
        return;

    string addr = 0;
    netAddrToStr(&addr, peer);

    evStart(ql, _S "transport:connection_started");
    put(ql, _S "\"");
    putKeyStr(ql, _S "dst_ip", addr);
    put(ql, _S ",\"");
    putKeyU(ql, _S "dst_port", peer->port);
    evEnd(ql);

    strDestroy(&addr);
}

_Use_decl_annotations_
void _quicQlogClosed(QuicQlog* ql, uint64 error, bool app, bool local, strref reason)
{
    if (!ql)
        return;

    evStart(ql, _S "transport:connection_closed");
    put(ql, _S "\"");
    putKeyStr(ql, _S "owner", local ? _S "local" : _S "remote");
    put(ql, _S ",\"");
    putKeyU(ql, app ? _S "application_code" : _S "connection_code", error);
    if (strLen(reason) > 0) {
        put(ql, _S ",\"reason\":\"");
        jsonStrEscape(&ql->line, reason, 0);
        put(ql, _S "\"");
    }
    evEnd(ql);
}

_Use_decl_annotations_
void _quicQlogParams(QuicQlog* ql, const QuicTransportParams* tp, bool local)
{
    if (!ql)
        return;

    evStart(ql, _S "transport:parameters_set");
    put(ql, _S "\"");
    putKeyStr(ql, _S "owner", local ? _S "local" : _S "remote");
    put(ql, _S ",\"");
    putKeyU(ql, _S "max_idle_timeout", tp->maxIdleTimeout);
    put(ql, _S ",\"");
    putKeyU(ql, _S "max_udp_payload_size", tp->maxUdpPayload);
    put(ql, _S ",\"");
    putKeyU(ql, _S "ack_delay_exponent", tp->ackDelayExponent);
    put(ql, _S ",\"");
    putKeyU(ql, _S "max_ack_delay", tp->maxAckDelay);
    put(ql, _S ",\"");
    putKeyU(ql, _S "active_connection_id_limit", tp->activeCidLimit);
    put(ql, _S ",\"");
    putKeyU(ql, _S "initial_max_data", tp->initMaxData);
    put(ql, _S ",\"");
    putKeyU(ql, _S "initial_max_stream_data_bidi_local", tp->initMaxSdBidiLocal);
    put(ql, _S ",\"");
    putKeyU(ql, _S "initial_max_stream_data_bidi_remote", tp->initMaxSdBidiRemote);
    put(ql, _S ",\"");
    putKeyU(ql, _S "initial_max_stream_data_uni", tp->initMaxSdUni);
    put(ql, _S ",\"");
    putKeyU(ql, _S "initial_max_streams_bidi", tp->initMaxStreamsBidi);
    put(ql, _S ",\"");
    putKeyU(ql, _S "initial_max_streams_uni", tp->initMaxStreamsUni);
    evEnd(ql);
}

_Pure static strref pktTypeName(uint8 type)
{
    switch (type) {
    case QUIC_PKT_INITIAL:    return _S "initial";
    case QUIC_PKT_0RTT:       return _S "0RTT";
    case QUIC_PKT_HANDSHAKE:  return _S "handshake";
    case QUIC_PKT_RETRY:      return _S "retry";
    case QUIC_PKT_SHORT:      return _S "1RTT";
    case QUIC_PKT_VERSIONNEG: return _S "version_negotiation";
    default:                  return _S "unknown";
    }
}

_Pure static strref frameName(uint64 type)
{
    switch (type) {
    case QUIC_FRAME_PADDING:             return _S "padding";
    case QUIC_FRAME_PING:                return _S "ping";
    case QUIC_FRAME_ACK:
    case QUIC_FRAME_ACK_ECN:             return _S "ack";
    case QUIC_FRAME_RESET_STREAM:        return _S "reset_stream";
    case QUIC_FRAME_STOP_SENDING:        return _S "stop_sending";
    case QUIC_FRAME_CRYPTO:              return _S "crypto";
    case QUIC_FRAME_NEW_TOKEN:           return _S "new_token";
    case QUIC_FRAME_MAX_DATA:            return _S "max_data";
    case QUIC_FRAME_MAX_STREAM_DATA:     return _S "max_stream_data";
    case QUIC_FRAME_MAX_STREAMS_BIDI:
    case QUIC_FRAME_MAX_STREAMS_UNI:     return _S "max_streams";
    case QUIC_FRAME_DATA_BLOCKED:        return _S "data_blocked";
    case QUIC_FRAME_STREAM_DATA_BLOCKED: return _S "stream_data_blocked";
    case QUIC_FRAME_STREAMS_BLOCKED_BIDI:
    case QUIC_FRAME_STREAMS_BLOCKED_UNI: return _S "streams_blocked";
    case QUIC_FRAME_NEW_CONNECTION_ID:   return _S "new_connection_id";
    case QUIC_FRAME_RETIRE_CONNECTION_ID: return _S "retire_connection_id";
    case QUIC_FRAME_PATH_CHALLENGE:      return _S "path_challenge";
    case QUIC_FRAME_PATH_RESPONSE:       return _S "path_response";
    case QUIC_FRAME_CONNECTION_CLOSE:
    case QUIC_FRAME_CONNECTION_CLOSE_APP: return _S "connection_close";
    case QUIC_FRAME_HANDSHAKE_DONE:      return _S "handshake_done";
    default:
        if (type >= QUIC_FRAME_STREAM && type < QUIC_FRAME_MAX_DATA)
            return _S "stream";
        return _S "unknown";
    }
}

static void putFrame(_Inout_ QuicQlog* ql, _In_ const QuicFrame* f)
{
    put(ql, _S "{\"");
    putKeyStr(ql, _S "frame_type", frameName(f->type));

    if (f->type >= QUIC_FRAME_STREAM && f->type < QUIC_FRAME_MAX_DATA) {
        put(ql, _S ",\"");
        putKeyU(ql, _S "stream_id", f->stream.id);
        put(ql, _S ",\"");
        putKeyU(ql, _S "offset", f->stream.offset);
        put(ql, _S ",\"");
        putKeyU(ql, _S "length", f->stream.len);
        put(ql, _S ",\"fin\":");
        put(ql, (f->type & QUIC_STREAM_FIN) ? _S "true" : _S "false");
        put(ql, _S "}");
        return;
    }

    switch (f->type) {
    case QUIC_FRAME_ACK:
    case QUIC_FRAME_ACK_ECN: {
        put(ql, _S ",\"acked_ranges\":[");
        QuicAckIter it;
        QuicAckRange r;
        bool first = true;
        if (_quicAckIterInit(&it, f)) {
            while (_quicAckIterNext(&it, &r)) {
                if (!first)
                    put(ql, _S ",");
                first = false;
                put(ql, _S "[");
                putU(ql, r.smallest);
                put(ql, _S ",");
                putU(ql, r.largest);
                put(ql, _S "]");
            }
        }
        put(ql, _S "]");
        break;
    }
    case QUIC_FRAME_CRYPTO:
        put(ql, _S ",\"");
        putKeyU(ql, _S "offset", f->crypto.offset);
        put(ql, _S ",\"");
        putKeyU(ql, _S "length", f->crypto.len);
        break;
    case QUIC_FRAME_MAX_DATA:
        put(ql, _S ",\"");
        putKeyU(ql, _S "maximum", f->maxData.max);
        break;
    case QUIC_FRAME_MAX_STREAM_DATA:
        put(ql, _S ",\"");
        putKeyU(ql, _S "stream_id", f->maxStreamData.id);
        put(ql, _S ",\"");
        putKeyU(ql, _S "maximum", f->maxStreamData.max);
        break;
    case QUIC_FRAME_MAX_STREAMS_BIDI:
    case QUIC_FRAME_MAX_STREAMS_UNI:
        put(ql, _S ",\"");
        putKeyStr(ql, _S "stream_type",
                  f->type == QUIC_FRAME_MAX_STREAMS_BIDI ? _S "bidirectional" : _S "unidirectional");
        put(ql, _S ",\"");
        putKeyU(ql, _S "maximum", f->maxStreams.max);
        break;
    case QUIC_FRAME_DATA_BLOCKED:
        put(ql, _S ",\"");
        putKeyU(ql, _S "limit", f->dataBlocked.limit);
        break;
    case QUIC_FRAME_STREAM_DATA_BLOCKED:
        put(ql, _S ",\"");
        putKeyU(ql, _S "stream_id", f->streamDataBlocked.id);
        put(ql, _S ",\"");
        putKeyU(ql, _S "limit", f->streamDataBlocked.limit);
        break;
    case QUIC_FRAME_STREAMS_BLOCKED_BIDI:
    case QUIC_FRAME_STREAMS_BLOCKED_UNI:
        put(ql, _S ",\"");
        putKeyU(ql, _S "limit", f->streamsBlocked.limit);
        break;
    case QUIC_FRAME_RESET_STREAM:
        put(ql, _S ",\"");
        putKeyU(ql, _S "stream_id", f->resetStream.id);
        put(ql, _S ",\"");
        putKeyU(ql, _S "error_code", f->resetStream.error);
        put(ql, _S ",\"");
        putKeyU(ql, _S "final_size", f->resetStream.finalSize);
        break;
    case QUIC_FRAME_STOP_SENDING:
        put(ql, _S ",\"");
        putKeyU(ql, _S "stream_id", f->stopSending.id);
        put(ql, _S ",\"");
        putKeyU(ql, _S "error_code", f->stopSending.error);
        break;
    case QUIC_FRAME_NEW_CONNECTION_ID:
        put(ql, _S ",\"");
        putKeyU(ql, _S "sequence_number", f->newConnId.seq);
        put(ql, _S ",\"");
        putKeyU(ql, _S "retire_prior_to", f->newConnId.retirePrior);
        break;
    case QUIC_FRAME_RETIRE_CONNECTION_ID:
        put(ql, _S ",\"");
        putKeyU(ql, _S "sequence_number", f->retireConnId.seq);
        break;
    case QUIC_FRAME_CONNECTION_CLOSE:
    case QUIC_FRAME_CONNECTION_CLOSE_APP:
        put(ql, _S ",\"");
        putKeyU(ql, _S "error_code", f->connClose.error);
        break;
    case QUIC_FRAME_PADDING:
        put(ql, _S ",\"");
        putKeyU(ql, _S "payload_length", f->padding);
        break;
    default:
        break;
    }

    put(ql, _S "}");
}

_Use_decl_annotations_
void _quicQlogPacket(QuicQlog* ql, bool sent, const QuicPktHdr* h, size_t len,
                     const uint8* payload, size_t plen)
{
    if (!ql)
        return;

    evStart(ql, sent ? _S "transport:packet_sent" : _S "transport:packet_received");

    put(ql, _S "\"header\":{\"");
    putKeyStr(ql, _S "packet_type", pktTypeName(h->type));
    put(ql, _S ",\"");
    putKeyU(ql, _S "packet_number", h->pn);
    put(ql, _S ",\"dcid\":\"");
    putHex(ql, h->dcid.id, h->dcid.len);
    put(ql, _S "\"},\"raw\":{\"");
    putKeyU(ql, _S "length", len);
    put(ql, _S "},\"frames\":[");

    QuicRd rd;
    _quicRdInit(&rd, payload, plen);

    QuicFrame f;
    bool first = true;
    while (_quicFrameDecode(&f, &rd)) {
        if (!first)
            put(ql, _S ",");
        first = false;
        putFrame(ql, &f);
    }

    put(ql, _S "]");
    evEnd(ql);
}

_Use_decl_annotations_
void _quicQlogDropped(QuicQlog* ql, strref trigger, size_t len)
{
    if (!ql)
        return;

    evStart(ql, _S "transport:packet_dropped");
    put(ql, _S "\"");
    putKeyStr(ql, _S "trigger", trigger);
    put(ql, _S ",\"raw\":{\"");
    putKeyU(ql, _S "length", len);
    put(ql, _S "}");
    evEnd(ql);
}

_Use_decl_annotations_
void _quicQlogMetrics(QuicQlog* ql, const QuicRecovery* r)
{
    if (!ql)
        return;

    evStart(ql, _S "recovery:metrics_updated");
    put(ql, _S "\"");
    putKeyU(ql, _S "smoothed_rtt", (uint64)(r->rtt.smoothed / 1000));
    put(ql, _S ",\"");
    putKeyU(ql, _S "latest_rtt", (uint64)(r->rtt.latest / 1000));
    put(ql, _S ",\"");
    putKeyU(ql, _S "congestion_window", r->window);
    put(ql, _S ",\"");
    putKeyU(ql, _S "bytes_in_flight", r->inFlight);
    evEnd(ql);
}

_Use_decl_annotations_
void _quicQlogLost(QuicQlog* ql, int sp, uint64 pn)
{
    if (!ql)
        return;

    static strref spaceName[QUIC_PNS_COUNT] = { 0 };
    spaceName[QUIC_PNS_INITIAL]   = _S "initial";
    spaceName[QUIC_PNS_HANDSHAKE] = _S "handshake";
    spaceName[QUIC_PNS_APP]       = _S "1RTT";

    evStart(ql, _S "recovery:packet_lost");
    put(ql, _S "\"header\":{\"");
    putKeyStr(ql, _S "packet_type", spaceName[sp]);
    put(ql, _S ",\"");
    putKeyU(ql, _S "packet_number", pn);
    put(ql, _S "}");
    evEnd(ql);
}
