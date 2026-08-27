// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "tlsconfig.h"
// clang-format on
// ==================== Auto-generated section ends ======================

#include "tls_private.h"

#include <cx/time/clock.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL TlsLogChannel

// Ticket lifetime when the caller does not pick one. Long enough that a client reconnecting within
// a working session resumes, short enough that a stolen ticket stops being useful quickly.
#define TLS_DEFAULT_TICKET_LIFETIME timeS(3600)

// ---------------------------------------------------------------------------------------------
// mbedTLS callback thunks
//
// Both of these run inside a handshake, on whichever thread is driving it, with the flow's filter
// lock held. They translate mbedTLS's calling convention into cxtls's and do nothing else -- the
// application callback is where any real work belongs.
// ---------------------------------------------------------------------------------------------

static int verifyThunk(void* p, mbedtls_x509_crt* crt, int depth, uint32_t* flags)
{
    TlsConfig* self = (TlsConfig*)p;

    // Returning nonzero aborts the handshake outright, which is what a callback saying "stop"
    // means. Anything it wants to communicate short of that, it does by editing *flags.
    return self->st->verifyCb(crt, depth, (uint32*)flags, self->st->verifyCtx) ? 0 : -1;
}

static int sniThunk(void* p, mbedtls_ssl_context* ssl, const unsigned char* name, size_t len)
{
    TlsConfig* self = (TlsConfig*)p;

    string host = 0;
    strFromBytes(&host, name, (uint32)len);

    TlsCreds* creds = self->st->sniCb(host, self->st->sniCtx);
    strDestroy(&host);

    // NULL means "no opinion": leave the handshake on the config's own certificate rather than
    // failing, which is what makes the callback an override rather than a gate.
    if (!creds)
        return 0;

    int ret = mbedtls_ssl_set_hs_own_cert(ssl, &creds->st->cert, &creds->st->key);
    if (ret != 0)
        tlsLogErr(Warn, _S"mbedtls_ssl_set_hs_own_cert", ret);

    return ret;
}

// ---------------------------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------------------------

static _Ret_maybenull_ TlsConfig* configAlloc(bool server)
{
    if (!_tlsInit())
        return NULL;

    TlsConfig* self = objInstCreate(TlsConfig);

    self->st         = xaAlloc(sizeof(TlsConfigState), XA_Zero);
    self->st->server = server;

    mbedtls_ssl_config_init(&self->st->conf);
    mutexInit(&self->st->sessionLock);
    htInit(&self->st->sessions, string, ptr, 8);

    // A client that cannot verify the server is not doing TLS in any useful sense, so it starts
    // out requiring verification and the application has to opt out deliberately. A server has
    // nothing to verify unless it is doing mutual TLS, so it starts out not asking.
    self->st->authMode = server ? TLSAUTH_None : TLSAUTH_Required;

    int ret = mbedtls_ssl_config_defaults(&self->st->conf,
                                          server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);

    if (ret != 0) {
        tlsLogErr(Error, _S"mbedtls_ssl_config_defaults", ret);
        objRelease(&self);
        return NULL;
    }

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

_objfactory_check TlsConfig* TlsConfig_createClient()
{
    return configAlloc(false);
}

_objfactory_check TlsConfig* TlsConfig_createServer(_In_ TlsCreds* creds)
{
    // A server with no identity has nothing to present and would fail every handshake, so this is
    // caught here rather than at seal() -- there is no useful object to hand back.
    if (!creds)
        return NULL;

    TlsConfig* self = configAlloc(true);
    if (!self)
        return NULL;

    self->creds = objAcquire(creds);
    return self;
}

// ---------------------------------------------------------------------------------------------
// Setters
//
// Every one of these refuses once the config is sealed. mbedTLS reads the config from every live
// handshake without locking it, so a late setter would be a data race across every connection
// sharing it -- and a silent one, since the write itself always succeeds.
// ---------------------------------------------------------------------------------------------

static bool checkUnsealed(_In_ TlsConfig* self, _In_ strref what)
{
    if (!self->st->sealed)
        return true;

    logFmt(Error,
           _SL("TLS: ${string} on a sealed configuration is ignored; build a new "
               "TlsConfig and rotate to it instead"),
           stvar(strref, what));
    devAssertMsg(false, "TlsConfig modified after sealing");
    return false;
}

void TlsConfig_setCA(_In_ TlsConfig* self, _In_opt_ TlsCAStore* ca)
{
    if (!checkUnsealed(self, _S"tlsconfigSetCA"))
        return;

    objRelease(&self->ca);
    if (ca)
        self->ca = objAcquire(ca);
}

void TlsConfig_setCreds(_In_ TlsConfig* self, _In_opt_ TlsCreds* creds)
{
    if (!checkUnsealed(self, _S"tlsconfigSetCreds"))
        return;

    objRelease(&self->creds);
    if (creds)
        self->creds = objAcquire(creds);
}

void TlsConfig_setAuthMode(_In_ TlsConfig* self, TlsAuthMode mode)
{
    if (!checkUnsealed(self, _S"tlsconfigSetAuthMode"))
        return;

    self->st->authMode = mode;
}

void TlsConfig_setVersions(_In_ TlsConfig* self, TlsVersion min, TlsVersion max)
{
    if (!checkUnsealed(self, _S"tlsconfigSetVersions"))
        return;

    self->st->minVer = min;
    self->st->maxVer = max;
}

// Release the copied ALPN table. Kept separate because both setALPN() and destroy() need it, and
// the array is NULL-terminated for mbedTLS's benefit rather than length-counted.
static void alpnClear(_Inout_ TlsConfigState* st)
{
    for (int32 i = 0; i < st->alpnCount; i++) xaFree(st->alpn[i]);
    xaDestroy(&st->alpn);
    st->alpnCount = 0;
}

bool TlsConfig_setALPN(_In_ TlsConfig* self, _In_opt_ sa_string* protos)
{
    if (!checkUnsealed(self, _S"tlsconfigSetALPN"))
        return false;

    alpnClear(self->st);

    int32 n = protos ? saSize(*protos) : 0;
    if (n == 0)
        return true;

    // mbedTLS keeps the pointer and walks the array during every handshake, so the whole thing is
    // copied here -- the caller's sarray is free to go the moment this returns.
    self->st->alpn      = xaAlloc(sizeof(char*) * (size_t)(n + 1), XA_Zero);
    self->st->alpnCount = n;

    for (int32 i = 0; i < n; i++) {
        strref p    = protos->a[i];
        uint32 plen = strLen(p);

        self->st->alpn[i] = xaAlloc(plen + 1);
        strCopyOut(p, 0, (uint8*)self->st->alpn[i], plen + 1);
    }

    return true;
}

int32 TlsConfig_getALPN(_In_ TlsConfig* self, _Out_ sa_string* out)
{
    saInit(out, string, self->st->alpnCount);

    for (int32 i = 0; i < self->st->alpnCount; i++) {
        string s = 0;
        strFromBytes(&s, (const uint8*)self->st->alpn[i], (uint32)strlen(self->st->alpn[i]));
        saPush(out, string, s);
        strDestroy(&s);
    }

    return saSize(*out);
}

bool TlsConfig_setResumption(_In_ TlsConfig* self, bool enable, int64 lifetime)
{
    if (!checkUnsealed(self, _S"tlsconfigSetResumption"))
        return false;

    self->st->resume         = enable;
    self->st->resumeLifetime = lifetime > 0 ? lifetime : TLS_DEFAULT_TICKET_LIFETIME;
    return true;
}

void TlsConfig_setVerifyCallback(_In_ TlsConfig* self, TlsVerifyCB cb, _In_opt_ void* ctx)
{
    if (!checkUnsealed(self, _S"tlsconfigSetVerifyCallback"))
        return;

    self->st->verifyCb  = cb;
    self->st->verifyCtx = ctx;
}

void TlsConfig_setSNICallback(_In_ TlsConfig* self, TlsSNICB cb, _In_opt_ void* ctx)
{
    if (!checkUnsealed(self, _S"tlsconfigSetSNICallback"))
        return;

    if (cb && !self->st->server) {
        logStr(Warn, _SL("TLS: an SNI callback on a client configuration has no effect"));
        return;
    }

    self->st->sniCb  = cb;
    self->st->sniCtx = ctx;
}

// ---------------------------------------------------------------------------------------------
// Sealing
// ---------------------------------------------------------------------------------------------

static int authModeToMbed(TlsAuthMode mode)
{
    switch (mode) {
    case TLSAUTH_None:
        return MBEDTLS_SSL_VERIFY_NONE;
    case TLSAUTH_Optional:
        return MBEDTLS_SSL_VERIFY_OPTIONAL;
    default:
        return MBEDTLS_SSL_VERIFY_REQUIRED;
    }
}

static bool versionToMbed(TlsVersion v, mbedtls_ssl_protocol_version* out)
{
    switch (v) {
    case TLSVER_1_2:
        *out = MBEDTLS_SSL_VERSION_TLS1_2;
        return true;
    case TLSVER_1_3:
        *out = MBEDTLS_SSL_VERSION_TLS1_3;
        return true;
    default:
        return false;
    }
}

// Stand up the server side of resumption: a ticket context whose key mbedTLS rotates on its own,
// plus an in-memory cache for the TLS 1.2 session-ID path that predates tickets.
static bool sealServerResumption(_Inout_ TlsConfigState* st)
{
    mbedtls_ssl_ticket_init(&st->ticket);

    int ret = mbedtls_ssl_ticket_setup(&st->ticket,
                                       PSA_ALG_GCM,
                                       PSA_KEY_TYPE_AES,
                                       256,
                                       (uint32_t)timeToSeconds(st->resumeLifetime));
    if (ret != 0) {
        tlsLogErr(Error, _S"mbedtls_ssl_ticket_setup", ret);
        mbedtls_ssl_ticket_free(&st->ticket);
        return false;
    }
    st->ticketInit = true;

    mbedtls_ssl_conf_session_tickets_cb(&st->conf,
                                        mbedtls_ssl_ticket_write,
                                        mbedtls_ssl_ticket_parse,
                                        &st->ticket);

    mbedtls_ssl_cache_init(&st->cache);
    st->cacheInit = true;
    mbedtls_ssl_conf_session_cache(&st->conf,
                                   &st->cache,
                                   mbedtls_ssl_cache_get,
                                   mbedtls_ssl_cache_set);

    return true;
}

bool TlsConfig_seal(_In_ TlsConfig* self)
{
    TlsConfigState* st = self->st;

    if (st->sealed)
        return true;

    mbedtls_ssl_conf_authmode(&st->conf, authModeToMbed(st->authMode));

    mbedtls_ssl_protocol_version ver;
    if (versionToMbed(st->minVer, &ver))
        mbedtls_ssl_conf_min_tls_version(&st->conf, ver);
    if (versionToMbed(st->maxVer, &ver))
        mbedtls_ssl_conf_max_tls_version(&st->conf, ver);

    if (self->ca)
        mbedtls_ssl_conf_ca_chain(&st->conf, &self->ca->chain->crt, NULL);

    // Verifying with nothing to verify against fails every handshake, and does it inside the
    // handshake where the cause is far from obvious. Say so here instead.
    if (st->authMode != TLSAUTH_None && (!self->ca || _tlsChainCount(&self->ca->chain->crt) == 0)) {
        logStr(Error,
               _SL("TLS: certificate verification is enabled but the trust store is empty; "
                   "every handshake will fail"));
        return false;
    }

    if (self->creds) {
        int ret = mbedtls_ssl_conf_own_cert(&st->conf,
                                            &self->creds->st->cert,
                                            &self->creds->st->key);
        if (ret != 0) {
            tlsLogErr(Error, _S"mbedtls_ssl_conf_own_cert", ret);
            return false;
        }
    }

    if (st->alpn) {
        int ret = mbedtls_ssl_conf_alpn_protocols(&st->conf, (const char**)st->alpn);
        if (ret != 0) {
            tlsLogErr(Error, _S"mbedtls_ssl_conf_alpn_protocols", ret);
            return false;
        }
    }

    if (st->verifyCb)
        mbedtls_ssl_conf_verify(&st->conf, verifyThunk, self);

    if (st->sniCb)
        mbedtls_ssl_conf_sni(&st->conf, sniThunk, self);

    if (st->resume) {
        if (st->server) {
            if (!sealServerResumption(st))
                return false;
        } else {
            // The client half is two pieces: telling mbedTLS to accept tickets, and keeping the
            // resulting sessions -- which cxtls does itself, per hostname, in the cache below.
            mbedtls_ssl_conf_session_tickets(&st->conf, MBEDTLS_SSL_SESSION_TICKETS_ENABLED);
        }
    }

    st->sealed = true;
    return true;
}

_Use_decl_annotations_
bool _tlsconfigEnsureSealed(TlsConfig* self)
{
    return tlsconfigSeal(self);
}

_Use_decl_annotations_
mbedtls_ssl_config* _tlsconfigConf(TlsConfig* self)
{
    return self->st->sealed ? &self->st->conf : NULL;
}

_Use_decl_annotations_
bool _tlsconfigResumes(TlsConfig* self)
{
    return self->st->resume;
}

// ---------------------------------------------------------------------------------------------
// Client session cache
//
// A server resumes through its ticket context and cache, both of which mbedTLS drives itself. A
// client has to hold the session it was given and offer it back, and since one config backs every
// connection an application makes, the store belongs here rather than on any one session.
// ---------------------------------------------------------------------------------------------

static bool cacheUsable(_In_ TlsConfig* self, _In_opt_ strref host)
{
    return self->st->resume && !self->st->server && !strEmpty(host);
}

_Use_decl_annotations_
void _tlsconfigOfferSession(TlsConfig* self, strref host, mbedtls_ssl_context* ssl)
{
    if (!cacheUsable(self, host))
        return;

    withMutex (&self->st->sessionLock) {
        void* p = NULL;
        if (htFind(self->st->sessions, strref, host, ptr, &p) && p) {
            int ret = mbedtls_ssl_set_session(ssl, (const mbedtls_ssl_session*)p);
            if (ret != 0) {
                // A session mbedTLS will not take is one that has expired or was written by a
                // different build. Nothing is lost -- the handshake just runs in full -- but the
                // entry is now dead weight, so drop it.
                tlsLogErr(DevInfo, _S"mbedtls_ssl_set_session", ret);
                mbedtls_ssl_session_free((mbedtls_ssl_session*)p);
                xaFree(p);
                htRemove(&self->st->sessions, strref, host);
            }
        }
    }
}

_Use_decl_annotations_
void _tlsconfigSaveSession(TlsConfig* self, strref host, mbedtls_ssl_context* ssl)
{
    if (!cacheUsable(self, host))
        return;

    mbedtls_ssl_session* sess = xaAlloc(sizeof(mbedtls_ssl_session), XA_Zero);
    mbedtls_ssl_session_init(sess);

    int ret = mbedtls_ssl_get_session(ssl, sess);
    if (ret != 0) {
        // Normal rather than exceptional: there is nothing to save until the server has actually
        // issued a ticket, which under TLS 1.3 happens after the handshake rather than during it.
        mbedtls_ssl_session_free(sess);
        xaFree(sess);
        return;
    }

    withMutex (&self->st->sessionLock) {
        // Replace any session already held for this host -- the newest ticket is the one worth
        // keeping, and the old one cannot be reused after this anyway.
        void* old = NULL;
        if (htFind(self->st->sessions, strref, host, ptr, &old) && old) {
            mbedtls_ssl_session_free((mbedtls_ssl_session*)old);
            xaFree(old);
        } else if (htSize(self->st->sessions) >= TLS_SESSION_CACHE_MAX) {
            // At the cap, drop one arbitrary entry rather than growing. Which one hardly matters:
            // losing an entry costs one full handshake, and a client with more than this many live
            // endpoints is not the case resumption is for.
            string victimKey = 0;
            foreach (hashtable, hti, self->st->sessions) {
                void* victim = htiVal(ptr, hti);
                if (victim) {
                    mbedtls_ssl_session_free((mbedtls_ssl_session*)victim);
                    xaFree(victim);
                }
                strDup(&victimKey, htiKey(string, hti));
                break;
            }
            htRemove(&self->st->sessions, string, victimKey);
            strDestroy(&victimKey);
        }

        htInsert(&self->st->sessions, string, (string)host, ptr, sess);
    }
}

// ---------------------------------------------------------------------------------------------

void TlsConfig_destroy(_In_ TlsConfig* self)
{
    TlsConfigState* st = self->st;
    if (!st)
        return;

    // Nothing may still be reading the config by now: every session holds a reference to this
    // object, so the last release is by definition after the last handshake finished.
    foreach (hashtable, hti, st->sessions) {
        void* sess = htiVal(ptr, hti);
        if (sess) {
            mbedtls_ssl_session_free((mbedtls_ssl_session*)sess);
            xaFree(sess);
        }
    }
    htDestroy(&st->sessions);
    mutexDestroy(&st->sessionLock);

    if (st->ticketInit)
        mbedtls_ssl_ticket_free(&st->ticket);
    if (st->cacheInit)
        mbedtls_ssl_cache_free(&st->cache);

    alpnClear(st);
    mbedtls_ssl_config_free(&st->conf);
    xaDestroy(&self->st);

    // Autogen begins -----
    objRelease(&self->ca);
    objRelease(&self->creds);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "tlsconfig.auto.inc"
// clang-format on
// Autogen ends -------
