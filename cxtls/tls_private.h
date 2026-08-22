#pragma once

#include <cxtls/tls_shared.h>

#include <cx/buffer/bufring.h>
#include <cx/container/hashtable.h>
#include <cx/log.h>
#include <cx/string.h>
#include <cx/thread/mutex.h>

#include <mbedtls/error.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_cache.h>
#include <mbedtls/ssl_ticket.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

CX_C_BEGIN

// One-time process init: creates TlsLogChannel and calls psa_crypto_init(), which 4.x requires
// before any TLS or X.509 call. Every factory in cxtls calls this first and refuses to build
// anything if it failed, so no object can exist over an uninitialized crypto core.
bool _tlsInit(void);

// Describe an mbedTLS or PSA status as text, for a log line: the numeric code plus whatever
// mbedtls_strerror() knows about it. 4.x returns MBEDTLS_ERR_* and PSA_ERROR_* from the same
// functions, so both are handled here rather than at every call site.
void _tlsErrDesc(_Inout_ strhandle out, int err);

// Log one mbedTLS failure. `level` is a bare log level (Warn, Error, ...) and `op` a _S-prefixed
// literal naming the call that failed.
#define tlsLogErr(level, op, errval)               \
    do {                                           \
        string _tlsed = 0;                         \
        _tlsErrDesc(&_tlsed, (int)(errval));       \
        logFmt(level,                              \
               _SL("${string} failed: ${string}"), \
               stvar(strref, (op)),                \
               stvar(string, _tlsed));             \
        strDestroy(&_tlsed);                       \
    } while (0)

// ---------------------------------------------------------------------------------------------
// Opaque state behind the class members declared in the .cxh files
// ---------------------------------------------------------------------------------------------

// A parsed certificate chain. Thin by design: it exists so TlsCAStore can name an mbedTLS type
// without publishing it.
typedef struct TlsCertChain {
    mbedtls_x509_crt crt;
} TlsCertChain;

// An identity: a leaf certificate plus the chain up to (but not including) a root, and the private
// key that goes with the leaf.
typedef struct TlsCredsState {
    mbedtls_x509_crt cert;
    mbedtls_pk_context key;
} TlsCredsState;

// Everything a TlsConfig owns. `conf` is mutated only before seal(); after that it is read
// concurrently by every session set up from it and must never be touched again.
typedef struct TlsConfigState {
    mbedtls_ssl_config conf;

    bool sealed;   // seal() has run; the config is now immutable and in use
    bool server;   // endpoint role, fixed at creation

    // Policy recorded by the setters and applied in seal(). Held rather than pushed straight into
    // `conf` so that the whole configuration lands in one place, in a known order -- the CA chain
    // and own certificate in particular have to be installed after config_defaults().
    TlsAuthMode authMode;
    TlsVersion minVer;
    TlsVersion maxVer;

    // ALPN wants a NULL-terminated array of C strings that outlives the config, so the protocol
    // list is copied here rather than borrowed from the caller's sarray.
    char** alpn;
    int32 alpnCount;

    bool resume;
    int64 resumeLifetime;

    // Server-side resumption. Both mbedTLS contexts mutate themselves at runtime (ticket key
    // rotation, cache eviction) and are internally locked through MBEDTLS_THREADING_C, which cx
    // enables -- so unlike `conf` they are safe to leave live across concurrent sessions.
    mbedtls_ssl_ticket_context ticket;
    mbedtls_ssl_cache_context cache;
    bool ticketInit;
    bool cacheInit;

    // Client-side resumption: hostname -> a heap mbedtls_ssl_session handed out by that server.
    // Sessions from different flows land here concurrently, and mbedTLS offers no lock for it, so
    // this one is guarded here. Capped at TLS_SESSION_CACHE_MAX so a client that talks to many
    // hosts cannot grow it without bound.
    hashtable sessions;
    Mutex sessionLock;

    TlsVerifyCB verifyCb;
    void* verifyCtx;
    TlsSNICB sniCb;
    void* sniCtx;
} TlsConfigState;

// Hosts kept in the client resumption cache. Small on purpose: resumption is worth having for the
// handful of endpoints a client talks to repeatedly, and an unbounded cache of session secrets is
// a liability rather than a feature.
#define TLS_SESSION_CACHE_MAX 64

// The per-connection session. Owned by a TlsStreamFilter, and only ever entered by one thread at a
// time: the flow holds its filterLock across every driver pass, which is what makes it safe to
// keep a bare mbedtls_ssl_context here.
typedef struct TlsSession {
    mbedtls_ssl_context ssl;

    // The ring the BIO recv callback draws from -- the `src` of the decode pass currently running,
    // or NULL. Leaving it NULL is how a handshake driven from the encode side is made to stall on
    // WANT_READ after emitting its flight instead of reading bytes that are not there.
    BufRing* in;

    // Where the BIO send callback writes: the owning stage's encOut. Held as a plain pointer
    // because the session's lifetime is strictly inside the stage's.
    BufRing* out;

    bool setup;   // mbedtls_ssl_setup() succeeded; the context is usable
} TlsSession;

// The handshake-completion snapshot behind TlsStreamFilter::info. A TlsInfo by value, with its
// strings owned here until nettlsFlowInfo() duplicates them for a caller.
typedef struct TlsInfoState {
    TlsInfo info;
} TlsInfoState;

// ---------------------------------------------------------------------------------------------
// Cross-file internals
// ---------------------------------------------------------------------------------------------

// The mbedtls_ssl_config a sealed TlsConfig owns, or NULL if it could not be sealed. Used by the
// filter to set up a session, and exported through <cxtls_mbed.h> for consumers.
struct TlsConfig;
mbedtls_ssl_config* _tlsconfigConf(_In_ struct TlsConfig* self);

// Seal-on-first-use, called by the filter before mbedtls_ssl_setup().
bool _tlsconfigEnsureSealed(_In_ struct TlsConfig* self);

// True if this config was asked to resume sessions. The filter checks it before spending anything
// on the cache lookup or the save.
bool _tlsconfigResumes(_In_ struct TlsConfig* self);

// Client resumption. Offer whatever session was last saved for `host` to a context that has not
// yet handshaked, and store the one a completed session produced. Both are no-ops when resumption
// is off, `host` is empty, or this is a server config -- a server resumes through its ticket
// context and cache instead, with no per-host state of its own.
void _tlsconfigOfferSession(_In_ struct TlsConfig* self, _In_opt_ strref host,
                            _Inout_ mbedtls_ssl_context* ssl);
void _tlsconfigSaveSession(_In_ struct TlsConfig* self, _In_opt_ strref host,
                           _In_ mbedtls_ssl_context* ssl);

// Number of certificates on a chain. mbedTLS models a chain as a linked list with no count, and a
// freshly initialized one is a single zeroed node rather than an empty list, which is the part
// worth having in one place.
int32 _tlsChainCount(_In_opt_ const mbedtls_x509_crt* chain);

// Load the platform's trusted roots onto the end of `chain`: a CA bundle or hashed directory on
// Unix, the CryptoAPI ROOT store on Windows. Returns the number of certificates this call added,
// or -1 if no store could be located or opened. Implemented per platform in tlssystemca.c.
int32 _tlsSystemCALoad(_Inout_ mbedtls_x509_crt* chain);

CX_C_END
