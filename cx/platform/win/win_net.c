#include "win_net.h"

#pragma comment(lib, "ws2_32.lib")

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

NetQueue* netPlatformCreateQueue(int32 nthreads, flags_t flags)
{
    return (NetQueue*)netqueuewinselectCreate(nthreads, flags);
}

_Use_decl_annotations_
bool netAddrToSockaddr(NetAddr* addr, struct sockaddr_storage* sa, int* sasz)
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

        // IPv6 address bytes are in big-endian order
        for (int i = 0; i < 16; i++) {
            in6->sin6_addr.u.Byte[i] = addr->ipv6[16 - i];
        }

        in6->sin6_scope_id = addr->scope;
        *sasz              = sizeof(struct sockaddr_in6);
        return true;
    }
    return false;
}
