// Known-answer tests for the TLS 1.3 cryptographic substrate in cxtls: HKDF-Expand-Label, the key
// schedule, the transcript hash, ephemeral key agreement, and the ClientHello / ServerHello codec.
//
// The whole RFC 8448 section 3 handshake is replayed here from its published bytes, and every
// secret, traffic key, IV and Finished value it lists is recomputed and compared. That covers the
// three pieces that are easy to get subtly wrong and hard to debug later -- the HkdfLabel encoding,
// which Extract input is the salt, and where the transcript is snapshotted -- against an
// authoritative trace rather than against cx itself.

#include <cxtls.h>

#include "../cxtls/tls13_private.h"

#include "tls13vec.h"

#define TEST_FILE  tls13test
#define TEST_FUNCS tls13test_funcs
#include "common.h"

// bool CHECK_BYTES(const char *what, const uint8 *got, size_t gotlen, const uint8 *want, size_t wantlen);
//
// Compares a computed byte string against a vector, logging the first differing offset and both
// values there. Assigns 1 to `ret` and jumps to `out` on a mismatch.
#define CHECK_BYTES(what, got, gotlen, want, wantlen)                                   \
    do {                                                                                \
        if ((size_t)(gotlen) != (size_t)(wantlen)) {                                    \
            TEST_FAILV(ret, 1, _SL("${string}: length ${int}, expected ${int}"),        \
                       stvar(strref, _S what), stvar(int64, (int64)(gotlen)),           \
                       stvar(int64, (int64)(wantlen)));                                 \
            goto out;                                                                   \
        }                                                                               \
        for (size_t _i = 0; _i < (size_t)(wantlen); _i++) {                             \
            if ((got)[_i] != (want)[_i]) {                                              \
                TEST_FAILV(ret, 1,                                                      \
                           _SL("${string}: byte ${int} is ${int}, expected ${int}"),    \
                           stvar(strref, _S what), stvar(int64, (int64)_i),             \
                           stvar(int32, (int32)(got)[_i]), stvar(int32, (int32)(want)[_i])); \
                goto out;                                                               \
            }                                                                           \
        }                                                                               \
    } while (0)

// The RFC 8448 trace uses TLS_AES_128_GCM_SHA256 throughout, so every secret in it is 32 bytes.
#define VHASH PSA_ALG_SHA_256
#define VLEN  32

// ---------------------------------------------------------------------------------------------

// The HkdfLabel encoding and HKDF-Expand-Label itself, against the info/expanded pairs the trace
// prints for each derivation. Checking the info bytes separately is what tells a label-construction
// bug apart from an HKDF bug when this fails.
static int test_tls13test_expandlabel(void)
{
    int ret = 0;
    uint8 info[512];
    size_t infoLen = 0;
    uint8 out[TLS13_MAX_HASH];

    // "derived" with the empty-string hash as context
    if (!_tls13HkdfLabel(_S"derived", tv_th_ch_sh, 0, VLEN, info, sizeof(info), &infoLen)) {
        // The context here is the SHA-256 of the empty string, which the trace prints as `hash`;
        // reuse the zero-length form only to prove the length prefix, then do the real one below.
        TEST_FAILV(ret, 1, _SL("HkdfLabel(derived) failed"), stvNone);
        goto out;
    }

    uint8 emptyHash[TLS13_MAX_HASH];
    size_t emptyLen = 0;
    psa_status_t st = psa_hash_compute(VHASH, NULL, 0, emptyHash, sizeof(emptyHash), &emptyLen);
    if (st != PSA_SUCCESS) {
        TEST_FAILV(ret, 1, _SL("psa_hash_compute failed: ${int}"), stvar(int32, (int32)st));
        goto out;
    }

    if (!_tls13HkdfLabel(_S"derived", emptyHash, emptyLen, VLEN, info, sizeof(info), &infoLen)) {
        TEST_FAILV(ret, 1, _SL("HkdfLabel(derived) failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("info(derived)", info, infoLen, tv_info_derived, sizeof(tv_info_derived));

    if (!_tls13HkdfLabel(_S"c hs traffic", tv_th_ch_sh, sizeof(tv_th_ch_sh), VLEN, info,
                         sizeof(info), &infoLen)) {
        TEST_FAILV(ret, 1, _SL("HkdfLabel(c hs traffic) failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("info(c hs traffic)", info, infoLen, tv_info_c_hs, sizeof(tv_info_c_hs));

    if (!_tls13HkdfLabel(_S"key", NULL, 0, 16, info, sizeof(info), &infoLen)) {
        TEST_FAILV(ret, 1, _SL("HkdfLabel(key) failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("info(key)", info, infoLen, tv_info_key, sizeof(tv_info_key));

    if (!_tls13HkdfLabel(_S"iv", NULL, 0, 12, info, sizeof(info), &infoLen)) {
        TEST_FAILV(ret, 1, _SL("HkdfLabel(iv) failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("info(iv)", info, infoLen, tv_info_iv, sizeof(tv_info_iv));

    if (!_tls13HkdfLabel(_S"finished", NULL, 0, VLEN, info, sizeof(info), &infoLen)) {
        TEST_FAILV(ret, 1, _SL("HkdfLabel(finished) failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("info(finished)", info, infoLen, tv_info_finished, sizeof(tv_info_finished));

    // Now the expansions themselves.
    if (!_tls13DeriveSecret(VHASH, tv_early_secret, _S"derived", emptyHash, emptyLen, out)) {
        TEST_FAILV(ret, 1, _SL("Derive-Secret(early, derived) failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("derived-for-handshake", out, VLEN, tv_derived_hs, sizeof(tv_derived_hs));

    if (!_tls13DeriveSecret(VHASH, tv_hs_secret, _S"c hs traffic", tv_th_ch_sh, sizeof(tv_th_ch_sh),
                            out)) {
        TEST_FAILV(ret, 1, _SL("Derive-Secret(c hs traffic) failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("c hs traffic", out, VLEN, tv_c_hs_traffic, sizeof(tv_c_hs_traffic));

out:
    return ret;
}

// The transcript hash, taken at the three points the trace snapshots it. Proves both that the
// running hash covers exactly the handshake messages (no record headers) and that snapshotting it
// does not end it.
static int test_tls13test_transcript(void)
{
    int ret = 0;
    Tls13Transcript tr;
    uint8 th[TLS13_MAX_HASH];

    if (!_tls13TranscriptInit(&tr, VHASH)) {
        TEST_FAILV(ret, 1, _SL("transcript init failed"), stvNone);
        return ret;
    }

    _tls13TranscriptUpdate(&tr, tv_clienthello, sizeof(tv_clienthello));
    _tls13TranscriptUpdate(&tr, tv_serverhello, sizeof(tv_serverhello));
    if (!_tls13TranscriptHash(&tr, th)) {
        TEST_FAILV(ret, 1, _SL("transcript hash (CH..SH) failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("transcript CH..SH", th, VLEN, tv_th_ch_sh, sizeof(tv_th_ch_sh));

    _tls13TranscriptUpdate(&tr, tv_encext, sizeof(tv_encext));
    _tls13TranscriptUpdate(&tr, tv_cert, sizeof(tv_cert));
    _tls13TranscriptUpdate(&tr, tv_certverify, sizeof(tv_certverify));
    _tls13TranscriptUpdate(&tr, tv_srv_fin_msg, sizeof(tv_srv_fin_msg));
    if (!_tls13TranscriptHash(&tr, th)) {
        TEST_FAILV(ret, 1, _SL("transcript hash (CH..SF) failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("transcript CH..server Finished", th, VLEN, tv_th_ch_sf, sizeof(tv_th_ch_sf));

    _tls13TranscriptUpdate(&tr, tv_cli_fin_msg, sizeof(tv_cli_fin_msg));
    if (!_tls13TranscriptHash(&tr, th)) {
        TEST_FAILV(ret, 1, _SL("transcript hash (CH..CF) failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("transcript CH..client Finished", th, VLEN, tv_th_ch_cf, sizeof(tv_th_ch_cf));

out:
    _tls13TranscriptDestroy(&tr);
    return ret;
}

// The whole key schedule, driven by the transcript computed from the trace's own message bytes
// rather than by its printed hashes -- so a transcript bug cannot hide behind a hardcoded input.
static int test_tls13test_schedule(void)
{
    int ret = 0;
    Tls13Schedule sched;
    Tls13Transcript tr;
    uint8 th[TLS13_MAX_HASH];
    uint8 secret[TLS13_MAX_HASH];
    uint8 key[TLS13_MAX_KEY];
    uint8 iv[TLS13_IV_LEN];
    uint8 chs[TLS13_MAX_HASH], shs[TLS13_MAX_HASH];

    if (!_tls13TranscriptInit(&tr, VHASH)) {
        TEST_FAILV(ret, 1, _SL("transcript init failed"), stvNone);
        return ret;
    }

    if (!_tls13SchedEarly(&sched, VHASH, NULL, 0)) {
        TEST_FAILV(ret, 1, _SL("early secret failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("early secret", sched.secret, VLEN, tv_early_secret, sizeof(tv_early_secret));

    _tls13TranscriptUpdate(&tr, tv_clienthello, sizeof(tv_clienthello));
    _tls13TranscriptUpdate(&tr, tv_serverhello, sizeof(tv_serverhello));
    _tls13TranscriptHash(&tr, th);

    if (!_tls13SchedHandshake(&sched, tv_ecdhe, sizeof(tv_ecdhe))) {
        TEST_FAILV(ret, 1, _SL("handshake secret failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("handshake secret", sched.secret, VLEN, tv_hs_secret, sizeof(tv_hs_secret));

    if (!_tls13SchedDerive(&sched, _S"c hs traffic", th, VLEN, chs) ||
        !_tls13SchedDerive(&sched, _S"s hs traffic", th, VLEN, shs)) {
        TEST_FAILV(ret, 1, _SL("handshake traffic secrets failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("c hs traffic", chs, VLEN, tv_c_hs_traffic, sizeof(tv_c_hs_traffic));
    CHECK_BYTES("s hs traffic", shs, VLEN, tv_s_hs_traffic, sizeof(tv_s_hs_traffic));

    if (!_tls13TrafficKeys(VHASH, shs, _S"key", key, 16, _S"iv", iv, 12)) {
        TEST_FAILV(ret, 1, _SL("server handshake keys failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("server hs key", key, 16, tv_s_hs_key, sizeof(tv_s_hs_key));
    CHECK_BYTES("server hs iv", iv, 12, tv_s_hs_iv, sizeof(tv_s_hs_iv));

    if (!_tls13TrafficKeys(VHASH, chs, _S"key", key, 16, _S"iv", iv, 12)) {
        TEST_FAILV(ret, 1, _SL("client handshake keys failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("client hs key", key, 16, tv_c_hs_key, sizeof(tv_c_hs_key));
    CHECK_BYTES("client hs iv", iv, 12, tv_c_hs_iv, sizeof(tv_c_hs_iv));

    // The server's Finished is an HMAC over the transcript through CertificateVerify, so it is
    // computed before its own message joins the transcript.
    _tls13TranscriptUpdate(&tr, tv_encext, sizeof(tv_encext));
    _tls13TranscriptUpdate(&tr, tv_cert, sizeof(tv_cert));
    _tls13TranscriptUpdate(&tr, tv_certverify, sizeof(tv_certverify));
    _tls13TranscriptHash(&tr, th);

    if (!_tls13Finished(VHASH, shs, th, VLEN, secret)) {
        TEST_FAILV(ret, 1, _SL("server Finished failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("server Finished", secret, VLEN, tv_s_fin, sizeof(tv_s_fin));

    _tls13TranscriptUpdate(&tr, tv_srv_fin_msg, sizeof(tv_srv_fin_msg));
    _tls13TranscriptHash(&tr, th);

    if (!_tls13SchedMaster(&sched)) {
        TEST_FAILV(ret, 1, _SL("master secret failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("master secret", sched.secret, VLEN, tv_master_secret, sizeof(tv_master_secret));

    if (!_tls13SchedDerive(&sched, _S"c ap traffic", th, VLEN, secret)) {
        TEST_FAILV(ret, 1, _SL("c ap traffic failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("c ap traffic", secret, VLEN, tv_c_ap_traffic, sizeof(tv_c_ap_traffic));

    if (!_tls13TrafficKeys(VHASH, secret, _S"key", key, 16, _S"iv", iv, 12)) {
        TEST_FAILV(ret, 1, _SL("client application keys failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("client ap key", key, 16, tv_c_ap_key, sizeof(tv_c_ap_key));
    CHECK_BYTES("client ap iv", iv, 12, tv_c_ap_iv, sizeof(tv_c_ap_iv));

    if (!_tls13SchedDerive(&sched, _S"s ap traffic", th, VLEN, secret)) {
        TEST_FAILV(ret, 1, _SL("s ap traffic failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("s ap traffic", secret, VLEN, tv_s_ap_traffic, sizeof(tv_s_ap_traffic));

    if (!_tls13TrafficKeys(VHASH, secret, _S"key", key, 16, _S"iv", iv, 12)) {
        TEST_FAILV(ret, 1, _SL("server application keys failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("server ap key", key, 16, tv_s_ap_key, sizeof(tv_s_ap_key));
    CHECK_BYTES("server ap iv", iv, 12, tv_s_ap_iv, sizeof(tv_s_ap_iv));

    if (!_tls13SchedDerive(&sched, _S"exp master", th, VLEN, secret)) {
        TEST_FAILV(ret, 1, _SL("exp master failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("exp master", secret, VLEN, tv_exp_master, sizeof(tv_exp_master));

    // The client's Finished uses the client handshake traffic secret over the transcript through
    // the server's Finished, which is where the transcript already stands.
    if (!_tls13Finished(VHASH, chs, th, VLEN, secret)) {
        TEST_FAILV(ret, 1, _SL("client Finished failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("client Finished", secret, VLEN, tv_c_fin, sizeof(tv_c_fin));

    _tls13TranscriptUpdate(&tr, tv_cli_fin_msg, sizeof(tv_cli_fin_msg));
    _tls13TranscriptHash(&tr, th);

    if (!_tls13SchedDerive(&sched, _S"res master", th, VLEN, secret)) {
        TEST_FAILV(ret, 1, _SL("res master failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("res master", secret, VLEN, tv_res_master, sizeof(tv_res_master));

out:
    _tls13TranscriptDestroy(&tr);
    return ret;
}

// Ephemeral key agreement on both groups cx implements: X25519 against the RFC 8448 trace, and
// P-256 against RFC 5903's vector, in both directions.
// The 0-RTT half of the key schedule, from RFC 8448 section 4 -- the resumed handshake, whose
// vectors are the only published ones for `client_early_traffic_secret`. Two endpoints of the same
// implementation would agree on a wrong derivation just as readily as on a right one, so this is
// what says the 0-RTT keys are the ones another implementation will compute.
static int test_tls13test_schedule_early(void)
{
    int ret = 0;

    // The resumption PSK the ticket carried, which is the whole input to the early secret.
    static const uint8 tv_res_psk[] = {
        0x4e, 0xcd, 0x0e, 0xb6, 0xec, 0x3b, 0x4d, 0x87, 0xf5, 0xd6, 0x02, 0x8f, 0x92, 0x2c, 0xa4,
        0xc5, 0x85, 0x1a, 0x27, 0x7f, 0xd4, 0x13, 0x11, 0xc9, 0xe6, 0x2d, 0x2c, 0x94, 0x92, 0xe1,
        0xc4, 0xf3
    };

    static const uint8 tv_res_early_secret[] = {
        0x9b, 0x21, 0x88, 0xe9, 0xb2, 0xfc, 0x6d, 0x64, 0xd7, 0x1d, 0xc3, 0x29, 0x90, 0x0e, 0x20,
        0xbb, 0x41, 0x91, 0x50, 0x00, 0xf6, 0x78, 0xaa, 0x83, 0x9c, 0xbb, 0x79, 0x7c, 0xb7, 0xd8,
        0x33, 0x2c
    };

    // The transcript through the ClientHello, which is everything both ends have in common at the
    // moment the client has to protect data it has heard no answer to.
    static const uint8 tv_early_th_ch[] = {
        0x08, 0xad, 0x0f, 0xa0, 0x5d, 0x7c, 0x72, 0x33, 0xb1, 0x77, 0x5b, 0xa2, 0xff, 0x9f, 0x4c,
        0x5b, 0x8b, 0x59, 0x27, 0x6b, 0x7f, 0x22, 0x7f, 0x13, 0xa9, 0x76, 0x24, 0x5f, 0x5d, 0x96,
        0x09, 0x13
    };

    static const uint8 tv_c_e_traffic[] = {
        0x3f, 0xbb, 0xe6, 0xa6, 0x0d, 0xeb, 0x66, 0xc3, 0x0a, 0x32, 0x79, 0x5a, 0xba, 0x0e, 0xff,
        0x7e, 0xaa, 0x10, 0x10, 0x55, 0x86, 0xe7, 0xbe, 0x5c, 0x09, 0x67, 0x8d, 0x63, 0xb6, 0xca,
        0xab, 0x62
    };

    Tls13Schedule sched;
    uint8 ce[TLS13_MAX_HASH];

    if (!_tls13SchedEarly(&sched, VHASH, tv_res_psk, sizeof(tv_res_psk))) {
        TEST_FAILV(ret, 1, _SL("early secret from a resumption PSK failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("early secret", sched.secret, VLEN, tv_res_early_secret,
                sizeof(tv_res_early_secret));

    if (!_tls13SchedDerive(&sched, _S"c e traffic", tv_early_th_ch, VLEN, ce)) {
        TEST_FAILV(ret, 1, _SL("client early traffic secret failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("c e traffic", ce, VLEN, tv_c_e_traffic, sizeof(tv_c_e_traffic));

out:
    return ret;
}

static int test_tls13test_ecdhe(void)
{
    int ret = 0;
    Tls13Kex cli = { 0 }, srv = { 0 };
    uint8 pub[TLS13_MAX_KEXPUB];
    uint8 shared[TLS13_MAX_KEXPUB];
    size_t publen = 0, sharedlen = 0;

    if (!_tls13KexImport(&cli, TLS13_GROUP_X25519, tv_cli_priv, sizeof(tv_cli_priv))) {
        TEST_FAILV(ret, 1, _SL("x25519 client key import failed"), stvNone);
        goto out;
    }
    if (!_tls13KexPublic(&cli, pub, sizeof(pub), &publen)) {
        TEST_FAILV(ret, 1, _SL("x25519 client public export failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("x25519 client public", pub, publen, tv_cli_pub, sizeof(tv_cli_pub));

    if (!_tls13KexImport(&srv, TLS13_GROUP_X25519, tv_srv_priv, sizeof(tv_srv_priv))) {
        TEST_FAILV(ret, 1, _SL("x25519 server key import failed"), stvNone);
        goto out;
    }
    if (!_tls13KexPublic(&srv, pub, sizeof(pub), &publen)) {
        TEST_FAILV(ret, 1, _SL("x25519 server public export failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("x25519 server public", pub, publen, tv_srv_pub, sizeof(tv_srv_pub));

    // The trace's handshake-secret IKM is exactly this shared secret, so both directions are
    // checked against it.
    if (!_tls13KexAgree(&cli, tv_srv_pub, sizeof(tv_srv_pub), shared, sizeof(shared), &sharedlen)) {
        TEST_FAILV(ret, 1, _SL("x25519 client agreement failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("x25519 shared (client side)", shared, sharedlen, tv_ecdhe, sizeof(tv_ecdhe));

    if (!_tls13KexAgree(&srv, tv_cli_pub, sizeof(tv_cli_pub), shared, sizeof(shared), &sharedlen)) {
        TEST_FAILV(ret, 1, _SL("x25519 server agreement failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("x25519 shared (server side)", shared, sharedlen, tv_ecdhe, sizeof(tv_ecdhe));

    _tls13KexDestroy(&cli);
    _tls13KexDestroy(&srv);

    if (!_tls13KexImport(&cli, TLS13_GROUP_SECP256R1, tv_p256_i_priv, sizeof(tv_p256_i_priv))) {
        TEST_FAILV(ret, 1, _SL("p256 initiator key import failed"), stvNone);
        goto out;
    }
    if (!_tls13KexPublic(&cli, pub, sizeof(pub), &publen)) {
        TEST_FAILV(ret, 1, _SL("p256 initiator public export failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("p256 initiator public", pub, publen, tv_p256_i_pub, sizeof(tv_p256_i_pub));

    if (!_tls13KexImport(&srv, TLS13_GROUP_SECP256R1, tv_p256_r_priv, sizeof(tv_p256_r_priv))) {
        TEST_FAILV(ret, 1, _SL("p256 responder key import failed"), stvNone);
        goto out;
    }
    if (!_tls13KexPublic(&srv, pub, sizeof(pub), &publen)) {
        TEST_FAILV(ret, 1, _SL("p256 responder public export failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("p256 responder public", pub, publen, tv_p256_r_pub, sizeof(tv_p256_r_pub));

    if (!_tls13KexAgree(&cli, tv_p256_r_pub, sizeof(tv_p256_r_pub), shared, sizeof(shared),
                        &sharedlen)) {
        TEST_FAILV(ret, 1, _SL("p256 initiator agreement failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("p256 shared (initiator)", shared, sharedlen, tv_p256_shared,
                sizeof(tv_p256_shared));

    if (!_tls13KexAgree(&srv, tv_p256_i_pub, sizeof(tv_p256_i_pub), shared, sizeof(shared),
                        &sharedlen)) {
        TEST_FAILV(ret, 1, _SL("p256 responder agreement failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("p256 shared (responder)", shared, sharedlen, tv_p256_shared,
                sizeof(tv_p256_shared));

out:
    _tls13KexDestroy(&cli);
    _tls13KexDestroy(&srv);
    return ret;
}

// Freshly generated keys on both groups, agreeing with each other. Catches the case where import
// works but generate produces something the agreement cannot use.
static int test_tls13test_ecdhe_generate(void)
{
    int ret          = 0;
    uint16 groups[2] = { TLS13_GROUP_X25519, TLS13_GROUP_SECP256R1 };

    for (int32 g = 0; g < 2; g++) {
        Tls13Kex a = { 0 }, b = { 0 };
        uint8 apub[TLS13_MAX_KEXPUB], bpub[TLS13_MAX_KEXPUB];
        uint8 as[TLS13_MAX_KEXPUB], bs[TLS13_MAX_KEXPUB];
        size_t apublen = 0, bpublen = 0, aslen = 0, bslen = 0;

        if (!_tls13KexGenerate(&a, groups[g]) || !_tls13KexGenerate(&b, groups[g])) {
            TEST_FAILV(ret, 1, _SL("generate failed for group ${int}"),
                       stvar(int32, (int32)groups[g]));
            goto next;
        }

        if (!_tls13KexPublic(&a, apub, sizeof(apub), &apublen) ||
            !_tls13KexPublic(&b, bpub, sizeof(bpub), &bpublen)) {
            TEST_FAILV(ret, 1, _SL("public export failed for group ${int}"),
                       stvar(int32, (int32)groups[g]));
            goto next;
        }

        if (apublen != _tls13GroupPubLen(groups[g])) {
            TEST_FAILV(ret, 1, _SL("group ${int} public is ${int} bytes, expected ${int}"),
                       stvar(int32, (int32)groups[g]), stvar(int64, (int64)apublen),
                       stvar(int64, (int64)_tls13GroupPubLen(groups[g])));
            goto next;
        }

        if (!_tls13KexAgree(&a, bpub, bpublen, as, sizeof(as), &aslen) ||
            !_tls13KexAgree(&b, apub, apublen, bs, sizeof(bs), &bslen)) {
            TEST_FAILV(ret, 1, _SL("agreement failed for group ${int}"),
                       stvar(int32, (int32)groups[g]));
            goto next;
        }

        if (aslen != bslen || memcmp(as, bs, aslen) != 0) {
            TEST_FAILV(ret, 1, _SL("group ${int} shared secrets differ"),
                       stvar(int32, (int32)groups[g]));
        }

    next:
        _tls13KexDestroy(&a);
        _tls13KexDestroy(&b);
        if (ret)
            break;
    }

    return ret;
}

// Parse the trace's two hellos, check the fields that matter, and re-encode them byte for byte.
// A codec that survives a real ClientHello -- legacy extensions and all -- round trip is the
// evidence that the length-prefix handling is right in both directions.
static int test_tls13test_hello(void)
{
    int ret = 0;
    Tls13Hello ch, sh;
    Buffer enc = NULL;

    if (!_tls13HelloParse(&ch, false, tv_clienthello, sizeof(tv_clienthello))) {
        TEST_FAILV(ret, 1, _SL("ClientHello parse failed"), stvNone);
        goto out;
    }

    if (ch.legacyVersion != 0x0303) {
        TEST_FAILV(ret, 1, _SL("ClientHello legacy_version is ${int}, expected ${int}"),
                   stvar(int32, (int32)ch.legacyVersion), stvar(int32, 0x0303));
        goto out;
    }
    if (ch.nsuites != 3 || ch.suites[0] != TLS13_AES_128_GCM_SHA256) {
        TEST_FAILV(ret, 1, _SL("ClientHello offered ${int} suites, first ${int}"),
                   stvar(int32, (int32)ch.nsuites), stvar(int32, (int32)ch.suites[0]));
        goto out;
    }
    if (ch.sessionIdLen != 0) {
        TEST_FAILV(ret, 1, _SL("ClientHello session id is ${int} bytes, expected 0"),
                   stvar(int32, (int32)ch.sessionIdLen));
        goto out;
    }

    const Tls13Ext* ks = _tls13HelloExt(&ch, TLS13_EXT_KEY_SHARE);
    if (!ks) {
        TEST_FAILV(ret, 1, _SL("ClientHello has no key_share extension"), stvNone);
        goto out;
    }

    // KeyShareClientHello: a 16-bit list of (group, key_exchange) entries. The trace offers one.
    Tls13Rd rd, list, kx;
    _tls13RdInit(&rd, ks->data, ks->len);
    if (!_tls13RdVec16(&rd, &list)) {
        TEST_FAILV(ret, 1, _SL("key_share list is malformed"), stvNone);
        goto out;
    }
    uint16 group = _tls13Rd16(&list);
    if (!_tls13RdVec16(&list, &kx)) {
        TEST_FAILV(ret, 1, _SL("key_share entry is malformed"), stvNone);
        goto out;
    }
    if (group != TLS13_GROUP_X25519) {
        TEST_FAILV(ret, 1, _SL("key_share group is ${int}, expected ${int}"),
                   stvar(int32, (int32)group), stvar(int32, TLS13_GROUP_X25519));
        goto out;
    }
    CHECK_BYTES("ClientHello key_share", kx.p, _tls13RdLeft(&kx), tv_cli_pub, sizeof(tv_cli_pub));

    enc = _tls13HelloEncode(&ch, false);
    if (!enc) {
        TEST_FAILV(ret, 1, _SL("ClientHello encode failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("ClientHello re-encode", enc->data, enc->len, tv_clienthello,
                sizeof(tv_clienthello));
    bufDestroy(&enc);

    if (!_tls13HelloParse(&sh, true, tv_serverhello, sizeof(tv_serverhello))) {
        TEST_FAILV(ret, 1, _SL("ServerHello parse failed"), stvNone);
        goto out;
    }
    if (sh.nsuites != 1 || sh.suites[0] != TLS13_AES_128_GCM_SHA256) {
        TEST_FAILV(ret, 1, _SL("ServerHello chose suite ${int}, expected ${int}"),
                   stvar(int32, (int32)sh.suites[0]), stvar(int32, TLS13_AES_128_GCM_SHA256));
        goto out;
    }

    const Tls13Ext* sv = _tls13HelloExt(&sh, TLS13_EXT_SUPPORTED_VERSIONS);
    if (!sv || sv->len != 2 || sv->data[0] != 0x03 || sv->data[1] != 0x04) {
        TEST_FAILV(ret, 1, _SL("ServerHello supported_versions is not TLS 1.3"), stvNone);
        goto out;
    }

    enc = _tls13HelloEncode(&sh, true);
    if (!enc) {
        TEST_FAILV(ret, 1, _SL("ServerHello encode failed"), stvNone);
        goto out;
    }
    CHECK_BYTES("ServerHello re-encode", enc->data, enc->len, tv_serverhello,
                sizeof(tv_serverhello));

out:
    bufDestroy(&enc);
    return ret;
}

// A truncated or internally inconsistent hello must be rejected rather than read past its end.
static int test_tls13test_hello_malformed(void)
{
    int ret = 0;
    Tls13Hello h;

    for (size_t n = 0; n < sizeof(tv_clienthello); n++) {
        if (_tls13HelloParse(&h, false, tv_clienthello, n)) {
            TEST_FAILV(ret, 1, _SL("ClientHello truncated to ${int} bytes parsed as valid"),
                       stvar(int64, (int64)n));
            return ret;
        }
    }

    // One byte too many is malformed as well: the declared body length must account for everything.
    uint8 extra[sizeof(tv_clienthello) + 1];
    memcpy(extra, tv_clienthello, sizeof(tv_clienthello));
    extra[sizeof(tv_clienthello)] = 0;
    if (_tls13HelloParse(&h, false, extra, sizeof(extra))) {
        TEST_FAILV(ret, 1, _SL("ClientHello with a trailing byte parsed as valid"), stvNone);
        return ret;
    }

    // A ClientHello is not a ServerHello, whichever way round it is read.
    if (_tls13HelloParse(&h, true, tv_clienthello, sizeof(tv_clienthello))) {
        TEST_FAILV(ret, 1, _SL("ClientHello parsed as a ServerHello"), stvNone);
        return ret;
    }
    if (_tls13HelloParse(&h, false, tv_serverhello, sizeof(tv_serverhello))) {
        TEST_FAILV(ret, 1, _SL("ServerHello parsed as a ClientHello"), stvNone);
        return ret;
    }

    return ret;
}

testfunc tls13test_funcs[] = {
    { "expandlabel", test_tls13test_expandlabel },
    { "transcript", test_tls13test_transcript },
    { "schedule", test_tls13test_schedule },
    { "schedule_early", test_tls13test_schedule_early },
    { "ecdhe", test_tls13test_ecdhe },
    { "ecdhe_generate", test_tls13test_ecdhe_generate },
    { "hello", test_tls13test_hello },
    { "hello_malformed", test_tls13test_hello_malformed },
    { NULL, NULL },
};
