// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "tlsquic.h"
// clang-format on
// ==================== Auto-generated section ends ======================

#include "tls13_private.h"
#include "tls_private.h"

#undef LOG_CHANNEL
#define LOG_CHANNEL TlsLogChannel

static _Ret_maybenull_ TlsQuic* quicAlloc(_In_opt_ TlsConfig* config, bool server)
{
    if (!config || !_tlsInit())
        return NULL;

    // A client handshake off a server configuration would present the wrong identity and verify
    // nothing; catching it here rather than mid-handshake is the whole point of two factories.
    if (config->st->server != server)
        return NULL;

    if (!_tlsconfigEnsureSealed(config))
        return NULL;

    TlsQuic* self = objInstCreate(TlsQuic);
    self->st      = xaAlloc(sizeof(TlsQuicState), XA_Zero);

    if (!_tls13HsInit(&self->st->hs, server, config) || !objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    self->config = objAcquire(config);
    return self;
}

_objfactory_check TlsQuic* TlsQuic_createClient(_In_ TlsConfig* config, _In_opt_ strref hostname)
{
    TlsQuic* self = quicAlloc(config, false);
    if (!self)
        return NULL;

    _tls13HsSetHostname(&self->st->hs, hostname);
    return self;
}

_objfactory_check TlsQuic* TlsQuic_createServer(_In_ TlsConfig* config)
{
    return quicAlloc(config, true);
}

void TlsQuic_setHandlers(_In_ TlsQuic* self, const TlsQuicHandlers* handlers, _In_opt_ void* ctx)
{
    _tls13HsSetHandlers(&self->st->hs, handlers, ctx);
}

bool TlsQuic_setTransportParams(_In_ TlsQuic* self, _In_ const uint8* data, size_t len)
{
    return _tls13HsSetTransportParams(&self->st->hs, data, len);
}

bool TlsQuic_setSuites(_In_ TlsQuic* self, _In_ const uint16* suites, size_t count)
{
    return _tls13HsSetSuites(&self->st->hs, suites, count);
}

bool TlsQuic_setGroups(_In_ TlsQuic* self, _In_ const uint16* groups, size_t count)
{
    return _tls13HsSetGroups(&self->st->hs, groups, count);
}

bool TlsQuic_start(_In_ TlsQuic* self)
{
    return _tls13HsStart(&self->st->hs);
}

bool TlsQuic_recv(_In_ TlsQuic* self, TlsQuicLevel level, _In_ const uint8* data, size_t len)
{
    return _tls13HsRecv(&self->st->hs, level, data, len);
}

bool TlsQuic_complete(_In_ TlsQuic* self)
{
    return self->st->hs.state == TLS13_ST_DONE;
}

uint8 TlsQuic_alert(_In_ TlsQuic* self)
{
    return self->st->hs.alert;
}

bool TlsQuic_getALPN(_In_ TlsQuic* self, _Out_ string* out)
{
    strDup(out, self->st->hs.alpnSel);
    return !strEmpty(*out);
}

// A distinguished name as text, for the snapshot. mbedTLS writes into a caller buffer, and a name
// longer than this is truncated rather than dropped.
static void dnGet(_Inout_ strhandle out, _In_ const mbedtls_x509_name* name)
{
    char buf[512];
    int n = mbedtls_x509_dn_gets(buf, sizeof(buf), name);
    if (n > 0)
        strFromBytes(out, (const uint8*)buf, (uint32)n);
}

bool TlsQuic_getInfo(_In_ TlsQuic* self, _Out_ TlsInfo* out)
{
    Tls13Hs* hs = &self->st->hs;

    memset(out, 0, sizeof(*out));
    if (hs->state != TLS13_ST_DONE)
        return false;

    STR_CONST(ver13, "TLSv1.3");

    out->secured      = true;
    out->peerVerified = hs->peerVerified;
    out->verifyFlags  = hs->verifyFlags;

    strDup(&out->version, ver13);
    strDup(&out->ciphersuite, hs->suite ? _tls13SuiteName(hs->suite->id) : NULL);
    strDup(&out->alpn, hs->alpnSel);

    if (hs->peerPresent) {
        dnGet(&out->peerSubject, &hs->peerCert.subject);
        dnGet(&out->peerIssuer, &hs->peerCert.issuer);
    }

    return true;
}

void TlsQuic_destroy(_In_ TlsQuic* self)
{
    if (self->st) {
        _tls13HsDestroy(&self->st->hs);
        xaDestroy(&self->st);
    }

    // Autogen begins -----
    objRelease(&self->config);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "tlsquic.auto.inc"
// clang-format on
// Autogen ends -------
