// The QUIC connection state machine (RFC 9000 sections 5 through 14, RFC 9001 section 4).
//
// A connection turns datagrams into frames and back. It drives the TLS handshake over CRYPTO
// frames, keeps the three packet number spaces and their keys, manages the connection IDs each
// end routes by, validates paths, and closes. What it deliberately does not do is decide when to
// retransmit -- that is loss recovery, and it arrives with its own file -- or interpret a STREAM
// frame, which belongs to the stream layer above.
//
// Nothing here touches a socket. Finished datagrams leave through the send handler and arrive
// through _quicConnRecv(), which is what lets two connections be wired to each other in a test
// with a clock the test controls.

#include "conn_private.h"
#include "qlog_private.h"

#include <cx/format.h>

#include <cx/time/clock.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL QuicLogChannel

// Largest amount of handshake data that will be held for reassembly at one encryption level. A
// certificate chain is the biggest thing that legitimately arrives, and this is far more than one
// needs; past it the peer is filling memory rather than handshaking.
#define QUIC_MAX_CRYPTO_BUFFER (64 * 1024)

// The ack_delay_exponent that applies to Initial and Handshake packets. The transport parameter
// arrives too late to cover them, so RFC 9000 section 18.2 fixes them at the default.
#define QUIC_HS_ACK_DELAY_EXP 3

// How many datagrams one _quicConnFlush() will build. Every pass consumes something -- handshake
// bytes, a pending acknowledgement, a queued frame -- so the loop terminates on its own; this
// only bounds how much work one call does.
#define QUIC_FLUSH_MAX_DATAGRAMS 16

// The packet number field plus the payload after it must be at least this long, because header
// protection samples sixteen bytes starting four bytes into that field (RFC 9001 section 5.4.2).
#define QUIC_MIN_PN_PAYLOAD 4

// The least congestion window room that can carry a packet at all: the largest short header this
// endpoint could write, plus the authentication tag and the smallest payload header protection
// leaves room to sample. Room below this is a full window rather than a nearly full one.
#define QUIC_MIN_SEND_ROOM (1 + QUIC_MAX_CID + 4 + QUIC_TAG_LEN + QUIC_MIN_PN_PAYLOAD)

struct QuicConn {
    bool server;
    uint8 state;              // QuicConnState
    uint32 version;

    QuicQlog* qlog;           // NULL unless qlog output was turned on before this was created

    TlsQuic* tls;
    const QuicConnHandlers* handlers;
    void* hctx;

    // The path in use: where packets go, and -- when the platform reported it -- which of this
    // machine's addresses they leave from. `prev` is the path this one replaced, kept until the new
    // one is validated so a move that fails has somewhere to go back to.
    NetAddr peer;
    NetAddr localAddr;
    bool haveLocal;
    NetAddr prevPeer;
    NetAddr prevLocalAddr;
    bool havePrev;
    bool prevHaveLocal;
    int64 pathDeadline;       // when an outstanding PATH_CHALLENGE gives up, or 0
    uint64 pathSent;          // bytes out on the current path while it is unvalidated
    uint64 pathRecv;          // bytes in on it, which is what bounds the above
    bool pathAmpLimited;      // the peer named this path, so what goes out on it is rationed
    uint32 nmigrations;
    NetAddr respondTo;        // where an owed PATH_RESPONSE goes, which is not always the peer
    bool respondElsewhere;

    size_t mtu;

    // Path MTU discovery (RFC 8899). The search is a bisection between a size known to work and
    // one known not to, and `probing` is the candidate currently on the wire.
    uint8 mtuState;           // QuicMtuState
    size_t mtuLow;            // largest size the path has carried
    size_t mtuHigh;           // smallest size it has not, exclusive
    size_t mtuProbing;        // size of the probe in flight, or 0
    uint64 mtuProbePn;
    uint32 mtuTries;          // probes sent at the current candidate
    int64 mtuRaiseAt;         // when to search again after finishing, or 0

    // ECN (RFC 9000 section 13.4).
    uint8 ecnState;           // QuicEcnState
    uint32 ecnTestPkts;       // packets left to mark before the peer's counts must show them

    // 0-RTT (RFC 9001 section 4.6). Only ever one direction: a client writes with `earlyTx`, a
    // server reads with `earlyRx`. 0-RTT and 1-RTT share the application packet number space, so
    // the packets sent under these keys are simply that space's first ones -- which is what lets a
    // rejection name them all without recording anything per packet.
    QuicKeys earlyTx;
    QuicKeys earlyRx;

    // 1-RTT key update (RFC 9001 section 6). `keyPhase` is the phase this endpoint sends under and
    // expects to receive; `rxNext` is the generation after it, derived in advance so a packet
    // carrying the other phase can be tried without deriving anything first, and `rxPrev` the one
    // before it, kept a little while so packets reordered across the change still open.
    QuicKeys rxNext;
    QuicKeys rxPrev;
    bool keyPhase;
    uint64 rxPhaseFirst;      // lowest packet number received in the current phase, or QUIC_PN_NONE
    uint64 txPhaseFirst;      // lowest packet number sent in it
    bool keyPhaseAcked;       // the peer has answered a packet sent in it
    int64 rxPrevUntil;        // when the previous generation is thrown away
    uint32 keyUpdates;        // how many times the keys have moved on
    uint8 earlyState;              // QuicEarlyState
    QuicTransportParams earlyTp;   // client: the limits the resumed session ran under
    bool haveEarlyTp;

    // The time of the call being processed, so a callback from inside the TLS handshake does not
    // have to be handed a clock of its own.
    int64 now;

    QuicTransportParams localTp;
    QuicTransportParams peerTp;
    bool peerTpValid;

    QuicPnSpace pns[QUIC_PNS_COUNT];

    QuicLocalCid local[QUIC_MAX_CIDS];
    QuicRemoteCid remote[QUIC_MAX_CIDS];
    uint64 localNextSeq;
    uint64 remoteRetirePrior; // the peer's retire_prior_to high water mark
    uint8 localCidLen;
    uint32 remoteActive;      // index into `remote` of the ID packets are addressed to
    bool peerCidKnown;        // the server's real Source Connection ID has arrived

    QuicCid origDcid;         // the Destination Connection ID of the client's very first Initial
    QuicCid retryScid;
    bool haveRetry;

    Buffer token;             // client: the address validation token to put in its Initial packets

    bool handshakeComplete;
    bool handshakeConfirmed;
    bool addrValidated;
    bool sentHandshakePkt;    // client: an Initial key discard trigger
    bool recvHandshakePkt;    // server: the same
    bool discardHandshakeSoon;
    uint64 bytesRecv;
    uint64 bytesSent;

    int64 idleDeadline;
    int64 closeDeadline;

    bool closePending;        // a CONNECTION_CLOSE frame is owed
    bool closeLocal;
    bool closeApp;
    uint64 closeError;
    uint64 closeFrameType;
    string closeReason;

    QuicCtl handshakeDone;
    QuicCtl pathResponse;
    QuicCtl pathChallenge;
    bool pathValidated;
    uint8 pathResponseData[QUIC_PATH_DATA_LEN];
    uint8 pathChallengeData[QUIC_PATH_DATA_LEN];

    // Peer connection IDs waiting for a RETIRE_CONNECTION_ID frame to go out and be acknowledged.
    struct {
        uint64 seq;
        QuicCtl ctl;
    } retireQueue[QUIC_MAX_CIDS];
    uint32 nretireQueue;

    QuicRecovery recov;

    // Frames for one packet are built here before the packet they go in exists, since a packet's
    // header cannot be written until its payload length is known.
    uint8 scratch[QUIC_MAX_DATAGRAM];
};

// ---------------------------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------------------------

static int64 idleTimeoutOf(_In_ const QuicConn* c);
static void cidReplenish(_Inout_ QuicConn* c);

static bool cidEq(_In_ const QuicCid* a, _In_ const QuicCid* b)
{
    return a->len == b->len && memcmp(a->id, b->id, a->len) == 0;
}

static bool addrEq(_In_ const NetAddr* a, _In_ const NetAddr* b)
{
    if (a->type != b->type || a->port != b->port)
        return false;
    if (a->type == NA_IPv6)
        return a->scope == b->scope && memcmp(a->ipv6, b->ipv6, sizeof(a->ipv6)) == 0;
    return memcmp(a->ipv4, b->ipv4, sizeof(a->ipv4)) == 0;
}

// ---------------------------------------------------------------------------------------------
// Paths
//
// A path is the pair of addresses packets travel between. Only the peer's half is always known --
// the local half needs the platform to report which of this machine's addresses a datagram arrived
// on, and not every platform can. Where it is missing, a path is just the peer address, which is
// what every QUIC endpoint had before this and is still correct on a machine with one address.
// ---------------------------------------------------------------------------------------------

// The ancillary information the current path wants on an outgoing datagram: leave from the local
// address the peer is reaching this endpoint at, and mark the packet if ECN is still in use.
static void pathInfo(_In_ const QuicConn* c, _Out_ NetPktInfo* info)
{
    memset(info, 0, sizeof(*info));

    if (c->haveLocal) {
        info->local     = c->localAddr;
        info->haveLocal = true;
    }

    if (c->ecnState != QUIC_ECN_OFF) {
        info->ecn     = NET_ECN_Ect0;
        info->haveEcn = true;
    }
}

// Moves the connection onto a new path and starts proving it works. The old one is remembered, so
// a validation that never answers can fall back to something that did.
//
// RFC 9000 section 9.4: everything the congestion controller and the round trip estimate hold is a
// measurement of the path being left, and none of it carries over.
static void pathSwitch(_Inout_ QuicConn* c, _In_ const NetAddr* peer, _In_opt_ const NetAddr* local,
                       bool peerChose, int64 now)
{
    c->prevPeer      = c->peer;
    c->prevLocalAddr = c->localAddr;
    c->prevHaveLocal = c->haveLocal;
    c->havePrev      = true;

    c->peer = *peer;
    if (local) {
        c->localAddr = *local;
        c->haveLocal = true;
    } else {
        c->haveLocal = false;
    }

    c->pathValidated  = false;
    c->pathSent       = 0;
    c->pathRecv       = 0;
    c->pathAmpLimited = peerChose;
    c->nmigrations++;

    // A path that has not been measured has no business carrying a datagram larger than every
    // endpoint must accept, so discovery starts over from the floor. This happens before the
    // congestion state is rebuilt, because the window it starts from is counted in datagrams of
    // whatever size the path is currently believed to carry.
    c->mtu        = QUIC_INITIAL_MTU;
    c->mtuState   = QUIC_MTU_IDLE;
    c->mtuProbing = 0;
    c->mtuProbePn = QUIC_PN_NONE;   // any probe in flight was measuring the path being left
    c->mtuTries   = 0;
    c->mtuRaiseAt = 0;
    _quicRecovSetMaxDatagram(&c->recov, QUIC_INITIAL_MTU);

    _quicRecovOnPathChange(&c->recov, now);

    // ECN is a property of the path, so what was learned about the last one says nothing here.
    c->ecnState    = QUIC_ECN_TESTING;
    c->ecnTestPkts = QUIC_ECN_TEST_PKTS;
    for (int sp = 0; sp < QUIC_PNS_COUNT; sp++) {
        c->pns[sp].ecnEct0 = c->pns[sp].ecnEct1 = c->pns[sp].ecnCe = 0;
        c->pns[sp].ecnAckedMarked = 0;
    }

    _quicConnValidatePath(c);
    c->pathDeadline = now + 3 * _quicRecovPto(&c->recov);
}

// The validation of the current path ran out of time. Falling back to the path that was working
// before is the only move left; without one the connection has nowhere to send.
static void pathValidationFailed(_Inout_ QuicConn* c, int64 now);

// ---------------------------------------------------------------------------------------------
// Path MTU discovery (RFC 8899)
//
// The path is measured by sending a datagram larger than it is known to carry and seeing whether it
// arrives. Every probe is a whole datagram containing nothing but a PING and padding, so one that
// is too large is lost without taking any application data with it, and its loss is the answer
// rather than a failure.
// ---------------------------------------------------------------------------------------------

// The largest datagram worth trying: what cx will send at all, and no more than the peer said it
// would accept.
static size_t mtuCeiling(_In_ const QuicConn* c)
{
    size_t cap = QUIC_MAX_DATAGRAM;
    if (c->peerTpValid && c->peerTp.maxUdpPayload < cap)
        cap = (size_t)c->peerTp.maxUdpPayload;
    if (cap < QUIC_INITIAL_MTU)
        cap = QUIC_INITIAL_MTU;
    return cap;
}

// Begins a search, or finishes one when there is no longer a gap worth closing.
static void mtuSearch(_Inout_ QuicConn* c, int64 now)
{
    size_t ceiling = mtuCeiling(c);
    if (c->mtuHigh > ceiling + 1)
        c->mtuHigh = ceiling + 1;

    if (c->mtuLow < QUIC_INITIAL_MTU)
        c->mtuLow = QUIC_INITIAL_MTU;

    if (c->mtuHigh <= c->mtuLow + QUIC_PMTU_STEP) {
        c->mtuState   = QUIC_MTU_DONE;
        c->mtuProbing = 0;
        c->mtuRaiseAt = now + QUIC_PMTU_RAISE_INTERVAL;
        return;
    }

    c->mtuState   = QUIC_MTU_SEARCHING;
    c->mtuProbing = c->mtuLow + (c->mtuHigh - c->mtuLow) / 2;
    c->mtuTries   = 0;
}

// Starts discovery once there is a connection to measure. Nothing is probed during the handshake:
// the sizes involved are fixed by the RFC, and a probe would compete with it for the window.
static void mtuStart(_Inout_ QuicConn* c, int64 now)
{
    // Nothing is measured until the path is known to work at all. A probe on a path that has not
    // answered a challenge yet would be a large datagram sent somewhere that may not be the peer,
    // and its loss would say nothing about size.
    if (c->mtuState != QUIC_MTU_IDLE || !c->handshakeConfirmed || !c->pathValidated)
        return;

    c->mtuLow  = QUIC_INITIAL_MTU;
    c->mtuHigh = mtuCeiling(c) + 1;
    mtuSearch(c, now);
}

// The probe of `size` arrived, so the path carries at least that much. Everything the connection
// sends grows to match, and the search narrows from below.
static void mtuProbeAcked(_Inout_ QuicConn* c, size_t size, int64 now)
{
    if (size > c->mtuLow) {
        c->mtuLow = size;
        c->mtu    = size;
        _quicRecovSetMaxDatagram(&c->recov, size);
    }
    c->mtuProbing = 0;
    mtuSearch(c, now);
}

// The probe did not arrive. One loss is not an answer -- a probe is an ordinary datagram as far as
// the path is concerned and can be dropped for ordinary reasons -- so the same size is tried again
// until it has been given enough chances.
static void mtuProbeLost(_Inout_ QuicConn* c, size_t size, int64 now)
{
    if (++c->mtuTries < QUIC_PMTU_PROBES)
        return;   // same candidate, another try; the size stays in c->mtuProbing

    if (size < c->mtuHigh)
        c->mtuHigh = size;
    c->mtuProbing = 0;
    mtuSearch(c, now);
}

static int spaceForLevel(TlsQuicLevel level)
{
    switch (level) {
    case TLSQL_Initial:   return QUIC_PNS_INITIAL;
    case TLSQL_Handshake: return QUIC_PNS_HANDSHAKE;
    default:              return QUIC_PNS_APP;
    }
}

static TlsQuicLevel levelForSpace(int sp)
{
    switch (sp) {
    case QUIC_PNS_INITIAL:   return TLSQL_Initial;
    case QUIC_PNS_HANDSHAKE: return TLSQL_Handshake;
    default:                 return TLSQL_App;
    }
}

// 0-RTT and 1-RTT are numbered together (RFC 9000 section 12.3), so both land in the application
// space; what separates them is which keys open them.
static int spaceForPktType(uint8 type)
{
    switch (type) {
    case QUIC_PKT_INITIAL:   return QUIC_PNS_INITIAL;
    case QUIC_PKT_HANDSHAKE: return QUIC_PNS_HANDSHAKE;
    case QUIC_PKT_0RTT:      return QUIC_PNS_APP;
    case QUIC_PKT_SHORT:     return QUIC_PNS_APP;
    default:                 return -1;
    }
}

static uint8 pktTypeForSpace(int sp)
{
    switch (sp) {
    case QUIC_PNS_INITIAL:   return QUIC_PKT_INITIAL;
    case QUIC_PNS_HANDSHAKE: return QUIC_PKT_HANDSHAKE;
    default:                 return QUIC_PKT_SHORT;
    }
}

static void spaceInit(_Out_ QuicPnSpace* s)
{
    memset(s, 0, sizeof(*s));
    s->largestAcked = QUIC_PN_NONE;
    s->ack.largest  = QUIC_PN_NONE;
    s->ack.sentPn   = QUIC_PN_NONE;
    _quicReasmInit(&s->cryptoIn, QUIC_MAX_CRYPTO_BUFFER);
    _quicSendBufInit(&s->cryptoOut);
}

static void spaceDestroy(_Inout_ QuicPnSpace* s)
{
    _quicKeysDestroy(&s->rx);
    _quicKeysDestroy(&s->tx);
    _quicReasmDestroy(&s->cryptoIn);
    _quicSendBufDestroy(&s->cryptoOut);
}

// Drops a packet number space for good: its keys, its pending acknowledgements, and the handshake
// bytes that travelled over it. RFC 9001 section 4.9 requires the keys to go; everything else goes
// with them because nothing can be sent or received in the space again.
static void spaceDiscard(_Inout_ QuicConn* c, int sp, int64 now)
{
    QuicPnSpace* s = &c->pns[sp];
    if (s->discarded)
        return;

    spaceDestroy(s);
    memset(s, 0, sizeof(*s));
    s->largestAcked = QUIC_PN_NONE;
    s->ack.largest  = QUIC_PN_NONE;
    s->ack.sentPn   = QUIC_PN_NONE;
    s->discarded    = true;

    // Whatever was still in flight here can never be acknowledged now, so it stops counting
    // against the congestion window rather than sitting there until a timer gives up on it.
    _quicRecovDiscardSpace(&c->recov, sp, now);
}

// Replaces one direction's keys, destroying whatever was there. A key set is only ever installed
// once per space per direction, but a failed derivation part way through would otherwise leave a
// half-built one behind.
static bool keysSet(_Inout_ QuicKeys* dst, _In_ const Tls13Suite* suite,
                    _In_reads_bytes_(suite->hashLen) const uint8* secret)
{
    QuicKeys k;
    if (!_quicKeysDerive(&k, suite, secret))
        return false;

    _quicKeysDestroy(dst);
    *dst = k;
    return true;
}

static void connNotifyClosed(_Inout_ QuicConn* c)
{
    _quicQlogClosed(c->qlog, c->closeError, c->closeApp, c->closeLocal, c->closeReason);

    if (c->handlers && c->handlers->closed)
        c->handlers->closed(c->hctx, c->closeError, c->closeApp, c->closeLocal, c->closeReason);
}

// Ends the connection without sending anything. Used where the RFC says an endpoint gives up
// rather than reporting: an idle timeout, a stateless reset, a version negotiation with nothing
// in common.
static void connSilentClose(_Inout_ QuicConn* c, uint64 error, _In_opt_ strref reason)
{
    if (c->state == QUIC_CS_CLOSED)
        return;

    // A connection that has already reported why it is going away just goes away: the layer above
    // is told once, and a stateless reset arriving during a close it already knows about does not
    // get to report a second, different reason.
    if (c->state == QUIC_CS_CLOSING || c->state == QUIC_CS_DRAINING) {
        c->state        = QUIC_CS_CLOSED;
        c->closePending = false;
        return;
    }

    c->state       = QUIC_CS_CLOSED;
    c->closeError  = error;
    c->closeApp    = false;
    c->closeLocal  = true;
    c->closePending = false;
    strDup(&c->closeReason, reason);
    connNotifyClosed(c);
}

_Use_decl_annotations_
void _quicConnClose(QuicConn* c, uint64 error, bool app, strref reason)
{
    if (c->state == QUIC_CS_CLOSING || c->state == QUIC_CS_DRAINING || c->state == QUIC_CS_CLOSED)
        return;

    c->state          = QUIC_CS_CLOSING;
    c->closeError     = error;
    c->closeApp       = app;
    c->closeLocal     = true;
    c->closeFrameType = 0;
    c->closePending   = true;
    strDup(&c->closeReason, reason);
    connNotifyClosed(c);
}

_Use_decl_annotations_
void _quicConnAbort(QuicConn* c, uint64 error, uint64 frameType)
{
    _quicConnClose(c, error, false, NULL);
    c->closeFrameType = frameType;
}

_Use_decl_annotations_
QuicConnState _quicConnGetState(const QuicConn* c)
{
    return (QuicConnState)c->state;
}

_Use_decl_annotations_
bool _quicConnHandshakeConfirmed(const QuicConn* c)
{
    return c->handshakeConfirmed;
}

_Use_decl_annotations_
bool _quicConnIsClosed(const QuicConn* c)
{
    return c->state == QUIC_CS_CLOSED;
}

_Use_decl_annotations_
const QuicTransportParams* _quicConnPeerParams(const QuicConn* c)
{
    return &c->peerTp;
}

_Use_decl_annotations_
TlsQuic* _quicConnTls(const QuicConn* c)
{
    return c->tls;
}

_Use_decl_annotations_
void _quicConnLocalCid(const QuicConn* c, QuicCid* out)
{
    *out = c->local[0].cid;
}

_Use_decl_annotations_
bool _quicConnValidated(const QuicConn* c)
{
    return c->addrValidated;
}

_Use_decl_annotations_
const QuicRecovery* _quicConnRecovery(const QuicConn* c)
{
    return &c->recov;
}

_Use_decl_annotations_
void _quicConnSetHandlers(QuicConn* c, const QuicConnHandlers* handlers, void* ctx)
{
    c->handlers = handlers;
    c->hctx     = ctx;
}

// ---------------------------------------------------------------------------------------------
// The TLS handshake
// ---------------------------------------------------------------------------------------------

static bool tlsSendCrypto(_In_opt_ void* ctx, TlsQuicLevel level,
                          _In_reads_bytes_(len) const uint8* data, size_t len)
{
    QuicConn* c    = ctx;
    QuicPnSpace* s = &c->pns[spaceForLevel(level)];

    if (s->discarded)
        return false;

    return _quicSendBufAdd(&s->cryptoOut, data, len);
}

static bool tlsSecrets(_In_opt_ void* ctx, TlsQuicLevel level, uint16 suite,
                       _In_reads_bytes_opt_(len) const uint8* readSecret,
                       _In_reads_bytes_opt_(len) const uint8* writeSecret, size_t len)
{
    QuicConn* c = ctx;
    unused_noeval(len);

    const Tls13Suite* su = _tls13Suite(suite);
    if (!su)
        return false;

    // 0-RTT runs one way, so exactly one of the two keys exists and neither belongs to a number
    // space of its own -- they protect application space packets that simply predate 1-RTT.
    if (level == TLSQL_EarlyData) {
        if (writeSecret && !keysSet(&c->earlyTx, su, writeSecret))
            return false;
        if (readSecret && !keysSet(&c->earlyRx, su, readSecret))
            return false;

        // Live from the moment the keys exist, which on a client is before the server has said
        // anything: that is what 0-RTT is.
        c->earlyState = QUIC_ES_LIVE;
        return true;
    }

    int sp         = spaceForLevel(level);
    QuicPnSpace* s = &c->pns[sp];

    if (readSecret && !keysSet(&s->rx, su, readSecret))
        return false;
    if (writeSecret && !keysSet(&s->tx, su, writeSecret))
        return false;

    if (writeSecret)
        _quicRecovSpaceReady(&c->recov, sp);

    // The generation after the current one is derived now rather than when a packet arrives
    // carrying it, so following the peer through a key update costs nothing on the receive path.
    if (sp == QUIC_PNS_APP && readSecret)
        _quicKeysNext(&c->rxNext, &s->rx);

    // 1-RTT write keys are where a client's early data stops: everything from here goes out
    // properly protected.
    if (!c->server && sp == QUIC_PNS_APP && writeSecret)
        _quicKeysDestroy(&c->earlyTx);

    return true;
}

// Whether this endpoint is still offering everything the session being resumed was told it could
// use. RFC 9001 section 4.5: a client sends its early data under the limits it remembers, so a
// server whose limits have shrunk since cannot take that data -- there is no way to tell the
// client the rules changed before it has already broken them.
static bool tpNotReduced(_In_ const QuicTransportParams* now, _In_ const QuicTransportParams* was)
{
    return now->initMaxData         >= was->initMaxData &&
           now->initMaxSdBidiLocal  >= was->initMaxSdBidiLocal &&
           now->initMaxSdBidiRemote >= was->initMaxSdBidiRemote &&
           now->initMaxSdUni        >= was->initMaxSdUni &&
           now->initMaxStreamsBidi  >= was->initMaxStreamsBidi &&
           now->initMaxStreamsUni   >= was->initMaxStreamsUni &&
           now->activeCidLimit      >= was->activeCidLimit;
}

// The transport parameters of the session a ticket came from. Always the server's, whichever end
// is reading them: a client sends early data under the limits the server gave it last time, and a
// server checks that those are still the limits it is giving.
static bool tlsEarlyParams(_In_opt_ void* ctx, _In_reads_bytes_(len) const uint8* data, size_t len)
{
    QuicConn* c = ctx;

    QuicTransportParams tp;
    if (!_quicTpDecode(&tp, data, len, true))
        return false;

    if (c->server)
        return tpNotReduced(&c->localTp, &tp);

    c->earlyTp     = tp;
    c->haveEarlyTp = true;

    return c->handlers && c->handlers->earlyOpen && c->handlers->earlyOpen(c->hctx, &tp);
}

// The server refused the early data, or a HelloRetryRequest threw away the hello it was keyed to.
// The keys go, and every 0-RTT packet still outstanding is declared lost so that what it carried
// is queued again -- which is the whole reason a send buffer holds bytes after they go out.
static void earlyRefused(_Inout_ QuicConn* c)
{
    _quicKeysDestroy(&c->earlyTx);
    _quicKeysDestroy(&c->earlyRx);

    if (c->earlyState != QUIC_ES_LIVE)
        return;

    // Every application space packet sent so far went out as 0-RTT: a rejection can only be heard
    // before the 1-RTT write keys exist, and nothing else has been able to use this space.
    c->earlyState = QUIC_ES_REFUSED;
    _quicRecovDiscardEarly(&c->recov, c->pns[QUIC_PNS_APP].next, c->now);

    if (c->handlers && c->handlers->earlyReject)
        c->handlers->earlyReject(c->hctx);
}

// The handshake has vouched for whatever went out or came in early: it is no longer something a
// third party could have replayed.
static void earlyConfirmed(_Inout_ QuicConn* c)
{
    if (c->earlyState == QUIC_ES_LIVE)
        c->earlyState = QUIC_ES_DONE;
}

static void tlsEarlyData(_In_opt_ void* ctx, bool accepted)
{
    QuicConn* c = ctx;

    if (!accepted) {
        earlyRefused(c);
        return;
    }

    // A server that takes the early data has undertaken to offer no less than it did when the
    // ticket was issued. Checking is the client's only protection against having sent under limits
    // that no longer apply, and RFC 9001 section 4.5 makes breaking it a protocol violation --
    // because by the time it can be noticed the data is already there.
    if (!c->server && c->haveEarlyTp && !tpNotReduced(&c->peerTp, &c->earlyTp)) {
        _quicConnAbort(c, QUIC_ERR_PROTOCOL_VIOLATION, 0);
        return;
    }

    // A server has had nothing to tell the layer above until now: the decision is what makes the
    // client's data readable, and a client was told when its keys were armed instead.
    if (c->server && c->handlers && c->handlers->earlyOpen)
        c->handlers->earlyOpen(c->hctx, NULL);
}

static bool tlsTransportParams(_In_opt_ void* ctx, _In_reads_bytes_(len) const uint8* data,
                               size_t len)
{
    QuicConn* c = ctx;

    if (!_quicTpDecode(&c->peerTp, data, len, !c->server)) {
        _quicConnAbort(c, QUIC_ERR_TRANSPORT_PARAMETER_ERROR, 0);
        return false;
    }

    // The connection IDs inside the parameters are the only proof that the ones used before the
    // handshake was authenticated were not rewritten in flight. Checking them is the entire point
    // of the three parameters carrying them.
    bool ok = cidEq(&c->peerTp.initScid, &c->remote[c->remoteActive].cid);

    if (ok && !c->server) {
        ok = c->peerTp.haveOrigDcid && cidEq(&c->peerTp.origDcid, &c->origDcid) &&
             c->peerTp.haveRetryScid == c->haveRetry &&
             (!c->haveRetry || cidEq(&c->peerTp.retryScid, &c->retryScid));
    }

    if (!ok) {
        logFmt(Warn, _SL("QUIC peer transport parameters name the wrong connection IDs"), stvNone);
        _quicConnAbort(c, QUIC_ERR_TRANSPORT_PARAMETER_ERROR, 0);
        return false;
    }

    if (c->peerTp.haveResetToken) {
        memcpy(c->remote[c->remoteActive].resetToken, c->peerTp.resetToken, QUIC_RESET_TOKEN_LEN);
        c->remote[c->remoteActive].haveToken = true;
    }

    if (c->peerTp.maxUdpPayload < c->mtu)
        c->mtu = (size_t)c->peerTp.maxUdpPayload;

    // How long the peer may sit on an acknowledgement before sending it, which is time the round
    // trip estimate has to discount and the probe timeout has to allow for.
    _quicRecovSetMaxAckDelay(&c->recov, timeMS((int64)c->peerTp.maxAckDelay));

    c->peerTpValid = true;
    _quicQlogParams(c->qlog, &c->peerTp, false);
    return true;
}

static void tlsComplete(_In_opt_ void* ctx)
{
    QuicConn* c = ctx;

    c->handshakeComplete = true;
    if (c->state == QUIC_CS_HANDSHAKE || c->state == QUIC_CS_NEW)
        c->state = QUIC_CS_CONNECTED;

    // Both ends stop being replayable here, for the same reason from opposite directions: a client
    // has 1-RTT keys and everything it sends from now on goes under them, and a server has checked
    // a Finished that only the real peer could have produced.
    earlyConfirmed(c);

    if (c->server) {
        // A server's handshake is confirmed the moment it completes: it has just checked the
        // client's Finished, so there is nothing left to wait for.
        c->handshakeConfirmed = true;
        c->addrValidated      = true;

        // Nothing may arrive under the 0-RTT keys after this that is worth reading: the client
        // has 1-RTT keys of its own and sends anything it still owes under those. Only a packet
        // delayed by more than a whole round trip would lose by it, and its contents come again.
        _quicKeysDestroy(&c->earlyRx);

        // Finishing a handshake is proof the peer receives what is sent to this address, which is
        // exactly what a path validation proves -- so the path the connection started on needs no
        // challenge of its own.
        c->pathValidated = true;
        _quicCtlQueue(&c->handshakeDone);
        _quicRecovConfirmHandshake(&c->recov, c->now);

        // The Handshake keys go after the next flush rather than right now, so the packet carrying
        // the client's Finished can still be acknowledged.
        c->discardHandshakeSoon = true;
    }

    // The peer's connection ID limit arrived with its transport parameters, so this is the first
    // moment either end knows how many spare IDs to hand out.
    cidReplenish(c);

    if (c->handlers && c->handlers->connected)
        c->handlers->connected(c->hctx);
}

static void tlsAlert(_In_opt_ void* ctx, uint8 alert)
{
    QuicConn* c = ctx;
    _quicConnClose(c, QUIC_ERR_CRYPTO_BASE + alert, false, NULL);
}

static const TlsQuicHandlers quicTlsHandlers = {
    .sendCrypto      = tlsSendCrypto,
    .secrets         = tlsSecrets,
    .transportParams = tlsTransportParams,
    .earlyParams     = tlsEarlyParams,
    .earlyData       = tlsEarlyData,
    .complete        = tlsComplete,
    .alert           = tlsAlert,
};

// Hands whatever handshake bytes have arrived in order to the TLS engine, which may respond with
// more of its own through tlsSendCrypto().
static bool cryptoDrain(_Inout_ QuicConn* c, int sp)
{
    for (;;) {
        QuicPnSpace* s = &c->pns[sp];
        if (s->discarded)
            return true;

        size_t n = _quicReasmReadable(&s->cryptoIn);
        if (n == 0)
            return true;

        const uint8* data = _quicReasmData(&s->cryptoIn);
        if (!tlsquicRecv(c->tls, levelForSpace(sp), data, n)) {
            // The alert handler has already closed the connection with the right error, unless the
            // failure was something with no alert attached.
            if (c->state != QUIC_CS_CLOSING)
                _quicConnAbort(c, QUIC_ERR_CRYPTO_BASE + 80, 0);
            return false;
        }

        // Re-read the space: the handshake may have moved on inside that call, taking the buffer
        // the bytes were read from with it.
        if (c->pns[sp].discarded)
            return true;

        _quicReasmConsume(&c->pns[sp].cryptoIn, n);
    }
}

// ---------------------------------------------------------------------------------------------
// Connection IDs
// ---------------------------------------------------------------------------------------------

static bool cidGenerate(_Inout_ QuicConn* c, _Out_ QuicLocalCid* out, uint64 seq)
{
    memset(out, 0, sizeof(*out));
    out->cid.len = c->localCidLen;

    if (c->localCidLen > 0 &&
        psa_generate_random(out->cid.id, c->localCidLen) != PSA_SUCCESS)
        return false;
    if (psa_generate_random(out->resetToken, QUIC_RESET_TOKEN_LEN) != PSA_SUCCESS)
        return false;

    out->seq  = seq;
    out->live = true;

    // A newly minted connection ID is no use to the peer until it has been told about it.
    out->announce.state = QUIC_CTL_PENDING;
    out->announce.pn    = QUIC_PN_NONE;
    return true;
}

// How many connection IDs this endpoint currently has out with the peer, and how many the peer
// said it will hold.
static uint32 cidLocalActive(_In_ const QuicConn* c)
{
    uint32 n = 0;
    for (uint32 i = 0; i < QUIC_MAX_CIDS; i++) {
        if (c->local[i].live && !c->local[i].retired)
            n++;
    }
    return n;
}

static uint32 cidLocalWanted(_In_ const QuicConn* c)
{
    // A zero-length connection ID says this endpoint does not route by one, and RFC 9000 section
    // 5.1.1 forbids it from issuing any others.
    if (c->localCidLen == 0 || !c->peerTpValid)
        return 1;

    uint64 want = c->peerTp.activeCidLimit;
    return want > QUIC_MAX_CIDS ? QUIC_MAX_CIDS : (uint32)want;
}

// Fills empty slots with fresh connection IDs so the peer always has as many as it agreed to hold.
static void cidReplenish(_Inout_ QuicConn* c)
{
    uint32 want = cidLocalWanted(c);

    for (uint32 i = 0; i < QUIC_MAX_CIDS && cidLocalActive(c) < want; i++) {
        if (c->local[i].live)
            continue;
        if (!cidGenerate(c, &c->local[i], c->localNextSeq))
            return;
        c->localNextSeq++;
    }
}

static void cidRetireLocal(_Inout_ QuicConn* c, uint64 seq)
{
    for (uint32 i = 0; i < QUIC_MAX_CIDS; i++) {
        if (!c->local[i].live || c->local[i].seq != seq)
            continue;

        if (c->handlers && c->handlers->cidRetired)
            c->handlers->cidRetired(c->hctx, &c->local[i].cid, seq);

        memset(&c->local[i], 0, sizeof(c->local[i]));
        break;
    }

    cidReplenish(c);
}

// Queues a RETIRE_CONNECTION_ID for one of the peer's connection IDs. Duplicates are dropped: the
// peer only needs to be told once, and the queue is the size of the table.
static void cidQueueRetire(_Inout_ QuicConn* c, uint64 seq)
{
    for (uint32 i = 0; i < c->nretireQueue; i++) {
        if (c->retireQueue[i].seq == seq)
            return;
    }

    if (c->nretireQueue < QUIC_MAX_CIDS) {
        uint32 i = c->nretireQueue++;
        memset(&c->retireQueue[i], 0, sizeof(c->retireQueue[i]));
        c->retireQueue[i].seq = seq;
        _quicCtlQueue(&c->retireQueue[i].ctl);
    }
}

// ---------------------------------------------------------------------------------------------
// Building packets
// ---------------------------------------------------------------------------------------------

static bool putFrame(_Inout_ QuicWr* wr, _In_ const QuicFrame* f)
{
    size_t need = _quicFrameSize(f);
    if (need == 0 || need > _quicWrLeft(wr))
        return false;

    return _quicFrameEncode(wr, f);
}

static uint64 pnMask(uint8 pnLen)
{
    return (UINT64_C(1) << (pnLen * 8)) - 1;
}

static void putAck(_Inout_ QuicConn* c, int sp, _Inout_ QuicWr* wr, uint64 pn, int64 now)
{
    QuicAckState* as = &c->pns[sp].ack;
    if (!as->pending || as->nranges == 0)
        return;

    // The exponent this endpoint advertised only applies to 1-RTT packets: the transport
    // parameters have not been read yet when the handshake spaces are in use, so those are fixed
    // at the default.
    uint64 exp = (sp == QUIC_PNS_APP) ? c->localTp.ackDelayExponent : QUIC_HS_ACK_DELAY_EXP;

    int64 elapsed = now - as->largestTime;
    if (elapsed < 0)
        elapsed = 0;
    uint64 delay = (uint64)elapsed >> exp;

    // Drop ranges from the small end until the frame fits. The peer treats the numbers that fall
    // off as unacknowledged, which costs a retransmission rather than correctness.
    uint8 rangeBuf[QUIC_MAX_ACK_RANGES * 20];
    for (uint32 n = as->nranges; n > 0; n--) {
        QuicFrame f;
        if (!_quicAckBuild(&f, delay, as->ranges, n, as->ecnSeen ? as->ecn : NULL, rangeBuf,
                           sizeof(rangeBuf)))
            continue;

        if (!putFrame(wr, &f))
            continue;

        as->pending     = false;
        as->elicitCount = 0;
        as->deadline    = 0;
        as->sentPn      = pn;
        return;
    }
}

static void putClose(_In_ QuicConn* c, int sp, _Inout_ QuicWr* wr)
{
    QuicFrame f;
    memset(&f, 0, sizeof(f));

    // An application error code has no representation before 1-RTT keys exist. RFC 9000 section
    // 10.2.3 says to report it as a transport APPLICATION_ERROR with no detail rather than leak
    // the code into a packet the peer cannot attribute to the application.
    bool asApp = c->closeApp && sp == QUIC_PNS_APP;

    f.type                = asApp ? QUIC_FRAME_CONNECTION_CLOSE_APP : QUIC_FRAME_CONNECTION_CLOSE;
    f.connClose.error     = asApp ? c->closeError
                                  : (c->closeApp ? QUIC_ERR_APPLICATION_ERROR : c->closeError);
    f.connClose.frameType = asApp ? 0 : c->closeFrameType;

    uint32 rlen = c->closeApp && !asApp ? 0 : strLen(c->closeReason);
    if (rlen > 0) {
        f.connClose.reason    = (const uint8*)strC(c->closeReason);
        f.connClose.reasonLen = rlen;
        if (putFrame(wr, &f))
            return;

        // The reason is explanatory; the error code is not. Drop the text rather than the frame.
        f.connClose.reason    = NULL;
        f.connClose.reasonLen = 0;
    }

    putFrame(wr, &f);
}

// The room a CRYPTO frame needs beyond the bytes it carries. The offset varint is measured
// against the end of the stream rather than the range's own offset, which never underestimates it
// and means the budget can be worked out before a range has been picked.
static size_t cryptoOverhead(_In_ const QuicSendBuf* sb)
{
    return 1 + _quicVarintSize(sb->end) + 2;
}

static bool putCryptoFrame(_Inout_ QuicWr* wr, uint64 off, _In_reads_(len) const uint8* data,
                           size_t len)
{
    QuicFrame f;
    memset(&f, 0, sizeof(f));
    f.type          = QUIC_FRAME_CRYPTO;
    f.crypto.offset = off;
    f.crypto.len    = len;
    f.crypto.data   = data;

    return putFrame(wr, &f);
}

static void putCrypto(_Inout_ QuicConn* c, int sp, _Inout_ QuicWr* wr, uint64 pn,
                      _Inout_ bool* eliciting)
{
    QuicSendBuf* sb = &c->pns[sp].cryptoOut;

    for (;;) {
        size_t overhead = cryptoOverhead(sb);
        size_t left     = _quicWrLeft(wr);
        if (left <= overhead)
            return;

        uint64 off;
        const uint8* data;
        size_t n;
        if (!_quicSendBufNext(sb, left - overhead, &off, &data, &n))
            return;

        if (!putCryptoFrame(wr, off, data, n))
            return;

        _quicSendBufSent(sb, off, n, pn);
        *eliciting = true;
    }
}

// Repeats handshake bytes the peer has not answered for, to draw out an acknowledgement. The
// bytes keep their place in the send buffer: this is a duplicate, not a replacement for what is
// already in flight, so the original packet still resolves normally.
static bool putCryptoProbe(_Inout_ QuicConn* c, int sp, _Inout_ QuicWr* wr,
                           _Inout_ bool* eliciting)
{
    QuicSendBuf* sb = &c->pns[sp].cryptoOut;

    size_t overhead = cryptoOverhead(sb);
    size_t left     = _quicWrLeft(wr);
    if (left <= overhead)
        return false;

    uint64 off;
    const uint8* data;
    size_t n;
    if (!_quicSendBufProbe(sb, left - overhead, &off, &data, &n))
        return false;

    if (!putCryptoFrame(wr, off, data, n))
        return false;

    *eliciting = true;
    return true;
}

static bool putPing(_Inout_ QuicWr* wr, _Inout_ bool* eliciting)
{
    QuicFrame f;
    memset(&f, 0, sizeof(f));
    f.type = QUIC_FRAME_PING;

    if (!putFrame(wr, &f))
        return false;

    *eliciting = true;
    return true;
}

// Frames that only exist once 1-RTT keys do: connection ID management, path validation, and the
// handshake-confirmed signal.
// `early` leaves out the two frames RFC 9000 section 12.4 does not allow in a 0-RTT packet. Both
// are answers to something -- the end of a handshake, a challenge -- and a 0-RTT packet is written
// before there is anything to answer.
static void putAppFrames(_Inout_ QuicConn* c, _Inout_ QuicWr* wr, uint64 pn, bool early,
                         _Inout_ bool* eliciting)
{
    QuicFrame f;

    if (!early && c->handshakeDone.state == QUIC_CTL_PENDING) {
        memset(&f, 0, sizeof(f));
        f.type = QUIC_FRAME_HANDSHAKE_DONE;
        if (putFrame(wr, &f)) {
            _quicCtlSent(&c->handshakeDone, pn);
            *eliciting = true;
        }
    }

    if (!early && c->pathResponse.state == QUIC_CTL_PENDING && !c->respondElsewhere) {
        memset(&f, 0, sizeof(f));
        f.type = QUIC_FRAME_PATH_RESPONSE;
        memcpy(f.path.data, c->pathResponseData, QUIC_PATH_DATA_LEN);
        if (putFrame(wr, &f)) {
            _quicCtlSent(&c->pathResponse, pn);
            *eliciting = true;
        }
    }

    if (c->pathChallenge.state == QUIC_CTL_PENDING) {
        memset(&f, 0, sizeof(f));
        f.type = QUIC_FRAME_PATH_CHALLENGE;
        memcpy(f.path.data, c->pathChallengeData, QUIC_PATH_DATA_LEN);
        if (putFrame(wr, &f)) {
            _quicCtlSent(&c->pathChallenge, pn);
            *eliciting = true;
        }
    }

    for (uint32 i = 0; i < c->nretireQueue; i++) {
        if (c->retireQueue[i].ctl.state != QUIC_CTL_PENDING)
            continue;

        memset(&f, 0, sizeof(f));
        f.type             = QUIC_FRAME_RETIRE_CONNECTION_ID;
        f.retireConnId.seq = c->retireQueue[i].seq;
        if (!putFrame(wr, &f))
            break;

        _quicCtlSent(&c->retireQueue[i].ctl, pn);
        *eliciting = true;
    }

    for (uint32 i = 0; i < QUIC_MAX_CIDS; i++) {
        if (!c->local[i].live || c->local[i].retired ||
            c->local[i].announce.state != QUIC_CTL_PENDING)
            continue;

        memset(&f, 0, sizeof(f));
        f.type                  = QUIC_FRAME_NEW_CONNECTION_ID;
        f.newConnId.seq         = c->local[i].seq;
        f.newConnId.retirePrior = 0;
        f.newConnId.cid         = c->local[i].cid;
        memcpy(f.newConnId.token, c->local[i].resetToken, QUIC_RESET_TOKEN_LEN);

        if (!putFrame(wr, &f))
            break;

        _quicCtlSent(&c->local[i].announce, pn);
        *eliciting = true;

        if (!c->local[i].notified && c->handlers && c->handlers->cidIssued) {
            c->local[i].notified = true;
            c->handlers->cidIssued(c->hctx, &c->local[i].cid, c->local[i].seq,
                                   c->local[i].resetToken);
        }
    }
}

// Fills one packet's payload. Returns how many bytes were written; zero means this number space
// has nothing to say and no packet should be built for it.
//
// `blocked` says the congestion window or the pacer will not take anything more that counts
// against them, which leaves only acknowledgements -- they cost the peer nothing to receive and
// RFC 9002 exempts them. `probe` says a packet is owed after a timeout, which overrides that: a
// probe is the only thing that can break a deadlock where everything in flight was lost.
static size_t buildPayload(_Inout_ QuicConn* c, int sp, bool early,
                           _Out_writes_(budget) uint8* out, size_t budget, uint64 pn, bool probe,
                           bool blocked, _Out_ bool* eliciting, int64 now)
{
    QuicWr wr;
    _quicWrInit(&wr, out, budget);
    *eliciting = false;

    // A closing connection says one thing and nothing else. Never in a 0-RTT packet: the Initial
    // and Handshake spaces are alive while one exists, and the close belongs in those.
    if (c->state == QUIC_CS_CLOSING) {
        if (!c->closePending || early)
            return 0;
        putClose(c, sp, &wr);
        return _quicWrLen(&wr);
    }

    if (!early)
        putAck(c, sp, &wr, pn, now);

    if (blocked && !probe)
        return _quicWrLen(&wr);

    if (!early)
        putCrypto(c, sp, &wr, pn, eliciting);

    if (sp == QUIC_PNS_APP && (early || c->handshakeComplete)) {
        putAppFrames(c, &wr, pn, early, eliciting);

        size_t used = _quicWrLen(&wr);
        if (c->handlers && c->handlers->fill && used < budget) {
            bool e   = false;
            size_t n = c->handlers->fill(c->hctx, out + used, budget - used, pn, &e);
            devAssert(n <= budget - used);

            // The handler wrote straight into the payload, so the writer is advanced past what it
            // produced rather than being handed the bytes a second time.
            if (n > 0) {
                wr.p += n;
                if (e)
                    *eliciting = true;
            }
        }
    }

    // A probe has to be something the peer must acknowledge, so if nothing above produced such a
    // frame one is added: handshake bytes the peer has not answered for, or failing that a PING,
    // which exists for exactly this.
    if (probe && !*eliciting) {
        if (early || !putCryptoProbe(c, sp, &wr, eliciting))
            putPing(&wr, eliciting);
    }

    return _quicWrLen(&wr);
}

static void buildHdr(_In_ QuicConn* c, int sp, bool early, uint8 pnLen, size_t len,
                     _Out_ QuicPktHdr* h)
{
    memset(h, 0, sizeof(*h));

    h->type    = early ? QUIC_PKT_0RTT : pktTypeForSpace(sp);
    h->version = c->version;
    h->keyPhase = c->keyPhase;
    h->dcid    = c->remote[c->remoteActive].cid;
    h->pnLen   = pnLen;
    h->len     = len;

    if (h->type != QUIC_PKT_SHORT) {
        h->scid = c->local[0].cid;

        // Pinning the Length width keeps the header size the same before and after the payload is
        // built, which is what makes budgeting the payload against it possible. Two bytes covers
        // every length that fits in a datagram.
        h->lenSize = 2;
    }

    if (h->type == QUIC_PKT_INITIAL && c->token) {
        h->token    = c->token->data;
        h->tokenLen = c->token->len;
    }
}

// Whether anything is waiting that only the congestion window or the pacer is holding back.
// Acknowledgements do not count: they go out regardless of either.
static bool connWantsToSend(_In_ const QuicConn* c)
{
    for (int sp = 0; sp < QUIC_PNS_COUNT; sp++) {
        if (c->pns[sp].discarded)
            continue;
        if (_quicRecovProbes(&c->recov, sp) > 0)
            return true;
        if (_quicSendBufPending(&c->pns[sp].cryptoOut))
            return true;
    }

    if (c->handshakeDone.state == QUIC_CTL_PENDING ||
        (c->pathResponse.state == QUIC_CTL_PENDING && !c->respondElsewhere) ||
        c->pathChallenge.state == QUIC_CTL_PENDING)
        return true;

    for (uint32 i = 0; i < c->nretireQueue; i++) {
        if (c->retireQueue[i].ctl.state == QUIC_CTL_PENDING)
            return true;
    }

    for (uint32 i = 0; i < QUIC_MAX_CIDS; i++) {
        if (c->local[i].live && !c->local[i].retired &&
            c->local[i].announce.state == QUIC_CTL_PENDING)
            return true;
    }

    return c->handlers && c->handlers->wantsToSend && c->handlers->wantsToSend(c->hctx);
}

// Builds and sends one datagram, coalescing a packet from each number space that has something to
// send. `sent` says whether anything went out.
static bool flushOne(_Inout_ QuicConn* c, int64 now, _Out_ bool* sent)
{
    *sent = false;

    if (!c->handlers || !c->handlers->send)
        return true;

    size_t cap = c->mtu;
    if (cap > QUIC_MAX_DATAGRAM)
        cap = QUIC_MAX_DATAGRAM;

    // RFC 9000 section 8: until the peer's address is validated, a server may not send more than
    // three times what it has received, so that it cannot be used to amplify an attack.
    if (c->server && !c->addrValidated) {
        uint64 limit = c->bytesRecv * 3;
        if (limit <= c->bytesSent)
            return true;
        if (limit - c->bytesSent < cap)
            cap = (size_t)(limit - c->bytesSent);
    }

    // RFC 9000 section 9.3.1: the same limit applies again on a path the peer has moved to and not
    // yet answered a challenge on, because an address the peer claims to be at is exactly as
    // unproven the second time as it was the first. It does not apply to a path this endpoint
    // chose for itself, where the peer's address never changed and was validated long ago.
    if (c->pathAmpLimited && !c->pathValidated) {
        uint64 limit = c->pathRecv * 3;
        if (limit <= c->pathSent)
            return true;
        if (limit - c->pathSent < cap)
            cap = (size_t)(limit - c->pathSent);
    }

    // RFC 9002 section 7: nothing that counts against the congestion window goes out while it is
    // full, and the pacer spreads what does go out across the round trip rather than letting a
    // whole window leave at once.
    bool blocked = !_quicRecovCanSend(&c->recov) || !_quicRecovPacerReady(&c->recov);

    // The window is a byte count, not a packet count, so the last datagram before it fills has to
    // be trimmed to what is left rather than allowed to run past it. What comes out is a short
    // datagram, which is exactly what a window with a little room left should produce.
    //
    // Room too small to hold any packet at all is a different thing, and has to be treated as a
    // full window rather than a tiny one. Trimming the datagram down to it leaves no space for an
    // acknowledgement, which does not count against the window, nor for a probe, which RFC 9002
    // section 7.5 says the window may not hold back -- so the connection would fall silent, and
    // the only things that could reopen the window are the answers to the packets it can no longer
    // send. A window a handful of bytes short of full is reachable whenever loss has pulled it
    // down near its floor, which is exactly when losing the ability to acknowledge is fatal.
    if (!blocked) {
        size_t room = _quicRecovWindowRoom(&c->recov);
        if (room < QUIC_MIN_SEND_ROOM)
            blocked = true;
        else if (room < cap)
            cap = room;
    }

    struct {
        size_t off, len;
        uint8 pnLen;
        bool present, elicit, inFlight, probe, early;
    } plan[QUIC_PNS_COUNT];
    memset(plan, 0, sizeof(plan));

    size_t used        = 0;   // what the finished datagram will measure
    size_t scratchUsed = 0;

    for (int sp = 0; sp < QUIC_PNS_COUNT; sp++) {
        QuicPnSpace* s = &c->pns[sp];
        if (s->discarded)
            continue;

        // A resuming client has application data to send before it has 1-RTT keys, and the 0-RTT
        // keys are what it sends under. Same number space and the same packet numbers -- what
        // differs is the header type and which keys seal it.
        bool early = sp == QUIC_PNS_APP && !s->tx.valid && c->earlyTx.valid;
        if (!early && !s->tx.valid)
            continue;

        uint8 pnLen = _quicPnSize(s->next, s->largestAcked);

        QuicPktHdr h;
        buildHdr(c, sp, early, pnLen, 0, &h);

        size_t hdrsz = _quicHdrSize(&h);
        if (hdrsz == 0 || used + hdrsz + QUIC_TAG_LEN + QUIC_MIN_PN_PAYLOAD > cap)
            continue;

        size_t budget = cap - used - hdrsz - QUIC_TAG_LEN;
        if (budget > sizeof(c->scratch) - scratchUsed)
            budget = sizeof(c->scratch) - scratchUsed;

        bool probe  = _quicRecovProbes(&c->recov, sp) > 0;
        bool elicit = false;
        size_t n    = buildPayload(c, sp, early, c->scratch + scratchUsed, budget, s->next, probe,
                                   blocked, &elicit, now);
        if (n == 0)
            continue;

        // Header protection samples sixteen bytes starting four bytes past the packet number
        // field, so a packet has to carry at least that much after it. A small packet is padded
        // out rather than made unprotectable.
        if (pnLen + n < QUIC_MIN_PN_PAYLOAD) {
            size_t pad = QUIC_MIN_PN_PAYLOAD - pnLen - n;
            memset(c->scratch + scratchUsed + n, 0, pad);
            n += pad;
        }

        plan[sp].off      = scratchUsed;
        plan[sp].len       = n;
        plan[sp].pnLen     = pnLen;
        plan[sp].present   = true;
        plan[sp].elicit    = elicit;
        plan[sp].inFlight  = elicit;
        plan[sp].probe     = probe && elicit;
        plan[sp].early     = early;

        scratchUsed += n;
        used += hdrsz + n + QUIC_TAG_LEN;
    }

    if (used == 0)
        return true;

    // RFC 9000 section 14.1: a datagram carrying an Initial packet is padded out to the smallest
    // datagram size every path must carry, so that neither end has to be the one that discovers
    // the path is too small. A client pads every such datagram; a server pads the ones whose
    // Initial has to be acknowledged, and may be unable to pad at all while the anti-amplification
    // limit holds -- which is what cap accounts for.
    bool padInitial = plan[QUIC_PNS_INITIAL].present &&
                      (!c->server || plan[QUIC_PNS_INITIAL].elicit);

    if (padInitial && used < QUIC_INITIAL_MTU && cap >= QUIC_INITIAL_MTU) {
        size_t pad = QUIC_INITIAL_MTU - used;

        int last = QUIC_PNS_INITIAL;
        for (int sp = 0; sp < QUIC_PNS_COUNT; sp++) {
            if (plan[sp].present)
                last = sp;
        }

        // PADDING is a run of zero bytes, and the last packet's payload ends exactly where the
        // scratch buffer's used region does, so the padding simply extends it.
        if (pad <= sizeof(c->scratch) - scratchUsed) {
            memset(c->scratch + scratchUsed, 0, pad);
            plan[last].len += pad;
            scratchUsed += pad;
            used += pad;

            // RFC 9002 section 2 counts a padded packet as in flight even when nothing in it has
            // to be acknowledged, because the bytes are on the path either way.
            plan[last].inFlight = true;
        }
    }

    uint8 dgram[QUIC_MAX_DATAGRAM];
    size_t dlen   = 0;
    bool elicited = false;

    for (int sp = 0; sp < QUIC_PNS_COUNT; sp++) {
        if (!plan[sp].present)
            continue;

        QuicPnSpace* s = &c->pns[sp];

        QuicPktHdr h;
        buildHdr(c, sp, plan[sp].early, plan[sp].pnLen,
                 plan[sp].len + plan[sp].pnLen + QUIC_TAG_LEN, &h);
        h.pn    = s->next;
        h.pnEnc = s->next & pnMask(plan[sp].pnLen);

        size_t hdrsz = _quicHdrSize(&h);

        QuicWr wr;
        _quicWrInit(&wr, dgram + dlen, sizeof(dgram) - dlen);
        if (!_quicHdrEncode(&wr, &h))
            return false;

        size_t outLen;
        if (!_quicPktSeal(plan[sp].early ? &c->earlyTx : &s->tx, dgram + dlen,
                          sizeof(dgram) - dlen, hdrsz - plan[sp].pnLen, plan[sp].pnLen, s->next,
                          c->scratch + plan[sp].off, plan[sp].len, &outLen))
            return false;

        _quicQlogPacket(c->qlog, true, &h, outLen, c->scratch + plan[sp].off, plan[sp].len);

        _quicRecovOnSent(&c->recov, sp, s->next, outLen, plan[sp].elicit, plan[sp].inFlight, now);

        if (plan[sp].probe)
            _quicRecovProbeSent(&c->recov, sp);

        dlen += outLen;
        s->next++;

        if (plan[sp].elicit)
            elicited = true;
        if (sp == QUIC_PNS_HANDSHAKE)
            c->sentHandshakePkt = true;
    }

    c->bytesSent += dlen;
    c->pathSent += dlen;
    *sent = true;

    NetPktInfo info;
    pathInfo(c, &info);

    if (info.haveEcn) {
        // Every packet in the datagram carries the mark, so every one of them is what the peer's
        // counts will be checked against.
        for (int sp = 0; sp < QUIC_PNS_COUNT; sp++) {
            if (plan[sp].present)
                _quicRecovMark(&c->recov, sp, c->pns[sp].next - 1, QUIC_SENT_ECT0);
        }
        if (c->ecnState == QUIC_ECN_TESTING && c->ecnTestPkts > 0)
            c->ecnTestPkts--;
    }

    if (c->state == QUIC_CS_CLOSING && c->closePending) {
        c->closePending  = false;
        c->closeDeadline = now + 3 * QUIC_INITIAL_RTT;
    }

    if (elicited)
        c->idleDeadline = now + idleTimeoutOf(c);

    return c->handlers->send(c->hctx, &c->peer, &info, dgram, dlen);
}

// Builds and sends one datagram containing a single frame, padded out to `padTo` bytes, addressed
// wherever the caller says. Two things need exactly this and nothing else: a path MTU probe, which
// is a datagram of a chosen size whose whole purpose is to be lost if the path is too small, and a
// PATH_RESPONSE going to an address the connection is not otherwise using. Neither may be
// coalesced with anything else -- the probe because losing real data to a size test would be
// perverse, the response because it is not going where the rest of the traffic is.
//
// Always 1-RTT, so it is only usable once the handshake has produced application keys.
_Success_(return) static bool sendSolo(_Inout_ QuicConn* c, _In_ const NetAddr* dest,
                                       _In_ const QuicFrame* f, size_t padTo, _Out_ uint64* pnOut,
                                       int64 now)
{
    *pnOut = QUIC_PN_NONE;

    QuicPnSpace* s = &c->pns[QUIC_PNS_APP];
    if (!c->handlers || !c->handlers->send || s->discarded || !s->tx.valid)
        return false;
    if (padTo > QUIC_MAX_DATAGRAM)
        return false;

    uint8 pnLen = _quicPnSize(s->next, s->largestAcked);

    QuicPktHdr h;
    buildHdr(c, QUIC_PNS_APP, false, pnLen, 0, &h);

    size_t hdrsz = _quicHdrSize(&h);
    if (hdrsz == 0 || hdrsz + QUIC_TAG_LEN >= padTo)
        return false;

    size_t plen = padTo - hdrsz - QUIC_TAG_LEN;
    if (plen > sizeof(c->scratch))
        return false;

    QuicWr wr;
    _quicWrInit(&wr, c->scratch, plen);
    if (!_quicFrameEncode(&wr, f) || wr.bad)
        return false;

    // PADDING is a run of zero bytes, so filling the rest of the payload is the whole of it.
    size_t used = _quicWrLen(&wr);
    memset(c->scratch + used, 0, plen - used);

    h.pn    = s->next;
    h.pnEnc = s->next & pnMask(pnLen);
    h.len   = plen + pnLen + QUIC_TAG_LEN;

    uint8 dgram[QUIC_MAX_DATAGRAM];
    _quicWrInit(&wr, dgram, sizeof(dgram));
    if (!_quicHdrEncode(&wr, &h))
        return false;

    size_t outLen;
    if (!_quicPktSeal(&s->tx, dgram, sizeof(dgram), hdrsz - pnLen, pnLen, s->next, c->scratch,
                      plen, &outLen))
        return false;

    *pnOut = s->next;
    _quicRecovOnSent(&c->recov, QUIC_PNS_APP, s->next, outLen, true, true, now);

    c->bytesSent += outLen;
    c->pathSent += outLen;
    c->idleDeadline = now + idleTimeoutOf(c);

    NetPktInfo info;
    pathInfo(c, &info);

    // Marked on the wire like every other datagram, so it counts as marked when the peer's ECN
    // counts are checked -- otherwise the peer reports more than this endpoint thinks it sent.
    if (info.haveEcn)
        _quicRecovMark(&c->recov, QUIC_PNS_APP, s->next, QUIC_SENT_ECT0);

    s->next++;

    // The datagram is going somewhere other than the path in use, so the local address that path
    // resolved to has nothing to do with it.
    if (!addrEq(dest, &c->peer))
        info.haveLocal = false;

    return c->handlers->send(c->hctx, dest, &info, dgram, outLen);
}

// Sends the path MTU probe the search is waiting on, if there is one and the connection is in a
// position to send it.
static void mtuProbeSend(_Inout_ QuicConn* c, int64 now)
{
    if (c->mtuState != QUIC_MTU_SEARCHING || c->mtuProbing == 0)
        return;

    // A probe in flight has to be resolved before another goes out; the search is one question at
    // a time.
    if (c->mtuProbePn != QUIC_PN_NONE)
        return;

    // A probe waits for room like anything else -- it is measuring the path, not jumping the queue
    // -- and for room it actually fits in, since it is bigger than every other datagram and its
    // size is known before it is built.
    if (!_quicRecovCanSendSize(&c->recov, c->mtuProbing) || !_quicRecovPacerReady(&c->recov))
        return;

    QuicFrame f;
    memset(&f, 0, sizeof(f));
    f.type = QUIC_FRAME_PING;

    uint64 pn;
    if (!sendSolo(c, &c->peer, &f, c->mtuProbing, &pn, now)) {
        // The size could not be built at all, which means it is not a size this endpoint can send.
        // Treat it exactly as a probe that did not arrive, so the search narrows instead of
        // stalling on it.
        c->mtuTries = QUIC_PMTU_PROBES;
        mtuProbeLost(c, c->mtuProbing, now);
        return;
    }

    c->mtuProbePn = pn;
    _quicRecovMark(&c->recov, QUIC_PMTU_SPACE, pn, QUIC_SENT_PMTU_PROBE);
}

// Answers a PATH_CHALLENGE that arrived from somewhere other than the address in use. RFC 9000
// section 8.2.2 puts the response on the path the challenge came from, and section 8.2.1 pads the
// datagram carrying it, so that a path is only ever validated for a size that is actually usable.
static void pathRespondElsewhere(_Inout_ QuicConn* c, int64 now)
{
    if (!c->respondElsewhere || c->pathResponse.state != QUIC_CTL_PENDING)
        return;

    QuicFrame f;
    memset(&f, 0, sizeof(f));
    f.type = QUIC_FRAME_PATH_RESPONSE;
    memcpy(f.path.data, c->pathResponseData, QUIC_PATH_DATA_LEN);

    uint64 pn;
    if (!sendSolo(c, &c->respondTo, &f, QUIC_INITIAL_MTU, &pn, now))
        return;

    _quicCtlSent(&c->pathResponse, pn);
}

// ---------------------------------------------------------------------------------------------
// Frames arriving
// ---------------------------------------------------------------------------------------------

// RFC 9000 section 12.4: the handshake number spaces carry only the frames the handshake itself
// needs. Anything else in one is a protocol violation rather than something to ignore.
// RFC 9000 section 12.4. `early` narrows the application space to what a 0-RTT packet may carry:
// nothing that answers something the peer has not said yet, and nothing that belongs to a
// handshake this packet was written before the end of.
static bool frameAllowed(uint64 type, int sp, bool early)
{
    if (sp == QUIC_PNS_APP) {
        if (!early)
            return true;

        switch (type) {
        case QUIC_FRAME_ACK:
        case QUIC_FRAME_ACK_ECN:
        case QUIC_FRAME_CRYPTO:
        case QUIC_FRAME_NEW_TOKEN:
        case QUIC_FRAME_PATH_RESPONSE:
        case QUIC_FRAME_HANDSHAKE_DONE:
            return false;
        default:
            return true;
        }
    }

    switch (type) {
    case QUIC_FRAME_PADDING:
    case QUIC_FRAME_PING:
    case QUIC_FRAME_ACK:
    case QUIC_FRAME_ACK_ECN:
    case QUIC_FRAME_CRYPTO:
    case QUIC_FRAME_CONNECTION_CLOSE:
        return true;
    default:
        return false;
    }
}

// Whether a frame obliges the peer to acknowledge the packet carrying it. Everything does except
// the three that would otherwise make two endpoints acknowledge each other forever.
static bool frameElicits(uint64 type)
{
    switch (type) {
    case QUIC_FRAME_PADDING:
    case QUIC_FRAME_ACK:
    case QUIC_FRAME_ACK_ECN:
    case QUIC_FRAME_CONNECTION_CLOSE:
    case QUIC_FRAME_CONNECTION_CLOSE_APP:
        return false;
    default:
        return true;
    }
}

// ECN validation, RFC 9000 section 13.4.2.
//
// The marks this endpoint puts on its packets are only worth reading if the path carries them all
// the way and the peer counts them honestly. The test is arithmetic on the counts the peer echoes:
// they may only go up, and they have to go up by at least as many as the marked packets this
// acknowledgement resolved. Anything else means something in between stripped or rewrote the
// field, and marking stops for the rest of the connection on this path.
static void ecnOnAck(_Inout_ QuicConn* c, int sp, _In_ const QuicFrame* f, int64 now)
{
    QuicPnSpace* s = &c->pns[sp];

    if (c->ecnState == QUIC_ECN_OFF)
        return;

    bool hasCounts = f->type == QUIC_FRAME_ACK_ECN;

    if (!hasCounts) {
        // An acknowledgement carrying no counts at all, for packets that were marked, is the plain
        // case of a peer or a path that is not doing this.
        if (s->ecnAckedMarked > 0)
            c->ecnState = QUIC_ECN_OFF;
        return;
    }

    if (f->ack.ect0 < s->ecnEct0 || f->ack.ect1 < s->ecnEct1 || f->ack.ecnce < s->ecnCe) {
        c->ecnState = QUIC_ECN_OFF;
        return;
    }

    uint64 dEct0 = f->ack.ect0 - s->ecnEct0;
    uint64 dCe   = f->ack.ecnce - s->ecnCe;

    // Everything this endpoint marks is ECT(0), so a packet it marked arrives counted either as
    // ECT(0) or -- if a router marked it on the way -- as CE. Fewer than were acknowledged means
    // some of them arrived as neither.
    if (dEct0 + dCe < s->ecnAckedMarked) {
        c->ecnState = QUIC_ECN_OFF;
        return;
    }

    s->ecnEct0 = f->ack.ect0;
    s->ecnEct1 = f->ack.ect1;
    s->ecnCe   = f->ack.ecnce;

    if (dEct0 + dCe > 0)
        c->ecnState = QUIC_ECN_ON;

    if (dCe > 0)
        _quicRecovOnEcnCe(&c->recov, sp, f->ack.largest, now);
}

static bool handleAck(_Inout_ QuicConn* c, int sp, _In_ const QuicFrame* f, int64 now)
{
    QuicPnSpace* s = &c->pns[sp];

    QuicAckIter it;
    QuicAckRange r;
    if (!_quicAckIterInit(&it, f)) {
        _quicConnAbort(c, QUIC_ERR_FRAME_ENCODING_ERROR, f->type);
        return false;
    }

    while (_quicAckIterNext(&it, &r)) {
        // An acknowledgement of a packet number this endpoint has not used says the peer is
        // guessing, and accepting it would corrupt every packet number decode after it.
        if (r.largest >= s->next) {
            _quicConnAbort(c, QUIC_ERR_PROTOCOL_VIOLATION, f->type);
            return false;
        }
    }

    if (it.bad) {
        _quicConnAbort(c, QUIC_ERR_FRAME_ENCODING_ERROR, f->type);
        return false;
    }

    if (s->largestAcked == QUIC_PN_NONE || f->ack.largest > s->largestAcked)
        s->largestAcked = f->ack.largest;

    // The delay the peer reports is in units of two to the power of the exponent this endpoint
    // advertised. The handshake spaces use the default, since the parameter carrying it has not
    // arrived while they are in use.
    uint64 exp = (sp == QUIC_PNS_APP) ? c->localTp.ackDelayExponent : QUIC_HS_ACK_DELAY_EXP;
    if (exp > 20)
        exp = 20;

    // A delay no peer could have taken would only distort the round trip estimate, and the shift
    // below has to stay inside the value it is applied to.
    uint64 delay = f->ack.delay;
    if (delay > (UINT64_C(1) << 32))
        delay = UINT64_C(1) << 32;

    s->ecnAckedMarked = 0;

    if (!_quicRecovOnAck(&c->recov, sp, f, (int64)(delay << exp), now)) {
        _quicConnAbort(c, QUIC_ERR_FRAME_ENCODING_ERROR, f->type);
        return false;
    }

    ecnOnAck(c, sp, f, now);
    return true;
}

// ---------------------------------------------------------------------------------------------
// What loss recovery reports back
// ---------------------------------------------------------------------------------------------

// A packet number is all either of these carries. Everything that would have to be sent again
// recorded the packet it went into, so finding what a packet held is a matter of asking each of
// those whether it was this one.
static void connOnAcked(_In_opt_ void* ctx, int sp, uint64 pn)
{
    QuicConn* c    = ctx;
    QuicPnSpace* s = &c->pns[sp];

    _quicSendBufAcked(&s->cryptoOut, pn);

    if (s->ack.sentPn == pn)
        s->ack.sentPn = QUIC_PN_NONE;

    uint8 marks = _quicRecovMarksOf(&c->recov, sp, pn);
    if (marks & QUIC_SENT_ECT0)
        s->ecnAckedMarked++;

    // RFC 9001 section 6.1: no second key update until the peer has answered a packet sent under
    // the current one, which is what proves it followed the first.
    if (sp == QUIC_PNS_APP && pn >= c->txPhaseFirst)
        c->keyPhaseAcked = true;

    if (sp != QUIC_PNS_APP)
        return;

    if (pn == c->mtuProbePn) {
        c->mtuProbePn = QUIC_PN_NONE;
        mtuProbeAcked(c, c->mtuProbing, c->now);
    }

    if (c->handlers && c->handlers->pktAcked)
        c->handlers->pktAcked(c->hctx, pn);

    _quicCtlAcked(&c->handshakeDone, pn);
    _quicCtlAcked(&c->pathResponse, pn);
    _quicCtlAcked(&c->pathChallenge, pn);

    for (uint32 i = 0; i < QUIC_MAX_CIDS; i++)
        _quicCtlAcked(&c->local[i].announce, pn);

    // A retirement the peer has acknowledged is finished with, so it leaves the queue entirely
    // rather than sitting in it as something that no longer needs saying.
    for (uint32 i = 0; i < c->nretireQueue;) {
        if (_quicCtlAcked(&c->retireQueue[i].ctl, pn)) {
            memmove(&c->retireQueue[i], &c->retireQueue[i + 1],
                    (c->nretireQueue - i - 1) * sizeof(c->retireQueue[0]));
            c->nretireQueue--;
        } else {
            i++;
        }
    }
}

static void connOnLost(_In_opt_ void* ctx, int sp, uint64 pn)
{
    QuicConn* c    = ctx;
    QuicPnSpace* s = &c->pns[sp];

    _quicSendBufLost(&s->cryptoOut, pn);

    // An acknowledgement is never retransmitted, because a later one says everything an earlier
    // one did. Losing the packet that carried one does mean another is owed, though, or the peer
    // waits for something that is not coming.
    if (s->ack.sentPn == pn) {
        s->ack.sentPn = QUIC_PN_NONE;
        if (s->ack.nranges > 0) {
            s->ack.pending  = true;
            s->ack.deadline = 0;
        }
    }

    if (sp != QUIC_PNS_APP)
        return;

    if (pn == c->mtuProbePn) {
        // A probe is the one packet whose loss is an answer rather than a problem, so it is
        // resolved here and never handed on to anything that would retransmit it: the search will
        // decide for itself whether to try that size again.
        c->mtuProbePn = QUIC_PN_NONE;
        mtuProbeLost(c, c->mtuProbing, c->now);
        return;
    }

    if (c->handlers && c->handlers->pktLost)
        c->handlers->pktLost(c->hctx, pn);

    _quicCtlLost(&c->handshakeDone, pn);
    _quicCtlLost(&c->pathResponse, pn);
    _quicCtlLost(&c->pathChallenge, pn);

    for (uint32 i = 0; i < QUIC_MAX_CIDS; i++)
        _quicCtlLost(&c->local[i].announce, pn);

    for (uint32 i = 0; i < c->nretireQueue; i++)
        _quicCtlLost(&c->retireQueue[i].ctl, pn);
}

static void connOnAckedQlog(_In_opt_ void* ctx, int sp, uint64 pn)
{
    QuicConn* c = (QuicConn*)ctx;
    connOnAcked(ctx, sp, pn);
    _quicQlogMetrics(c->qlog, &c->recov);
}

static void connOnLostQlog(_In_opt_ void* ctx, int sp, uint64 pn)
{
    QuicConn* c = (QuicConn*)ctx;
    _quicQlogLost(c->qlog, sp, pn);
    connOnLost(ctx, sp, pn);
    _quicQlogMetrics(c->qlog, &c->recov);
}

static const QuicRecovHandlers quicRecovHandlers = {
    .acked = connOnAckedQlog,
    .lost  = connOnLostQlog,
};

static bool handleNewConnId(_Inout_ QuicConn* c, _In_ const QuicFrame* f)
{
    // An endpoint that chose zero-length connection IDs is saying it does not route by them, so it
    // has nothing to hand out. The length of the ID being offered, and a retire_prior_to above the
    // sequence number, are rejected by the frame decoder before this is reached.
    if (c->remote[c->remoteActive].cid.len == 0) {
        _quicConnAbort(c, QUIC_ERR_PROTOCOL_VIOLATION, f->type);
        return false;
    }

    if (f->newConnId.retirePrior > c->remoteRetirePrior) {
        c->remoteRetirePrior = f->newConnId.retirePrior;

        for (uint32 i = 0; i < QUIC_MAX_CIDS; i++) {
            if (!c->remote[i].live || c->remote[i].seq >= c->remoteRetirePrior)
                continue;

            cidQueueRetire(c, c->remote[i].seq);
            memset(&c->remote[i], 0, sizeof(c->remote[i]));
        }
    }

    // Already superseded: acknowledge it by retiring it rather than storing it.
    if (f->newConnId.seq < c->remoteRetirePrior) {
        cidQueueRetire(c, f->newConnId.seq);
    } else {
        int32 slot = -1;
        for (uint32 i = 0; i < QUIC_MAX_CIDS; i++) {
            if (c->remote[i].live && c->remote[i].seq == f->newConnId.seq) {
                // The same sequence number naming a different ID means one of the two frames was
                // forged or the peer is confused; either way the mapping is no longer trustworthy.
                if (!cidEq(&c->remote[i].cid, &f->newConnId.cid)) {
                    _quicConnAbort(c, QUIC_ERR_PROTOCOL_VIOLATION, f->type);
                    return false;
                }
                return true;
            }
            if (!c->remote[i].live && slot < 0)
                slot = (int32)i;
        }

        if (slot < 0) {
            _quicConnAbort(c, QUIC_ERR_CONNECTION_ID_LIMIT_ERROR, f->type);
            return false;
        }

        memset(&c->remote[slot], 0, sizeof(c->remote[slot]));
        c->remote[slot].cid  = f->newConnId.cid;
        c->remote[slot].seq  = f->newConnId.seq;
        c->remote[slot].live = true;
        memcpy(c->remote[slot].resetToken, f->newConnId.token, QUIC_RESET_TOKEN_LEN);
        c->remote[slot].haveToken = true;
    }

    // The ID packets were being addressed to may have just been retired.
    if (!c->remote[c->remoteActive].live) {
        for (uint32 i = 0; i < QUIC_MAX_CIDS; i++) {
            if (c->remote[i].live) {
                c->remoteActive = i;
                break;
            }
        }
    }

    return true;
}

static bool handleRetireConnId(_Inout_ QuicConn* c, _In_ const QuicFrame* f,
                               _In_ const QuicPktHdr* h)
{
    if (f->retireConnId.seq >= c->localNextSeq) {
        _quicConnAbort(c, QUIC_ERR_PROTOCOL_VIOLATION, f->type);
        return false;
    }

    // RFC 9000 section 19.16: a peer cannot retire the connection ID it addressed the packet to,
    // because that is the one it just proved it is using.
    for (uint32 i = 0; i < QUIC_MAX_CIDS; i++) {
        if (c->local[i].live && c->local[i].seq == f->retireConnId.seq &&
            cidEq(&c->local[i].cid, &h->dcid)) {
            _quicConnAbort(c, QUIC_ERR_PROTOCOL_VIOLATION, f->type);
            return false;
        }
    }

    cidRetireLocal(c, f->retireConnId.seq);
    return true;
}

// A frame the stream layer owns. Without one attached, this endpoint advertised no streams and no
// data, so anything naming a stream is over a limit of zero.
static bool handleStreamFrame(_Inout_ QuicConn* c, _In_ const QuicFrame* f)
{
    if (c->handlers && c->handlers->frame) {
        if (c->handlers->frame(c->hctx, f))
            return true;

        // The handler is expected to have said why through _quicConnAbort(). If it did not, the
        // connection still has to stop rather than carry on with the frame half-processed.
        if (c->state != QUIC_CS_CLOSING)
            _quicConnAbort(c, QUIC_ERR_INTERNAL_ERROR, f->type);
        return false;
    }

    switch (f->type) {
    case QUIC_FRAME_MAX_DATA:
    case QUIC_FRAME_DATA_BLOCKED:
    case QUIC_FRAME_MAX_STREAMS_BIDI:
    case QUIC_FRAME_MAX_STREAMS_UNI:
    case QUIC_FRAME_STREAMS_BLOCKED_BIDI:
    case QUIC_FRAME_STREAMS_BLOCKED_UNI:
        return true;

    default:
        _quicConnAbort(c, QUIC_ERR_STREAM_LIMIT_ERROR, f->type);
        return false;
    }
}

static void handleConnClose(_Inout_ QuicConn* c, _In_ const QuicFrame* f, int64 now)
{
    bool announce = c->state != QUIC_CS_DRAINING && c->state != QUIC_CS_CLOSED;

    c->state         = QUIC_CS_DRAINING;
    c->closePending  = false;
    c->closeDeadline = now + 3 * QUIC_INITIAL_RTT;

    if (announce) {
        c->closeError = f->connClose.error;
        c->closeApp   = f->type == QUIC_FRAME_CONNECTION_CLOSE_APP;
        c->closeLocal = false;

        strDestroy(&c->closeReason);
        if (f->connClose.reasonLen > 0)
            strFromBytes(&c->closeReason, f->connClose.reason, (uint32)f->connClose.reasonLen);

        connNotifyClosed(c);
    }
}

static bool handleFrame(_Inout_ QuicConn* c, int sp, _In_ const QuicFrame* f,
                        _In_ const QuicPktHdr* h, int64 now)
{
    switch (f->type) {
    case QUIC_FRAME_PADDING:
    case QUIC_FRAME_PING:
        return true;

    case QUIC_FRAME_ACK:
    case QUIC_FRAME_ACK_ECN:
        return handleAck(c, sp, f, now);

    case QUIC_FRAME_CRYPTO:
        if (!_quicReasmAdd(&c->pns[sp].cryptoIn, f->crypto.offset, f->crypto.data,
                           (size_t)f->crypto.len)) {
            _quicConnAbort(c, QUIC_ERR_CRYPTO_BUFFER_EXCEEDED, f->type);
            return false;
        }
        return true;

    case QUIC_FRAME_NEW_TOKEN:
        if (c->server || f->newToken.len == 0) {
            _quicConnAbort(c, c->server ? QUIC_ERR_PROTOCOL_VIOLATION
                                        : QUIC_ERR_FRAME_ENCODING_ERROR,
                           f->type);
            return false;
        }
        if (c->handlers && c->handlers->token)
            c->handlers->token(c->hctx, f->newToken.token, (size_t)f->newToken.len);
        return true;

    case QUIC_FRAME_NEW_CONNECTION_ID:
        return handleNewConnId(c, f);

    case QUIC_FRAME_RETIRE_CONNECTION_ID:
        return handleRetireConnId(c, f, h);

    case QUIC_FRAME_PATH_CHALLENGE:
        memcpy(c->pathResponseData, f->path.data, QUIC_PATH_DATA_LEN);
        _quicCtlQueue(&c->pathResponse);

        // Assume the answer goes back the way everything else does. If this challenge turned out to
        // have come from somewhere else, connPeerMoved() sets it again once the whole datagram has
        // been read -- which is the only point at which that is known.
        c->respondElsewhere = false;
        return true;

    case QUIC_FRAME_PATH_RESPONSE:
        if (memcmp(f->path.data, c->pathChallengeData, QUIC_PATH_DATA_LEN) == 0) {
            c->pathValidated = true;
            c->pathDeadline  = 0;
            // The path this one replaced is no longer needed as somewhere to fall back to, and
            // holding an address the peer may have long since left would only be misleading.
            c->havePrev = false;
        }
        return true;

    case QUIC_FRAME_CONNECTION_CLOSE:
    case QUIC_FRAME_CONNECTION_CLOSE_APP:
        handleConnClose(c, f, now);
        return true;

    case QUIC_FRAME_HANDSHAKE_DONE:
        if (c->server) {
            _quicConnAbort(c, QUIC_ERR_PROTOCOL_VIOLATION, f->type);
            return false;
        }
        if (!c->handshakeConfirmed) {
            c->handshakeConfirmed = true;
            c->pathValidated      = true;
            _quicRecovConfirmHandshake(&c->recov, now);
            spaceDiscard(c, QUIC_PNS_HANDSHAKE, now);
            cidReplenish(c);
        }
        return true;

    default:
        return handleStreamFrame(c, f);
    }
}

// RFC 9000 section 9.1. These four frames are the ones an endpoint may send to test a path it is
// not otherwise using, so a packet made only of them says nothing about where the peer has moved to
// -- and an attacker who replayed a captured packet from a forged address could make it look like
// it had.
static bool frameIsProbing(uint64 type)
{
    switch (type) {
    case QUIC_FRAME_PADDING:
    case QUIC_FRAME_PATH_CHALLENGE:
    case QUIC_FRAME_PATH_RESPONSE:
    case QUIC_FRAME_NEW_CONNECTION_ID:
        return true;
    default:
        return false;
    }
}

static bool processFrames(_Inout_ QuicConn* c, int sp, _In_reads_(len) const uint8* payload,
                          size_t len, _In_ const QuicPktHdr* h, _Out_ bool* eliciting,
                          _Inout_ bool* probing, int64 now)
{
    QuicRd rd;
    _quicRdInit(&rd, payload, len);
    *eliciting = false;

    bool any = false;
    QuicFrame f;

    while (_quicFrameDecode(&f, &rd)) {
        any = true;

        if (!frameAllowed(f.type, sp, h->type == QUIC_PKT_0RTT)) {
            _quicConnAbort(c, QUIC_ERR_PROTOCOL_VIOLATION, f.type);
            return false;
        }
        if (frameElicits(f.type))
            *eliciting = true;
        if (!frameIsProbing(f.type))
            *probing = false;
        if (!handleFrame(c, sp, &f, h, now))
            return false;
        if (c->state == QUIC_CS_DRAINING || c->state == QUIC_CS_CLOSED)
            return true;
    }

    // Decoding stops either at the end of the payload or on a frame that could not be read: a
    // truncated one, or a type this version does not define. RFC 9000 section 12.4 makes both a
    // FRAME_ENCODING_ERROR, and there is no way to skip an unknown frame because nothing says how
    // long it is.
    if (rd.bad || _quicRdLeft(&rd) > 0) {
        _quicConnAbort(c, QUIC_ERR_FRAME_ENCODING_ERROR, 0);
        return false;
    }

    // RFC 9000 section 12.4: a packet with nothing in it is a protocol violation, not an empty
    // keepalive.
    if (!any) {
        _quicConnAbort(c, QUIC_ERR_PROTOCOL_VIOLATION, 0);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------------------------
// Packets arriving
// ---------------------------------------------------------------------------------------------

// Moves both directions on to the next generation of keys.
//
// RFC 9001 section 6: a key update is one bit on the wire, and an endpoint that sees the phase it
// is not using has to follow -- deriving the next keys in both directions and sending under them
// from then on. The generation just replaced is kept for a while, because packets sent before the
// peer flipped can still arrive after it.
// `pn` is the packet number that announced the update, or QUIC_PN_NONE when this endpoint started
// it and no packet in the new phase has arrived yet.
static void keyUpdate(_Inout_ QuicConn* c, uint64 pn, int64 now)
{
    QuicPnSpace* s = &c->pns[QUIC_PNS_APP];

    QuicKeys txNext;
    memset(&txNext, 0, sizeof(txNext));
    if (!_quicKeysNext(&txNext, &s->tx))
        return;

    _quicKeysDestroy(&c->rxPrev);
    c->rxPrev = s->rx;      // ownership moves; the old read keys live on here
    s->rx     = c->rxNext;
    memset(&c->rxNext, 0, sizeof(c->rxNext));
    _quicKeysNext(&c->rxNext, &s->rx);

    _quicKeysDestroy(&s->tx);
    s->tx = txNext;

    c->keyPhase      = !c->keyPhase;
    c->rxPhaseFirst  = pn;
    c->txPhaseFirst  = c->pns[QUIC_PNS_APP].next;
    c->keyPhaseAcked = false;
    c->keyUpdates++;

    // RFC 9001 section 6.5: long enough for anything still in flight under the old keys to arrive
    // or be given up on.
    c->rxPrevUntil = now + 3 * _quicRecovPto(&c->recov);

    logFmt(Verbose, _SL("QUIC keys updated to phase ${int}"), stvar(int32, c->keyPhase ? 1 : 0));
}

_Use_decl_annotations_
uint32 _quicConnKeyUpdates(const QuicConn* c)
{
    return c->keyUpdates;
}

_Use_decl_annotations_
bool _quicConnUpdateKeys(QuicConn* c)
{
    QuicPnSpace* s = &c->pns[QUIC_PNS_APP];

    if (!c->handshakeConfirmed || !s->tx.valid || !s->rx.valid || !c->rxNext.valid)
        return false;

    // The first update needs nothing to have been acknowledged in the phase the handshake left
    // behind; every one after it does.
    if (c->keyUpdates > 0 && !c->keyPhaseAcked)
        return false;

    keyUpdate(c, QUIC_PN_NONE, c->now);
    return true;
}

// Opens a 1-RTT packet, following the peer through a key update if that is what the phase bit
// says. Header protection never changes, so a failed attempt leaves the header unprotected and the
// next attempt picks up from there.
static bool openApp(_Inout_ QuicConn* c, _Inout_ QuicPktHdr* h, _Inout_ uint8* pkt,
                    _Out_writes_(outsz) uint8* out, size_t outsz, _Out_ size_t* outLen, int64 now)
{
    QuicPnSpace* s = &c->pns[QUIC_PNS_APP];

    if (_quicPktOpen(&s->rx, h, pkt, s->ack.largest, out, outsz, outLen)) {
        if (c->rxPhaseFirst == QUIC_PN_NONE || h->pn < c->rxPhaseFirst)
            c->rxPhaseFirst = h->pn;
        return true;
    }

    // Nothing was unprotected, so there is no phase to read and nothing to try.
    if (h->pnLen == 0 || h->keyPhase == c->keyPhase)
        return false;

    // Which generation the other phase means is decided by the packet number: below where the
    // current phase started it is the one this connection has already left, above it the next one.
    // Without that test a packet reordered across an update and a packet announcing the next one
    // look identical.
    if (c->keyUpdates > 0 && (c->rxPhaseFirst == QUIC_PN_NONE || h->pn < c->rxPhaseFirst))
        return c->rxPrev.valid && now < c->rxPrevUntil &&
               _quicPktOpen(&c->rxPrev, h, pkt, s->ack.largest, out, outsz, outLen);

    if (!c->rxNext.valid || !_quicPktOpen(&c->rxNext, h, pkt, s->ack.largest, out, outsz, outLen))
        return false;

    // RFC 9001 section 6.1: before the handshake is confirmed there is no key to update from that
    // both ends agree on, so a peer asking for one this early is in error.
    if (!c->handshakeConfirmed) {
        _quicConnAbort(c, QUIC_ERR_KEY_UPDATE_ERROR, 0);
        return false;
    }

    keyUpdate(c, h->pn, now);
    return true;
}

static bool processPacket(_Inout_ QuicConn* c, _Inout_ QuicPktHdr* h, _Inout_ uint8* pkt,
                          uint8 ecn, bool haveEcn, _Inout_ bool* probing, _Inout_ bool* newest,
                          int64 now)
{
    int sp = spaceForPktType(h->type);
    if (sp < 0)
        return false;

    QuicPnSpace* s = &c->pns[sp];

    // A 0-RTT packet is numbered with the application space but protected with the early keys,
    // which is the only place the two come apart.
    QuicKeys* rx = h->type == QUIC_PKT_0RTT ? &c->earlyRx : &s->rx;

    if (s->discarded || !rx->valid)
        return false;

    uint8 payload[QUIC_MAX_DATAGRAM];
    size_t plen;
    if (h->type == QUIC_PKT_SHORT) {
        if (!openApp(c, h, pkt, payload, sizeof(payload), &plen, now))
            return false;
    } else if (!_quicPktOpen(rx, h, pkt, s->ack.largest, payload, sizeof(payload), &plen)) {
        return false;
    }

    _quicQlogPacket(c->qlog, false, h, h->pktLen, payload, plen);

    // The packet is authenticated from here on, so what it says about the path can be believed.
    // RFC 9000 section 9.3 hangs everything on that: a migration decided before this point could
    // be driven by anyone who can forge a source address.
    if (s->ack.largest == QUIC_PN_NONE || h->pn > s->ack.largest)
        *newest = true;

    // A client learns the server's real Source Connection ID from the first packet it can open,
    // and addresses everything after that to it rather than to the random ID it invented.
    if (!c->server && !c->peerCidKnown && h->type != QUIC_PKT_SHORT) {
        c->remote[0].cid = h->scid;
        c->peerCidKnown  = true;
    }

    if (!_quicAckStateAdd(&s->ack, h->pn, now, false))
        return true;   // already seen; its frames were processed the first time

    // RFC 9000 section 13.4.1: the counts are per packet, not per datagram, and go back to the
    // peer in the acknowledgements for this space -- which is how the peer finds out whether the
    // path carried its marks at all.
    if (haveEcn)
        _quicAckStateEcn(&s->ack, ecn);

    if (c->server && sp == QUIC_PNS_HANDSHAKE) {
        // Only a peer that received the server's flight can produce a Handshake packet, so this is
        // proof the address is real.
        c->recvHandshakePkt = true;
        c->addrValidated    = true;
    }

    bool eliciting = false;
    if (!processFrames(c, sp, payload, plen, h, &eliciting, probing, now))
        return true;   // the connection is closing; the packet was still received

    if (eliciting) {
        s->ack.elicitCount++;
        s->ack.pending  = true;
        s->ack.deadline = now;
    }

    if (!cryptoDrain(c, sp))
        return true;

    if (c->state == QUIC_CS_NEW)
        c->state = QUIC_CS_HANDSHAKE;

    return true;
}

// A datagram arrived from an address that is not the one this connection is using. Deciding what
// that means is RFC 9000 section 9, and almost all of it is about not being fooled: an off-path
// attacker who copies a packet and replays it from an address of their choosing must not be able to
// move the connection there, and neither must one who is merely watching.
static void connPeerMoved(_Inout_ QuicConn* c, _In_ const NetAddr* peer,
                          _In_opt_ const NetPktInfo* info, bool probing, bool newest, int64 now)
{
    // A packet made only of probing frames is exactly what a path test looks like, so it is
    // answered on the path it came from and changes nothing else. Its PATH_RESPONSE has already
    // been queued by the frame handler; this is only where the address it goes to is recorded.
    if (probing) {
        c->respondTo        = *peer;
        c->respondElsewhere = true;
        return;
    }

    // Not the newest packet this endpoint has seen, so it is a reordered one from where the peer
    // used to be rather than news about where it is now.
    if (!newest)
        return;

    // A client keeps sending where it dialled. A server changing address mid-connection is the
    // preferred-address mechanism, which is negotiated in the handshake and is not this.
    if (!c->server)
        return;

    // RFC 9000 section 9: neither endpoint may migrate before the handshake is confirmed, so a
    // change of address that early is not one -- and acting on it would let an attacker who
    // reflected a handshake packet steer the connection.
    if (!c->handshakeConfirmed)
        return;

    logFmt(Info, _SL("QUIC peer moved to a new address"), stvNone);

    pathSwitch(c, peer, (info && info->haveLocal) ? &info->local : NULL, true, now);

    // RFC 9000 section 9.5: the peer is addressed by a connection ID it has not used from this
    // address before, so that someone watching both paths cannot tie them together. Without a
    // spare the move still happens -- the alternative is dropping the connection -- but it is
    // linkable, which is a privacy cost and not a correctness one.
    for (uint32 i = 0; i < QUIC_MAX_CIDS; i++) {
        if (i != c->remoteActive && c->remote[i].live) {
            c->remoteActive = i;
            break;
        }
    }
}

static void pathValidationFailed(_Inout_ QuicConn* c, int64 now)
{
    c->pathDeadline = 0;

    if (!c->havePrev) {
        // Nothing to go back to: there is no address left that this endpoint knows reaches the
        // peer, which is what NO_VIABLE_PATH is for.
        connSilentClose(c, QUIC_ERR_NO_VIABLE_PATH, _S"path validation failed");
        return;
    }

    logFmt(Info, _SL("QUIC path validation failed; falling back"), stvNone);

    c->peer      = c->prevPeer;
    c->localAddr = c->prevLocalAddr;
    c->haveLocal = c->prevHaveLocal;
    c->havePrev  = false;

    // The path being returned to was working before it was left, so it does not have to prove
    // itself again -- but nothing that was measured on the path just abandoned applies to it.
    c->pathValidated  = true;
    c->pathSent       = 0;
    c->pathRecv       = 0;
    c->pathAmpLimited = false;
    _quicRecovOnPathChange(&c->recov, now);
}

// A datagram that opens nothing may be a stateless reset: an endpoint that lost this connection's
// state answering with a token only this connection knows. RFC 9000 section 10.3.
static bool checkStatelessReset(_Inout_ QuicConn* c, _In_reads_(len) const uint8* data, size_t len)
{
    if (len < 21 + QUIC_RESET_TOKEN_LEN)
        return false;

    const uint8* tok = data + len - QUIC_RESET_TOKEN_LEN;

    for (uint32 i = 0; i < QUIC_MAX_CIDS; i++) {
        if (!c->remote[i].live || !c->remote[i].haveToken)
            continue;
        if (memcmp(tok, c->remote[i].resetToken, QUIC_RESET_TOKEN_LEN) != 0)
            continue;

        logFmt(Info, _SL("QUIC connection reset by the peer"), stvNone);
        connSilentClose(c, QUIC_ERR_NO_ERROR, _S"stateless reset");
        return true;
    }

    return false;
}

static void handleVersionNeg(_Inout_ QuicConn* c, _In_ const QuicPktHdr* h)
{
    // Only a client that has heard nothing yet can act on one: after that, an unauthenticated
    // packet claiming the version is wrong is an attack on a connection that is already working.
    if (c->server || c->pns[QUIC_PNS_INITIAL].ack.largest != QUIC_PN_NONE)
        return;
    if (!cidEq(&h->dcid, &c->local[0].cid) || !cidEq(&h->scid, &c->remote[c->remoteActive].cid))
        return;

    // A Version Negotiation packet that offers the version already in use is invalid, and RFC 9000
    // section 6.2 says to ignore it rather than act on it.
    for (size_t i = 0; i + 4 <= h->payloadLen; i += 4) {
        uint32 v = ((uint32)h->payload[i] << 24) | ((uint32)h->payload[i + 1] << 16) |
                   ((uint32)h->payload[i + 2] << 8) | h->payload[i + 3];
        if (v == c->version)
            return;
    }

    // cx implements version 1 only, so there is nothing to fall back to. Nothing goes on the wire:
    // the client simply stops.
    connSilentClose(c, QUIC_ERR_CONNECTION_REFUSED, _S"no mutually supported QUIC version");
}

static void handleRetry(_Inout_ QuicConn* c, _In_ const QuicPktHdr* h,
                        _In_reads_(len) const uint8* pkt, size_t len)
{
    // One Retry per connection, and only before anything has been heard from the server. A second
    // one, or one arriving later, is an attacker trying to restart the handshake.
    if (c->server || c->haveRetry || c->pns[QUIC_PNS_INITIAL].ack.largest != QUIC_PN_NONE)
        return;
    if (h->tokenLen == 0 || len < QUIC_TAG_LEN)
        return;

    // The integrity tag is keyed by the connection ID the client used, so only something on the
    // path that saw the Initial can produce one.
    uint8 tag[QUIC_TAG_LEN];
    if (!_quicRetryTag(tag, c->origDcid.id, c->origDcid.len, pkt, len - QUIC_TAG_LEN))
        return;
    if (memcmp(tag, h->tag, QUIC_TAG_LEN) != 0)
        return;

    // A Retry that reuses the client's own Destination Connection ID would leave the Initial keys
    // unchanged, which RFC 9000 section 17.2.5.2 forbids.
    if (h->scid.len == 0 || cidEq(&h->scid, &c->origDcid))
        return;

    QuicKeys client, server;
    if (!_quicKeysInitial(&client, &server, h->scid.id, h->scid.len))
        return;

    QuicPnSpace* s = &c->pns[QUIC_PNS_INITIAL];
    _quicKeysDestroy(&s->tx);
    _quicKeysDestroy(&s->rx);
    s->tx = client;
    s->rx = server;

    c->haveRetry     = true;
    c->retryScid     = h->scid;
    c->remote[0].cid = h->scid;
    c->peerCidKnown  = false;

    bufDestroy(&c->token);
    bufAppendBytes(&c->token, h->token, h->tokenLen);

    // The handshake starts over on the wire but not inside TLS: the same ClientHello goes out
    // again, from the beginning of the CRYPTO stream. Packet numbers carry on, as RFC 9000
    // section 17.2.5.3 requires.
    _quicSendBufReset(&s->cryptoOut);
}

_Use_decl_annotations_
bool _quicConnRecv(QuicConn* c, const NetAddr* peer, const NetPktInfo* info, uint8* data,
                   size_t len, int64 now)
{
    if (c->state == QUIC_CS_CLOSED)
        return false;

    c->now = now;
    c->bytesRecv += len;

    uint8 ecn    = info && info->haveEcn ? info->ecn : 0;
    bool haveEcn = info && info->haveEcn;

    bool elsewhere = !addrEq(peer, &c->peer);

    // Both are decided by the whole datagram, not by any one packet in it, and both are only read
    // once at least one packet in it has been authenticated. "Probing" starts true and is knocked
    // down by the first frame that is not one of the four a path test may use.
    bool probing = true;
    bool newest  = false;

    bool processed = false;
    size_t off     = 0;

    while (off < len) {
        QuicPktHdr h;
        if (!_quicHdrDecode(&h, data + off, len - off, c->localCidLen))
            break;

        if (h.pktLen == 0 || h.pktLen > len - off)
            break;

        // RFC 9000 section 14.1: a client pads every datagram carrying an Initial packet, so one
        // that is short did not come from a conforming client and may be an attempt to get a large
        // reply out of a small request. The whole datagram goes.
        if (c->server && h.type == QUIC_PKT_INITIAL && len < QUIC_INITIAL_MTU)
            break;

        if (h.type == QUIC_PKT_VERSIONNEG) {
            handleVersionNeg(c, &h);
            return c->state != QUIC_CS_CLOSED;
        }

        if (h.type == QUIC_PKT_RETRY) {
            handleRetry(c, &h, data + off, h.pktLen);
            return _quicConnFlush(c, now);
        }

        // A packet in a version this endpoint does not speak cannot be parsed past its connection
        // IDs, so there is no way to find the next one in the datagram either.
        if (h.type == QUIC_PKT_UNKNOWN)
            break;

        if (processPacket(c, &h, data + off, ecn, haveEcn, &probing, &newest, now))
            processed = true;

        if (c->state == QUIC_CS_CLOSED)
            return false;

        off += h.pktLen;
    }

    if (!processed) {
        if (checkStatelessReset(c, data, len))
            return false;
        return c->state != QUIC_CS_CLOSED;
    }

    c->idleDeadline = now + idleTimeoutOf(c);

    if (elsewhere)
        connPeerMoved(c, peer, info, probing, newest, now);
    else if (info && info->haveLocal && !c->haveLocal) {
        // First time the platform has said which of this machine's addresses the peer is reaching
        // it at. Nothing has moved; the path simply became fully known.
        c->localAddr = info->local;
        c->haveLocal = true;
    }

    // Counted after the move, not before it, so a path adopted because this datagram arrived on it
    // starts with the datagram's own bytes to answer with rather than with nothing.
    c->pathRecv += len;

    // RFC 9000 section 10.2.1: a closing endpoint answers further packets by repeating its
    // CONNECTION_CLOSE, so a peer that lost the first one still learns why.
    if (c->state == QUIC_CS_CLOSING)
        c->closePending = true;

    if (c->state == QUIC_CS_DRAINING)
        return true;

    return _quicConnFlush(c, now);
}

// ---------------------------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------------------------

// The idle timeout in force, which is the smaller of what the two endpoints asked for. RFC 9000
// section 10.1 puts a floor under it so a connection is never declared dead sooner than the
// protocol's own retransmission would have taken.
static int64 idleTimeoutOf(_In_ const QuicConn* c)
{
    uint64 mine  = c->localTp.maxIdleTimeout;
    uint64 peers = c->peerTpValid ? c->peerTp.maxIdleTimeout : 0;

    uint64 ms = (mine != 0 && peers != 0) ? (mine < peers ? mine : peers)
                                          : (mine != 0 ? mine : peers);
    if (ms == 0)
        return timeForever;

    // A day is far past any useful idle timeout, and stops a peer's absurd value from overflowing
    // the conversion below.
    if (ms > 86400000)
        ms = 86400000;

    int64 t     = timeMS((int64)ms);
    int64 floor = 3 * QUIC_INITIAL_RTT;
    return t > floor ? t : floor;
}

_Use_decl_annotations_
int64 _quicConnDeadline(const QuicConn* c)
{
    if (c->state == QUIC_CS_CLOSED)
        return timeForever;

    if (c->state == QUIC_CS_CLOSING || c->state == QUIC_CS_DRAINING)
        return c->closePending ? 0 : c->closeDeadline;

    int64 d = c->idleDeadline;

    int64 lossTimer = _quicRecovTimer(&c->recov);
    if (lossTimer < d)
        d = lossTimer;

    // An acknowledgement is only a deadline in a space that has keys to send it under. Asking to
    // be woken for something that cannot be done would spin the timer until it can be.
    for (int sp = 0; sp < QUIC_PNS_COUNT; sp++) {
        const QuicPnSpace* s = &c->pns[sp];
        if (!s->discarded && s->tx.valid && s->ack.pending && s->ack.deadline < d)
            d = s->ack.deadline;
    }

    // Anything the pacer is holding back needs a wake-up of its own, since nothing else is going
    // to arrive to trigger one.
    if (!_quicRecovPacerReady(&c->recov)) {
        int64 p = _quicRecovPacerNext(&c->recov);
        if (p < d)
            d = p;
    }

    // A path validation that is never answered is only noticed by a timer, and so is the moment to
    // measure a path again after a search finished.
    if (c->pathDeadline != 0 && c->pathDeadline < d)
        d = c->pathDeadline;
    if (c->mtuState == QUIC_MTU_DONE && c->mtuRaiseAt != 0 && c->mtuRaiseAt < d)
        d = c->mtuRaiseAt;

    return d;
}

_Use_decl_annotations_
void _quicConnTick(QuicConn* c, int64 now)
{
    if (c->state == QUIC_CS_CLOSED)
        return;

    if (c->state == QUIC_CS_CLOSING || c->state == QUIC_CS_DRAINING) {
        if (c->closePending) {
            _quicConnFlush(c, now);
            return;
        }
        // The closing and draining periods exist so a late packet from the peer still finds
        // someone to answer it, or at least does not reach a connection that has been rebuilt.
        if (c->closeDeadline != 0 && now >= c->closeDeadline)
            c->state = QUIC_CS_CLOSED;
        return;
    }

    if (now >= c->idleDeadline) {
        connSilentClose(c, QUIC_ERR_NO_ERROR, _S"idle timeout");
        return;
    }

    c->now = now;
    _quicRecovOnTimeout(&c->recov, now);

    if (c->pathDeadline != 0 && now >= c->pathDeadline && !c->pathValidated) {
        pathValidationFailed(c, now);
        if (c->state == QUIC_CS_CLOSED || c->state == QUIC_CS_CLOSING) {
            _quicConnFlush(c, now);
            return;
        }
    }

    // A path that was measured a long time ago may not be the path any more. Searching again costs
    // a handful of probes and can only find the path got bigger, since a shrink shows up as loss.
    if (c->mtuState == QUIC_MTU_DONE && c->mtuRaiseAt != 0 && now >= c->mtuRaiseAt) {
        c->mtuRaiseAt = 0;
        c->mtuHigh    = mtuCeiling(c) + 1;
        mtuSearch(c, now);
    }

    _quicConnFlush(c, now);
}

_Use_decl_annotations_
void _quicConnValidatePath(QuicConn* c)
{
    if (psa_generate_random(c->pathChallengeData, QUIC_PATH_DATA_LEN) != PSA_SUCCESS)
        return;

    _quicCtlQueue(&c->pathChallenge);
    c->pathValidated = false;
}

_Use_decl_annotations_
bool _quicConnPathValid(const QuicConn* c)
{
    return c->pathValidated;
}

_Use_decl_annotations_
bool _quicConnMigrate(QuicConn* c, int64 now)
{
    // RFC 9000 section 9: not before the handshake is confirmed, and not at all if the peer said
    // it could not follow.
    if (c->state != QUIC_CS_CONNECTED || !c->handshakeConfirmed)
        return false;
    if (c->peerTpValid && c->peerTp.disableMigration)
        return false;

    // Moving to an address the peer has not seen before while still addressing it by the same
    // connection ID would tie the two together for anyone watching, which is most of what section
    // 9.5 is about. Without a spare ID there is nothing to move behind, so the move is refused
    // rather than made linkable.
    uint32 next = c->remoteActive;
    for (uint32 i = 0; i < QUIC_MAX_CIDS; i++) {
        if (i != c->remoteActive && c->remote[i].live) {
            next = i;
            break;
        }
    }
    if (next == c->remoteActive)
        return false;

    // The peer address does not change -- this endpoint is the one that moved -- so the path is
    // the same pair of ends with a different local half, and the local half is whatever the new
    // socket turns out to be bound to. Everything measured about the old one is still discarded.
    NetAddr peer = c->peer;
    pathSwitch(c, &peer, NULL, false, now);
    c->remoteActive = next;

    // There is nothing to go back to. The path being left is this endpoint's own local address, and
    // whatever was bound there is being given up -- so a validation that fails here has to close the
    // connection rather than return to an address nothing is listening on.
    c->havePrev = false;

    return true;
}

_Use_decl_annotations_
void _quicConnPeerAddr(const QuicConn* c, NetAddr* out)
{
    *out = c->peer;
}

_Use_decl_annotations_
void _quicConnRemoteCid(const QuicConn* c, QuicCid* out)
{
    *out = c->remote[c->remoteActive].cid;
}

_Use_decl_annotations_
bool _quicConnLocalAddr(const QuicConn* c, NetAddr* out)
{
    if (!c->haveLocal)
        return false;
    *out = c->localAddr;
    return true;
}

_Use_decl_annotations_
uint32 _quicConnMigrations(const QuicConn* c)
{
    return c->nmigrations;
}

_Use_decl_annotations_
size_t _quicConnPathMtu(const QuicConn* c)
{
    return c->mtu;
}

_Use_decl_annotations_
bool _quicConnPathMtuDone(const QuicConn* c)
{
    return c->mtuState == QUIC_MTU_DONE;
}

_Use_decl_annotations_
void _quicConnEcnCounts(const QuicConn* c, int sp, uint64* out)
{
    out[0] = c->pns[sp].ack.ecn[0];
    out[1] = c->pns[sp].ack.ecn[1];
    out[2] = c->pns[sp].ack.ecn[2];
}

_Use_decl_annotations_
QuicEarlyState _quicConnEarlyState(const QuicConn* c)
{
    return (QuicEarlyState)c->earlyState;
}

_Use_decl_annotations_
bool _quicConnEcnActive(const QuicConn* c)
{
    return c->ecnState != QUIC_ECN_OFF;
}

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
bool _quicConnFlush(QuicConn* c, int64 now)
{
    if (c->state == QUIC_CS_DRAINING || c->state == QUIC_CS_CLOSED)
        return true;

    c->now = now;
    _quicRecovPacerRefill(&c->recov, now);

    for (int i = 0; i < QUIC_FLUSH_MAX_DATAGRAMS; i++) {
        bool sent = false;
        if (!flushOne(c, now, &sent))
            return false;
        if (!sent)
            break;

        // One datagram carries the whole close; repeating it here would only shout.
        if (c->state == QUIC_CS_CLOSING)
            break;
    }

    // RFC 9001 section 4.9.1: the Initial keys go as soon as the handshake has moved past them,
    // which a client knows by having sent a Handshake packet and a server by having received one.
    if (!c->pns[QUIC_PNS_INITIAL].discarded &&
        ((!c->server && c->sentHandshakePkt) || (c->server && c->recvHandshakePkt)))
        spaceDiscard(c, QUIC_PNS_INITIAL, now);

    if (c->discardHandshakeSoon) {
        c->discardHandshakeSoon = false;
        spaceDiscard(c, QUIC_PNS_HANDSHAKE, now);
    }

    // A PATH_RESPONSE owed to an address the connection is not using cannot ride the normal flush,
    // which only ever addresses the path in use.
    pathRespondElsewhere(c, now);

    mtuStart(c, now);
    mtuProbeSend(c, now);

    // Whether the congestion window is what is holding this connection back, or it simply has
    // nothing more to say. Acknowledgements that come back during the latter measure how fast this
    // endpoint runs out of data rather than how much the path will carry.
    _quicRecovSetAppLimited(&c->recov, !connWantsToSend(c));

    return true;
}

_Use_decl_annotations_
QuicConn* _quicConnCreate(bool server, const QuicConnConfig* cfg, const NetAddr* peer)
{
    if (!_quicInit() || !cfg || !cfg->tls || !peer || cfg->localCidLen > QUIC_MAX_CID)
        return NULL;

    QuicConn* c    = xaAlloc(sizeof(QuicConn), XA_Zero);
    c->server      = server;
    c->state       = QUIC_CS_NEW;
    c->version     = QUIC_VERSION_1;
    c->mtu         = QUIC_INITIAL_MTU;
    c->localCidLen = cfg->localCidLen;
    c->peer        = *peer;
    c->localTp     = cfg->tp;

    c->mtuLow     = QUIC_INITIAL_MTU;
    c->mtuHigh    = QUIC_MAX_DATAGRAM + 1;
    c->mtuState   = QUIC_MTU_IDLE;
    c->mtuProbePn = QUIC_PN_NONE;

    // Marking starts on, because the only way to find out whether a path carries the marks is to
    // put some on it and see whether the peer counts them.
    c->ecnState    = QUIC_ECN_TESTING;
    c->ecnTestPkts = QUIC_ECN_TEST_PKTS;

    for (int sp = 0; sp < QUIC_PNS_COUNT; sp++)
        spaceInit(&c->pns[sp]);

    _quicRecovInit(&c->recov, server, QUIC_INITIAL_MTU);
    _quicRecovSetHandlers(&c->recov, &quicRecovHandlers, c);
    _quicRecovSpaceReady(&c->recov, QUIC_PNS_INITIAL);

    if (!cidGenerate(c, &c->local[0], 0))
        goto fail;

    // Sequence number 0 travels in the handshake itself rather than in a frame, so there is
    // nothing to send and nothing to acknowledge for it.
    c->local[0].announce.state = QUIC_CTL_IDLE;
    c->local[0].notified       = true;
    c->localNextSeq            = 1;

    c->localTp.initScid     = c->local[0].cid;
    c->localTp.haveInitScid = true;

    if (server) {
        memcpy(c->localTp.resetToken, c->local[0].resetToken, QUIC_RESET_TOKEN_LEN);
        c->localTp.haveResetToken = true;
    }

    QuicKeys ckeys, skeys;

    if (server) {
        // A Retry happened if the listener passed the connection ID from before it. The client
        // checks all three of these against what it saw, which is what makes a Retry it did not
        // ask for detectable.
        c->origDcid             = cfg->haveOrigDcid ? cfg->origDcid : cfg->clientDcid;
        c->localTp.origDcid     = c->origDcid;
        c->localTp.haveOrigDcid = true;

        if (cfg->haveOrigDcid) {
            c->haveRetry             = true;
            c->retryScid             = cfg->clientDcid;
            c->localTp.retryScid     = cfg->clientDcid;
            c->localTp.haveRetryScid = true;

            // A token that opened is proof the client received something sent to that address.
            c->addrValidated = true;
        }

        c->remote[0].cid = cfg->peerCid;
        c->peerCidKnown  = true;

        if (!_quicKeysInitial(&ckeys, &skeys, cfg->clientDcid.id, cfg->clientDcid.len))
            goto fail;

        c->pns[QUIC_PNS_INITIAL].rx = ckeys;
        c->pns[QUIC_PNS_INITIAL].tx = skeys;
    } else {
        // The client invents the connection ID its first Initial is addressed to. Both ends derive
        // the Initial keys from it, and it is what the server has to name back in its transport
        // parameters.
        c->origDcid.len = 8;
        if (psa_generate_random(c->origDcid.id, c->origDcid.len) != PSA_SUCCESS)
            goto fail;

        c->remote[0].cid = c->origDcid;

        if (!_quicKeysInitial(&ckeys, &skeys, c->origDcid.id, c->origDcid.len))
            goto fail;

        c->pns[QUIC_PNS_INITIAL].tx = ckeys;
        c->pns[QUIC_PNS_INITIAL].rx = skeys;
    }

    c->remote[0].live = true;
    c->remote[0].seq  = 0;
    c->remoteActive   = 0;

    c->tls = server ? tlsquicCreateServer(cfg->tls) : tlsquicCreateClient(cfg->tls, cfg->hostname);
    if (!c->tls)
        goto fail;

    tlsquicSetHandlers(c->tls, &quicTlsHandlers, c);

    uint8 tpbuf[512];
    QuicWr wr;
    _quicWrInit(&wr, tpbuf, sizeof(tpbuf));
    if (!_quicTpEncode(&wr, &c->localTp, server) ||
        !tlsquicSetTransportParams(c->tls, tpbuf, _quicWrLen(&wr)))
        goto fail;

    c->idleDeadline = timeForever;
    c->rxPhaseFirst = QUIC_PN_NONE;

    // The file is named after the connection ID both ends derived their Initial keys from, so a
    // client's log and a server's log of the same connection sit next to each other.
    c->qlog = _quicQlogCreate(&c->origDcid, server);
    _quicQlogStarted(c->qlog, peer);
    _quicQlogParams(c->qlog, &c->localTp, true);

    return c;

fail:
    _quicConnDestroy(&c);
    return NULL;
}

_Use_decl_annotations_
void _quicConnDestroy(QuicConn** conn)
{
    QuicConn* c = *conn;
    if (!c)
        return;

    for (int sp = 0; sp < QUIC_PNS_COUNT; sp++)
        spaceDestroy(&c->pns[sp]);

    _quicKeysDestroy(&c->earlyTx);
    _quicKeysDestroy(&c->earlyRx);
    _quicKeysDestroy(&c->rxNext);
    _quicKeysDestroy(&c->rxPrev);

    _quicRecovDestroy(&c->recov);

    objRelease(&c->tls);
    bufDestroy(&c->token);
    strDestroy(&c->closeReason);
    _quicQlogDestroy(&c->qlog);
    xaFree(c);
    *conn = NULL;
}

_Use_decl_annotations_
bool _quicConnStart(QuicConn* c, int64 now)
{
    if (c->server || c->state != QUIC_CS_NEW)
        return false;

    c->state        = QUIC_CS_HANDSHAKE;
    c->now          = now;
    c->idleDeadline = now + idleTimeoutOf(c);

    if (!tlsquicStart(c->tls))
        return false;

    return _quicConnFlush(c, now);
}

// ---------------------------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------------------------

// Appends one formatted line to a report. Used only by the debug dump below, where a line is
// built and appended rather than being written into a fixed buffer.
#define DBGLINE(...)                       \
    do {                                   \
        string _l = 0;                     \
        strFormat(&_l, __VA_ARGS__);       \
        strAppend(out, _l);                \
        strDestroy(&_l);                   \
    } while (0)

_Use_decl_annotations_
void _quicConnDebug(const QuicConn* c, int64 now, strhandle out)
{
    const QuicRecovery* r = &c->recov;

    DBGLINE(_SLL("conn: role=${string} state=${uint} hsConfirmed=${uint} addrValidated=${uint} "
                "pathValidated=${uint} pathAmpLimited=${uint} closePending=${uint}\n"),
            stvar(strref, c->server ? _S "server" : _S "client"), stvar(uint32, (uint32)c->state),
            stvar(uint32, r->handshakeConfirmed ? 1u : 0u),
            stvar(uint32, c->addrValidated ? 1u : 0u), stvar(uint32, c->pathValidated ? 1u : 0u),
            stvar(uint32, c->pathAmpLimited ? 1u : 0u), stvar(uint32, c->closePending ? 1u : 0u));

    DBGLINE(_SLL("conn: bytesRecv=${uint} bytesSent=${uint} ampRoom=${int} pathRecv=${uint} "
                "pathSent=${uint} pathAmpRoom=${int} mtu=${uint}\n"),
            stvar(uint64, c->bytesRecv), stvar(uint64, c->bytesSent),
            stvar(int64, (int64)(c->bytesRecv * 3) - (int64)c->bytesSent),
            stvar(uint64, c->pathRecv), stvar(uint64, c->pathSent),
            stvar(int64, (int64)(c->pathRecv * 3) - (int64)c->pathSent),
            stvar(uint64, (uint64)c->mtu));

    DBGLINE(_SL("conn: wantsToSend=${uint} idleIn=${int}us deadlineIn=${int}us\n"),
            stvar(uint32, connWantsToSend(c) ? 1u : 0u), stvar(int64, c->idleDeadline - now),
            stvar(int64, _quicConnDeadline(c) - now));

    bool canSend    = _quicRecovCanSend(r);
    bool pacerReady = _quicRecovPacerReady(r);

    DBGLINE(_SLL("recov: window=${uint} inFlight=${uint} room=${uint} ssthresh=${uint} "
                "canSend=${uint} pacerReady=${uint} pacerIn=${int}us tokens=${int} "
                "appLimited=${uint} inRecovery=${uint}\n"),
            stvar(uint64, r->window), stvar(uint64, r->inFlight),
            stvar(uint64, (uint64)_quicRecovWindowRoom(r)), stvar(uint64, r->ssthresh),
            stvar(uint32, canSend ? 1u : 0u), stvar(uint32, pacerReady ? 1u : 0u),
            stvar(int64, pacerReady ? 0 : _quicRecovPacerNext(r) - now), stvar(int64, r->tokens),
            stvar(uint32, r->appLimited ? 1u : 0u), stvar(uint32, r->inRecovery ? 1u : 0u));

    DBGLINE(_SLL("recov: rttHave=${uint} smoothed=${int} var=${int} min=${int} ptoCount=${uint} "
                "timerIn=${int}us nlost=${uint} ncong=${uint} npersistent=${uint}\n"),
            stvar(uint32, r->rtt.have ? 1u : 0u), stvar(int64, r->rtt.smoothed),
            stvar(int64, r->rtt.var), stvar(int64, r->rtt.min), stvar(uint32, r->ptoCount),
            stvar(int64, r->timer == timeForever ? -1 : r->timer - now), stvar(uint64, r->nlost),
            stvar(uint64, r->ncongestion), stvar(uint64, r->npersistent));

    for (int sp = 0; sp < QUIC_PNS_COUNT; sp++) {
        const QuicPnSpace* s = &c->pns[sp];

        DBGLINE(_SLL("space${uint}: discarded=${uint} txValid=${uint} next=${uint} "
                    "largestAcked=${int} ringBase=${uint} ringCount=${uint} elicitInFlight=${uint} "
                    "probes=${uint} lossTimeIn=${int} lastElicitAgo=${int} ackPending=${uint} "
                    "ackDeadlineIn=${int} cryptoPending=${uint}\n"),
                stvar(uint32, (uint32)sp), stvar(uint32, s->discarded ? 1u : 0u),
                stvar(uint32, s->tx.valid ? 1u : 0u), stvar(uint64, s->next),
                stvar(int64, s->largestAcked == QUIC_PN_NONE ? -1 : (int64)s->largestAcked),
                stvar(uint64, r->sent[sp].basePn), stvar(uint64, (uint64)r->sent[sp].count),
                stvar(uint32, r->elicitInFlight[sp]), stvar(uint32, r->probes[sp]),
                stvar(int64, r->lossTime[sp] == 0 ? -1 : r->lossTime[sp] - now),
                stvar(int64, now - r->lastElicitSent[sp]),
                stvar(uint32, s->ack.pending ? 1u : 0u),
                stvar(int64, s->ack.pending ? s->ack.deadline - now : -1),
                stvar(uint32, _quicSendBufPending(&s->cryptoOut) ? 1u : 0u));
    }

    // The same order flushOne() asks the questions in, so the first "yes" is the reason nothing is
    // going out. Anti-amplification comes before the congestion window because it caps the
    // datagram rather than the window does.
    strref why = _S "nothing-pending";
    if (c->state == QUIC_CS_DRAINING || c->state == QUIC_CS_CLOSED)
        why = _S "closed";
    else if (c->server && !c->addrValidated && c->bytesRecv * 3 <= c->bytesSent)
        why = _S "anti-amplification";
    else if (c->pathAmpLimited && !c->pathValidated && c->pathRecv * 3 <= c->pathSent)
        why = _S "path-anti-amplification";
    else if (!canSend)
        why = _S "congestion-window";
    else if (!pacerReady)
        why = _S "pacer";
    else if (connWantsToSend(c))
        why = _S "nothing-blocking-but-wants-to-send";

    DBGLINE(_SL("verdict: blockedBy=${string}\n"), stvar(strref, why));
}

#undef DBGLINE
