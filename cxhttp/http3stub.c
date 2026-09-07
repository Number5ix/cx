#include "http_private.h"

// What cxhttp is when it was built without HTTP/3.
//
// The version-policy API exists in every build, because a public header that changed shape with a
// build option would make every consumer of cxhttp carry the same conditional. Instead the setters
// ask this, and a program that asks for HTTP/3 in a build that has none is told so at the call
// rather than at the link.

bool _http3Available(void)
{
    return false;
}

_Use_decl_annotations_
bool _http3ServerListen(HttpServer* srv, const NetAddr* addr, TlsConfig* cfg)
{
    unused_noeval(srv);
    unused_noeval(addr);
    unused_noeval(cfg);
    return false;
}

_Use_decl_annotations_
bool _http3ConnClose(ObjInst* conn)
{
    unused_noeval(conn);
    return false;
}

// Neither of these is reachable: both are called only when HttpServerRequest::h3conn is set, and
// nothing in this build can set it.

_Use_decl_annotations_
bool _http3SrvReqRespond(HttpServerRequest* req)
{
    unused_noeval(req);
    return false;
}

_Use_decl_annotations_
bool _http3SrvReqContinue(HttpServerRequest* req)
{
    unused_noeval(req);
    return false;
}

_Use_decl_annotations_
NetSocket* _http3Dial(NetQueue* q, strref host, uint16 port, strref hostname, TlsConfig* cfg,
                      const NetHandlers* handlers, void* ctx, NetConnectPrepCB prep, void* prepctx)
{
    unused_noeval(q);
    unused_noeval(host);
    unused_noeval(port);
    unused_noeval(hostname);
    unused_noeval(cfg);
    unused_noeval(handlers);
    unused_noeval(ctx);
    unused_noeval(prep);
    unused_noeval(prepctx);
    return NULL;
}

_Use_decl_annotations_
bool _http3Negotiated(NetSocket* sock)
{
    unused_noeval(sock);
    return false;
}

_Use_decl_annotations_
ObjInst* _http3ConnCreate(NetSocket* sock, strref host)
{
    unused_noeval(sock);
    unused_noeval(host);
    return NULL;
}

// Everything below is reached only through an HttpRequest that is already on an HTTP/3
// connection, which nothing in this build can produce.

_Use_decl_annotations_
bool _http3ConnRequest(ObjInst* conn, HttpRequest* req, const HttpHandlers* handlers, void* ctx,
                       int64 timeout)
{
    unused_noeval(conn);
    unused_noeval(req);
    unused_noeval(handlers);
    unused_noeval(ctx);
    unused_noeval(timeout);
    return false;
}

_Use_decl_annotations_
bool _http3ConnUsable(ObjInst* conn)
{
    unused_noeval(conn);
    return false;
}

_Use_decl_annotations_
uint32 _http3ConnRequests(ObjInst* conn)
{
    unused_noeval(conn);
    return 0;
}

_Use_decl_annotations_
void _http3ConnSetClosed(ObjInst* conn, Http3ConnClosedCB cb, void* ctx)
{
    unused_noeval(conn);
    unused_noeval(cb);
    unused_noeval(ctx);
}

_Use_decl_annotations_
void _http3ReqBodyNotify(HttpRequest* req)
{
    unused_noeval(req);
}

_Use_decl_annotations_
void _http3ReqCancel(HttpRequest* req)
{
    unused_noeval(req);
}

_Use_decl_annotations_
bool _http3ReqRetryable(HttpRequest* req)
{
    unused_noeval(req);
    return false;
}

_Use_decl_annotations_
void _http3ReqRelease(HttpRequest* req)
{
    unused_noeval(req);
}
