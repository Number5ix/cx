#pragma once

// The QUIC connection state machine: everything that turns the pure codec in quic_private.h into
// a connection.
//
// Nothing here touches a socket either. A connection is fed datagrams through _quicConnRecv() and
// hands finished ones back through the send handler, so the whole state machine -- handshake,
// connection IDs, path validation, close -- can be driven from a test with two connections wired
// to each other and a clock the test controls.

#include "quic_private.h"

#include <cx/net.h>
#include <cxtls/tlsquic.h>

CX_C_BEGIN

// ---------------------------------------------------------------------------------------------
// Transport parameters (RFC 9000 section 18)
// ---------------------------------------------------------------------------------------------

#define QUIC_TP_ORIG_DCID           0x00
#define QUIC_TP_MAX_IDLE_TIMEOUT    0x01
#define QUIC_TP_STATELESS_RESET     0x02
#define QUIC_TP_MAX_UDP_PAYLOAD     0x03
#define QUIC_TP_INIT_MAX_DATA       0x04
#define QUIC_TP_INIT_MAX_SD_BIDI_L  0x05
#define QUIC_TP_INIT_MAX_SD_BIDI_R  0x06
#define QUIC_TP_INIT_MAX_SD_UNI     0x07
#define QUIC_TP_INIT_MAX_STR_BIDI   0x08
#define QUIC_TP_INIT_MAX_STR_UNI    0x09
#define QUIC_TP_ACK_DELAY_EXPONENT  0x0a
#define QUIC_TP_MAX_ACK_DELAY       0x0b
#define QUIC_TP_DISABLE_MIGRATION   0x0c
#define QUIC_TP_PREFERRED_ADDRESS   0x0d
#define QUIC_TP_ACTIVE_CID_LIMIT    0x0e
#define QUIC_TP_INIT_SCID           0x0f
#define QUIC_TP_RETRY_SCID          0x10

// RFC 9221. Sits well above the block RFC 9000 defines, which is why the decoder cannot tell a
// known id from an unknown one with a range test.
#define QUIC_TP_MAX_DATAGRAM_FRAME  0x20

// One endpoint's transport parameters.
//
// The three connection ID parameters are how an endpoint proves that the connection IDs it used
// before the handshake was authenticated are the ones it meant to use: an attacker who rewrote an
// Initial packet's connection IDs cannot make the values inside the encrypted parameters match.
typedef struct QuicTransportParams {
    QuicCid origDcid;       // server: the DCID of the client's first Initial
    QuicCid initScid;       // both: the SCID of this endpoint's first packet
    QuicCid retryScid;      // server: the SCID of the Retry it sent, if it sent one
    bool haveOrigDcid;
    bool haveInitScid;
    bool haveRetryScid;

    uint8 resetToken[QUIC_RESET_TOKEN_LEN];
    bool haveResetToken;    // server only

    uint64 maxIdleTimeout;  // milliseconds; 0 means no timeout from this endpoint
    uint64 maxUdpPayload;
    uint64 initMaxData;
    uint64 initMaxSdBidiLocal;
    uint64 initMaxSdBidiRemote;
    uint64 initMaxSdUni;
    uint64 initMaxStreamsBidi;
    uint64 initMaxStreamsUni;
    uint64 ackDelayExponent;
    uint64 maxAckDelay;     // milliseconds
    uint64 activeCidLimit;
    uint64 maxDatagramFrame;    // RFC 9221; 0 means this endpoint accepts no DATAGRAM frames
    bool disableMigration;
} QuicTransportParams;

// Fills in the values RFC 9000 section 18.2 defines for parameters that were not sent. Every
// parameter set starts here, both the one an endpoint is about to encode and the one it is about
// to decode into, so an absent parameter and an explicitly default one are the same thing.
void _quicTpDefaults(_Out_ QuicTransportParams* tp);

// Encodes a parameter set. `server` selects which parameters are legal to send: the three that
// only a server may send are dropped from a client's set rather than rejected, since a client
// simply has nothing to put in them.
//
// Parameters equal to their default are omitted, which is what makes a set that has not been
// customized encode to almost nothing.
_Success_(return) bool _quicTpEncode(_Inout_ QuicWr* wr, _In_ const QuicTransportParams* tp,
                                     bool server);

// Decodes a parameter set received from the peer. `fromServer` says which side sent it, so the
// server-only parameters can be rejected when a client sends them.
//
// Returns false on a malformed set, a duplicate parameter, or a value outside the range the RFC
// allows; all of those are TRANSPORT_PARAMETER_ERROR to the caller.
_Success_(return) bool _quicTpDecode(_Out_ QuicTransportParams* tp,
                                     _In_reads_(len) const uint8* data, size_t len,
                                     bool fromServer);

// ---------------------------------------------------------------------------------------------
// Reassembly (RFC 9000 section 2.2)
// ---------------------------------------------------------------------------------------------

// Largest number of holes a reassembly buffer will track. Data that would make a hole past this
// is dropped rather than stored: the peer has to retransmit it anyway if it is never acknowledged,
// and an unbounded list of ranges is something the peer alone decides the size of.
#define QUIC_REASM_MAX_RANGES 16

// A byte range, half-open: [off, end).
typedef struct QuicRange {
    uint64 off;
    uint64 end;
} QuicRange;

// An ordered byte stream arriving out of order.
//
// Holds a window starting at `base`, which is the offset of the first byte that has not yet been
// consumed. Bytes below `base` are discarded on arrival, since they have already been handed to
// the reader.
typedef struct QuicReasm {
    Buffer buf;             // covers [base, base + bufLen(buf))
    uint64 base;
    uint64 limit;           // largest offset that may be stored, for flow control
    QuicRange ranges[QUIC_REASM_MAX_RANGES];   // received, sorted, non-overlapping, non-touching
    uint32 nranges;
} QuicReasm;

void _quicReasmInit(_Out_ QuicReasm* r, uint64 limit);
void _quicReasmDestroy(_Inout_ QuicReasm* r);

// Adds received bytes at a stream offset.
//
// Returns false only when the data runs past `limit`, which is a flow control violation. Data
// that arrives out of order and does not fit in the tracked ranges is dropped silently and
// returns true, since dropping it is a local decision the peer will recover from.
_Success_(return) bool _quicReasmAdd(_Inout_ QuicReasm* r, uint64 off,
                                     _In_reads_(len) const uint8* data, size_t len);

// Number of bytes readable in order from `base`, and a pointer to them.
_Pure size_t _quicReasmReadable(_In_ const QuicReasm* r);
_Ret_maybenull_ _Pure const uint8* _quicReasmData(_In_ const QuicReasm* r);

// Discards `len` bytes from the front, which must be no more than _quicReasmReadable() returned.
void _quicReasmConsume(_Inout_ QuicReasm* r, size_t len);

// ---------------------------------------------------------------------------------------------
// Send buffers
// ---------------------------------------------------------------------------------------------

// Where one range of a send buffer stands.
#define QUIC_CHUNK_PENDING 0   // waiting to go into a packet
#define QUIC_CHUNK_SENT    1   // in a packet the peer has not answered yet
#define QUIC_CHUNK_ACKED   2   // the peer has it

// Ranges a send buffer starts out able to track. It grows from here as it needs to: each packet in
// flight leaves at most one range behind, so a connection with a large congestion window has as
// many as it has packets outstanding, and a fixed ceiling would be a limit on throughput dressed
// up as a limit on bookkeeping.
#define QUIC_SEND_INIT_CHUNKS 16

typedef struct QuicSendChunk {
    uint64 off;
    uint64 end;
    uint64 pn;      // the packet carrying it, while QUIC_CHUNK_SENT
    uint8 state;
} QuicSendChunk;

// An ordered byte stream being sent, held until the peer acknowledges it.
//
// QUIC retransmits what was in a lost packet rather than the packet itself, so a sender cannot
// forget a byte the moment it goes out. This keeps every byte from the first one the peer has not
// acknowledged, and remembers which packet carried each range: an acknowledgement releases the
// range, a loss puts it back in the queue to be sent again in whatever packet it next fits.
typedef struct QuicSendBuf {
    Buffer buf;             // holds the bytes of [base, end)
    uint64 base;            // stream offset of the first byte still held
    uint64 end;             // stream offset just past the last byte added
    QuicSendChunk* chunks;  // sorted, covering [base, end) exactly
    uint32 nchunks;
    uint32 cap;
} QuicSendBuf;

void _quicSendBufInit(_Out_ QuicSendBuf* sb);
void _quicSendBufDestroy(_Inout_ QuicSendBuf* sb);

// Adds bytes to the end of the stream. Returns false only if the bytes could not be stored, which
// is an allocation failure and nothing the caller can retry its way out of.
_Success_(return) bool _quicSendBufAdd(_Inout_ QuicSendBuf* sb,
                                       _In_reads_(len) const uint8* data, size_t len);

// Finds the next range waiting to be sent, no longer than `max` bytes. Returns false when nothing
// is waiting.
_Success_(return) bool _quicSendBufNext(_In_ const QuicSendBuf* sb, size_t max, _Out_ uint64* off,
                                        _Outptr_ const uint8** data, _Out_ size_t* len);

// Records that a range _quicSendBufNext() handed out went into packet `pn`.
void _quicSendBufSent(_Inout_ QuicSendBuf* sb, uint64 off, size_t len, uint64 pn);

// A packet carrying part of this stream was acknowledged, or was declared lost.
void _quicSendBufAcked(_Inout_ QuicSendBuf* sb, uint64 pn);
void _quicSendBufLost(_Inout_ QuicSendBuf* sb, uint64 pn);

// Finds the oldest range still waiting for an answer, so a probe packet can repeat it. The range
// keeps its place: a probe is a duplicate sent to draw out an acknowledgement, not a replacement
// for what is already in flight.
_Success_(return) bool _quicSendBufProbe(_In_ const QuicSendBuf* sb, size_t max, _Out_ uint64* off,
                                         _Outptr_ const uint8** data, _Out_ size_t* len);

// Puts the whole stream back in the queue, for a client whose Retry means the server never saw
// any of what it sent.
void _quicSendBufReset(_Inout_ QuicSendBuf* sb);

// Whether anything is waiting to be sent, and how many bytes the peer has yet to acknowledge.
_Pure bool _quicSendBufPending(_In_ const QuicSendBuf* sb);
_Pure uint64 _quicSendBufOutstanding(_In_ const QuicSendBuf* sb);

// A single control frame that has to keep being sent until the peer acknowledges it.
//
// The same inversion QuicSendBuf uses, for the frames that are one thing rather than a range of
// bytes: rather than each sent packet listing what it carried, each item remembers the packet
// that carried it. An acknowledgement retires the item, a loss puts it back in the queue, and
// nothing has to be stored per packet to make either work.
#define QUIC_CTL_IDLE    0   // nothing to send
#define QUIC_CTL_PENDING 1   // waiting for room in a packet
#define QUIC_CTL_SENT    2   // in a packet the peer has not answered yet

typedef struct QuicCtl {
    uint64 pn;     // the packet carrying it, while QUIC_CTL_SENT
    uint8 state;
} QuicCtl;

// Queues a control frame. Anything already in flight for it is superseded rather than waited for:
// where the frame's contents can change -- a path challenge, the answer to one, a flow control
// limit -- an acknowledgement of the old contents would say nothing about the new, and where they
// cannot, one arriving twice costs the peer nothing.
_meta_inline void _quicCtlQueue(_Inout_ QuicCtl* ctl)
{
    ctl->state = QUIC_CTL_PENDING;
    ctl->pn    = QUIC_PN_NONE;
}

_meta_inline void _quicCtlSent(_Inout_ QuicCtl* ctl, uint64 pn)
{
    ctl->state = QUIC_CTL_SENT;
    ctl->pn    = pn;
}

// Both report whether this was the packet the item was waiting on.
_meta_inline bool _quicCtlAcked(_Inout_ QuicCtl* ctl, uint64 pn)
{
    if (ctl->state != QUIC_CTL_SENT || ctl->pn != pn)
        return false;

    ctl->state = QUIC_CTL_IDLE;
    ctl->pn    = QUIC_PN_NONE;
    return true;
}

_meta_inline bool _quicCtlLost(_Inout_ QuicCtl* ctl, uint64 pn)
{
    if (ctl->state != QUIC_CTL_SENT || ctl->pn != pn)
        return false;

    ctl->state = QUIC_CTL_PENDING;
    ctl->pn    = QUIC_PN_NONE;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Received packet numbers
// ---------------------------------------------------------------------------------------------

// Largest number of acknowledgement ranges an endpoint will track and send. Old ranges fall off
// the small end when the list is full; the peer treats them as lost, which costs a retransmission
// but never correctness.
#define QUIC_MAX_ACK_RANGES 32

// Which packet numbers have been received in one number space, and whether an acknowledgement is
// owed for them.
typedef struct QuicAckState {
    QuicAckRange ranges[QUIC_MAX_ACK_RANGES];   // sorted, largest first
    uint32 nranges;
    uint64 largest;         // largest packet number received, or QUIC_PN_NONE
    int64 largestTime;      // when it arrived, for the ACK delay field
    uint32 elicitCount;     // ack-eliciting packets received since the last ACK was sent
    bool pending;           // an ACK is owed
    int64 deadline;         // when the owed ACK must go out, or 0 if it can wait
    uint64 sentPn;          // the packet the last ACK went in, so its loss can owe another
    uint64 ecn[3];          // ECT(0), ECT(1), CE counts
    bool ecnSeen;
} QuicAckState;

// Records the ECN codepoint a received packet carried, so the counts can be echoed back to the
// peer. `ecn` is a NetEcn value; an unmarked packet is not counted at all.
void _quicAckStateEcn(_Inout_ QuicAckState* as, uint8 ecn);

// Records a received packet. Returns false if this packet number has already been seen, which is
// a duplicate the caller should drop without processing its frames.
_Success_(return) bool _quicAckStateAdd(_Inout_ QuicAckState* as, uint64 pn, int64 now,
                                        bool ackEliciting);

// ---------------------------------------------------------------------------------------------
// Packet number spaces
// ---------------------------------------------------------------------------------------------

// The three packet number spaces of RFC 9000 section 12.3. 0-RTT packets are numbered in the
// application space along with 1-RTT ones, which is why there are three spaces and four
// encryption levels.
typedef enum QuicPnSpaceId {
    QUIC_PNS_INITIAL   = 0,
    QUIC_PNS_HANDSHAKE = 1,
    QUIC_PNS_APP       = 2,
    QUIC_PNS_COUNT     = 3
} QuicPnSpaceId;

// Largest datagram cx will send. One MTU minus the IPv6 and UDP headers, which is the largest
// size that is safe on any path an IPv6 endpoint might be on.
#define QUIC_MAX_DATAGRAM 1452

// Smallest datagram every QUIC endpoint must accept, and so the size cx sends until path MTU
// discovery raises it.
#define QUIC_INITIAL_MTU 1200

// ---------------------------------------------------------------------------------------------
// Path MTU discovery (RFC 8899)
// ---------------------------------------------------------------------------------------------

// How many times one candidate size is probed before the path is taken to be smaller than it. A
// probe is a whole datagram that is deliberately lost when the path is too small, so the count is
// what separates "too big" from "unlucky".
#define QUIC_PMTU_PROBES 3

// How close the search has to get before it stops. Below this the next probe would gain less than
// the round trip it costs.
#define QUIC_PMTU_STEP 16

// How long after a completed search before the path is measured again. A path that grew -- a route
// change, a tunnel going away -- is worth finding, but not often.
#define QUIC_PMTU_RAISE_INTERVAL timeS(600)

// Where a path MTU probe sits in the packet number spaces. Probes are only sent once the handshake
// is confirmed, so this is always the application space; naming it makes the intent visible at the
// call sites.
#define QUIC_PMTU_SPACE QUIC_PNS_APP

// What path MTU discovery is doing (RFC 8899 section 5.2).
typedef enum QuicMtuState {
    QUIC_MTU_IDLE = 0,      // not started: the handshake has not been confirmed yet
    QUIC_MTU_SEARCHING,     // looking for the largest size the path carries
    QUIC_MTU_DONE,          // found it; the raise timer will start another search later
    QUIC_MTU_ERROR          // the base size itself did not get through
} QuicMtuState;

// ---------------------------------------------------------------------------------------------
// ECN (RFC 9000 section 13.4)
// ---------------------------------------------------------------------------------------------

// How many packets are marked at the start of a path before the peer's counts have to show them.
// RFC 9000 section 13.4.2 puts the figure at ten.
#define QUIC_ECN_TEST_PKTS 10

// Whether this endpoint is still marking packets on this path.
typedef enum QuicEcnState {
    QUIC_ECN_TESTING = 0,   // marking, and waiting for the peer's counts to prove the path carries it
    QUIC_ECN_ON,            // the marks came back; congestion reports from the peer are believed
    QUIC_ECN_OFF            // something on the path did not carry them, so nothing is marked
} QuicEcnState;

// The round trip time assumed before one has been measured, from RFC 9002 section 6.2.2.
#define QUIC_INITIAL_RTT timeMS(333)

// ---------------------------------------------------------------------------------------------
// Loss recovery and congestion control (RFC 9002)
// ---------------------------------------------------------------------------------------------

// How many packets may be acknowledged after one before it is declared lost, and the shortest
// delay any of these timers will use. RFC 9002 sections 6.1.1 and 6.1.2.
#define QUIC_PACKET_THRESHOLD 3
#define QUIC_GRANULARITY      timeMS(1)

// How many probe timeouts' worth of unbroken loss means the path is gone rather than congested.
// RFC 9002 section 7.6.
#define QUIC_PERSISTENT_CONGESTION_THRESHOLD 3

// How many sent packets one number space keeps a record of. Resolved packets are kept for a while
// after the fact, because telling a run of losses apart from one with an acknowledgement in the
// middle of it means being able to see what happened either side. This is the ceiling on that: a
// connection with more than this outstanding stops being able to measure the longest runs, which
// costs it a window it could have given up rather than anything it needed.
#define QUIC_SENT_MAX_RETAIN 4096

// How far the exponential backoff on the probe timeout is allowed to run. A connection whose peer
// has stopped answering is ended by the idle timeout long before this; the cap is only here so
// the shift cannot run off the end of the value it is applied to.
#define QUIC_MAX_PTO_BACKOFF 16

// The round trip time estimate of RFC 9002 section 5.
//
// `smoothed` is the running average the probe timeout is built on and `var` its variation;
// `min` is the smallest sample seen, which is what the peer's reported acknowledgement delay is
// measured against.
typedef struct QuicRtt {
    int64 latest;
    int64 smoothed;
    int64 var;
    int64 min;
    int64 firstSample;   // when the first sample was taken
    bool have;
} QuicRtt;

// How a sent packet ended. A packet stays in the ring after it is resolved, because working out
// whether a run of losses was continuous means being able to tell a gap that was acknowledged
// from one that was lost.
#define QUIC_SENT_LIVE  0
#define QUIC_SENT_ACKED 1
#define QUIC_SENT_LOST  2

// One packet this endpoint has sent.
//
// What the packet contained is deliberately not recorded. Everything that would have to be sent
// again instead remembers the packet that carried it, so this stays the same size no matter how
// much a packet held.
// What a packet was, beyond its size. Both of these change how the packet's fate is read: an
// ECT(0) mark is what the peer's ECN counts are checked against, and a path MTU probe that is lost
// says the path is too small rather than that it is congested.
#define QUIC_SENT_ECT0       0x01
#define QUIC_SENT_PMTU_PROBE 0x02

typedef struct QuicSentPkt {
    int64 sent;
    uint32 size;
    uint8 fate;
    uint8 marks;         // QUIC_SENT_* flags
    bool ackEliciting;
    bool inFlight;
    bool fresh;          // acknowledged by the frame currently being processed
} QuicSentPkt;

// The sent packets of one number space.
//
// Packet numbers are handed out in order and none is ever skipped, so a packet is found by the
// distance from the oldest one still recorded rather than by searching.
typedef struct QuicSentRing {
    QuicSentPkt* pkts;
    uint32 cap;        // a power of two, or 0 while nothing has been sent
    uint32 head;       // where basePn sits in the ring
    uint32 count;
    uint64 basePn;
} QuicSentRing;

// What loss recovery tells the connection about. A packet number is all either one carries: the
// connection knows what was in the packet because everything that needs sending again recorded
// the packet it went into.
typedef struct QuicRecovHandlers {
    void (*acked)(_In_opt_ void* ctx, int sp, uint64 pn);
    void (*lost)(_In_opt_ void* ctx, int sp, uint64 pn);
} QuicRecovHandlers;

// Loss recovery and congestion control for one connection.
//
// This is where every decision about *when* to send lives. It never builds a packet and never
// looks at one: the connection tells it what went out and what came back, and it answers with how
// much may be in flight, when the next timer expires, and which packets the connection should
// arrange to send again.
typedef struct QuicRecovery {
    const QuicRecovHandlers* handlers;
    void* hctx;
    bool server;

    QuicSentRing sent[QUIC_PNS_COUNT];
    uint64 largestAcked[QUIC_PNS_COUNT];    // largest the peer has acknowledged, or QUIC_PN_NONE
    int64 lossTime[QUIC_PNS_COUNT];         // when a packet here becomes old enough to be lost
    int64 lastElicitSent[QUIC_PNS_COUNT];   // when the last ack-eliciting packet went out
    uint32 elicitInFlight[QUIC_PNS_COUNT];
    uint32 probes[QUIC_PNS_COUNT];          // probe packets owed after a timeout
    bool ready[QUIC_PNS_COUNT];             // keys exist, so a probe could be sent here
    bool discarded[QUIC_PNS_COUNT];

    QuicRtt rtt;
    int64 maxAckDelay;      // the peer's advertised value
    uint32 ptoCount;        // consecutive probe timeouts, which back the timer off
    int64 timer;            // when the next loss or probe timer expires

    // Congestion control: NewReno, RFC 9002 section 7.
    size_t maxDatagram;
    uint64 window;
    uint64 inFlight;
    uint64 ssthresh;        // UINT64_MAX while in slow start
    uint64 ccAcked;         // bytes acknowledged towards the next window increase
    int64 recoveryStart;
    bool inRecovery;

    // Pacing: a token bucket that refills at the rate the congestion window and round trip time
    // imply, holding at most one initial window so a flight still leaves in one burst.
    int64 tokens;
    int64 tokenTime;

    bool handshakeConfirmed;
    bool peerValidated;     // client: the peer has proved it received something
    bool appLimited;        // there is nothing waiting to send, so the window is not the limit

    // What the path has done to this connection, for whoever is watching it.
    uint64 nlost;
    uint64 ncongestion;
    uint64 npersistent;
} QuicRecovery;

void _quicRecovInit(_Out_ QuicRecovery* r, bool server, size_t maxDatagram);
void _quicRecovDestroy(_Inout_ QuicRecovery* r);
void _quicRecovSetHandlers(_Inout_ QuicRecovery* r, _In_opt_ const QuicRecovHandlers* handlers,
                           _In_opt_ void* ctx);

// The peer's max_ack_delay, once its transport parameters have arrived.
void _quicRecovSetMaxAckDelay(_Inout_ QuicRecovery* r, int64 delay);

// Whether the connection has run out of things to send. While it has, acknowledgements coming
// back say nothing about how much the path will carry, so the congestion window stops growing.
void _quicRecovSetAppLimited(_Inout_ QuicRecovery* r, bool limited);

// A number space has keys and can carry a probe, or has been discarded and can carry nothing.
void _quicRecovSpaceReady(_Inout_ QuicRecovery* r, int sp);
void _quicRecovDiscardSpace(_Inout_ QuicRecovery* r, int sp, int64 now);

// The server refused the 0-RTT a client sent, so every application space packet below `pnEnd` is
// one it never read. They are reported lost -- which is what puts their contents back in the send
// queue -- without being counted as loss, because nothing on the path went wrong.
void _quicRecovDiscardEarly(_Inout_ QuicRecovery* r, uint64 pnEnd, int64 now);

// The handshake is confirmed, which is what lets the application space arm a probe timer and what
// stops a client from probing to prove its address.
void _quicRecovConfirmHandshake(_Inout_ QuicRecovery* r, int64 now);

// Records a packet as it goes out. `inFlight` says whether it counts against the congestion
// window, which every packet does except one carrying nothing but acknowledgements.
void _quicRecovOnSent(_Inout_ QuicRecovery* r, int sp, uint64 pn, size_t size, bool ackEliciting,
                      bool inFlight, int64 now);

// Notes something about a packet that has just been recorded: that it was marked ECT(0), or that it
// is a path MTU probe. Separate from _quicRecovOnSent because both are decided by the datagram the
// packet ended up in rather than by the packet itself.
void _quicRecovMark(_Inout_ QuicRecovery* r, int sp, uint64 pn, uint8 marks);
_Pure uint8 _quicRecovMarksOf(_In_ const QuicRecovery* r, int sp, uint64 pn);

// The peer reported that a packet was marked congestion-experienced on the way. Treated exactly as
// a loss is by the congestion controller, and a round trip earlier.
void _quicRecovOnEcnCe(_Inout_ QuicRecovery* r, int sp, uint64 largestAcked, int64 now);

// Everything the congestion controller and the round trip estimate know is about one path. When
// the connection moves to another they are all wrong at once, so they are thrown away rather than
// adjusted (RFC 9000 section 9.4).
void _quicRecovOnPathChange(_Inout_ QuicRecovery* r, int64 now);

// The datagram size the congestion controller counts windows in. Follows the path MTU as discovery
// raises it.
void _quicRecovSetMaxDatagram(_Inout_ QuicRecovery* r, size_t maxDatagram);

// Processes an acknowledgement. `ackDelay` is the peer's reported delay, already converted out of
// the exponent it was encoded with. Newly acknowledged and newly lost packets are reported
// through the handlers before this returns.
//
// Returns false only if the frame's ranges are malformed.
_Success_(return) bool _quicRecovOnAck(_Inout_ QuicRecovery* r, int sp, _In_ const QuicFrame* f,
                                       int64 ackDelay, int64 now);

// When loss recovery next needs attention, or timeForever.
_Pure int64 _quicRecovTimer(_In_ const QuicRecovery* r);

// One probe timeout: the round trip plus the slack a peer is allowed before acknowledging. Also
// the unit path validation gives up in (RFC 9000 section 8.2.4).
_Pure int64 _quicRecovPto(_In_ const QuicRecovery* r);

// Runs whatever that deadline was for: declaring packets lost, or arming probe packets.
void _quicRecovOnTimeout(_Inout_ QuicRecovery* r, int64 now);

// How many probe packets are owed in a number space. A probe may be sent even when the congestion
// window is full, and is what breaks a deadlock where everything in flight was lost.
_Pure uint32 _quicRecovProbes(_In_ const QuicRecovery* r, int sp);
void _quicRecovProbeSent(_Inout_ QuicRecovery* r, int sp);

// Whether the congestion window has room for another packet.
_Pure bool _quicRecovCanSend(_In_ const QuicRecovery* r);

// Whether it has room for a packet of exactly this size. The plain test only asks whether the
// window is full, which lets a datagram that is being built overshoot it a little; a path MTU probe
// knows its size before it is built and is deliberately larger than anything else, so it waits for
// room it will actually fit in.
_Pure bool _quicRecovCanSendSize(_In_ const QuicRecovery* r, size_t size);

// How many more bytes may be put in flight before the congestion window is full.
_Pure size_t _quicRecovWindowRoom(_In_ const QuicRecovery* r);

// Adds the tokens the time since the last refill has earned the pacer, then reports whether it
// will let a packet out and, if not, when it will. The pacer only ever delays a packet the
// congestion window would have allowed through.
void _quicRecovPacerRefill(_Inout_ QuicRecovery* r, int64 now);
_Pure bool _quicRecovPacerReady(_In_ const QuicRecovery* r);
_Pure int64 _quicRecovPacerNext(_In_ const QuicRecovery* r);

// ---------------------------------------------------------------------------------------------
// Address validation tokens and the packets a listener sends before a connection exists
// ---------------------------------------------------------------------------------------------

// A token is sealed rather than remembered, so a server keeps no per-client state before a
// connection is created. This is the largest one _quicTokenSeal produces.
#define QUIC_MAX_TOKEN 128

// How long a Retry token stays valid, and how long a NEW_TOKEN token does. Retry tokens are
// consumed within one round trip; NEW_TOKEN tokens are meant to be kept by the client and used on
// a later connection, so they live far longer.
#define QUIC_RETRY_TOKEN_LIFETIME timeS(10)
#define QUIC_NEW_TOKEN_LIFETIME   timeS(3600)

// Seals an address validation token for `peer`. `odcid` is the Destination Connection ID of the
// Initial being answered and is only carried by a Retry token, which has to prove which connection
// it belongs to; a NEW_TOKEN token is for a connection that does not exist yet.
//
// `now` is a wall clock time, from clockWall(): a NEW_TOKEN token outlives the process that
// issued it, so the timestamp inside it cannot come from a monotonic clock.
_Success_(return) bool _quicTokenSeal(_Out_writes_bytes_to_(QUIC_MAX_TOKEN, *outLen) uint8* out,
                                      _Out_ size_t* outLen,
                                      _In_reads_bytes_(32) const uint8* key, bool retry,
                                      _In_ const NetAddr* peer, _In_opt_ const QuicCid* odcid,
                                      int64 now);

// Opens a token and checks that it was issued to this peer and has not expired. `odcid` receives
// the connection ID a Retry token carried, and `retry` says which kind it was.
_Success_(return) bool _quicTokenOpen(_In_reads_(len) const uint8* token, size_t len,
                                      _In_reads_bytes_(32) const uint8* key,
                                      _In_ const NetAddr* peer, int64 now, _Out_ bool* retry,
                                      _Out_ QuicCid* odcid);

// Builds a Retry packet: the server's answer to an Initial from an address it has not validated.
// Returns the packet length, or 0 if it does not fit.
size_t _quicRetryBuild(_Out_writes_(outsz) uint8* out, size_t outsz, uint32 version,
                       _In_ const QuicCid* odcid, _In_ const QuicCid* clientScid,
                       _In_ const QuicCid* retryScid, _In_reads_(tokenLen) const uint8* token,
                       size_t tokenLen);

// Builds a Version Negotiation packet listing the versions this endpoint supports. The connection
// IDs are swapped from the packet being answered, as RFC 9000 section 17.2.1 requires.
size_t _quicVersionNegBuild(_Out_writes_(outsz) uint8* out, size_t outsz,
                            _In_ const QuicCid* dcid, _In_ const QuicCid* scid,
                            _In_reads_(nversions) const uint32* versions, size_t nversions);

// Builds a Stateless Reset packet: unpredictable bytes ending in the reset token for a connection
// ID whose state this endpoint has lost.
//
// `maxLen` is one less than the length of the packet that provoked it, so the reset can never be
// used to amplify traffic. Returns 0 if that leaves no room for a plausible packet.
size_t _quicStatelessResetBuild(_Out_writes_(outsz) uint8* out, size_t outsz, size_t maxLen,
                                _In_reads_bytes_(QUIC_RESET_TOKEN_LEN) const uint8* token);

// ---------------------------------------------------------------------------------------------
// Connections
// ---------------------------------------------------------------------------------------------

typedef enum QuicConnState {
    QUIC_CS_NEW,          // created; a client has not sent its first Initial yet
    QUIC_CS_HANDSHAKE,    // handshaking
    QUIC_CS_CONNECTED,    // handshake complete
    QUIC_CS_CLOSING,      // this endpoint closed and is echoing CONNECTION_CLOSE
    QUIC_CS_DRAINING,     // the peer closed; nothing more is sent
    QUIC_CS_CLOSED        // the closing or draining period is over
} QuicConnState;

// How many connection IDs an endpoint tracks in each direction. The active_connection_id_limit
// this endpoint advertises is one less, since sequence number 0 is issued in the handshake rather
// than through a NEW_CONNECTION_ID frame.
#define QUIC_MAX_CIDS 8

// One connection ID this endpoint has issued to the peer, along with the stateless reset token
// that goes with it.
typedef struct QuicLocalCid {
    QuicCid cid;
    uint64 seq;
    uint8 resetToken[QUIC_RESET_TOKEN_LEN];
    QuicCtl announce;   // sequence number 0 goes out in the handshake; the rest need a frame
    bool live;
    bool retired;
    bool notified;      // the layer above has been told about it, which happens once
} QuicLocalCid;

// One connection ID the peer issued to this endpoint.
typedef struct QuicRemoteCid {
    QuicCid cid;
    uint64 seq;
    uint8 resetToken[QUIC_RESET_TOKEN_LEN];
    bool haveToken;
    bool live;
} QuicRemoteCid;

// Everything that is per number space: the keys, the packet numbers in both directions, and the
// handshake bytes travelling over it.
typedef struct QuicPnSpace {
    QuicKeys rx;
    QuicKeys tx;

    uint64 next;            // next packet number to send
    uint64 largestAcked;    // largest packet number the peer has acknowledged, or QUIC_PN_NONE
    QuicAckState ack;

    QuicReasm cryptoIn;     // CRYPTO frames arriving, reassembled into handshake message bytes
    QuicSendBuf cryptoOut;  // handshake message bytes going out, held until acknowledged

    // ECN validation (RFC 9000 section 13.4.2): the counts the peer last reported for this space,
    // and how many of the packets it has newly acknowledged this endpoint had marked. The two are
    // compared on every acknowledgement -- counts that go backwards, or forwards by less than the
    // marks that were acknowledged, mean the path did not carry them.
    uint64 ecnEct0, ecnEct1, ecnCe;
    uint64 ecnAckedMarked;  // marked packets acknowledged since the last comparison

    bool discarded;         // the keys were dropped and the space is gone
} QuicPnSpace;

typedef struct QuicConn QuicConn;

// What a connection needs from the layer above it.
//
// Everything a connection cannot decide for itself leaves through here: where a finished datagram
// goes, what to do with a frame that belongs to a stream, and what to put in a packet that has
// room left. A handler left NULL is not an error -- the connection does the safe thing without it,
// which for the stream hooks means refusing streams the peer tries to open.
typedef struct QuicConnHandlers {
    // Hand a finished datagram to the network. `info` carries the local address it should leave
    // from and the ECN mark it should have; a network that can do neither still has to send it.
    // Returning false stops the flush.
    bool (*send)(_In_opt_ void* ctx, _In_ const NetAddr* peer, _In_ const NetPktInfo* info,
                 _In_reads_bytes_(len) const uint8* data, size_t len);

    // The handshake completed and the connection can carry application data.
    void (*connected)(_In_opt_ void* ctx);

    // The connection is over. `app` says whether the error code is an application one or a
    // transport one, and `local` whether this endpoint closed or the peer did.
    void (*closed)(_In_opt_ void* ctx, uint64 error, bool app, bool local, _In_opt_ strref reason);

    // A frame that belongs to the stream layer arrived. Returning false closes the connection
    // with the error the handler stored through _quicConnAbort().
    bool (*frame)(_In_opt_ void* ctx, _In_ const QuicFrame* f);

    // Fill a 1-RTT packet with stream layer frames. Returns how many bytes were written and sets
    // `ackEliciting` if any of them require acknowledgement. `pn` is the packet they are going
    // into, which is what anything needing retransmission records.
    size_t (*fill)(_In_opt_ void* ctx, _Out_writes_(bufsz) uint8* buf, size_t bufsz, uint64 pn,
                   _Out_ bool* ackEliciting);

    // A 1-RTT packet was acknowledged, or was declared lost. Only the packet number is carried:
    // whatever the stream layer put in it recorded which packet that was.
    void (*pktAcked)(_In_opt_ void* ctx, uint64 pn);
    void (*pktLost)(_In_opt_ void* ctx, uint64 pn);

    // Whether the stream layer has anything waiting that only the congestion window is holding
    // back. Without this the connection cannot tell a full window from an idle one, and grows the
    // window on acknowledgements that measured nothing.
    bool (*wantsToSend)(_In_opt_ void* ctx);

    // This endpoint issued a connection ID, or the peer retired one. A listener routes datagrams
    // by connection ID, so it has to be told as the set changes.
    void (*cidIssued)(_In_opt_ void* ctx, _In_ const QuicCid* cid, uint64 seq,
                      _In_reads_bytes_(QUIC_RESET_TOKEN_LEN) const uint8* resetToken);
    void (*cidRetired)(_In_opt_ void* ctx, _In_ const QuicCid* cid, uint64 seq);

    // The server handed out an address validation token for a later connection.
    void (*token)(_In_opt_ void* ctx, _In_reads_bytes_(len) const uint8* data, size_t len);

    // 0-RTT is armed. On a client that means application data may go out now, under `tp` -- the
    // limits the session being resumed ran under, which is all there is to send by until the
    // server's real parameters arrive. On a server it means the client's 0-RTT packets are about
    // to be read, and `tp` is NULL because the peer's real parameters came with the ClientHello.
    // Returning false declines it and the handshake carries on without early data.
    bool (*earlyOpen)(_In_opt_ void* ctx, _In_opt_ const QuicTransportParams* tp);

    // Client only: the server refused the early data. Everything sent under it has already been
    // put back in the queue to go again once 1-RTT keys exist, so nothing above has to act -- it
    // is reported because whether the first request was replayable is worth knowing.
    void (*earlyReject)(_In_opt_ void* ctx);

    // An unreliable datagram arrived (RFC 9221). `data` points into the packet it came out of and
    // is gone when this returns, so anything that outlives the call has to be copied.
    void (*datagramRecv)(_In_opt_ void* ctx, _In_reads_bytes_(len) const uint8* data, size_t len);

    // The datagram waiting to be sent went out, so _quicConnDatagramSend() will take another.
    void (*datagramWritable)(_In_opt_ void* ctx);
} QuicConnHandlers;

// How a connection is set up. Everything here is copied into the connection, so the struct can be
// a stack local.
typedef struct QuicConnConfig {
    TlsConfig* tls;             // required; must match the role
    strref hostname;            // client only: the name to send in SNI and verify against
    QuicTransportParams tp;     // this endpoint's parameters
    uint8 localCidLen;          // length of the connection IDs this endpoint issues, 0 to 20

    // Server only, and only when the listener answered the first Initial with a Retry: the DCID
    // of that Initial, which has to reach the client in original_destination_connection_id.
    QuicCid origDcid;
    bool haveOrigDcid;

    // Server only: the DCID of the client's first Initial, from which both ends derive the
    // Initial keys.
    QuicCid clientDcid;
    // Both: the peer's Source Connection ID, which is where packets are addressed.
    QuicCid peerCid;
} QuicConnConfig;

// Creates a connection. `peer` is the address packets go to, and for a server the address the
// first Initial came from.
_Ret_maybenull_ QuicConn* _quicConnCreate(bool server, _In_ const QuicConnConfig* cfg,
                                          _In_ const NetAddr* peer);
void _quicConnDestroy(_Inout_ QuicConn** conn);

void _quicConnSetHandlers(_Inout_ QuicConn* c, _In_opt_ const QuicConnHandlers* handlers,
                          _In_opt_ void* ctx);

// Starts a client's handshake, producing its first Initial packet. Servers start when the first
// Initial reaches _quicConnRecv().
_Success_(return) bool _quicConnStart(_Inout_ QuicConn* c, int64 now);

// Processes one received datagram, which may hold several coalesced packets.
// `info` is what the IP layer reported about the datagram: the local address it arrived on, which
// is half of the path it belongs to, and its ECN mark. Either may be absent; NULL means both are.
_Success_(return) bool _quicConnRecv(_Inout_ QuicConn* c, _In_ const NetAddr* peer,
                                     _In_opt_ const NetPktInfo* info,
                                     _Inout_updates_bytes_(len) uint8* data, size_t len,
                                     int64 now);

// Sends whatever is pending: acknowledgements, handshake bytes, connection ID management, and
// whatever the fill handler adds. Called after a receive, after the layer above does something,
// and when a deadline from _quicConnDeadline() passes.
_Success_(return) bool _quicConnFlush(_Inout_ QuicConn* c, int64 now);

// When this connection next needs attention, or timeForever if it does not. Zero means it needs
// attention now: there is something waiting to go out that no timer is holding back.
_Pure int64 _quicConnDeadline(_In_ const QuicConn* c);

// Runs whatever the deadline was for. Safe to call at any time.
void _quicConnTick(_Inout_ QuicConn* c, int64 now);

// Sends a PATH_CHALLENGE frame and waits for the answer. _quicConnPathValid() reports whether one
// came back. Only meaningful once 1-RTT keys exist.
void _quicConnValidatePath(_Inout_ QuicConn* c);
_Pure bool _quicConnPathValid(_In_ const QuicConn* c);

// Moves the connection to a new local address of its own choosing -- the layer above has bound a
// new socket and is about to start sending from it. The new path is probed, the congestion
// controller and round trip estimate are thrown away, and an unused connection ID is put in front
// of the peer so the move cannot be linked to what came before.
//
// Returns false if the connection is not in a position to migrate: the handshake is not confirmed,
// the peer forbade migration, or there is no spare connection ID to move to.
_Success_(return) bool _quicConnMigrate(_Inout_ QuicConn* c, int64 now);

// The peer's address on the path currently in use, and the local address packets leave from (which
// is only known if the platform reported it on a received datagram).
void _quicConnPeerAddr(_In_ const QuicConn* c, _Out_ NetAddr* out);
_Success_(return) bool _quicConnLocalAddr(_In_ const QuicConn* c, _Out_ NetAddr* out);

// How many times the connection has moved to a different path, whether because the peer's address
// changed or because this endpoint asked for it.
_Pure uint32 _quicConnMigrations(_In_ const QuicConn* c);

// The largest datagram the connection will send, which is the base size until path MTU discovery
// raises it, and whether discovery has finished searching.
_Pure size_t _quicConnPathMtu(_In_ const QuicConn* c);
_Pure bool _quicConnPathMtuDone(_In_ const QuicConn* c);

// The ECN counts this endpoint has tallied for packets it received in one number space: ECT(0),
// ECT(1) and CE, in that order. These are what its acknowledgements carry back to the peer.
void _quicConnEcnCounts(_In_ const QuicConn* c, int sp, _Out_writes_(3) uint64* out);

// Whether ECN is still being used on this path. Marking stops for good once something on the path
// is seen not to carry the marks (RFC 9000 section 13.4.2).
_Pure bool _quicConnEcnActive(_In_ const QuicConn* c);

// Closes the connection, sending a CONNECTION_CLOSE frame carrying `error`. `app` selects an
// application error code rather than a transport one.
void _quicConnClose(_Inout_ QuicConn* c, uint64 error, bool app, _In_opt_ strref reason);

// Records a transport error from inside frame processing. The frame handler calls this and then
// returns false; the connection turns it into a CONNECTION_CLOSE.
void _quicConnAbort(_Inout_ QuicConn* c, uint64 error, uint64 frameType);

_Pure QuicConnState _quicConnGetState(_In_ const QuicConn* c);

// What became of 0-RTT on this connection.
typedef enum QuicEarlyState {
    QUIC_ES_NONE = 0,   // never used: no ticket, no permission, or nothing to resume
    QUIC_ES_LIVE,       // in use, and the handshake has not finished vouching for it yet
    QUIC_ES_DONE,       // it was used, and the handshake has since confirmed it
    QUIC_ES_REFUSED     // client: the server would not take it, and the data went again
} QuicEarlyState;

_Pure QuicEarlyState _quicConnEarlyState(_In_ const QuicConn* c);

// Moves both directions on to the next generation of 1-RTT keys (RFC 9001 section 6). Following a
// peer that does this is automatic; starting one is not, so this is how an endpoint that wants to
// rotate its own keys says so.
//
// Returns false when the connection is not in a position to: the handshake is not confirmed, there
// are no 1-RTT keys yet, or the last update has not been acknowledged.
_Success_(return) bool _quicConnUpdateKeys(_Inout_ QuicConn* c);

// How many times the keys have moved on, counting updates this endpoint started and updates it
// followed.
_Pure uint32 _quicConnKeyUpdates(_In_ const QuicConn* c);

// Whether the handshake is confirmed, which is a later moment than complete and is not the same on
// the two sides. A server confirms when it checks the client's Finished; a client confirms when the
// server tells it so with a HANDSHAKE_DONE frame. Nothing may be retransmitted in the Handshake
// number space after this, and its keys are gone.
_Pure bool _quicConnHandshakeConfirmed(_In_ const QuicConn* c);
_Pure bool _quicConnIsClosed(_In_ const QuicConn* c);

// The peer's transport parameters, or the defaults until they arrive.
_Ret_valid_ _Pure const QuicTransportParams* _quicConnPeerParams(_In_ const QuicConn* c);

// The TLS handshake, so the layer above can read the ALPN protocol and the peer's certificate.
_Ret_maybenull_ _Pure TlsQuic* _quicConnTls(_In_ const QuicConn* c);

// The peer's connection ID that packets are currently addressed to. This changes when the
// connection moves to a new path, so that an observer of both cannot tie them together.
void _quicConnRemoteCid(_In_ const QuicConn* c, _Out_ QuicCid* out);

// The connection ID this endpoint issued at sequence number zero, which is what a listener routes
// the connection's first packets by.
void _quicConnLocalCid(_In_ const QuicConn* c, _Out_ QuicCid* out);

// True once the peer's address has been validated, either by a token or by the handshake getting
// far enough that the peer must have received something at that address.
_Pure bool _quicConnValidated(_In_ const QuicConn* c);

// The largest unreliable datagram (RFC 9221) that can be sent right now, or 0 when the channel is
// not available: either endpoint left max_datagram_frame_size at zero, or the peer's parameters
// have not arrived. The value rises as path MTU discovery raises the packet size, so it is worth
// reading before each send rather than once.
_Pure size_t _quicConnMaxDatagram(_In_ const QuicConn* c);

// Queues one unreliable datagram. There is room for exactly one at a time, so this fails when the
// previous one has not gone out yet -- which only happens while the congestion window or the pacer
// is holding it back. It also fails for a payload larger than _quicConnMaxDatagram().
//
// Nothing is retransmitted: a datagram lost on the way is simply gone. It is still ack-eliciting
// and still counts against the congestion window, per RFC 9221 section 5.
_Success_(return) bool _quicConnDatagramSend(_Inout_ QuicConn* c,
                                             _In_reads_bytes_(len) const uint8* data, size_t len);

// Loss recovery and congestion control, for reading only: the round trip estimate, the congestion
// window, and the counters a connection's behaviour under loss is judged by.
_Ret_valid_ _Pure const QuicRecovery* _quicConnRecovery(_In_ const QuicConn* c);

// A snapshot of everything that decides whether this connection can send right now, appended to
// `out` as text. For diagnosing a connection that has stopped making progress; not part of the
// normal API and not stable.
void _quicConnDebug(_In_ const QuicConn* c, int64 now, _Inout_ strhandle out);

CX_C_END
