#include "net_private.h"
#include <cx/format.h>
#include <cx/utils/murmur.h>

// Number of significant address bytes for a given address type. Anything past this is union
// padding and must not participate in comparison or hashing.
static size_t addrBytes(const NetAddr* a)
{
    switch (a->type) {
    case NA_IPv4:
        return 4;
    case NA_IPv6:
        return 16;
    default:
        return 0;
    }
}

static intptr netAddrCmp(stype st, stgeneric g1, stgeneric g2, flags_t flags)
{
    const NetAddr* a1 = (const NetAddr*)g1.st_opaque;
    const NetAddr* a2 = (const NetAddr*)g2.st_opaque;

    if (a1->type != a2->type)
        return (a1->type < a2->type) ? -1 : 1;
    if (a1->port != a2->port)
        return (a1->port < a2->port) ? -1 : 1;
    if (a1->scope != a2->scope)
        return (a1->scope < a2->scope) ? -1 : 1;

    size_t n = addrBytes(a1);
    if (n == 0)
        return 0;

    int ret = memcmp(a1->ipv6, a2->ipv6, n);
    return (ret < 0) ? -1 : ((ret > 0) ? 1 : 0);
}

static uint32 netAddrHash(stype st, stgeneric gen, flags_t flags)
{
    const NetAddr* a = (const NetAddr*)gen.st_opaque;

    // Hash only the fields that netAddrCmp() looks at, packed into a contiguous scratch block,
    // so that equal addresses always hash equal regardless of union padding.
    struct {
        uint32 type;
        uint32 scope;
        uint16 port;
        uint8 addr[16];
    } key = { 0 };

    key.type  = (uint32)a->type;
    key.scope = a->scope;
    key.port  = a->port;
    memcpy(key.addr, a->ipv6, addrBytes(a));

    return hashMurmur3((const uint8*)&key, sizeof(key));
}

stDefine(NetAddr) { .id    = stTypeId(opaque),
                    .size  = sizeof(NetAddr),
                    .flags = stFlag(PassPtr),
                    .ops   = { .cmp = netAddrCmp, .hash = netAddrHash } };

// ---------------------------------------------------------------------------------------------
// Address <-> string
//
// Pure byte work on NetAddr, deliberately free of any winsock dependency so the formatting lives
// in cx/net/ rather than the platform layer. NetAddr stores IPv4 in host order (ipv4[0] the least
// significant octet) and IPv6 as the on-wire network-order byte string.
// ---------------------------------------------------------------------------------------------

// Parse one base-10 octet (0-255) from str[*pos], advancing past it. Returns false if there is no
// digit or the value is out of range.
static bool parseOctet(strref str, uint32* pos, uint32 len, uint8* out)
{
    uint32 p     = *pos;
    uint32 val   = 0;
    uint32 start = p;

    while (p < len) {
        uint8 c = strGetChar(str, p);
        if (c < '0' || c > '9')
            break;
        val = val * 10 + (uint32)(c - '0');
        if (val > 255)
            return false;
        p++;
    }

    if (p == start)
        return false;   // no digits consumed

    *out = (uint8)val;
    *pos = p;
    return true;
}

// Parse a dotted-quad IPv4 literal. Returns false for anything that is not exactly four octets.
static bool parseIPv4(strref str, uint32 len, NetAddr* addr)
{
    uint32 pos   = 0;
    uint8 oct[4] = { 0 };

    for (int i = 0; i < 4; i++) {
        if (!parseOctet(str, &pos, len, &oct[i]))
            return false;
        if (i < 3) {
            if (pos >= len || strGetChar(str, pos) != '.')
                return false;
            pos++;
        }
    }
    if (pos != len)
        return false;   // trailing garbage

    addr->type    = NA_IPv4;
    addr->ipv4[3] = oct[0];
    addr->ipv4[2] = oct[1];
    addr->ipv4[1] = oct[2];
    addr->ipv4[0] = oct[3];
    return true;
}

// Parse an IPv6 literal: hex groups separated by ':', at most one "::" run compression, an
// optional embedded IPv4 tail ("::ffff:192.168.1.1"), and an optional "%zone" suffix. A numeric
// zone is taken as the scope ID directly; a named zone (an interface name like "eth0") goes
// through the platform for lookup.
static bool parseIPv6(strref str, uint32 len, NetAddr* addr)
{
    // Split off the zone suffix first; everything before '%' is the address proper.
    uint32 alen = len;
    for (uint32 i = 0; i < len; i++) {
        if (strGetChar(str, i) == '%') {
            alen = i;
            break;
        }
    }
    if (alen < 2)
        return false;

    uint16 grp[8];
    int ngrp     = 0;
    int compress = -1;   // group index where "::" appeared, -1 for none
    uint32 pos   = 0;

    // A leading ':' is only valid as the start of "::".
    if (strGetChar(str, 0) == ':') {
        if (strGetChar(str, 1) != ':')
            return false;
        compress = 0;
        pos      = 2;
    }

    while (pos < alen) {
        // Scan the current token up to the next ':' or the end, noting a '.' which marks an
        // embedded IPv4 tail.
        uint32 tstart = pos;
        bool hasDot   = false;
        while (pos < alen && strGetChar(str, pos) != ':') {
            if (strGetChar(str, pos) == '.')
                hasDot = true;
            pos++;
        }
        uint32 tlen = pos - tstart;
        if (tlen == 0)
            return false;   // empty group: ":::" or a stray ':'

        if (hasDot) {
            // Embedded IPv4 tail; must be the final token and fill the last two groups. The
            // octets go straight into network order here, unlike the standalone IPv4 form.
            if (pos != alen || ngrp > 6)
                return false;

            uint32 p     = tstart;
            uint8 oct[4] = { 0 };
            for (int i = 0; i < 4; i++) {
                if (!parseOctet(str, &p, alen, &oct[i]))
                    return false;
                if (i < 3) {
                    if (p >= alen || strGetChar(str, p) != '.')
                        return false;
                    p++;
                }
            }
            if (p != alen)
                return false;

            grp[ngrp++] = (uint16)(((uint16)oct[0] << 8) | oct[1]);
            grp[ngrp++] = (uint16)(((uint16)oct[2] << 8) | oct[3]);
            break;
        }

        // Plain hex group, 1-4 digits.
        if (tlen > 4 || ngrp >= 8)
            return false;
        uint32 val = 0;
        for (uint32 i = tstart; i < pos; i++) {
            uint8 c = strGetChar(str, i);
            uint32 d;
            if (c >= '0' && c <= '9')
                d = c - '0';
            else if (c >= 'a' && c <= 'f')
                d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                d = c - 'A' + 10;
            else
                return false;
            val = val * 16 + d;
        }
        grp[ngrp++] = (uint16)val;

        if (pos < alen) {
            pos++;   // consume the ':'
            if (pos == alen)
                return false;   // trailing single ':' -- trailing "::" is caught below
            if (strGetChar(str, pos) == ':') {
                if (compress >= 0)
                    return false;   // only one "::" allowed
                compress = ngrp;
                pos++;
            }
        }
    }

    if (compress < 0) {
        if (ngrp != 8)
            return false;
    } else {
        // "::" stands for at least one zero group; expand it by shifting what followed it to
        // the end and zero-filling the gap.
        if (ngrp >= 8)
            return false;
        int tail = ngrp - compress;
        for (int i = 0; i < tail; i++)
            grp[7 - i] = grp[ngrp - 1 - i];
        for (int i = compress; i < 8 - tail; i++)
            grp[i] = 0;
    }

    // Zone suffix: digits are a scope ID as-is, anything else is an interface name for the
    // platform to resolve. An unknown name fails the parse rather than silently dropping the zone.
    uint32 scope = 0;
    if (alen < len) {
        uint32 zp = alen + 1;
        if (zp >= len)
            return false;   // '%' with nothing after it

        bool numeric = true;
        uint64 zval  = 0;
        for (uint32 i = zp; i < len; i++) {
            uint8 c = strGetChar(str, i);
            if (c < '0' || c > '9') {
                numeric = false;
                break;
            }
            zval = zval * 10 + (c - '0');
            if (zval > UINT32_MAX)
                return false;
        }

        if (numeric) {
            scope = (uint32)zval;
        } else {
            char name[64];
            uint32 n = len - zp;
            if (n >= sizeof(name))
                return false;
            for (uint32 i = 0; i < n; i++)
                name[i] = (char)strGetChar(str, zp + i);
            name[n] = 0;

            scope = netPlatformIfNameToIndex(name);
            if (scope == 0)
                return false;
        }
    }

    addr->type = NA_IPv6;
    for (int i = 0; i < 8; i++) {
        addr->ipv6[i * 2]     = (uint8)(grp[i] >> 8);
        addr->ipv6[i * 2 + 1] = (uint8)grp[i];
    }
    addr->scope = scope;
    return true;
}

_Use_decl_annotations_
bool netAddrFromStr(NetAddr* addr, strref str)
{
    memset(addr, 0, sizeof(NetAddr));

    uint32 len = strLen(str);
    if (len == 0)
        return false;

    // Any ':' means IPv6 -- an IPv4 literal has no place for one (this parser takes bare
    // addresses only, no port suffix).
    for (uint32 i = 0; i < len; i++) {
        if (strGetChar(str, i) == ':')
            return parseIPv6(str, len, addr);
    }
    return parseIPv4(str, len, addr);
}

_Use_decl_annotations_
bool netAddrToStr(string* str, const NetAddr* addr)
{
    if (addr->type == NA_IPv4) {
        return strFormat(str, _S"${uint}.${uint}.${uint}.${uint}", stvar(uint8, addr->ipv4[3]),
                         stvar(uint8, addr->ipv4[2]), stvar(uint8, addr->ipv4[1]),
                         stvar(uint8, addr->ipv4[0]));
    }

    if (addr->type == NA_IPv6) {
        // Group the 16 bytes into eight big-endian 16-bit hextets, then apply the RFC 5952 "::"
        // rule: compress the single longest run of two or more zero hextets.
        uint16 grp[8];
        for (int i = 0; i < 8; i++)
            grp[i] = (uint16)((addr->ipv6[i * 2] << 8) | addr->ipv6[i * 2 + 1]);

        int bestStart = -1, bestLen = 0;
        for (int i = 0; i < 8;) {
            if (grp[i] != 0) {
                i++;
                continue;
            }
            int j = i;
            while (j < 8 && grp[j] == 0)
                j++;
            if (j - i > bestLen) {
                bestStart = i;
                bestLen   = j - i;
            }
            i = j;
        }
        if (bestLen < 2)
            bestStart = -1;   // a lone zero is not compressed

        strClear(str);
        string tmp = 0;
        for (int i = 0; i < 8;) {
            if (i == bestStart) {
                strAppend(str, _S"::");
                i += bestLen;
                continue;
            }
            strFormat(&tmp, _S"${uint(hex)}", stvar(uint16, grp[i]));
            strAppend(str, tmp);
            i++;
            if (i < 8 && i != bestStart)
                strAppend(str, _S":");
        }

        // A nonzero scope is written numerically ("%3"), which netAddrFromStr round-trips.
        if (addr->scope != 0) {
            strFormat(&tmp, _S"%${uint}", stvar(uint32, addr->scope));
            strAppend(str, tmp);
        }

        strDestroy(&tmp);
        return true;
    }

    return false;
}