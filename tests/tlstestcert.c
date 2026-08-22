// Test PKI, minted at runtime.
//
// A self-signed CA plus the leaf certificates the TLS tests need, generated fresh on every run
// rather than checked in as PEM. That costs a couple of P-256 keygens per test file and buys two
// things worth more: a suite that cannot start failing on a date years from now, and no private
// key living in the repository.

#include "tlstestcert.h"

#include <cx/string.h>

#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

// Wide enough for a PEM certificate or key with room to spare; these are P-256, so the real
// output is a fraction of this.
#define PEMBUF 4096

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

void tlsTestPKIDestroy(_Inout_ TlsTestPKI* pki)
{
    strDestroy(&pki->caCert);
    strDestroy(&pki->serverCert);
    strDestroy(&pki->serverKey);
    strDestroy(&pki->altCert);
    strDestroy(&pki->altKey);
    strDestroy(&pki->otherCACert);
}
