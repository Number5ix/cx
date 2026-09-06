#include "tls13_private.h"
#include "tls_private.h"

#include <cx/format.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL TlsLogChannel

// The all-zero string the key schedule uses wherever RFC 8446 writes "0" as an Extract input. It
// is hashLen zero bytes, not an empty string, so it needs real storage.
static const uint8 tls13Zeroes[TLS13_MAX_HASH] = { 0 };

// The suites cx implements, in preference order. AES-GCM first because it is hardware-accelerated
// nearly everywhere; ChaCha20-Poly1305 last as the software fallback.
static const Tls13Suite tls13Suites[] = {
    { TLS13_AES_128_GCM_SHA256, 32, 16, PSA_ALG_SHA_256, PSA_ALG_GCM, PSA_KEY_TYPE_AES },
    { TLS13_AES_256_GCM_SHA384, 48, 32, PSA_ALG_SHA_384, PSA_ALG_GCM, PSA_KEY_TYPE_AES },
    { TLS13_CHACHA20_POLY1305_SHA256, 32, 32, PSA_ALG_SHA_256, PSA_ALG_CHACHA20_POLY1305,
      PSA_KEY_TYPE_CHACHA20 },
};

_Use_decl_annotations_
const Tls13Suite* _tls13Suite(uint16 id)
{
    for (size_t i = 0; i < sizeof(tls13Suites) / sizeof(tls13Suites[0]); i++) {
        if (tls13Suites[i].id == id)
            return &tls13Suites[i];
    }
    return NULL;
}

uint8 _tls13HashLen(psa_algorithm_t hash)
{
    switch (hash) {
    case PSA_ALG_SHA_256:
        return 32;
    case PSA_ALG_SHA_384:
        return 48;
    default:
        return 0;
    }
}

// ---------------------------------------------------------------------------------------------
// HKDF
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
bool _tls13Extract(psa_algorithm_t hash, const uint8* salt, size_t saltLen, const uint8* ikm,
                   size_t ikmLen, uint8* out)
{
    uint8 hashLen = _tls13HashLen(hash);
    if (!hashLen || !_tlsInit())
        return false;

    psa_key_derivation_operation_t op = PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_status_t st = psa_key_derivation_setup(&op, PSA_ALG_HKDF_EXTRACT(hash));

    // PSA takes the salt before the IKM for HKDF-Extract, and treats the IKM as the "secret"
    // input even though HMAC uses the salt as its key -- that is RFC 5869's naming, not a swap.
    if (st == PSA_SUCCESS)
        st = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_SALT, salt, saltLen);
    if (st == PSA_SUCCESS)
        st = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_SECRET, ikm, ikmLen);
    if (st == PSA_SUCCESS)
        st = psa_key_derivation_output_bytes(&op, out, hashLen);

    psa_key_derivation_abort(&op);

    if (st != PSA_SUCCESS) {
        tlsLogErr(Error, _S"HKDF-Extract", (int)st);
        return false;
    }
    return true;
}

_Use_decl_annotations_
bool _tls13HkdfLabel(strref label, const uint8* ctx, size_t ctxLen, uint16 outLen, uint8* buf,
                     size_t bufsz, size_t* len)
{
    uint32 labelLen = strLen(label);

    // Both length prefixes are one byte, and the label carries a fixed 6-byte "tls13 " prefix.
    if (labelLen + 6 > 255 || ctxLen > 255)
        return false;

    size_t need = 2 + 1 + (size_t)labelLen + 6 + 1 + ctxLen;
    if (bufsz < need)
        return false;

    uint8* p = buf;
    *p++     = (uint8)(outLen >> 8);
    *p++     = (uint8)(outLen & 0xff);
    *p++     = (uint8)(labelLen + 6);
    memcpy(p, "tls13 ", 6);
    p += 6;
    memcpy(p, strC(label), labelLen);
    p += labelLen;
    *p++ = (uint8)ctxLen;
    if (ctxLen)
        memcpy(p, ctx, ctxLen);
    p += ctxLen;

    *len = (size_t)(p - buf);
    return true;
}

_Use_decl_annotations_
bool _tls13ExpandLabel(psa_algorithm_t hash, const uint8* secret, size_t secretLen, strref label,
                       const uint8* ctx, size_t ctxLen, uint8* out, size_t outLen)
{
    if (!_tls13HashLen(hash) || outLen > 0xffff || !_tlsInit())
        return false;

    // 2 length octets, then two length-prefixed byte strings that are 255 bytes at most each.
    uint8 info[2 + 1 + 255 + 1 + 255];
    size_t infoLen = 0;
    if (!_tls13HkdfLabel(label, ctx, ctxLen, (uint16)outLen, info, sizeof(info), &infoLen))
        return false;

    psa_key_derivation_operation_t op = PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_status_t st = psa_key_derivation_setup(&op, PSA_ALG_HKDF_EXPAND(hash));

    if (st == PSA_SUCCESS)
        st = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_SECRET, secret, secretLen);
    if (st == PSA_SUCCESS)
        st = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_INFO, info, infoLen);
    if (st == PSA_SUCCESS)
        st = psa_key_derivation_output_bytes(&op, out, outLen);

    psa_key_derivation_abort(&op);

    if (st != PSA_SUCCESS) {
        tlsLogErr(Error, _S"HKDF-Expand-Label", (int)st);
        return false;
    }
    return true;
}

_Use_decl_annotations_
bool _tls13DeriveSecret(psa_algorithm_t hash, const uint8* secret, strref label, const uint8* thash,
                        size_t thashLen, uint8* out)
{
    uint8 hashLen = _tls13HashLen(hash);
    if (!hashLen)
        return false;

    return _tls13ExpandLabel(hash, secret, hashLen, label, thash, thashLen, out, hashLen);
}

_Use_decl_annotations_
bool _tls13TrafficKeys(psa_algorithm_t hash, const uint8* secret, strref keyLabel, uint8* key,
                       size_t keyLen, strref ivLabel, uint8* iv, size_t ivLen)
{
    uint8 hashLen = _tls13HashLen(hash);
    if (!hashLen)
        return false;

    return _tls13ExpandLabel(hash, secret, hashLen, keyLabel, NULL, 0, key, keyLen) &&
           _tls13ExpandLabel(hash, secret, hashLen, ivLabel, NULL, 0, iv, ivLen);
}

_Use_decl_annotations_
bool _tls13Finished(psa_algorithm_t hash, const uint8* secret, const uint8* thash, size_t thashLen,
                    uint8* out)
{
    uint8 hashLen = _tls13HashLen(hash);
    if (!hashLen || !_tlsInit())
        return false;

    uint8 finKey[TLS13_MAX_HASH];
    if (!_tls13ExpandLabel(hash, secret, hashLen, _S"finished", NULL, 0, finKey, hashLen))
        return false;

    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attr, PSA_ALG_HMAC(hash));
    psa_set_key_type(&attr, PSA_KEY_TYPE_HMAC);

    psa_key_id_t kid = PSA_KEY_ID_NULL;
    psa_status_t st  = psa_import_key(&attr, finKey, hashLen, &kid);

    size_t outLen = 0;
    if (st == PSA_SUCCESS) {
        st = psa_mac_compute(kid, PSA_ALG_HMAC(hash), thash, thashLen, out, TLS13_MAX_HASH,
                             &outLen);
        psa_destroy_key(kid);
    }

    memset(finKey, 0, sizeof(finKey));

    if (st != PSA_SUCCESS || outLen != hashLen) {
        tlsLogErr(Error, _S"Finished HMAC", (int)st);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Transcript hash
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
bool _tls13TranscriptInit(Tls13Transcript* tr, psa_algorithm_t hash)
{
    memset(tr, 0, sizeof(*tr));

    if (!_tls13HashLen(hash) || !_tlsInit())
        return false;

    psa_hash_operation_t init = PSA_HASH_OPERATION_INIT;
    tr->op                    = init;
    tr->hash                  = hash;

    psa_status_t st = psa_hash_setup(&tr->op, hash);
    if (st != PSA_SUCCESS) {
        tlsLogErr(Error, _S"psa_hash_setup", (int)st);
        return false;
    }

    tr->live = true;
    return true;
}

_Use_decl_annotations_
bool _tls13TranscriptUpdate(Tls13Transcript* tr, const uint8* data, size_t len)
{
    if (!tr->live)
        return false;
    if (!len)
        return true;

    psa_status_t st = psa_hash_update(&tr->op, data, len);
    if (st != PSA_SUCCESS) {
        tlsLogErr(Error, _S"psa_hash_update", (int)st);
        return false;
    }
    return true;
}

_Use_decl_annotations_
bool _tls13TranscriptHash(const Tls13Transcript* tr, uint8* out)
{
    if (!tr->live)
        return false;

    // The transcript keeps growing after a snapshot is taken, so the running operation is cloned
    // and the clone finished. psa_hash_finish() would end the real one.
    psa_hash_operation_t tmp = PSA_HASH_OPERATION_INIT;
    psa_status_t st          = psa_hash_clone(&tr->op, &tmp);

    size_t outLen = 0;
    if (st == PSA_SUCCESS)
        st = psa_hash_finish(&tmp, out, TLS13_MAX_HASH, &outLen);

    psa_hash_abort(&tmp);

    if (st != PSA_SUCCESS || outLen != _tls13HashLen(tr->hash)) {
        tlsLogErr(Error, _S"transcript hash", (int)st);
        return false;
    }
    return true;
}

_Use_decl_annotations_
void _tls13TranscriptDestroy(Tls13Transcript* tr)
{
    if (tr->live) {
        psa_hash_abort(&tr->op);
        tr->live = false;
    }
}

// ---------------------------------------------------------------------------------------------
// Key schedule
// ---------------------------------------------------------------------------------------------

// Advance from one Extract stage to the next: the current secret becomes the salt for the next
// Extract, after being run through Derive-Secret(., "derived", Hash("")).
static bool tls13SchedAdvance(_Inout_ Tls13Schedule* s,
                              _In_reads_bytes_opt_(ikmLen) const uint8* ikm, size_t ikmLen)
{
    uint8 emptyHash[TLS13_MAX_HASH];
    size_t emptyLen = 0;
    psa_status_t st = psa_hash_compute(s->hash, NULL, 0, emptyHash, sizeof(emptyHash), &emptyLen);
    if (st != PSA_SUCCESS) {
        tlsLogErr(Error, _S"psa_hash_compute", (int)st);
        return false;
    }

    uint8 salt[TLS13_MAX_HASH];
    if (!_tls13DeriveSecret(s->hash, s->secret, _S"derived", emptyHash, emptyLen, salt))
        return false;

    return _tls13Extract(s->hash, salt, s->hashLen, ikm, ikmLen, s->secret);
}

_Use_decl_annotations_
bool _tls13SchedEarly(Tls13Schedule* s, psa_algorithm_t hash, const uint8* psk, size_t pskLen)
{
    memset(s, 0, sizeof(*s));

    s->hashLen = _tls13HashLen(hash);
    if (!s->hashLen)
        return false;
    s->hash = hash;

    if (!psk) {
        psk    = tls13Zeroes;
        pskLen = s->hashLen;
    }

    // The salt for the early Extract is a zero-length string, not hashLen zero bytes -- the one
    // place in the schedule where RFC 8446's "0" means empty rather than zero-filled.
    return _tls13Extract(hash, NULL, 0, psk, pskLen, s->secret);
}

_Use_decl_annotations_
bool _tls13SchedHandshake(Tls13Schedule* s, const uint8* ecdhe, size_t ecdheLen)
{
    if (!s->hashLen)
        return false;
    return tls13SchedAdvance(s, ecdhe, ecdheLen);
}

_Use_decl_annotations_
bool _tls13SchedMaster(Tls13Schedule* s)
{
    if (!s->hashLen)
        return false;
    return tls13SchedAdvance(s, tls13Zeroes, s->hashLen);
}

_Use_decl_annotations_
bool _tls13SchedDerive(const Tls13Schedule* s, strref label, const uint8* thash, size_t thashLen,
                       uint8* out)
{
    if (!s->hashLen)
        return false;
    return _tls13DeriveSecret(s->hash, s->secret, label, thash, thashLen, out);
}
