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
