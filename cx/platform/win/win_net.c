#include "win_net.h"
#include "platform/win/win_net_socket.h"
#include "platform/win/win_net_iocp.h"
#include "win_os.h"
#include "net/queue_select.h"

#pragma comment(lib, "ws2_32.lib")

#ifndef CX_XP_COMPAT
// for if_nametoindex, used by IPv6 zone-ID parsing
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#endif

static void netPlatformShutdown(void)
{
    WSACleanup();
}

bool netPlatformInit(void)
{
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) == 0) {
        atexit(netPlatformShutdown);
        return true;
    }
    return false;
}

_Use_decl_annotations_
NetQueue* netPlatformCreateIOCP(const NetQueueConfig* conf)
{
    // This is also a direct entry point (the IOCP test suite calls it to pin the backend), so it
    // cannot rely on netqueueCreate() having run the one-time init (WSAStartup) first.
    lazyInit(&_netInit_done, _netInit, NULL);

    // IOCP over Wine is emulated on the readiness path with no throughput win and less coverage, so
    // it is not offered there even when asked for directly.
    if (osIsWine())
        return NULL;
    return (NetQueue*)netqueuewiniocpCreate((NetQueueConfig*)conf);
}

NetQueue* netPlatformCreateQueue(_In_ const NetQueueConfig* conf)
{
    // Prefer the native completion port -- it is the performance target of the whole design. Fall
    // back to the shared, platform-independent select backend when the caller forces it with
    // NQ_SelectOnly, or when netPlatformCreateIOCP() declines (under Wine, where IOCP is emulated on
    // the readiness path with no benefit).
    flags_t flags = conf ? conf->flags : 0;
    if (!(flags & NQ_SelectOnly)) {
        NetQueue* q = netPlatformCreateIOCP(conf);
        if (q)
            return q;
    }
    return (NetQueue*)netqueueselectCreate((NetQueueConfig*)conf);
}

_Use_decl_annotations_
NetSocket* netPlatformCreateSocket(NetSocketType type)
{
    NetSocketWin* ws = netsocketwinCreate(type);
    return ws ? NetSocket(ws) : NULL;
}

_Use_decl_annotations_
bool netAddrToSockaddr(const NetAddr* addr, struct sockaddr_storage* sa, int* sasz)
{
    if (addr->type == NA_IPv4) {
        struct sockaddr_in* in4 = (struct sockaddr_in*)sa;
        memset(in4, 0, sizeof(struct sockaddr_in));
        in4->sin_family = AF_INET;
        in4->sin_port   = htons(addr->port);

        // IPv4 address bytes are in big-endian order
        in4->sin_addr.S_un.S_un_b.s_b1 = addr->ipv4[3];
        in4->sin_addr.S_un.S_un_b.s_b2 = addr->ipv4[2];
        in4->sin_addr.S_un.S_un_b.s_b3 = addr->ipv4[1];
        in4->sin_addr.S_un.S_un_b.s_b4 = addr->ipv4[0];
        *sasz                          = sizeof(struct sockaddr_in);

        return true;
    } else if (addr->type == NA_IPv6) {
        struct sockaddr_in6* in6 = (struct sockaddr_in6*)sa;
        memset(in6, 0, sizeof(struct sockaddr_in6));
        in6->sin6_family = AF_INET6;
        in6->sin6_port   = htons(addr->port);

        // ipv6 is already the on-wire network-order byte string; copy straight through.
        memcpy(in6->sin6_addr.u.Byte, addr->ipv6, 16);

        in6->sin6_scope_id = addr->scope;
        *sasz              = sizeof(struct sockaddr_in6);
        return true;
    }
    return false;
}

_Use_decl_annotations_
bool netAddrFromSockaddr(NetAddr* addr, const struct sockaddr* sa)
{
    memset(addr, 0, sizeof(NetAddr));

    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in* in4 = (const struct sockaddr_in*)sa;
        addr->type                    = NA_IPv4;
        addr->port                    = ntohs(in4->sin_port);

        // Reverse of netAddrToSockaddr: ipv4[0] is the least significant octet.
        addr->ipv4[3] = in4->sin_addr.S_un.S_un_b.s_b1;
        addr->ipv4[2] = in4->sin_addr.S_un.S_un_b.s_b2;
        addr->ipv4[1] = in4->sin_addr.S_un.S_un_b.s_b3;
        addr->ipv4[0] = in4->sin_addr.S_un.S_un_b.s_b4;
        return true;
    } else if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6* in6 = (const struct sockaddr_in6*)sa;
        addr->type                     = NA_IPv6;
        addr->port                     = ntohs(in6->sin6_port);
        addr->scope                    = in6->sin6_scope_id;

        memcpy(addr->ipv6, in6->sin6_addr.u.Byte, 16);
        return true;
    }
    return false;
}

// Map a raw winsock error code to a NetErrorCode. Split out from netLastError() so it can also
// classify codes that do not come from the thread-local WSAGetLastError() value: the result of
// getsockopt(SO_ERROR) on a completed non-blocking connect, and (via the IOCP backend's completion
// mapper) the error a failed overlapped op completes with.
NetErrorCode _netMapWsaError(int e)
{
    switch (e) {
    case 0:
        return NERR_None;
    case WSAEWOULDBLOCK:
        return NERR_WouldBlock;
    case WSAEINPROGRESS:
        return NERR_WouldBlock;
    case WSAEINTR:
        return NERR_Interrupted;
    case WSAECONNREFUSED:
        return NERR_ConnectionRefused;
    case WSAETIMEDOUT:
        return NERR_Timeout;
    case WSAENETUNREACH:
        return NERR_NetworkUnreachable;
    case WSAEHOSTUNREACH:
        return NERR_HostUnreachable;
    case WSAENETDOWN:
        return NERR_NetworkDown;
    case WSAEADDRINUSE:
        return NERR_AddressInUse;
    case WSAEISCONN:
        return NERR_AlreadyConnected;
    case WSAENOTCONN:
        return NERR_NotConnected;
    case WSAECONNRESET:
        return NERR_ConnectionReset;
    default:
        return NERR_Unknown;
    }
}

_Use_decl_annotations_
NetErrorCode netLastError(void)
{
    return _netMapWsaError(WSAGetLastError());
}

_Use_decl_annotations_
NetConnectStatus netSockConnect(NetSockHandle h, const NetAddr* addr, NetErrorCode* err)
{
    struct sockaddr_storage sa;
    int sasz = 0;
    if (!netAddrToSockaddr((NetAddr*)addr, &sa, &sasz)) {
        *err = NERR_Unknown;
        return NETCONN_Failed;
    }

    if (connect((SOCKET)h, (struct sockaddr*)&sa, sasz) == 0) {
        *err = NERR_None;
        return NETCONN_Connected;   // connected synchronously (common on loopback)
    }

    int e = WSAGetLastError();
    if (e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY) {
        *err = NERR_None;
        return NETCONN_InProgress;   // pending; the backend watches for completion
    }

    *err = _netMapWsaError(e);
    return NETCONN_Failed;
}

_Use_decl_annotations_
NetErrorCode netSockConnectResult(NetSockHandle h)
{
    int so_err = 0;
    int len    = sizeof(so_err);
    if (getsockopt((SOCKET)h, SOL_SOCKET, SO_ERROR, (char*)&so_err, &len) != 0)
        return netLastError();
    return _netMapWsaError(so_err);
}

_Use_decl_annotations_
NetErrorCode netPlatformResolve(strref host, uint16 port, sa_NetAddr* out)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;   // return both IPv4 and IPv6 candidates
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags    = AI_ADDRCONFIG;   // suppress a family the host has no address for at all

    // A NULL/empty host resolves to loopback (no AI_PASSIVE), which is what a bare connect wants.
    const char* node = strEmpty(host) ? NULL : strC(host);

    struct addrinfo* res = NULL;
    int rc               = getaddrinfo(node, NULL, &hints, &res);
    if (rc != 0)
        return _netMapWsaError(WSAGetLastError());

    // Push every resolved address in the order the resolver returned them; the connect state machine
    // tries them sequentially. The service was left NULL, so set the caller's port explicitly.
    for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
        NetAddr a;
        if (netAddrFromSockaddr(&a, ai->ai_addr)) {
            a.port = port;
            saPush(out, NetAddr, a);
        }
    }
    freeaddrinfo(res);

    return saSize(*out) > 0 ? NERR_None : NERR_HostUnreachable;
}

_Use_decl_annotations_
uint32 netPlatformIfNameToIndex(const char* name)
{
#ifndef CX_XP_COMPAT
    // if_nametoindex lives in iphlpapi (Vista+). XP compat builds skip named zone lookup
    // entirely -- numeric zone IDs still work since netAddrFromStr parses those itself.
    return (uint32)if_nametoindex(name);
#else
    unused_noeval(name);
    return 0;
#endif
}

_Use_decl_annotations_
intptr netSockRecv(NetSockHandle h, void* buf, size_t len, NetErrorCode* err)
{
    int n = recv((SOCKET)h, (char*)buf, (int)min(len, INT_MAX), 0);
    if (n == SOCKET_ERROR) {
        *err = netLastError();
        return -1;
    }
    *err = NERR_None;
    return n;   // 0 means the peer closed the connection cleanly
}

_Use_decl_annotations_
intptr netSockRecvFrom(NetSockHandle h, void* buf, size_t len, NetAddr* from, NetErrorCode* err)
{
    struct sockaddr_storage sa;
    int salen = sizeof(sa);

    int n = recvfrom((SOCKET)h, (char*)buf, (int)min(len, INT_MAX), 0, (struct sockaddr*)&sa,
                     &salen);
    if (n == SOCKET_ERROR) {
        *err = netLastError();
        return -1;
    }

    if (from)
        netAddrFromSockaddr(from, (struct sockaddr*)&sa);
    *err = NERR_None;
    return n;   // 0 is a legitimate zero-length datagram here, not a shutdown
}

// ---------------------------------------------------------------------------------------------
// Per-datagram IP-layer information
//
// WSARecvMsg and WSASendMsg are the only calls that carry control messages, and neither is a plain
// export: both are reached through a function pointer the socket hands out. The pointers are
// fetched once from a throwaway datagram socket and are valid for every socket in the process.
//
// Everything here degrades to the plain recvfrom/sendto path when the pointer cannot be had or the
// SDK is too old to name the options, which is the same answer a path that strips ECN gives.
// ---------------------------------------------------------------------------------------------

#if defined(NET_HAVE_WSAMSG)

static LazyInitState netMsgFnState;
static NetRecvMsgFn netRecvMsgFn;
static NetSendMsgFn netSendMsgFn;

static void netMsgFnInit(void* unused)
{
    unused_noeval(unused);

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return;

    GUID rid   = WSAID_WSARECVMSG;
    GUID sid   = WSAID_WSASENDMSG;
    DWORD got  = 0;
    WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &rid, sizeof(rid), &netRecvMsgFn,
             sizeof(netRecvMsgFn), &got, NULL, NULL);
    WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &sid, sizeof(sid), &netSendMsgFn,
             sizeof(netSendMsgFn), &got, NULL, NULL);
    closesocket(s);
}

// Whether a control message carries the traffic class byte the ECN bits live in. Windows spells
// this two ways depending on which option asked for it, and which one a given build produces is
// not worth predicting.
static bool cmsgIsEcn(int level, int type)
{
    if (level == IPPROTO_IP && type == IP_TOS)
        return true;
    if (level == IPPROTO_IPV6 && type == IPV6_TCLASS)
        return true;
#if defined(IP_ECN)
    if (level == IPPROTO_IP && type == IP_ECN)
        return true;
#endif
#if defined(IPV6_ECN)
    if (level == IPPROTO_IPV6 && type == IPV6_ECN)
        return true;
#endif
    return false;
}

_Use_decl_annotations_
NetRecvMsgFn _netRecvMsgFn(void)
{
    lazyInit(&netMsgFnState, netMsgFnInit, NULL);
    return netRecvMsgFn;
}

_Use_decl_annotations_
NetSendMsgFn _netSendMsgFn(void)
{
    lazyInit(&netMsgFnState, netMsgFnInit, NULL);
    return netSendMsgFn;
}

_Use_decl_annotations_
bool _netPktInfoToCmsg(WSAMSG* mh, const NetPktInfo* info, int family)
{
    bool v6        = family == AF_INET6;
    size_t used    = 0;
    WSACMSGHDR* cm = WSA_CMSG_FIRSTHDR(mh);

    if (info->haveLocal && cm) {
        struct sockaddr_storage ls;
        int lsz = 0;
        if (netAddrToSockaddr((NetAddr*)&info->local, &ls, &lsz) && ls.ss_family == family) {
            if (v6) {
                IN6_PKTINFO pi;
                memset(&pi, 0, sizeof(pi));
                pi.ipi6_addr    = ((struct sockaddr_in6*)&ls)->sin6_addr;
                pi.ipi6_ifindex = ((struct sockaddr_in6*)&ls)->sin6_scope_id;
                cm->cmsg_level  = IPPROTO_IPV6;
                cm->cmsg_type   = IPV6_PKTINFO;
                cm->cmsg_len    = WSA_CMSG_LEN(sizeof(pi));
                memcpy(WSA_CMSG_DATA(cm), &pi, sizeof(pi));
                used += WSA_CMSG_SPACE(sizeof(pi));
            } else {
                IN_PKTINFO pi;
                memset(&pi, 0, sizeof(pi));
                pi.ipi_addr    = ((struct sockaddr_in*)&ls)->sin_addr;
                cm->cmsg_level = IPPROTO_IP;
                cm->cmsg_type  = IP_PKTINFO;
                cm->cmsg_len   = WSA_CMSG_LEN(sizeof(pi));
                memcpy(WSA_CMSG_DATA(cm), &pi, sizeof(pi));
                used += WSA_CMSG_SPACE(sizeof(pi));
            }
            cm = WSA_CMSG_NXTHDR(mh, cm);
        }
    }

#if defined(IP_ECN) && defined(IPV6_ECN)
    if (info->haveEcn && cm) {
        INT tc         = info->ecn & 0x03;
        cm->cmsg_level = v6 ? IPPROTO_IPV6 : IPPROTO_IP;
        cm->cmsg_type  = v6 ? IPV6_ECN : IP_ECN;
        cm->cmsg_len   = WSA_CMSG_LEN(sizeof(tc));
        memcpy(WSA_CMSG_DATA(cm), &tc, sizeof(tc));
        used += WSA_CMSG_SPACE(sizeof(tc));
    }
#endif

    mh->Control.len = (ULONG)used;
    return used > 0;
}

_Use_decl_annotations_
void _netCmsgToPktInfo(WSAMSG* mh, NetPktInfo* info)
{
    memset(info, 0, sizeof(*info));

    for (WSACMSGHDR* cm = WSA_CMSG_FIRSTHDR(mh); cm; cm = WSA_CMSG_NXTHDR(mh, cm)) {
        if (cm->cmsg_level == IPPROTO_IP && cm->cmsg_type == IP_PKTINFO) {
            IN_PKTINFO pi;
            memcpy(&pi, WSA_CMSG_DATA(cm), sizeof(pi));
            struct sockaddr_in sin;
            memset(&sin, 0, sizeof(sin));
            sin.sin_family  = AF_INET;
            sin.sin_addr    = pi.ipi_addr;
            info->haveLocal = netAddrFromSockaddr(&info->local, (struct sockaddr*)&sin);
            continue;
        }
        if (cm->cmsg_level == IPPROTO_IPV6 && cm->cmsg_type == IPV6_PKTINFO) {
            IN6_PKTINFO pi;
            memcpy(&pi, WSA_CMSG_DATA(cm), sizeof(pi));
            struct sockaddr_in6 sin6;
            memset(&sin6, 0, sizeof(sin6));
            sin6.sin6_family   = AF_INET6;
            sin6.sin6_addr     = pi.ipi6_addr;
            sin6.sin6_scope_id = pi.ipi6_ifindex;
            info->haveLocal    = netAddrFromSockaddr(&info->local, (struct sockaddr*)&sin6);
            continue;
        }
        if (cmsgIsEcn(cm->cmsg_level, cm->cmsg_type)) {
            unsigned int tc = 0;
            size_t len      = (size_t)(cm->cmsg_len - WSA_CMSG_LEN(0));
            if (len >= sizeof(INT)) {
                INT v;
                memcpy(&v, WSA_CMSG_DATA(cm), sizeof(v));
                tc = (unsigned int)v;
            } else if (len >= 1) {
                tc = *(const unsigned char*)WSA_CMSG_DATA(cm);
            }
            info->ecn     = (uint8)(tc & 0x03);
            info->haveEcn = true;
            continue;
        }
    }
}

#endif   // NET_HAVE_WSAMSG

// The address family a handle is bound to, or AF_UNSPEC if that cannot be determined.
static int sockFamily(SOCKET s)
{
    struct sockaddr_storage sa;
    int salen = sizeof(sa);
    if (getsockname(s, (struct sockaddr*)&sa, &salen) != 0)
        return AF_UNSPEC;
    return sa.ss_family;
}

_Use_decl_annotations_
intptr netSockRecvFromEx(NetSockHandle h, void* buf, size_t len, NetAddr* from, NetPktInfo* info,
                         NetErrorCode* err)
{
#if defined(NET_HAVE_WSAMSG)
    lazyInit(&netMsgFnState, netMsgFnInit, NULL);

    if (netRecvMsgFn) {
        struct sockaddr_storage sa;
        WSABUF iov;
        iov.buf = (char*)buf;
        iov.len = (ULONG)min(len, INT_MAX);

        char control[NET_CMSG_SPACE];
        WSAMSG mh;
        memset(&mh, 0, sizeof(mh));
        mh.name             = (struct sockaddr*)&sa;
        mh.namelen          = sizeof(sa);
        mh.lpBuffers        = &iov;
        mh.dwBufferCount    = 1;
        mh.Control.buf      = control;
        mh.Control.len      = sizeof(control);

        DWORD got = 0;
        if (netRecvMsgFn((SOCKET)h, &mh, &got, NULL, NULL) == SOCKET_ERROR) {
            *err = netLastError();
            memset(info, 0, sizeof(*info));
            return -1;
        }

        if (from)
            netAddrFromSockaddr(from, (struct sockaddr*)&sa);
        _netCmsgToPktInfo(&mh, info);
        *err = NERR_None;
        return (intptr)got;
    }
#endif

    memset(info, 0, sizeof(*info));
    return netSockRecvFrom(h, buf, len, from, err);
}

_Use_decl_annotations_
intptr netSockSendToEx(NetSockHandle h, const void* buf, size_t len, const NetAddr* dest,
                       const NetPktInfo* info, NetErrorCode* err)
{
#if defined(NET_HAVE_WSAMSG)
    lazyInit(&netMsgFnState, netMsgFnInit, NULL);

    if (netSendMsgFn && info && (info->haveLocal || info->haveEcn)) {
        struct sockaddr_storage sa;
        int sasz = 0;
        if (!netAddrToSockaddr((NetAddr*)dest, &sa, &sasz)) {
            *err = NERR_Unknown;
            return -1;
        }

        WSABUF iov;
        iov.buf = (char*)buf;
        iov.len = (ULONG)min(len, INT_MAX);

        char control[NET_CMSG_SPACE];
        memset(control, 0, sizeof(control));

        WSAMSG mh;
        memset(&mh, 0, sizeof(mh));
        mh.name          = (struct sockaddr*)&sa;
        mh.namelen       = sasz;
        mh.lpBuffers     = &iov;
        mh.dwBufferCount = 1;
        mh.Control.buf   = control;
        mh.Control.len   = sizeof(control);

        if (_netPktInfoToCmsg(&mh, info, sa.ss_family)) {
            DWORD sent = 0;
            if (netSendMsgFn((SOCKET)h, &mh, 0, &sent, NULL, NULL) == SOCKET_ERROR) {
                *err = netLastError();
                return -1;
            }
            *err = NERR_None;
            return (intptr)sent;
        }
    }
#else
    unused_noeval(info);
#endif

    return netSockSendTo(h, buf, len, dest, err);
}

_Use_decl_annotations_
bool netSockRecvInfo(NetSockHandle h, bool enable)
{
#if defined(NET_HAVE_WSAMSG)
    SOCKET s = (SOCKET)h;
    DWORD on = enable ? 1 : 0;
    int fam  = sockFamily(s);
    bool ok  = false;

    if (fam == AF_INET6) {
        ok = setsockopt(s, IPPROTO_IPV6, IPV6_PKTINFO, (const char*)&on, sizeof(on)) == 0;
#if defined(IPV6_ECN)
        ok = setsockopt(s, IPPROTO_IPV6, IPV6_ECN, (const char*)&on, sizeof(on)) == 0 && ok;
#else
        ok = false;
#endif
        return ok;
    }
    if (fam != AF_INET)
        return false;

    ok = setsockopt(s, IPPROTO_IP, IP_PKTINFO, (const char*)&on, sizeof(on)) == 0;
#if defined(IP_ECN)
    ok = setsockopt(s, IPPROTO_IP, IP_ECN, (const char*)&on, sizeof(on)) == 0 && ok;
#else
    ok = false;
#endif
    return ok;
#else
    unused_noeval(h);
    unused_noeval(enable);
    return false;
#endif
}

_Use_decl_annotations_
bool netSockDontFragment(NetSockHandle h, bool enable)
{
    SOCKET s = (SOCKET)h;
    DWORD on = enable ? 1 : 0;
    int fam  = sockFamily(s);

#if defined(IPV6_DONTFRAG)
    if (fam == AF_INET6)
        return setsockopt(s, IPPROTO_IPV6, IPV6_DONTFRAG, (const char*)&on, sizeof(on)) == 0;
#endif
    if (fam == AF_INET)
        return setsockopt(s, IPPROTO_IP, IP_DONTFRAGMENT, (const char*)&on, sizeof(on)) == 0;
    return false;
}

_Use_decl_annotations_
intptr netSockSendv(NetSockHandle h, const BufIov* iov, size_t niov, NetErrorCode* err)
{
    if (niov == 0) {
        *err = NERR_None;
        return 0;
    }
    if (niov > NET_MAX_IOV)
        niov = NET_MAX_IOV;

    NetPlatIov pv[NET_MAX_IOV];
    netIovToPlatform(pv, iov, niov);

    DWORD sent = 0;
    if (WSASend((SOCKET)h, pv, (DWORD)niov, &sent, 0, NULL, NULL) == SOCKET_ERROR) {
        *err = netLastError();
        return -1;
    }
    *err = NERR_None;
    return (intptr)sent;
}

_Use_decl_annotations_
intptr netSockSendTo(NetSockHandle h, const void* buf, size_t len, const NetAddr* dest,
                     NetErrorCode* err)
{
    struct sockaddr_storage sa;
    int sasz = 0;
    if (!netAddrToSockaddr((NetAddr*)dest, &sa, &sasz)) {
        *err = NERR_Unknown;
        return -1;
    }

    int n = sendto((SOCKET)h, (const char*)buf, (int)min(len, INT_MAX), 0, (struct sockaddr*)&sa,
                   sasz);
    if (n == SOCKET_ERROR) {
        *err = netLastError();
        return -1;
    }
    *err = NERR_None;
    return n;
}
