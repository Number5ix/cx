#include "http3_private.h"

#include <string.h>

// The connection-wide half of HTTP/3: the control stream in each direction, SETTINGS, GOAWAY, and
// working out what each unidirectional stream the peer opened is for.
//
// Nothing here touches a socket. The connection classes read bytes off a stream and hand them in,
// and take back either zero or the error code the connection has to be closed with -- which is
// what lets a malformed peer be built by hand in a test rather than having to be found in the
// wild.

bool _http3Available(void)
{
    return true;
}

_Use_decl_annotations_
void _h3SessionInit(Http3Session* s, bool server, const HttpLimits* limits)
{
    memset(s, 0, sizeof(*s));
    s->server = server;

    // A dynamic table capacity of zero is what forbids the peer from using one, which is the whole
    // of cxhttp's QPACK scope decision. Advertising zero blocked streams says the same thing a
    // second way: no field section may ever wait on table state.
    s->local.qpackMaxTableCapacity = 0;
    s->local.qpackBlockedStreams   = 0;
    s->local.maxFieldSectionSize   = limits ? limits->maxHeadBytes : 0;
}

_Use_decl_annotations_
void _h3SessionDestroy(Http3Session* s)
{
    memset(s, 0, sizeof(*s));
}

_Use_decl_annotations_
bool _h3SessionControlPreamble(const Http3Session* s, strhandle out)
{
    uint8 settings[64];
    H3Wr sw;
    _h3WrInit(&sw, settings, sizeof(settings));

    // Both QPACK settings default to zero, so sending them changes nothing on the wire. They go
    // out anyway because a capture of a connection that will not use the dynamic table should say
    // so rather than leave it to be inferred from an absence.
    _h3WrVarint(&sw, H3_SETTING_QPACK_MAX_TABLE_CAPACITY);
    _h3WrVarint(&sw, s->local.qpackMaxTableCapacity);
    _h3WrVarint(&sw, H3_SETTING_QPACK_BLOCKED_STREAMS);
    _h3WrVarint(&sw, s->local.qpackBlockedStreams);
    if (s->local.maxFieldSectionSize > 0) {
        _h3WrVarint(&sw, H3_SETTING_MAX_FIELD_SECTION_SIZE);
        _h3WrVarint(&sw, s->local.maxFieldSectionSize);
    }
    if (sw.bad)
        return false;

    uint8 hdr[1 + H3_FRAME_HDR_MAX];
    H3Wr hw;
    _h3WrInit(&hw, hdr, sizeof(hdr));
    _h3WrVarint(&hw, H3_STREAM_CONTROL);
    _h3WrVarint(&hw, H3_FRAME_SETTINGS);
    _h3WrVarint(&hw, _h3WrLen(&sw));
    if (hw.bad)
        return false;

    return _httpAppendBytes(out, hdr, _h3WrLen(&hw)) &&
           _httpAppendBytes(out, settings, _h3WrLen(&sw));
}

_Use_decl_annotations_
bool _h3SessionUniPrefix(strhandle out, uint64 type)
{
    uint8 buf[8];
    H3Wr wr;
    _h3WrInit(&wr, buf, sizeof(buf));
    _h3WrVarint(&wr, type);
    return !wr.bad && _httpAppendBytes(out, buf, _h3WrLen(&wr));
}

_Use_decl_annotations_
bool _h3SessionGoawayFrame(Http3Session* s, strhandle out, uint64 id)
{
    // RFC 9114 section 5.2 lets an endpoint send several GOAWAYs as it narrows down what it will
    // still serve, but never a larger identifier than one already sent: the peer has already been
    // told that everything above the old one was not processed and may be retried elsewhere.
    if (s->goawaySent && id > s->goawaySentId)
        return false;

    uint8 buf[H3_FRAME_HDR_MAX + 8];
    H3Wr wr;
    _h3WrInit(&wr, buf, sizeof(buf));
    _h3WrVarint(&wr, H3_FRAME_GOAWAY);
    _h3WrVarint(&wr, _h3VarintSize(id));
    _h3WrVarint(&wr, id);
    if (wr.bad || !_httpAppendBytes(out, buf, _h3WrLen(&wr)))
        return false;

    s->goawaySent   = true;
    s->goawaySentId = id;
    return true;
}

_Use_decl_annotations_
void _h3UniInit(H3UniStream* u, uint64 id)
{
    memset(u, 0, sizeof(*u));
    u->id = id;
    bufringInit(&u->in, 256);
    _h3FrameReaderInit(&u->fr);
}

_Use_decl_annotations_
void _h3UniDestroy(H3UniStream* u)
{
    bufringDestroy(&u->in);
    xaDestroy(&u->frame);
    u->frameLen = 0;
}

// ---------------------------------------------------------------------------------------------
// Control stream frames
// ---------------------------------------------------------------------------------------------

static uint64 parseSettings(_Inout_ Http3Session* s, _In_reads_bytes_(len) const uint8* data,
                            size_t len)
{
    H3Rd rd;
    _h3RdInit(&rd, data, len);

    // Duplicate detection covers the identifiers cxhttp acts on, one bit each. A peer repeating an
    // identifier this endpoint does not implement is equally against the rules and equally
    // harmless -- both copies are discarded -- and tracking every identifier it might invent would
    // let it choose how much work we do.
    uint32 seen = 0;

    while (_h3RdLeft(&rd) > 0) {
        uint64 id  = _h3RdVarint(&rd);
        uint64 val = _h3RdVarint(&rd);
        if (rd.bad)
            return H3ERR_FRAME_ERROR;

        // The HTTP/2 settings with no HTTP/3 counterpart are reserved rather than merely unknown,
        // so a peer sending one is confused about which protocol it is speaking.
        if (id >= 0x02 && id <= 0x05)
            return H3ERR_SETTINGS_ERROR;

        uint32 bit = 0;
        switch (id) {
        case H3_SETTING_QPACK_MAX_TABLE_CAPACITY:
            bit                          = 1;
            s->peer.qpackMaxTableCapacity = val;
            break;
        case H3_SETTING_MAX_FIELD_SECTION_SIZE:
            bit                         = 2;
            s->peer.maxFieldSectionSize = val;
            break;
        case H3_SETTING_QPACK_BLOCKED_STREAMS:
            bit                        = 4;
            s->peer.qpackBlockedStreams = val;
            break;
        case H3_SETTING_ENABLE_CONNECT_PROTOCOL:
            if (val > 1)
                return H3ERR_SETTINGS_ERROR;
            bit                            = 8;
            s->peer.enableConnectProtocol = val != 0;
            break;
        default:
            break;   // unknown, and therefore ignored -- which is what makes greasing work
        }

        if (bit) {
            if (seen & bit)
                return H3ERR_SETTINGS_ERROR;
            seen |= bit;
        }
    }

    s->peerSettings = true;
    return 0;
}

static uint64 parseGoaway(_Inout_ Http3Session* s, _In_reads_bytes_(len) const uint8* data,
                          size_t len)
{
    H3Rd rd;
    _h3RdInit(&rd, data, len);

    uint64 id = _h3RdVarint(&rd);
    if (rd.bad || _h3RdLeft(&rd) != 0)
        return H3ERR_FRAME_ERROR;

    // A server's GOAWAY names a request stream, which is always client-initiated and
    // bidirectional -- the low two bits of such an id are both zero. A client's names a push id,
    // which has no such shape.
    if (!s->server && (id & 0x03) != 0)
        return H3ERR_ID_ERROR;

    if (s->goawayRecvd && id > s->goawayId)
        return H3ERR_ID_ERROR;

    s->goawayRecvd = true;
    s->goawayId    = id;
    return 0;
}

// Whether a frame type may appear on the control stream at all. Decided from the type alone and
// checked as soon as the header arrives, so a bogus frame is refused before its payload has been
// read rather than after.
static uint64 controlFrameAllowed(_In_ const Http3Session* s, uint64 type)
{
    switch (type) {
    case H3_FRAME_SETTINGS:
        // SETTINGS is the first frame on the control stream and appears exactly once, so a second
        // one is not a settings problem but a framing one.
        return s->peerSettings ? H3ERR_FRAME_UNEXPECTED : 0;

    case H3_FRAME_GOAWAY:
        return 0;

    case H3_FRAME_MAX_PUSH_ID:
        // Only a client sends this, so a client receiving one has a server that is confused about
        // its own role.
        return s->server ? 0 : H3ERR_FRAME_UNEXPECTED;

    case H3_FRAME_CANCEL_PUSH:
        // cxhttp never sends PUSH_PROMISE and never sends MAX_PUSH_ID, so no push id has ever been
        // mentioned by either end and there is nothing a cancel could name.
        return H3ERR_ID_ERROR;

    case H3_FRAME_DATA:
    case H3_FRAME_HEADERS:
    case H3_FRAME_PUSH_PROMISE:
        return H3ERR_FRAME_UNEXPECTED;

    default:
        // The HTTP/2 frame types with no HTTP/3 counterpart are reserved, and receiving one means
        // the peer is speaking the wrong protocol rather than a newer one.
        if (type == 0x02 || type == 0x06 || type == 0x08 || type == 0x09)
            return H3ERR_FRAME_UNEXPECTED;
        return 0;   // unknown or reserved for greasing: read and discarded
    }
}

// The three control frames whose payload is worth keeping. Everything else that gets this far is
// unknown or reserved for greasing, so its payload is discarded as it arrives and never allocated
// for -- which is also what stops a peer from choosing how much memory a frame we ignore costs.
static bool controlFrameKept(uint64 type)
{
    return type == H3_FRAME_SETTINGS || type == H3_FRAME_GOAWAY || type == H3_FRAME_MAX_PUSH_ID;
}

static uint64 controlFrame(_Inout_ Http3Session* s, uint64 type,
                           _In_reads_bytes_opt_(len) const uint8* data, size_t len)
{
    switch (type) {
    case H3_FRAME_SETTINGS:
        return parseSettings(s, data, len);

    case H3_FRAME_GOAWAY:
        return parseGoaway(s, data, len);

    case H3_FRAME_MAX_PUSH_ID: {
        H3Rd rd;
        _h3RdInit(&rd, data, len);
        uint64 id = _h3RdVarint(&rd);
        if (rd.bad || _h3RdLeft(&rd) != 0)
            return H3ERR_FRAME_ERROR;

        // The limit may be raised but never lowered. Stored as one past the largest permitted id
        // so that "none yet" and "zero permitted" stay distinguishable.
        if (s->maxPushId > 0 && id + 1 < s->maxPushId)
            return H3ERR_ID_ERROR;
        s->maxPushId = id + 1;
        return 0;
    }

    default:
        return 0;
    }
}

static uint64 runControl(_Inout_ Http3Session* s, _Inout_ H3UniStream* u)
{
    for (;;) {
        H3FrameResult r = _h3FrameReaderStep(&u->fr, &u->in);

        switch (r) {
        case H3FR_NeedMore:
            return 0;

        case H3FR_Error:
            return H3ERR_GENERAL_PROTOCOL_ERROR;

        case H3FR_Header: {
            // Nothing may precede SETTINGS on the control stream, which is what stops a peer from
            // changing the terms after this endpoint has already acted on the defaults.
            if (!s->peerSettings && u->fr.type != H3_FRAME_SETTINGS)
                return H3ERR_MISSING_SETTINGS;

            uint64 err = controlFrameAllowed(s, u->fr.type);
            if (err != 0)
                return err;

            xaDestroy(&u->frame);
            u->frameLen  = 0;
            u->frameKeep = controlFrameKept(u->fr.type);
            if (u->frameKeep) {
                if (u->fr.len > H3_MAX_CONTROL_FRAME)
                    return H3ERR_EXCESSIVE_LOAD;
                if (u->fr.len > 0)
                    u->frame = xaAlloc((size_t)u->fr.len);
            }
            break;
        }

        case H3FR_Payload:
            if (u->frameKeep)
                u->frameLen += bufringRead(&u->in, u->frame + u->frameLen, u->fr.avail);
            else
                bufringSkip(&u->in, u->fr.avail);
            break;

        case H3FR_Complete: {
            if (!u->frameKeep)
                break;
            uint64 err = controlFrame(s, u->fr.type, u->frame, u->frameLen);
            if (err != 0)
                return err;
            break;
        }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Unidirectional streams
// ---------------------------------------------------------------------------------------------

// Classify a stream from the type varint that just arrived on it.
static uint64 classifyUni(_Inout_ Http3Session* s, _Inout_ H3UniStream* u, uint64 type)
{
    switch (type) {
    case H3_STREAM_CONTROL:
        if (s->peerControl)
            return H3ERR_STREAM_CREATION_ERROR;
        s->peerControl   = true;
        s->peerControlId = u->id;
        u->kind          = H3UNI_Control;
        return 0;

    case H3_STREAM_PUSH:
        // A server has no business receiving a push stream at all, and a client only after it has
        // said how many pushes it will take -- which cxhttp never does, because never sending
        // MAX_PUSH_ID is the whole of refusing server push.
        return s->server ? H3ERR_STREAM_CREATION_ERROR : H3ERR_ID_ERROR;

    case H3_STREAM_QPACK_ENCODER:
        if (s->peerEncoderStream)
            return H3ERR_STREAM_CREATION_ERROR;
        s->peerEncoderStream = true;
        u->kind              = H3UNI_QpackEncoder;
        return 0;

    case H3_STREAM_QPACK_DECODER:
        if (s->peerDecoderStream)
            return H3ERR_STREAM_CREATION_ERROR;
        s->peerDecoderStream = true;
        u->kind              = H3UNI_QpackDecoder;
        return 0;

    default:
        u->kind = H3UNI_Ignored;
        return 0;
    }
}

// The peer's QPACK encoder stream. With a dynamic table capacity of zero the only instruction that
// can legitimately appear is a Set Dynamic Table Capacity of zero, which says nothing; anything
// else is the peer building table state this endpoint will not have, so its next field section
// would decode to something other than what it sent.
static uint64 runQpackEncoder(_Inout_ H3UniStream* u)
{
    for (;;) {
        uint8 b;
        if (bufringPeek(&u->in, &b, 0, 1) != 1)
            return 0;

        if ((b & 0xe0) != 0x20 || (b & 0x1f) != 0)
            return H3ERR_QPACK_ENCODER_STREAM_ERROR;

        bufringSkip(&u->in, 1);
    }
}

_Use_decl_annotations_
uint64 _h3SessionUniRecv(Http3Session* s, H3UniStream* u, const uint8* data, size_t len)
{
    if (len > 0)
        bufringWrite(&u->in, data, len);

    if (!u->typeRead) {
        uint8 b[8];
        if (bufringPeek(&u->in, b, 0, 1) != 1)
            return 0;

        size_t n = (size_t)1u << (b[0] >> 6);
        if (bufringPeek(&u->in, b, 0, n) != n)
            return 0;

        uint64 type = b[0] & 0x3f;
        for (size_t i = 1; i < n; i++)
            type = (type << 8) | b[i];

        bufringSkip(&u->in, n);
        u->typeRead = true;

        uint64 err = classifyUni(s, u, type);
        if (err != 0)
            return err;
    }

    switch (u->kind) {
    case H3UNI_Control:
        return runControl(s, u);

    case H3UNI_QpackEncoder:
        return runQpackEncoder(u);

    case H3UNI_QpackDecoder:
        // The peer's decoder stream carries acknowledgements for this endpoint's encoder, and this
        // endpoint's encoder never references the dynamic table, so there is nothing here worth
        // reading. Discarded rather than refused: a peer that sends one anyway costs us nothing,
        // and refusing would fail a connection that is otherwise working.
        bufringSkip(&u->in, u->in.total);
        return 0;

    default:
        bufringSkip(&u->in, u->in.total);
        return 0;
    }
}

_Use_decl_annotations_
uint64 _h3SessionUniEnd(Http3Session* s, H3UniStream* u)
{
    unused_noeval(s);

    // The control stream and both QPACK streams last as long as the connection does. A peer that
    // ends one has ended the connection, whether or not it meant to.
    switch (u->kind) {
    case H3UNI_Control:
    case H3UNI_QpackEncoder:
    case H3UNI_QpackDecoder:
        return H3ERR_CLOSED_CRITICAL_STREAM;
    default:
        return 0;
    }
}

_Use_decl_annotations_
bool _h3SessionGoneAway(const Http3Session* s, uint64 streamId)
{
    return s->goawayRecvd && streamId >= s->goawayId;
}

// ---------------------------------------------------------------------------------------------
// The connection's own streams
// ---------------------------------------------------------------------------------------------

// A unidirectional stream this endpoint opened. It never carries anything inbound, so there is
// nothing to read and nothing to tear down beyond the flow itself.
static const NetHandlers kOwnUniHandlers = { 0 };

_Use_decl_annotations_
bool _h3OpenUniStreams(NetSocket* sock, const Http3Session* s, NetFlow** control, NetFlow** enc,
                       NetFlow** dec)
{
    *control = *enc = *dec = NULL;

    NetFlow* ctl = netquicOpen(sock, true);
    NetFlow* e   = netquicOpen(sock, true);
    NetFlow* d   = netquicOpen(sock, true);

    string out = 0;
    bool ok    = ctl && e && d;

    ok = ok && _h3SessionControlPreamble(s, &out) &&
         netflowSend(ctl, (uint8*)strPC(&out), strLen(out), 0);
    strClear(&out);

    ok = ok && _h3SessionUniPrefix(&out, H3_STREAM_QPACK_ENCODER) &&
         netflowSend(e, (uint8*)strPC(&out), strLen(out), 0);
    strClear(&out);

    ok = ok && _h3SessionUniPrefix(&out, H3_STREAM_QPACK_DECODER) &&
         netflowSend(d, (uint8*)strPC(&out), strLen(out), 0);
    strDestroy(&out);

    if (!ok) {
        objRelease(&ctl);
        objRelease(&e);
        objRelease(&d);
        return false;
    }

    netflowSetHandlers(ctl, &kOwnUniHandlers, NULL);
    netflowSetHandlers(e, &kOwnUniHandlers, NULL);
    netflowSetHandlers(d, &kOwnUniHandlers, NULL);

    *control = ctl;
    *enc     = e;
    *dec     = d;
    return true;
}

static void uniOnRecv(NetEvent* ev)
{
    H3Uni* su = (H3Uni*)ev->ctx;

    // Resolving the connection is what proves the session behind it is still there; nothing below
    // touches it otherwise.
    ObjInst* conn = objAcquireFromWeak(ObjInst, su->conn);
    if (!conn)
        return;

    uint8 buf[HTTP3_READ_CHUNK];
    size_t n;
    bool fin   = false;
    bool ended = false;
    uint64 err = 0;

    while (!err && (n = netquicRecv(ev->flow, buf, sizeof(buf), &fin)) > 0) {
        ended = ended || fin;
        err   = _h3SessionUniRecv(su->session, &su->u, buf, n);
    }

    if (!err && (ended || fin))
        err = _h3SessionUniEnd(su->session, &su->u);

    if (err && su->onError)
        su->onError(conn, err);

    objRelease(&conn);
}

static void uniOnClosed(NetEvent* ev)
{
    H3Uni* su = (H3Uni*)ev->ctx;

    ObjInst* conn = objAcquireFromWeak(ObjInst, su->conn);
    if (conn) {
        // The control stream and both QPACK streams last as long as the connection does, so the
        // peer ending one has ended the connection whether or not it meant to.
        uint64 err = _h3SessionUniEnd(su->session, &su->u);
        if (err && su->onError)
            su->onError(conn, err);
        objRelease(&conn);
    }

    // The flow is going away, so nothing may reach this state again.
    netflowSetHandlers(ev->flow, NULL, NULL);

    _h3UniDestroy(&su->u);
    objDestroyWeak(&su->conn);
    xaFree(su);
}

static const NetHandlers kUniHandlers = {
    .recv       = uniOnRecv,
    .flowClosed = uniOnClosed,
};

_Use_decl_annotations_
void _h3UniAttach(NetFlow* flow, ObjInst* conn, Http3Session* s, H3ConnErrorCB onError)
{
    H3Uni* su = xaAlloc(sizeof(H3Uni), XA_Zero);
    _h3UniInit(&su->u, flow->key);
    su->conn    = objGetWeak(ObjInst, conn);
    su->session = s;
    su->onError = onError;

    netflowSetHandlers(flow, &kUniHandlers, su);
}

_Use_decl_annotations_
HttpError _h3ErrorToHttp(uint64 code)
{
    switch (code) {
    case H3ERR_REQUEST_CANCELLED:
    case H3ERR_REQUEST_REJECTED:
        return HTTPERR_Aborted;

    case H3ERR_NO_ERROR:
    case H3ERR_REQUEST_INCOMPLETE:
        return HTTPERR_Closed;

    case H3ERR_EXCESSIVE_LOAD:
        return HTTPERR_TooLarge;

    case H3ERR_GENERAL_PROTOCOL_ERROR:
    case H3ERR_STREAM_CREATION_ERROR:
    case H3ERR_CLOSED_CRITICAL_STREAM:
    case H3ERR_FRAME_UNEXPECTED:
    case H3ERR_FRAME_ERROR:
    case H3ERR_ID_ERROR:
    case H3ERR_SETTINGS_ERROR:
    case H3ERR_MISSING_SETTINGS:
    case H3ERR_MESSAGE_ERROR:
    case H3ERR_CONNECT_ERROR:
    case H3ERR_QPACK_DECOMPRESSION_FAILED:
    case H3ERR_QPACK_ENCODER_STREAM_ERROR:
    case H3ERR_QPACK_DECODER_STREAM_ERROR:
        return HTTPERR_BadMessage;

    default:
        // Everything else is a QUIC transport code, or an application code from some other
        // protocol entirely -- either way the transport failed rather than the message.
        return HTTPERR_Network;
    }
}
