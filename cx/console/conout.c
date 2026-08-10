#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "console_private.h"

#include <cx/debug/assert.h>
#include <cx/format.h>
#include <cx/string/striter.h>
#include <cx/string/strnum.h>
// must follow striter.h -- its decoder walks the string through striChar()
#include <cx/string/string_private_utf8.h>
#include <cx/utils/compare.h>

#include <string.h>

// Appends raw bytes to a CON_Kind_Mem stream's capture buffer. strBuffer() grows and
// zero-pads while preserving existing content, so this is a plain append.
static void memAppend(ConStream* con, const void* buf, size_t sz)
{
    uint32 oldlen = strLen(con->memcapture);
    uint8* dst    = strBuffer(&con->memcapture, oldlen + (uint32)sz);
    memcpy(dst + oldlen, buf, sz);
    strSetLen(&con->memcapture, oldlen + (uint32)sz);
}

// Must be called with con locked.
static bool flushLocked(ConStream* con)
{
    if (con->kind == CON_Kind_Mem || con->bufused == 0)
        return true;

    bool ok      = _conPlatWrite(con, con->buf, con->bufused);
    con->bufused = 0;
    return ok;
}

// Must be called with con locked. Shared with constyle.c via console_private.h.
_Use_decl_annotations_
bool _conWriteLocked(ConStream* con, const uint8* buf, size_t sz)
{
    if (sz == 0)
        return true;

    if (con->kind == CON_Kind_Mem) {
        memAppend(con, buf, sz);
        return true;
    }

    bool hasNL = con->linebuffered && memchr(buf, '\n', sz) != NULL;
    bool ok    = true;

    while (sz > 0) {
        if (con->bufused == con->bufsz && !flushLocked(con)) {
            ok = false;
            break;
        }

        uint32 space = con->bufsz - con->bufused;
        uint32 n     = (uint32)clamphigh(sz, (size_t)space);
        memcpy(con->buf + con->bufused, buf, n);
        con->bufused += n;
        buf += n;
        sz -= n;
    }

    if (ok && (con->autoflush || hasNL))
        ok = flushLocked(con);

    return ok;
}

// unlike string encoder, always emit *something*, even if it's the placeholder
_Use_decl_annotations_
uint32 _conUtf8Encode(uint8 out[4], int32 cp)
{
    uint32 n = _strUTF8Encode(out, cp);
    return n ? n : _strUTF8Encode(out, 0xFFFD);
}

// Shared with conin.c's unix backend via console_private.h.
_Use_decl_annotations_
uint32 _conUtf8Decode(const uint8* buf, uint32 len, int32* cp)
{
    devAssert(len > 0);

    uint8 b0 = buf[0];
    if (b0 < 0x80) {
        *cp = b0;
        return 1;
    }

    uint32 need;
    int32 c;
    if ((b0 & 0xE0) == 0xC0) {
        need = 1;
        c    = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
        need = 2;
        c    = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {
        need = 3;
        c    = b0 & 0x07;
    } else {
        *cp = 0xFFFD;   // not a valid UTF-8 lead byte
        return 1;
    }

    if (len < 1 + need) {
        *cp = 0xFFFD;   // sequence truncated by a short/interrupted read
        return len;
    }

    for (uint32 i = 1; i <= need; i++) {
        if ((buf[i] & 0xC0) != 0x80) {
            *cp = 0xFFFD;   // bad continuation byte -- stop before it, not past it
            return i;
        }
        c = (c << 6) | (buf[i] & 0x3F);
    }

    *cp = c;
    return 1 + need;
}

// Shared with constyle.c and concursor.c via console_private.h -- both build escape
// sequences out of small unsigned numbers.
_Use_decl_annotations_
uint32 _conAppendDec(char* buf, uint32 pos, uint32 v)
{
    uint8 digits[STRNUM_INTBUF];
    uint32 len;
    uint8* p = _strnum_u64toa(digits, &len, v, 10, 0, 0, false);
    memcpy(buf + pos, p, len);
    return pos + len;
}

_Use_decl_annotations_
uint32 _conAppendCode(char* buf, uint32 pos, uint32 code)
{
    buf[pos++] = ';';
    return _conAppendDec(buf, pos, code);
}

_Use_decl_annotations_
bool conWrite(ConStream* con, const void* buf, size_t sz)
{
    if (sz == 0)
        return true;

    conLock(con);
    bool ok = _conWriteLocked(con, (const uint8*)buf, sz);
    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conPuts(ConStream* con, strref s)
{
    if (strEmpty(s))
        return true;

    conLock(con);

    striter it;
    striBorrow(&it, s);
    bool ok = true;
    while (it.len > 0 && ok) {
        ok = _conWriteLocked(con, it.bytes, it.len);
        striNext(&it);
    }

    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conPutsz(ConStream* con, const char* sz)
{
    if (!sz || !*sz)
        return true;

    conLock(con);
    bool ok = _conWriteLocked(con, (const uint8*)sz, strlen(sz));
    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conPutc(ConStream* con, int32 codepoint)
{
    uint8 buf[4];
    uint32 n = _conUtf8Encode(buf, codepoint);

    conLock(con);
    bool ok = _conWriteLocked(con, buf, n);
    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conNL(ConStream* con)
{
    static const uint8 nl[] = { '\n' };
#if defined(_PLATFORM_WIN)
    static const uint8 crlf[] = { '\r', '\n' };
#endif

    conLock(con);
#if defined(_PLATFORM_WIN)
    // Mem streams are a portable capture buffer, not a real console -- they always get a
    // bare '\n' so tests get byte-identical output on every platform. Only a real Windows
    // stream needs the CRLF that WriteConsole/file-handle writes require.
    bool ok = con->kind == CON_Kind_Mem ?
        _conWriteLocked(con, nl, sizeof(nl)) :
        _conWriteLocked(con, crlf, sizeof(crlf));
#else
    bool ok = _conWriteLocked(con, nl, sizeof(nl));
#endif
    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conFlush(ConStream* con)
{
    conLock(con);
    bool ok = flushLocked(con);
    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool _conFmt(ConStream* con, strref fmt, int n, stvar* args)
{
    string tmp = 0;
    bool ok    = _strFormat(&tmp, fmt, n, args);
    ok         = conPuts(con, tmp) && ok;
    strDestroy(&tmp);
    return ok;
}
