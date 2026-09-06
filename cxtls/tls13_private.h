#pragma once

// The TLS 1.3 cryptographic substrate: HKDF-Expand-Label, the key schedule, the transcript hash,
// and ephemeral key agreement, all on the public PSA API.
//
// This sits below the record layer rather than inside one. mbedTLS has no way to detach its own
// record layer, and QUIC (RFC 9001) carries bare handshake messages with its own packet protection
// instead of TLS records, so cx derives its own keys here and hands the handshake engine above it
// the four sets of secrets it needs.

#include <cxtls/tls_shared.h>

#include <cx/buffer/buffer.h>
#include <cx/string.h>

#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

struct TlsConfig;
struct TlsCreds;

CX_C_BEGIN

// Longest hash output any TLS 1.3 cipher suite uses (SHA-384), and so the size of every secret.
#define TLS13_MAX_HASH 48

// Longest AEAD key any TLS 1.3 cipher suite uses (AES-256 and ChaCha20 are both 32).
#define TLS13_MAX_KEY 32

// Every TLS 1.3 AEAD uses a 12-byte nonce, so the IV derived alongside the key is always this long.
#define TLS13_IV_LEN 12

// Largest key_exchange octet string across the groups below: an uncompressed P-256 point.
#define TLS13_MAX_KEXPUB 65

// ---------------------------------------------------------------------------------------------
// Cipher suites
// ---------------------------------------------------------------------------------------------

// TLS 1.3 cipher suite codepoints (RFC 8446 appendix B.4). The two CCM suites are deliberately
// absent: nothing requires them and QUIC's mandatory set does not include them.
typedef enum {
    TLS13_AES_128_GCM_SHA256       = 0x1301,
    TLS13_AES_256_GCM_SHA384       = 0x1302,
    TLS13_CHACHA20_POLY1305_SHA256 = 0x1303
} Tls13SuiteId;

// Everything derived from a cipher suite codepoint: which hash drives the key schedule, and which
// AEAD the resulting keys are for.
typedef struct Tls13Suite {
    uint16 id;                // Tls13SuiteId
    uint8 hashLen;            // output of `hash`, and the length of every secret
    uint8 keyLen;             // AEAD key length
    psa_algorithm_t hash;     // PSA_ALG_SHA_256 / PSA_ALG_SHA_384
    psa_algorithm_t aead;     // PSA_ALG_GCM / PSA_ALG_CHACHA20_POLY1305
    psa_key_type_t keyType;   // PSA_KEY_TYPE_AES / PSA_KEY_TYPE_CHACHA20
} Tls13Suite;

// The suite table entry for a codepoint, or NULL if cx does not implement it.
_Ret_maybenull_ const Tls13Suite* _tls13Suite(uint16 id);

// Length of a hash's output in bytes, or 0 if it is not one of the two TLS 1.3 uses.
uint8 _tls13HashLen(psa_algorithm_t hash);

// ---------------------------------------------------------------------------------------------
// Key derivation (RFC 8446 section 7.1)
// ---------------------------------------------------------------------------------------------

// HKDF-Extract. `out` receives exactly _tls13HashLen(hash) bytes. A NULL salt or ikm is treated as
// a zero-length string, which is not the same as a zero-filled one -- the key schedule's "0" inputs
// are hashLen zero bytes and must be passed explicitly.
bool _tls13Extract(psa_algorithm_t hash, _In_reads_bytes_opt_(saltLen) const uint8* salt,
                   size_t saltLen, _In_reads_bytes_opt_(ikmLen) const uint8* ikm, size_t ikmLen,
                   _Out_writes_bytes_(TLS13_MAX_HASH) uint8* out);

// HKDF-Expand-Label. `label` is the part after the "tls13 " prefix, so pass _SL("c hs traffic").
// mbedTLS has this, but only behind library/ssl_tls13_keys.h, which is not installed.
bool _tls13ExpandLabel(psa_algorithm_t hash, _In_reads_bytes_(secretLen) const uint8* secret,
                       size_t secretLen, _In_ strref label,
                       _In_reads_bytes_opt_(ctxLen) const uint8* ctx, size_t ctxLen,
                       _Out_writes_bytes_(outLen) uint8* out, size_t outLen);

// Serialize the HkdfLabel structure _tls13ExpandLabel() feeds to HKDF-Expand. Exposed only so a
// test can compare it against a published vector; nothing else needs it.
bool _tls13HkdfLabel(_In_ strref label, _In_reads_bytes_opt_(ctxLen) const uint8* ctx, size_t ctxLen,
                     uint16 outLen, _Out_writes_bytes_(bufsz) uint8* buf, size_t bufsz,
                     _Out_ size_t* len);

// Derive-Secret(Secret, Label, Messages), where `thash` is the already-computed transcript hash of
// Messages. `secret` and `out` are both _tls13HashLen(hash) bytes.
bool _tls13DeriveSecret(psa_algorithm_t hash, _In_reads_bytes_(TLS13_MAX_HASH) const uint8* secret,
                        _In_ strref label, _In_reads_bytes_opt_(thashLen) const uint8* thash,
                        size_t thashLen, _Out_writes_bytes_(TLS13_MAX_HASH) uint8* out);

// Expand a traffic secret into the AEAD key and IV it protects records (or QUIC packets) with. The
// labels differ by protocol -- TLS uses "key"/"iv", QUIC uses "quic key"/"quic iv" -- so they are
// passed in rather than assumed.
bool _tls13TrafficKeys(psa_algorithm_t hash, _In_reads_bytes_(TLS13_MAX_HASH) const uint8* secret,
                       _In_ strref keyLabel, _Out_writes_bytes_(keyLen) uint8* key, size_t keyLen,
                       _In_ strref ivLabel, _Out_writes_bytes_(ivLen) uint8* iv, size_t ivLen);

// The Finished message's verify_data: HMAC(HKDF-Expand-Label(secret, "finished", "", hashLen),
// transcript hash). `out` receives _tls13HashLen(hash) bytes.
bool _tls13Finished(psa_algorithm_t hash, _In_reads_bytes_(TLS13_MAX_HASH) const uint8* secret,
                    _In_reads_bytes_(thashLen) const uint8* thash, size_t thashLen,
                    _Out_writes_bytes_(TLS13_MAX_HASH) uint8* out);

// ---------------------------------------------------------------------------------------------
// Transcript hash
// ---------------------------------------------------------------------------------------------

// The running hash of every handshake message sent and received, in order. Cloned rather than
// finished when a hash is needed, since the transcript keeps growing afterwards.
typedef struct Tls13Transcript {
    psa_hash_operation_t op;
    psa_algorithm_t hash;
    bool live;   // op has been set up and not yet aborted
} Tls13Transcript;

bool _tls13TranscriptInit(_Out_ Tls13Transcript* tr, psa_algorithm_t hash);
bool _tls13TranscriptUpdate(_Inout_ Tls13Transcript* tr,
                            _In_reads_bytes_(len) const uint8* data, size_t len);

// Snapshot the transcript so far without ending it. `out` receives _tls13HashLen() bytes.
bool _tls13TranscriptHash(_In_ const Tls13Transcript* tr,
                          _Out_writes_bytes_(TLS13_MAX_HASH) uint8* out);

void _tls13TranscriptDestroy(_Inout_ Tls13Transcript* tr);

// ---------------------------------------------------------------------------------------------
// Key schedule (RFC 8446 section 7.1)
// ---------------------------------------------------------------------------------------------

// The key schedule walks three Extract stages -- early, handshake, master -- each of which becomes
// the salt for the next through Derive-Secret(., "derived", ""). This holds whichever stage's
// secret is current; the per-stage traffic secrets are pulled out with _tls13SchedDerive() before
// advancing.
typedef struct Tls13Schedule {
    psa_algorithm_t hash;
    uint8 hashLen;
    uint8 secret[TLS13_MAX_HASH];
} Tls13Schedule;

// Stage 1: Extract(0, PSK). A NULL psk means the all-zero PSK used when there is no resumption.
bool _tls13SchedEarly(_Out_ Tls13Schedule* s, psa_algorithm_t hash,
                      _In_reads_bytes_opt_(pskLen) const uint8* psk, size_t pskLen);

// Stage 2: Extract(Derive-Secret(early, "derived", ""), ECDHE).
bool _tls13SchedHandshake(_Inout_ Tls13Schedule* s,
                          _In_reads_bytes_(ecdheLen) const uint8* ecdhe, size_t ecdheLen);

// Stage 3: Extract(Derive-Secret(handshake, "derived", ""), 0).
bool _tls13SchedMaster(_Inout_ Tls13Schedule* s);

// Derive-Secret from whichever stage the schedule is currently on.
bool _tls13SchedDerive(_In_ const Tls13Schedule* s, _In_ strref label,
                       _In_reads_bytes_opt_(thashLen) const uint8* thash, size_t thashLen,
                       _Out_writes_bytes_(TLS13_MAX_HASH) uint8* out);

// ---------------------------------------------------------------------------------------------
// Ephemeral key agreement
// ---------------------------------------------------------------------------------------------

// TLS named group codepoints (RFC 8446 section 4.2.7). These two are what QUIC deployments
// actually negotiate; everything else is declined.
typedef enum {
    TLS13_GROUP_SECP256R1 = 0x0017,
    TLS13_GROUP_X25519    = 0x001d
} Tls13Group;

// One side's ephemeral key agreement key pair.
typedef struct Tls13Kex {
    psa_key_id_t key;
    uint16 group;
} Tls13Kex;

// Length of a group's key_exchange octet string, or 0 if the group is not supported.
size_t _tls13GroupPubLen(uint16 group);

bool _tls13KexGenerate(_Out_ Tls13Kex* kex, uint16 group);

// Import a known private key instead of generating one. Only a test with a published vector has
// any business calling this -- a real handshake must use a fresh random key every time.
bool _tls13KexImport(_Out_ Tls13Kex* kex, uint16 group,
                     _In_reads_bytes_(privLen) const uint8* priv, size_t privLen);

// Our key_exchange octets, as they go into a key_share entry.
bool _tls13KexPublic(_In_ const Tls13Kex* kex, _Out_writes_bytes_(bufsz) uint8* buf, size_t bufsz,
                     _Out_ size_t* len);

// The ECDHE shared secret: the x coordinate for P-256, the raw X25519 output otherwise.
bool _tls13KexAgree(_In_ const Tls13Kex* kex, _In_reads_bytes_(peerLen) const uint8* peer,
                    size_t peerLen, _Out_writes_bytes_(bufsz) uint8* buf, size_t bufsz,
                    _Out_ size_t* len);

void _tls13KexDestroy(_Inout_ Tls13Kex* kex);

// ---------------------------------------------------------------------------------------------
// Handshake message codec
// ---------------------------------------------------------------------------------------------

// Handshake message types (RFC 8446 appendix B.3).
typedef enum {
    TLS13_HS_CLIENT_HELLO         = 1,
    TLS13_HS_SERVER_HELLO         = 2,
    TLS13_HS_NEW_SESSION_TICKET   = 4,
    TLS13_HS_END_OF_EARLY_DATA    = 5,
    TLS13_HS_ENCRYPTED_EXTENSIONS = 8,
    TLS13_HS_CERTIFICATE          = 11,
    TLS13_HS_CERTIFICATE_REQUEST  = 13,
    TLS13_HS_CERTIFICATE_VERIFY   = 15,
    TLS13_HS_FINISHED             = 20,
    TLS13_HS_KEY_UPDATE           = 24,

    // Not a message that is ever sent: the synthetic wrapper RFC 8446 section 4.4.1 substitutes
    // for the first ClientHello in the transcript once a HelloRetryRequest has gone out.
    TLS13_HS_MESSAGE_HASH         = 254
} Tls13HsType;

// Extension codepoints (RFC 8446 section 4.2, plus RFC 9001 for the transport parameters).
typedef enum {
    TLS13_EXT_SERVER_NAME            = 0,
    TLS13_EXT_SUPPORTED_GROUPS       = 10,
    TLS13_EXT_SIGNATURE_ALGORITHMS   = 13,
    TLS13_EXT_ALPN                   = 16,
    TLS13_EXT_PRE_SHARED_KEY         = 41,
    TLS13_EXT_EARLY_DATA             = 42,
    TLS13_EXT_SUPPORTED_VERSIONS     = 43,
    TLS13_EXT_COOKIE                 = 44,
    TLS13_EXT_PSK_KEY_EXCHANGE_MODES = 45,
    TLS13_EXT_KEY_SHARE              = 51,
    TLS13_EXT_QUIC_TRANSPORT_PARAMS  = 57
} Tls13ExtType;

// Reader over a handshake message. Every read is bounds-checked and sets a sticky error flag
// rather than returning one, so a parse can run start to finish and be checked once at the end.
typedef struct Tls13Rd {
    const uint8* p;
    const uint8* end;
    bool bad;
} Tls13Rd;

void _tls13RdInit(_Out_ Tls13Rd* rd, _In_reads_bytes_(len) const uint8* data, size_t len);
size_t _tls13RdLeft(_In_ const Tls13Rd* rd);
uint8 _tls13Rd8(_Inout_ Tls13Rd* rd);
uint16 _tls13Rd16(_Inout_ Tls13Rd* rd);
uint32 _tls13Rd24(_Inout_ Tls13Rd* rd);
uint32 _tls13Rd32(_Inout_ Tls13Rd* rd);
uint64 _tls13Rd64(_Inout_ Tls13Rd* rd);
_Ret_maybenull_ const uint8* _tls13RdBytes(_Inout_ Tls13Rd* rd, size_t n);

// Enter a length-prefixed vector: `sub` is left covering the vector's contents and `rd` advances
// past it. False on underrun, leaving `sub` empty.
bool _tls13RdVec8(_Inout_ Tls13Rd* rd, _Out_ Tls13Rd* sub);
bool _tls13RdVec16(_Inout_ Tls13Rd* rd, _Out_ Tls13Rd* sub);
bool _tls13RdVec24(_Inout_ Tls13Rd* rd, _Out_ Tls13Rd* sub);

// Writer, backed by a Buffer that grows as needed. A failed allocation or a vector longer than its
// length prefix can express sets `bad`, checked once when the message is taken.
typedef struct Tls13Wr {
    Buffer buf;
    size_t len;
    bool bad;
} Tls13Wr;

void _tls13WrInit(_Out_ Tls13Wr* wr, size_t hint);
void _tls13WrDestroy(_Inout_ Tls13Wr* wr);
void _tls13Wr8(_Inout_ Tls13Wr* wr, uint8 v);
void _tls13Wr16(_Inout_ Tls13Wr* wr, uint16 v);
void _tls13Wr24(_Inout_ Tls13Wr* wr, uint32 v);
void _tls13Wr32(_Inout_ Tls13Wr* wr, uint32 v);
void _tls13Wr64(_Inout_ Tls13Wr* wr, uint64 v);
void _tls13WrBytes(_Inout_ Tls13Wr* wr, _In_reads_bytes_opt_(len) const uint8* data, size_t len);

// Length-prefixed vectors are written by reserving the prefix, writing the contents, then
// backfilling: the mark returned by open is passed straight back to close.
size_t _tls13WrOpen8(_Inout_ Tls13Wr* wr);
size_t _tls13WrOpen16(_Inout_ Tls13Wr* wr);
size_t _tls13WrOpen24(_Inout_ Tls13Wr* wr);
void _tls13WrClose8(_Inout_ Tls13Wr* wr, size_t mark);
void _tls13WrClose16(_Inout_ Tls13Wr* wr, size_t mark);
void _tls13WrClose24(_Inout_ Tls13Wr* wr, size_t mark);

// Hand the finished message to the caller, transferring ownership of the buffer and leaving the
// writer empty. NULL if anything went wrong along the way.
_Ret_maybenull_ Buffer _tls13WrTake(_Inout_ Tls13Wr* wr);

// One extension as it appears on the wire. `data` points into the message that was parsed, so it
// lives exactly as long as those bytes do.
typedef struct Tls13Ext {
    uint16 type;
    uint16 len;
    const uint8* data;
} Tls13Ext;

// Caps on what a hello may carry. Deliberately small: a peer that offers more than this is not one
// cx wants to talk to, and a fixed array keeps parsing allocation-free.
#define TLS13_MAX_EXTS   24
#define TLS13_MAX_SUITES 16

// A parsed (or to-be-encoded) ClientHello or ServerHello. The two differ only in whether the
// cipher suite list may hold more than one entry, so one struct covers both.
typedef struct Tls13Hello {
    uint16 legacyVersion;   // 0x0303 in TLS 1.3; the real version is in supported_versions
    uint8 random[32];
    uint8 sessionId[32];
    uint8 sessionIdLen;

    uint16 suites[TLS13_MAX_SUITES];
    uint8 nsuites;   // exactly 1 for a ServerHello

    Tls13Ext exts[TLS13_MAX_EXTS];
    uint8 nexts;
} Tls13Hello;

// Parse a complete handshake message, header included. `server` selects ServerHello over
// ClientHello. Extension bodies are borrowed from `msg`, which must outlive `h`.
bool _tls13HelloParse(_Out_ Tls13Hello* h, bool server,
                      _In_reads_bytes_(len) const uint8* msg, size_t len);

// Encode a complete handshake message, header included.
_Ret_maybenull_ Buffer _tls13HelloEncode(_In_ const Tls13Hello* h, bool server);

// The first extension of a given type, or NULL if the hello does not carry one.
_Ret_maybenull_ const Tls13Ext* _tls13HelloExt(_In_ const Tls13Hello* h, uint16 type);

// Append an extension. The body is copied into the hello's own storage only in the sense that the
// pointer is kept -- `data` must outlive the hello, which it does for the static tables and the
// key-share buffers a handshake builds on its own stack frame.
bool _tls13HelloAddExt(_Inout_ Tls13Hello* h, uint16 type,
                       _In_reads_bytes_(len) const uint8* data, uint16 len);

// ---------------------------------------------------------------------------------------------
// Handshake engine
// ---------------------------------------------------------------------------------------------

// Alert codes (RFC 8446 appendix B.2). QUIC has no alert record, so these are reported to the
// transport instead and end up in a CONNECTION_CLOSE frame with error code 0x0100 + alert.
#define TLS13_ALERT_CLOSE_NOTIFY            0
#define TLS13_ALERT_UNEXPECTED_MESSAGE      10
#define TLS13_ALERT_HANDSHAKE_FAILURE       40
#define TLS13_ALERT_BAD_CERTIFICATE         42
#define TLS13_ALERT_CERTIFICATE_EXPIRED     45
#define TLS13_ALERT_CERTIFICATE_UNKNOWN     46
#define TLS13_ALERT_ILLEGAL_PARAMETER       47
#define TLS13_ALERT_UNKNOWN_CA              48
#define TLS13_ALERT_DECODE_ERROR            50
#define TLS13_ALERT_DECRYPT_ERROR           51
#define TLS13_ALERT_PROTOCOL_VERSION        70
#define TLS13_ALERT_INTERNAL_ERROR          80
#define TLS13_ALERT_MISSING_EXTENSION       109
#define TLS13_ALERT_UNSUPPORTED_EXTENSION   110
#define TLS13_ALERT_UNRECOGNIZED_NAME       112
#define TLS13_ALERT_CERTIFICATE_REQUIRED    116
#define TLS13_ALERT_NO_APPLICATION_PROTOCOL 120

// SignatureScheme codepoints (RFC 8446 section 4.2.3). PKCS#1 v1.5 is absent because TLS 1.3
// forbids it in CertificateVerify, and the Ed25519/Ed448 schemes because mbedTLS has no EdDSA.
typedef enum {
    TLS13_SIG_ECDSA_SECP256R1_SHA256 = 0x0403,
    TLS13_SIG_ECDSA_SECP384R1_SHA384 = 0x0503,
    TLS13_SIG_ECDSA_SECP521R1_SHA512 = 0x0603,
    TLS13_SIG_RSA_PSS_RSAE_SHA256    = 0x0804,
    TLS13_SIG_RSA_PSS_RSAE_SHA384    = 0x0805,
    TLS13_SIG_RSA_PSS_RSAE_SHA512    = 0x0806
} Tls13SigScheme;

// Every suite and group is offered by default; the arrays exist so an endpoint can narrow or
// reorder them.
#define TLS13_MAX_SUITEPREF 4
#define TLS13_MAX_GROUPS    4

// Ceiling on the handshake bytes buffered per encryption level while a message is incomplete. A
// certificate chain is the only thing that comes anywhere near it, and a peer that exceeds it is
// not one worth continuing with.
#define TLS13_MAX_HS_BUFFER (128 * 1024)

// A resumption ticket, in the form both ends keep it. The server seals this into the opaque bytes
// it hands out and reopens it when the client offers it back; the client stores the same shape,
// with `id` holding the sealed bytes to offer.
typedef struct Tls13Ticket {
    uint16 suite;                 // cipher suite the original handshake ran, which fixes the hash
    uint8 pskLen;
    uint8 psk[TLS13_MAX_HASH];    // the resumption PSK itself
    uint32 ageAdd;                // added to the reported ticket age to obfuscate it
    uint32 lifetime;              // seconds the ticket stays usable
    int64 issued;                 // wall-clock time the ticket was issued
    Buffer id;                    // the opaque ticket octets; empty in the server's sealed copy
    string alpn;                  // protocol negotiated by the original handshake

    // 0-RTT. `maxEarlyData` is the early_data extension of the NewSessionTicket -- zero when the
    // ticket does not permit it, and 0xffffffff when it does, which is the only value QUIC allows.
    // `tp` is the QUIC transport parameters the server sent in the original handshake: a client
    // has to send its early data under those, and a server has to check they are still the ones
    // it offers before it accepts any (RFC 9001 section 4.5).
    uint32 maxEarlyData;
    Buffer tp;
} Tls13Ticket;

void _tls13TicketDestroy(_Inout_ Tls13Ticket* t);

// Serialize into (or out of) the flat form the client caches and the server seals.
_Ret_maybenull_ Buffer _tls13TicketSerialize(_In_ const Tls13Ticket* t);
bool _tls13TicketParse(_Out_ Tls13Ticket* t, _In_reads_bytes_(len) const uint8* data, size_t len);

// The same thing under AES-256-GCM with a random 12-byte nonce, which is what the server actually
// hands out: `key` is the config's ticket key, and the sealed form is nonce || ciphertext || tag.
_Ret_maybenull_ Buffer _tls13TicketSeal(_In_reads_bytes_(32) const uint8* key,
                                        _In_ const Tls13Ticket* t);
bool _tls13TicketOpen(_In_reads_bytes_(32) const uint8* key,
                      _In_reads_bytes_(len) const uint8* data, size_t len, _Out_ Tls13Ticket* t);

// Where a handshake has got to. The client and server walk disjoint halves of this.
typedef enum {
    TLS13_ST_START = 0,
    TLS13_ST_WAIT_SH,           // client: ServerHello or HelloRetryRequest
    TLS13_ST_WAIT_EE,           // client: EncryptedExtensions
    TLS13_ST_WAIT_CERT_CR,      // client: Certificate or CertificateRequest
    TLS13_ST_WAIT_CERT,         // client: Certificate
    TLS13_ST_WAIT_CV,           // client: CertificateVerify
    TLS13_ST_WAIT_SRV_FIN,      // client: server Finished
    TLS13_ST_WAIT_CH,           // server: ClientHello
    TLS13_ST_WAIT_CH2,          // server: the second ClientHello, after a HelloRetryRequest
    TLS13_ST_WAIT_CLI_CERT,     // server: client Certificate
    TLS13_ST_WAIT_CLI_CV,       // server: client CertificateVerify
    TLS13_ST_WAIT_CLI_FIN,      // server: client Finished
    TLS13_ST_DONE,
    TLS13_ST_FAILED
} Tls13HsState;

// Bytes accumulated for one encryption level while a handshake message is still incomplete.
typedef struct Tls13HsIn {
    Buffer buf;
    size_t len;    // valid bytes in buf
    size_t used;   // bytes already handed to a message
} Tls13HsIn;

// One TLS 1.3 handshake, without a record layer. Fed complete CRYPTO-stream bytes per encryption
// level and driven entirely by what arrives; everything it produces leaves through `handlers`.
typedef struct Tls13Hs {
    bool server;
    uint8 state;   // Tls13HsState
    uint8 alert;   // alert raised by a failure, or 0

    struct TlsConfig* config;        // borrowed; the owning TlsQuic holds the reference
    const TlsQuicHandlers* handlers; // borrowed, must outlive the handshake
    void* hctx;

    const Tls13Suite* suite;
    Tls13Schedule sched;
    Tls13Transcript tr;
    bool trLive;

    uint16 suites[TLS13_MAX_SUITEPREF];
    uint8 nsuites;

    Tls13Kex kex;
    uint16 groups[TLS13_MAX_GROUPS];
    uint8 ngroups;
    uint16 group;   // group the key share was generated for

    string hostname;   // client: SNI to send and name to verify. server: the name that arrived
    string alpnSel;    // protocol negotiated out of the config's ALPN list

    Buffer tp;   // our quic_transport_parameters body

    // Transcript bytes accumulated before the running hash can be started. The hash is fixed by
    // the cipher suite, which the client does not learn until the ServerHello, and both ends need
    // to hash a *truncated* ClientHello to check a PSK binder -- so everything up to and including
    // the ClientHello is kept here and fed in one go once the suite is known.
    Buffer trPre;

    uint8 random[32];   // our hello random; a second ClientHello must repeat the first one's
    Buffer cookie;      // client: cookie echoed back from a HelloRetryRequest

    Tls13HsIn in[4];

    uint8 clientHsSecret[TLS13_MAX_HASH];
    uint8 serverHsSecret[TLS13_MAX_HASH];
    uint8 clientApSecret[TLS13_MAX_HASH];
    uint8 resumption[TLS13_MAX_HASH];

    Tls13Ticket ticket;   // client: the ticket being offered. server: the one that was accepted
    bool pskOffered;
    bool pskAccepted;

    // 0-RTT. `earlyOffered` is set on a client that put early_data in its ClientHello and on a
    // server that saw one; `earlyAccepted` is what was decided, and `earlyDecided` that the
    // transport has been told. `peerTp` is the server's transport parameters as a client saw
    // them, kept so that the ticket it is about to be issued can carry them.
    bool earlyOffered;
    bool earlyAccepted;
    bool earlyDecided;
    Buffer peerTp;

    mbedtls_x509_crt peerCert;
    bool peerCertInit;
    bool peerPresent;    // a peer Certificate message arrived and parsed
    bool peerVerified;   // the chain was checked against the trust store
    uint32 verifyFlags;

    uint16 peerSchemes[16];   // signature_algorithms the peer will accept
    uint8 npeerSchemes;

    bool hrrDone;        // one HelloRetryRequest has already happened
    bool certRequested;  // the server asked for a client certificate
    struct TlsCreds* creds;   // server: the identity to present, borrowed
} Tls13Hs;

bool _tls13HsInit(_Out_ Tls13Hs* hs, bool server, _In_ struct TlsConfig* config);
void _tls13HsDestroy(_Inout_ Tls13Hs* hs);
void _tls13HsSetHandlers(_Inout_ Tls13Hs* hs, _In_opt_ const TlsQuicHandlers* handlers,
                         _In_opt_ void* ctx);
bool _tls13HsSetHostname(_Inout_ Tls13Hs* hs, _In_opt_ strref host);
bool _tls13HsSetSuites(_Inout_ Tls13Hs* hs, _In_reads_(n) const uint16* suites, size_t n);
bool _tls13HsSetGroups(_Inout_ Tls13Hs* hs, _In_reads_(n) const uint16* groups, size_t n);
bool _tls13HsSetTransportParams(_Inout_ Tls13Hs* hs,
                                _In_reads_bytes_(len) const uint8* data, size_t len);

// Send the first flight. Client only; a server starts when a ClientHello arrives.
bool _tls13HsStart(_Inout_ Tls13Hs* hs);

// Feed CRYPTO-stream bytes for one encryption level, in order. Returns false once the handshake
// has failed, with `alert` describing why.
bool _tls13HsRecv(_Inout_ Tls13Hs* hs, TlsQuicLevel level,
                  _In_reads_bytes_(len) const uint8* data, size_t len);

// Everything a TlsQuic object owns: the engine, plus the snapshot taken once it completed.
typedef struct TlsQuicState {
    Tls13Hs hs;
    TlsInfo info;
    bool infoValid;
} TlsQuicState;

// The IANA name of a cipher suite, for a log line or a TlsInfo snapshot. NULL if it is not one of
// the three this engine implements.
_Ret_maybenull_ strref _tls13SuiteName(uint16 id);

// ---------------------------------------------------------------------------------------------
// Certificates and signatures (tls13auth.c)
// ---------------------------------------------------------------------------------------------

// The Certificate message body -- everything after the 4-byte handshake header.
void _tls13CertEncode(_Inout_ Tls13Wr* wr, _In_opt_ const mbedtls_x509_crt* chain);
bool _tls13CertParse(_Inout_ mbedtls_x509_crt* out, _In_reads_bytes_(len) const uint8* body,
                     size_t len, _Out_ bool* empty);

// Pick a signature scheme this key can produce and the peer said it accepts.
bool _tls13SigSelect(_In_ const mbedtls_pk_context* key,
                     _In_reads_(n) const uint16* peerSchemes, size_t n, _Out_ uint16* out);

// CertificateVerify, both directions. `serverSig` selects which of the two context strings the
// signed content is built from, and is the role of whoever produced the signature.
bool _tls13SigMake(uint16 scheme, _Inout_ mbedtls_pk_context* key, bool serverSig,
                   _In_reads_bytes_(thashLen) const uint8* thash, size_t thashLen,
                   _Out_writes_bytes_to_(sigsz, *sigLen) uint8* sig, size_t sigsz,
                   _Out_ size_t* sigLen);
bool _tls13SigCheck(uint16 scheme, _Inout_ mbedtls_pk_context* key, bool serverSig,
                    _In_reads_bytes_(thashLen) const uint8* thash, size_t thashLen,
                    _In_reads_bytes_(sigLen) const uint8* sig, size_t sigLen);

// Verify a parsed peer chain against the config's trust store, checking `hostname` when one is
// given. Fills in the MBEDTLS_X509_BADCERT_* flags either way; the return value says whether the
// chain is acceptable under the config's authentication mode.
bool _tls13ChainVerify(_In_ struct TlsConfig* config, _Inout_ mbedtls_x509_crt* chain,
                       _In_opt_ strref hostname, _Out_ uint32* flags);

CX_C_END
