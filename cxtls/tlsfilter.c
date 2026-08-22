// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "tlsfilter.h"
// clang-format on
// ==================== Auto-generated section ends ======================

#include "tls_private.h"

#include <cxtls.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL TlsLogChannel

// How much plaintext one encode pass hands to mbedTLS at a time, and how much decoded plaintext one
// mbedtls_ssl_read() may return. Both are scratch on the stack, so this is a stack-pressure figure
// rather than a throughput one: a pass loops until the input is drained either way, and the only
// cost of a smaller value is a few more records. TLS caps a record's plaintext at 16384 regardless.
#define TLS_SCRATCH 4096

// ---------------------------------------------------------------------------------------------
// The BIO bridge
//
// mbedTLS wants a socket. What it gets instead is the pair of boundary rings the filter chain
// already owns: writes land in the stage's encOut, on its way to the wire, and reads come out of
// whatever ring the current decode pass was handed.
//
// The recv side is where the handshake gating lives. `sess->in` is set only for the length of a
// decode() call, so a handshake driven from the encode side finds nothing to read, answers
// WANT_READ, and stops -- after having pushed its flight into encOut, which is exactly the
// behavior the priming pass exists to produce.
// ---------------------------------------------------------------------------------------------

static int bioSend(void* ctx, const unsigned char* buf, size_t len)
{
    TlsSession* sess = (TlsSession*)ctx;

    // A ring never refuses, so this never reports WANT_WRITE -- which is what keeps the retry rule
    // on mbedtls_ssl_write() from ever being exercised by a full transmit buffer. What bounds the
    // ring is the encode side feeding it TLS_SCRATCH at a time, and the driver draining it to the
    // socket at the end of every pass.
    bufringWrite(sess->out, buf, len);
    return (int)len;
}

static int bioRecv(void* ctx, unsigned char* buf, size_t len)
{
    TlsSession* sess = (TlsSession*)ctx;

    if (!sess->in)
        return MBEDTLS_ERR_SSL_WANT_READ;

    size_t n = bufringRead(sess->in, buf, len);

    // Returning 0 would tell mbedTLS the transport hit EOF, which is a very different thing from a
    // record that has not fully arrived yet. WANT_READ leaves the partial record buffered where it
    // is, to be resumed on the next wire read.
    return n > 0 ? (int)n : MBEDTLS_ERR_SSL_WANT_READ;
}

// ---------------------------------------------------------------------------------------------
// Session state
// ---------------------------------------------------------------------------------------------

// Take the snapshot nettlsFlowInfo() hands out. Done once, at the moment the handshake completes,
// so the answer stays available and cheap for the life of the flow and nothing mbedTLS owns has to
// escape the filter lock.
static void captureInfo(_Inout_ TlsStreamFilter* self)
{
    mbedtls_ssl_context* ssl = &self->sess->ssl;
    TlsInfoState* st         = xaAlloc(sizeof(TlsInfoState), XA_Zero);

    st->info.secured = true;

    // mbedTLS reports "nothing was checked" in two different shapes, and neither is a failure:
    // 0xFFFFFFFF when no result is available at all, and MBEDTLS_X509_BADCERT_SKIP_VERIFY when
    // verification was deliberately skipped -- which is what a *resumed* session reports, since
    // resumption carries the identity forward from the original handshake instead of sending a
    // certificate again, and what TLSAUTH_None reports for the opposite reason.
    //
    // Both fold to peerVerified = false with verifyFlags = 0, so that `verifyFlags != 0` always
    // means something actually went wrong. Anyone who needs to tell "checked and clean" from
    // "never checked" reads peerVerified.
    uint32 flags = mbedtls_ssl_get_verify_result(ssl);
    bool checked = flags != 0xFFFFFFFFu && !(flags & MBEDTLS_X509_BADCERT_SKIP_VERIFY);

    st->info.peerVerified = checked;
    st->info.verifyFlags  = checked ? flags : 0;

    const char* s = mbedtls_ssl_get_version(ssl);
    if (s)
        strFromBytes(&st->info.version, s, (uint32)strlen(s));

    s = mbedtls_ssl_get_ciphersuite(ssl);
    if (s)
        strFromBytes(&st->info.ciphersuite, s, (uint32)strlen(s));

    s = mbedtls_ssl_get_alpn_protocol(ssl);
    if (s)
        strFromBytes(&st->info.alpn, s, (uint32)strlen(s));

    const mbedtls_x509_crt* peer = mbedtls_ssl_get_peer_cert(ssl);
    if (peer) {
        char buf[512];
        int n = mbedtls_x509_dn_gets(buf, sizeof(buf), &peer->subject);
        if (n > 0)
            strFromBytes(&st->info.peerSubject, buf, (uint32)n);

        n = mbedtls_x509_dn_gets(buf, sizeof(buf), &peer->issuer);
        if (n > 0)
            strFromBytes(&st->info.peerIssuer, buf, (uint32)n);
    }

    self->info = st;
}

// Mark the session dead. Every subsequent pass reports the failure, which the chain driver turns
// into netflow_close(NCR_Error) -- there is no way to resynchronize a broken record stream, and
// continuing without encryption is not an option a TLS filter gets to take.
static void sessionFail(_Inout_ TlsStreamFilter* self, _In_ strref op, int err)
{
    if (!self->failed) {
        self->failed = true;
        tlsLogErr(Warn, op, err);
    }
}

// Push the handshake one step further. Called from both directions: from decode() with records to
// feed it, and from encode() with nothing, which is how the side that opens the negotiation gets
// its first flight out.
static bool driveHandshake(_Inout_ TlsStreamFilter* self)
{
    int ret = mbedtls_ssl_handshake(&self->sess->ssl);

    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        return true;   // mid-flight; whatever it produced is already in encOut

    if (ret != 0) {
        sessionFail(self, _S"mbedtls_ssl_handshake", ret);
        return false;
    }

    self->secured = true;
    captureInfo(self);

    // TLS 1.2 hands out its session with the handshake. TLS 1.3 sends a ticket afterwards instead,
    // which arrives through decode() -- so this attempt is expected to come up empty there, and
    // _tlsconfigSaveSession() treats that as ordinary rather than as an error.
    _tlsconfigSaveSession(self->config, self->hostname, &self->sess->ssl);

    // The framework never infers this. Raising it explicitly is what puts NET_FilterNotify ahead of
    // the NET_DataReceived produced by the very pass that finished the handshake.
    netstreamfilterNotify(self, NFN_Secured);
    return true;
}

// ---------------------------------------------------------------------------------------------
// TlsStreamFilter
// ---------------------------------------------------------------------------------------------

_objfactory_guaranteed TlsStreamFilter*
TlsStreamFilter_create(_In_ TlsConfig* config, _In_opt_ strref hostname, bool server)
{
    TlsStreamFilter* self;
    self = objInstCreate(TlsStreamFilter);

    self->server = server;
    strDup(&self->hostname, hostname);
    if (config)
        self->config = objAcquire(config);

    self->sess      = xaAlloc(sizeof(TlsSession), XA_Zero);
    self->sess->out = &self->encOut;
    mbedtls_ssl_init(&self->sess->ssl);

    objInstInit(self);

    // Everything below can fail, and a failure here must not produce a stage that quietly passes
    // bytes through: this factory is deliberately not [canfail], because returning NULL would leave
    // the flow unfiltered and put plaintext on a wire the application believes is encrypted.
    // Marking the session failed instead makes the first driver pass tear the flow down.
    if (!config) {
        sessionFail(self, _S"tlsstreamfilterCreate (no configuration)", 0);
        return self;
    }

    if (!_tlsconfigEnsureSealed(config)) {
        sessionFail(self, _S"tlsstreamfilterCreate (configuration could not be sealed)", 0);
        return self;
    }

    int ret = mbedtls_ssl_setup(&self->sess->ssl, _tlsconfigConf(config));
    if (ret != 0) {
        sessionFail(self, _S"mbedtls_ssl_setup", ret);
        return self;
    }
    self->sess->setup = true;

    mbedtls_ssl_set_bio(&self->sess->ssl, self->sess, bioSend, bioRecv, NULL);

    if (!server) {
        // Always called on a client, with NULL when there is no name to check. Skipping the call
        // entirely is a distinct thing to mbedTLS 4.x, which refuses to verify at all without it
        // (MBEDTLS_ERR_SSL_CERTIFICATE_VERIFICATION_WITHOUT_HOSTNAME) rather than silently
        // accepting any certificate the trust store happens to chain to.
        ret = mbedtls_ssl_set_hostname(&self->sess->ssl,
                                       strEmpty(self->hostname) ? NULL : strC(self->hostname));
        if (ret != 0) {
            sessionFail(self, _S"mbedtls_ssl_set_hostname", ret);
            return self;
        }

        if (strEmpty(self->hostname))
            logStr(Warn,
                   _SL("TLS: connecting with no expected hostname; the server's certificate "
                       "will be checked against the trust store but not against its name"));

        _tlsconfigOfferSession(config, self->hostname, &self->sess->ssl);
    }

    return self;
}

void TlsStreamFilter_shutdown(_In_ TlsStreamFilter* self)
{
    if (self->closing)
        return;

    self->closing = true;

    // A session that never came up has no close_notify to send, and one that already failed has a
    // record stream the peer could not read anyway.
    if (self->failed || !self->secured)
        return;

    // This writes the alert through bioSend into encOut. The driver runs one more encode pass right
    // after this returns, which is what carries it to the wire while the socket is still open.
    int ret = mbedtls_ssl_close_notify(&self->sess->ssl);
    if (ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        tlsLogErr(DevInfo, _S"mbedtls_ssl_close_notify", ret);
}

intptr TlsStreamFilter_encode(_In_ TlsStreamFilter* self, BufRing* src)
{
    if (self->failed)
        return -1;

    size_t before = self->encOut.total;

    // Past shutdown() the only thing owed to the wire is the close record, and it is already in
    // encOut -- the driver drains it regardless of what is returned here. Encrypting anything more
    // would be writing after the peer has been told the stream ended.
    if (self->closing)
        return 0;

    if (!self->secured) {
        // Mid-handshake, application data is left untouched in `src`. Declining to consume it *is*
        // the gate that holds plaintext back; it resumes on its own once the channel is up.
        if (!driveHandshake(self))
            return -1;

        if (!self->secured)
            return (intptr)(self->encOut.total - before);
    }

    uint8 tmp[TLS_SCRATCH];
    for (;;) {
        // An interrupted write has to be retried with the same arguments, so its length is
        // remembered and re-offered from the head of the ring -- which still holds those exact
        // bytes, since nothing is consumed until the write reports how much it took.
        size_t want = self->pendingWrite ? self->pendingWrite : src->total;
        if (want > sizeof(tmp))
            want = sizeof(tmp);
        if (want == 0)
            break;

        size_t n = bufringPeek(src, tmp, 0, want);
        if (n < want)
            break;   // cannot happen: `want` came from src->total or a prior peek of the same ring

        self->pendingWrite = (uint32)want;

        int ret = mbedtls_ssl_write(&self->sess->ssl, tmp, want);

        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
            break;   // retry next pass with the same `want`, which pendingWrite now holds

        if (ret < 0) {
            sessionFail(self, _S"mbedtls_ssl_write", ret);
            return -1;
        }

        if (ret == 0)
            break;   // documented not to happen for a nonzero length; looping on it would not end

        // A short write is normal -- the record layer caps a fragment -- so only what was actually
        // taken is consumed, and the rest goes round again.
        bufringSkip(src, (size_t)ret);
        self->pendingWrite = 0;
    }

    return (intptr)(self->encOut.total - before);
}

intptr TlsStreamFilter_decode(_In_ TlsStreamFilter* self, BufRing* src)
{
    if (self->failed)
        return -1;

    size_t before = self->decOut.total;
    bool ok       = true;

    // Publish the input ring for the length of this call, and only this call. See the BIO bridge
    // note above: a NULL `in` outside a decode pass is what makes the encode side stall correctly.
    self->sess->in = src;

    if (!self->secured)
        ok = driveHandshake(self);

    uint8 tmp[TLS_SCRATCH];
    while (ok && self->secured && !self->peerClosed) {
        int ret = mbedtls_ssl_read(&self->sess->ssl, tmp, sizeof(tmp));

        if (ret > 0) {
            bufringWrite(&self->decOut, tmp, (size_t)ret);
            continue;
        }

        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            // WANT_READ does not reliably mean "everything already read has been processed",
            // whatever mbedtls_ssl_check_pending()'s documentation says about it. One record can
            // carry several handshake messages, and mbedtls_ssl_read() surfaces them one call at a
            // time: the state transition it performs on the way out reports WANT_READ without
            // going anywhere near the transport. A TLS 1.3 server that packs two NewSessionTickets
            // into a single record -- Cloudflare does, on every connection -- lands here with the
            // second ticket still buffered.
            //
            // Stopping the pass at that point is what makes it a bug rather than a hiccup. The
            // pass consumed nothing from `src` and produced nothing, so the chain driver's
            // fixpoint loop sees no progress and stops, and nothing re-enters decode() until more
            // bytes arrive from the wire. If the peer's next act is to close -- exactly what a
            // `Connection: close` response does -- those bytes never come, and an HTTP response
            // sitting fully received in the socket's ring is thrown away with the flow.
            if (mbedtls_ssl_check_pending(&self->sess->ssl))
                continue;

            break;   // a partial record; it stays in `src` and resumes on the next wire read
        }

        if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            // An orderly close, not a failure. Nothing more will decode, but whatever this pass
            // already produced is still delivered, and the transport's own close carries the flow's
            // NET_FlowClosed as usual.
            self->peerClosed = true;
            break;
        }

        if (ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
            // A TLS 1.3 server issues its ticket after the handshake, so this is where a client
            // learns it can resume next time.
            _tlsconfigSaveSession(self->config, self->hostname, &self->sess->ssl);
            continue;
        }

        if (ret == 0) {
            // The transport reported EOF without a close_notify. The BIO above never says that, so
            // reaching here means mbedTLS decided the stream ended some other way; either way the
            // context cannot be used again.
            logStr(DevWarn, _SL("TLS: peer closed the connection without close_notify"));
            self->peerClosed = true;
            break;
        }

        sessionFail(self, _S"mbedtls_ssl_read", ret);
        ok = false;
    }

    self->sess->in = NULL;

    if (!ok)
        return -1;

    return (intptr)(self->decOut.total - before);
}

void TlsStreamFilter_destroy(_In_ TlsStreamFilter* self)
{
    if (self->sess) {
        mbedtls_ssl_free(&self->sess->ssl);
        xaDestroy(&self->sess);
    }

    if (self->info) {
        nettlsInfoDestroy(&self->info->info);
        xaDestroy(&self->info);
    }

    // Autogen begins -----
    objRelease(&self->config);
    strDestroy(&self->hostname);
    // Autogen ends -------
}

// ---------------------------------------------------------------------------------------------
// TlsClientFilter
// ---------------------------------------------------------------------------------------------

_objfactory_check TlsClientFilter*
TlsClientFilter_create(_In_ TlsConfig* config, _In_opt_ strref hostname)
{
    // A server configuration on a client socket would present the wrong endpoint role to mbedTLS
    // and fail every handshake. Caught here, where the caller can still see it, rather than on the
    // first connection.
    if (!config || config->st->server) {
        logStr(Error, _SL("TLS: tlsclientfilterCreate needs a client configuration"));
        return NULL;
    }

    TlsClientFilter* self;
    self = objInstCreate(TlsClientFilter);

    self->config = objAcquire(config);
    strDup(&self->hostname, hostname);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

NetFlowFilter* TlsClientFilter_createFlow(_In_ TlsClientFilter* self, NetSocketType type)
{
    unused_noeval(type);   // canFilter() already refused anything but a stream socket

    return NetFlowFilter(tlsstreamfilterCreate(self->config, self->hostname, false));
}

bool TlsClientFilter_canFilter(_In_ TlsClientFilter* self, NetSocketType type)
{
    unused_noeval(self);

    // DTLS over a datagram socket is a separate filter that does not exist yet. Refusing here is
    // what stops a datagram socket from being attached to and then running unencrypted.
    return type == NST_Stream;
}

void TlsClientFilter_destroy(_In_ TlsClientFilter* self)
{
    // Autogen begins -----
    objRelease(&self->config);
    strDestroy(&self->hostname);
    // Autogen ends -------
}

// ---------------------------------------------------------------------------------------------
// TlsServerFilter
// ---------------------------------------------------------------------------------------------

_objfactory_check TlsServerFilter* TlsServerFilter_create(_In_ TlsConfig* config)
{
    if (!config || !config->st->server) {
        logStr(Error, _SL("TLS: tlsserverfilterCreate needs a server configuration"));
        return NULL;
    }

    TlsServerFilter* self;
    self = objInstCreate(TlsServerFilter);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    // After objInstInit, since the mutex guarding it is what init() sets up. A configuration that
    // will not seal leaves the slot empty, and a filter with no configuration can only fail every
    // connection closed -- so report it here, where the caller still has somewhere to go.
    tlsserverfilterSetConfig(self, config);

    if (!self->config) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

_objinit_guaranteed bool TlsServerFilter_init(_In_ TlsServerFilter* self)
{
    // Autogen begins -----
    mutexInit(&self->lock);
    return true;
    // Autogen ends -------
}

void TlsServerFilter_setConfig(_In_ TlsServerFilter* self, _In_ TlsConfig* config)
{
    if (!config || !config->st->server) {
        logStr(Error,
               _SL("TLS: tlsserverfilterSetConfig needs a server configuration; the "
                   "existing one is kept"));
        return;
    }

    // Sealing here rather than at the first connection means a rotation to a broken configuration
    // is reported at the moment it is installed, while the caller still has the old one.
    if (!_tlsconfigEnsureSealed(config)) {
        logStr(Error, _SL("TLS: new configuration could not be sealed; the existing one is kept"));
        return;
    }

    TlsConfig* old = NULL;

    withMutex (&self->lock) {
        old          = self->config;
        self->config = objAcquire(config);
    }

    // Released outside the lock: this may be the last reference, and a destructor has no business
    // running with a lock held that createFlow() takes on every new connection.
    objRelease(&old);
}

_Ret_maybenull_ TlsConfig* TlsServerFilter_getConfig(_In_ TlsServerFilter* self)
{
    TlsConfig* cfg = NULL;

    withMutex (&self->lock) cfg = self->config ? objAcquire(self->config) : NULL;

    return cfg;
}

NetFlowFilter* TlsServerFilter_createFlow(_In_ TlsServerFilter* self, NetSocketType type)
{
    unused_noeval(type);

    // The reference is taken here and handed to the session, which holds it for its whole life --
    // that is what lets a rotation replace this filter's configuration without disturbing a
    // handshake already in progress on the old one.
    TlsConfig* cfg      = tlsserverfilterGetConfig(self);
    TlsStreamFilter* st = tlsstreamfilterCreate(cfg, NULL, true);
    objRelease(&cfg);

    return NetFlowFilter(st);
}

bool TlsServerFilter_canFilter(_In_ TlsServerFilter* self, NetSocketType type)
{
    unused_noeval(self);
    return type == NST_Stream;
}

void TlsServerFilter_destroy(_In_ TlsServerFilter* self)
{
    // Autogen begins -----
    objRelease(&self->config);
    mutexDestroy(&self->lock);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "tlsfilter.auto.inc"
// clang-format on
// Autogen ends -------
