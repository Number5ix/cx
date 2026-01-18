#include <cx/net/net_private.h>
#include <cx/platform/win.h>
#include "platform/win/win_net_queue_select.h"

#include <Winsock2.h>

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