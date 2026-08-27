#pragma once

#include <cx/net/net_shared.h>

/// @file addr.h
/// @brief Network address structures

/// @addtogroup net_addr
/// @{
/// Network address functions for IPv4 and IPv6.

/// Parse an IPv4 or IPv6 address string
///
/// Accepts a dotted-quad IPv4 literal ("192.168.1.1") or an IPv6 literal ("::1",
/// "fe80::1%eth0"), including "::" compression, an embedded IPv4 tail ("::ffff:10.0.0.1"),
/// and a zone suffix -- numeric ("%3") or an interface name resolved through the OS. No port
/// suffix is accepted; the port field is left 0, set addr->port separately.
///
/// @param addr Output address structure
/// @param str Address string to parse
/// @return true if the string was a valid address, false otherwise
///
/// Example:
/// @code
///   NetAddr addr;
///   netAddrFromStr(&addr, _SL("127.0.0.1"));
///   addr.port = 8080;
/// @endcode
bool netAddrFromStr(_Out_ NetAddr* addr, _In_opt_ strref str);

/// Format an address as a string
///
/// Writes the host portion only (dotted-quad for IPv4, RFC 5952 compressed form for IPv6,
/// with a numeric zone suffix if the scope is nonzero) -- the port is not included.
///
/// @param str Output string; overwritten with the formatted address
/// @param addr Address to format
/// @return true if the address was formatted, false if its type was invalid
///
/// Example:
/// @code
///   string s = 0;
///   netAddrToStr(&s, &addr);
/// @endcode
bool netAddrToStr(_Inout_ string* str, _In_ const NetAddr* addr);

/// @}