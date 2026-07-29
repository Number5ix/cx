#pragma once
#include <cx/buffer/buffer.h>
#include <cx/container/sarray.h>

typedef struct NetSocket NetSocket;
typedef struct NetQueue NetQueue;

/// @addtogroup net_types
/// @{

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

/// Flags for socket send/receive operations
typedef enum {
    NSO_None = 0x00,   ///< No special options

    /// For send operations, do not queue data if the socket is not currently writable.
    /// Instead, return immediately with the number of bytes actually sent (may be zero).
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
    NS_Closed = 0,   ///< Socket is closed
    NS_Connected,    ///< Socket is connected (default for bound connectionless sockets)
    NS_Listening,    ///< Socket is listening for incoming connections
    NS_Connecting,   ///< Socket is in the process of connecting
    NS_Resolving     ///< Waiting for name resolution
} NetSocketState;

/// Network event types
/// These are defined as a bitmask to allow combination of multiple events when registering /
/// unregistering for events.
typedef enum {
    NET_Connection   = 0x01,   ///< Connection state changed (or connection attempt failed)
    NET_Accepted     = 0x02,   ///< Incoming connection accepted
    NET_DataReceived = 0x04,   ///< Data received on socket
    NET_DataSent     = 0x08,   ///< Data sent on socket
    NET_SendReady    = 0x10,   ///< Socket is ready to send more data without queuing
    NET_Error        = 0x20    ///< An error occurred on the socket
} NetEventType;

/// Network connection states (for event notification)
typedef enum {
    NCS_NotConnected = 0,   ///< Not connected
    NCS_Connecting   = 1,   ///< Connection in progress
    NCS_Connected    = 2,   ///< Connected
} NetConnectionState;

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
} NetErrorCode;

/// Network Event Structure
typedef struct NetEvent {
    NetEventType event;   ///< Type of event
    NetQueue* queue;      ///< Originating NetQueue
    NetSocket* socket;    ///< Associated socket

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
        } recv;

        /// NET_DataSent
        struct {
            size_t bytes;   ///< Number of bytes sent
            size_t total;   ///< Total bytes pending in send buffer
        } sent;

        /// NET_Error
        struct {
            NetErrorCode err;   ///< Error code
        } error;
    };
} NetEvent;

typedef void (*NetEventCB)(_In_ NetEvent* event);

/// Network address types
typedef enum {
    NA_Unknown = 0,   ///< Unknown or invalid
    NA_IPv4    = 1,   ///< IPv4 address
    NA_IPv6    = 2    ///< IPv6 address
} NetAddrType;

/// Network address structure
/// All fields are in host byte order
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

/// Network message for buffered datagrams
typedef struct NetMessage {
    buffer buf;     ///< Message data
    NetAddr addr;   ///< Source / destination address
} NetMessage;

/// @}
