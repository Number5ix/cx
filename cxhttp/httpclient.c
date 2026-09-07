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
STR_CONST(kAlpnHttp11, HTTP_ALPN_HTTP11);

#define HTTPCLIENT_MAX_REDIRECTS    10
#define HTTPCLIENT_RESPONSE_TIMEOUT timeS(60)
#define HTTPCLIENT_IDLE_TIMEOUT     timeS(30)

static void startExchange(HttpClient* self, HttpRequest* req);
static TlsConfig* clientTls(HttpClient* self);

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
// The HTTP/3 pool
//
// A second pair of arrays beside the first, because an HTTP/3 connection is used differently
// rather than merely being a different type. An HTTP/1.1 entry is taken out for the duration of a
// request and put back by _recycle(); an HTTP/3 entry stays where it is, hands out a reference,
// and serves as many requests at once as the peer's stream limit allows.
// ---------------------------------------------------------------------------------------------

// Borrow the connection for this origin, if there is one that will still take a request. The
// reference is the caller's; the pool keeps its own.
_Ret_maybenull_ static ObjInst* h3PoolBorrow(_Inout_ HttpClient* self, _In_opt_ strref key)
{
    ObjInst* found = NULL;

    withMutex (&self->lock) {
        for (int32 i = saSize(self->h3pool) - 1; i >= 0; i--) {
            if (!strEq(self->h3poolKeys.a[i], key))
                continue;

            // A connection the peer has said goodbye to, or that died quietly, must not be handed
            // out again. Dropping it here as well as from the closed handler costs one predicate
            // and closes the window between the peer's close arriving and a worker delivering it.
            if (_http3ConnUsable(self->h3pool.a[i])) {
                found = objAcquire(self->h3pool.a[i]);
                break;
            }

            saRemove(&self->h3pool, i);
            saRemove(&self->h3poolKeys, i);
        }
    }

    return found;
}

static void h3PoolAdd(_Inout_ HttpClient* self, _In_ ObjInst* conn, _In_opt_ strref key)
{
    withMutex (&self->lock) {
        saPush(&self->h3pool, object, conn);
        saPush(&self->h3poolKeys, strref, key);
    }
}

static void h3PoolEvict(_Inout_ HttpClient* self, _In_opt_ ObjInst* conn)
{
    withMutex (&self->lock) {
        for (int32 i = saSize(self->h3pool) - 1; i >= 0; i--) {
            if (self->h3pool.a[i] == conn) {
                saRemove(&self->h3pool, i);
                saRemove(&self->h3poolKeys, i);
            }
        }
    }
}

static void onH3PooledClosed(ObjInst* conn, void* ctx)
{
    HttpClient* self = (HttpClient*)ctx;
    if (self)
        h3PoolEvict(self, conn);
}

// ---------------------------------------------------------------------------------------------
// Dial coalescing
// ---------------------------------------------------------------------------------------------

// Claim the right to dial this origin over QUIC, or park the request behind a dial already in
// flight. True means "go ahead and dial"; false means the request is waiting and will be started
// when that dial lands.
static bool h3DialClaim(_Inout_ HttpClient* self, _Inout_ HttpRequest* req, _In_opt_ strref key)
{
    bool mine = true;

    withMutex (&self->lock) {
        for (int32 i = 0; i < saSize(self->h3Dialing); i++) {
            if (strEq(self->h3Dialing.a[i], key)) {
                mine = false;
                break;
            }
        }

        if (mine) {
            saPush(&self->h3Dialing, strref, key);
        } else {
            saPush(&self->h3Waiters, HttpRequest, req);
            saPush(&self->h3WaiterKeys, strref, key);
        }
    }

    return mine;
}

// A dial for this origin has landed, one way or the other. Takes the requests that were waiting on
// it and clears the claim, so the next request for this origin dials rather than waiting for a
// dial that has already finished.
static void h3DialDone(_Inout_ HttpClient* self, _In_opt_ strref key, _Out_ sa_HttpRequest* waiting)
{
    saInit(waiting, HttpRequest, 4);

    withMutex (&self->lock) {
        for (int32 i = saSize(self->h3Dialing) - 1; i >= 0; i--) {
            if (strEq(self->h3Dialing.a[i], key))
                saRemove(&self->h3Dialing, i);
        }

        for (int32 i = saSize(self->h3WaiterKeys) - 1; i >= 0; i--) {
            if (!strEq(self->h3WaiterKeys.a[i], key))
                continue;
            saPush(waiting, HttpRequest, self->h3Waiters.a[i]);
            saRemove(&self->h3Waiters, i);
            saRemove(&self->h3WaiterKeys, i);
        }
    }
}

void HttpClient__recycle(_In_ HttpClient* self, _In_ HttpRequest* req, bool reusable)
{
    // An HTTP/3 connection was borrowed rather than taken, so there is nothing to put back:
    // recycling a request is only letting go of its stream. The connection stays in the pool
    // serving whatever else is on it, and is reaped when nothing is.
    if (req->h3conn) {
        _http3ReqRelease(req);

        // With pooling off there is nobody to reap it later, so the last request out closes it.
        if (self->idleTimeout <= 0)
            httpclientCloseIdle(self);
        return;
    }

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
    NetSocket* dialing  = NULL;
    NetSocket* dialQuic = NULL;

    withMutex (&req->exLock) {
        // A cancelled exchange reports why it ended rather than what the transport happened to say
        // on the way out -- a closed socket looks identical from below whether we closed it or the
        // peer did. A response that beat the cancel keeps its own success.
        if (req->cancelled && err != HTTPERR_None)
            err = HTTPERR_Aborted;

        // Unbinding the client is what makes a later cancel answer false, so it belongs here with
        // everything else the cancel path reads.
        req->client = NULL;

        // The sockets, if the exchange never got as far as a connection to own them. Under
        // HTTPV_Any there may be two: whichever lost the race is closed here along with the one
        // that never finished.
        dialing       = req->dialSock;
        req->dialSock = NULL;
        dialQuic      = req->dialQuic;
        req->dialQuic = NULL;
    }

    if (dialing) {
        netsocketClose(dialing);
        objRelease(&dialing);
    }
    if (dialQuic) {
        netsocketClose(dialQuic);
        objRelease(&dialQuic);
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

    // An Alt-Svc header is how a client that has only ever spoken HTTP/1.1 to this origin learns
    // that it also answers HTTP/3. Read on every response rather than only on HTTP/1.1 ones,
    // because `clear` has to be honoured wherever it arrives.
    {
        string key = 0;
        poolKey(&key, &req->url);
        _httpOriginLearnAltSvc(self, key, &req->url, ev->headers);
        strDestroy(&key);
    }

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

static void onExProgress(HttpEvent* ev)
{
    HttpRequest* req = ev->request;
    if (req->appHandlers)
        forward(ev, req->appHandlers->progress);
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

    // A server restarting gracefully sends a GOAWAY naming the last stream it will serve, and
    // resets everything above it. The peer has guaranteed those were not processed, so starting
    // one again on a fresh connection is safe without knowing anything about the method -- and
    // that single retry is what makes a rolling restart invisible.
    bool retry = !req->h3Retried && _http3ReqRetryable(req);

    req->neterr = ev->neterr;
    httpclient_recycle(self, req, false);

    if (retry) {
        req->h3Retried = true;
        startExchange(self, req);
        return;
    }

    httpclient_finish(self, req, ev->err);
}

static const HttpHandlers kExchangeHandlers = {
    .status   = onExStatus,
    .headers  = onExHeaders,
    .data     = onExData,
    .progress = onExProgress,
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
    .progress = onExProgress,
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

// ---------------------------------------------------------------------------------------------
// The race
//
// Under HTTPV_Any against an origin nothing is remembered about, both transports are dialled at
// once and the first to become *usable* wins. Nothing is sent until there is a winner, so there is
// no double-send and no idempotency question -- which is why racing is as safe for a POST as for a
// GET.
// ---------------------------------------------------------------------------------------------

// Claim the race for one transport. False means the other one already won, in which case the
// caller closes what it was holding and does nothing else. The loser's socket is closed here,
// which its own dial handlers then see as a teardown for a request that has moved on.
static bool raceClaim(_Inout_ HttpRequest* req, bool quic)
{
    NetSocket* loser = NULL;
    bool won         = false;

    withMutex (&req->exLock) {
        if (!req->raced) {
            req->raced  = true;
            req->racing = false;
            won         = true;

            if (quic) {
                loser         = req->dialSock;
                req->dialSock = NULL;
            } else {
                loser         = req->dialQuic;
                req->dialQuic = NULL;
            }
        }
    }

    // Outside the lock: closing reaches the socket layer, and a per-request mutex has no business
    // being held across that.
    if (loser) {
        netsocketClose(loser);
        objRelease(&loser);
    }

    return won;
}

// One transport's dial failed. On its own that is not a failure: the other may still be coming.
// Only when nothing is left does the request fail, reported by whichever finished last.
static void raceFailed(HttpClient* self, HttpRequest* req, bool quic, HttpError err,
                       NetErrorCode neterr)
{
    NetSocket* dead = NULL;
    bool last       = false;

    withMutex (&req->exLock) {
        if (quic) {
            dead          = req->dialQuic;
            req->dialQuic = NULL;
        } else {
            dead          = req->dialSock;
            req->dialSock = NULL;
        }
        last = !req->raced && !req->dialSock && !req->dialQuic;
    }

    if (dead) {
        netsocketClose(dead);
        objRelease(&dead);
    }

    if (last)
        failExchange(self, req, err, neterr);
}

// Hand the socket over to a connection and write the request. Shared by the plaintext and TLS
// paths, which differ only in when they get here.
static void beginOnSocket(HttpRequest* req, NetSocket* sock)
{
    HttpClient* self = req->client;
    if (!self)
        return;

    // Under HTTPV_Any the QUIC dial may already have won, in which case this socket is surplus.
    if (req->racing && !raceClaim(req, false)) {
        netsocketClose(sock);
        return;
    }

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
        raceFailed(self, req, false, HTTPERR_Network, ev->conn.err);
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
            raceFailed(self, req, false, HTTPERR_BadMessage, NERR_None);
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
        raceFailed(self, req, false, HTTPERR_Network, NERR_None);
}

static void onDialError(NetEvent* ev)
{
    HttpRequest* req = (HttpRequest*)ev->ctx;
    HttpClient* self = req ? req->client : NULL;

    if (self && req->dialSock)
        raceFailed(self, req, false, HTTPERR_Network, ev->error.err);
}

static const NetHandlers kDialHandlers = {
    .connection   = onDialConnection,
    .filterNotify = onDialFilterNotify,
    .flowClosed   = onDialClosed,
    .error        = onDialError,
};

// ---------------------------------------------------------------------------------------------
// Dialing HTTP/3
//
// A separate handler set from the TCP one, because the two say different things. A QUIC
// connection is usable the moment NET_Connection says it is connected and the handshake picked
// `h3`; there is no NFN_Secured step, since QUIC has no unencrypted phase to wait past.
// ---------------------------------------------------------------------------------------------

// Start one request on a connection that has just come up, or fail it. Used for the request that
// did the dialling and for every request that was parked behind it.
static void startOnH3(HttpClient* self, HttpRequest* req, ObjInst* conn)
{
    if (!req->client)
        return;

    // The TCP half of a race may already have won for this one, in which case it is already on its
    // way over HTTP/1.1 and this connection is simply not its business.
    if (req->racing && !raceClaim(req, true))
        return;

    bool cancelled = false;
    withMutex (&req->exLock) {
        cancelled = req->cancelled;
    }
    if (cancelled) {
        failExchange(self, req, HTTPERR_Aborted, NERR_None);
        return;
    }

    if (!conn) {
        failExchange(self, req, HTTPERR_Network, NERR_None);
        return;
    }

    if (!_http3ConnRequest(conn, req, exchangeHandlers(req), NULL, self->responseTimeout))
        failExchange(self, req, HTTPERR_Network, NERR_None);
}

// A QUIC dial has landed. Release everything that was parked behind it, on the connection if there
// is one and through the ordinary failure path if there is not.
static void h3DialLanded(HttpClient* self, HttpRequest* req, ObjInst* conn, strref key)
{
    sa_HttpRequest waiting;
    h3DialDone(self, key, &waiting);

    startOnH3(self, req, conn);

    for (int32 i = 0; i < saSize(waiting); i++) {
        // A parked request that is racing may have been won by TCP while it waited, and one whose
        // dial failed goes back through startExchange() -- which now finds the origin remembered
        // as one where QUIC does not work, and falls back.
        HttpRequest* w = waiting.a[i];
        if (conn)
            startOnH3(self, w, conn);
        else if (!w->racing)
            startExchange(self, w);
        else
            raceFailed(self, w, true, HTTPERR_Network, NERR_None);
    }

    saDestroy(&waiting);
}

// Hand the QUIC socket over to a connection and start the request on it.
static void beginOnQuic(HttpRequest* req, NetSocket* sock)
{
    HttpClient* self = req->client;
    if (!self)
        return;

    string key = 0;
    poolKey(&key, &req->url);

    // A server that negotiated something other than h3 over QUIC is not speaking a protocol this
    // client can read. Unlike the TCP case there is no benign "negotiated nothing": QUIC requires
    // ALPN, so an empty answer means the handshake did not do what it had to.
    string host   = 0;
    ObjInst* conn = NULL;
    if (_http3Negotiated(sock)) {
        httpUrlHostHeader(&host, &req->url);
        conn = _http3ConnCreate(sock, host);
        strDestroy(&host);
    }

    if (!conn) {
        // Remembered, so the next request to this origin does not pay for finding out again.
        _httpOriginRemember(self, key, HTTPORIGIN_H3Failed, 0, HTTPORIGIN_TTL);
        netsocketClose(sock);

        NetSocket* dialing = NULL;
        withMutex (&req->exLock) {
            dialing       = req->dialQuic;
            req->dialQuic = NULL;
        }
        objRelease(&dialing);

        h3DialLanded(self, req, NULL, key);
        if (!req->racing)
            failExchange(self, req, HTTPERR_BadMessage, NERR_None);
        else
            raceFailed(self, req, true, HTTPERR_Network, NERR_None);
        strDestroy(&key);
        return;
    }

    _httpOriginRemember(self, key, HTTPORIGIN_H3Works, 0, HTTPORIGIN_TTL);

    // The connection owns the socket now; the request's dialing reference has done its job.
    NetSocket* dialing = NULL;
    withMutex (&req->exLock) {
        dialing       = req->dialQuic;
        req->dialQuic = NULL;
    }
    objRelease(&dialing);

    // Into the pool before any request starts, because an HTTP/3 connection is borrowed rather
    // than taken: the pool is where it lives from now on, and it has to be reachable by the next
    // request for this origin even while this one is still running.
    _http3ConnSetClosed(conn, onH3PooledClosed, self);
    h3PoolAdd(self, conn, key);

    h3DialLanded(self, req, conn, key);

    objRelease(&conn);   // the pool holds it now
    strDestroy(&key);
}

// The QUIC dial for this origin failed before there was ever a connection. Everything parked
// behind it has to be released, and the origin is remembered so the next request does not repeat
// the wait.
static void quicDialFailed(HttpClient* self, HttpRequest* req, HttpError err, NetErrorCode neterr)
{
    string key = 0;
    poolKey(&key, &req->url);
    _httpOriginRemember(self, key, HTTPORIGIN_H3Failed, 0, HTTPORIGIN_TTL);

    sa_HttpRequest waiting;
    h3DialDone(self, key, &waiting);
    strDestroy(&key);

    for (int32 i = 0; i < saSize(waiting); i++) {
        HttpRequest* w = waiting.a[i];
        if (!w->racing)
            startExchange(self, w);
        else
            raceFailed(self, w, true, err, neterr);
    }
    saDestroy(&waiting);

    raceFailed(self, req, true, err, neterr);
}

static void onQuicDialConnection(NetEvent* ev)
{
    HttpRequest* req = (HttpRequest*)ev->ctx;
    HttpClient* self = req ? req->client : NULL;
    if (!self)
        return;

    if (ev->conn.state == NCS_NotConnected) {
        quicDialFailed(self, req, HTTPERR_Network, ev->conn.err);
        return;
    }

    if (ev->conn.state == NCS_Connected)
        beginOnQuic(req, ev->socket);
}

static void onQuicDialClosed(NetEvent* ev)
{
    HttpRequest* req = (HttpRequest*)ev->ctx;
    HttpClient* self = req ? req->client : NULL;

    // Only reachable while the request is still dialing: once a connection exists it owns the
    // socket's handlers, and this is no longer installed.
    if (self && req->dialQuic)
        quicDialFailed(self, req, HTTPERR_Network, NERR_None);
}

static void onQuicDialError(NetEvent* ev)
{
    HttpRequest* req = (HttpRequest*)ev->ctx;
    HttpClient* self = req ? req->client : NULL;

    if (self && req->dialQuic)
        quicDialFailed(self, req, HTTPERR_Network, ev->error.err);
}

static const NetHandlers kQuicDialHandlers = {
    .connection = onQuicDialConnection,
    .flowClosed = onQuicDialClosed,
    .error      = onQuicDialError,
};

// Which protocol this request should be sent over: its own override if it set one, otherwise the
// client's policy.
static HttpVersionPolicy requestPolicy(HttpClient* self, HttpRequest* req)
{
    return (HttpVersionPolicy)(req->versions ? req->versions : self->versions);
}

// Start the exchange over HTTP/3: on a connection the client already has for this origin, parked
// behind a dial already in flight, or on one dialled now.
//
// `racing` says the caller has a TCP dial going as well, which changes what a failure here means
// -- it is one racer losing rather than the request failing.
static bool startHttp3(HttpClient* self, HttpRequest* req, bool racing)
{
    // HTTP/3 is https by definition: there is no plaintext QUIC to fall back to.
    if (!strEqi(req->url.scheme, _SL("https"))) {
        if (!racing)
            failExchange(self, req, HTTPERR_BadUrl, NERR_None);
        return false;
    }

    string key = 0;
    poolKey(&key, &req->url);

    ObjInst* pooled = h3PoolBorrow(self, key);
    if (pooled) {
        bool ok = _http3ConnRequest(pooled, req, exchangeHandlers(req), NULL,
                                    self->responseTimeout);
        objRelease(&pooled);
        if (ok) {
            strDestroy(&key);
            return true;
        }

        // The connection went away, or is at the peer's stream limit. Either way this request
        // needs one of its own.
    }

    // Somebody else is already dialling this origin. Waiting for that one is what stops ten
    // concurrent requests to a new origin from ending with ten connections.
    if (!h3DialClaim(self, req, key)) {
        strDestroy(&key);
        return true;
    }

    // An Alt-Svc header may have named a port other than the origin's own. The connection is
    // still keyed under the origin and still sends the origin's :authority -- what changes is only
    // where the packets go, which is the whole of what an alternative service is.
    uint16 altPort = 0;
    _httpOriginRecall(self, key, &altPort);
    if (altPort == 0)
        altPort = httpUrlEffectivePort(&req->url);

    TlsConfig* cfg  = clientTls(self);
    NetSocket* sock = NULL;
    if (cfg)
        sock = _http3Dial(self->queue, req->url.host, altPort, NULL, cfg, &kQuicDialHandlers, req);

    if (!sock) {
        quicDialFailed(self, req, HTTPERR_Network, NERR_None);
        strDestroy(&key);
        return false;
    }

    strDestroy(&key);

    withMutex (&req->exLock) {
        req->dialQuic = sock;
    }
    return true;
}

// Which transports to use for this request, decided from the policy and from what the client has
// learned about this origin.
typedef enum {
    HTTPTRY_Http1 = 1,
    HTTPTRY_Http3,
    HTTPTRY_Both      // race them
} HttpTryWhat;

static HttpTryWhat chooseTransport(HttpClient* self, HttpRequest* req)
{
    HttpVersionPolicy pol = requestPolicy(self, req);

    if (pol == HTTPV_Http1)
        return HTTPTRY_Http1;
    if (pol == HTTPV_Http3)
        return HTTPTRY_Http3;

    // Nothing over QUIC without TLS to protect it, whatever the policy says.
    if (!strEqi(req->url.scheme, _SL("https")))
        return HTTPTRY_Http1;

    string key = 0;
    poolKey(&key, &req->url);
    uint16 altPort = 0;
    uint32 known   = _httpOriginRecall(self, key, &altPort);
    strDestroy(&key);

    if (known & HTTPORIGIN_H3Works)
        return HTTPTRY_Http3;
    if (known & HTTPORIGIN_H3Failed)
        return HTTPTRY_Http1;

    // Nothing is known. HTTPV_Default sends HTTP/1.1 -- HTTP/3 is never reached by accident --
    // while HTTPV_Any pays one extra connection attempt to find out, once per origin.
    return (pol == HTTPV_Any) ? HTTPTRY_Both : HTTPTRY_Http1;
}

// The protocols a client offers, put on before anything can seal the configuration.
//
// Both of them, always. A TlsConfig is frozen by the first connection built from it, so a client
// that added h3 only when it first wanted HTTP/3 could never add it at all -- which is exactly the
// case Alt-Svc creates: speak HTTP/1.1, learn the origin also answers HTTP/3, and switch. The
// policy can change at any moment for the same reason, so the list cannot depend on it.
//
// Offering h3 in a TCP handshake is harmless. No conforming server has it in its own list, since
// h3 names HTTP/3 over QUIC and there is no such thing over TCP; one that selected it anyway would
// be answering with a protocol that does not exist there, and onDialFilterNotify() already refuses
// anything but http/1.1.
static void ensureClientAlpn(TlsConfig* cfg)
{
    _httpAlpnAdd(cfg, kAlpnHttp11);
    if (_http3Available())
        _httpAlpnAdd(cfg, _SL(HTTP_ALPN_H3));
}


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
                ensureClientAlpn(made);
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

    HttpTryWhat try = chooseTransport(self, req);

    // Every hop of a redirect decides again, so the race state from the previous one is cleared
    // rather than carried forward.
    withMutex (&req->exLock) {
        req->racing = (try == HTTPTRY_Both);
        req->raced  = false;
    }

    if (try == HTTPTRY_Http3) {
        startHttp3(self, req, false);
        return;
    }

    if (try == HTTPTRY_Both && !startHttp3(self, req, true)) {
        // The QUIC half could not even be started, so there is no race after all.
        withMutex (&req->exLock) {
            req->racing = false;
        }
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
    saInit(&self->h3pool, object, 2);
    saInit(&self->h3poolKeys, string, 2);
    saInit(&self->originKeys, string, 4);
    saInit(&self->originFlags, uint32, 4);
    saInit(&self->originExpires, int64, 4);
    saInit(&self->originAltPort, uint16, 4);
    saInit(&self->h3Dialing, string, 2);
    saInit(&self->h3Waiters, HttpRequest, 4);
    saInit(&self->h3WaiterKeys, string, 4);

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
    if (cfg)
        ensureClientAlpn(cfg);

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

bool HttpClient_setVersions(_In_ HttpClient* self, HttpVersionPolicy v)
{
    // A build with no HTTP/3 in it refuses at the setter rather than at the first request, so a
    // program that asked for it hears about it where it asked.
    if (v != HTTPV_Default && v != HTTPV_Http1 && !_http3Available())
        return false;

    self->versions = (flags_t)v;
    return true;
}

void HttpClient_closeIdle(_In_ HttpClient* self)
{
    sa_HttpConn taken;
    sa_object h3taken;
    saInit(&taken, HttpConn, 4);
    saInit(&h3taken, object, 2);

    // Closing happens outside the lock: it reaches the socket layer, which is not somewhere to be
    // holding a lock that every request's completion path also wants.
    withMutex (&self->lock) {
        for (int32 i = 0; i < saSize(self->pool); i++) saPush(&taken, HttpConn, self->pool.a[i]);
        saClear(&self->pool);
        saClear(&self->poolKeys);

        // An HTTP/3 connection is in the pool while it is in use, so "idle" here means no request
        // is running on it -- unlike an HTTP/1.1 entry, whose presence already says that.
        for (int32 i = saSize(self->h3pool) - 1; i >= 0; i--) {
            if (_http3ConnRequests(self->h3pool.a[i]) > 0)
                continue;
            saPush(&h3taken, object, self->h3pool.a[i]);
            saRemove(&self->h3pool, i);
            saRemove(&self->h3poolKeys, i);
        }
    }

    for (int32 i = 0; i < saSize(taken); i++) {
        httpconnSetClosedHandler(taken.a[i], NULL, NULL);
        httpconnClose(taken.a[i]);
    }

    for (int32 i = 0; i < saSize(h3taken); i++) {
        _http3ConnSetClosed(h3taken.a[i], NULL, NULL);
        _http3ConnClose(h3taken.a[i]);
    }

    saDestroy(&h3taken);
    saDestroy(&taken);
}

void HttpClient_destroy(_In_ HttpClient* self)
{
    httpclientCloseIdle(self);

    httpHeadersDestroy(&self->defaultHeaders);
    saDestroy(&self->pool);
    saDestroy(&self->poolKeys);
    saDestroy(&self->h3pool);
    saDestroy(&self->h3poolKeys);
    saDestroy(&self->originKeys);
    saDestroy(&self->originFlags);
    saDestroy(&self->originExpires);
    saDestroy(&self->originAltPort);
    saDestroy(&self->h3Dialing);
    saDestroy(&self->h3Waiters);
    saDestroy(&self->h3WaiterKeys);
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
