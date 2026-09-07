#pragma once

#include <cx/net.h>
#include <cxtls.h>

#include <cxquic/quic_shared.h>

/// @file quicnet.h
/// @brief One-call setup and the stream operations QUIC adds to a flow

CX_C_BEGIN

/// @addtogroup quic_net
/// @{

/// What became of 0-RTT on a connection
///
/// 0-RTT lets a client send its first request in the same flight as its handshake, a full round
/// trip before the connection is established. What it costs is that those bytes are **replayable**:
/// anyone who copied them off the network can send them to the server a second time, and nothing
/// in the protocol can tell the copy from the original. Everything sent after the handshake
/// finishes is safe again.
///
/// Turn it on with tlsconfigSetEarlyData() on the TLS configuration, on whichever side wants it;
/// it does nothing unless tlsconfigSetResumption() is on as well, since there is nothing to resume
/// without a ticket.
typedef enum QuicEarlyData {
    QUIC_EARLY_None = 0,   ///< 0-RTT was not used on this connection
    QUIC_EARLY_Pending,    ///< In use: what has crossed this connection so far could be a replay
    QUIC_EARLY_Accepted,   ///< It was used, and the handshake has since vouched for it
    QUIC_EARLY_Rejected    ///< Client only: the server would not take it, and the data went again
} QuicEarlyData;

/// How a QUIC endpoint is configured
///
/// Every field except `tls` may be left zero, which selects the default beside it. The four limits
/// are what this endpoint advertises to the peer -- they bound what the peer may send, not what
/// this endpoint may.
typedef struct QuicConfig {
    /// @brief TLS configuration the handshake runs against, required
    ///
    /// A client one for netquicConnect(), a server one for netquicListen(). Set the application
    /// protocol on it with tlsconfigSetALPN() before connecting or listening.
    TlsConfig* tls;

    uint64 maxData;          ///< Bytes the peer may have in flight across all streams (default 1 MB)
    uint64 maxStreamData;    ///< Bytes the peer may have in flight on one stream (default 256 KB)
    uint64 maxStreamsBidi;   ///< Bidirectional streams the peer may open (default 100)
    uint64 maxStreamsUni;    ///< Unidirectional streams the peer may open (default 100)

    /// @brief Microseconds of silence after which the connection is abandoned (default 30 seconds)
    ///
    /// Both ends advertise one and the smaller of the two applies. See timeS() / timeMS().
    int64 idleTimeout;

    /// @brief Server: prove the client's address before creating any connection state
    ///
    /// Answers a new client's first packet with a Retry, which the client must echo back with the
    /// token it carried. That costs one extra round trip per connection and makes a flood of
    /// spoofed source addresses cost the server nothing but the reply.
    bool retry;

    /// @brief Offer the unreliable datagram channel (RFC 9221)
    ///
    /// Both ends must set it, and it can only be agreed during the handshake. Off by default. See
    /// netquicOpenDatagram().
    bool datagrams;
} QuicConfig;

/// Write a qlog file for every connection created after this call
///
/// qlog is the standard debugging format for QUIC: one file per connection, holding every packet
/// with its frames decoded, the transport parameters both ends chose, and how the congestion
/// window and round trip estimate moved. The files are named after the connection ID, so the two
/// ends of one handshake sit next to each other, and they can be read with any qlog viewer.
///
/// This costs a line of JSON per packet, written as it happens, so it is a debugging switch rather
/// than something to leave on in a shipping program.
///
/// @param dir Existing directory to write into, or empty to stop logging
///
/// Example:
/// @code
///   netquicQlog(_S"/tmp/qlog");
/// @endcode
void netquicQlog(_In_opt_ strref dir);

/// Open a QUIC listener
///
/// Creates the UDP endpoint, binds it, and returns a listening socket that delivers NET_Accepted
/// for every connection whose handshake completes. The handlers are inherited by each accepted
/// connection, so no event can arrive before there is something to deliver it to.
///
/// A connection carrying 0-RTT is handed over earlier than that; see #QuicEarlyData.
///
/// @param q Queue to register the listener with
/// @param addr Address and port to bind, port 0 to let the OS choose (read it back from
///             `sock->local`)
/// @param cfg Endpoint configuration; `cfg->tls` must be a server configuration
/// @param handlers Handler set for the listener and everything it accepts, or NULL for none. Not
///                 copied -- it must outlive the socket.
/// @param ctx Context passed to those handlers on NetEvent.ctx
/// @return The listening socket (a reference the caller must release), or NULL on failure
///
/// Example:
/// @code
///   NetAddr addr;
///   netAddrFromStr(&addr, _SL("0.0.0.0"));
///   addr.port = 4433;
///
///   QuicConfig cfg = { .tls = servercfg };
///   NetSocket *lsn = netquicListen(q, &addr, &cfg, &handlers, NULL);
/// @endcode
_Ret_maybenull_ NetSocket* netquicListen(_In_ NetQueue* q, _In_ const NetAddr* addr,
                                         _In_ const QuicConfig* cfg,
                                         _In_opt_ const NetHandlers* handlers, _In_opt_ void* ctx);

/// Open a QUIC connection
///
/// Resolves the host, creates a UDP endpoint of its own, and starts the handshake. The call
/// returns as soon as that is under way; NET_Connection is delivered on the socket's control flow
/// when the handshake completes or fails, exactly as it is for a TCP socket -- or as soon as 0-RTT
/// is armed, if this connection is resuming one and #QuicEarlyData applies.
///
/// @param q Queue to register the connection with
/// @param host Hostname or literal address to connect to
/// @param port Port number, host byte order
/// @param hostname Name to send in SNI and verify the certificate against; empty uses `host`
/// @param cfg Endpoint configuration; `cfg->tls` must be a client configuration
/// @param handlers Handler set for the connection, or NULL for none. Not copied -- it must
///                 outlive the socket.
/// @param ctx Context passed to those handlers on NetEvent.ctx
/// @return The connection socket (a reference the caller must release), or NULL if the connect
///         could not be started
///
/// Example:
/// @code
///   QuicConfig cfg = { .tls = clientcfg };
///   NetSocket *sock = netquicConnect(q, _S"example.com", 443, NULL, &cfg, &handlers, NULL);
/// @endcode
_Ret_maybenull_ NetSocket* netquicConnect(_In_ NetQueue* q, _In_ strref host, uint16 port,
                                          _In_opt_ strref hostname, _In_ const QuicConfig* cfg,
                                          _In_opt_ const NetHandlers* handlers,
                                          _In_opt_ void* ctx);

/// Dial a QUIC connection, with a look at the socket before it starts
///
/// netquicConnect() with one addition: `prep` is called with the finished connection socket
/// immediately before the handshake begins. Use it when the socket has to be reachable from
/// somewhere else -- a request, a session, a cancel path -- before its first event can arrive,
/// which the return value is too late for: the handshake can complete on another thread while
/// this call is still returning.
///
/// A NULL return means nothing was started. Anything `prep` stored is the caller's to clean up.
///
/// @param q Queue to register the connection with
/// @param host Hostname or literal address to connect to
/// @param port Port number, host byte order
/// @param hostname Name to send in SNI and verify the certificate against; empty uses `host`
/// @param cfg Endpoint configuration; `cfg->tls` must be a client configuration
/// @param handlers Handler set for the connection, or NULL for none. Not copied -- it must
///                 outlive the socket.
/// @param ctx Context passed to those handlers on NetEvent.ctx
/// @param prep Called with the socket just before the handshake starts, or NULL for none
/// @param prepctx Context passed to `prep`
/// @return The connection socket (a reference the caller must release), or NULL if the connect
///         could not be started
///
/// Example:
/// @code
///   NetSocket *sock = netquicConnectPrep(q, _S"example.com", 443, NULL, &cfg, &handlers, NULL,
///                                        publishSock, req);
/// @endcode
_Ret_maybenull_ NetSocket* netquicConnectPrep(_In_ NetQueue* q, _In_ strref host, uint16 port,
                                              _In_opt_ strref hostname, _In_ const QuicConfig* cfg,
                                              _In_opt_ const NetHandlers* handlers,
                                              _In_opt_ void* ctx,
                                              _In_opt_ NetConnectPrepCB prep,
                                              _In_opt_ void* prepctx);

/// Close a QUIC connection, telling the peer why
///
/// Sends a CONNECTION_CLOSE carrying an application error code and tears the socket down. Plain
/// netsocketClose() does the same thing with a code of zero.
///
/// @param sock Connection socket to close
/// @param error Application error code for the peer
/// @param reason Human-readable reason, or empty for none
void netquicClose(_In_ NetSocket* sock, uint64 error, _In_opt_ strref reason);

/// Move this connection to a fresh local port
///
/// The connection stops using the socket it has been on and takes a new one, then proves to the
/// peer that the new path works before trusting it. Everything above -- the streams, their
/// contents, the socket handle the application holds -- is untouched: what changes is the address
/// the packets come from, which is exactly what a QUIC connection is built to survive.
///
/// Use it when the local address is about to become useless or unwelcome: a laptop leaving one
/// network for another, or an application that would rather not be followed from one address to the
/// next.
///
/// This is asynchronous. It returns once the move has started, and the peer is not told in advance
/// -- what proves the new address works is traffic arriving from it. There is no going back: the
/// socket the connection was using is given up as part of the move, so if the peer never answers on
/// the new one the connection closes with a NET_Error.
///
/// @param sock A connected QUIC socket, client side
/// @return true if the move started, false if the connection is not in a position to move -- the
///         handshake is not finished, it is a server, the peer asked not to be migrated to, or
///         there was no spare connection ID to move behind
_Success_(return) bool netquicMigrate(_Inout_ NetSocket* sock);

/// The largest datagram this connection is sending
///
/// Starts at the 1200 bytes every QUIC endpoint must accept and grows as the connection measures
/// what the path really carries. Useful for reporting; nothing needs to consult it to send.
///
/// @param sock A QUIC connection socket
/// @return The datagram size in bytes, or 0 if this is not a connected QUIC socket
_Pure size_t netquicPathMtu(_In_ NetSocket* sock);

/// Whether this connection's packets are still being marked for congestion notification
///
/// Marking is turned off for good the moment the peer's counts show that something on the path did
/// not carry the marks, which is common enough that this being false says nothing is wrong.
///
/// @param sock A QUIC connection socket
/// @return true if packets are still marked
_Pure bool netquicEcn(_In_ NetSocket* sock);

/// How many times this connection has moved to a different network path
///
/// Counts both the moves this endpoint asked for with netquicMigrate() and the ones it followed
/// because the peer's address changed -- which on a server is how a client roaming between networks,
/// or a NAT rebinding its mapping, shows up.
///
/// @param sock A QUIC connection socket
/// @return The number of moves, 0 for a connection that has stayed where it started
_Pure uint32 netquicMigrations(_In_ NetSocket* sock);

/// Whether this connection carried 0-RTT, and whether it is still replayable
///
/// A connection using 0-RTT is handed over early -- NET_Connection on a client, NET_Accepted on a
/// server -- so that the round trip 0-RTT saves is actually saved. Until the handshake finishes it
/// reads QUIC_EARLY_Pending, and an application that has anything to send that must not happen
/// twice should hold it until then. The moment it becomes safe arrives as NET_FilterNotify with
/// NFN_Secured on the connection's control flow, which is delivered only on connections that used
/// 0-RTT and means exactly this.
///
/// A client never has to act on QUIC_EARLY_Rejected: whatever was sent early has already been
/// queued to go again.
///
/// @param sock A QUIC connection socket
/// @return What became of 0-RTT here
_Pure QuicEarlyData netquicEarlyData(_In_ NetSocket* sock);

/// The application protocol the handshake settled on
///
/// @param sock Connection socket
/// @param out Receives the protocol name; empty if none was negotiated
/// @return true if the handshake has finished and a protocol was negotiated
_Success_(return) bool netquicALPN(_In_ NetSocket* sock, _Inout_ strhandle out);

/// Open a stream on a connection
///
/// The new stream's NET_FlowOpen is delivered on a worker like any other flow's, so a handler
/// registered for it still runs; the flow is returned here as well so the caller can start sending
/// without waiting for the event.
///
/// @param sock Connection socket
/// @param uni Open a unidirectional stream (this endpoint sends, the peer only receives)
/// @return The new stream's flow (a reference the caller must release), or NULL if the peer's
///         limit on how many streams may exist has been reached -- in which case a
///         STREAMS_BLOCKED frame has been queued telling it so
_Ret_maybenull_ NetFlow* netquicOpen(_In_ NetSocket* sock, bool uni);

/// Open the unreliable datagram channel of a connection
///
/// A connection has at most one, and this returns the same flow every time. Sending on it puts one
/// datagram on the wire whole; each one that arrives is a single NET_DataReceived carrying the
/// whole payload in NetEvent::recv.msg, the way a UDP socket delivers one. Nothing is
/// retransmitted -- a datagram lost on the way is gone, and nothing is delivered to say so.
///
/// The stream calls (netquicRecv(), netquicWritable(), netquicFinish(), netquicReset()) do not
/// apply to this flow.
///
/// @param sock Connection socket
/// @return The datagram flow (a reference the caller must release), or NULL if either end left
///         QuicConfig::datagrams off, or the handshake has not settled the parameters yet
///
/// Example:
/// @code
///   NetFlow *dg = netquicOpenDatagram(sock);
///   if (dg && len <= netquicMaxDatagram(sock))
///       netflowSend(dg, data, len, 0);
/// @endcode
_Ret_maybenull_ NetFlow* netquicOpenDatagram(_In_ NetSocket* sock);

/// Largest datagram netflowSend() would accept on the datagram flow right now
///
/// This grows as path MTU discovery finds room, so read it before each send rather than keeping
/// the answer. A send larger than this is refused.
///
/// @param sock Connection socket
/// @return Number of bytes that fit in one datagram, or 0 if the connection has no datagram
///         channel
size_t netquicMaxDatagram(_In_ NetSocket* sock);

/// Read from a stream
///
/// This is what reopens the stream's receive window, so an application that stops calling it stops
/// the peer from sending rather than buffering without limit.
///
/// @param flow Stream flow, from NetEvent::flow or netquicOpen()
/// @param buf Buffer to read into
/// @param bufsz Size of the buffer
/// @param fin Receives true once the end of the stream has been reached, meaning nothing further
///            will ever arrive on it
/// @return Number of bytes copied, which is 0 when nothing is readable
size_t netquicRecv(_In_ NetFlow* flow, _Out_writes_(bufsz) uint8* buf, size_t bufsz,
                   _Out_opt_ bool* fin);

/// Bytes that netquicRecv() would return right now
///
/// @param flow Stream flow
/// @return Number of bytes readable without blocking
size_t netquicReadable(_In_ NetFlow* flow);

/// Bytes that netflowSend() would accept right now
///
/// A send of more than this is refused, and nothing is queued. NET_SendReady is delivered on the
/// stream once there is more room, so a refused send is always followed by one -- there is no need
/// to spend the window down to exactly zero to be told, and a protocol whose smallest unit does
/// not fit in what is left may simply stop.
///
/// The event waits for enough room to be worth having rather than firing on the first byte that
/// comes free: at least as much as the refused send asked for, and at least NetSocket::sendLow.
/// A sender writing whole frames is therefore woken once per frame it can write, not once per
/// packet the peer acknowledges. Room the connection turns out not to be able to reach does not
/// strand it -- the event still arrives with whatever there is.
///
/// @param flow Stream flow
/// @return Number of bytes that can be sent
size_t netquicWritable(_In_ NetFlow* flow);

/// End the sending half of a stream
///
/// The peer sees the end of the stream once everything already queued has reached it. Nothing more
/// may be sent afterwards; the stream stays open in the other direction until the peer ends its
/// half too, and NET_FlowClosed follows when both are done.
///
/// @param flow Stream flow
void netquicFinish(_In_ NetFlow* flow);

/// Abandon the sending half of a stream
///
/// Tells the peer with a RESET_STREAM that the bytes it has not received are not coming.
///
/// @param flow Stream flow
/// @param error Application error code for the peer
void netquicReset(_In_ NetFlow* flow, uint64 error);

/// Ask the peer to stop sending on a stream
///
/// The peer answers with a RESET_STREAM, which is what actually ends the receiving half.
///
/// @param flow Stream flow
/// @param error Application error code for the peer
void netquicStopSending(_In_ NetFlow* flow, uint64 error);

// A snapshot of everything that decides whether this connection can send right now, appended to
// `out` as text: connection state, congestion control, every packet number space, connection flow
// control, and every live stream, ending in a line naming what is holding the connection back.
// For diagnosing a connection that has stopped making progress. Not part of the general API, and
// the format is not stable.
void netquicDebugState(_In_ NetSocket* sock, _Inout_ strhandle out);

/// @}

CX_C_END
