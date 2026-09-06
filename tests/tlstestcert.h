#pragma once
// A throwaway PKI for the TLS tests. See tlstestcert.c.

#include <cx/cx.h>
#include <cx/string.h>

// The name the server leaf certificate carries in its SAN, and therefore the only name a verifying
// client can successfully connect under.
#define TLS_TEST_HOSTNAME "cxtest.invalid"

// The name on the second identity, used to prove a certificate rotation actually took effect.
#define TLS_TEST_ALT_HOSTNAME "cxrotated.invalid"

typedef struct TlsTestPKI {
    string caCert;        ///< Self-signed CA, PEM
    string serverCert;    ///< Leaf for TLS_TEST_HOSTNAME, issued by the CA, PEM
    string serverKey;     ///< Private key for serverCert, PEM
    string altCert;       ///< Leaf for TLS_TEST_ALT_HOSTNAME, issued by the same CA, PEM
    string altKey;        ///< Private key for altCert, PEM
    string otherCACert;   ///< An unrelated self-signed CA that issued none of the above, PEM
} TlsTestPKI;

// Mint the whole set. Returns false and leaves nothing behind if any step fails.
bool tlsTestPKIInit(_Out_ TlsTestPKI* pki);
void tlsTestPKIDestroy(_Inout_ TlsTestPKI* pki);

// Key size for the RSA identity below. Small enough that generating one per test run is cheap,
// large enough that mbedTLS will actually use it.
#define TLS_TEST_RSA_BITS 2048

// A standalone self-signed RSA identity, carrying the one private key in both of the encodings
// deployments ship it in. Separate from TlsTestPKI because an RSA key generation costs far more
// than the P-256 ones and only the key-format test needs it.
typedef struct TlsTestRSAIdentity {
    string cert;       ///< Self-signed leaf for TLS_TEST_HOSTNAME, PEM
    string keyPKCS1;   ///< The private key as "BEGIN RSA PRIVATE KEY", PEM
    string keyPKCS8;   ///< The same private key as "BEGIN PRIVATE KEY", PEM
} TlsTestRSAIdentity;

bool tlsTestRSAIdentity(_Out_ TlsTestRSAIdentity* id);
void tlsTestRSAIdentityDestroy(_Inout_ TlsTestRSAIdentity* id);
