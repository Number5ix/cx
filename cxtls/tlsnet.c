#include "tls_private.h"

#include <cxtls.h>
#include <cxtls_mbed.h>

#include <cx/net/net_private.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL TlsLogChannel

// ---------------------------------------------------------------------------------------------
// Reaching a session from the flow that owns it
//
// The application never holds a flow filter: the flow's array is its only owner. But the chain is
// a public member of NetFlow, so the session can be found by walking it -- under filterLock, which
// is what every driver pass takes and therefore the only safe way in.
// ---------------------------------------------------------------------------------------------

// The TLS stage of a flow's chain, or NULL. Caller holds filterLock.
static _Ret_maybenull_ TlsStreamFilter* findStage(_In_ NetFlow* flow)
{
    for (int32 i = 0; i < saSize(flow->filters); i++) {
        TlsStreamFilter* f = objDynCast(TlsStreamFilter, flow->filters.a[i]);
        if (f)
            return f;
    }
    return NULL;
}

_Use_decl_annotations_
bool nettlsFlowInfo(NetFlow* flow, TlsInfo* out)
{
    if (!flow || !out)
        return false;

    bool found = false;

    withMutex (&flow->filterLock) {
        TlsStreamFilter* f = findStage(flow);

        // A stage exists from the moment the chain is built, but its snapshot only after the
        // handshake completed -- which is the same edge NFN_Secured is raised on.
        if (f && f->info) {
            const TlsInfo* src = &f->info->info;

            *out = (TlsInfo) { .secured      = src->secured,
                               .peerVerified = src->peerVerified,
                               .verifyFlags  = src->verifyFlags };

            // Copied rather than shared: these outlive the lock, and may outlive the flow.
            strDup(&out->version, src->version);
            strDup(&out->ciphersuite, src->ciphersuite);
            strDup(&out->alpn, src->alpn);
            strDup(&out->peerSubject, src->peerSubject);
            strDup(&out->peerIssuer, src->peerIssuer);

            found = true;
        }
    }

    return found;
}

_Use_decl_annotations_
void nettlsInfoDestroy(TlsInfo* info)
{
    if (!info)
        return;

    strDestroy(&info->version);
    strDestroy(&info->ciphersuite);
    strDestroy(&info->alpn);
    strDestroy(&info->peerSubject);
    strDestroy(&info->peerIssuer);

    info->secured      = false;
    info->peerVerified = false;
    info->verifyFlags  = 0;
}

_Use_decl_annotations_
bool nettlsFlowWithSsl(NetFlow* flow, TlsSslCB cb, void* ctx)
{
    if (!flow || !cb)
        return false;

    bool ran = false;

    // The callback runs inside the lock on purpose: an mbedtls_ssl_context is not thread-safe, and
    // the encode and decode paths reach it from different threads. Handing the pointer out and
    // dropping the lock first would hand out a race.
    withMutex (&flow->filterLock) {
        TlsStreamFilter* f = findStage(flow);
        if (f && f->sess && f->sess->setup) {
            cb(&f->sess->ssl, ctx);
            ran = true;
        }
    }

    return ran;
}

// ---------------------------------------------------------------------------------------------
// mbedTLS interop accessors (cxtls_mbed.h)
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
mbedtls_ssl_config* tlsconfigMbedConf(TlsConfig* config)
{
    return _tlsconfigConf(config);
}

_Use_decl_annotations_
mbedtls_x509_crt* tlscastoreMbedCrt(TlsCAStore* ca)
{
    return &ca->chain->crt;
}

_Use_decl_annotations_
void tlscredsMbed(TlsCreds* creds, mbedtls_x509_crt** crt, mbedtls_pk_context** key)
{
    if (crt)
        *crt = &creds->st->cert;
    if (key)
        *key = &creds->st->key;
}

// ---------------------------------------------------------------------------------------------
// One-call setup
//
// Both mirror their netqueue counterparts (NetQueue_connect / NetQueue_listen) with the filter
// attached between registering the socket and starting traffic on it. That ordering is the whole
// point: a filter installed after the socket is reachable is a filter some bytes can arrive ahead
// of.
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
NetSocket* nettlsConnect(NetQueue* q, strref host, uint16 port, strref hostname, TlsConfig* config,
                         const NetHandlers* handlers, void* ctx)
{
    // The name to authenticate defaults to the name dialed, which is what it is almost always.
    TlsClientFilter* tls = tlsclientfilterCreate(config, strEmpty(hostname) ? host : hostname);
    if (!tls)
        return NULL;

    NetSocket* sock = netqueueSocket(q, NST_Stream);
    if (!sock) {
        objRelease(&tls);
        return NULL;
    }

    bool ok = netqueueAddSocket(q, sock);

    if (ok && handlers)
        netsocketSetHandlers(sock, handlers, ctx);

    // Before connect, so the chain is in place by the time NET_Connection primes it.
    if (ok)
        ok = netsocketAddFilter(sock, NetFilter(tls));

    objRelease(&tls);   // the socket holds its own reference now

    if (ok)
        ok = netsocketConnect(sock, host, port);

    if (!ok) {
        netsocketClose(sock);   // also removes it from the queue
        objRelease(&sock);
        return NULL;
    }

    return sock;
}

_Use_decl_annotations_
NetSocket* nettlsListen(NetQueue* q, const NetAddr* addr, int backlog, TlsConfig* config,
                        const NetHandlers* handlers, void* ctx)
{
    TlsServerFilter* tls = tlsserverfilterCreate(config);
    if (!tls)
        return NULL;

    NetSocket* sock = netqueueSocket(q, NST_Stream);
    if (!sock) {
        objRelease(&tls);
        return NULL;
    }

    bool ok = netqueueAddSocket(q, sock);

    if (ok && handlers)
        netsocketSetHandlers(sock, handlers, ctx);

    // Attached to the listener, not to what it accepts: accepted sockets inherit the listener's
    // filters before they are reachable, which is what closes the window between a connection
    // being serviced and its NET_Accepted handler running.
    if (ok)
        ok = netsocketAddFilter(sock, NetFilter(tls));

    objRelease(&tls);

    if (ok)
        ok = netsocketBind(sock, addr) && netsocketListen(sock, backlog);

    if (!ok) {
        netsocketClose(sock);
        objRelease(&sock);
        return NULL;
    }

    return sock;
}
