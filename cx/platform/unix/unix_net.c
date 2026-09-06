#include "unix_net.h"
#include "platform/unix/unix_net_socket.h"
#include "net/queue_select.h"

#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>

#if defined(_PLATFORM_LINUX)
#include "platform/unix/unix_net_epoll.h"
#elif defined(_PLATFORM_FBSD)
#include "platform/unix/unix_net_kqueue.h"
#endif

bool netPlatformInit(void)
{
    // A write to a peer-closed stream socket raises SIGPIPE, whose default disposition kills the
    // process; Windows has no equivalent failure mode, which is why nothing in this tree has ever
    // had to touch SIGPIPE before. Every non-blocking send already reports the failure through
    // netLastError() (EPIPE -> NERR_ConnectionReset), so the signal itself carries no information
    // the return value doesn't already have.
    signal(SIGPIPE, SIG_IGN);
    return true;
}

NetQueue* netPlatformCreateQueue(_In_ const NetQueueConfig* conf)
{
    flags_t flags = conf ? conf->flags : 0;
#if defined(_PLATFORM_LINUX)
    // epoll is the Unix performance target, exactly as IOCP is on Windows: prefer it unless the
    // caller pins select with NQ_SelectOnly.
    if (!(flags & NQ_SelectOnly)) {
        NetQueue* q = (NetQueue*)netqueueepollCreate((NetQueueConfig*)conf);
        if (q)
            return q;
    }
#elif defined(_PLATFORM_FBSD)
    // kqueue is the FreeBSD performance target, the same role epoll plays on Linux.
    if (!(flags & NQ_SelectOnly)) {
        NetQueue* q = (NetQueue*)netqueuekqueueCreate((NetQueueConfig*)conf);
        if (q)
            return q;
    }
#else
    unused_noeval(flags);
#endif
    return (NetQueue*)netqueueselectCreate((NetQueueConfig*)conf);
}

_Use_decl_annotations_
NetSocket* netPlatformCreateSocket(NetSocketType type)
{
    NetSocketPosix* ps = netsocketposixCreate(type);
    return ps ? NetSocket(ps) : NULL;
}

_Use_decl_annotations_
bool netAddrToSockaddr(const NetAddr* addr, struct sockaddr_storage* sa, int* sasz)
{
    if (addr->type == NA_IPv4) {
        struct sockaddr_in* in4 = (struct sockaddr_in*)sa;
        memset(in4, 0, sizeof(struct sockaddr_in));
        in4->sin_family = AF_INET;
        in4->sin_port   = htons(addr->port);

        // ipv4[0] is the least significant octet (host order); pack big-endian onto the wire.
        uint32_t be = ((uint32_t)addr->ipv4[3] << 24) | ((uint32_t)addr->ipv4[2] << 16) |
                     ((uint32_t)addr->ipv4[1] << 8) | (uint32_t)addr->ipv4[0];
        in4->sin_addr.s_addr = htonl(be);
        *sasz                = sizeof(struct sockaddr_in);
        return true;
    } else if (addr->type == NA_IPv6) {
        struct sockaddr_in6* in6 = (struct sockaddr_in6*)sa;
        memset(in6, 0, sizeof(struct sockaddr_in6));
        in6->sin6_family = AF_INET6;
        in6->sin6_port   = htons(addr->port);

        // ipv6 is already the on-wire network-order byte string; copy straight through.
        memcpy(in6->sin6_addr.s6_addr, addr->ipv6, 16);

        in6->sin6_scope_id = addr->scope;
        *sasz               = sizeof(struct sockaddr_in6);
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
        uint32_t be   = ntohl(in4->sin_addr.s_addr);
        addr->ipv4[3] = (uint8)(be >> 24);
        addr->ipv4[2] = (uint8)(be >> 16);
        addr->ipv4[1] = (uint8)(be >> 8);
        addr->ipv4[0] = (uint8)be;
        return true;
    } else if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6* in6 = (const struct sockaddr_in6*)sa;
        addr->type                     = NA_IPv6;
        addr->port                     = ntohs(in6->sin6_port);
        addr->scope                    = in6->sin6_scope_id;

        memcpy(addr->ipv6, in6->sin6_addr.s6_addr, 16);
        return true;
    }
    return false;
}

// Map an errno value to a NetErrorCode. Split out from netLastError() so it can also classify the
// value returned by getsockopt(SO_ERROR) on a completed non-blocking connect.
static NetErrorCode mapErrnoError(int e)
{
    switch (e) {
    case 0:
        return NERR_None;
    case EINPROGRESS:
        return NERR_WouldBlock;
    case EINTR:
        return NERR_Interrupted;
    case ECONNREFUSED:
        return NERR_ConnectionRefused;
    case ETIMEDOUT:
        return NERR_Timeout;
    case ENETUNREACH:
        return NERR_NetworkUnreachable;
    case EHOSTUNREACH:
        return NERR_HostUnreachable;
    case ENETDOWN:
        return NERR_NetworkDown;
    case EADDRINUSE:
        return NERR_AddressInUse;
    case EISCONN:
        return NERR_AlreadyConnected;
    case ENOTCONN:
        return NERR_NotConnected;
    case ECONNRESET:
    case EPIPE:
        return NERR_ConnectionReset;
#if EAGAIN != EWOULDBLOCK
    case EAGAIN:
        return NERR_WouldBlock;
#endif
    case EWOULDBLOCK:
        return NERR_WouldBlock;
    default:
        return NERR_Unknown;
    }
}

_Use_decl_annotations_
NetErrorCode netLastError(void)
{
    return mapErrnoError(errno);
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

    if (connect((int)h, (struct sockaddr*)&sa, (socklen_t)sasz) == 0) {
        *err = NERR_None;
        return NETCONN_Connected;   // connected synchronously (common on loopback)
    }

    if (errno == EINPROGRESS) {
        *err = NERR_None;
        return NETCONN_InProgress;   // pending; the backend watches for completion
    }

    *err = mapErrnoError(errno);
    return NETCONN_Failed;
}

_Use_decl_annotations_
NetErrorCode netSockConnectResult(NetSockHandle h)
{
    int so_err        = 0;
    socklen_t len      = sizeof(so_err);
    if (getsockopt((int)h, SOL_SOCKET, SO_ERROR, &so_err, &len) != 0)
        return netLastError();
    return mapErrnoError(so_err);
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
    int rc                = getaddrinfo(node, NULL, &hints, &res);
    if (rc != 0)
        return NERR_HostUnreachable;

    // Push every resolved address in the order the resolver returned them; the connect state
    // machine tries them sequentially. The service was left NULL, so set the caller's port
    // explicitly.
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
    // Used by IPv6 zone-ID parsing ("fe80::1%eth0"); returns 0 for an unknown name.
    return (uint32)if_nametoindex(name);
}

_Use_decl_annotations_
intptr netSockRecv(NetSockHandle h, void* buf, size_t len, NetErrorCode* err)
{
    ssize_t n = recv((int)h, buf, len, 0);
    if (n < 0) {
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
    socklen_t salen = sizeof(sa);

    ssize_t n = recvfrom((int)h, buf, len, 0, (struct sockaddr*)&sa, &salen);
    if (n < 0) {
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
// Two things a plain recvfrom()/sendto() cannot carry: which of the machine's own addresses a
// datagram arrived on, and the ECN codepoint the path marked it with. Both travel as control
// messages on recvmsg()/sendmsg(), and both are spelled differently on Linux and the BSDs -- IPv4
// especially, where Linux has one option carrying the whole picture and the BSDs have separate
// ones for the receive address and the send address.
// ---------------------------------------------------------------------------------------------

// The address family a handle is bound to, or AF_UNSPEC if that cannot be determined. Every option
// below is family-specific and a socket's family is not visible from the neutral handle.
static int sockFamily(int fd)
{
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);
    if (getsockname(fd, (struct sockaddr*)&sa, &salen) != 0)
        return AF_UNSPEC;
    return sa.ss_family;
}

_Use_decl_annotations_
void _netCmsgToPktInfo(struct msghdr* mh, NetPktInfo* info)
{
    memset(info, 0, sizeof(*info));

    for (struct cmsghdr* cm = CMSG_FIRSTHDR(mh); cm; cm = CMSG_NXTHDR(mh, cm)) {
#if defined(IP_PKTINFO)
        if (cm->cmsg_level == IPPROTO_IP && cm->cmsg_type == IP_PKTINFO) {
            struct in_pktinfo pi;
            memcpy(&pi, CMSG_DATA(cm), sizeof(pi));
            struct sockaddr_in sin;
            memset(&sin, 0, sizeof(sin));
            sin.sin_family = AF_INET;
            sin.sin_addr   = pi.ipi_addr;
            info->haveLocal = netAddrFromSockaddr(&info->local, (struct sockaddr*)&sin);
            continue;
        }
#endif
#if defined(IP_RECVDSTADDR)
        if (cm->cmsg_level == IPPROTO_IP && cm->cmsg_type == IP_RECVDSTADDR) {
            struct in_addr ia;
            memcpy(&ia, CMSG_DATA(cm), sizeof(ia));
            struct sockaddr_in sin;
            memset(&sin, 0, sizeof(sin));
            sin.sin_family = AF_INET;
            sin.sin_addr   = ia;
            info->haveLocal = netAddrFromSockaddr(&info->local, (struct sockaddr*)&sin);
            continue;
        }
#endif
        if (cm->cmsg_level == IPPROTO_IPV6 && cm->cmsg_type == IPV6_PKTINFO) {
            struct in6_pktinfo pi;
            memcpy(&pi, CMSG_DATA(cm), sizeof(pi));
            struct sockaddr_in6 sin6;
            memset(&sin6, 0, sizeof(sin6));
            sin6.sin6_family   = AF_INET6;
            sin6.sin6_addr     = pi.ipi6_addr;
            sin6.sin6_scope_id = pi.ipi6_ifindex;
            info->haveLocal = netAddrFromSockaddr(&info->local, (struct sockaddr*)&sin6);
            continue;
        }
        bool isTos = (cm->cmsg_level == IPPROTO_IP && cm->cmsg_type == IP_TOS) ||
                     (cm->cmsg_level == IPPROTO_IPV6 && cm->cmsg_type == IPV6_TCLASS);
#if defined(IP_RECVTOS) && IP_RECVTOS != IP_TOS
        // Linux hands the byte back under the option that carries it, the BSDs under the option
        // that asked for it. Both are the same byte.
        isTos = isTos || (cm->cmsg_level == IPPROTO_IP && cm->cmsg_type == IP_RECVTOS);
#endif
        if (isTos) {
            // The traffic class byte is delivered as a u_char by some kernels and an int by
            // others, for the same option; take whichever width actually arrived.
            unsigned int tc = 0;
            size_t len      = (size_t)(cm->cmsg_len - CMSG_LEN(0));
            if (len >= sizeof(int)) {
                int v;
                memcpy(&v, CMSG_DATA(cm), sizeof(v));
                tc = (unsigned int)v;
            } else if (len >= 1) {
                tc = *(const unsigned char*)CMSG_DATA(cm);
            }
            info->ecn     = (uint8)(tc & 0x03);
            info->haveEcn = true;
            continue;
        }
    }
}

_Use_decl_annotations_
intptr netSockRecvFromEx(NetSockHandle h, void* buf, size_t len, NetAddr* from, NetPktInfo* info,
                         NetErrorCode* err)
{
    struct sockaddr_storage sa;
    struct iovec iov = { .iov_base = buf, .iov_len = len };
    uint8 control[NET_CMSG_SPACE];

    struct msghdr mh;
    memset(&mh, 0, sizeof(mh));
    mh.msg_name       = &sa;
    mh.msg_namelen    = sizeof(sa);
    mh.msg_iov        = &iov;
    mh.msg_iovlen     = 1;
    mh.msg_control    = control;
    mh.msg_controllen = sizeof(control);

    ssize_t n = recvmsg((int)h, &mh, 0);
    if (n < 0) {
        *err = netLastError();
        memset(info, 0, sizeof(*info));
        return -1;
    }

    if (from)
        netAddrFromSockaddr(from, (struct sockaddr*)&sa);
    _netCmsgToPktInfo(&mh, info);
    *err = NERR_None;
    return n;   // 0 is a legitimate zero-length datagram here, not a shutdown
}

_Use_decl_annotations_
intptr netSockSendToEx(NetSockHandle h, const void* buf, size_t len, const NetAddr* dest,
                       const NetPktInfo* info, NetErrorCode* err)
{
    struct sockaddr_storage sa;
    int sasz = 0;
    if (!netAddrToSockaddr((NetAddr*)dest, &sa, &sasz)) {
        *err = NERR_Unknown;
        return -1;
    }

    struct iovec iov = { .iov_base = (void*)buf, .iov_len = len };
    uint8 control[NET_CMSG_SPACE];

    struct msghdr mh;
    memset(&mh, 0, sizeof(mh));
    mh.msg_name    = &sa;
    mh.msg_namelen = (socklen_t)sasz;
    mh.msg_iov     = &iov;
    mh.msg_iovlen  = 1;

    bool v6 = sa.ss_family == AF_INET6;
    memset(control, 0, sizeof(control));
    mh.msg_control    = control;
    mh.msg_controllen = sizeof(control);
    size_t used       = 0;
    struct cmsghdr* cm = CMSG_FIRSTHDR(&mh);

    if (info && info->haveLocal && cm) {
        struct sockaddr_storage ls;
        int lsz = 0;
        if (netAddrToSockaddr((NetAddr*)&info->local, &ls, &lsz) && ls.ss_family == sa.ss_family) {
            if (v6) {
                struct in6_pktinfo pi;
                memset(&pi, 0, sizeof(pi));
                pi.ipi6_addr    = ((struct sockaddr_in6*)&ls)->sin6_addr;
                pi.ipi6_ifindex = ((struct sockaddr_in6*)&ls)->sin6_scope_id;
                cm->cmsg_level  = IPPROTO_IPV6;
                cm->cmsg_type   = IPV6_PKTINFO;
                cm->cmsg_len    = CMSG_LEN(sizeof(pi));
                memcpy(CMSG_DATA(cm), &pi, sizeof(pi));
                used += CMSG_SPACE(sizeof(pi));
                cm = CMSG_NXTHDR(&mh, cm);
            } else {
#if defined(IP_PKTINFO)
                struct in_pktinfo pi;
                memset(&pi, 0, sizeof(pi));
                pi.ipi_spec_dst = ((struct sockaddr_in*)&ls)->sin_addr;
                cm->cmsg_level  = IPPROTO_IP;
                cm->cmsg_type   = IP_PKTINFO;
                cm->cmsg_len    = CMSG_LEN(sizeof(pi));
                memcpy(CMSG_DATA(cm), &pi, sizeof(pi));
                used += CMSG_SPACE(sizeof(pi));
                cm = CMSG_NXTHDR(&mh, cm);
#elif defined(IP_SENDSRCADDR)
                struct in_addr ia = ((struct sockaddr_in*)&ls)->sin_addr;
                cm->cmsg_level    = IPPROTO_IP;
                cm->cmsg_type     = IP_SENDSRCADDR;
                cm->cmsg_len      = CMSG_LEN(sizeof(ia));
                memcpy(CMSG_DATA(cm), &ia, sizeof(ia));
                used += CMSG_SPACE(sizeof(ia));
                cm = CMSG_NXTHDR(&mh, cm);
#endif
            }
        }
    }

    if (info && info->haveEcn && cm) {
        // RFC 3542 fixes IPV6_TCLASS as an int. IPv4 has no such rule and the platforms disagree:
        // Linux accepts either width for IP_TOS, the BSDs accept only the byte -- and reject the
        // whole sendmsg if it is anything else. So the byte is what goes out.
        if (v6) {
            int tc         = info->ecn & 0x03;
            cm->cmsg_level = IPPROTO_IPV6;
            cm->cmsg_type  = IPV6_TCLASS;
            cm->cmsg_len   = CMSG_LEN(sizeof(tc));
            memcpy(CMSG_DATA(cm), &tc, sizeof(tc));
            used += CMSG_SPACE(sizeof(tc));
        } else {
            unsigned char tc = (unsigned char)(info->ecn & 0x03);
            cm->cmsg_level   = IPPROTO_IP;
            cm->cmsg_type    = IP_TOS;
            cm->cmsg_len     = CMSG_LEN(sizeof(tc));
            memcpy(CMSG_DATA(cm), &tc, sizeof(tc));
            used += CMSG_SPACE(sizeof(tc));
        }
    }

    // A msghdr with a control buffer but no control messages in it is not the same as one with no
    // control buffer, and some kernels reject the former.
    mh.msg_controllen = (socklen_t)used;
    if (used == 0)
        mh.msg_control = NULL;

    ssize_t n = sendmsg((int)h, &mh, 0);

    // A platform that will not take one of these rejects the whole call, which would make an
    // unsupported option cost every datagram rather than just its mark. Nothing here can tell which
    // option it objected to, so the retry drops all of them -- the datagram matters and the
    // ancillary request does not.
    if (n < 0 && used > 0 && (errno == EINVAL || errno == ENOPROTOOPT || errno == EOPNOTSUPP)) {
        mh.msg_control    = NULL;
        mh.msg_controllen = 0;
        n                 = sendmsg((int)h, &mh, 0);
    }

    if (n < 0) {
        *err = netLastError();
        return -1;
    }
    *err = NERR_None;
    return n;
}

_Use_decl_annotations_
bool netSockRecvInfo(NetSockHandle h, bool enable)
{
    int fd  = (int)h;
    int on  = enable ? 1 : 0;
    int fam = sockFamily(fd);
    bool ok = false;

    if (fam == AF_INET6) {
        ok = setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on, sizeof(on)) == 0;
        ok = setsockopt(fd, IPPROTO_IPV6, IPV6_RECVTCLASS, &on, sizeof(on)) == 0 && ok;
        return ok;
    }
    if (fam != AF_INET)
        return false;

#if defined(IP_PKTINFO)
    ok = setsockopt(fd, IPPROTO_IP, IP_PKTINFO, &on, sizeof(on)) == 0;
#elif defined(IP_RECVDSTADDR)
    ok = setsockopt(fd, IPPROTO_IP, IP_RECVDSTADDR, &on, sizeof(on)) == 0;
#endif
#if defined(IP_RECVTOS)
    ok = setsockopt(fd, IPPROTO_IP, IP_RECVTOS, &on, sizeof(on)) == 0 && ok;
#else
    ok = false;
#endif
    return ok;
}

_Use_decl_annotations_
bool netSockDontFragment(NetSockHandle h, bool enable)
{
    int fd  = (int)h;
    int fam = sockFamily(fd);

#if defined(IP_MTU_DISCOVER)
    // IP_PMTUDISC_PROBE rather than _DO: both set the don't-fragment bit, but _DO also caps the
    // datagram at whatever the kernel's path MTU cache currently believes, which would silently
    // fail the probes that are trying to measure that number for themselves.
    if (fam == AF_INET6) {
        int v = enable ? IPV6_PMTUDISC_PROBE : IPV6_PMTUDISC_DONT;
        return setsockopt(fd, IPPROTO_IPV6, IPV6_MTU_DISCOVER, &v, sizeof(v)) == 0;
    }
    if (fam == AF_INET) {
        int v = enable ? IP_PMTUDISC_PROBE : IP_PMTUDISC_DONT;
        return setsockopt(fd, IPPROTO_IP, IP_MTU_DISCOVER, &v, sizeof(v)) == 0;
    }
    return false;
#elif defined(IP_DONTFRAG)
    int on = enable ? 1 : 0;
    if (fam == AF_INET6)
        return setsockopt(fd, IPPROTO_IPV6, IPV6_DONTFRAG, &on, sizeof(on)) == 0;
    if (fam == AF_INET)
        return setsockopt(fd, IPPROTO_IP, IP_DONTFRAG, &on, sizeof(on)) == 0;
    return false;
#else
    unused_noeval(fd);
    unused_noeval(fam);
    unused_noeval(enable);
    return false;
#endif
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

    ssize_t sent = writev((int)h, pv, (int)niov);
    if (sent < 0) {
        *err = netLastError();
        return -1;
    }
    *err = NERR_None;
    return sent;
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

    ssize_t n = sendto((int)h, buf, len, 0, (struct sockaddr*)&sa, (socklen_t)sasz);
    if (n < 0) {
        *err = netLastError();
        return -1;
    }
    *err = NERR_None;
    return n;
}
