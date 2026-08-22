#pragma once

#include <cx/net.h>

#include <cxtls/tls_shared.h>
#include <cxtls/tlscert.h>
#include <cxtls/tlsconfig.h>
#include <cxtls/tlsfilter.h>

/// @file cxtls.h
/// @brief TLS for cx: certificate handling and NetQueue filters over mbedTLS

/// @defgroup tls TLS
/// @{
/// Transport security for the netqueue socket layer.
///
/// cxtls is not a wrapper around mbedTLS. mbedTLS is linked into the same static library and its
/// headers are on the include path, so anything cxtls does not cover is a direct call away. What
/// cxtls provides is the part that cannot be a direct call: fitting a TLS session into the
/// @ref net_filter chain, so that a secured socket is used exactly like an unsecured one.
///
/// @defgroup tls_overview Overview
/// @ingroup tls
/// @{
///
/// @section tls_model Mental model
///
/// Three objects, in order of how long they live:
///
/// - **Certificate material** -- TlsCAStore (whom to trust) and TlsCreds (who you are). Loaded once
///   at startup, shared by everything.
/// - **A TlsConfig** -- the policy built from that material: verification mode, protocol versions,
///   ALPN, resumption. Built once, then *sealed*, because mbedTLS reads it from every live
///   handshake without locking it. Sealing is what makes it the unit of certificate rotation.
/// - **A filter** -- TlsClientFilter on an outbound socket, TlsServerFilter on a listener. The
///   filter builds one session per flow, and that session is where the encryption happens.
///
/// Nothing about sending or receiving changes. netsocketSend() takes plaintext, netsocketRecv()
/// returns plaintext, and the same NetHandlers set delivers the same events. See @ref tls_filter
/// for the client, server, and STARTTLS shapes.
///
/// @section tls_ready Knowing when the channel is up
///
/// A connected socket is not yet a secured one: NET_Connection fires when TCP is up, while the
/// handshake still has a round trip or two to go. The edge that matters is **NFN_Secured**,
/// delivered as a NET_FilterNotify event through NetHandlers::filterNotify, and guaranteed to
/// arrive ahead of the first NET_DataReceived carrying decoded application data.
///
/// Sending before that is allowed and does the right thing -- the payload waits in the flow's
/// staging ring and goes out encrypted once the handshake finishes -- but the peer's identity is
/// not established until NFN_Secured, so anything that depends on *who* the peer is belongs after
/// it.
///
/// @code
///   static void onNotify(NetEvent *ev)
///   {
///       if (ev->filter.notify != NFN_Secured)
///           return;
///
///       TlsInfo info;
///       if (nettlsFlowInfo(ev->flow, &info)) {
///           logFmt(Info, _SL("secured with ${string} / ${string}, peer ${string}"),
///                  stvar(string, info.version), stvar(string, info.ciphersuite),
///                  stvar(string, info.peerSubject));
///           nettlsInfoDestroy(&info);
///       }
///   }
/// @endcode
///
/// @section tls_failure How failure surfaces
///
/// A handshake that cannot complete -- an untrusted certificate, a name mismatch, no shared
/// ciphersuite -- is a fatal error for the connection, and arrives as NET_FlowClosed with
/// NCR_Error. The reason is logged on the @ref TlsLogChannel; route that channel somewhere visible
/// while bringing a deployment up.
///
/// A filter that cannot be constructed **fails closed**. If a configuration is unusable, the stage
/// still joins the chain and tears the flow down on its first pass, rather than dropping out and
/// leaving the socket running in the clear.
///
/// @}
///
/// @}  // end of tls group

/// @defgroup tls_types Types
/// @ingroup tls
/// Plain-C types shared across the TLS API: verification policy, protocol versions, the session
/// snapshot, and the callback signatures. Declared in cxtls/tls_shared.h.

/// @defgroup tls_cert Certificates
/// @ingroup tls

/// @defgroup tls_config Configuration
/// @ingroup tls

/// @defgroup tls_filter Filters
/// @ingroup tls

/// @defgroup tls_misc NetQueue Integration
/// @ingroup tls
/// Reading a session's state through the flow that owns it, and the one-call helpers that set a
/// filtered socket up.

/// @addtogroup tls_misc
/// @{

/// Callback invoked with a flow's live mbedTLS session context
///
/// See nettlsFlowWithSsl(). Runs with the flow's filter lock held: read what you need and return.
/// Do not call back into the socket, the flow, or anything that could close either.
///
/// @param ssl The session's `mbedtls_ssl_context *` (cast it after including <cxtls_mbed.h>)
/// @param ctx Context passed to nettlsFlowWithSsl()
typedef void (*TlsSslCB)(_In_ void* ssl, _In_opt_ void* ctx);

/// Read a flow's TLS state
///
/// Copies the snapshot the session took when its handshake completed. Safe to call from any
/// handler on any thread, and safe to call on a flow that has no TLS filter or has not finished
/// its handshake -- both simply return false.
///
/// Every string in the result is owned by the caller; release them all with nettlsInfoDestroy().
///
/// @param flow Flow to inspect, typically NetEvent::flow
/// @param out Receives the snapshot; untouched if this returns false
/// @return true if the flow has a TLS session whose handshake has completed
///
/// Example:
/// @code
///   TlsInfo info;
///   if (nettlsFlowInfo(ev->flow, &info)) {
///       if (info.verifyFlags != 0)
///           logStr(Warn, _SL("peer certificate did not verify cleanly"));
///       nettlsInfoDestroy(&info);
///   }
/// @endcode
_Success_(return) bool nettlsFlowInfo(_In_ NetFlow* flow, _Out_ TlsInfo* out);

/// Release the strings in a TlsInfo
///
/// @param info Snapshot filled in by nettlsFlowInfo()
void nettlsInfoDestroy(_Inout_ TlsInfo* info);

/// Run a callback against a flow's live mbedTLS session context
///
/// The escape hatch for anything TlsInfo does not carry. The callback runs under the flow's filter
/// lock, which is the only condition under which an `mbedtls_ssl_context` is safe to touch -- it is
/// not thread-safe, and the encode and decode paths reach it from different threads.
///
/// Prefer nettlsFlowInfo() where it suffices: it copies out, so it imposes none of these rules on
/// the caller.
///
/// @param flow Flow to inspect
/// @param cb Callback invoked with the session context
/// @param ctx Context passed to the callback
/// @return true if the flow has a TLS session and the callback ran
_Success_(return) bool nettlsFlowWithSsl(_In_ NetFlow* flow, _In_ TlsSslCB cb, _In_opt_ void* ctx);

/// @}

/// @addtogroup tls_filter
/// @{

/// Open a TLS connection in one call
///
/// Creates a stream socket, registers it with the queue and its handlers, attaches a client filter,
/// and starts connecting -- in that order, so no byte can move before the chain exists. Equivalent
/// to netqueueConnect() with the filter step inserted.
///
/// Like netqueueConnect(), this returns as soon as the attempt has started; the outcome arrives as
/// NET_Connection, and the secure channel as NFN_Secured after it.
///
/// `hostname` is the name the server's certificate must carry, and is normally the same as `host`
/// -- pass NULL and it is. They differ when the address dialed is not the name being
/// authenticated: connecting through a proxy or forwarded port, or to a literal address for a
/// service whose certificate names it something else.
///
/// @param q Queue to run the connection on
/// @param host Hostname or literal address to connect to
/// @param port Port number, host byte order
/// @param hostname Name to send as SNI and require the certificate to match, or NULL to use `host`
/// @param config Client configuration
/// @param handlers Handler set for the socket, or NULL
/// @param ctx Context passed to those handlers
/// @return The new socket, or NULL if it could not be created, filtered, or started
///
/// Example:
/// @code
///   NetSocket *s = nettlsConnect(q, _S"example.com", 443, NULL, cfg, &handlers, ctx);
/// @endcode
_Ret_maybenull_ NetSocket*
nettlsConnect(_In_ NetQueue* q, _In_opt_ strref host, uint16 port, _In_opt_ strref hostname,
              _In_ TlsConfig* config, _In_opt_ const NetHandlers* handlers, _In_opt_ void* ctx);

/// Listen for TLS connections in one call
///
/// Creates a listening socket, registers it with the queue and its handlers, attaches a server
/// filter, and begins listening. Accepted connections inherit the listener's filter before they
/// become reachable, so every one of them is secured from its first byte.
///
/// Equivalent to netqueueListen() with the filter step inserted. Hold onto the returned
/// TlsServerFilter (via netsocketAddFilter's own reference, or by building one yourself) if you
/// intend to rotate certificates; otherwise this form is all that is needed.
///
/// @param q Queue to run the listener on
/// @param addr Local address and port to bind
/// @param backlog Listen backlog, or 0 for a platform default
/// @param config Server configuration
/// @param handlers Handler set for the listener and its connections, or NULL
/// @param ctx Context passed to those handlers
/// @return The listening socket, or NULL if it could not be created, filtered, bound, or listened
_Ret_maybenull_ NetSocket*
nettlsListen(_In_ NetQueue* q, _In_ NetAddr* addr, int backlog, _In_ TlsConfig* config,
             _In_opt_ const NetHandlers* handlers, _In_opt_ void* ctx);

/// @}
