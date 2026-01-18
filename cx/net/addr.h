#pragma once

#include <cx/net/net_shared.h>

/// @file addr.h
/// @brief Network address structures

/// @defgroup net_addr Network Addresses
/// @ingroup net
/// @{
/// Network address functions for IPv4 and IPv6.

bool netAddrFromStr(_Out_ NetAddr* addr, _In_opt_ strref str);
bool netAddrToStr(_Inout_ string* str, _In_ NetAddr* addr);

/// @}