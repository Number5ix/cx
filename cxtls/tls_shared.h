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
