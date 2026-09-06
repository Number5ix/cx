#include "tls13_private.h"
#include "tls_private.h"
#include "tlsconfig.h"

#include <cx/time/clock.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL TlsLogChannel

// Element count of a static table.
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

#define TLS13_VERSION        0x0304
#define TLS13_LEGACY_VERSION 0x0303

// RFC 8446 section 4.1.3: a ServerHello whose random is this is a HelloRetryRequest. The value is
// SHA-256("HelloRetryRequest"), so a real random can never collide with it.
static const uint8 hrrRandom[32] = { 0xcf, 0x21, 0xad, 0x74, 0xe5, 0x9a, 0x61, 0x11,
                                     0xbe, 0x1d, 0x8c, 0x02, 0x1e, 0x65, 0xb8, 0x91,
                                     0xc2, 0xa2, 0x11, 0x16, 0x7a, 0xbb, 0x8c, 0x5e,
                                     0x07, 0x9e, 0x09, 0xe2, 0xc8, 0xa8, 0x33, 0x9c };

// Cipher suites offered, most preferred first. A server walks this list and takes the first entry
// the client also offered, so it is the server's preference that decides.
static const uint16 suitePref[] = { TLS13_AES_128_GCM_SHA256,
                                    TLS13_CHACHA20_POLY1305_SHA256,
                                    TLS13_AES_256_GCM_SHA384 };

// Signature schemes advertised in signature_algorithms, and the order a signer picks from.
static const uint16 sigPref[] = { TLS13_SIG_ECDSA_SECP256R1_SHA256,
                                  TLS13_SIG_ECDSA_SECP384R1_SHA384,
                                  TLS13_SIG_ECDSA_SECP521R1_SHA512,
                                  TLS13_SIG_RSA_PSS_RSAE_SHA256,
                                  TLS13_SIG_RSA_PSS_RSAE_SHA384,
                                  TLS13_SIG_RSA_PSS_RSAE_SHA512 };

static const uint16 groupPref[] = { TLS13_GROUP_X25519, TLS13_GROUP_SECP256R1 };

// Longest a ticket may claim to be usable for (RFC 8446 section 4.6.1), in seconds.
#define TLS13_MAX_TICKET_LIFETIME 604800

// The only max_early_data_size a QUIC server may name (RFC 9001 section 4.6.1). QUIC bounds early
// data with its own flow control, so the TLS limit has nothing left to say and is pinned here.
#define TLS13_QUIC_MAX_EARLY_DATA 0xffffffffu

// ---------------------------------------------------------------------------------------------
// Failure and transcript plumbing
// ---------------------------------------------------------------------------------------------

static bool hsFail(_Inout_ Tls13Hs* hs, uint8 alert, _In_ strref why)
{
    if (hs->state == TLS13_ST_FAILED)
        return false;

    hs->state = TLS13_ST_FAILED;
    hs->alert = alert;

    logFmt(Warn,
           _SL("TLS/QUIC handshake failed: ${string} (alert ${int})"),
           stvar(strref, why),
           stvar(int32, (int32)alert));

    if (hs->handlers && hs->handlers->alert)
        hs->handlers->alert(hs->hctx, alert);

    return false;
}

// Add bytes to the transcript. Before the cipher suite is known there is no running hash to add
// them to, so they pile up in trPre until hsTrStart() feeds them in one go.
static bool hsTrUpdate(_Inout_ Tls13Hs* hs, _In_reads_bytes_(len) const uint8* data, size_t len)
{
    if (hs->trLive)
        return _tls13TranscriptUpdate(&hs->tr, data, len);

    size_t have = hs->trPre ? hs->trPre->len : 0;
    bufResize(&hs->trPre, have + len);
    if (!hs->trPre)
        return false;

    memcpy(hs->trPre->data + have, data, len);
    hs->trPre->len = have + len;
    return true;
}

static bool hsTrStart(_Inout_ Tls13Hs* hs)
{
    if (hs->trLive || !hs->suite)
        return false;

    if (!_tls13TranscriptInit(&hs->tr, hs->suite->hash))
        return false;
    hs->trLive = true;

    bool ok = !hs->trPre || _tls13TranscriptUpdate(&hs->tr, hs->trPre->data, hs->trPre->len);
    bufDestroy(&hs->trPre);
    return ok;
}

// Hash everything buffered so far plus `extra`, without disturbing anything. Used for the two
// places that hash a message the running transcript has not been given: a PSK binder, which
// covers a truncated ClientHello, and the ClientHello digest a HelloRetryRequest replaces it with.
static bool hsPreHash(_In_ const Tls13Hs* hs, _In_reads_bytes_opt_(len) const uint8* extra,
                      size_t len, _Out_writes_bytes_(TLS13_MAX_HASH) uint8* out)
{
    Tls13Transcript t;
    if (!_tls13TranscriptInit(&t, hs->suite->hash))
        return false;

    bool ok = (!hs->trPre || _tls13TranscriptUpdate(&t, hs->trPre->data, hs->trPre->len)) &&
              (!len || _tls13TranscriptUpdate(&t, extra, len)) && _tls13TranscriptHash(&t, out);

    _tls13TranscriptDestroy(&t);
    return ok;
}

// Replace the buffered transcript with the synthetic message_hash wrapper RFC 8446 section 4.4.1
// substitutes for the first ClientHello once a HelloRetryRequest has been sent.
static bool hsTrCollapse(_Inout_ Tls13Hs* hs, _In_reads_bytes_opt_(len) const uint8* ch, size_t len)
{
    uint8 h1[TLS13_MAX_HASH];
    if (!hsPreHash(hs, ch, len, h1))
        return false;

    uint8 hdr[4] = { TLS13_HS_MESSAGE_HASH, 0, 0, hs->suite->hashLen };

    bufDestroy(&hs->trPre);
    hs->trPre = bufCreate(sizeof(hdr) + hs->suite->hashLen);
    memcpy(hs->trPre->data, hdr, sizeof(hdr));
    memcpy(hs->trPre->data + sizeof(hdr), h1, hs->suite->hashLen);
    hs->trPre->len = sizeof(hdr) + hs->suite->hashLen;

    return true;
}

static bool hsSend(_Inout_ Tls13Hs* hs, TlsQuicLevel level, _Inout_ Buffer* msg)
{
    if (!*msg)
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not build a handshake message");

    bool ok = hsTrUpdate(hs, (*msg)->data, (*msg)->len) && hs->handlers &&
              hs->handlers->sendCrypto &&
              hs->handlers->sendCrypto(hs->hctx, level, (*msg)->data, (*msg)->len);

    bufDestroy(msg);

    return ok ? true
              : hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not send a handshake message");
}

static bool hsSecrets(_Inout_ Tls13Hs* hs, TlsQuicLevel level,
                      _In_opt_ const uint8* rd, _In_opt_ const uint8* wr)
{
    if (!hs->handlers || !hs->handlers->secrets)
        return false;

    return hs->handlers->secrets(hs->hctx, level, hs->suite->id, rd, wr, hs->suite->hashLen);
}

// Constant-time equality, so that a wrong Finished or binder cannot be narrowed down a byte at a
// time by timing the comparison.
static bool ctEqual(_In_reads_bytes_(len) const uint8* a, _In_reads_bytes_(len) const uint8* b,
                    size_t len)
{
    uint8 diff = 0;
    for (size_t i = 0; i < len; i++)
        diff |= (uint8)(a[i] ^ b[i]);
    return diff == 0;
}

// ---------------------------------------------------------------------------------------------
// Message framing
// ---------------------------------------------------------------------------------------------

static void msgStart(_Out_ Tls13Wr* wr, uint8 type, _Out_ size_t* mark)
{
    _tls13WrInit(wr, 512);
    _tls13Wr8(wr, type);
    *mark = _tls13WrOpen24(wr);
}

static _Ret_maybenull_ Buffer msgFinish(_Inout_ Tls13Wr* wr, size_t mark)
{
    _tls13WrClose24(wr, mark);
    return _tls13WrTake(wr);
}

// Extension bodies are borrowed by Tls13Hello rather than copied, so each one is held here until
// the hello has been encoded.
#define TLS13_EXT_POOL 12

typedef struct ExtPool {
    Buffer b[TLS13_EXT_POOL];
    uint8 n;
} ExtPool;

static bool extPush(_Inout_ Tls13Hello* h, _Inout_ ExtPool* pool, uint16 type, _Inout_ Tls13Wr* wr)
{
    Buffer b = _tls13WrTake(wr);
    if (!b || pool->n >= TLS13_EXT_POOL || b->len > 0xffff) {
        bufDestroy(&b);
        return false;
    }

    pool->b[pool->n++] = b;
    return _tls13HelloAddExt(h, type, b->data, (uint16)b->len);
}

static void extPoolDestroy(_Inout_ ExtPool* pool)
{
    for (uint8 i = 0; i < pool->n; i++)
        bufDestroy(&pool->b[i]);
    pool->n = 0;
}

// ---------------------------------------------------------------------------------------------
// Extension bodies
// ---------------------------------------------------------------------------------------------

static bool extVersions(_Inout_ Tls13Hello* h, _Inout_ ExtPool* pool, bool client)
{
    Tls13Wr wr;
    _tls13WrInit(&wr, 8);

    if (client) {
        size_t mark = _tls13WrOpen8(&wr);
        _tls13Wr16(&wr, TLS13_VERSION);
        _tls13WrClose8(&wr, mark);
    } else {
        _tls13Wr16(&wr, TLS13_VERSION);
    }

    return extPush(h, pool, TLS13_EXT_SUPPORTED_VERSIONS, &wr);
}

static bool extGroups(_Inout_ Tls13Hello* h, _Inout_ ExtPool* pool, _In_ const Tls13Hs* hs)
{
    Tls13Wr wr;
    _tls13WrInit(&wr, 16);

    size_t mark = _tls13WrOpen16(&wr);
    for (uint8 i = 0; i < hs->ngroups; i++)
        _tls13Wr16(&wr, hs->groups[i]);
    _tls13WrClose16(&wr, mark);

    return extPush(h, pool, TLS13_EXT_SUPPORTED_GROUPS, &wr);
}

static bool extSigAlgs(_Inout_ Tls13Hello* h, _Inout_ ExtPool* pool)
{
    Tls13Wr wr;
    _tls13WrInit(&wr, 32);

    size_t mark = _tls13WrOpen16(&wr);
    for (size_t i = 0; i < NELEM(sigPref); i++)
        _tls13Wr16(&wr, sigPref[i]);
    _tls13WrClose16(&wr, mark);

    return extPush(h, pool, TLS13_EXT_SIGNATURE_ALGORITHMS, &wr);
}

static bool extSNI(_Inout_ Tls13Hello* h, _Inout_ ExtPool* pool, _In_ strref host)
{
    Tls13Wr wr;
    _tls13WrInit(&wr, 64);

    size_t list = _tls13WrOpen16(&wr);
    _tls13Wr8(&wr, 0);   // name_type: host_name
    size_t name = _tls13WrOpen16(&wr);
    _tls13WrBytes(&wr, (const uint8*)strC(host), strLen(host));
    _tls13WrClose16(&wr, name);
    _tls13WrClose16(&wr, list);

    return extPush(h, pool, TLS13_EXT_SERVER_NAME, &wr);
}

// The config's ALPN list, as a client offers it. Nothing is written when the config has none.
static bool extALPN(_Inout_ Tls13Hello* h, _Inout_ ExtPool* pool, _In_ TlsConfig* cfg)
{
    Tls13Wr wr;
    _tls13WrInit(&wr, 64);

    size_t list = _tls13WrOpen16(&wr);
    for (int32 i = 0; i < cfg->st->alpnCount; i++) {
        size_t n = strlen(cfg->st->alpn[i]);
        _tls13Wr8(&wr, (uint8)n);
        _tls13WrBytes(&wr, (const uint8*)cfg->st->alpn[i], n);
    }
    _tls13WrClose16(&wr, list);

    return extPush(h, pool, TLS13_EXT_ALPN, &wr);
}

static bool extKeyShareClient(_Inout_ Tls13Hello* h, _Inout_ ExtPool* pool, _In_ const Tls13Hs* hs)
{
    uint8 pub[TLS13_MAX_KEXPUB];
    size_t publen;
    if (!_tls13KexPublic(&hs->kex, pub, sizeof(pub), &publen))
        return false;

    Tls13Wr wr;
    _tls13WrInit(&wr, 128);

    size_t list = _tls13WrOpen16(&wr);
    _tls13Wr16(&wr, hs->group);
    size_t ke = _tls13WrOpen16(&wr);
    _tls13WrBytes(&wr, pub, publen);
    _tls13WrClose16(&wr, ke);
    _tls13WrClose16(&wr, list);

    return extPush(h, pool, TLS13_EXT_KEY_SHARE, &wr);
}

static bool extKeyShareServer(_Inout_ Tls13Hello* h, _Inout_ ExtPool* pool, _In_ const Tls13Hs* hs)
{
    uint8 pub[TLS13_MAX_KEXPUB];
    size_t publen;
    if (!_tls13KexPublic(&hs->kex, pub, sizeof(pub), &publen))
        return false;

    Tls13Wr wr;
    _tls13WrInit(&wr, 128);

    _tls13Wr16(&wr, hs->group);
    size_t ke = _tls13WrOpen16(&wr);
    _tls13WrBytes(&wr, pub, publen);
    _tls13WrClose16(&wr, ke);

    return extPush(h, pool, TLS13_EXT_KEY_SHARE, &wr);
}

static bool extKeyShareRetry(_Inout_ Tls13Hello* h, _Inout_ ExtPool* pool, uint16 group)
{
    Tls13Wr wr;
    _tls13WrInit(&wr, 8);
    _tls13Wr16(&wr, group);
    return extPush(h, pool, TLS13_EXT_KEY_SHARE, &wr);
}

static bool extBytes(_Inout_ Tls13Hello* h, _Inout_ ExtPool* pool, uint16 type,
                     _In_reads_bytes_opt_(len) const uint8* data, size_t len)
{
    Tls13Wr wr;
    _tls13WrInit(&wr, len + 8);
    _tls13WrBytes(&wr, data, len);
    return extPush(h, pool, type, &wr);
}

static bool extCookie(_Inout_ Tls13Hello* h, _Inout_ ExtPool* pool, _In_ Buffer cookie)
{
    Tls13Wr wr;
    _tls13WrInit(&wr, cookie->len + 8);

    size_t mark = _tls13WrOpen16(&wr);
    _tls13WrBytes(&wr, cookie->data, cookie->len);
    _tls13WrClose16(&wr, mark);

    return extPush(h, pool, TLS13_EXT_COOKIE, &wr);
}

static bool extPskModes(_Inout_ Tls13Hello* h, _Inout_ ExtPool* pool)
{
    Tls13Wr wr;
    _tls13WrInit(&wr, 8);

    size_t mark = _tls13WrOpen8(&wr);
    _tls13Wr8(&wr, 1);   // psk_dhe_ke: the only mode TLS 1.3 over QUIC has any use for
    _tls13WrClose8(&wr, mark);

    return extPush(h, pool, TLS13_EXT_PSK_KEY_EXCHANGE_MODES, &wr);
}

// The offered identity plus a binder of the right length but no value yet; the real binder can
// only be computed once the whole ClientHello has been encoded, and is patched in afterwards.
static bool extPreSharedKey(_Inout_ Tls13Hello* h, _Inout_ ExtPool* pool, _In_ const Tls13Hs* hs)
{
    int64 ageMs = timeToMsec(clockWall() - hs->ticket.issued);
    if (ageMs < 0)
        ageMs = 0;

    Tls13Wr wr;
    _tls13WrInit(&wr, 256);

    size_t ids = _tls13WrOpen16(&wr);
    size_t id  = _tls13WrOpen16(&wr);
    _tls13WrBytes(&wr, hs->ticket.id->data, hs->ticket.id->len);
    _tls13WrClose16(&wr, id);
    _tls13Wr32(&wr, (uint32)ageMs + hs->ticket.ageAdd);
    _tls13WrClose16(&wr, ids);

    size_t binders = _tls13WrOpen16(&wr);
    _tls13Wr8(&wr, hs->suite->hashLen);
    for (uint8 i = 0; i < hs->suite->hashLen; i++)
        _tls13Wr8(&wr, 0);
    _tls13WrClose16(&wr, binders);

    return extPush(h, pool, TLS13_EXT_PRE_SHARED_KEY, &wr);
}

// ---------------------------------------------------------------------------------------------
// Extension reading
// ---------------------------------------------------------------------------------------------

static bool extRd(_In_ const Tls13Ext* e, _Out_ Tls13Rd* rd)
{
    _tls13RdInit(rd, e->data, e->len);
    return true;
}

// supported_versions as a server sends it: exactly TLS 1.3 and nothing else.
static bool extCheckVersion(_In_ const Tls13Hello* h)
{
    const Tls13Ext* e = _tls13HelloExt(h, TLS13_EXT_SUPPORTED_VERSIONS);
    if (!e || e->len != 2)
        return false;

    Tls13Rd rd;
    extRd(e, &rd);
    return _tls13Rd16(&rd) == TLS13_VERSION;
}

// The same extension as a client sends it: a list, which must contain TLS 1.3.
static bool extOffersVersion(_In_ const Tls13Hello* h)
{
    const Tls13Ext* e = _tls13HelloExt(h, TLS13_EXT_SUPPORTED_VERSIONS);
    if (!e)
        return false;

    Tls13Rd rd, list;
    extRd(e, &rd);
    if (!_tls13RdVec8(&rd, &list))
        return false;

    while (_tls13RdLeft(&list) >= 2) {
        if (_tls13Rd16(&list) == TLS13_VERSION)
            return true;
    }
    return false;
}

static void extReadSigAlgs(_Inout_ Tls13Hs* hs, _In_ const Tls13Hello* h)
{
    hs->npeerSchemes = 0;

    const Tls13Ext* e = _tls13HelloExt(h, TLS13_EXT_SIGNATURE_ALGORITHMS);
    if (!e)
        return;

    Tls13Rd rd, list;
    extRd(e, &rd);
    if (!_tls13RdVec16(&rd, &list))
        return;

    while (_tls13RdLeft(&list) >= 2 && hs->npeerSchemes < NELEM(hs->peerSchemes))
        hs->peerSchemes[hs->npeerSchemes++] = _tls13Rd16(&list);
}

static bool extReadTransportParams(_Inout_ Tls13Hs* hs, _In_ const Tls13Hello* h)
{
    const Tls13Ext* e = _tls13HelloExt(h, TLS13_EXT_QUIC_TRANSPORT_PARAMS);
    if (!e)
        return false;

    // A client keeps the server's parameters, because a ticket this session goes on to issue has
    // to carry them: whatever early data the next connection sends is sent under these limits.
    if (!hs->server) {
        bufDestroy(&hs->peerTp);
        if (e->len > 0) {
            hs->peerTp = bufCreate(e->len);
            memcpy(hs->peerTp->data, e->data, e->len);
            hs->peerTp->len = e->len;
        }
    }

    return !hs->handlers || !hs->handlers->transportParams ||
           hs->handlers->transportParams(hs->hctx, e->data, e->len);
}

// Walk the peer's ALPN list against the config's, in the config's order, and record the match.
static bool extSelectALPN(_Inout_ Tls13Hs* hs, _In_ const Tls13Hello* h)
{
    TlsConfig* cfg = hs->config;
    if (cfg->st->alpnCount == 0)
        return true;

    const Tls13Ext* e = _tls13HelloExt(h, TLS13_EXT_ALPN);
    if (!e)
        return false;

    for (int32 i = 0; i < cfg->st->alpnCount; i++) {
        size_t want = strlen(cfg->st->alpn[i]);

        Tls13Rd rd, list;
        extRd(e, &rd);
        if (!_tls13RdVec16(&rd, &list))
            return false;

        while (_tls13RdLeft(&list) > 0) {
            Tls13Rd proto;
            if (!_tls13RdVec8(&list, &proto))
                return false;
            if (_tls13RdLeft(&proto) == want && memcmp(proto.p, cfg->st->alpn[i], want) == 0) {
                strFromBytes(&hs->alpnSel, (const uint8*)cfg->st->alpn[i], (uint32)want);
                return true;
            }
        }
    }

    return false;
}

// The single protocol a server echoes back in EncryptedExtensions.
static bool extReadALPNOne(_Inout_ Tls13Hs* hs, _In_ const Tls13Ext* e)
{
    Tls13Rd rd, list, proto;
    extRd(e, &rd);

    if (!_tls13RdVec16(&rd, &list) || !_tls13RdVec8(&list, &proto) ||
        _tls13RdLeft(&list) != 0 || _tls13RdLeft(&proto) == 0)
        return false;

    strFromBytes(&hs->alpnSel, proto.p, (uint32)_tls13RdLeft(&proto));
    return true;
}

static void extReadSNI(_Inout_ Tls13Hs* hs, _In_ const Tls13Hello* h)
{
    const Tls13Ext* e = _tls13HelloExt(h, TLS13_EXT_SERVER_NAME);
    if (!e)
        return;

    Tls13Rd rd, list, name;
    extRd(e, &rd);
    if (!_tls13RdVec16(&rd, &list) || _tls13Rd8(&list) != 0 || !_tls13RdVec16(&list, &name))
        return;

    if (_tls13RdLeft(&name) > 0)
        strFromBytes(&hs->hostname, name.p, (uint32)_tls13RdLeft(&name));
}

// ---------------------------------------------------------------------------------------------
// Key schedule wiring
// ---------------------------------------------------------------------------------------------

static bool hsHandshakeSecrets(_Inout_ Tls13Hs* hs, _In_reads_bytes_(len) const uint8* ecdhe,
                               size_t len)
{
    const uint8* psk = hs->pskAccepted ? hs->ticket.psk : NULL;
    size_t pskLen    = hs->pskAccepted ? hs->ticket.pskLen : 0;

    if (!_tls13SchedEarly(&hs->sched, hs->suite->hash, psk, pskLen) ||
        !_tls13SchedHandshake(&hs->sched, ecdhe, len))
        return false;

    uint8 thash[TLS13_MAX_HASH];
    if (!_tls13TranscriptHash(&hs->tr, thash) ||
        !_tls13SchedDerive(&hs->sched, _S"c hs traffic", thash, hs->suite->hashLen,
                           hs->clientHsSecret) ||
        !_tls13SchedDerive(&hs->sched, _S"s hs traffic", thash, hs->suite->hashLen,
                           hs->serverHsSecret) ||
        !_tls13SchedMaster(&hs->sched))
        return false;

    return hsSecrets(hs,
                     TLSQL_Handshake,
                     hs->server ? hs->clientHsSecret : hs->serverHsSecret,
                     hs->server ? hs->serverHsSecret : hs->clientHsSecret);
}

// Application traffic secrets, derived from the transcript through the server's Finished. The
// server only publishes its write half here: it has no business reading 1-RTT packets until the
// client's Finished has been checked, which is where the read half is handed over instead.
static bool hsAppSecrets(_Inout_ Tls13Hs* hs)
{
    uint8 thash[TLS13_MAX_HASH];
    uint8 srvAp[TLS13_MAX_HASH];

    if (!_tls13TranscriptHash(&hs->tr, thash) ||
        !_tls13SchedDerive(&hs->sched, _S"c ap traffic", thash, hs->suite->hashLen,
                           hs->clientApSecret) ||
        !_tls13SchedDerive(&hs->sched, _S"s ap traffic", thash, hs->suite->hashLen, srvAp))
        return false;

    if (hs->server)
        return hsSecrets(hs, TLSQL_App, NULL, srvAp);

    return hsSecrets(hs, TLSQL_App, srvAp, hs->clientApSecret);
}

static bool hsResumptionSecret(_Inout_ Tls13Hs* hs)
{
    uint8 thash[TLS13_MAX_HASH];
    return _tls13TranscriptHash(&hs->tr, thash) &&
           _tls13SchedDerive(&hs->sched, _S"res master", thash, hs->suite->hashLen,
                             hs->resumption);
}

// The 0-RTT traffic secret. It hangs off a key schedule of its own -- the ticket's PSK and nothing
// else -- keyed to the transcript through the ClientHello, which is the last thing both ends have
// in common at the moment a client has to protect data it has heard no answer to.
//
// `forRead` picks the direction: 0-RTT only ever flows from the client, so a client is handed the
// write half and a server the read half.
static bool hsEarlySecret(_Inout_ Tls13Hs* hs,
                          _In_reads_bytes_(TLS13_MAX_HASH) const uint8* chHash, bool forRead)
{
    Tls13Schedule early;
    if (!_tls13SchedEarly(&early, hs->suite->hash, hs->ticket.psk, hs->ticket.pskLen))
        return false;

    uint8 secret[TLS13_MAX_HASH];
    bool ok = _tls13SchedDerive(&early, _S"c e traffic", chHash, hs->suite->hashLen, secret);

    memset(&early, 0, sizeof(early));

    if (ok)
        ok = hsSecrets(hs, TLSQL_EarlyData, forRead ? secret : NULL, forRead ? NULL : secret);

    memset(secret, 0, sizeof(secret));
    return ok;
}

// Tell the transport what became of the early data, once.
static void hsEarlyDecided(_Inout_ Tls13Hs* hs, bool accepted)
{
    if (hs->earlyDecided)
        return;

    hs->earlyDecided  = true;
    hs->earlyAccepted = accepted;

    if (hs->handlers && hs->handlers->earlyData)
        hs->handlers->earlyData(hs->hctx, accepted);
}

static _Ret_maybenull_ Buffer hsBuildFinished(_Inout_ Tls13Hs* hs,
                                              _In_reads_bytes_(TLS13_MAX_HASH) const uint8* secret)
{
    uint8 thash[TLS13_MAX_HASH];
    uint8 verify[TLS13_MAX_HASH];

    if (!_tls13TranscriptHash(&hs->tr, thash) ||
        !_tls13Finished(hs->suite->hash, secret, thash, hs->suite->hashLen, verify))
        return NULL;

    Tls13Wr wr;
    size_t mark;
    msgStart(&wr, TLS13_HS_FINISHED, &mark);
    _tls13WrBytes(&wr, verify, hs->suite->hashLen);
    return msgFinish(&wr, mark);
}

static bool hsCheckFinished(_Inout_ Tls13Hs* hs,
                            _In_reads_bytes_(TLS13_MAX_HASH) const uint8* secret,
                            _In_reads_bytes_(len) const uint8* body, size_t len)
{
    uint8 thash[TLS13_MAX_HASH];
    uint8 verify[TLS13_MAX_HASH];

    if (len != hs->suite->hashLen || !_tls13TranscriptHash(&hs->tr, thash) ||
        !_tls13Finished(hs->suite->hash, secret, thash, hs->suite->hashLen, verify))
        return false;

    return ctEqual(verify, body, len);
}

// The binder over a truncated ClientHello: HMAC under a key derived from the offered PSK alone,
// which is what lets a server tell a genuine offer from a replayed identity.
static bool hsBinder(_Inout_ Tls13Hs* hs, _In_reads_bytes_(len) const uint8* truncated, size_t len,
                     _Out_writes_bytes_(TLS13_MAX_HASH) uint8* out)
{
    Tls13Schedule early;
    if (!_tls13SchedEarly(&early, hs->suite->hash, hs->ticket.psk, hs->ticket.pskLen))
        return false;

    uint8 emptyHash[TLS13_MAX_HASH];
    size_t emptyLen = 0;
    if (psa_hash_compute(hs->suite->hash, NULL, 0, emptyHash, sizeof(emptyHash), &emptyLen) !=
        PSA_SUCCESS)
        return false;

    uint8 binderKey[TLS13_MAX_HASH];
    uint8 thash[TLS13_MAX_HASH];

    bool ok = _tls13SchedDerive(&early, _S"res binder", emptyHash, emptyLen, binderKey) &&
              hsPreHash(hs, truncated, len, thash) &&
              _tls13Finished(hs->suite->hash, binderKey, thash, hs->suite->hashLen, out);

    memset(binderKey, 0, sizeof(binderKey));
    memset(&early, 0, sizeof(early));

    return ok;
}

// ---------------------------------------------------------------------------------------------
// Shared message helpers
// ---------------------------------------------------------------------------------------------

// Parse a bare `Extension extensions<..>` vector into the extension array of a Tls13Hello, so that
// _tls13HelloExt() can be used to look through it. Nothing else in the hello is filled in.
static bool extListParse(_Out_ Tls13Hello* h, _Inout_ Tls13Rd* rd)
{
    memset(h, 0, sizeof(*h));

    Tls13Rd exts;
    if (!_tls13RdVec16(rd, &exts))
        return false;

    while (_tls13RdLeft(&exts) > 0) {
        uint16 type = _tls13Rd16(&exts);

        Tls13Rd data;
        if (!_tls13RdVec16(&exts, &data) ||
            !_tls13HelloAddExt(h, type, data.p, (uint16)_tls13RdLeft(&data)))
            return false;
    }

    return !exts.bad;
}

static bool hsGroupSupported(_In_ const Tls13Hs* hs, uint16 group)
{
    for (uint8 i = 0; i < hs->ngroups; i++) {
        if (hs->groups[i] == group)
            return true;
    }
    return false;
}

// Certificate and CertificateVerify, as either end sends them. `serverSig` is the sender's role.
static bool hsSendCertificate(_Inout_ Tls13Hs* hs, _In_ TlsCreds* creds, bool serverSig)
{
    Tls13Wr wr;
    size_t mark;

    msgStart(&wr, TLS13_HS_CERTIFICATE, &mark);
    _tls13CertEncode(&wr, creds ? &creds->st->cert : NULL);

    Buffer msg = msgFinish(&wr, mark);
    if (!hsSend(hs, TLSQL_Handshake, &msg))
        return false;

    // An empty Certificate is a complete answer on its own: it says "I have no identity", and
    // there is nothing to prove possession of.
    if (!creds)
        return true;

    uint16 scheme;
    if (!_tls13SigSelect(&creds->st->key, hs->peerSchemes, hs->npeerSchemes, &scheme))
        return hsFail(hs, TLS13_ALERT_HANDSHAKE_FAILURE,
                      _S"no signature scheme the peer accepts fits this certificate's key");

    uint8 thash[TLS13_MAX_HASH];
    uint8 sig[MBEDTLS_PK_SIGNATURE_MAX_SIZE];
    size_t sigLen;

    if (!_tls13TranscriptHash(&hs->tr, thash) ||
        !_tls13SigMake(scheme, &creds->st->key, serverSig, thash, hs->suite->hashLen, sig,
                       sizeof(sig), &sigLen))
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not sign the handshake transcript");

    msgStart(&wr, TLS13_HS_CERTIFICATE_VERIFY, &mark);
    _tls13Wr16(&wr, scheme);
    size_t sigMark = _tls13WrOpen16(&wr);
    _tls13WrBytes(&wr, sig, sigLen);
    _tls13WrClose16(&wr, sigMark);

    msg = msgFinish(&wr, mark);
    return hsSend(hs, TLSQL_Handshake, &msg);
}

static bool hsRecvCertificate(_Inout_ Tls13Hs* hs, _In_reads_bytes_(len) const uint8* msg,
                              size_t len)
{
    if (!hsTrUpdate(hs, msg, len))
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"transcript update");

    bool empty;
    if (!_tls13CertParse(&hs->peerCert, msg + 4, len - 4, &empty))
        return hsFail(hs, TLS13_ALERT_BAD_CERTIFICATE, _S"peer certificate did not parse");

    if (empty)
        return true;

    hs->peerPresent = true;

    // A client checks the name it dialed; a server has no name to check a client certificate
    // against, so it checks the chain alone.
    strref name = hs->server ? NULL : hs->hostname;
    if (!_tls13ChainVerify(hs->config, &hs->peerCert, name, &hs->verifyFlags))
        return hsFail(hs, TLS13_ALERT_BAD_CERTIFICATE, _S"peer certificate did not verify");

    // TLSAUTH_None checks nothing, so there is nothing to report as verified.
    hs->peerVerified = hs->config->st->authMode != TLSAUTH_None;
    return true;
}

static bool hsRecvCertVerify(_Inout_ Tls13Hs* hs, _In_reads_bytes_(len) const uint8* msg,
                             size_t len)
{
    if (!hs->peerPresent)
        return hsFail(hs, TLS13_ALERT_UNEXPECTED_MESSAGE,
                      _S"a CertificateVerify with no certificate before it");

    Tls13Rd rd;
    _tls13RdInit(&rd, msg + 4, len - 4);

    uint16 scheme = _tls13Rd16(&rd);

    Tls13Rd sig;
    if (!_tls13RdVec16(&rd, &sig) || _tls13RdLeft(&rd) != 0)
        return hsFail(hs, TLS13_ALERT_DECODE_ERROR, _S"malformed CertificateVerify");

    // The signature covers everything up to but not including this message.
    uint8 thash[TLS13_MAX_HASH];
    if (!_tls13TranscriptHash(&hs->tr, thash))
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"transcript hash");

    if (!_tls13SigCheck(scheme, &hs->peerCert.pk, !hs->server, thash, hs->suite->hashLen, sig.p,
                        _tls13RdLeft(&sig)))
        return hsFail(hs, TLS13_ALERT_DECRYPT_ERROR, _S"CertificateVerify signature is wrong");

    if (!hsTrUpdate(hs, msg, len))
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"transcript update");

    return true;
}

// ---------------------------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------------------------

static bool clientHello(_Inout_ Tls13Hs* hs)
{
    TlsConfig* cfg = hs->config;

    Tls13Hello h;
    memset(&h, 0, sizeof(h));

    h.legacyVersion = TLS13_LEGACY_VERSION;
    memcpy(h.random, hs->random, sizeof(h.random));

    // QUIC bans the TLS 1.3 middlebox compatibility mode, so the legacy session id stays empty
    // (RFC 9001 section 8.4).
    h.sessionIdLen = 0;

    for (uint8 i = 0; i < hs->nsuites; i++)
        h.suites[h.nsuites++] = hs->suites[i];

    ExtPool pool = { 0 };

    bool ok = extVersions(&h, &pool, true) && extGroups(&h, &pool, hs) && extSigAlgs(&h, &pool);

    if (ok && !strEmpty(hs->hostname))
        ok = extSNI(&h, &pool, hs->hostname);
    if (ok && cfg->st->alpnCount > 0)
        ok = extALPN(&h, &pool, cfg);
    if (ok)
        ok = extKeyShareClient(&h, &pool, hs);
    if (ok)
        ok = extBytes(&h, &pool, TLS13_EXT_QUIC_TRANSPORT_PARAMS, hs->tp->data, hs->tp->len);
    if (ok && hs->cookie)
        ok = extCookie(&h, &pool, hs->cookie);

    // Announcing 0-RTT is an empty extension; what it means is carried by the ticket the
    // pre_shared_key extension names.
    if (ok && hs->earlyOffered)
        ok = _tls13HelloAddExt(&h, TLS13_EXT_EARLY_DATA, NULL, 0);

    // pre_shared_key has to be the last extension in the hello, because the binder is computed
    // over everything in front of it.
    if (ok && hs->pskOffered)
        ok = extPskModes(&h, &pool) && extPreSharedKey(&h, &pool, hs);

    Buffer msg = ok ? _tls13HelloEncode(&h, false) : NULL;
    extPoolDestroy(&pool);

    if (!msg)
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not build a ClientHello");

    if (hs->pskOffered) {
        // The binder sits at the very end: a 2-byte list length, a 1-byte entry length, and the
        // value itself, so the bytes it covers are everything before those.
        size_t hl = hs->suite->hashLen;
        uint8 binder[TLS13_MAX_HASH];

        if (msg->len < hl + 3 || !hsBinder(hs, msg->data, msg->len - (hl + 3), binder)) {
            bufDestroy(&msg);
            return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not compute the PSK binder");
        }

        memcpy(msg->data + msg->len - hl, binder, hl);
    }

    // The 0-RTT keys exist from here, before a single byte has come back. Derived from the
    // finished ClientHello, binder included, because that is what the server will hash.
    if (hs->earlyOffered) {
        uint8 chHash[TLS13_MAX_HASH];
        bool ok2 = hsPreHash(hs, msg->data, msg->len, chHash) && hsEarlySecret(hs, chHash, false);
        memset(chHash, 0, sizeof(chHash));

        if (!ok2) {
            bufDestroy(&msg);
            return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not derive the 0-RTT secret");
        }
    }

    return hsSend(hs, TLSQL_Initial, &msg);
}

static bool clientRetry(_Inout_ Tls13Hs* hs, _In_ const Tls13Hello* h,
                        _In_reads_bytes_(len) const uint8* msg, size_t len)
{
    if (hs->hrrDone)
        return hsFail(hs, TLS13_ALERT_UNEXPECTED_MESSAGE, _S"a second HelloRetryRequest");
    hs->hrrDone = true;

    // A retry throws away the ClientHello the 0-RTT keys were derived from, so whatever was sent
    // under them is gone and the second hello may not ask again (RFC 8446 section 4.2.10).
    if (hs->earlyOffered) {
        hs->earlyOffered = false;
        hsEarlyDecided(hs, false);
    }

    const Tls13Ext* ks = _tls13HelloExt(h, TLS13_EXT_KEY_SHARE);
    if (!ks || ks->len != 2)
        return hsFail(hs, TLS13_ALERT_ILLEGAL_PARAMETER,
                      _S"HelloRetryRequest without a group to retry with");

    Tls13Rd rd;
    extRd(ks, &rd);
    uint16 group = _tls13Rd16(&rd);

    // Retrying with the group already offered would loop forever and gains the server nothing.
    if (group == hs->group || !hsGroupSupported(hs, group))
        return hsFail(hs, TLS13_ALERT_ILLEGAL_PARAMETER,
                      _S"HelloRetryRequest named a group that was not offered");

    const Tls13Ext* ck = _tls13HelloExt(h, TLS13_EXT_COOKIE);
    if (ck) {
        Tls13Rd crd, cookie;
        extRd(ck, &crd);
        if (!_tls13RdVec16(&crd, &cookie) || _tls13RdLeft(&cookie) == 0)
            return hsFail(hs, TLS13_ALERT_DECODE_ERROR, _S"malformed cookie");

        bufDestroy(&hs->cookie);
        hs->cookie = bufCreate(_tls13RdLeft(&cookie));
        memcpy(hs->cookie->data, cookie.p, _tls13RdLeft(&cookie));
        hs->cookie->len = _tls13RdLeft(&cookie);
    }

    if (!hsTrCollapse(hs, NULL, 0) || !hsTrUpdate(hs, msg, len))
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"transcript update");

    _tls13KexDestroy(&hs->kex);
    hs->group = group;
    if (!_tls13KexGenerate(&hs->kex, group))
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not generate a key share");

    return clientHello(hs);
}

static bool clientServerHello(_Inout_ Tls13Hs* hs, _In_reads_bytes_(len) const uint8* msg,
                              size_t len)
{
    Tls13Hello h;
    if (!_tls13HelloParse(&h, true, msg, len))
        return hsFail(hs, TLS13_ALERT_DECODE_ERROR, _S"malformed ServerHello");

    if (!extCheckVersion(&h))
        return hsFail(hs, TLS13_ALERT_PROTOCOL_VERSION, _S"the server did not select TLS 1.3");

    const Tls13Suite* suite = _tls13Suite(h.suites[0]);
    if (!suite)
        return hsFail(hs, TLS13_ALERT_ILLEGAL_PARAMETER,
                      _S"the server selected a cipher suite that was not offered");

    // After a HelloRetryRequest the server is bound to the suite it named there.
    if (hs->hrrDone && hs->suite && hs->suite->id != suite->id)
        return hsFail(hs, TLS13_ALERT_ILLEGAL_PARAMETER,
                      _S"the server changed cipher suite after a HelloRetryRequest");

    hs->suite = suite;

    if (memcmp(h.random, hrrRandom, sizeof(hrrRandom)) == 0)
        return clientRetry(hs, &h, msg, len);

    const Tls13Ext* psk = _tls13HelloExt(&h, TLS13_EXT_PRE_SHARED_KEY);
    if (psk) {
        Tls13Rd rd;
        extRd(psk, &rd);

        if (!hs->pskOffered || psk->len != 2 || _tls13Rd16(&rd) != 0 ||
            _tls13Suite(hs->ticket.suite) != suite)
            return hsFail(hs, TLS13_ALERT_ILLEGAL_PARAMETER,
                          _S"the server accepted a pre-shared key that was not offered");

        hs->pskAccepted = true;
    }

    const Tls13Ext* ks = _tls13HelloExt(&h, TLS13_EXT_KEY_SHARE);
    if (!ks)
        return hsFail(hs, TLS13_ALERT_MISSING_EXTENSION, _S"ServerHello without a key share");

    Tls13Rd rd, ke;
    extRd(ks, &rd);
    uint16 group = _tls13Rd16(&rd);
    if (!_tls13RdVec16(&rd, &ke) || _tls13RdLeft(&rd) != 0)
        return hsFail(hs, TLS13_ALERT_DECODE_ERROR, _S"malformed key share");

    if (group != hs->group)
        return hsFail(hs, TLS13_ALERT_ILLEGAL_PARAMETER,
                      _S"the server's key share is for a different group");

    uint8 shared[TLS13_MAX_KEXPUB];
    size_t sharedLen;
    bool ok = _tls13KexAgree(&hs->kex, ke.p, _tls13RdLeft(&ke), shared, sizeof(shared), &sharedLen);

    if (ok)
        ok = hsTrStart(hs) && hsTrUpdate(hs, msg, len) && hsHandshakeSecrets(hs, shared, sharedLen);

    memset(shared, 0, sizeof(shared));

    if (!ok)
        return hsFail(hs, TLS13_ALERT_ILLEGAL_PARAMETER, _S"key agreement failed");

    hs->state = TLS13_ST_WAIT_EE;
    return true;
}

static bool clientEncryptedExts(_Inout_ Tls13Hs* hs, _In_reads_bytes_(len) const uint8* msg,
                                size_t len)
{
    Tls13Rd rd;
    _tls13RdInit(&rd, msg + 4, len - 4);

    Tls13Hello exts;
    if (!extListParse(&exts, &rd) || _tls13RdLeft(&rd) != 0)
        return hsFail(hs, TLS13_ALERT_DECODE_ERROR, _S"malformed EncryptedExtensions");

    if (!hsTrUpdate(hs, msg, len))
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"transcript update");

    // QUIC requires the transport parameters, and requires them here rather than in the hello.
    if (!extReadTransportParams(hs, &exts))
        return hsFail(hs, TLS13_ALERT_MISSING_EXTENSION,
                      _S"the server sent no QUIC transport parameters");

    const Tls13Ext* alpn = _tls13HelloExt(&exts, TLS13_EXT_ALPN);
    if (alpn) {
        if (!extReadALPNOne(hs, alpn))
            return hsFail(hs, TLS13_ALERT_DECODE_ERROR, _S"malformed ALPN selection");
    } else if (hs->config->st->alpnCount > 0) {
        return hsFail(hs, TLS13_ALERT_NO_APPLICATION_PROTOCOL,
                      _S"the server selected no application protocol");
    }

    const Tls13Ext* early = _tls13HelloExt(&exts, TLS13_EXT_EARLY_DATA);
    if (early && !hs->earlyOffered)
        return hsFail(hs, TLS13_ALERT_UNSUPPORTED_EXTENSION,
                      _S"the server accepted early data that was never offered");

    // Early data was protected under the protocol of the session being resumed, so a server that
    // takes it and then names a different one has changed what those bytes meant.
    if (early && !strEq(hs->alpnSel, hs->ticket.alpn))
        return hsFail(hs, TLS13_ALERT_ILLEGAL_PARAMETER,
                      _S"the server took early data under a different application protocol");

    if (hs->earlyOffered)
        hsEarlyDecided(hs, early != NULL);

    // A resumed handshake carries the identity forward and sends no certificate, so Finished is
    // the only thing left to expect.
    hs->state = hs->pskAccepted ? TLS13_ST_WAIT_SRV_FIN : TLS13_ST_WAIT_CERT_CR;
    return true;
}

static bool clientCertRequest(_Inout_ Tls13Hs* hs, _In_reads_bytes_(len) const uint8* msg,
                              size_t len)
{
    Tls13Rd rd, reqctx;
    _tls13RdInit(&rd, msg + 4, len - 4);

    if (!_tls13RdVec8(&rd, &reqctx))
        return hsFail(hs, TLS13_ALERT_DECODE_ERROR, _S"malformed CertificateRequest");

    Tls13Hello exts;
    if (!extListParse(&exts, &rd) || _tls13RdLeft(&rd) != 0)
        return hsFail(hs, TLS13_ALERT_DECODE_ERROR, _S"malformed CertificateRequest");

    if (!hsTrUpdate(hs, msg, len))
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"transcript update");

    // The schemes the client may sign with come from here, replacing whatever the ServerHello
    // flight said, since this is the message that governs the client's own signature.
    extReadSigAlgs(hs, &exts);
    if (hs->npeerSchemes == 0)
        return hsFail(hs, TLS13_ALERT_MISSING_EXTENSION,
                      _S"CertificateRequest without signature_algorithms");

    hs->certRequested = true;
    hs->state         = TLS13_ST_WAIT_CERT;
    return true;
}

static bool clientFinish(_Inout_ Tls13Hs* hs, _In_reads_bytes_(len) const uint8* msg, size_t len)
{
    if (!hsCheckFinished(hs, hs->serverHsSecret, msg + 4, len - 4))
        return hsFail(hs, TLS13_ALERT_DECRYPT_ERROR, _S"the server's Finished is wrong");

    if (!hsTrUpdate(hs, msg, len) || !hsAppSecrets(hs))
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not derive application secrets");

    if (hs->certRequested) {
        // An empty Certificate is the right answer when there is nothing to present; whether the
        // server accepts that is its decision, not the client's.
        if (!hsSendCertificate(hs, hs->config->creds, false))
            return false;
    }

    Buffer fin = hsBuildFinished(hs, hs->clientHsSecret);
    if (!hsSend(hs, TLSQL_Handshake, &fin))
        return false;

    if (!hsResumptionSecret(hs))
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not derive the resumption secret");

    hs->state = TLS13_ST_DONE;
    if (hs->handlers && hs->handlers->complete)
        hs->handlers->complete(hs->hctx);

    return true;
}

// A ticket the server handed out after the handshake. Kept against the hostname it came from, so
// the next connection to the same server can offer it back.
static bool clientNewTicket(_Inout_ Tls13Hs* hs, _In_reads_bytes_(len) const uint8* msg, size_t len)
{
    Tls13Rd rd;
    _tls13RdInit(&rd, msg + 4, len - 4);

    Tls13Ticket t;
    memset(&t, 0, sizeof(t));

    t.lifetime = _tls13Rd32(&rd);
    t.ageAdd   = _tls13Rd32(&rd);

    Tls13Rd nonce, id;
    if (!_tls13RdVec8(&rd, &nonce) || !_tls13RdVec16(&rd, &id) || _tls13RdLeft(&id) == 0)
        return hsFail(hs, TLS13_ALERT_DECODE_ERROR, _S"malformed NewSessionTicket");

    Tls13Hello exts;
    if (!extListParse(&exts, &rd) || _tls13RdLeft(&rd) != 0)
        return hsFail(hs, TLS13_ALERT_DECODE_ERROR, _S"malformed NewSessionTicket");

    // A lifetime over a week is not usable per RFC 8446; treat it as a ticket not worth keeping
    // rather than as a protocol violation.
    if (t.lifetime == 0 || t.lifetime > TLS13_MAX_TICKET_LIFETIME)
        return true;

    const Tls13Ext* early = _tls13HelloExt(&exts, TLS13_EXT_EARLY_DATA);
    if (early) {
        Tls13Rd ed;
        if (!extRd(early, &ed))
            return hsFail(hs, TLS13_ALERT_DECODE_ERROR,
                          _S"malformed early_data in a NewSessionTicket");

        uint32 max = _tls13Rd32(&ed);
        if (ed.bad || _tls13RdLeft(&ed) != 0)
            return hsFail(hs, TLS13_ALERT_DECODE_ERROR,
                          _S"malformed early_data in a NewSessionTicket");

        // RFC 9001 section 4.6.1: QUIC bounds early data with its own flow control, so the only
        // size a QUIC server may name is the one that means "no limit of mine".
        if (max != TLS13_QUIC_MAX_EARLY_DATA)
            return hsFail(hs, TLS13_ALERT_ILLEGAL_PARAMETER,
                          _S"a max_early_data_size QUIC does not allow");

        t.maxEarlyData = max;
    }

    t.suite  = hs->suite->id;
    t.pskLen = hs->suite->hashLen;
    t.issued = clockWall();

    if (!_tls13ExpandLabel(hs->suite->hash, hs->resumption, hs->suite->hashLen, _S"resumption",
                           nonce.p, _tls13RdLeft(&nonce), t.psk, t.pskLen))
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not derive the resumption key");

    t.id = bufCreate(_tls13RdLeft(&id));
    memcpy(t.id->data, id.p, _tls13RdLeft(&id));
    t.id->len = _tls13RdLeft(&id);

    strDup(&t.alpn, hs->alpnSel);

    // The server's limits go in the ticket too: early data on the next connection is sent under
    // them, before there is anything else to go on.
    if (hs->peerTp) {
        t.tp = bufCreate(hs->peerTp->len);
        memcpy(t.tp->data, hs->peerTp->data, hs->peerTp->len);
        t.tp->len = hs->peerTp->len;
    }

    Buffer flat = _tls13TicketSerialize(&t);
    _tls13TicketDestroy(&t);

    _tlsconfigSaveQuicTicket(hs->config, hs->hostname, &flat);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------------------------

// Post-handshake messages are sent without touching the transcript: nothing derived after the
// client's Finished depends on it, and QUIC bans the two messages that would.
static bool hsSendPost(_Inout_ Tls13Hs* hs, _Inout_ Buffer* msg)
{
    if (!*msg)
        return false;

    bool ok = hs->handlers && hs->handlers->sendCrypto &&
              hs->handlers->sendCrypto(hs->hctx, TLSQL_App, (*msg)->data, (*msg)->len);

    bufDestroy(msg);
    return ok;
}

static bool serverSendHello(_Inout_ Tls13Hs* hs, bool retry, uint16 retryGroup)
{
    Tls13Hello h;
    memset(&h, 0, sizeof(h));

    h.legacyVersion = TLS13_LEGACY_VERSION;
    h.sessionIdLen  = 0;
    h.suites[0]     = hs->suite->id;
    h.nsuites       = 1;

    if (retry) {
        memcpy(h.random, hrrRandom, sizeof(hrrRandom));
    } else if (psa_generate_random(h.random, sizeof(h.random)) != PSA_SUCCESS) {
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not generate a server random");
    }

    ExtPool pool = { 0 };
    bool ok      = extVersions(&h, &pool, false);

    if (ok)
        ok = retry ? extKeyShareRetry(&h, &pool, retryGroup) : extKeyShareServer(&h, &pool, hs);

    if (ok && !retry && hs->pskAccepted) {
        // selected_identity: always the first offer, because that is the only one considered.
        Tls13Wr wr;
        _tls13WrInit(&wr, 8);
        _tls13Wr16(&wr, 0);
        ok = extPush(&h, &pool, TLS13_EXT_PRE_SHARED_KEY, &wr);
    }

    Buffer msg = ok ? _tls13HelloEncode(&h, true) : NULL;
    extPoolDestroy(&pool);

    if (!msg)
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not build a ServerHello");

    return hsSend(hs, TLSQL_Initial, &msg);
}

static bool serverSendEncryptedExts(_Inout_ Tls13Hs* hs)
{
    Tls13Wr wr;
    size_t mark;
    msgStart(&wr, TLS13_HS_ENCRYPTED_EXTENSIONS, &mark);

    size_t exts = _tls13WrOpen16(&wr);

    _tls13Wr16(&wr, TLS13_EXT_QUIC_TRANSPORT_PARAMS);
    size_t tp = _tls13WrOpen16(&wr);
    _tls13WrBytes(&wr, hs->tp->data, hs->tp->len);
    _tls13WrClose16(&wr, tp);

    if (!strEmpty(hs->alpnSel)) {
        _tls13Wr16(&wr, TLS13_EXT_ALPN);
        size_t ext  = _tls13WrOpen16(&wr);
        size_t list = _tls13WrOpen16(&wr);
        _tls13Wr8(&wr, (uint8)strLen(hs->alpnSel));
        _tls13WrBytes(&wr, (const uint8*)strC(hs->alpnSel), strLen(hs->alpnSel));
        _tls13WrClose16(&wr, list);
        _tls13WrClose16(&wr, ext);
    }

    // Taking the early data is said here and nowhere else: an empty extension, whose meaning is
    // that the client's 0-RTT packets are being read rather than thrown away.
    if (hs->earlyAccepted) {
        _tls13Wr16(&wr, TLS13_EXT_EARLY_DATA);
        size_t ed = _tls13WrOpen16(&wr);
        _tls13WrClose16(&wr, ed);
    }

    _tls13WrClose16(&wr, exts);

    Buffer msg = msgFinish(&wr, mark);
    return hsSend(hs, TLSQL_Handshake, &msg);
}

static bool serverSendCertRequest(_Inout_ Tls13Hs* hs)
{
    Tls13Wr wr;
    size_t mark;
    msgStart(&wr, TLS13_HS_CERTIFICATE_REQUEST, &mark);

    _tls13Wr8(&wr, 0);   // certificate_request_context, empty during the handshake

    size_t exts = _tls13WrOpen16(&wr);
    _tls13Wr16(&wr, TLS13_EXT_SIGNATURE_ALGORITHMS);
    size_t ext  = _tls13WrOpen16(&wr);
    size_t list = _tls13WrOpen16(&wr);
    for (size_t i = 0; i < NELEM(sigPref); i++)
        _tls13Wr16(&wr, sigPref[i]);
    _tls13WrClose16(&wr, list);
    _tls13WrClose16(&wr, ext);
    _tls13WrClose16(&wr, exts);

    Buffer msg = msgFinish(&wr, mark);
    return hsSend(hs, TLSQL_Handshake, &msg);
}

static void serverSendTicket(_Inout_ Tls13Hs* hs)
{
    uint8 key[32];
    if (!_tlsconfigQuicTicketKey(hs->config, key))
        return;

    uint8 nonce[8];
    uint32 ageAdd;
    if (psa_generate_random(nonce, sizeof(nonce)) != PSA_SUCCESS ||
        psa_generate_random((uint8*)&ageAdd, sizeof(ageAdd)) != PSA_SUCCESS)
        return;

    bool early     = _tlsconfigEarlyData(hs->config);
    int64 lifetime = timeToSeconds(_tlsconfigResumeLifetime(hs->config));
    if (lifetime <= 0 || lifetime > TLS13_MAX_TICKET_LIFETIME)
        lifetime = TLS13_MAX_TICKET_LIFETIME;

    Tls13Ticket t;
    memset(&t, 0, sizeof(t));

    t.suite    = hs->suite->id;
    t.pskLen   = hs->suite->hashLen;
    t.ageAdd   = ageAdd;
    t.lifetime = (uint32)lifetime;
    t.issued   = clockWall();
    strDup(&t.alpn, hs->alpnSel);

    t.maxEarlyData = early ? TLS13_QUIC_MAX_EARLY_DATA : 0;

    // The limits this handshake offered, sealed into the ticket so that the connection resuming it
    // can be checked against them rather than trusted to remember the same thing.
    if (early && hs->tp) {
        t.tp = bufCreate(hs->tp->len);
        memcpy(t.tp->data, hs->tp->data, hs->tp->len);
        t.tp->len = hs->tp->len;
    }

    Buffer sealed = NULL;
    if (_tls13ExpandLabel(hs->suite->hash, hs->resumption, hs->suite->hashLen, _S"resumption",
                          nonce, sizeof(nonce), t.psk, t.pskLen))
        sealed = _tls13TicketSeal(key, &t);

    memset(key, 0, sizeof(key));
    _tls13TicketDestroy(&t);

    // A ticket that could not be built is not a handshake failure: the connection is up, and the
    // next one simply runs a full handshake.
    if (!sealed)
        return;

    Tls13Wr wr;
    size_t mark;
    msgStart(&wr, TLS13_HS_NEW_SESSION_TICKET, &mark);

    _tls13Wr32(&wr, (uint32)lifetime);
    _tls13Wr32(&wr, ageAdd);

    size_t nm = _tls13WrOpen8(&wr);
    _tls13WrBytes(&wr, nonce, sizeof(nonce));
    _tls13WrClose8(&wr, nm);

    size_t tm = _tls13WrOpen16(&wr);
    _tls13WrBytes(&wr, sealed->data, sealed->len);
    _tls13WrClose16(&wr, tm);

    size_t em = _tls13WrOpen16(&wr);
    if (early) {
        _tls13Wr16(&wr, TLS13_EXT_EARLY_DATA);
        size_t ed = _tls13WrOpen16(&wr);
        _tls13Wr32(&wr, TLS13_QUIC_MAX_EARLY_DATA);
        _tls13WrClose16(&wr, ed);
    }
    _tls13WrClose16(&wr, em);

    bufDestroy(&sealed);

    Buffer msg = msgFinish(&wr, mark);
    hsSendPost(hs, &msg);
}

// Consider the client's pre_shared_key offer. A ticket that does not open, or belongs to another
// cipher suite, simply means a full handshake; only a *selected* identity whose binder is wrong is
// an error, since that is the one case where the client proved nothing.
static bool serverTryPsk(_Inout_ Tls13Hs* hs, _In_ const Tls13Hello* h,
                         _In_reads_bytes_(len) const uint8* msg, size_t len)
{
    const Tls13Ext* pskExt = _tls13HelloExt(h, TLS13_EXT_PRE_SHARED_KEY);
    const Tls13Ext* modes  = _tls13HelloExt(h, TLS13_EXT_PSK_KEY_EXCHANGE_MODES);
    if (!pskExt || !modes)
        return true;

    bool dhe = false;
    Tls13Rd mrd, mlist;
    extRd(modes, &mrd);
    if (_tls13RdVec8(&mrd, &mlist)) {
        while (_tls13RdLeft(&mlist) > 0)
            dhe |= _tls13Rd8(&mlist) == 1;
    }
    if (!dhe)
        return true;

    uint8 key[32];
    if (!_tlsconfigQuicTicketKey(hs->config, key))
        return true;

    Tls13Rd rd, ids, id, binders, binder;
    extRd(pskExt, &rd);

    // Every sub-reader starts empty at the head of the extension, so the offset arithmetic below
    // stays inside the message even when one of the reads fails.
    _tls13RdInit(&ids, rd.p, 0);
    _tls13RdInit(&id, rd.p, 0);
    _tls13RdInit(&binders, rd.p, 0);
    _tls13RdInit(&binder, rd.p, 0);

    // Only the first identity is ever considered, since that is the only one this server issues.
    bool ok = _tls13RdVec16(&rd, &ids) && _tls13RdVec16(&ids, &id) &&
              _tls13RdVec16(&rd, &binders);

    // The binder covers the ClientHello up to the binder list's own length prefix, which sits two
    // bytes ahead of the list contents. Taken before the first binder is read, because reading it
    // advances the list.
    size_t truncated = ok ? (size_t)(binders.p - msg) - 2 : 0;

    ok = ok && _tls13RdVec8(&binders, &binder);

    if (!ok || _tls13RdLeft(&id) == 0) {
        memset(key, 0, sizeof(key));
        return hsFail(hs, TLS13_ALERT_DECODE_ERROR, _S"malformed pre_shared_key");
    }

    Tls13Ticket t;
    bool opened = _tls13TicketOpen(key, id.p, _tls13RdLeft(&id), &t);
    memset(key, 0, sizeof(key));

    if (!opened)
        return true;

    if (_tls13Suite(t.suite) != hs->suite) {
        _tls13TicketDestroy(&t);
        return true;
    }

    _tls13TicketDestroy(&hs->ticket);
    hs->ticket = t;

    uint8 expect[TLS13_MAX_HASH];
    if (!hsBinder(hs, msg, truncated, expect))
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not compute the PSK binder");

    if (_tls13RdLeft(&binder) != hs->suite->hashLen ||
        !ctEqual(expect, binder.p, hs->suite->hashLen))
        return hsFail(hs, TLS13_ALERT_DECRYPT_ERROR, _S"the PSK binder is wrong");

    hs->pskAccepted = true;
    return true;
}

// Whether to read the 0-RTT this client offered. Called with the transcript holding exactly the
// ClientHello, which is what the early secret is keyed to and so the only moment the keys can be
// derived.
//
// Everything tested here is a way the client's bytes could mean something other than what this
// server would make of them: a ticket that was not taken, a retry that replaced the hello they
// were keyed to, a different application protocol, or limits that have shrunk since the session
// the client is remembering.
static bool serverEarlyDecide(_Inout_ Tls13Hs* hs)
{
    if (!hs->earlyOffered)
        return true;

    bool accept = hs->pskAccepted && !hs->hrrDone && _tlsconfigEarlyData(hs->config) &&
                  hs->ticket.maxEarlyData != 0 && strEq(hs->alpnSel, hs->ticket.alpn);

    // A transport with no opinion gets no early data: whether the old limits are still on offer
    // is not something the handshake can answer for it.
    if (accept) {
        accept = hs->ticket.tp && hs->handlers && hs->handlers->earlyParams &&
                 hs->handlers->earlyParams(hs->hctx, hs->ticket.tp->data, hs->ticket.tp->len);
    }

    if (accept) {
        uint8 chHash[TLS13_MAX_HASH];
        bool ok = _tls13TranscriptHash(&hs->tr, chHash) && hsEarlySecret(hs, chHash, true);
        memset(chHash, 0, sizeof(chHash));

        if (!ok)
            return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not derive the 0-RTT secret");
    }

    hsEarlyDecided(hs, accept);
    return true;
}

static bool serverFlight(_Inout_ Tls13Hs* hs)
{
    TlsConfig* cfg = hs->config;

    if (!serverSendEncryptedExts(hs))
        return false;

    if (!hs->pskAccepted) {
        hs->certRequested = cfg->st->authMode != TLSAUTH_None;
        if (hs->certRequested && !serverSendCertRequest(hs))
            return false;

        if (!hs->creds)
            return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR,
                          _S"the server configuration has no certificate to present");

        if (!hsSendCertificate(hs, hs->creds, true))
            return false;
    }

    Buffer fin = hsBuildFinished(hs, hs->serverHsSecret);
    if (!hsSend(hs, TLSQL_Handshake, &fin))
        return false;

    if (!hsAppSecrets(hs))
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not derive application secrets");

    hs->state = hs->certRequested ? TLS13_ST_WAIT_CLI_CERT : TLS13_ST_WAIT_CLI_FIN;
    return true;
}

static bool serverClientHello(_Inout_ Tls13Hs* hs, _In_reads_bytes_(len) const uint8* msg,
                              size_t len)
{
    TlsConfig* cfg = hs->config;

    Tls13Hello h;
    if (!_tls13HelloParse(&h, false, msg, len))
        return hsFail(hs, TLS13_ALERT_DECODE_ERROR, _S"malformed ClientHello");

    // RFC 9001 section 8.4: a QUIC client must not ask for TLS 1.3 compatibility mode.
    if (h.sessionIdLen != 0)
        return hsFail(hs, TLS13_ALERT_ILLEGAL_PARAMETER,
                      _S"the client asked for TLS compatibility mode, which QUIC forbids");

    if (!extOffersVersion(&h))
        return hsFail(hs, TLS13_ALERT_PROTOCOL_VERSION, _S"the client does not offer TLS 1.3");

    const Tls13Suite* suite = NULL;
    for (uint8 i = 0; i < hs->nsuites && !suite; i++) {
        for (uint8 j = 0; j < h.nsuites; j++) {
            if (h.suites[j] == hs->suites[i]) {
                suite = _tls13Suite(hs->suites[i]);
                break;
            }
        }
    }

    if (!suite)
        return hsFail(hs, TLS13_ALERT_HANDSHAKE_FAILURE, _S"no cipher suite in common");

    if (hs->hrrDone && hs->suite != suite)
        return hsFail(hs, TLS13_ALERT_ILLEGAL_PARAMETER,
                      _S"the client changed cipher suite after a HelloRetryRequest");

    hs->suite = suite;

    extReadSigAlgs(hs, &h);
    if (hs->npeerSchemes == 0)
        return hsFail(hs, TLS13_ALERT_MISSING_EXTENSION,
                      _S"ClientHello without signature_algorithms");

    strDestroy(&hs->hostname);
    extReadSNI(hs, &h);

    hs->creds = cfg->creds;
    if (cfg->st->sniCb && !strEmpty(hs->hostname)) {
        TlsCreds* picked = cfg->st->sniCb(hs->hostname, cfg->st->sniCtx);
        if (picked)
            hs->creds = picked;
    }

    if (!extSelectALPN(hs, &h))
        return hsFail(hs, TLS13_ALERT_NO_APPLICATION_PROTOCOL,
                      _S"no application protocol in common");

    if (!extReadTransportParams(hs, &h))
        return hsFail(hs, TLS13_ALERT_MISSING_EXTENSION,
                      _S"the client sent no QUIC transport parameters");

    // A retry replaces the ClientHello any early data was keyed to, so the second one may not ask
    // for it again (RFC 8446 section 4.2.10).
    hs->earlyOffered = _tls13HelloExt(&h, TLS13_EXT_EARLY_DATA) != NULL;
    if (hs->earlyOffered && hs->hrrDone)
        return hsFail(hs, TLS13_ALERT_ILLEGAL_PARAMETER,
                      _S"early_data in a second ClientHello");

    // Each ClientHello carries its own binder, so a second one has to be judged from scratch.
    hs->pskAccepted = false;
    if (!serverTryPsk(hs, &h, msg, len))
        return false;

    // Prefer a group the client has already sent a key share for; falling back to a group it only
    // listed costs the extra round trip a HelloRetryRequest is.
    const Tls13Ext* ks = _tls13HelloExt(&h, TLS13_EXT_KEY_SHARE);
    uint16 group       = 0;
    Tls13Rd share;
    _tls13RdInit(&share, NULL, 0);

    for (uint8 i = 0; i < hs->ngroups && !group; i++) {
        if (!ks)
            break;

        Tls13Rd rd, list;
        extRd(ks, &rd);
        if (!_tls13RdVec16(&rd, &list))
            return hsFail(hs, TLS13_ALERT_DECODE_ERROR, _S"malformed key share");

        while (_tls13RdLeft(&list) > 0) {
            uint16 g = _tls13Rd16(&list);
            Tls13Rd ke;
            if (!_tls13RdVec16(&list, &ke))
                return hsFail(hs, TLS13_ALERT_DECODE_ERROR, _S"malformed key share");
            if (g == hs->groups[i]) {
                group = g;
                share = ke;
                break;
            }
        }
    }

    if (!group) {
        // Nothing usable was offered: retry with the best group the client says it supports.
        const Tls13Ext* sg = _tls13HelloExt(&h, TLS13_EXT_SUPPORTED_GROUPS);
        uint16 retry       = 0;

        for (uint8 i = 0; i < hs->ngroups && !retry && sg; i++) {
            Tls13Rd rd, list;
            extRd(sg, &rd);
            if (!_tls13RdVec16(&rd, &list))
                return hsFail(hs, TLS13_ALERT_DECODE_ERROR, _S"malformed supported_groups");

            while (_tls13RdLeft(&list) >= 2) {
                if (_tls13Rd16(&list) == hs->groups[i]) {
                    retry = hs->groups[i];
                    break;
                }
            }
        }

        if (!retry)
            return hsFail(hs, TLS13_ALERT_HANDSHAKE_FAILURE, _S"no key exchange group in common");

        if (hs->hrrDone)
            return hsFail(hs, TLS13_ALERT_ILLEGAL_PARAMETER,
                          _S"the client did not send the key share it was asked for");
        hs->hrrDone = true;

        if (!hsTrCollapse(hs, msg, len))
            return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"transcript update");

        if (!serverSendHello(hs, true, retry))
            return false;

        hs->state = TLS13_ST_WAIT_CH2;
        return true;
    }

    hs->group = group;
    if (!_tls13KexGenerate(&hs->kex, group))
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not generate a key share");

    uint8 shared[TLS13_MAX_KEXPUB];
    size_t sharedLen;
    bool ok = _tls13KexAgree(&hs->kex, share.p, _tls13RdLeft(&share), shared, sizeof(shared),
                             &sharedLen);

    // The early decision sits between starting the transcript and sending the ServerHello,
    // because the secret it derives is keyed to a transcript holding the ClientHello and nothing
    // after it.
    if (ok)
        ok = hsTrUpdate(hs, msg, len) && hsTrStart(hs) && serverEarlyDecide(hs) &&
             serverSendHello(hs, false, 0) && hsHandshakeSecrets(hs, shared, sharedLen);

    memset(shared, 0, sizeof(shared));

    if (!ok)
        return hs->state == TLS13_ST_FAILED
                   ? false
                   : hsFail(hs, TLS13_ALERT_ILLEGAL_PARAMETER, _S"key agreement failed");

    return serverFlight(hs);
}

static bool serverClientFinished(_Inout_ Tls13Hs* hs, _In_reads_bytes_(len) const uint8* msg,
                                 size_t len)
{
    if (hs->certRequested && hs->config->st->authMode == TLSAUTH_Required && !hs->peerPresent)
        return hsFail(hs, TLS13_ALERT_CERTIFICATE_REQUIRED,
                      _S"the client presented no certificate");

    if (!hsCheckFinished(hs, hs->clientHsSecret, msg + 4, len - 4))
        return hsFail(hs, TLS13_ALERT_DECRYPT_ERROR, _S"the client's Finished is wrong");

    if (!hsTrUpdate(hs, msg, len) || !hsResumptionSecret(hs))
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not derive the resumption secret");

    // The read half of the 1-RTT keys is only handed over now: until the Finished checked out
    // there was nothing to say the peer was who it claimed.
    if (!hsSecrets(hs, TLSQL_App, hs->clientApSecret, NULL))
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not install application secrets");

    hs->state = TLS13_ST_DONE;
    if (hs->handlers && hs->handlers->complete)
        hs->handlers->complete(hs->hctx);

    serverSendTicket(hs);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------------------------

static bool hsCertArrived(_Inout_ Tls13Hs* hs, _In_reads_bytes_(len) const uint8* msg, size_t len,
                          uint8 next, uint8 skip)
{
    if (!hsRecvCertificate(hs, msg, len))
        return false;

    // An empty Certificate has no signature to follow it. A client cannot accept that from a
    // server; a server treats it as "no client identity" and decides at the Finished.
    if (!hs->peerPresent && !hs->server)
        return hsFail(hs, TLS13_ALERT_CERTIFICATE_REQUIRED,
                      _S"the server presented no certificate");

    hs->state = hs->peerPresent ? next : skip;
    return true;
}

static bool hsProcess(_Inout_ Tls13Hs* hs, TlsQuicLevel level,
                      _In_reads_bytes_(len) const uint8* msg, size_t len)
{
    uint8 type = msg[0];

    // RFC 9001 section 8.3: QUIC marks the end of early data by moving to 1-RTT keys, so the TLS
    // message that would say so has nothing left to do and must never be sent.
    if (type == TLS13_HS_END_OF_EARLY_DATA)
        return hsFail(hs, TLS13_ALERT_UNEXPECTED_MESSAGE,
                      _S"an EndOfEarlyData, which QUIC forbids");

    switch (hs->state) {
    case TLS13_ST_WAIT_SH:
        if (level == TLSQL_Initial && type == TLS13_HS_SERVER_HELLO)
            return clientServerHello(hs, msg, len);
        break;

    case TLS13_ST_WAIT_EE:
        if (level == TLSQL_Handshake && type == TLS13_HS_ENCRYPTED_EXTENSIONS)
            return clientEncryptedExts(hs, msg, len);
        break;

    case TLS13_ST_WAIT_CERT_CR:
        if (level == TLSQL_Handshake && type == TLS13_HS_CERTIFICATE_REQUEST)
            return clientCertRequest(hs, msg, len);
        if (level == TLSQL_Handshake && type == TLS13_HS_CERTIFICATE)
            return hsCertArrived(hs, msg, len, TLS13_ST_WAIT_CV, TLS13_ST_WAIT_SRV_FIN);
        break;

    case TLS13_ST_WAIT_CERT:
        if (level == TLSQL_Handshake && type == TLS13_HS_CERTIFICATE)
            return hsCertArrived(hs, msg, len, TLS13_ST_WAIT_CV, TLS13_ST_WAIT_SRV_FIN);
        break;

    case TLS13_ST_WAIT_CV:
        if (level == TLSQL_Handshake && type == TLS13_HS_CERTIFICATE_VERIFY) {
            if (!hsRecvCertVerify(hs, msg, len))
                return false;
            hs->state = TLS13_ST_WAIT_SRV_FIN;
            return true;
        }
        break;

    case TLS13_ST_WAIT_SRV_FIN:
        if (level == TLSQL_Handshake && type == TLS13_HS_FINISHED)
            return clientFinish(hs, msg, len);
        break;

    case TLS13_ST_WAIT_CH:
    case TLS13_ST_WAIT_CH2:
        if (level == TLSQL_Initial && type == TLS13_HS_CLIENT_HELLO)
            return serverClientHello(hs, msg, len);
        break;

    case TLS13_ST_WAIT_CLI_CERT:
        if (level == TLSQL_Handshake && type == TLS13_HS_CERTIFICATE)
            return hsCertArrived(hs, msg, len, TLS13_ST_WAIT_CLI_CV, TLS13_ST_WAIT_CLI_FIN);
        break;

    case TLS13_ST_WAIT_CLI_CV:
        if (level == TLSQL_Handshake && type == TLS13_HS_CERTIFICATE_VERIFY) {
            if (!hsRecvCertVerify(hs, msg, len))
                return false;
            hs->state = TLS13_ST_WAIT_CLI_FIN;
            return true;
        }
        break;

    case TLS13_ST_WAIT_CLI_FIN:
        if (level == TLSQL_Handshake && type == TLS13_HS_FINISHED)
            return serverClientFinished(hs, msg, len);
        break;

    case TLS13_ST_DONE:
        if (level == TLSQL_App && type == TLS13_HS_NEW_SESSION_TICKET && !hs->server)
            return clientNewTicket(hs, msg, len);

        // RFC 9001 section 6: QUIC updates its own keys, so a TLS KeyUpdate is a protocol error
        // rather than something to act on.
        if (type == TLS13_HS_KEY_UPDATE)
            return hsFail(hs, TLS13_ALERT_UNEXPECTED_MESSAGE,
                          _S"a TLS KeyUpdate, which QUIC forbids");
        break;

    default:
        break;
    }

    return hsFail(hs, TLS13_ALERT_UNEXPECTED_MESSAGE, _S"a handshake message out of order");
}

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
bool _tls13HsInit(Tls13Hs* hs, bool server, struct TlsConfig* config)
{
    memset(hs, 0, sizeof(*hs));

    if (!_tlsInit() || !config)
        return false;

    hs->server = server;
    hs->config = (TlsConfig*)config;
    hs->state  = server ? TLS13_ST_WAIT_CH : TLS13_ST_START;

    for (size_t i = 0; i < NELEM(suitePref); i++)
        hs->suites[hs->nsuites++] = suitePref[i];
    for (size_t i = 0; i < NELEM(groupPref); i++)
        hs->groups[hs->ngroups++] = groupPref[i];

    mbedtls_x509_crt_init(&hs->peerCert);
    hs->peerCertInit = true;

    return true;
}

_Use_decl_annotations_
void _tls13HsDestroy(Tls13Hs* hs)
{
    _tls13TranscriptDestroy(&hs->tr);
    _tls13KexDestroy(&hs->kex);
    _tls13TicketDestroy(&hs->ticket);

    if (hs->peerCertInit)
        mbedtls_x509_crt_free(&hs->peerCert);

    for (size_t i = 0; i < NELEM(hs->in); i++)
        bufDestroy(&hs->in[i].buf);

    bufDestroy(&hs->tp);
    bufDestroy(&hs->peerTp);
    bufDestroy(&hs->trPre);
    bufDestroy(&hs->cookie);

    strDestroy(&hs->hostname);
    strDestroy(&hs->alpnSel);

    // Everything below is key material, so it is wiped rather than merely dropped.
    memset(hs->clientHsSecret, 0, sizeof(hs->clientHsSecret));
    memset(hs->serverHsSecret, 0, sizeof(hs->serverHsSecret));
    memset(hs->clientApSecret, 0, sizeof(hs->clientApSecret));
    memset(hs->resumption, 0, sizeof(hs->resumption));
    memset(&hs->sched, 0, sizeof(hs->sched));
}

_Use_decl_annotations_
void _tls13HsSetHandlers(Tls13Hs* hs, const TlsQuicHandlers* handlers, void* ctx)
{
    hs->handlers = handlers;
    hs->hctx     = ctx;
}

_Use_decl_annotations_
bool _tls13HsSetHostname(Tls13Hs* hs, strref host)
{
    if (hs->state != TLS13_ST_START && hs->state != TLS13_ST_WAIT_CH)
        return false;

    strDup(&hs->hostname, host);
    return true;
}

_Use_decl_annotations_
bool _tls13HsSetSuites(Tls13Hs* hs, const uint16* suites, size_t n)
{
    if (n == 0 || n > TLS13_MAX_SUITEPREF)
        return false;
    if (hs->state != TLS13_ST_START && hs->state != TLS13_ST_WAIT_CH)
        return false;

    for (size_t i = 0; i < n; i++) {
        if (!_tls13Suite(suites[i]))
            return false;
    }

    hs->nsuites = 0;
    for (size_t i = 0; i < n; i++)
        hs->suites[hs->nsuites++] = suites[i];

    return true;
}

_Use_decl_annotations_
bool _tls13HsSetGroups(Tls13Hs* hs, const uint16* groups, size_t n)
{
    if (n == 0 || n > TLS13_MAX_GROUPS)
        return false;
    if (hs->state != TLS13_ST_START && hs->state != TLS13_ST_WAIT_CH)
        return false;

    for (size_t i = 0; i < n; i++) {
        if (!_tls13GroupPubLen(groups[i]))
            return false;
    }

    hs->ngroups = 0;
    for (size_t i = 0; i < n; i++)
        hs->groups[hs->ngroups++] = groups[i];

    return true;
}

_Use_decl_annotations_
bool _tls13HsSetTransportParams(Tls13Hs* hs, const uint8* data, size_t len)
{
    if (hs->state != TLS13_ST_START && hs->state != TLS13_ST_WAIT_CH)
        return false;

    bufDestroy(&hs->tp);
    hs->tp = bufCreate(len ? len : 1);
    if (len)
        memcpy(hs->tp->data, data, len);
    hs->tp->len = len;

    return true;
}

_Use_decl_annotations_
bool _tls13HsStart(Tls13Hs* hs)
{
    if (hs->server || hs->state != TLS13_ST_START)
        return false;

    if (!hs->tp)
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR,
                      _S"no QUIC transport parameters were set before starting");

    if (psa_generate_random(hs->random, sizeof(hs->random)) != PSA_SUCCESS)
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not generate a client random");

    Buffer saved = _tlsconfigOfferQuicTicket(hs->config, hs->hostname);
    if (saved) {
        Tls13Ticket t;
        if (_tls13TicketParse(&t, saved->data, saved->len)) {
            int64 age = clockWall() - t.issued;

            if (t.id && _tls13Suite(t.suite) && age >= 0 &&
                timeToSeconds(age) <= (int64)t.lifetime) {
                hs->ticket     = t;
                hs->pskOffered = true;

                // The binder is keyed by the ticket's own hash, so the suite has to be settled
                // before the ClientHello is built. A server that picks a different one simply
                // declines the ticket, and the ServerHello resets this.
                hs->suite = _tls13Suite(t.suite);

                // 0-RTT needs a ticket that permits it and a transport willing to send under the
                // limits the session it came from was given. Asked here rather than assumed,
                // because those limits are the transport's business and not the handshake's.
                if (_tlsconfigEarlyData(hs->config) && hs->ticket.maxEarlyData != 0 &&
                    hs->ticket.tp && hs->handlers && hs->handlers->earlyParams) {
                    hs->earlyOffered = hs->handlers->earlyParams(hs->hctx, hs->ticket.tp->data,
                                                                 hs->ticket.tp->len);
                }
            } else {
                _tls13TicketDestroy(&t);
            }
        }
        bufDestroy(&saved);
    }

    hs->group = hs->groups[0];
    if (!_tls13KexGenerate(&hs->kex, hs->group))
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"could not generate a key share");

    hs->state = TLS13_ST_WAIT_SH;
    return clientHello(hs);
}

// Take in whatever arrived and hand every complete message to the state machine. A message can be
// split across any number of calls, so the tail of an incomplete one stays buffered per level.
_Use_decl_annotations_
bool _tls13HsRecv(Tls13Hs* hs, TlsQuicLevel level, const uint8* data, size_t len)
{
    if (hs->state == TLS13_ST_FAILED)
        return false;

    if ((int)level < 0 || level > TLSQL_App)
        return hsFail(hs, TLS13_ALERT_UNEXPECTED_MESSAGE,
                      _S"handshake data at an unknown encryption level");

    Tls13HsIn* in = &hs->in[level];

    if (in->len + len > TLS13_MAX_HS_BUFFER)
        return hsFail(hs, TLS13_ALERT_INTERNAL_ERROR, _S"the peer's handshake flight is too large");

    if (len) {
        bufResize(&in->buf, in->len + len);
        memcpy(in->buf->data + in->len, data, len);
        in->len += len;
    }

    while (in->len - in->used >= 4) {
        const uint8* p = in->buf->data + in->used;
        size_t body    = ((size_t)p[1] << 16) | ((size_t)p[2] << 8) | p[3];

        if (in->len - in->used < 4 + body)
            break;

        in->used += 4 + body;

        if (!hsProcess(hs, level, p, 4 + body))
            return false;
    }

    if (in->used > 0) {
        memmove(in->buf->data, in->buf->data + in->used, in->len - in->used);
        in->len -= in->used;
        in->used = 0;
    }

    return true;
}

_Use_decl_annotations_
strref _tls13SuiteName(uint16 id)
{
    STR_CONST(aes128, "TLS_AES_128_GCM_SHA256");
    STR_CONST(aes256, "TLS_AES_256_GCM_SHA384");
    STR_CONST(chacha, "TLS_CHACHA20_POLY1305_SHA256");

    switch (id) {
    case TLS13_AES_128_GCM_SHA256:
        return aes128;
    case TLS13_AES_256_GCM_SHA384:
        return aes256;
    case TLS13_CHACHA20_POLY1305_SHA256:
        return chacha;
    default:
        return NULL;
    }
}
