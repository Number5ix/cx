#pragma once
#include <cx/buffer/buffer.h>
#include <cx/buffer/bufchain.h>
#include <cx/buffer/bufring.h>
#include <cx/container/sarray.h>
#include <cx/stype/stype.h>
#include <cx/thread/prqueue.h>

typedef struct NetSocket NetSocket;
typedef struct NetQueue NetQueue;
typedef struct NetFlow NetFlow;

/// @addtogroup net_types
/// @{

/// Platform-neutral OS socket handle: a Windows `SOCKET` or a Unix file descriptor.
typedef intptr NetSockHandle;

/// An invalid socket handle. Matches both `INVALID_SOCKET` (Windows) and a negative fd (Unix).
#define NET_INVALID_HANDLE ((NetSockHandle)-1)

/// Flags for creating a NetQueue
typedef enum {
    NQ_None = 0x00,

    /// Force use of the select() API, even if more efficient backends are available
    NQ_SelectOnly = 0x01,

    /// @brief Automatically add accepted sockets to the queue
    ///
    /// Without this flag, the application must listen for NET_Accepted events and either add
    /// the socket to a queue, or use objAcquire() to hold onto a reference to the new socket
    /// and prevent it from being destroyed
    NQ_AutoAccept = 0x02
} NetQueueFlags;

/// Address-family preference for outbound connect, applied to the resolved address list after DNS
/// lookup. Most applications can leave this at NCP_Default; use the other values when a
/// deployment needs to force or exclude a family (an IPv4-only network, for example).
typedef enum {
    NCP_Default  = 0,   ///< Interleave; the first address the resolver returned leads
    NCP_PreferV4 = 1,   ///< Interleave, but IPv4 leads regardless of resolver order
    NCP_PreferV6 = 2,   ///< Interleave, but IPv6 leads regardless of resolver order
    NCP_V4Only   = 3,   ///< Discard every IPv6 result
    NCP_V6Only   = 4    ///< Discard every IPv4 result
} NetConnectPref;

/// Creation-time configuration for a NetQueue.
///
/// Start from a preset and override what you need:
///
/// @code
///   NetQueueConfig conf;
///   netqueuePresetServer(&conf);
///   conf.maxflows = 50000;
///   NetQueue* q = netqueueCreate(&conf);
/// @endcode
typedef struct NetQueueConfig {
    int32 nthreads;         ///< Number of worker threads, or 0 for polled mode
    flags_t flags;          ///< NetQueueFlags

    size_t recvBufSize;     ///< Size of each pooled receive buffer
    uint32 recvBufInitial;  ///< Buffers preallocated at queue creation
    uint32 recvBufMax;      ///< Hard cap on live buffers; the receive memory ceiling

    uint32 maxflows;        ///< Cap on concurrent flows across the queue (0 = unlimited)
    uint32 reclaimBatch;    ///< Flows to reclaim per cap hit
    bool noReclaim;         ///< Never reclaim, even at the cap

    /// @brief Minimum time a flow must have been idle before cap-pressure reclaim may evict it,
    /// in microseconds (0 = any flow is fair game)
    ///
    /// Protects live sessions from churn at the flow cap: when every flow has been active more
    /// recently than this, nothing is reclaimed and the new peer is refused through the
    /// NET_FlowRefused handler instead -- where the application can still admit it explicitly
    /// with netqueuePromoteFlow(). Worth setting when session setup is expensive enough that
    /// evicting a hot flow costs more than turning a new peer away.
    /// @note Idle time is tracked at a coarse ~1s granularity; nonzero values round up to at
    /// least one tick.
    int64 reclaimMinIdle;

    size_t sendHigh;        ///< Default send high watermark for sockets on this queue
    size_t sendLow;         ///< Default send low watermark for sockets on this queue

    /// @brief How long a single connect attempt may run before it's cancelled and the next
    /// resolved address is tried, in microseconds (0 = a sensible default)
    ///
    /// This is a per-address limit, not a total budget: a host that resolves to several dead
    /// addresses can still take this long on each one. The last remaining address of each family
    /// always gets the full timeout (see connectAttemptTimeout), so a family's one real chance is
    /// never cut short.
    int64 connectTimeout;

    /// @brief Shorter timeout for connect attempts other than the last remaining address of their
    /// family, in microseconds (0 = disabled -- every attempt gets the full connectTimeout)
    ///
    /// Addresses are tried interleaved by family (IPv4/IPv6), so this keeps a dead family from
    /// using up a full connectTimeout on every one of its addresses before the other family gets
    /// a turn. Each family's last remaining address always gets the full connectTimeout.
    int64 connectAttemptTimeout;

    /// @brief Default address-family preference for connects on this queue (see NetConnectPref)
    ///
    /// A socket inherits this when added to the queue, the same way sendHigh/sendLow are
    /// inherited; set NetSocket::connectPref to override it per socket.
    NetConnectPref connectPref;
} NetQueueConfig;

// Backend hook called when a send leaves data queued on a socket, so the backend can arrange
// for it to drain later. Not used by application code; each backend supplies its own.
typedef void (*NetSendPumpFn)(void* ctx, NetSocket* sock);

/// Flags for socket send/receive operations
typedef enum {
    NSO_None = 0x00,   ///< No special options

    /// For send operations, do not queue data if the socket is not currently writable.
    /// Instead, return immediately with the number of bytes actually sent (may be zero).
    /// Not valid if the socket has a filter attached.
    NSO_Immediate = 0x01,

    /// For receive operations, peek at the data without removing it from the socket buffer.
    NSO_Peek = 0x02
} NetSocketOpFlags;

/// Socket types
typedef enum {
    /// Stream socket (TCP)
    /// @note This socket type requires a connection
    NST_Stream = 1,

    /// Datagram socket (UDP)
    /// @note This socket type is connectionless
    NST_Datagram
} NetSocketType;

/// State of a network socket
typedef enum {
    NS_Init = 0,     ///< Socket is not yet used
    NS_Connected,    ///< Socket is connected (default for bound connectionless sockets)
    NS_Listening,    ///< Socket is listening for incoming connections
    NS_Connecting,   ///< Socket is in the process of connecting
    NS_Resolving,    ///< Waiting for name resolution
    NS_Closed        ///< Socket is closed and cannot be reused
} NetSocketState;

/// Why a flow was closed, carried on NET_FlowClosed.
///
/// Only NCR_Reclaimed can resurrect: it means the queue guessed the peer was gone, and an
/// arriving packet can prove that guess wrong. Every other reason is final.
typedef enum {
    NCR_None         = 0,   ///< Not closing
    NCR_Reclaimed    = 1,   ///< Reclaimed under flow cap pressure (resurrectable)
    NCR_AppClosed    = 2,   ///< Application called netflowClose()
    NCR_PeerClosed   = 3,   ///< Stream peer closed the connection cleanly
    NCR_Error        = 4,   ///< Connection reset or other fatal socket error
    NCR_SocketClosed = 5,   ///< The owning socket was closed with flows still live
    NCR_Shutdown     = 6    ///< The queue is shutting down
} NetCloseReason;

/// Network connection states (for event notification)
typedef enum {
    NCS_NotConnected = 0,   ///< Not connected
    NCS_Connecting   = 1,   ///< Connection in progress
    NCS_Connected    = 2,   ///< Connected
} NetConnectionState;

/// Segment size for a stream filter's boundary rings (encOut / decOut) and the flow's encode
/// staging ring. A small default; revisit if a real multi-stage chain profiles badly.
#define NET_FILTER_RING_SEGSZ 16384

/// Filter notification code, carried as the payload of a NET_FilterNotify event (see @ref
/// net_filter)
///
/// A filter raises one of these through netflowfilterNotify() when it reaches a milestone the
/// application should hear about. Confining filter signals to their own enum -- delivered on a
/// dedicated event with a dedicated payload arm -- means a filter can raise application-visible
/// notifications without being able to forge an ordinary NET_ event whose payload it could not
/// populate correctly.
typedef enum {
    NFN_None    = 0,   ///< No notification
    NFN_Secured = 1,   ///< The filter's secure channel is up; the application may send now

    /// @brief First code reserved for application-defined filter notifications
    ///
    /// Built-in notifications stay below this line; a custom filter numbers its own notifications
    /// at NFN_AppCustom and above, so they can never collide with codes the framework adds later.
    NFN_AppCustom = 1000
} NetFilterNotify;

/// Network error codes
typedef enum {
    NERR_None               = 0,    ///< No error
    NERR_Unknown            = 1,    ///< Unknown error
    NERR_ConnectionRefused  = 2,    ///< Connection was refused by the remote host
    NERR_Timeout            = 3,    ///< Connection timed out
    NERR_NetworkUnreachable = 4,    ///< Network is unreachable
    NERR_HostUnreachable    = 5,    ///< Host is unreachable
    NERR_NetworkDown        = 6,    ///< Network is down
    NERR_AddressInUse       = 7,    ///< Address already in use
    NERR_AlreadyConnected   = 8,    ///< Socket is already connected
    NERR_NotConnected       = 9,    ///< Socket is not connected
    NERR_ConnectionReset    = 10,   ///< Connection was reset by peer

    /// @brief The operation would have blocked; no data available yet
    ///
    /// Not a failure -- readiness backends report this often (for example, another thread may
    /// have already read the data). Just try again later.
    NERR_WouldBlock         = 11,

    /// @brief Interrupted by a signal before anything was transferred
    ///
    /// Unix only in practice; the caller should simply retry. Windows has no equivalent.
    NERR_Interrupted        = 12,
} NetErrorCode;

/// Network address types
typedef enum {
    NA_Unknown = 0,   ///< Unknown or invalid
    NA_IPv4    = 1,   ///< IPv4 address
    NA_IPv6    = 2    ///< IPv6 address
} NetAddrType;

/// Network address structure
///
/// Byte order differs by field:
/// - `port` and `scope` are host byte order, ordinary integers.
/// - `ipv4` is host order too: `ipv4[0]` is the least significant octet, so all four bytes can be
///   treated together as a `uint32`.
/// - `ipv6` is stored in network order exactly as it travels on the wire, `ipv6[0]` first -- IPv6
///   addresses are just a 16-byte identifier with no meaningful "host order".
typedef struct NetAddr {
    NetAddrType type;     ///< Address type (NA_IPv4, NA_IPv6)
    union {
        uint8 ipv4[4];    ///< IPv4 address
        uint8 ipv6[16];   ///< IPv6 address
    };
    uint32 scope;         ///< Scope ID (for IPv6)
    uint16 port;          ///< Port number
} NetAddr;
saDeclare(NetAddr);

// NetAddr is a full stype so it can be used as a hashtable key -- the datagram flow table is
// keyed on it. Comparison and hashing look only at the bytes that are actually meaningful for
// the address's type, rather than doing a plain memcmp of the whole struct, which would treat
// two identical IPv4 peers as different since part of the union is left unset.
stDeclare(NetAddr);
#define SType_NetAddr                         NetAddr*
#define STStorageType_NetAddr                 NetAddr
#define STypeArg_NetAddr(type, val)           stgeneric(opaque, &(val))
#define STypeArgPtr_NetAddr(type, val)        &stgeneric(opaque, (val))
#define STypeCheckedArg_NetAddr(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_NetAddr(type, val) stType(type), stArgPtr(type, val)

// Internally a NetMessage is any packet in flight: the library rides its own bookkeeping
// messages (NetMessageKind, net_private.h) through flow inboxes alongside data, but drainFlow()
// translates those into NetEvents and retires them before any handler runs, so an application
// never sees one -- hence the consumer-oriented documentation below.

/// @brief A received packet, as delivered to a handler
///
/// Arrives as NetEvent.recv.msg (a complete datagram), NetEvent.refused.msg (a packet from a
/// source with no flow), or through a netsocketRecvMsgs() callback. Read the payload from `buf`
/// and the peer's address from `addr`. The message itself is only valid until the handler
/// returns -- to keep the payload without copying it, take ownership of the buffer (see `buf`).
typedef struct NetMessage {
    struct NetMessage* next;   // internal: intrusive link for the flow inbox / NetMsgQueue

    /// @brief Message data (NULL for stream data, which is buffered in the socket's ring)
    ///
    /// For a delivered datagram, set to NULL to take ownership of the payload; otherwise it is
    /// returned to the pool when the handler returns.
    Buffer buf;

    // internal: how many bytes a stream receive appended to the socket's ring; overloaded as a
    // NetErrorCode by some internal message kinds
    size_t bytes;

    NetAddr addr;    ///< Source / destination address

    // internal: the accepted socket in transit to a NET_Accepted event
    NetSocket* asock;

    uint8 kind;      // internal: NetMessageKind (net_private.h)
    uint8 reason;    // internal: NetCloseReason, for a flow's terminal message
    uint8 flags;     // internal: NetMessageFlags (net_private.h) -- how `buf` must be reclaimed
} NetMessage;

// Type-specific socket buffers, selected by NetSocketType. Internal to the socket
// implementation -- application code does not touch this directly.
typedef struct NetSocketBufs {
    union {
        struct {
            BufRing recv;    // Inbound byte stream
            BufChain send;   // Outbound; owns app buffers with no copy on the way in
        } stream;
        struct {
            PrQueue send;   // NetMessage entries, each with its own destination address
        } dgram;
    };
} NetSocketBufs;

/// @brief Simple intrusive FIFO of NetMessage, linked through NetMessage::next
///
/// The boundary storage between datagram filter chain stages (see NetDatagramFilter::encOut /
/// decOut in @ref net_filter) -- the message-queue analog of the byte-oriented BufRing used
/// between NetStreamFilter stages. It needs no locking of its own: every driver pass runs under
/// the owning flow's filter lock, so only one thread is ever inside a chain.
typedef struct NetMsgQueue {
    NetMessage* head;   ///< Oldest queued message, or NULL if empty
    NetMessage* tail;   ///< Newest queued message, or NULL if empty
} NetMsgQueue;

/// bool netMsgQueueEmpty(NetMsgQueue *q);
///
/// Tests whether a message queue has nothing in it
///
/// @param q Queue to test
/// @return true if the queue holds no messages
#define netMsgQueueEmpty(q) ((q)->head == NULL)

/// Append a message to the end of a message queue
///
/// Takes ownership of the message. This is how a datagram filter stage emits output: push onto
/// `self->encOut` for the wire direction, `self->decOut` for the application direction.
///
/// @param q Queue to append to
/// @param msg Message to append (ownership transferred)
_meta_inline void netMsgQueuePush(_Inout_ NetMsgQueue* q, _Inout_ NetMessage* msg)
{
    msg->next = NULL;
    if (q->tail)
        q->tail->next = msg;
    else
        q->head = msg;
    q->tail = msg;
}

/// Remove and return the oldest message in a message queue
///
/// Ownership passes to the caller. A filter consuming its input queue pops from `src` in a loop
/// until this returns NULL, or stops early to leave the rest for a later pass.
///
/// @param q Queue to pop from
/// @return The oldest queued message, or NULL if the queue is empty
_Check_return_ _Ret_maybenull_ _meta_inline NetMessage* netMsgQueuePop(_Inout_ NetMsgQueue* q)
{
    NetMessage* msg = q->head;
    if (!msg)
        return NULL;

    q->head = msg->next;
    if (!q->head)
        q->tail = NULL;
    msg->next = NULL;
    return msg;
}

/// @}

/// @addtogroup net_handlers
/// @{
///
/// Handlers are a plain struct of function pointers, set with designated initializers:
///
/// @code
///   static const NetHandlers clientHandlers = {
///       .recv       = onRecv,
///       .flowClosed = onFlowClosed,
///   };
/// @endcode
///
/// Register the same struct at the flow, socket, or queue level. A NULL entry falls through to
/// the next level, resolved separately for each event type:
///
/// @code
///   flow handlers  ->  socket handlers  ->  queue-wide handlers
/// @endcode
///
/// The ctx registered alongside whichever level supplied the handler arrives on NetEvent.ctx, so
/// the queue-wide set is a good place for logging and error handling while sockets and flows
/// override only the events they care about.

/// @brief Network event types
///
/// Each type describes a different kind of event that can be delivered to a handler. The
/// event-specific data for each type lives in the matching arm of NetEvent's union.
typedef enum {
    /// @brief A connection attempt resolved
    ///
    /// Delivered when netsocketConnect() reaches a terminal state: established, or failed once
    /// every resolved address has been tried. NetEvent.conn.state carries the resulting state
    /// and NetEvent.conn.err the failure cause (NERR_None on success). On success this is
    /// always ordered ahead of any NET_DataReceived from the new connection.
    NET_Connection = 1,

    /// @brief A filter raised an out-of-band notification
    ///
    /// The channel a filter uses to tell the application about a milestone -- most commonly
    /// NFN_Secured, the "secure channel is up, you may send now" edge for a TLS/DTLS session.
    /// NetEvent.filter.notify carries which one (a NetFilterNotify: a built-in code or an
    /// application-defined one at NFN_AppCustom+). It is never inferred from filter state: the
    /// filter raises it itself with netflowfilterNotify(), and the data-plane driver delivers it -- so
    /// a non-security filter (compression, framing) never triggers it merely by establishing.
    /// NET_Connection still fires at the transport level when the socket connects; an NFN_Secured
    /// notification is the higher, secure-channel-ready edge layered on top of it, ordered ahead of
    /// the first NET_DataReceived carrying decoded application data.
    NET_FilterNotify,

    /// @brief A listening socket accepted an incoming connection
    ///
    /// The new socket arrives in NetEvent.accept.newSocket, already connected. It is only
    /// guaranteed to live until the handler returns -- call objAcquire() to keep it. Under
    /// NQ_AutoAccept the queue has already registered it and begun servicing receives;
    /// otherwise the handler adds it to a queue itself.
    NET_Accepted,

    /// @brief Data arrived on a socket
    ///
    /// For a datagram socket: one event per datagram, delivered whole as NetEvent.recv.msg.
    /// For a stream socket: bytes were appended to the socket's receive ring -- recv.msg is
    /// NULL, and the handler drains the ring with netsocketRecv() or netsocketRecvMsgs().
    /// NetEvent.recv.bytes is what this event delivered; NetEvent.recv.total is everything
    /// currently pending.
    NET_DataReceived,

    /// @brief The send backlog drained; sending can resume
    ///
    /// Fires as a real edge, only after a netsocketSend() was refused with more than sendHigh
    /// bytes already queued: once the backlog drains back below sendLow, this event says the
    /// socket is accepting data again.
    /// @note Currently delivered only for stream sockets.
    NET_SendReady,

    /// @brief An error surfaced asynchronously on the socket
    ///
    /// Reports a failure with no call left to return it from: data that was accepted into the
    /// send buffer failed when the backend later flushed it. NetEvent.error.err carries the
    /// cause. On a stream socket this is followed by NET_FlowClosed (NCR_Error), since bytes
    /// lost mid-stream break it; a datagram socket stays open, having dropped only the one
    /// datagram. Errors that happen synchronously are reported on the failing call's return
    /// instead, without this event.
    NET_Error,

    /// @brief A flow was created for a new peer
    ///
    /// Fires once for every datagram flow that comes into being, whether the queue auto-created
    /// it for an arriving packet or the application admitted the peer with
    /// netqueuePromoteFlow(). Delivered through the new flow's own queue, ordered ahead of its
    /// first NET_DataReceived, which makes it the natural place to set up session state on
    /// flow->user and register flow-level handlers with netflowSetHandlers(). Pairs with
    /// NET_FlowClosed: every open is eventually matched by exactly one close.
    /// @note Datagram sockets only. A stream socket's session start is NET_Connection or
    /// NET_Accepted; its single flow exists from socket creation. See @ref net_flow.
    NET_FlowOpen,

    /// @brief A packet arrived from a source with no flow, and none could be created
    ///
    /// The queue is at its flow cap with nothing reclaimable (see maxflows, noReclaim, and
    /// reclaimMinIdle in NetQueueConfig). Delivered with the raw packet in NetEvent.refused.msg,
    /// without allocating anything -- this is what keeps a spoofed-source flood from forcing
    /// unbounded allocation. The application validates the packet and calls
    /// netqueuePromoteFlow() to admit the peer, which may exceed the cap by one.
    /// @note Datagram sockets only, and unlike other events this one is delivered inline on the
    /// ingest thread, not a worker -- there is no flow to order it behind.
    NET_FlowRefused,

    /// @brief A flow is being torn down; release any state hanging off flow->user
    ///
    /// Delivered as a terminal event on the flow's own queue, so it is ordered after every
    /// packet already queued for that flow. Fires exactly once for every flow the application
    /// has seen, on every close cause; the cause is carried in NetEvent.closed.reason.
    NET_FlowClosed
} NetEventType;

/// Network Event Structure
typedef struct NetEvent {
    NetEventType event;   ///< Type of event
    NetQueue* queue;      ///< Originating NetQueue
    NetSocket* socket;    ///< Associated socket

    /// @brief Flow the event belongs to, or NULL for socket-wide events
    ///
    /// Stream sockets have exactly one flow, created during socket init; consumers that only
    /// ever handle a single connection can ignore this field entirely. Datagram sockets have
    /// one flow per peer address.
    NetFlow* flow;

    /// @brief The context pointer passed to netflowSetHandlers / netsocketSetHandlers /
    ///        netqueueSetHandlers when this handler was registered
    ///
    /// This is the right place for per-socket and per-queue application state; only flows carry
    /// a `user` array (see @ref net_flow_state).
    void* ctx;

    /// Event-specific data
    union {
        /// NET_Connection event data
        struct {
            NetConnectionState state;   ///< Current connection state
            NetErrorCode err;   ///< Error code if connection failed (NERR_None if successful)
        } conn;

        /// NET_Accepted
        struct {
            NetSocket* newSocket;   //< Socket for newly accepted connection
        } accept;

        /// NET_DataReceived
        struct {
            size_t bytes;   ///< Number of bytes received
            size_t total;   ///< Total bytes pending in receive buffer

            /// @brief Datagram: the packet itself. NULL for stream sockets.
            ///
            /// Set `msg->buf` to NULL to take ownership of the payload; otherwise it is returned
            /// to the pool when the handler returns. Stream sockets buffer into a ring instead --
            /// call netsocketRecv() or netsocketRecvMsgs() to drain it.
            struct NetMessage* msg;
        } recv;

        /// NET_Error
        struct {
            NetErrorCode err;   ///< Error code
        } error;

        /// NET_FilterNotify
        struct {
            NetFilterNotify notify;   ///< Which notification the filter raised (NFN_Secured, ...)
        } filter;

        /// NET_FlowRefused (NET_FlowOpen carries no event data; the flow itself is the payload)
        struct {
            struct NetMessage* msg;   ///< The packet that could not be assigned a flow
        } refused;

        /// NET_FlowClosed
        struct {
            NetCloseReason reason;   ///< Why the flow is being torn down
        } closed;
    };
} NetEvent;

/// The callback type for a network event handler. The handler is called on the thread that
/// generated the event, which is usually the queue's worker thread. The handler must not block
/// or perform long-running work, since that would stall the queue's dispatch loop.
typedef void (*NetEventCB)(_In_ NetEvent* event);

/// Set of event handlers, registered per flow, per socket, or queue-wide
typedef struct NetHandlers {
    NetEventCB connection;   ///< NET_Connection: established, failed, or state changed
    NetEventCB filterNotify; ///< NET_FilterNotify: a filter raised a notification (NFN_Secured, ...)
    NetEventCB accepted;     ///< NET_Accepted: new incoming connection
    NetEventCB recv;         ///< NET_DataReceived: stream data or a complete datagram
    NetEventCB sendReady;    ///< NET_SendReady: send buffer drained below the low watermark
    NetEventCB flowOpen;     ///< NET_FlowOpen: a flow was created; set up state on flow->user
    NetEventCB flowRefused;  ///< NET_FlowRefused: packet from an unknown source, no flow made
    NetEventCB flowClosed;   ///< NET_FlowClosed: release state hanging off flow->user
    NetEventCB error;        ///< NET_Error: a queued send failed asynchronously
} NetHandlers;

/// @}  // end of net_handlers group
