#pragma once

#include <cx/cx.h>

/// @file quic_shared.h
/// @brief QUIC constants, error codes, and connection IDs

CX_C_BEGIN

/// @defgroup quic_shared Shared QUIC Types
/// @ingroup quic
/// @{

/// QUIC version 1, as defined by RFC 9000.
#define QUIC_VERSION_1          0x00000001u

/// Version number reserved for Version Negotiation packets.
///
/// A packet carrying this version is not a real packet of any version; it is a server
/// telling a client which versions it does support.
#define QUIC_VERSION_NEGOTIATE  0x00000000u

/// Largest connection ID QUIC version 1 allows, in bytes.
#define QUIC_MAX_CID            20

/// Smallest datagram an endpoint must be able to receive, in bytes.
///
/// Clients pad datagrams containing Initial packets out to at least this size so a server
/// can be sure it has room to reply.
#define QUIC_MIN_INITIAL_LEN    1200

/// Length of an AEAD authentication tag, in bytes.
///
/// The same for all cipher suites QUIC version 1 permits.
#define QUIC_TAG_LEN            16

/// Length of a stateless reset token, in bytes.
#define QUIC_RESET_TOKEN_LEN    16

/// Length of the data carried by PATH_CHALLENGE and PATH_RESPONSE frames, in bytes.
#define QUIC_PATH_DATA_LEN      8

/// Largest packet number QUIC allows, plus one.
///
/// Packet numbers are 62-bit values; a connection that would exceed this must be closed.
#define QUIC_MAX_PACKET_NUMBER  (UINT64_C(1) << 62)

/// Transport error codes reported by the peer in a CONNECTION_CLOSE frame
typedef enum QuicTransportError {
    QUIC_ERR_NO_ERROR                  = 0x00,   ///< Connection closed normally
    QUIC_ERR_INTERNAL_ERROR            = 0x01,   ///< The peer hit an internal fault
    QUIC_ERR_CONNECTION_REFUSED        = 0x02,   ///< The server declined to accept the connection
    QUIC_ERR_FLOW_CONTROL_ERROR        = 0x03,   ///< More data was sent than flow control allowed
    QUIC_ERR_STREAM_LIMIT_ERROR        = 0x04,   ///< More streams were opened than were allowed
    QUIC_ERR_STREAM_STATE_ERROR        = 0x05,   ///< A frame arrived for a stream in the wrong state
    QUIC_ERR_FINAL_SIZE_ERROR          = 0x06,   ///< A stream's final size changed after it was set
    QUIC_ERR_FRAME_ENCODING_ERROR      = 0x07,   ///< A frame could not be decoded
    QUIC_ERR_TRANSPORT_PARAMETER_ERROR = 0x08,   ///< The transport parameters were invalid
    QUIC_ERR_CONNECTION_ID_LIMIT_ERROR = 0x09,   ///< More connection IDs were issued than were allowed
    QUIC_ERR_PROTOCOL_VIOLATION        = 0x0a,   ///< A protocol rule with no more specific code was broken
    QUIC_ERR_INVALID_TOKEN             = 0x0b,   ///< A Retry or NEW_TOKEN token did not validate
    QUIC_ERR_APPLICATION_ERROR         = 0x0c,   ///< The application layer closed the connection
    QUIC_ERR_CRYPTO_BUFFER_EXCEEDED    = 0x0d,   ///< More handshake data arrived than could be buffered
    QUIC_ERR_KEY_UPDATE_ERROR          = 0x0e,   ///< A key update was attempted incorrectly
    QUIC_ERR_AEAD_LIMIT_REACHED        = 0x0f,   ///< The safe usage limit for the cipher was reached
    QUIC_ERR_NO_VIABLE_PATH            = 0x10,   ///< No network path to the peer could be validated
    QUIC_ERR_CRYPTO_BASE               = 0x100,  ///< TLS alerts are reported as this plus the alert number
} QuicTransportError;

/// A QUIC connection ID
///
/// Connection IDs, not addresses, are what identify a QUIC connection, which is how a
/// connection survives the peer's address changing. Version 1 allows 0 to
/// #QUIC_MAX_CID bytes; a zero-length ID is legal and means the endpoint does not need
/// one to route packets.
typedef struct QuicCid {
    uint8 len;                  ///< Length of the ID in bytes, 0 to #QUIC_MAX_CID
    uint8 id[QUIC_MAX_CID];     ///< The ID itself; bytes past `len` are unused
} QuicCid;

/// @}

CX_C_END
