// The platform's default set of trusted certificate authorities.
//
// mbedTLS ships no trust store of its own and has no way to find one, so every platform needs its
// own answer here. Unix distributions all publish a CA bundle, but each in its own place, so the
// only portable approach is to try the known locations in order and take the first that exists.
// Windows keeps its roots in certificate stores rather than files and has no mbedTLS bridge at
// all, so the stores are enumerated through CryptoAPI and each root handed over as DER.
//
// Two things that snapshot cannot reproduce, both worth knowing before relying on it. Windows
// populates its root store *lazily*, fetching a root from Windows Update the first time a
// verification needs one, so enumerating it on a fresh install can return a small fraction of the
// Microsoft root program. And Windows gates trust with a signed certificate trust list on top of
// the stores; the Disallowed store is honored below, but the CTL is not, so a root Microsoft has
// wound down through the CTL alone still looks trusted here. Both are inherent to copying anchors
// into mbedTLS, and are the argument for a Windows-native verification path -- handing the chain
// to CertGetCertificateChain from a verify callback -- as the eventual answer rather than this.

#include "tls_private.h"

#include <cx/fs/fs.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL TlsLogChannel

#if !defined(_PLATFORM_WIN)

// Every path here is plain ASCII, so these go straight to mbedTLS's own file routines rather than
// through cx's filesystem layer. TlsCAStore::addFile() does the opposite for exactly the reason
// that does not apply here: a caller-supplied path may be UTF-8, and mbedTLS reaches for fopen().

// Single-file bundles, most specific first: Debian/Ubuntu, RHEL/Fedora, OpenSUSE, older SUSE,
// Alpine and OpenBSD, FreeBSD ports, then NetBSD.
static const strref kCABundles[] = {
    _S"/etc/ssl/certs/ca-certificates.crt",
    _S"/etc/pki/tls/certs/ca-bundle.crt",
    _S"/etc/ssl/ca-bundle.pem",
    _S"/var/lib/ca-certificates/ca-bundle.pem",
    _S"/etc/ssl/cert.pem",
    _S"/usr/local/share/certs/ca-root-nss.crt",
    _S"/etc/openssl/certs/ca-certificates.crt",
};

// Hashed CA directories, tried only when no single-file bundle was found. Walking a directory
// costs one open per root, so it is a distinctly worse deal than a bundle and deliberately second.
static const strref kCADirs[] = {
    _S"/etc/ssl/certs",
    _S"/etc/pki/tls/certs",
    _S"/system/etc/security/cacerts",
};

_Use_decl_annotations_
int32 _tlsSystemCALoad(mbedtls_x509_crt* chain)
{
    int32 before = _tlsChainCount(chain);

    for (int i = 0; i < (int)(sizeof(kCABundles) / sizeof(kCABundles[0])); i++) {
        if (fsStat(kCABundles[i], NULL) != FS_File)
            continue;

        int ret = mbedtls_x509_crt_parse_file(chain, strC(kCABundles[i]));

        // A positive return counts the certificates in the bundle this build could not read --
        // normal when a root uses an algorithm the crypto config has disabled -- so the bundle
        // still counts as found. Only a negative return means it was unusable.
        if (ret < 0) {
            tlsLogErr(Warn, _S"mbedtls_x509_crt_parse_file", ret);
            continue;
        }

        return _tlsChainCount(chain) - before;
    }

    for (int i = 0; i < (int)(sizeof(kCADirs) / sizeof(kCADirs[0])); i++) {
        if (fsStat(kCADirs[i], NULL) != FS_Directory)
            continue;

        int ret = mbedtls_x509_crt_parse_path(chain, strC(kCADirs[i]));
        if (ret < 0) {
            tlsLogErr(Warn, _S"mbedtls_x509_crt_parse_path", ret);
            continue;
        }

        return _tlsChainCount(chain) - before;
    }

    return -1;
}

#else

#pragma comment(lib, "crypt32.lib")

#include <cx/platform/win.h>
#include <wincrypt.h>

// Trust anchors are taken from two of the Windows system stores, and deliberately not from a third.
//
// ROOT ("Trusted Root Certification Authorities") is the obvious one. AuthRoot ("Third-Party Root
// Certification Authorities") is the other half of the same trust set: it holds the roots of the
// Microsoft Root Certificate Program that are not Microsoft's own. Everything in it is a
// self-signed root that Windows treats as an anchor, and which of the two stores a given root ends
// up in varies by Windows version and by how it got there -- so skipping AuthRoot would mean
// failing handshakes that Windows itself accepts, on some machines and not others.
//
// CA ("Intermediate Certification Authorities") is excluded on purpose. mbedTLS treats every
// certificate handed to it as a trust anchor, so importing intermediates would promote them to
// roots: trusted to sign anything, directly, with no path back to a real root. That is a strictly
// wider trust set than Windows applies, and the difference is not cosmetic.
static const LPCWSTR kWinRootStores[] = {
    L"ROOT",
    L"AuthRoot",
};

// The same names again, for log lines. Kept parallel rather than paired in a struct because the
// _S prefix is a cast, and clang-format cannot align a table of them.
static const strref kWinRootNames[] = {
    _S"ROOT",
    _S"AuthRoot",
};

// True if `chain` already holds a byte-identical certificate. ROOT and AuthRoot overlap on some
// systems, and a duplicated anchor is one more parent for mbedTLS to try and discard on every path
// search. Quadratic in the size of the store, but the length test resolves nearly every pair in a
// single integer compare.
static bool chainHasCert(_In_ const mbedtls_x509_crt* chain, _In_ const uint8* der, size_t len)
{
    for (const mbedtls_x509_crt* c = chain; c; c = c->next) {
        // A freshly initialized chain is one zeroed node, not an empty list.
        if (c->raw.len == 0)
            break;
        if (c->raw.len == len && memcmp(c->raw.p, der, len) == 0)
            return true;
    }
    return false;
}

// Hand one store entry to mbedTLS, dropping it if it is a duplicate, distrusted, unparseable, or
// expired.
static void addSystemCert(_Inout_ mbedtls_x509_crt* chain, _In_ PCCERT_CONTEXT ctx,
                          _In_opt_ HCERTSTORE disallowed)
{
    const uint8* der = (const uint8*)ctx->pbCertEncoded;
    size_t len       = ctx->cbCertEncoded;

    if (chainHasCert(chain, der, len))
        return;

    // CERT_FIND_EXISTING matches the whole encoded certificate, so this drops only the certificate
    // Windows was actually told to distrust, never one that merely resembles it.
    if (disallowed) {
        PCCERT_CONTEXT bad = CertFindCertificateInStore(disallowed,
                                                        X509_ASN_ENCODING,
                                                        0,
                                                        CERT_FIND_EXISTING,
                                                        ctx,
                                                        NULL);
        if (bad) {
            CertFreeCertificateContext(bad);
            return;
        }
    }

    // Parsed into a scratch chain first only so the dates can be checked. The store accumulates
    // roots that have aged out, and every one left in place is another parent mbedTLS tries and
    // rejects on every path search.
    mbedtls_x509_crt tmp;
    mbedtls_x509_crt_init(&tmp);

    // is_future(valid_to) reads as "has not expired yet". It also answers 1 when the clock cannot
    // be read at all, which keeps the certificate -- failing open here is right, because the
    // alternative is discarding the entire trust store on a machine with no working clock.
    if (mbedtls_x509_crt_parse_der(&tmp, der, len) == 0 &&
        mbedtls_x509_time_is_future(&tmp.valid_to)) {
        // Parsed a second time rather than spliced across from `tmp`: a DER walk per root is cheap
        // next to enumerating the store, and it leaves the shape of the chain entirely to mbedTLS
        // instead of duplicating its append logic here.
        int ret = mbedtls_x509_crt_parse_der(chain, der, len);
        if (ret != 0)
            tlsLogErr(Warn, _S"mbedtls_x509_crt_parse_der", ret);
    }

    mbedtls_x509_crt_free(&tmp);
}

_Use_decl_annotations_
int32 _tlsSystemCALoad(mbedtls_x509_crt* chain)
{
    int32 before = _tlsChainCount(chain);
    int32 opened = 0;

    // Windows keeps negative trust in a store of its own, and a root that has been distrusted is
    // very often still sitting in ROOT. Opening this is best-effort: without the blocklist the
    // anchors are still exactly the ones Windows shipped, which beats refusing to load a trust
    // store at all.
    HCERTSTORE disallowed = CertOpenSystemStoreW((HCRYPTPROV_LEGACY)NULL, L"Disallowed");

    for (int i = 0; i < (int)(sizeof(kWinRootStores) / sizeof(kWinRootStores[0])); i++) {
        HCERTSTORE store = CertOpenSystemStoreW((HCRYPTPROV_LEGACY)NULL, kWinRootStores[i]);
        if (!store) {
            winMapLastError();
            logFmt(Warn,
                   _SL("TLS: could not open the Windows ${string} certificate store"),
                   stvar(strref, kWinRootNames[i]));
            continue;
        }
        opened++;

        // CertEnumCertificatesInStore frees the context passed in as the cursor, so the loop owns
        // nothing at any point and there is no context to release on the way out.
        PCCERT_CONTEXT ctx = NULL;
        while ((ctx = CertEnumCertificatesInStore(store, ctx)) != NULL) {
            if (ctx->dwCertEncodingType & X509_ASN_ENCODING)
                addSystemCert(chain, ctx, disallowed);
        }

        CertCloseStore(store, 0);
    }

    if (disallowed)
        CertCloseStore(disallowed, 0);

    // Distinguish "there was no trust store to read" from "the trust store was empty".
    if (opened == 0)
        return -1;

    return _tlsChainCount(chain) - before;
}

#endif
