// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "httpclient.h"
// clang-format on
// ==================== Auto-generated section ends ======================

// ---------------------------------------------------------------------------------------------
// The high-level client
//
// Everything here is policy layered over HttpConn, which speaks the protocol and knows nothing
// about any of it. There are four pieces:
//
//   dialing      -- turn a URL into a connected socket, plaintext or TLS
//   redirects    -- decide whether a 3xx is the answer or the start of another exchange
//   cookies      -- read Set-Cookie on the way in, write Cookie on the way out
//   pooling      -- keep a connection when the peer is willing, reap it when it goes quiet
//
// The exchange state lives on the HttpRequest rather than in a separate object, because a request
// is already the refcounted per-transaction thing and a redirect chain is one request that visits
// several connections, not several requests.
// ---------------------------------------------------------------------------------------------

#include "http_private.h"

#include <cx/net.h>
#include <cx/time/clock.h>

STR_CONST(kDefaultAgent, "cx/1 cxhttp");
STR_CONST(kAlpnHttp11, "http/1.1");

#define HTTPCLIENT_MAX_REDIRECTS    10
#define HTTPCLIENT_RESPONSE_TIMEOUT timeS(60)
#define HTTPCLIENT_IDLE_TIMEOUT     timeS(30)

static void startExchange(HttpClient* self, HttpRequest* req);

// ---------------------------------------------------------------------------------------------
// Pool
// ---------------------------------------------------------------------------------------------

// scheme://host:port. The scheme is in the key rather than implied by the port, so an https
// connection can never be handed to a plaintext request for the same endpoint.
static void poolKey(strhandle out, const HttpUrl* url)
{
    string port = 0;
    strFromUInt32(&port, httpUrlEffectivePort(url), 10);

    strClear(out);
    strAppend(out, url->scheme);
    strAppend(out, _SL("://"));
    strAppend(out, url->host);
    strAppendChar(out, ':');
    strAppend(out, port);
    strDestroy(&port);
}

// Caller holds the lock.
static void poolRemoveAt(HttpClient* self, int32 idx)
{
    saRemove(&self->pool, idx);
    saRemove(&self->poolKeys, idx);
}

// Take a usable connection for this origin out of the pool, or NULL. The reference comes with it:
// what the pool held is now the caller's.
static HttpConn* poolTake(HttpClient* self, strref key)
{
    HttpConn* found = NULL;

    withMutex (&self->lock) {
        for (int32 i = saSize(self->pool) - 1; i >= 0; i--) {
            if (!strEq(self->poolKeys.a[i], key))
                continue;

            // Acquire before removing. The pool's is normally the only reference a connection
            // has, so taking it out first would destroy the object -- which clears the socket's
            // handlers on the way down -- and leave this holding freed memory.
            HttpConn* c = objAcquire(self->pool.a[i]);
            poolRemoveAt(self, i);

            // A pooled connection can have died quietly since it went in. The closed handler
            // normally evicts it, but checking here as well costs one predicate and closes the
            // window between the peer's close arriving and the worker delivering it.
            if (httpconnIdle(c)) {
                found = c;
                break;
            }

            httpconnClose(c);
            objRelease(&c);
        }
    }

    return found;
}

static void poolEvict(HttpClient* self, HttpConn* conn)
{
    withMutex (&self->lock) {
        for (int32 i = saSize(self->pool) - 1; i >= 0; i--) {
            if (self->pool.a[i] == conn)
                poolRemoveAt(self, i);
        }
    }
}

// The connection's closed handler while it sits in the pool: whatever killed it -- the peer, an
// error, or its own idle deadline -- it must not be handed out again.
static void onPooledClosed(HttpConn* conn, void* ctx)
{
    HttpClient* self = (HttpClient*)ctx;
    if (self)
        poolEvict(self, conn);
}

// ---------------------------------------------------------------------------------------------
// Terminal paths
// ---------------------------------------------------------------------------------------------

void HttpClient__recycle(_In_ HttpClient* self, _In_ HttpRequest* req, bool reusable)
{
    HttpConn* conn = NULL;
    bool cancelled = false;

    // Detach and read the cancel flag in the same breath. Everything below can end in the
    // connection being destroyed, and a request that still pointed at it would be pointing at freed
    // memory -- and a cancel that set its flag after this read must not find a connection here to
    // close, which is why the two happen under one lock. See httprequestCancel().
    withMutex (&req->exLock) {
        conn      = req->conn;
        req->conn = NULL;
        cancelled = req->cancelled;
    }

    if (!conn)
        return;

    // An abandoned exchange leaves the connection at an unknown point in a response nobody read.
    // Pooling that would hand the next request a stream starting mid-message.
    if (cancelled)
        reusable = false;

    if (reusable && self->idleTimeout > 0 && httpconnIdle(conn)) {
        string key = 0;
        poolKey(&key, &req->url);

        httpconnSetClosedHandler(conn, onPooledClosed, self);

        // Armed before the connection is visible in the pool, so there is no moment at which a
        // connection is poolable but unreaped.
        if (httpconnSetIdleTimeout(conn, self->idleTimeout)) {
            withMutex (&self->lock) {
                saPush(&self->pool, HttpConn, conn);
                saPush(&self->poolKeys, string, key);
            }
            strDestroy(&key);
            objRelease(&conn);
            return;
        }

        strDestroy(&key);
    }

    httpconnSetClosedHandler(conn, NULL, NULL);
    httpconnClose(conn);
    objRelease(&conn);
}

void HttpClient__finish(_In_ HttpClient* self, _In_ HttpRequest* req, HttpError err)
{
    NetSocket* dialing = NULL;

    withMutex (&req->exLock) {
        // A cancelled exchange reports why it ended rather than what the transport happened to say
        // on the way out -- a closed socket looks identical from below whether we closed it or the
        // peer did. A response that beat the cancel keeps its own success.
        if (req->cancelled && err != HTTPERR_None)
            err = HTTPERR_Aborted;

        // Unbinding the client is what makes a later cancel answer false, so it belongs here with
        // everything else the cancel path reads.
        req->client = NULL;

        // The socket, if the exchange never got as far as a connection to own it.
        dialing       = req->dialSock;
        req->dialSock = NULL;
    }

    if (dialing) {
        netsocketClose(dialing);
        objRelease(&dialing);
    }

    req->err = err;

    HttpEventCB cb = NULL;
    if (req->appHandlers)
        cb = (err == HTTPERR_None) ? req->appHandlers->complete : req->appHandlers->error;

    HttpEvent ev = { 0 };
    ev.event     = (err == HTTPERR_None) ? HTTPEV_Complete : HTTPEV_Error;
    ev.request   = req;
    ev.ctx       = req->appCtx;
    ev.status    = req->status;
    ev.version   = req->version;
    ev.headers   = &req->respHeaders;
    ev.err       = err;
    ev.neterr    = req->neterr;

    strDestroy(&req->redirectTo);
    req->discardBody = false;

    if (cb)
        cb(&ev);

    // The reference taken in send(). Last, because the callback above is entitled to be the only
    // other thing holding the request.
    objRelease(&req);
}

// A failure that happens on the dialing path, where there is no connection to settle first.
static void failExchange(HttpClient* self, HttpRequest* req, HttpError err, NetErrorCode neterr)
{
    req->neterr = neterr;
    httpclient_recycle(self, req, false);
    httpclient_finish(self, req, err);
}

// ---------------------------------------------------------------------------------------------
// Redirects
// ---------------------------------------------------------------------------------------------

static bool isRedirect(uint16 status)
{
    return status == HTTP_MovedPermanently || status == HTTP_Found || status == HTTP_SeeOther ||
        status == HTTP_TemporaryRedirect || status == HTTP_PermanentRedirect;
}

static bool sameOrigin(const HttpUrl* a, const HttpUrl* b)
{
    return strEqi(a->scheme, b->scheme) && strEqi(a->host, b->host) &&
        httpUrlEffectivePort(a) == httpUrlEffectivePort(b);
}

// Move the request onto the URL its response pointed at. Returns false if the chain should end
// instead, having already failed the request.
static bool applyRedirect(HttpClient* self, HttpRequest* req)
{
    HttpUrl next;
    if (!httpUrlResolve(&next, &req->url, req->redirectTo)) {
        failExchange(self, req, HTTPERR_BadUrl, NERR_None);
        return false;
    }

    req->redirects++;
    if (req->redirects > self->maxRedirects) {
        httpUrlDestroy(&next);
        failExchange(self, req, HTTPERR_TooManyRedirects, NERR_None);
        return false;
    }

    // 303 says "fetch the result somewhere else with GET" regardless of what was sent. 301 and 302
    // say nothing of the kind, but every implementation has turned a redirected POST into a GET
    // since Netscape did, and a server that redirects a POST is relying on it. 307 and 308 exist
    // precisely to say "no, really, send it again as it was".
    if (req->status == HTTP_SeeOther ||
        ((req->status == HTTP_MovedPermanently || req->status == HTTP_Found) &&
         req->method == HTTP_Post)) {
        req->method = HTTP_Get;
        strDestroy(&req->methodName);
        httprequestSetBody(req, NULL, NULL);
        httpHeadersRemove(&req->reqHeaders, _SL("Content-Type"));
        httpHeadersRemove(&req->reqHeaders, _SL("Content-Length"));
    }

    // Credentials do not follow a request off its origin. A redirect to another host is exactly how
    // an Authorization header ends up somewhere it was never meant to go.
    if (!sameOrigin(&req->url, &next)) {
        httpHeadersRemove(&req->reqHeaders, _SL("Authorization"));
        httpHeadersRemove(&req->reqHeaders, _SL("Cookie"));
    }

    // A body the caller is streaming cannot be sent a second time: those bytes belong to its
    // producer and are gone. Failing is honest; silently sending an empty body would not be. A body
    // that is resident here is fine -- the next hop builds a fresh stream over the same bytes.
    if (req->reqBodyExternal) {
        httpUrlDestroy(&next);
        failExchange(self, req, HTTPERR_BadMessage, NERR_None);
        return false;
    }

    httpUrlDestroy(&req->url);
    req->url = next;

    // The previous response is not this request's answer, and none of it should survive into the
    // next one.
    req->status  = 0;
    req->version = HTTPVER_Unknown;
    strDestroy(&req->reason);
    strDestroy(&req->respBody);
    httpHeadersClear(&req->respHeaders);
    strDestroy(&req->redirectTo);
    req->discardBody = false;

    return true;
}

// ---------------------------------------------------------------------------------------------
// Response handlers
//
// These sit between HttpConn and the application, applying everything the application asked the
// client to handle for it, then forwarding what is left.
// ---------------------------------------------------------------------------------------------

static void forward(HttpEvent* ev, HttpEventCB cb)
{
    HttpRequest* req = ev->request;
    HttpEvent out    = *ev;
    out.ctx          = req->appCtx;
    if (cb)
        cb(&out);
}

static void onExStatus(HttpEvent* ev)
{
    HttpRequest* req = ev->request;
    req->status      = ev->status;
    req->version     = ev->version;

    // Held back until the headers decide whether this response is the answer or a signpost. A
    // status callback firing for each hop of a redirect chain would report four 302s and then a
    // 200 for what the application asked as one question.
    if (!(req->flags & HTTPREQ_NoRedirect) && isRedirect(ev->status))
        return;

    if (req->appHandlers)
        forward(ev, req->appHandlers->status);
}

static void onExHeaders(HttpEvent* ev)
{
    HttpRequest* req = ev->request;
    HttpClient* self = req->client;
    if (!self)
        return;

    if (self->jar && !(req->flags & HTTPREQ_NoCookies))
        httpcookiejarStoreAll(self->jar, &req->url, ev->headers);

    if (!(req->flags & HTTPREQ_NoRedirect) && isRedirect(ev->status)) {
        string loc = 0;
        if (httpHeadersGet(ev->headers, _SL("Location"), &loc) && !strEmpty(loc)) {
            // The body of this response is not an answer to anything, and reading it is only
            // necessary to get the connection to the start of the next message.
            strDup(&req->redirectTo, loc);
            req->discardBody = true;
            strDestroy(&loc);
            return;
        }
        strDestroy(&loc);
        // A 3xx with no Location is not a redirect, whatever it says. Deliver it as the response.
        if (req->appHandlers)
            forward(ev, req->appHandlers->status);
    }

    if (req->appHandlers)
        forward(ev, req->appHandlers->headers);
}

static void onExData(HttpEvent* ev)
{
    HttpRequest* req = ev->request;
    if (req->appHandlers)
        forward(ev, req->appHandlers->data);
}

static void onExComplete(HttpEvent* ev)
{
    HttpRequest* req = ev->request;
    HttpClient* self = req->client;
    if (!self)
        return;

    bool reusable = ev->conn && httpconnIdle(ev->conn);

    if (!strEmpty(req->redirectTo)) {
        httpclient_recycle(self, req, reusable);
        if (applyRedirect(self, req))
            startExchange(self, req);
        return;
    }

    httpclient_recycle(self, req, reusable);
    httpclient_finish(self, req, HTTPERR_None);
}

static void onExError(HttpEvent* ev)
{
    HttpRequest* req = ev->request;
    HttpClient* self = req->client;
    if (!self)
        return;

    req->neterr = ev->neterr;
    httpclient_recycle(self, req, false);
    httpclient_finish(self, req, ev->err);
}

static const HttpHandlers kExchangeHandlers = {
    .status   = onExStatus,
    .headers  = onExHeaders,
    .data     = onExData,
    .complete = onExComplete,
    .error    = onExError,
};

// The same set with no data handler, and the reason it has to exist: HttpConn decides where a body
// goes by asking whether anyone registered for it, so a client that always registered would make
// the buffered disposition unreachable. Which set an exchange uses is decided by whether the
// application asked for chunks.
static const HttpHandlers kExchangeHandlersNoData = {
    .status   = onExStatus,
    .headers  = onExHeaders,
    .complete = onExComplete,
    .error    = onExError,
};

static const HttpHandlers* exchangeHandlers(HttpRequest* req)
{
    return (req->appHandlers && req->appHandlers->data) ?
        &kExchangeHandlers :
        &kExchangeHandlersNoData;
}

// ---------------------------------------------------------------------------------------------
// Dialing
// ---------------------------------------------------------------------------------------------

// Fill in the headers the client contributes: defaults the request did not set for itself, and the
// cookies the jar says belong on this URL. Called for every hop of a redirect chain, because the
// right cookies for the second host are not the ones that went to the first.
static void applyClientHeaders(HttpClient* self, HttpRequest* req)
{
    withMutex (&self->lock) {
        for (int32 i = 0; i < httpHeadersCount(&self->defaultHeaders); i++) {
            strref name = self->defaultHeaders.names.a[i];
            if (!httpHeadersHas(&req->reqHeaders, name))
                httpHeadersSet(&req->reqHeaders, name, self->defaultHeaders.values.a[i]);
        }
    }

    // Only touch Cookie when a jar is actually managing it. Clearing it unconditionally would throw
    // away a header the application set by hand, which is the only way to send one when there is
    // no jar.
    if (self->jar && !(req->flags & HTTPREQ_NoCookies)) {
        httpHeadersRemove(&req->reqHeaders, _SL("Cookie"));

        string cookies = 0;
        if (httpcookiejarHeader(self->jar, &cookies, &req->url))
            httpHeadersSet(&req->reqHeaders, _SL("Cookie"), cookies);
        strDestroy(&cookies);
    }
}

// Hand the socket over to a connection and write the request. Shared by the plaintext and TLS
// paths, which differ only in when they get here.
static void beginOnSocket(HttpRequest* req, NetSocket* sock)
{
    HttpClient* self = req->client;
    if (!self)
        return;

    string host = 0;
    httpUrlHostHeader(&host, &req->url);

    HttpConn* conn = httpconnCreate(sock, host);
    strDestroy(&host);

    if (!conn) {
        failExchange(self, req, HTTPERR_Network, NERR_None);
        return;
    }

    // The connection owns the socket now; the request's dialing reference has done its job. Both
    // fields move together so a concurrent cancel sees exactly one of them, never neither.
    NetSocket* dialing = NULL;
    bool cancelled     = false;
    withMutex (&req->exLock) {
        dialing       = req->dialSock;
        req->dialSock = NULL;
        req->conn     = conn;
        cancelled     = req->cancelled;
    }
    objRelease(&dialing);

    // A cancel that landed while the handshake was finishing claimed the request but found nothing
    // to close. Honour it now rather than writing a request nobody is waiting for.
    if (cancelled) {
        failExchange(self, req, HTTPERR_Aborted, NERR_None);
        return;
    }

    conn->timeout = self->responseTimeout;

    if (!httpconnRequest(conn, req, exchangeHandlers(req), NULL))
        failExchange(self, req, HTTPERR_Network, NERR_None);
}

static void onDialConnection(NetEvent* ev)
{
    HttpRequest* req = (HttpRequest*)ev->ctx;
    HttpClient* self = req ? req->client : NULL;
    if (!self)
        return;

    if (ev->conn.state == NCS_NotConnected) {
        failExchange(self, req, HTTPERR_Network, ev->conn.err);
        return;
    }

    if (ev->conn.state != NCS_Connected)
        return;

    // On a TLS connection this is the TCP layer only; the request waits for NFN_Secured, because
    // the Host header names the peer being authenticated and sending before the certificate is
    // verified would be asserting a name nobody has checked.
    if (!strEqi(req->url.scheme, _SL("https")))
        beginOnSocket(req, ev->socket);
}

static void onDialFilterNotify(NetEvent* ev)
{
    HttpRequest* req = (HttpRequest*)ev->ctx;
    HttpClient* self = req ? req->client : NULL;
    if (!self || ev->filter.notify != NFN_Secured)
        return;

    // A server that negotiated something other than http/1.1 is not speaking a protocol this
    // client can read. Nothing negotiated at all is fine -- ALPN is an optimization, and a server
    // that ignores it is assumed to be speaking 1.1.
    TlsInfo info;
    if (nettlsFlowInfo(ev->flow, &info)) {
        bool ok = strEmpty(info.alpn) || strEq(info.alpn, kAlpnHttp11);
        nettlsInfoDestroy(&info);
        if (!ok) {
            failExchange(self, req, HTTPERR_BadMessage, NERR_None);
            return;
        }
    }

    beginOnSocket(req, ev->socket);
}

static void onDialClosed(NetEvent* ev)
{
    HttpRequest* req = (HttpRequest*)ev->ctx;
    HttpClient* self = req ? req->client : NULL;

    // Only reachable while the request is still dialing: once a connection exists it owns the
    // socket's handlers, and this is no longer installed. A close here is a handshake that failed.
    if (self && req->dialSock)
        failExchange(self, req, HTTPERR_Network, NERR_None);
}

static void onDialError(NetEvent* ev)
{
    HttpRequest* req = (HttpRequest*)ev->ctx;
    HttpClient* self = req ? req->client : NULL;

    if (self && req->dialSock)
        failExchange(self, req, HTTPERR_Network, ev->error.err);
}

static const NetHandlers kDialHandlers = {
    .connection   = onDialConnection,
    .filterNotify = onDialFilterNotify,
    .flowClosed   = onDialClosed,
    .error        = onDialError,
};

// Build the TLS configuration on first use, so an application that never makes an https request
// never pays for loading the system trust store.
static TlsConfig* clientTls(HttpClient* self)
{
    TlsConfig* cfg = NULL;

    withMutex (&self->lock) {
        if (!self->tls) {
            TlsCAStore* ca = tlscastoreCreate();
            if (ca)
                tlscastoreAddSystem(ca);

            TlsConfig* made = tlsconfigCreateClient();
            if (made) {
                if (ca)
                    tlsconfigSetCA(made, ca);

                sa_string alpn;
                saInit(&alpn, string, 1);
                saPush(&alpn, strref, kAlpnHttp11);
                tlsconfigSetALPN(made, &alpn);
                saDestroy(&alpn);
            }
            objRelease(&ca);
            self->tls = made;
        }
        cfg = self->tls;
    }

    return cfg;
}

static void startExchange(HttpClient* self, HttpRequest* req)
{
    // A cancel that arrived between two hops of a redirect claimed the request but had no transport
    // to close, because there was none. This is where that claim takes effect.
    //
    // Note the lock discipline: this takes the *request's* lock, and applyClientHeaders() below
    // takes the *client's*. The two regions are disjoint and are never nested, in either order.
    bool cancelled = false;
    withMutex (&req->exLock) {
        cancelled = req->cancelled;
    }
    if (cancelled) {
        failExchange(self, req, HTTPERR_Aborted, NERR_None);
        return;
    }

    applyClientHeaders(self, req);

    bool tls    = strEqi(req->url.scheme, _SL("https"));
    uint16 port = httpUrlEffectivePort(&req->url);
    if (port == 0 || strEmpty(req->url.host)) {
        failExchange(self, req, HTTPERR_BadUrl, NERR_None);
        return;
    }

    string key = 0;
    poolKey(&key, &req->url);
    HttpConn* pooled = poolTake(self, key);
    strDestroy(&key);

    if (pooled) {
        // A pooled connection carries the pool's closed handler; the exchange needs it back so a
        // death mid-request reaches the request rather than the pool it is no longer in.
        httpconnSetClosedHandler(pooled, NULL, NULL);
        pooled->timeout = self->responseTimeout;

        withMutex (&req->exLock) {
            req->conn = pooled;
        }
        if (httpconnRequest(pooled, req, exchangeHandlers(req), NULL))
            return;

        // The connection went stale between the pool check and the write. Drop it and dial.
        httpclient_recycle(self, req, false);
    }

    NetSocket* sock;
    if (tls) {
        TlsConfig* cfg = clientTls(self);
        if (!cfg) {
            failExchange(self, req, HTTPERR_Network, NERR_None);
            return;
        }
        sock = nettlsConnect(self->queue, req->url.host, port, NULL, cfg, &kDialHandlers, req);
    } else {
        sock = netqueueConnect(self->queue, req->url.host, port, &kDialHandlers, req);
    }

    if (!sock) {
        failExchange(self, req, HTTPERR_Network, NERR_None);
        return;
    }

    withMutex (&req->exLock) {
        req->dialSock = sock;
    }
}

bool HttpClient__start(_In_ HttpClient* self, _In_ HttpRequest* req)
{
    startExchange(self, req);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Lifecycle and configuration
// ---------------------------------------------------------------------------------------------

_objfactory_check HttpClient* HttpClient_create(_In_ NetQueue* q)
{
    _httpInit();

    if (!q)
        return NULL;

    HttpClient* self;
    self = objInstCreate(HttpClient);

    self->queue = objAcquire(q);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

_objinit_guaranteed bool HttpClient_init(_In_ HttpClient* self)
{
    httpHeadersInit(&self->defaultHeaders);
    httpHeadersSet(&self->defaultHeaders, _SL("User-Agent"), kDefaultAgent);

    saInit(&self->pool, HttpConn, 4);
    saInit(&self->poolKeys, string, 4);

    self->maxRedirects    = HTTPCLIENT_MAX_REDIRECTS;
    self->responseTimeout = HTTPCLIENT_RESPONSE_TIMEOUT;
    self->idleTimeout     = HTTPCLIENT_IDLE_TIMEOUT;

    // Autogen begins -----
    mutexInit(&self->lock);
    return true;
    // Autogen ends -------
}

void HttpClient_setTlsConfig(_In_ HttpClient* self, _In_opt_ TlsConfig* cfg)
{
    withMutex (&self->lock) {
        objRelease(&self->tls);
        self->tls = cfg ? objAcquire(cfg) : NULL;
    }
}

void HttpClient_setCookieJar(_In_ HttpClient* self, _In_opt_ HttpCookieJar* jar)
{
    withMutex (&self->lock) {
        objRelease(&self->jar);
        self->jar = jar ? objAcquire(jar) : NULL;
    }
}

bool HttpClient_setHeader(_In_ HttpClient* self, _In_opt_ strref name, _In_opt_ strref value)
{
    if (strEmpty(name))
        return false;

    bool ok = true;
    withMutex (&self->lock) {
        if (strEmpty(value))
            httpHeadersRemove(&self->defaultHeaders, name);
        else
            ok = httpHeadersSet(&self->defaultHeaders, name, value);
    }
    return ok;
}

bool HttpClient_send(_In_ HttpClient* self, _In_ HttpRequest* req,
                     _In_opt_ const HttpHandlers* handlers, _In_opt_ void* ctx)
{
    if (!req)
        return false;

    bool busy = false;
    withMutex (&req->exLock) {
        busy = req->client != NULL;
    }
    if (busy)
        return false;   // already in flight; one exchange per request at a time

    if (strEmpty(req->url.host) || httpUrlEffectivePort(&req->url) == 0)
        return false;

    withMutex (&req->exLock) {
        req->client    = self;
        req->cancelled = false;   // a request may be sent again after a cancelled one finished
    }

    req->appHandlers = (HttpHandlers*)handlers;
    req->appCtx      = ctx;
    req->redirects   = 0;
    req->err         = HTTPERR_None;
    req->neterr      = NERR_None;
    req->discardBody = false;
    strDestroy(&req->redirectTo);

    // The client's reference, held until the exchange reaches a terminal event. This is what lets
    // an application create a request, send it, and release it immediately.
    objAcquire(req);

    startExchange(self, req);
    return true;
}

void HttpClient_closeIdle(_In_ HttpClient* self)
{
    sa_HttpConn taken;
    saInit(&taken, HttpConn, 4);

    // Closing happens outside the lock: it reaches the socket layer, which is not somewhere to be
    // holding a lock that every request's completion path also wants.
    withMutex (&self->lock) {
        for (int32 i = 0; i < saSize(self->pool); i++) saPush(&taken, HttpConn, self->pool.a[i]);
        saClear(&self->pool);
        saClear(&self->poolKeys);
    }

    for (int32 i = 0; i < saSize(taken); i++) {
        httpconnSetClosedHandler(taken.a[i], NULL, NULL);
        httpconnClose(taken.a[i]);
    }

    saDestroy(&taken);
}

void HttpClient_destroy(_In_ HttpClient* self)
{
    httpclientCloseIdle(self);

    httpHeadersDestroy(&self->defaultHeaders);
    saDestroy(&self->pool);
    saDestroy(&self->poolKeys);
    // Autogen begins -----
    objRelease(&self->queue);
    objRelease(&self->tls);
    objRelease(&self->jar);
    mutexDestroy(&self->lock);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
bool HttpClient__start(_In_ HttpClient* self, _In_ HttpRequest* req);
void HttpClient__finish(_In_ HttpClient* self, _In_ HttpRequest* req, HttpError err);
void HttpClient__recycle(_In_ HttpClient* self, _In_ HttpRequest* req, bool reusable);
#include "httpclient.auto.inc"
// clang-format on
// Autogen ends -------
