#pragma once

#include <cx/net/net_private.h>
#include <cx/platform/wasm.h>
#include <cx/buffer/buffer.h>

#include <sys/socket.h>

bool netAddrToSockaddr(_In_ const NetAddr* addr, _Out_ struct sockaddr_storage* sa,
                       _Out_ int* sasz);

/// Fill a NetAddr from an OS sockaddr (the inverse of netAddrToSockaddr).
///
/// @param addr Destination address, populated on success
/// @param sa Source sockaddr, expected to be AF_INET or AF_INET6
/// @return true if the family was recognized and addr was written
bool netAddrFromSockaddr(_Out_ NetAddr* addr, _In_ const struct sockaddr* sa);

// No NetPlatIov/netIovToPlatform here, unlike unix_net.h/win_net.h: this backend does not use
// writev()/WSASend() scatter/gather at all (see netSockSendv() in wasm_net.c) -- one send() call
// per BufIov entry, so there is no platform vector type to translate into. net_private.h's default
// NET_MAX_IOV (64) is left alone; it only bounds how many BufIov entries socket.c gathers before
// calling netSockSendv(), which this backend's loop handles regardless of the count.
