#pragma once

// QUIC streams (RFC 9000 sections 2, 3, 4 and 19.4 through 19.14).
//
// A stream is an ordered byte stream in each direction, multiplexed with every other stream over
// one connection. This layer owns the two halves of each one, the flow control that decides how
// much may be in flight, and the limits on how many streams may exist.
//
// It does not build packets. The connection hands it whatever room is left in a 1-RTT packet and
// it writes frames into that, so the two can be driven independently: this file has no socket, no
// clock, and no packet protection in it.

#include "conn_private.h"

CX_C_BEGIN

// ---------------------------------------------------------------------------------------------
// Stream identifiers (RFC 9000 section 2.1)
// ---------------------------------------------------------------------------------------------

// The low two bits of a stream id say who opened it and which way it runs. The rest is a counter,
// so the streams of one kind are numbered 0, 4, 8, ... in the order they are opened.
#define QUIC_STREAM_SERVER 0x1
#define QUIC_STREAM_UNI    0x2

// The two kinds of stream a limit is counted in, which is what MAX_STREAMS applies to.
#define QUIC_SDIR_BIDI 0
#define QUIC_SDIR_UNI  1
#define QUIC_SDIR_COUNT 2

// The counter is a 62-bit varint with two bits spent on the type, so this is the largest number of
// streams of one kind that can ever exist. RFC 9000 section 19.11 makes a larger MAX_STREAMS a
// FRAME_ENCODING_ERROR.
#define QUIC_MAX_STREAM_COUNT (UINT64_C(1) << 60)

_meta_inline bool _quicStreamUni(uint64 id) { return (id & QUIC_STREAM_UNI) != 0; }
_meta_inline int _quicStreamDir(uint64 id) { return _quicStreamUni(id) ? QUIC_SDIR_UNI
                                                                       : QUIC_SDIR_BIDI; }

// Whether this endpoint opened the stream, given which role it is.
_meta_inline bool _quicStreamLocal(uint64 id, bool server)
{
    return ((id & QUIC_STREAM_SERVER) != 0) == server;
}

_meta_inline uint64 _quicStreamIndex(uint64 id) { return id >> 2; }

_meta_inline uint64 _quicStreamMakeId(uint64 index, bool server, bool uni)
{
    return (index << 2) | (server ? QUIC_STREAM_SERVER : 0) | (uni ? QUIC_STREAM_UNI : 0);
}

// ---------------------------------------------------------------------------------------------
// Stream state (RFC 9000 sections 3.1 and 3.2)
// ---------------------------------------------------------------------------------------------

// The sending half. A stream either runs to the end of its data or is abandoned with a
// RESET_STREAM; the two paths do not rejoin.
#define QUIC_SEND_READY      0   // open, nothing written yet
#define QUIC_SEND_SEND       1   // writing
#define QUIC_SEND_DATA_SENT  2   // the application finished; waiting on the peer
#define QUIC_SEND_DATA_DONE  3   // the peer acknowledged everything, including the end
#define QUIC_SEND_RESET_SENT 4
#define QUIC_SEND_RESET_DONE 5
#define QUIC_SEND_NONE       6   // this half does not exist: a stream the peer opened one way

// The receiving half.
#define QUIC_RECV_RECV       0
#define QUIC_RECV_SIZE_KNOWN 1   // the end of the stream has been named, but not all of it is here
#define QUIC_RECV_DATA_RECVD 2   // every byte has arrived
#define QUIC_RECV_DATA_READ  3   // the application read to the end
#define QUIC_RECV_RESET      4   // the peer abandoned it
#define QUIC_RECV_RESET_READ 5
#define QUIC_RECV_NONE       6

// How much one stream will hold for the application before refusing to take more.
//
// Flow control already bounds this, but only by what the peer advertised, and a peer is free to
// advertise a window far larger than anything worth buffering. This is the memory cost of one
// busy stream; raising it buys throughput on a path with a large delay and bandwidth product.
#define QUIC_STREAM_SEND_MAX (256 * 1024)

// One stream, as the transport sees it. The socket layer's QuicStream is the flow that carries it
// to the application; this is the pair of state machines and buffers underneath.
//
// Both halves are here even for a unidirectional stream, where the half that cannot exist sits in
// its NONE state and is never looked at again.
typedef struct QuicStreamState {
    uint64 id;

    // Sending
    QuicSendBuf out;
    uint64 sendMax;         // the peer's limit for this stream
    uint64 sendBlocked;     // the limit a STREAM_DATA_BLOCKED was last queued for
    uint64 sendFinal;       // the offset the sending half stopped at, for RESET_STREAM
    uint64 resetError;
    QuicCtl fin;            // the frame carrying the end of the stream
    QuicCtl reset;
    QuicCtl blocked;
    uint8 sendState;
    bool finAcked;

    // Receiving
    QuicReasm in;           // in.limit is the offset this endpoint has advertised
    uint64 recvHighest;     // one past the largest offset seen
    uint64 recvFinal;       // the final size, once it is known
    uint64 recvRead;        // bytes the application has taken, or a reset made unreachable
    uint64 recvWindow;      // how far in.limit is kept ahead of recvRead
    uint64 stopError;
    QuicCtl maxData;        // MAX_STREAM_DATA
    QuicCtl stopSending;
    uint8 recvState;
    bool recvFinalKnown;

    bool active;            // on the list of streams with something to send or something in flight
} QuicStreamState;

// ---------------------------------------------------------------------------------------------
// The stream layer
// ---------------------------------------------------------------------------------------------

// What the stream layer tells the layer above.
//
// Every one of these is optional. Without them the streams still work -- data is read by polling
// _quicStreamRecv() -- but nothing above will hear about a stream it did not open itself.
typedef struct QuicStreamHandlers {
    // The peer opened a stream. Returning false refuses it, which closes the connection.
    bool (*opened)(_In_opt_ void* ctx, uint64 id);

    // There is something to read: bytes, or the end of the stream.
    void (*readable)(_In_opt_ void* ctx, uint64 id);

    // The send window opened on a stream that had run out of it.
    void (*writable)(_In_opt_ void* ctx, uint64 id);

    // The peer abandoned its sending half. Nothing more will arrive on this stream.
    void (*reset)(_In_opt_ void* ctx, uint64 id, uint64 error);

    // The peer wants nothing further from this endpoint on this stream.
    void (*stopped)(_In_opt_ void* ctx, uint64 id, uint64 error);

    // Both halves are finished and the stream is about to be forgotten.
    void (*closed)(_In_opt_ void* ctx, uint64 id);
} QuicStreamHandlers;

// Every stream of one connection, and the connection-wide flow control they share.
//
// The two `[QUIC_SDIR_COUNT]` arrays are indexed by QUIC_SDIR_BIDI and QUIC_SDIR_UNI: a limit on
// how many streams may exist applies to each kind separately.
typedef struct QuicStreams {
    const QuicStreamHandlers* handlers;
    void* hctx;
    bool server;

    hashtable byId;         // uint64 -> QuicStreamState*
    sa_uint64 active;       // streams with something to send or something unacknowledged

    // Connection-level flow control (RFC 9000 section 4.1). `sendMax` is the peer's limit on this
    // endpoint and `recvMax` this endpoint's limit on the peer, both counted over every stream.
    uint64 sendMax;
    uint64 sendOff;         // total bytes handed to the send buffers
    uint64 sendBlocked;     // the limit a DATA_BLOCKED was last queued for
    uint64 recvMax;
    uint64 recvOff;         // total bytes received
    uint64 recvRead;        // total bytes the application has taken
    uint64 recvWindow;      // the size recvMax is kept ahead of recvRead by
    QuicCtl maxData;
    QuicCtl dataBlocked;

    // How many streams of each kind may exist (RFC 9000 section 4.6), and how many have been made.
    uint64 sendMaxStreams[QUIC_SDIR_COUNT];
    uint64 recvMaxStreams[QUIC_SDIR_COUNT];
    uint64 nextLocal[QUIC_SDIR_COUNT];
    uint64 nextRemote[QUIC_SDIR_COUNT];
    uint64 streamsBlockedAt[QUIC_SDIR_COUNT];
    QuicCtl maxStreams[QUIC_SDIR_COUNT];
    QuicCtl streamsBlocked[QUIC_SDIR_COUNT];

    // The per-stream windows each end advertised in its transport parameters, which is where a
    // new stream's limits come from.
    uint64 localSdBidiLocal, localSdBidiRemote, localSdUni;
    uint64 peerSdBidiLocal, peerSdBidiRemote, peerSdUni;

    // Why the connection has to close, set when one of the frame calls below returns false.
    uint64 error;
    uint64 errorFrame;
} QuicStreams;

// Sets up the stream layer from this endpoint's own transport parameters, which is where every
// limit it advertises comes from. The peer's parameters arrive later, through
// _quicStreamsPeerParams().
void _quicStreamsInit(_Out_ QuicStreams* ss, bool server, _In_ const QuicTransportParams* tp);
void _quicStreamsDestroy(_Inout_ QuicStreams* ss);

void _quicStreamsSetHandlers(_Inout_ QuicStreams* ss, _In_opt_ const QuicStreamHandlers* handlers,
                             _In_opt_ void* ctx);

// The peer's transport parameters, which is when this endpoint learns how much it may send. Until
// this is called nothing may be sent, because every limit is zero.
void _quicStreamsPeerParams(_Inout_ QuicStreams* ss, _In_ const QuicTransportParams* tp);

// ---------------------------------------------------------------------------------------------
// What the connection calls
// ---------------------------------------------------------------------------------------------

// Handles a frame the connection did not recognise as its own. Returns false when the frame is a
// connection error, with `error` and `errorFrame` saying which.
_Success_(return) bool _quicStreamsFrame(_Inout_ QuicStreams* ss, _In_ const QuicFrame* f);

// Writes stream frames into whatever room is left in a 1-RTT packet. Returns how many bytes were
// written and sets `eliciting` if any of them have to be acknowledged.
size_t _quicStreamsFill(_Inout_ QuicStreams* ss, _Out_writes_(bufsz) uint8* buf, size_t bufsz,
                        uint64 pn, _Out_ bool* eliciting);

// Whether anything is waiting that only the congestion window is holding back.
_Pure bool _quicStreamsWantsToSend(_In_ const QuicStreams* ss);

// A packet was acknowledged, or was declared lost. Whatever it carried finds itself by the packet
// number, the same way the connection's own frames do.
void _quicStreamsAcked(_Inout_ QuicStreams* ss, uint64 pn);
void _quicStreamsLost(_Inout_ QuicStreams* ss, uint64 pn);

// ---------------------------------------------------------------------------------------------
// What the application calls
// ---------------------------------------------------------------------------------------------

// Opens a stream. Returns false when the peer's limit on how many may exist has been reached, and
// queues a STREAMS_BLOCKED frame telling it so.
_Success_(return) bool _quicStreamOpen(_Inout_ QuicStreams* ss, bool uni, _Out_ uint64* id);

// Whether a stream exists, and its two halves' states for a caller that wants to know why a send
// or a receive did nothing.
_Ret_maybenull_ QuicStreamState* _quicStreamFind(_In_ const QuicStreams* ss, uint64 id);

// Queues bytes to send. Returns how many were taken, which is less than `len` when the peer's
// flow control limits or the send buffer are full; the writable handler fires when there is room
// again. Returns 0 for a stream that cannot be written to at all.
size_t _quicStreamSend(_Inout_ QuicStreams* ss, uint64 id, _In_reads_(len) const uint8* data,
                       size_t len);

// Ends the sending half: no more bytes may be queued, and the end of the stream is marked once
// everything already queued has gone out.
void _quicStreamFinish(_Inout_ QuicStreams* ss, uint64 id);

// Reads from a stream. Returns how many bytes were copied, and sets `fin` once the end of the
// stream has been reached -- either the peer finished it or the peer reset it, which the reset
// handler reports separately.
size_t _quicStreamRecv(_Inout_ QuicStreams* ss, uint64 id, _Out_writes_(outsz) uint8* out,
                       size_t outsz, _Out_ bool* fin);

// How many bytes can be read without blocking.
_Pure size_t _quicStreamReadable(_In_ const QuicStreams* ss, uint64 id);

// How many more bytes _quicStreamSend() would take right now.
_Pure size_t _quicStreamWritable(_In_ const QuicStreams* ss, uint64 id);

// Abandons the sending half, telling the peer with a RESET_STREAM that the bytes it has not
// received are not coming.
void _quicStreamReset(_Inout_ QuicStreams* ss, uint64 id, uint64 error);

// Tells the peer this endpoint wants nothing more on this stream. The peer answers with a
// RESET_STREAM, which is what actually ends the receiving half.
void _quicStreamStopSending(_Inout_ QuicStreams* ss, uint64 id, uint64 error);

// A snapshot of connection-level flow control and every live stream, appended to `out` as text.
// For diagnosing a connection that has stopped making progress; not part of the normal API and
// not stable.
void _quicStreamsDebug(_In_ QuicStreams* ss, _Inout_ strhandle out);

CX_C_END
