#pragma once

#include <cx/net.h>
#include <cxtls.h>

#include <cxquic/quic_shared.h>
#include <cxquic/quicsocket.h>
#include <cxquic/quicnet.h>

/// @file cxquic.h
/// @brief QUIC (RFC 9000) transport for cx

/// @defgroup quic QUIC
/// @{
/// QUIC transport over the netqueue socket layer.
///
/// A QUIC connection is a NetSocket and a QUIC stream is a NetFlow, so a connection is used
/// through the same handlers, the same events and the same send call as a TCP connection. What
/// QUIC adds -- many independent streams on one connection, a handshake that is part of the
/// transport, and an identity that survives the peer changing address -- arrives without a second
/// API to learn.
///
/// @defgroup quic_net Sockets and streams
/// @ingroup quic
/// @{
/// @}

/// @}
