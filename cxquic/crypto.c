// QUIC packet protection: RFC 9001 section 5.
//
// Two independent layers cover every packet. The AEAD protects the payload with the header as
// associated data, and header protection then hides the packet number and the low bits of the
// first byte behind a mask derived from a sample of the ciphertext. They use different keys, and
// only the AEAD key changes when the connection updates keys.

#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS

#include "quic_private.h"

#include <cxtls/tls_private.h>

#include <mbedtls/private/chacha20.h>

#include <string.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL QuicLogChannel

// RFC 9001 section 5.2. Version 1's Initial keys come from this fixed value and the client's
// first Destination Connection ID, so both endpoints can derive them before anything is
// negotiated -- which is also why they protect against nothing but off-path corruption.
static const uint8 quicInitialSalt[20] = {
    0x38, 0x76, 0x2c, 0xf7, 0xf5, 0x59, 0x34, 0xb3, 0x4d, 0x17,
    0x9a, 0xe6, 0xa4, 0xc8, 0x0c, 0xad, 0xcc, 0xbb, 0x7f, 0x0a,
};

// RFC 9001 section 5.8. The Retry integrity tag is not a secret: these constants are published so
// that any endpoint can check the tag, which is only there to prove the Retry came from something
// on the path rather than an off-path attacker.
static const uint8 quicRetryKey[16] = {
    0xbe, 0x0c, 0x69, 0x0b, 0x9f, 0x66, 0x57, 0x5a,
    0x1d, 0x76, 0x6b, 0x54, 0xe3, 0x68, 0xc8, 0x4e,
};

static const uint8 quicRetryNonce[12] = {
    0x46, 0x15, 0x99, 0xd3, 0x5d, 0x63, 0x2b, 0xf2, 0x23, 0x98, 0x25, 0xbb,
};

STR_CONST(quicLblClientIn, "client in");
STR_CONST(quicLblServerIn, "server in");
STR_CONST(quicLblKey, "quic key");
STR_CONST(quicLblIv, "quic iv");
STR_CONST(quicLblHp, "quic hp");
STR_CONST(quicLblKu, "quic ku");

// Imports the header protection key already sitting in k->hpKey. ChaCha20 header protection needs
// the keystream at an arbitrary block counter, which the PSA cipher API cannot express, so that
// case keeps the raw bytes and calls the block function directly.
static bool quicKeysImportHp(QuicKeys* k)
{
    if (k->hpChaCha)
        return true;

    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, k->keyType);
    psa_set_key_bits(&attr, (size_t)k->keyLen * 8);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT);
    psa_set_key_algorithm(&attr, PSA_ALG_ECB_NO_PADDING);

    psa_status_t st = psa_import_key(&attr, k->hpKey, k->keyLen, &k->hp);
    psa_reset_key_attributes(&attr);

    if (st != PSA_SUCCESS) {
        tlsLogErr(Error, _S"psa_import_key(hp)", (int)st);
        return false;
    }
    return true;
}

// Derives the AEAD key and IV from the traffic secret already in k->secret and imports the key.
// The header protection key is the caller's to fill in first, because a key update reuses the old
// one rather than deriving a new one.
static bool quicKeysFromSecret(QuicKeys* k)
{
    uint8 key[TLS13_MAX_KEY];

    if (!_tls13ExpandLabel(k->hash, k->secret, k->secretLen, quicLblKey, NULL, 0, key,
                           k->keyLen) ||
        !_tls13ExpandLabel(k->hash, k->secret, k->secretLen, quicLblIv, NULL, 0, k->iv,
                           TLS13_IV_LEN)) {
        memset(key, 0, sizeof(key));
        return false;
    }

    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, k->keyType);
    psa_set_key_bits(&attr, (size_t)k->keyLen * 8);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attr, k->aeadAlg);

    psa_status_t st = psa_import_key(&attr, key, k->keyLen, &k->aead);
    psa_reset_key_attributes(&attr);
    memset(key, 0, sizeof(key));

    if (st != PSA_SUCCESS) {
        tlsLogErr(Error, _S"psa_import_key(aead)", (int)st);
        return false;
    }

    k->valid = true;
    return true;
}

_Use_decl_annotations_
bool _quicKeysDerive(QuicKeys* k, const Tls13Suite* suite, const uint8* secret)
{
    memset(k, 0, sizeof(*k));

    if (!_quicInit() || !suite)
        return false;

    k->hash = suite->hash;
    k->aeadAlg = suite->aead;
    k->keyType = suite->keyType;
    k->keyLen = suite->keyLen;
    k->secretLen = suite->hashLen;
    k->hpChaCha = (suite->keyType == PSA_KEY_TYPE_CHACHA20);
    memcpy(k->secret, secret, k->secretLen);

    if (!_tls13ExpandLabel(k->hash, k->secret, k->secretLen, quicLblHp, NULL, 0, k->hpKey,
                           k->keyLen) ||
        !quicKeysImportHp(k) || !quicKeysFromSecret(k)) {
        _quicKeysDestroy(k);
        return false;
    }

    return true;
}

_Use_decl_annotations_
bool _quicKeysNext(QuicKeys* next, const QuicKeys* cur)
{
    if (!cur->valid)
        return false;

    memset(next, 0, sizeof(*next));
    next->hash = cur->hash;
    next->aeadAlg = cur->aeadAlg;
    next->keyType = cur->keyType;
    next->keyLen = cur->keyLen;
    next->secretLen = cur->secretLen;
    next->hpChaCha = cur->hpChaCha;
    memcpy(next->hpKey, cur->hpKey, sizeof(next->hpKey));

    if (!_tls13ExpandLabel(next->hash, cur->secret, cur->secretLen, quicLblKu, NULL, 0,
                           next->secret, next->secretLen) ||
        !quicKeysImportHp(next) || !quicKeysFromSecret(next)) {
        _quicKeysDestroy(next);
        return false;
    }

    return true;
}

_Use_decl_annotations_
bool _quicKeysInitial(QuicKeys* client, QuicKeys* server, const uint8* dcid, size_t dcidLen)
{
    memset(client, 0, sizeof(*client));
    memset(server, 0, sizeof(*server));

    if (!_quicInit())
        return false;

    // The Initial packet protection suite is fixed at AES-128-GCM with SHA-256; the negotiated
    // suite only starts applying at the Handshake level.
    const Tls13Suite* suite = _tls13Suite(TLS13_AES_128_GCM_SHA256);
    if (!suite)
        return false;

    uint8 initial[TLS13_MAX_HASH], cs[TLS13_MAX_HASH], ss[TLS13_MAX_HASH];
    bool ok = _tls13Extract(suite->hash, quicInitialSalt, sizeof(quicInitialSalt), dcid, dcidLen,
                            initial) &&
              _tls13ExpandLabel(suite->hash, initial, suite->hashLen, quicLblClientIn, NULL, 0, cs,
                                suite->hashLen) &&
              _tls13ExpandLabel(suite->hash, initial, suite->hashLen, quicLblServerIn, NULL, 0, ss,
                                suite->hashLen) &&
              _quicKeysDerive(client, suite, cs) && _quicKeysDerive(server, suite, ss);

    memset(initial, 0, sizeof(initial));
    memset(cs, 0, sizeof(cs));
    memset(ss, 0, sizeof(ss));

    if (!ok) {
        _quicKeysDestroy(client);
        _quicKeysDestroy(server);
    }
    return ok;
}

_Use_decl_annotations_
void _quicKeysDestroy(QuicKeys* k)
{
    if (k->aead != PSA_KEY_ID_NULL)
        psa_destroy_key(k->aead);
    if (k->hp != PSA_KEY_ID_NULL)
        psa_destroy_key(k->hp);
    memset(k, 0, sizeof(*k));
}

// ---------------------------------------------------------------------------------------------
// Header protection
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
bool _quicHpMask(const QuicKeys* k, const uint8* sample, uint8* mask)
{
    if (!k->valid)
        return false;

    if (k->hpChaCha) {
        // RFC 9001 section 5.4.4 splits the sample into a little-endian block counter and a
        // nonce, then encrypts five zero bytes, which is the same as taking the keystream.
        uint32 counter = (uint32)sample[0] | ((uint32)sample[1] << 8) |
                         ((uint32)sample[2] << 16) | ((uint32)sample[3] << 24);
        uint8 zeroes[5] = { 0 };

        int r = mbedtls_chacha20_crypt(k->hpKey, sample + 4, counter, sizeof(zeroes), zeroes,
                                       mask);
        if (r != 0) {
            tlsLogErr(Error, _S"mbedtls_chacha20_crypt", r);
            return false;
        }
        return true;
    }

    uint8 block[16];
    size_t olen = 0;
    psa_status_t st =
        psa_cipher_encrypt(k->hp, PSA_ALG_ECB_NO_PADDING, sample, 16, block, sizeof(block), &olen);
    if (st != PSA_SUCCESS || olen != sizeof(block)) {
        tlsLogErr(Error, _S"psa_cipher_encrypt(hp)", (int)st);
        return false;
    }

    memcpy(mask, block, 5);
    memset(block, 0, sizeof(block));
    return true;
}

// Applies a header protection mask to a packet, in either direction: the operation is its own
// inverse. The number of first-byte bits covered differs because a long header has four bits
// below the type field and a short header has five.
static void quicHpApply(uint8* pkt, size_t pnOff, uint8 pnLen, const uint8* mask)
{
    pkt[0] ^= mask[0] & ((pkt[0] & 0x80) ? 0x0f : 0x1f);
    for (uint8 i = 0; i < pnLen; i++)
        pkt[pnOff + i] ^= mask[1 + i];
}

// ---------------------------------------------------------------------------------------------
// Payload protection
// ---------------------------------------------------------------------------------------------

// The nonce is the IV with the packet number exclusive-ored into its low-order end, so every
// packet in a number space gets a distinct one without anything being sent on the wire.
static void quicNonce(const QuicKeys* k, uint64 pn, uint8* nonce)
{
    memcpy(nonce, k->iv, TLS13_IV_LEN);
    for (uint8 i = 0; i < 8; i++)
        nonce[TLS13_IV_LEN - 1 - i] ^= (uint8)(pn >> (i * 8));
}

_Use_decl_annotations_
bool _quicAeadSeal(const QuicKeys* k, uint64 pn, const uint8* aad, size_t aadLen, const uint8* pt,
                   size_t ptLen, uint8* out, size_t outsz, size_t* outLen)
{
    *outLen = 0;
    if (!k->valid)
        return false;

    uint8 nonce[TLS13_IV_LEN];
    quicNonce(k, pn, nonce);

    psa_status_t st = psa_aead_encrypt(k->aead, k->aeadAlg, nonce, sizeof(nonce), aad, aadLen, pt,
                                       ptLen, out, outsz, outLen);
    if (st != PSA_SUCCESS) {
        tlsLogErr(Error, _S"psa_aead_encrypt", (int)st);
        return false;
    }
    return true;
}

_Use_decl_annotations_
bool _quicAeadOpen(const QuicKeys* k, uint64 pn, const uint8* aad, size_t aadLen, const uint8* ct,
                   size_t ctLen, uint8* out, size_t outsz, size_t* outLen)
{
    *outLen = 0;
    if (!k->valid)
        return false;

    uint8 nonce[TLS13_IV_LEN];
    quicNonce(k, pn, nonce);

    // A failure here is ordinary: it is how a stateless reset, a packet for keys that have been
    // discarded, or an injected packet all show up. Logging it at all would be a denial of
    // service, so the caller decides what a failed open means.
    psa_status_t st = psa_aead_decrypt(k->aead, k->aeadAlg, nonce, sizeof(nonce), aad, aadLen, ct,
                                       ctLen, out, outsz, outLen);
    return st == PSA_SUCCESS;
}

_Use_decl_annotations_
bool _quicPktSeal(const QuicKeys* k, uint8* pkt, size_t bufsz, size_t pnOff, uint8 pnLen,
                  uint64 pn, const uint8* payload, size_t payloadLen, size_t* outLen)
{
    *outLen = 0;

    if (pnLen < 1 || pnLen > 4)
        return false;

    size_t hdrLen = pnOff + pnLen;
    if (bufsz < hdrLen || bufsz - hdrLen < payloadLen + QUIC_TAG_LEN)
        return false;

    size_t ctLen = 0;
    if (!_quicAeadSeal(k, pn, pkt, hdrLen, payload, payloadLen, pkt + hdrLen, bufsz - hdrLen,
                       &ctLen))
        return false;

    size_t total = hdrLen + ctLen;

    // The sample skips four bytes past the packet number field so that both ends can find it
    // without knowing how wide the packet number is -- the receiver has not decoded that yet.
    if (pnOff + 4 + 16 > total)
        return false;

    uint8 mask[5];
    if (!_quicHpMask(k, pkt + pnOff + 4, mask))
        return false;

    quicHpApply(pkt, pnOff, pnLen, mask);
    *outLen = total;
    return true;
}

_Use_decl_annotations_
bool _quicPktOpen(const QuicKeys* k, QuicPktHdr* h, uint8* pkt, uint64 largestPn, uint8* out,
                  size_t outsz, size_t* outLen)
{
    *outLen = 0;

    if (h->pnOff + 4 + 16 > h->pktLen)
        return false;

    // Skipped when the header has already been unprotected by an earlier attempt with a different
    // set of AEAD keys, since header protection keys do not change on a key update.
    if (h->pnLen == 0) {
        uint8 mask[5];
        if (!_quicHpMask(k, pkt + h->pnOff + 4, mask))
            return false;

        // The first byte has to be unmasked before the packet number width can be read out of it,
        // so the two are unmasked in that order rather than together.
        pkt[0] ^= mask[0] & ((pkt[0] & 0x80) ? 0x0f : 0x1f);
        uint8 pnLen = (uint8)((pkt[0] & 0x03) + 1);
        for (uint8 i = 0; i < pnLen; i++)
            pkt[h->pnOff + i] ^= mask[1 + i];

        if (!_quicHdrDecodePN(h, pkt, largestPn))
            return false;
    }

    size_t hdrLen = h->pnOff + h->pnLen;
    return _quicAeadOpen(k, h->pn, pkt, hdrLen, pkt + hdrLen, h->payloadLen, out, outsz, outLen);
}

_Use_decl_annotations_
bool _quicRetryTag(uint8* tag, const uint8* odcid, size_t odcidLen, const uint8* retry,
                   size_t retryLen)
{
    if (!_quicInit() || odcidLen > QUIC_MAX_CID)
        return false;

    // The tag covers a pseudo-packet: the connection ID the client originally chose, length
    // prefixed, followed by the whole Retry packet up to the tag itself.
    uint8 pseudo[1 + QUIC_MAX_CID];
    pseudo[0] = (uint8)odcidLen;
    memcpy(pseudo + 1, odcid, odcidLen);

    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attr, sizeof(quicRetryKey) * 8);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT);
    psa_set_key_algorithm(&attr, PSA_ALG_GCM);

    psa_key_id_t key = PSA_KEY_ID_NULL;
    psa_status_t st = psa_import_key(&attr, quicRetryKey, sizeof(quicRetryKey), &key);
    psa_reset_key_attributes(&attr);
    if (st != PSA_SUCCESS) {
        tlsLogErr(Error, _S"psa_import_key(retry)", (int)st);
        return false;
    }

    // The associated data is in two pieces and psa_aead_encrypt only takes one, so the
    // multipart form is the only way to feed it without copying the whole packet.
    psa_aead_operation_t op = PSA_AEAD_OPERATION_INIT;
    size_t olen = 0, flen = 0;
    st = psa_aead_encrypt_setup(&op, key, PSA_ALG_GCM);
    if (st == PSA_SUCCESS)
        st = psa_aead_set_nonce(&op, quicRetryNonce, sizeof(quicRetryNonce));
    if (st == PSA_SUCCESS)
        st = psa_aead_set_lengths(&op, 1 + odcidLen + retryLen, 0);
    if (st == PSA_SUCCESS)
        st = psa_aead_update_ad(&op, pseudo, 1 + odcidLen);
    if (st == PSA_SUCCESS)
        st = psa_aead_update_ad(&op, retry, retryLen);
    if (st == PSA_SUCCESS)
        st = psa_aead_finish(&op, NULL, 0, &olen, tag, QUIC_TAG_LEN, &flen);

    if (st != PSA_SUCCESS) {
        psa_aead_abort(&op);
        tlsLogErr(Error, _S"psa_aead(retry)", (int)st);
    }

    psa_destroy_key(key);
    return st == PSA_SUCCESS && flen == QUIC_TAG_LEN;
}
