// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "httpserver.h"
// clang-format on
// ==================== Auto-generated section ends ======================

// ---------------------------------------------------------------------------------------------
// The server
//
// Owns a listener, turns every connection on it into an HttpServerConn, and owns those too. Nothing
// outside holds an accepted connection, so without the table here a connection would have to keep
// itself alive and could never be closed from outside.
// ---------------------------------------------------------------------------------------------

#include "http_private.h"

#include <cx/net.h>

// ---------------------------------------------------------------------------------------------
// Accepting
// ---------------------------------------------------------------------------------------------

static void onAccepted(NetEvent* ev)
{
    HttpServer* self  = (HttpServer*)ev->ctx;
    NetSocket* newsock = ev->accept.newSocket;
    if (!self || !newsock)
        return;

    // Without NQ_AutoAccept the queue leaves registration to the application, and a socket that is
    // on no queue never receives anything.
    if (!newsock->queue && !netqueueAddSocket(self->queue, newsock))
        return;

    HttpServerConn* conn = httpsrvconnCreate(self, newsock);
    if (!conn) {
        netsocketClose(newsock);
        return;
    }

    bool taken = false;
    withMutex (&self->lock) {
        if (!self->shuttingDown)
            taken = htInsert(&self->connections, ptr, conn, object, conn, HT_Ignore) != 0;
    }

    if (!taken) {
        // Shutting down. The connection is closed rather than served, and the reference from
        // create() is the only one there ever was.
        httpsrvconnClose(conn);
        objRelease(&conn);
        return;
    }

    // The table holds its own reference, so the factory's is handed back here.
    objRelease(&conn);

    // Bytes may already have arrived: with NQ_AutoAccept the queue starts servicing the accepted
    // socket before this handler runs, and a NET_DataReceived delivered before the handlers above
    // were installed went nowhere. Pumping once picks up anything it left in the socket's ring.
    conn = (HttpServerConn*)newsock->handlerCtx;
    if (conn)
        httpsrvconn_pump(conn);
}

static const NetHandlers kListenHandlers = {
    .accepted = onAccepted,
};

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

_objfactory_check HttpServer* HttpServer_create(_In_ NetQueue* queue)
{
    _httpInit();

    if (!queue)
        return NULL;

    HttpServer* self;
    self = objInstCreate(HttpServer);

    self->queue = objAcquire(queue);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

_objinit_guaranteed bool HttpServer_init(_In_ HttpServer* self)
{
    htInit(&self->connections, ptr, object, 16);
    httpLimitsDefault(&self->limits);

    // A server reads from strangers, so both defaults are set rather than left off. Thirty seconds
    // to send a request head is far more than any real client needs and far less than a connection
    // held open deliberately would like; the idle deadline is what keeps a keep-alive connection
    // from being free to hold forever.
    self->readHeadTimeout = timeS(30);
    self->idleTimeout     = timeS(75);

    // On by default because a client that asked and is not answered simply waits: curl sends
    // Expect on any body over a kilobyte, and a server that ignores it stalls every such upload
    // until curl's own patience runs out.
    self->autoContinue = true;
    // Autogen begins -----
    mutexInit(&self->lock);
    return true;
    // Autogen ends -------
}

void HttpServer_destroy(_In_ HttpServer* self)
{
    httpserverShutdown(self);
    // Autogen begins -----
    objRelease(&self->queue);
    objRelease(&self->listener);
    htDestroy(&self->connections);
    mutexDestroy(&self->lock);
    // Autogen ends -------
}

// ---------------------------------------------------------------------------------------------
// Listening
// ---------------------------------------------------------------------------------------------

bool HttpServer_listen(_In_ HttpServer* self, _In_ NetAddr* addr, int backlog)
{
    if (self->listener || self->shuttingDown)
        return false;

    NetSocket* sock = netqueueListen(self->queue, addr, backlog, &kListenHandlers, self);
    if (!sock)
        return false;

    self->listener = sock;   // the reference netqueueListen() returned is the one we keep
    return true;
}

bool HttpServer_attach(_In_ HttpServer* self, _In_ NetSocket* listener)
{
    if (self->listener || self->shuttingDown || !listener)
        return false;

    if (listener->type != NST_Stream ||
        atomicLoad(uint32, &listener->state, Relaxed) != NS_Listening)
        return false;

    if (!listener->queue && !netqueueAddSocket(self->queue, listener))
        return false;

    self->listener = objAcquire(listener);
    netsocketSetHandlers(listener, &kListenHandlers, self);

    // Whatever was accepted before the server took over is not reachable from here, so a listener
    // should be handed over before anything can have connected to it.
    return true;
}

void HttpServer_setHandlers(_In_ HttpServer* self, _In_opt_ const HttpServerHandlers* handlers,
                            _In_opt_ void* ctx)
{
    self->handlers   = (HttpServerHandlers*)handlers;
    self->handlerCtx = ctx;
}

uint16 HttpServer_port(_In_ HttpServer* self)
{
    return self->listener ? self->listener->local.port : 0;
}

int32 HttpServer_connCount(_In_ HttpServer* self)
{
    int32 n = 0;
    withMutex (&self->lock)
        n = (int32)htSize(self->connections);
    return n;
}

void HttpServer_shutdown(_In_ HttpServer* self)
{
    sa_HttpServerConn conns;
    saInit(&conns, HttpServerConn, 8);

    // Snapshot under the lock and close outside it: closing reaches the socket layer, and a
    // connection dying calls back into _forget(), which takes this same lock.
    withMutex (&self->lock) {
        self->shuttingDown = true;
        foreach (hashtable, hti, self->connections) {
            saPush(&conns, HttpServerConn, (HttpServerConn*)htiVal(object, hti));
        }
    }

    if (self->listener) {
        netsocketSetHandlers(self->listener, NULL, NULL);
        netsocketClose(self->listener);
    }

    for (int32 i = 0; i < saSize(conns); i++)
        httpsrvconnClose(conns.a[i]);

    saDestroy(&conns);
}

// ---------------------------------------------------------------------------------------------
// Talking to the connections
// ---------------------------------------------------------------------------------------------

void HttpServer__forget(_In_ HttpServer* self, _In_ HttpServerConn* conn)
{
    // Normally the last reference the connection has, so nothing may touch it afterwards without
    // holding one of its own -- which the caller, running inside the connection's own teardown,
    // does.
    withMutex (&self->lock)
        htRemove(&self->connections, ptr, conn);
}

void HttpServer__deliver(_In_ HttpServer* self, HttpServerEventType type, _In_opt_ HttpServerConn* conn, _In_opt_ HttpServerRequest* req, _In_opt_ const uint8* data, size_t len, HttpError err, NetErrorCode neterr)
{
    if (!self->handlers)
        return;

    HttpServerEventCB cb = NULL;
    switch (type) {
    case HTTPSRVEV_Head:
        cb = self->handlers->head;
        break;
    case HTTPSRVEV_Data:
        cb = self->handlers->data;
        break;
    case HTTPSRVEV_Progress:
        // Never routed through here -- _deliverProgress() fills in counts this has no way to know,
        // and delivers it itself.
        break;
    case HTTPSRVEV_Request:
        cb = self->handlers->request;
        break;
    case HTTPSRVEV_Error:
        cb = self->handlers->error;
        break;
    case HTTPSRVEV_Closed:
        cb = self->handlers->closed;
        break;
    }

    if (!cb)
        return;

    HttpServerEvent ev = { 0 };
    ev.event   = type;
    ev.conn    = conn;
    ev.request = req;
    ev.ctx     = self->handlerCtx;
    ev.data    = data;
    ev.len     = len;
    ev.err     = err;
    ev.neterr  = neterr;

    cb(&ev);
}

void HttpServer__deliverProgress(_In_ HttpServer* self, _In_opt_ HttpServerConn* conn,
                                 _In_opt_ HttpServerRequest* req, HttpProgressDir dir, uint64 done,
                                 int64 total)
{
    if (!self->handlers || !self->handlers->progress)
        return;

    HttpServerEvent ev = { 0 };
    ev.event           = HTTPSRVEV_Progress;
    ev.conn            = conn;
    ev.request         = req;
    ev.ctx             = self->handlerCtx;
    ev.dir             = dir;
    ev.done            = done;
    ev.total           = total;

    self->handlers->progress(&ev);
}

bool HttpServer_listenTls(_In_ HttpServer* self, _In_ NetAddr* addr, int backlog,
                          _In_ TlsConfig* cfg)
{
    if (self->listener || self->shuttingDown || !cfg)
        return false;

    // nettlsListen() is netqueueListen() with the filter step folded in, and the filter lands on
    // the listener rather than on each accepted socket.
    NetSocket* sock = nettlsListen(self->queue, addr, backlog, cfg, &kListenHandlers, self);
    if (!sock)
        return false;

    self->listener = sock;   // the reference nettlsListen() returned is the one we keep
    return true;
}

// Autogen begins -----
// clang-format off
void HttpServer__forget(_In_ HttpServer* self, _In_ HttpServerConn* conn);
void HttpServer__deliver(_In_ HttpServer* self, HttpServerEventType type, _In_opt_ HttpServerConn* conn, _In_opt_ HttpServerRequest* req, _In_opt_ const uint8* data, size_t len, HttpError err, NetErrorCode neterr);
void HttpServer__deliverProgress(_In_ HttpServer* self, _In_opt_ HttpServerConn* conn, _In_opt_ HttpServerRequest* req, HttpProgressDir dir, uint64 done, int64 total);
#include "httpserver.auto.inc"
// clang-format on
// Autogen ends -------
