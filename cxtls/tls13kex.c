#include "tls13_private.h"
#include "tls_private.h"

#include <cx/format.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL TlsLogChannel

// PSA's description of a named group: which curve family and size, and how long the key_exchange
// octet string on the wire is.
typedef struct Tls13GroupInfo {
    uint16 group;
    uint16 bits;
    uint8 pubLen;
    psa_ecc_family_t family;
} Tls13GroupInfo;

static const Tls13GroupInfo tls13Groups[] = {
    // Uncompressed point: 0x04 followed by both coordinates.
    { TLS13_GROUP_SECP256R1, 256, 65, PSA_ECC_FAMILY_SECP_R1 },
    // Montgomery curves send the u coordinate alone.
    { TLS13_GROUP_X25519, 255, 32, PSA_ECC_FAMILY_MONTGOMERY },
};

static _Ret_maybenull_ const Tls13GroupInfo* tls13GroupInfo(uint16 group)
{
    for (size_t i = 0; i < sizeof(tls13Groups) / sizeof(tls13Groups[0]); i++) {
        if (tls13Groups[i].group == group)
            return &tls13Groups[i];
    }
    return NULL;
}

size_t _tls13GroupPubLen(uint16 group)
{
    const Tls13GroupInfo* gi = tls13GroupInfo(group);
    return gi ? gi->pubLen : 0;
}

// Attributes shared by generate and import: an ephemeral agreement key that is never persisted and
// is only ever used to derive a shared secret and to export its own public half.
static void tls13KexAttrs(_Out_ psa_key_attributes_t* attr, _In_ const Tls13GroupInfo* gi)
{
    psa_key_attributes_t init = PSA_KEY_ATTRIBUTES_INIT;
    *attr                     = init;

    psa_set_key_usage_flags(attr, PSA_KEY_USAGE_DERIVE | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(attr, PSA_ALG_ECDH);
    psa_set_key_type(attr, PSA_KEY_TYPE_ECC_KEY_PAIR(gi->family));
    psa_set_key_bits(attr, gi->bits);
}

_Use_decl_annotations_
bool _tls13KexGenerate(Tls13Kex* kex, uint16 group)
{
    memset(kex, 0, sizeof(*kex));

    const Tls13GroupInfo* gi = tls13GroupInfo(group);
    if (!gi || !tlsInit())
        return false;

    psa_key_attributes_t attr;
    tls13KexAttrs(&attr, gi);

    psa_key_id_t kid = PSA_KEY_ID_NULL;
    psa_status_t st  = psa_generate_key(&attr, &kid);
    if (st != PSA_SUCCESS) {
        tlsLogErr(Error, _S"psa_generate_key", (int)st);
        return false;
    }

    kex->key   = kid;
    kex->group = group;
    return true;
}

_Use_decl_annotations_
bool _tls13KexImport(Tls13Kex* kex, uint16 group, const uint8* priv, size_t privLen)
{
    memset(kex, 0, sizeof(*kex));

    const Tls13GroupInfo* gi = tls13GroupInfo(group);
    if (!gi || !tlsInit())
        return false;

    psa_key_attributes_t attr;
    tls13KexAttrs(&attr, gi);

    psa_key_id_t kid = PSA_KEY_ID_NULL;
    psa_status_t st  = psa_import_key(&attr, priv, privLen, &kid);
    if (st != PSA_SUCCESS) {
        tlsLogErr(Error, _S"psa_import_key", (int)st);
        return false;
    }

    kex->key   = kid;
    kex->group = group;
    return true;
}

_Use_decl_annotations_
bool _tls13KexPublic(const Tls13Kex* kex, uint8* buf, size_t bufsz, size_t* len)
{
    *len = 0;

    const Tls13GroupInfo* gi = tls13GroupInfo(kex->group);
    if (!gi || bufsz < gi->pubLen)
        return false;

    size_t got      = 0;
    psa_status_t st = psa_export_public_key(kex->key, buf, bufsz, &got);
    if (st != PSA_SUCCESS || got != gi->pubLen) {
        tlsLogErr(Error, _S"psa_export_public_key", (int)st);
        return false;
    }

    *len = got;
    return true;
}

_Use_decl_annotations_
bool _tls13KexAgree(const Tls13Kex* kex, const uint8* peer, size_t peerLen, uint8* buf, size_t bufsz,
                    size_t* len)
{
    *len = 0;

    const Tls13GroupInfo* gi = tls13GroupInfo(kex->group);
    if (!gi || peerLen != gi->pubLen)
        return false;

    size_t got      = 0;
    psa_status_t st = psa_raw_key_agreement(PSA_ALG_ECDH, kex->key, peer, peerLen, buf, bufsz, &got);
    if (st != PSA_SUCCESS) {
        // A peer point that is not on the curve, or an all-zero X25519 result, lands here. Both are
        // handshake failures rather than internal errors, so this is only worth a warning.
        tlsLogErr(Warn, _S"psa_raw_key_agreement", (int)st);
        return false;
    }

    *len = got;
    return true;
}

_Use_decl_annotations_
void _tls13KexDestroy(Tls13Kex* kex)
{
    if (kex->key != PSA_KEY_ID_NULL) {
        psa_destroy_key(kex->key);
        kex->key = PSA_KEY_ID_NULL;
    }
    kex->group = 0;
}
