#pragma once

#include <cx/net/net_private.h>
#include <cx/platform/win.h>
#include "platform/win/win_net_queue_select.h"

#include <ws2tcpip.h>

bool netAddrToSockaddr(_In_ NetAddr* addr, _Out_ struct sockaddr_storage* sa, _Out_ int* sasz);