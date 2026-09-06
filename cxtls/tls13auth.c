#include "tls13_private.h"
#include "tls_private.h"
#include "tlsconfig.h"

#include <cx/time/clock.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL TlsLogChannel

// ---------------------------------------------------------------------------------------------
// Certificate message (RFC 8446 section 4.4.2)
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
void _tls13CertEncode(Tls13Wr* wr, const mbedtls_x509_crt* chain)
{
    // certificate_request_context: always empty here. It is only ever nonempty in a Certificate
    // sent in answer to a post-handshake CertificateRequest, which this engine does not send.
    _tls13Wr8(wr, 0);

    size_t listMark = _tls13WrOpen24(wr);
    for (const mbedtls_x509_crt* c = chain; c && c->raw.len; c = c->next) {
        size_t certMark = _tls13WrOpen24(wr);
        _tls13WrBytes(wr, c->raw.p, c->raw.len);
        _tls13WrClose24(wr, certMark);

        // Per-certificate extensions. Nothing this engine sends needs any.
        _tls13Wr16(wr, 0);
    }
    _tls13WrClose24(wr, listMark);
}

_Use_decl_annotations_
bool _tls13CertParse(mbedtls_x509_crt* out, const uint8* body, size_t len, bool* empty)
{
    *empty = true;

    Tls13Rd rd;
    _tls13RdInit(&rd, body, len);

    Tls13Rd reqctx;
    if (!_tls13RdVec8(&rd, &reqctx))
        return false;

    Tls13Rd list;
    if (!_tls13RdVec24(&rd, &list) || _tls13RdLeft(&rd) != 0)
        return false;

    while (_tls13RdLeft(&list) > 0) {
        Tls13Rd cert;
        if (!_tls13RdVec24(&list, &cert) || _tls13RdLeft(&cert) == 0)
            return false;

        int ret = mbedtls_x509_crt_parse_der(out, cert.p, _tls13RdLeft(&cert));
        if (ret != 0) {
            tlsLogErr(Warn, _S"mbedtls_x509_crt_parse_der", ret);
            return false;
        }
        *empty = false;

        Tls13Rd exts;
        if (!_tls13RdVec16(&list, &exts))
            return false;
    }

    return !list.bad;
}

// ---------------------------------------------------------------------------------------------
// CertificateVerify (RFC 8446 section 4.4.3)
// ---------------------------------------------------------------------------------------------

// The content that is actually signed: 64 spaces, a context string, a zero byte, and the
// transcript hash. The 64 spaces are there so that a signature over this can never be mistaken
// for a signature over a TLS 1.2 structure, or the other way round.
static size_t sigContent(_Out_writes_bytes_(bufsz) uint8* buf, size_t bufsz, bool serverSig,
                         _In_reads_bytes_(thashLen) const uint8* thash, size_t thashLen)
{
    STR_CONST(srvctx, "TLS 1.3, server CertificateVerify");
    STR_CONST(clictx, "TLS 1.3, client CertificateVerify");

    strref ctx  = serverSig ? srvctx : clictx;
    uint32 clen = strLen(ctx);

    size_t need = 64 + clen + 1 + thashLen;
    if (need > bufsz)
        return 0;

    memset(buf, 0x20, 64);

    // strCopyOut() appends the NUL, which is exactly the separator byte the structure calls for.
    strCopyOut(ctx, 0, buf + 64, clen + 1);
    memcpy(buf + 64 + clen + 1, thash, thashLen);

    return need;
}

// The hash a scheme signs with, and whether the key type has to be RSA or EC to use it.
static bool sigParams(uint16 scheme, _Out_ mbedtls_md_type_t* md, _Out_ psa_algorithm_t* psaMd,
                      _Out_ bool* rsa, _Out_ size_t* ecBits)
{
    *ecBits = 0;

    switch (scheme) {
    case TLS13_SIG_ECDSA_SECP256R1_SHA256:
        *md = MBEDTLS_MD_SHA256; *psaMd = PSA_ALG_SHA_256; *rsa = false; *ecBits = 256;
        return true;
    case TLS13_SIG_ECDSA_SECP384R1_SHA384:
        *md = MBEDTLS_MD_SHA384; *psaMd = PSA_ALG_SHA_384; *rsa = false; *ecBits = 384;
        return true;
    case TLS13_SIG_ECDSA_SECP521R1_SHA512:
        *md = MBEDTLS_MD_SHA512; *psaMd = PSA_ALG_SHA_512; *rsa = false; *ecBits = 521;
        return true;
    case TLS13_SIG_RSA_PSS_RSAE_SHA256:
        *md = MBEDTLS_MD_SHA256; *psaMd = PSA_ALG_SHA_256; *rsa = true;
        return true;
    case TLS13_SIG_RSA_PSS_RSAE_SHA384:
        *md = MBEDTLS_MD_SHA384; *psaMd = PSA_ALG_SHA_384; *rsa = true;
        return true;
    case TLS13_SIG_RSA_PSS_RSAE_SHA512:
        *md = MBEDTLS_MD_SHA512; *psaMd = PSA_ALG_SHA_512; *rsa = true;
        return true;
    default:
        return false;
    }
}

// Hash the signed content down to what the signature primitive takes.
static bool sigHash(psa_algorithm_t alg, _In_reads_bytes_(len) const uint8* content, size_t len,
                    _Out_writes_bytes_(64) uint8* out, _Out_ size_t* outLen)
{
    psa_status_t st = psa_hash_compute(alg, content, len, out, 64, outLen);
    if (st != PSA_SUCCESS) {
        tlsLogErr(Warn, _S"psa_hash_compute", (int)st);
        return false;
    }
    return true;
}

_Use_decl_annotations_
bool _tls13SigSelect(const mbedtls_pk_context* key, const uint16* peerSchemes, size_t n,
                     uint16* out)
{
    psa_key_type_t kt = mbedtls_pk_get_key_type(key);
    size_t bits       = mbedtls_pk_get_bitlen(key);

    for (size_t i = 0; i < n; i++) {
        mbedtls_md_type_t md;
        psa_algorithm_t psaMd;
        bool rsa;
        size_t ecBits;

        if (!sigParams(peerSchemes[i], &md, &psaMd, &rsa, &ecBits))
            continue;

        if (rsa) {
            if (!PSA_KEY_TYPE_IS_RSA(kt))
                continue;
            // PSS needs room for two hashes and the trailer inside the modulus.
            if (bits < 8 * (2 * PSA_HASH_LENGTH(psaMd) + 2))
                continue;
        } else {
            // The scheme names the curve as well as the hash, so a P-256 key can only ever produce
            // ecdsa_secp256r1_sha256.
            if (!PSA_KEY_TYPE_IS_ECC(kt) ||
                PSA_KEY_TYPE_ECC_GET_FAMILY(kt) != PSA_ECC_FAMILY_SECP_R1 || bits != ecBits)
                continue;
        }

        *out = peerSchemes[i];
        return true;
    }

    return false;
}

_Use_decl_annotations_
bool _tls13SigMake(uint16 scheme, mbedtls_pk_context* key, bool serverSig, const uint8* thash,
                   size_t thashLen, uint8* sig, size_t sigsz, size_t* sigLen)
{
    mbedtls_md_type_t md;
    psa_algorithm_t psaMd;
    bool rsa;
    size_t ecBits;

    *sigLen = 0;
    if (!sigParams(scheme, &md, &psaMd, &rsa, &ecBits))
        return false;

    uint8 content[64 + 64 + 1 + TLS13_MAX_HASH];
    size_t clen = sigContent(content, sizeof(content), serverSig, thash, thashLen);
    if (!clen)
        return false;

    uint8 hash[64];
    size_t hlen;
    if (!sigHash(psaMd, content, clen, hash, &hlen))
        return false;

    int ret;
    if (rsa) {
        ret = mbedtls_pk_sign_ext(MBEDTLS_PK_SIGALG_RSA_PSS, key, md, hash, hlen,
                                  sig, sigsz, sigLen);
    } else {
        ret = mbedtls_pk_sign(key, md, hash, hlen, sig, sigsz, sigLen);
    }

    if (ret != 0) {
        tlsLogErr(Warn, _S"mbedtls_pk_sign", ret);
        return false;
    }

    return true;
}

_Use_decl_annotations_
bool _tls13SigCheck(uint16 scheme, mbedtls_pk_context* key, bool serverSig, const uint8* thash,
                    size_t thashLen, const uint8* sig, size_t sigLen)
{
    mbedtls_md_type_t md;
    psa_algorithm_t psaMd;
    bool rsa;
    size_t ecBits;

    if (!sigParams(scheme, &md, &psaMd, &rsa, &ecBits))
        return false;

    // A scheme naming a curve is only valid over a key on that curve, so an attacker cannot pick a
    // weaker hash than the certificate's key was issued for.
    psa_key_type_t kt = mbedtls_pk_get_key_type(key);
    if (rsa) {
        if (!PSA_KEY_TYPE_IS_RSA(kt))
            return false;
    } else if (!PSA_KEY_TYPE_IS_ECC(kt) ||
               PSA_KEY_TYPE_ECC_GET_FAMILY(kt) != PSA_ECC_FAMILY_SECP_R1 ||
               mbedtls_pk_get_bitlen(key) != ecBits) {
        return false;
    }

    uint8 content[64 + 64 + 1 + TLS13_MAX_HASH];
    size_t clen = sigContent(content, sizeof(content), serverSig, thash, thashLen);
    if (!clen)
        return false;

    uint8 hash[64];
    size_t hlen;
    if (!sigHash(psaMd, content, clen, hash, &hlen))
        return false;

    int ret;
    if (rsa) {
        ret = mbedtls_pk_verify_ext(MBEDTLS_PK_SIGALG_RSA_PSS, key, md, hash, hlen, sig, sigLen);
    } else {
        ret = mbedtls_pk_verify(key, md, hash, hlen, sig, sigLen);
    }

    if (ret != 0) {
        tlsLogErr(Warn, _S"mbedtls_pk_verify", ret);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------------------------
// Chain verification
// ---------------------------------------------------------------------------------------------

static int verifyThunk(void* p, mbedtls_x509_crt* crt, int depth, uint32_t* flags)
{
    TlsConfig* cfg = (TlsConfig*)p;
    return cfg->st->verifyCb(crt, depth, (uint32*)flags, cfg->st->verifyCtx) ? 0 : -1;
}

_Use_decl_annotations_
bool _tls13ChainVerify(struct TlsConfig* config, mbedtls_x509_crt* chain, strref hostname,
                       uint32* flags)
{
    TlsConfig* cfg = (TlsConfig*)config;
    *flags         = 0;

    if (cfg->st->authMode == TLSAUTH_None)
        return true;

    if (!cfg->ca) {
        *flags = MBEDTLS_X509_BADCERT_NOT_TRUSTED;
        return cfg->st->authMode != TLSAUTH_Required;
    }

    // mbedTLS wants the expected name as a C string, and NULL to skip the name check entirely --
    // which is what an empty hostname means here.
    const char* cn = NULL;
    if (!strEmpty(hostname))
        cn = strC(hostname);

    int ret = mbedtls_x509_crt_verify(chain,
                                      &cfg->ca->chain->crt,
                                      NULL,
                                      cn,
                                      (uint32_t*)flags,
                                      cfg->st->verifyCb ? verifyThunk : NULL,
                                      cfg);

    // A nonzero return other than the ordinary "chain did not verify" means the callback aborted
    // or something went wrong inside; either way the chain is not usable.
    if (ret != 0 && ret != MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
        tlsLogErr(Warn, _S"mbedtls_x509_crt_verify", ret);
        if (*flags == 0)
            *flags = MBEDTLS_X509_BADCERT_NOT_TRUSTED;
        return false;
    }

    return *flags == 0 || cfg->st->authMode != TLSAUTH_Required;
}

// ---------------------------------------------------------------------------------------------
// Session tickets
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
void _tls13TicketDestroy(Tls13Ticket* t)
{
    bufDestroy(&t->id);
    bufDestroy(&t->tp);
    strDestroy(&t->alpn);
    memset(t, 0, sizeof(*t));
}

_Use_decl_annotations_
Buffer _tls13TicketSerialize(const Tls13Ticket* t)
{
    if (t->pskLen > TLS13_MAX_HASH)
        return NULL;

    Tls13Wr wr;
    _tls13WrInit(&wr, 256);

    _tls13Wr16(&wr, t->suite);
    _tls13Wr8(&wr, t->pskLen);
    _tls13WrBytes(&wr, t->psk, t->pskLen);
    _tls13Wr32(&wr, t->ageAdd);
    _tls13Wr32(&wr, t->lifetime);
    _tls13Wr64(&wr, (uint64)t->issued);

    size_t mark = _tls13WrOpen16(&wr);
    if (t->id)
        _tls13WrBytes(&wr, t->id->data, t->id->len);
    _tls13WrClose16(&wr, mark);

    mark = _tls13WrOpen8(&wr);
    if (!strEmpty(t->alpn))
        _tls13WrBytes(&wr, (const uint8*)strC(t->alpn), strLen(t->alpn));
    _tls13WrClose8(&wr, mark);

    _tls13Wr32(&wr, t->maxEarlyData);

    mark = _tls13WrOpen16(&wr);
    if (t->tp)
        _tls13WrBytes(&wr, t->tp->data, t->tp->len);
    _tls13WrClose16(&wr, mark);

    return _tls13WrTake(&wr);
}

_Use_decl_annotations_
bool _tls13TicketParse(Tls13Ticket* t, const uint8* data, size_t len)
{
    memset(t, 0, sizeof(*t));

    Tls13Rd rd;
    _tls13RdInit(&rd, data, len);

    t->suite  = _tls13Rd16(&rd);
    t->pskLen = _tls13Rd8(&rd);
    if (t->pskLen > TLS13_MAX_HASH)
        return false;

    const uint8* psk = _tls13RdBytes(&rd, t->pskLen);
    if (psk)
        memcpy(t->psk, psk, t->pskLen);

    t->ageAdd   = _tls13Rd32(&rd);
    t->lifetime = _tls13Rd32(&rd);
    t->issued   = (int64)_tls13Rd64(&rd);

    Tls13Rd id;
    if (!_tls13RdVec16(&rd, &id)) {
        _tls13TicketDestroy(t);
        return false;
    }
    if (_tls13RdLeft(&id) > 0) {
        t->id = bufCreate(_tls13RdLeft(&id));
        memcpy(t->id->data, id.p, _tls13RdLeft(&id));
        t->id->len = _tls13RdLeft(&id);
    }

    Tls13Rd alpn;
    if (!_tls13RdVec8(&rd, &alpn)) {
        _tls13TicketDestroy(t);
        return false;
    }
    if (_tls13RdLeft(&alpn) > 0)
        strFromBytes(&t->alpn, alpn.p, (uint32)_tls13RdLeft(&alpn));

    t->maxEarlyData = _tls13Rd32(&rd);

    Tls13Rd tp;
    if (!_tls13RdVec16(&rd, &tp)) {
        _tls13TicketDestroy(t);
        return false;
    }
    if (_tls13RdLeft(&tp) > 0) {
        t->tp = bufCreate(_tls13RdLeft(&tp));
        memcpy(t->tp->data, tp.p, _tls13RdLeft(&tp));
        t->tp->len = _tls13RdLeft(&tp);
    }

    if (rd.bad || _tls13RdLeft(&rd) != 0) {
        _tls13TicketDestroy(t);
        return false;
    }

    return true;
}

// AES-256-GCM under the config's ticket key. The nonce is random rather than a counter because a
// server has no durable place to keep one across restarts, and 96 random bits is far more room
// than the number of tickets any one key will ever protect.
#define TLS13_TICKET_NONCE 12

static bool ticketKey(_In_reads_bytes_(32) const uint8* key, _Out_ psa_key_id_t* out)
{
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
    psa_set_key_algorithm(&attr, PSA_ALG_GCM);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);

    psa_status_t st = psa_import_key(&attr, key, 32, out);
    if (st != PSA_SUCCESS) {
        tlsLogErr(Warn, _S"psa_import_key", (int)st);
        return false;
    }
    return true;
}

_Use_decl_annotations_
Buffer _tls13TicketSeal(const uint8* key, const Tls13Ticket* t)
{
    Buffer plain = _tls13TicketSerialize(t);
    if (!plain)
        return NULL;

    psa_key_id_t k;
    if (!ticketKey(key, &k)) {
        bufDestroy(&plain);
        return NULL;
    }

    Buffer out = bufCreate(TLS13_TICKET_NONCE + plain->len + 16);

    psa_status_t st = psa_generate_random(out->data, TLS13_TICKET_NONCE);
    if (st == PSA_SUCCESS) {
        size_t n;
        st = psa_aead_encrypt(k, PSA_ALG_GCM, out->data, TLS13_TICKET_NONCE, NULL, 0,
                              plain->data, plain->len,
                              out->data + TLS13_TICKET_NONCE, out->sz - TLS13_TICKET_NONCE, &n);
        if (st == PSA_SUCCESS)
            out->len = TLS13_TICKET_NONCE + n;
    }

    psa_destroy_key(k);
    bufDestroy(&plain);

    if (st != PSA_SUCCESS) {
        tlsLogErr(Warn, _S"psa_aead_encrypt", (int)st);
        bufDestroy(&out);
        return NULL;
    }

    return out;
}

_Use_decl_annotations_
bool _tls13TicketOpen(const uint8* key, const uint8* data, size_t len, Tls13Ticket* t)
{
    memset(t, 0, sizeof(*t));

    if (len <= TLS13_TICKET_NONCE + 16)
        return false;

    psa_key_id_t k;
    if (!ticketKey(key, &k))
        return false;

    size_t cipherLen = len - TLS13_TICKET_NONCE;
    Buffer plain     = bufCreate(cipherLen);

    size_t n;
    psa_status_t st = psa_aead_decrypt(k, PSA_ALG_GCM, data, TLS13_TICKET_NONCE, NULL, 0,
                                       data + TLS13_TICKET_NONCE, cipherLen,
                                       plain->data, plain->sz, &n);
    psa_destroy_key(k);

    // A ticket that does not authenticate is the ordinary case after a key rotation or a restart,
    // not an attack worth logging: the handshake simply runs in full.
    bool ok = st == PSA_SUCCESS && _tls13TicketParse(t, plain->data, n);
    bufDestroy(&plain);

    if (ok && t->lifetime > 0) {
        int64 age = clockWall() - t->issued;
        if (age < 0 || timeToSeconds(age) > (int64)t->lifetime) {
            _tls13TicketDestroy(t);
            ok = false;
        }
    }

    return ok;
}
