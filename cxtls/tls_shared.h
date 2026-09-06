#pragma once
#include <cx/cx.h>
#include <cx/log/log.h>
#include <cx/string.h>

/// @file tls_shared.h
/// @brief Plain-C types shared by the cxtls classes and their consumers

CX_C_BEGIN

/// @addtogroup tls_types
/// @{

// Opaque state carried by the cxtls classes. Each one wraps the mbedTLS objects for its owner,
// and is defined in tls_private.h -- keeping mbedtls/*.h out of the generated public headers, so
// including <cxtls.h> does not drag the whole library in. Consumers that want the underlying
// handles include <cxtls_mbed.h> and ask for them explicitly.
typedef struct TlsCertChain TlsCertChain;
typedef struct TlsCredsState TlsCredsState;
typedef struct TlsConfigState TlsConfigState;
typedef struct TlsSession TlsSession;
typedef struct TlsInfoState TlsInfoState;
typedef struct TlsQuicState TlsQuicState;

/// How hard the peer's certificate is checked
///
/// Applies to whichever side is doing the checking: a client validating the server, or a server
/// validating a client certificate for mutual TLS.
typedef enum {
    /// @brief Do not request or check a peer certificate at all
    ///
    /// The channel is encrypted but the peer is unauthenticated, so it is no protection against an
    /// active attacker. Only appropriate when something outside TLS establishes who the peer is.
    TLSAUTH_None = 0,

    /// @brief Check the certificate, but continue the handshake whichever way it goes
    ///
    /// The handshake succeeds regardless; the result lands in TlsInfo::verifyFlags for the
    /// application to judge. This is how a server asks for a client certificate without requiring
    /// one, and how a client that wants to make its own trust decision gets the answer.
    TLSAUTH_Optional = 1,

    /// @brief Check the certificate and fail the handshake if it does not verify
    ///
    /// The default for a client, and the right setting for a server doing mutual TLS.
    TLSAUTH_Required = 2
} TlsAuthMode;

/// Protocol version, for the floor and ceiling set with tlsconfigSetVersions()
typedef enum {
    TLSVER_Default = 0,   ///< Leave mbedTLS's own default in place
    TLSVER_1_2     = 2,   ///< TLS 1.2
    TLSVER_1_3     = 3    ///< TLS 1.3
} TlsVersion;

/// @brief A snapshot of a session's TLS state, taken when its handshake completed
///
/// Filled in by nettlsFlowInfo(). Every string is owned by the caller; release the whole struct
/// with nettlsInfoDestroy() when done. Nothing in here points at mbedTLS-owned memory, so it stays
/// valid after the flow it came from is gone.
typedef struct TlsInfo {
    /// @brief The handshake completed and application data is flowing
    ///
    /// False means the snapshot was taken before the channel came up; every other field is unset.
    bool secured;

    /// @brief A peer certificate was presented and checked during this handshake
    ///
    /// False in the two cases where nothing was checked: the authentication mode was TLSAUTH_None,
    /// or the session was **resumed**, which reuses the identity established by the original
    /// handshake rather than sending a certificate again. Neither is a failure, and neither means
    /// the peer is unauthenticated -- a resumed session is as authenticated as the one it resumes.
    bool peerVerified;

    /// @brief Certificate verification result, 0 for a clean verify
    ///
    /// The bitmask mbedtls_ssl_get_verify_result() returns (MBEDTLS_X509_BADCERT_*), and 0 when
    /// no verification was performed at all -- mbedTLS's "not checked" sentinel is normalized away
    /// so that `verifyFlags != 0` always means something went wrong and never merely that there
    /// was nothing to check. Use `peerVerified` to tell those two apart.
    ///
    /// Only ever nonzero under TLSAUTH_Optional: TLSAUTH_Required fails the handshake instead, so
    /// the session never reaches a state where a snapshot could report it.
    uint32 verifyFlags;

    string version;       ///< Negotiated protocol, e.g. "TLSv1.3"
    string ciphersuite;   ///< Negotiated ciphersuite name
    string alpn;          ///< Negotiated ALPN protocol, or empty if none was
    string peerSubject;   ///< Peer certificate subject DN, or empty if there was no peer cert
    string peerIssuer;    ///< Peer certificate issuer DN, or empty if there was no peer cert
} TlsInfo;

/// Callback invoked for each certificate in the peer's chain, innermost last
///
/// Registered with tlsconfigSetVerifyCallback(), and bridged to mbedtls_ssl_conf_verify(). The
/// callback may clear bits in `*flags` to forgive a defect, or set bits to reject a chain mbedTLS
/// would have accepted. Runs on whichever thread is driving the handshake, under the flow's filter
/// lock, so it must not call back into the socket.
///
/// @param crt Certificate being checked, as an `mbedtls_x509_crt *` (cast it after including
///            <cxtls_mbed.h>)
/// @param depth Position in the chain: 0 is the peer's own certificate, higher is closer to the
/// root
/// @param flags In/out verification flags for this certificate (MBEDTLS_X509_BADCERT_*)
/// @param ctx Context registered alongside the callback
/// @return true to continue verification, false to abort the handshake outright
typedef bool (*TlsVerifyCB)(_In_ void* crt, int32 depth, _Inout_ uint32* flags, _In_opt_ void* ctx);

/// Callback invoked on a server when a client sends an SNI hostname
///
/// Registered with tlsconfigSetSNICallback(), and bridged to mbedtls_ssl_conf_sni(). Return the
/// credentials to present for `hostname`, or NULL to fall back to the config's own. The returned
/// reference is borrowed -- the callback keeps ownership, and whatever it returns must stay alive
/// for the life of the session, which is why the usual implementation hands back a TlsCreds it is
/// holding in a table rather than one it built on the spot.
///
/// @param hostname Name the client asked for
/// @param ctx Context registered alongside the callback
/// @return Credentials to use, or NULL for the config's default
typedef struct TlsCreds* (*TlsSNICB)(_In_ strref hostname, _In_opt_ void* ctx);

/// Encryption level a QUIC handshake message or traffic secret belongs to
///
/// QUIC carries TLS handshake messages in CRYPTO frames rather than TLS records, and each level
/// has its own packet protection keys and its own CRYPTO stream. RFC 9001 calls these encryption
/// levels; they run in the order listed.
typedef enum {
    /// @brief Client and server hellos
    ///
    /// Packets at this level are protected with keys both ends derive from the connection ID
    /// rather than from the handshake, so TlsQuic never reports secrets for it.
    TLSQL_Initial = 0,

    /// @brief Client 0-RTT data, protected by a resumed session's early secret
    TLSQL_EarlyData = 1,

    /// @brief The rest of the handshake, from EncryptedExtensions to Finished
    TLSQL_Handshake = 2,

    /// @brief Application data, and post-handshake messages such as session tickets
    TLSQL_App = 3
} TlsQuicLevel;

/// @brief Callbacks a TlsQuic engine drives its transport through
///
/// The engine never touches the network. Everything it needs to send, and every key it derives,
/// leaves through this table, which is registered with tlsquicSetHandlers() and must outlive the
/// engine. Every callback runs on whichever thread called into the engine.
///
/// Returning false from any of the `bool` callbacks fails the handshake.
typedef struct TlsQuicHandlers {
    /// @brief Handshake bytes to send at `level`
    ///
    /// Append them to that level's CRYPTO stream. The bytes belong to the engine and are only
    /// valid for the duration of the call, so copy whatever is not sent immediately.
    bool (*sendCrypto)(_In_opt_ void* ctx, TlsQuicLevel level,
                       _In_reads_bytes_(len) const uint8* data, size_t len);

    /// @brief Traffic secrets for `level` are now available
    ///
    /// Either secret may be NULL, meaning that direction is not ready yet; both are `len` bytes.
    /// Expand them into packet protection keys with the AEAD and hash of `suite`, which is a TLS
    /// 1.3 cipher suite codepoint.
    ///
    /// TLSQL_EarlyData only ever carries one direction, because 0-RTT only runs one way: a client
    /// is given the write secret and a server the read secret.
    bool (*secrets)(_In_opt_ void* ctx, TlsQuicLevel level, uint16 suite,
                    _In_reads_bytes_opt_(len) const uint8* readSecret,
                    _In_reads_bytes_opt_(len) const uint8* writeSecret, size_t len);

    /// @brief The transport parameters the session being resumed ran under
    ///
    /// Called only when 0-RTT is being considered, and always before any early data could be
    /// produced. A client is handed what the server sent it last time, to use as the peer's limits
    /// until the real ones arrive. A server is handed what it sent last time, to compare against
    /// what it is about to send now -- returning false refuses the early data, which is how a
    /// server whose limits have shrunk stops a client sending under the old ones.
    bool (*earlyParams)(_In_opt_ void* ctx,
                        _In_reads_bytes_(len) const uint8* data, size_t len);

    /// @brief Whether 0-RTT is being used, decided once per handshake
    ///
    /// A client that offered early data is told what the server said; a server that was offered it
    /// is told what it decided. False means nothing may be sent or read at TLSQL_EarlyData, and a
    /// client must send everything it already sent that way again once the handshake finishes.
    void (*earlyData)(_In_opt_ void* ctx, bool accepted);

    /// @brief The peer's quic_transport_parameters extension
    ///
    /// The body only, with no extension header. Valid for the duration of the call.
    bool (*transportParams)(_In_opt_ void* ctx,
                            _In_reads_bytes_(len) const uint8* data, size_t len);

    /// @brief The handshake finished successfully
    void (*complete)(_In_opt_ void* ctx);

    /// @brief The handshake failed and this alert describes why
    ///
    /// Send it to the peer in a CONNECTION_CLOSE frame with error code 0x0100 plus the alert.
    void (*alert)(_In_opt_ void* ctx, uint8 alert);
} TlsQuicHandlers;
/// @}

/// @addtogroup tls_misc
/// @{

/// Log channel for the TLS module
///
/// Everything cxtls logs -- handshake failures, certificate parse errors, fatal alerts -- goes
/// through this channel. Log channels are filtered by pointer identity, so this is public
/// precisely so applications can pass it to logRegisterDest() to route or suppress TLS
/// diagnostics as a group. NULL until the first cxtls object is created.
extern LogChannel* TlsLogChannel;

/// @}

CX_C_END
