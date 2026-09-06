// Address validation, and the three packets a server sends to a client it has no connection with.
//
// Retry, Version Negotiation and Stateless Reset all answer a datagram for which no connection
// state exists -- or, in the case of Retry, for which none should exist yet. None of them is
// encrypted under connection keys, and none of them needs a connection to build, which is why
// they live here rather than in the state machine.

#include "conn_private.h"

#include <cx/time/clock.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL QuicLogChannel

#define QUIC_TOKEN_NONCE 12
#define QUIC_TOKEN_VER   1

// Number of address bytes that identify a peer, by address family. The port is deliberately left
// out: a NAT rebinding changes a client's port without moving it, and a token that stopped working
// then would send the client back through a Retry for no reason.
static size_t addrBytes(_In_ const NetAddr* peer, _Outptr_ const uint8** out)
{
    if (peer->type == NA_IPv6) {
        *out = peer->ipv6;
        return sizeof(peer->ipv6);
    }

    *out = peer->ipv4;
    return sizeof(peer->ipv4);
}

static bool tokenKey(_In_reads_bytes_(32) const uint8* key, _Out_ psa_key_id_t* out)
{
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attr, PSA_ALG_GCM);
    psa_set_key_type(&attr, PSA_KEY_TYPE_AES);

    psa_status_t st = psa_import_key(&attr, key, 32, out);
    if (st != PSA_SUCCESS) {
        logFmt(Warn, _SL("QUIC token key import failed: ${int}"), stvar(int32, (int32)st));
        return false;
    }
    return true;
}

_Use_decl_annotations_
bool _quicTokenSeal(uint8* out, size_t* outLen, const uint8* key, bool retry,
                    const NetAddr* peer, const QuicCid* odcid, int64 now)
{
    *outLen = 0;

    uint8 plain[1 + 1 + 8 + 1 + QUIC_MAX_CID];
    size_t plainLen = 0;

    plain[plainLen++] = QUIC_TOKEN_VER;
    plain[plainLen++] = retry ? 1 : 0;
    for (int i = 7; i >= 0; i--)
        plain[plainLen++] = (uint8)((uint64)now >> (i * 8));

    // Only a Retry token names a connection. A NEW_TOKEN token is for a connection that does not
    // exist yet, so there is nothing to bind it to beyond the address.
    uint8 cidLen = (retry && odcid) ? odcid->len : 0;
    if (cidLen > QUIC_MAX_CID)
        return false;

    plain[plainLen++] = cidLen;
    if (cidLen > 0)
        memcpy(plain + plainLen, odcid->id, cidLen);
    plainLen += cidLen;

    const uint8* aad;
    size_t aadLen = addrBytes(peer, &aad);

    if (QUIC_TOKEN_NONCE + plainLen + QUIC_TAG_LEN > QUIC_MAX_TOKEN)
        return false;

    psa_key_id_t k;
    if (!tokenKey(key, &k))
        return false;

    psa_status_t st = psa_generate_random(out, QUIC_TOKEN_NONCE);
    if (st == PSA_SUCCESS) {
        size_t n;
        st = psa_aead_encrypt(k, PSA_ALG_GCM, out, QUIC_TOKEN_NONCE, aad, aadLen, plain, plainLen,
                              out + QUIC_TOKEN_NONCE, QUIC_MAX_TOKEN - QUIC_TOKEN_NONCE, &n);
        if (st == PSA_SUCCESS)
            *outLen = QUIC_TOKEN_NONCE + n;
    }

    psa_destroy_key(k);
    return st == PSA_SUCCESS;
}

_Use_decl_annotations_
bool _quicTokenOpen(const uint8* token, size_t len, const uint8* key, const NetAddr* peer,
                    int64 now, bool* retry, QuicCid* odcid)
{
    memset(odcid, 0, sizeof(*odcid));
    *retry = false;

    if (len <= QUIC_TOKEN_NONCE + QUIC_TAG_LEN || len > QUIC_MAX_TOKEN)
        return false;

    const uint8* aad;
    size_t aadLen = addrBytes(peer, &aad);

    psa_key_id_t k;
    if (!tokenKey(key, &k))
        return false;

    uint8 plain[QUIC_MAX_TOKEN];
    size_t n;
    psa_status_t st = psa_aead_decrypt(k, PSA_ALG_GCM, token, QUIC_TOKEN_NONCE, aad, aadLen,
                                       token + QUIC_TOKEN_NONCE, len - QUIC_TOKEN_NONCE, plain,
                                       sizeof(plain), &n);
    psa_destroy_key(k);

    // A token that does not authenticate is the ordinary case -- a token issued to another
    // address, or before a key rotation -- and means the address is simply unvalidated.
    if (st != PSA_SUCCESS || n < 11 || plain[0] != QUIC_TOKEN_VER)
        return false;

    bool isRetry = plain[1] != 0;

    int64 issued = 0;
    for (int i = 0; i < 8; i++)
        issued = (int64)(((uint64)issued << 8) | plain[2 + i]);

    int64 age = now - issued;
    if (age < 0 || age > (isRetry ? QUIC_RETRY_TOKEN_LIFETIME : QUIC_NEW_TOKEN_LIFETIME))
        return false;

    uint8 cidLen = plain[10];
    if (cidLen > QUIC_MAX_CID || n != (size_t)11 + cidLen)
        return false;

    odcid->len = cidLen;
    memcpy(odcid->id, plain + 11, cidLen);
    *retry = isRetry;
    return true;
}

_Use_decl_annotations_
size_t _quicRetryBuild(uint8* out, size_t outsz, uint32 version, const QuicCid* odcid,
                       const QuicCid* clientScid, const QuicCid* retryScid, const uint8* token,
                       size_t tokenLen)
{
    uint8 zeroTag[QUIC_TAG_LEN] = { 0 };

    QuicPktHdr h = { 0 };
    h.type       = QUIC_PKT_RETRY;
    h.version    = version;
    h.dcid       = *clientScid;
    h.scid       = *retryScid;
    h.token      = token;
    h.tokenLen   = tokenLen;
    h.tag        = zeroTag;

    QuicWr wr;
    _quicWrInit(&wr, out, outsz);
    if (!_quicHdrEncode(&wr, &h))
        return 0;

    // The tag covers the whole packet up to itself, prefixed with the connection ID the client
    // originally used. Encoding with a placeholder and overwriting keeps one encoder rather than
    // a second one that stops just short of the tag.
    size_t len = _quicWrLen(&wr);
    if (!_quicRetryTag(out + len - QUIC_TAG_LEN, odcid->id, odcid->len, out, len - QUIC_TAG_LEN))
        return 0;

    return len;
}

_Use_decl_annotations_
size_t _quicVersionNegBuild(uint8* out, size_t outsz, const QuicCid* dcid, const QuicCid* scid,
                            const uint32* versions, size_t nversions)
{
    QuicPktHdr h = { 0 };
    h.type       = QUIC_PKT_VERSIONNEG;

    // The connection IDs are swapped: a client matches the packet to its connection attempt by
    // finding its own Source Connection ID in the Destination field.
    h.dcid = *scid;
    h.scid = *dcid;

    QuicWr wr;
    _quicWrInit(&wr, out, outsz);
    if (!_quicHdrEncode(&wr, &h))
        return 0;

    for (size_t i = 0; i < nversions; i++)
        _quicWr32(&wr, versions[i]);

    return wr.bad ? 0 : _quicWrLen(&wr);
}

_Use_decl_annotations_
size_t _quicStatelessResetBuild(uint8* out, size_t outsz, size_t maxLen, const uint8* token)
{
    // The packet has to be long enough to be mistaken for a real one -- a short header, a
    // connection ID, a packet number and something to decrypt -- or an observer could recognize
    // resets by their size alone. RFC 9000 section 10.3 puts the floor at 21 bytes of padding
    // before the token.
    size_t len = maxLen < outsz ? maxLen : outsz;
    if (len < 21 + QUIC_RESET_TOKEN_LEN)
        return 0;

    // Cap the size so a reset is never much larger than it needs to be, and randomize the length
    // so successive resets from the same endpoint do not look alike.
    uint8 jitter = 0;
    if (psa_generate_random(&jitter, 1) != PSA_SUCCESS)
        return 0;

    size_t want = 21 + QUIC_RESET_TOKEN_LEN + (jitter & 0x1f);
    if (want < len)
        len = want;

    if (psa_generate_random(out, len - QUIC_RESET_TOKEN_LEN) != PSA_SUCCESS)
        return 0;

    // The first two bits have to say "short header" or the packet is not plausible as one.
    out[0] = (uint8)((out[0] & 0x3f) | 0x40);
    memcpy(out + len - QUIC_RESET_TOKEN_LEN, token, QUIC_RESET_TOKEN_LEN);

    return len;
}
