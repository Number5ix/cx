// Test PKI, minted at runtime.
//
// A self-signed CA plus the leaf certificates the TLS tests need, generated fresh on every run
// rather than checked in as PEM. That costs a couple of P-256 keygens per test file and buys two
// things worth more: a suite that cannot start failing on a date years from now, and no private
// key living in the repository.

#include "tlstestcert.h"

#include <cx/string.h>

#include <mbedtls/asn1write.h>
#include <mbedtls/pem.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

// Wide enough for a PEM certificate or key with room to spare; these are P-256, so the real
// output is a fraction of this.
#define PEMBUF 4096

// Room for an RSA-2048 private key in either encoding, DER or PEM.
#define RSABUF 4096

// Serial numbers only have to be distinct within an issuer, and these chains are three
// certificates long at most.
static uint8 nextSerial = 1;

// Generate a P-256 key pair and hand it back as a pk context. The key is exportable because
// mbedtls_pk_copy_from_psa() has to read it out to build the context, and because the leaf's key
// is written to PEM for TlsCreds to parse back in -- which is the same path a real deployment
// takes, and therefore the one worth testing.
static bool genKey(mbedtls_pk_context* pk)
{
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attr,
                            PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_VERIFY_HASH |
                                PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, 256);

    mbedtls_svc_key_id_t id = MBEDTLS_SVC_KEY_ID_INIT;
    if (psa_generate_key(&attr, &id) != PSA_SUCCESS)
        return false;

    bool ok = mbedtls_pk_copy_from_psa(id, pk) == 0;

    // The pk context holds its own copy, so the PSA slot has done its job.
    psa_destroy_key(id);
    return ok;
}

// Write one certificate. `issuerPk` NULL means self-signed, which is how the CA is made.
static bool writeCert(_Inout_ strhandle out, _In_ mbedtls_pk_context* subjectPk,
                      _In_opt_ mbedtls_pk_context* issuerPk, const char* subject,
                      const char* issuer, bool isCA, _In_opt_z_ const char* dnsName)
{
    mbedtls_x509write_cert crt;
    mbedtls_x509write_crt_init(&crt);

    uint8 serial[1] = { nextSerial++ };
    bool ok         = true;

    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, subjectPk);
    mbedtls_x509write_crt_set_issuer_key(&crt, issuerPk ? issuerPk : subjectPk);

    ok = ok && mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial)) == 0;
    ok = ok && mbedtls_x509write_crt_set_subject_name(&crt, subject) == 0;
    ok = ok && mbedtls_x509write_crt_set_issuer_name(&crt, issuer) == 0;

    // Deliberately not "valid from now": a clock skewed a few seconds behind the machine that
    // minted these would otherwise reject a certificate created moments earlier.
    ok = ok && mbedtls_x509write_crt_set_validity(&crt, "20200101000000", "20991231235959") == 0;
    ok = ok && mbedtls_x509write_crt_set_basic_constraints(&crt, isCA ? 1 : 0, isCA ? 1 : -1) == 0;
    ok = ok && mbedtls_x509write_crt_set_subject_key_identifier(&crt) == 0;
    ok = ok && mbedtls_x509write_crt_set_authority_key_identifier(&crt) == 0;

    if (dnsName) {
        // The name verification tests turn on this extension: mbedTLS checks the SAN, not the
        // subject CN, so a leaf without one cannot match any hostname at all.
        mbedtls_x509_san_list san;
        memset(&san, 0, sizeof(san));
        san.node.type                      = MBEDTLS_X509_SAN_DNS_NAME;
        san.node.san.unstructured_name.p   = (unsigned char*)(uintptr_t)dnsName;
        san.node.san.unstructured_name.len = strlen(dnsName);
        san.next                           = NULL;

        ok = ok && mbedtls_x509write_crt_set_subject_alternative_name(&crt, &san) == 0;
    }

    if (ok) {
        unsigned char buf[PEMBUF];
        ok = mbedtls_x509write_crt_pem(&crt, buf, sizeof(buf)) == 0;
        if (ok)
            strFromBytes(out, buf, (uint32)strlen((const char*)buf));
    }

    mbedtls_x509write_crt_free(&crt);
    return ok;
}

bool tlsTestPKIInit(_Out_ TlsTestPKI* pki)
{
    memset(pki, 0, sizeof(*pki));

    // Certificate writing signs with the issuer key, and signing is PSA in 4.x.
    if (psa_crypto_init() != PSA_SUCCESS)
        return false;

    mbedtls_pk_context caKey, leafKey;
    mbedtls_pk_init(&caKey);
    mbedtls_pk_init(&leafKey);

    bool ok = genKey(&caKey) && genKey(&leafKey);

    ok = ok && writeCert(&pki->caCert, &caKey, NULL, "CN=cx test CA", "CN=cx test CA", true, NULL);
    ok = ok &&
        writeCert(&pki->serverCert,
                  &leafKey,
                  &caKey,
                  "CN=" TLS_TEST_HOSTNAME,
                  "CN=cx test CA",
                  false,
                  TLS_TEST_HOSTNAME);

    if (ok) {
        unsigned char buf[PEMBUF];
        ok = mbedtls_pk_write_key_pem(&leafKey, buf, sizeof(buf)) == 0;
        if (ok)
            strFromBytes(&pki->serverKey, buf, (uint32)strlen((const char*)buf));
    }

    // A second identity under the same CA, so the rotation test can tell which certificate a
    // connection actually got.
    mbedtls_pk_context altKey;
    mbedtls_pk_init(&altKey);

    ok = ok && genKey(&altKey);
    ok = ok &&
        writeCert(&pki->altCert,
                  &altKey,
                  &caKey,
                  "CN=" TLS_TEST_ALT_HOSTNAME,
                  "CN=cx test CA",
                  false,
                  TLS_TEST_ALT_HOSTNAME);

    if (ok) {
        unsigned char buf[PEMBUF];
        ok = mbedtls_pk_write_key_pem(&altKey, buf, sizeof(buf)) == 0;
        if (ok)
            strFromBytes(&pki->altKey, buf, (uint32)strlen((const char*)buf));
    }

    // And an unrelated self-signed CA, which nothing here is issued under -- the trust-failure
    // test verifies against this one and must not succeed.
    mbedtls_pk_context otherKey;
    mbedtls_pk_init(&otherKey);

    ok = ok && genKey(&otherKey);
    ok = ok &&
        writeCert(&pki->otherCACert,
                  &otherKey,
                  NULL,
                  "CN=cx other CA",
                  "CN=cx other CA",
                  true,
                  NULL);

    mbedtls_pk_free(&otherKey);
    mbedtls_pk_free(&altKey);
    mbedtls_pk_free(&leafKey);
    mbedtls_pk_free(&caKey);

    if (!ok)
        tlsTestPKIDestroy(pki);

    return ok;
}

// Generate an RSA key pair and hand it back as a pk context, the RSA counterpart of genKey().
static bool genRSAKey(mbedtls_pk_context* pk)
{
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attr,
                            PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_VERIFY_HASH |
                                PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attr, PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256));
    psa_set_key_type(&attr, PSA_KEY_TYPE_RSA_KEY_PAIR);
    psa_set_key_bits(&attr, TLS_TEST_RSA_BITS);

    mbedtls_svc_key_id_t id = MBEDTLS_SVC_KEY_ID_INIT;
    if (psa_generate_key(&attr, &id) != PSA_SUCCESS)
        return false;

    bool ok = mbedtls_pk_copy_from_psa(id, pk) == 0;

    psa_destroy_key(id);
    return ok;
}

// Wrap an RSAPrivateKey (PKCS#1) in a PrivateKeyInfo (PKCS#8) and write the result as PEM.
// mbedTLS only ever emits RSA keys in PKCS#1, so nothing in its own API produces the encoding
// OpenSSL has defaulted to since 3.0 and this is the only way for a test to get one.
//
//   PrivateKeyInfo ::= SEQUENCE { version INTEGER, algorithm AlgorithmIdentifier,
//                                 privateKey OCTET STRING }
//
// asn1write fills the buffer back to front, so the fields go in reverse order and `p` walks down.
static int wrapPKCS8(unsigned char* buf, size_t bufsize, const unsigned char* pkcs1,
                     size_t pkcs1len, unsigned char** start)
{
    // rsaEncryption, 1.2.840.113549.1.1.1. Spelled out rather than taken from mbedTLS, whose OID
    // table is a private header.
    static const char oidRSA[] = "\x2a\x86\x48\x86\xf7\x0d\x01\x01\x01";

    unsigned char* p = buf + bufsize;
    size_t len       = 0;
    int ret;

    MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_octet_string(&p, buf, pkcs1, pkcs1len));
    MBEDTLS_ASN1_CHK_ADD(
        len, mbedtls_asn1_write_algorithm_identifier(&p, buf, oidRSA, sizeof(oidRSA) - 1, 0));
    MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_int(&p, buf, 0));
    MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_len(&p, buf, len));
    MBEDTLS_ASN1_CHK_ADD(
        len, mbedtls_asn1_write_tag(&p, buf, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE));

    *start = p;
    return (int)len;
}

bool tlsTestRSAIdentity(_Out_ TlsTestRSAIdentity* id)
{
    memset(id, 0, sizeof(*id));

    if (psa_crypto_init() != PSA_SUCCESS)
        return false;

    mbedtls_pk_context key;
    mbedtls_pk_init(&key);

    bool ok = genRSAKey(&key);
    ok      = ok &&
        writeCert(&id->cert,
                  &key,
                  NULL,
                  "CN=" TLS_TEST_HOSTNAME,
                  "CN=" TLS_TEST_HOSTNAME,
                  false,
                  TLS_TEST_HOSTNAME);

    if (ok) {
        unsigned char buf[RSABUF];
        ok = mbedtls_pk_write_key_pem(&key, buf, sizeof(buf)) == 0;
        if (ok)
            strFromBytes(&id->keyPKCS1, buf, (uint32)strlen((const char*)buf));
    }

    if (ok) {
        unsigned char der[RSABUF];
        int derlen = mbedtls_pk_write_key_der(&key, der, sizeof(der));
        ok         = derlen > 0;

        unsigned char p8[RSABUF];
        unsigned char* p8start = NULL;
        int p8len              = 0;
        if (ok) {
            // write_key_der also fills back to front, so the DER sits at the end of the buffer.
            p8len = wrapPKCS8(p8, sizeof(p8), der + sizeof(der) - derlen, (size_t)derlen, &p8start);
            ok    = p8len > 0;
        }

        unsigned char pem[RSABUF];
        size_t pemlen = 0;
        ok            = ok && mbedtls_pem_write_buffer("-----BEGIN PRIVATE KEY-----\n",
                                            "-----END PRIVATE KEY-----\n",
                                            p8start,
                                            (size_t)p8len,
                                            pem,
                                            sizeof(pem),
                                            &pemlen) == 0;
        if (ok)
            strFromBytes(&id->keyPKCS8, pem, (uint32)strlen((const char*)pem));
    }

    mbedtls_pk_free(&key);

    if (!ok)
        tlsTestRSAIdentityDestroy(id);

    return ok;
}

void tlsTestRSAIdentityDestroy(_Inout_ TlsTestRSAIdentity* id)
{
    strDestroy(&id->cert);
    strDestroy(&id->keyPKCS1);
    strDestroy(&id->keyPKCS8);
}

void tlsTestPKIDestroy(_Inout_ TlsTestPKI* pki)
{
    strDestroy(&pki->caCert);
    strDestroy(&pki->serverCert);
    strDestroy(&pki->serverKey);
    strDestroy(&pki->altCert);
    strDestroy(&pki->altKey);
    strDestroy(&pki->otherCACert);
}
