// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "tlscert.h"
// clang-format on
// ==================== Auto-generated section ends ======================

#include "tls_private.h"

#include <cx/fs/file.h>
#include <cx/fs/fs.h>
#include <cx/fs/path.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL TlsLogChannel

// Certificates are not large, but a truncated or runaway file should not be turned into an
// allocation. Any real chain or bundle is far below this.
#define TLS_MAX_CERT_FILE (8 * 1024 * 1024)

// ---------------------------------------------------------------------------------------------
// Shared parsing helpers
// ---------------------------------------------------------------------------------------------

// Read a whole file into a string. Certificates are parsed from memory rather than through
// mbedtls_x509_crt_parse_file() so that paths go through cx's filesystem layer, which handles
// UTF-8 path names on Windows -- mbedTLS reaches for fopen() and would mangle them.
static bool readWholeFile(_Inout_ strhandle out, _In_ strref path)
{
    FSStat st;
    if (fsStat(path, &st) != FS_File)
        return false;

    if (st.size == 0 || st.size > TLS_MAX_CERT_FILE)
        return false;

    FSFile* f = fsOpen(path, FS_Read);
    if (!f)
        return false;

    // strBuffer() grows the string to hold the whole file and hands back a writable pointer; cx
    // keeps a NUL behind the length, so strC() on the result is safe for the PEM path below.
    uint8* buf   = strBuffer(out, (uint32)st.size);
    size_t nread = 0;
    bool ok      = fsRead(f, buf, (size_t)st.size, &nread) && nread == (size_t)st.size;

    fsClose(f);

    if (!ok)
        strDestroy(out);

    return ok;
}

// True if the buffer looks like PEM. mbedTLS makes the same test internally, and the answer
// decides the length convention: PEM wants the terminating NUL counted, DER wants it left out.
static bool looksLikePEM(_In_ strref data)
{
    return strFind(data, 0, _S"-----BEGIN") >= 0;
}

// Parse whatever is in `data` (PEM or DER) onto the end of `chain`. mbedTLS parses permissively:
// a positive return is the number of certificates in a bundle it could not read, which is normal
// for a system trust store carrying an algorithm this build has disabled, and not a failure as
// long as something got through.
static bool parseCerts(_Inout_ mbedtls_x509_crt* chain, _In_ strref data, _In_ strref what)
{
    uint32 len = strLen(data);
    bool pem   = looksLikePEM(data);
    int ret    = mbedtls_x509_crt_parse(chain, (const uint8*)strC(data), pem ? len + 1 : len);

    if (ret < 0) {
        tlsLogErr(Warn, _S"mbedtls_x509_crt_parse", ret);
        return false;
    }

    if (ret > 0)
        logFmt(Warn,
               _SL("TLS: skipped ${int} unparseable certificate(s) in ${string}"),
               stvar(int32, ret),
               stvar(strref, what));

    return true;
}

_Use_decl_annotations_
int32 _tlsChainCount(const mbedtls_x509_crt* chain)
{
    int32 n = 0;
    for (const mbedtls_x509_crt* c = chain; c; c = c->next) {
        // A freshly initialized chain is one zeroed node, not an empty list.
        if (c->raw.len == 0)
            break;
        n++;
    }
    return n;
}

// ---------------------------------------------------------------------------------------------
// TlsCAStore
// ---------------------------------------------------------------------------------------------

_objfactory_check TlsCAStore* TlsCAStore_create()
{
    if (!_tlsInit())
        return NULL;

    TlsCAStore* self;
    self = objInstCreate(TlsCAStore);

    self->chain = xaAllocStruct(TlsCertChain, XA_Zero);
    mbedtls_x509_crt_init(&self->chain->crt);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

bool TlsCAStore_addPEM(_In_ TlsCAStore* self, _In_opt_ strref pem)
{
    if (strEmpty(pem))
        return false;

    return parseCerts(&self->chain->crt, pem, _S"PEM data");
}

bool TlsCAStore_addDER(_In_ TlsCAStore* self, _In_ const uint8* der, size_t len)
{
    if (!der || len == 0)
        return false;

    // DER is exact-length by definition, so it does not go through parseCerts(), whose whole job
    // is deciding whether to count a terminating NUL.
    int ret = mbedtls_x509_crt_parse_der(&self->chain->crt, der, len);
    if (ret != 0) {
        tlsLogErr(Warn, _S"mbedtls_x509_crt_parse_der", ret);
        return false;
    }

    return true;
}

bool TlsCAStore_addFile(_In_ TlsCAStore* self, _In_opt_ strref path)
{
    string data = 0;
    if (!readWholeFile(&data, path)) {
        logFmt(Warn, _SL("TLS: could not read certificate file ${string}"), stvar(strref, path));
        return false;
    }

    bool ret = parseCerts(&self->chain->crt, data, path);
    strDestroy(&data);
    return ret;
}

bool TlsCAStore_addDir(_In_ TlsCAStore* self, _In_opt_ strref path)
{
    FSSearchIter iter;
    int32 before = _tlsChainCount(&self->chain->crt);

    // A hashed CA directory is full of symlinks and files that are not certificates at all, so a
    // file that will not parse is skipped rather than failing the call. Adding nothing at all is
    // the only failure.
    if (fsSearchInit(&iter, path, NULL, false)) {
        do {
            if (iter.type != FS_File)
                continue;

            string full = 0;
            pathJoin(&full, path, iter.name);

            string data = 0;
            if (readWholeFile(&data, full))
                mbedtls_x509_crt_parse(&self->chain->crt,
                                       (const uint8*)strC(data),
                                       looksLikePEM(data) ? strLen(data) + 1 : strLen(data));

            strDestroy(&data);
            strDestroy(&full);
        } while (fsSearchNext(&iter));
    }
    fsSearchFinish(&iter);

    return _tlsChainCount(&self->chain->crt) > before;
}

bool TlsCAStore_addSystem(_In_ TlsCAStore* self)
{
    // The platform trust set is fixed for the life of the process, so a second pass can only
    // duplicate what the first one added. Windows would catch that per certificate anyway; the
    // Unix side parses a bundle wholesale and has no per-certificate hook to catch it with, so
    // the guard lives here where both platforms get it.
    if (self->sysLoaded)
        return false;

    int32 added = _tlsSystemCALoad(&self->chain->crt);

    if (added < 0) {
        logStr(Warn, _SL("TLS: no system certificate store could be located"));
        return false;
    }

    self->sysLoaded = true;
    return added > 0;
}

int32 TlsCAStore_count(_In_ TlsCAStore* self)
{
    return _tlsChainCount(&self->chain->crt);
}

void TlsCAStore_destroy(_In_ TlsCAStore* self)
{
    if (self->chain) {
        mbedtls_x509_crt_free(&self->chain->crt);
        xaDestroy(&self->chain);
    }
}

// ---------------------------------------------------------------------------------------------
// TlsCreds
// ---------------------------------------------------------------------------------------------

// Shared tail of both TlsCreds factories: parse the two halves, then prove they belong together.
// mbedtls_ssl_conf_own_cert() explicitly does not check that, and a mismatch would otherwise
// surface as an opaque handshake failure on the first connection rather than at load time.
static bool credsLoad(_Inout_ TlsCredsState* st, _In_ strref certData, _In_ strref keyData,
                      _In_opt_ strref password)
{
    if (!parseCerts(&st->cert, certData, _S"certificate"))
        return false;

    const uint8* pwd = strEmpty(password) ? NULL : (const uint8*)strC(password);
    size_t pwdlen    = strEmpty(password) ? 0 : strLen(password);

    // Same PEM/DER length convention as the certificate side.
    uint32 klen = strLen(keyData);
    int ret     = mbedtls_pk_parse_key(&st->key,
                                   (const uint8*)strC(keyData),
                                   looksLikePEM(keyData) ? klen + 1 : klen,
                                   pwd,
                                   pwdlen);
    if (ret != 0) {
        tlsLogErr(Warn, _S"mbedtls_pk_parse_key", ret);
        return false;
    }

    ret = mbedtls_pk_check_pair(&st->cert.pk, &st->key);
    if (ret != 0) {
        tlsLogErr(Error, _S"mbedtls_pk_check_pair", ret);
        return false;
    }

    return true;
}

// Build the object around an already-parsed state, or tear it down and fail.
static TlsCreds* credsFinish(_Inout_ TlsCreds* self, bool ok)
{
    if (!ok) {
        objRelease(&self);
        return NULL;
    }

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

// Allocate the object and its mbedTLS state, ready for credsLoad().
static _Ret_maybenull_ TlsCreds* credsAlloc(void)
{
    if (!_tlsInit())
        return NULL;

    TlsCreds* self = objInstCreate(TlsCreds);

    self->st = xaAlloc(sizeof(TlsCredsState), XA_Zero);
    mbedtls_x509_crt_init(&self->st->cert);
    mbedtls_pk_init(&self->st->key);

    return self;
}

_objfactory_check TlsCreds*
TlsCreds_createPEM(_In_opt_ strref certPem, _In_opt_ strref keyPem, _In_opt_ strref password)
{
    TlsCreds* self = credsAlloc();
    if (!self)
        return NULL;

    return credsFinish(self, credsLoad(self->st, certPem, keyPem, password));
}

_objfactory_check TlsCreds*
TlsCreds_createFiles(_In_opt_ strref certPath, _In_opt_ strref keyPath, _In_opt_ strref password)
{
    TlsCreds* self = credsAlloc();
    if (!self)
        return NULL;

    string certData = 0, keyData = 0;
    bool ok = readWholeFile(&certData, certPath) && readWholeFile(&keyData, keyPath);

    if (!ok)
        logFmt(Warn,
               _SL("TLS: could not read credentials from ${string} / ${string}"),
               stvar(strref, certPath),
               stvar(strref, keyPath));
    else
        ok = credsLoad(self->st, certData, keyData, password);

    strDestroy(&certData);
    strDestroy(&keyData);

    return credsFinish(self, ok);
}

bool TlsCreds_getSubject(_In_ TlsCreds* self, _Out_ string* out)
{
    strDestroy(out);

    if (!self->st || self->st->cert.raw.len == 0)
        return false;

    char buf[512];
    int n = mbedtls_x509_dn_gets(buf, sizeof(buf), &self->st->cert.subject);
    if (n < 0) {
        tlsLogErr(Warn, _S"mbedtls_x509_dn_gets", n);
        return false;
    }

    strFromBytes(out, buf, (uint32)n);
    return true;
}

void TlsCreds_destroy(_In_ TlsCreds* self)
{
    if (self->st) {
        mbedtls_pk_free(&self->st->key);
        mbedtls_x509_crt_free(&self->st->cert);
        xaDestroy(&self->st);
    }
}

// Autogen begins -----
// clang-format off
#include "tlscert.auto.inc"
// clang-format on
// Autogen ends -------
