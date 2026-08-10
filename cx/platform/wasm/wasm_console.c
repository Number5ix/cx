#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "cx/console/console_private.h"

#include <cx/debug/assert.h>

#include <errno.h>
#include <unistd.h>

// wasm has no terminal: fd 0/1/2 exist under the node/emulation layer but are never a tty,
// have no queryable size, and accept no escape sequences.
typedef struct ConPlatWasm {
    int fd;
} ConPlatWasm;

_Use_decl_annotations_
void _conPlatInit(ConStream* con, ConKind kind)
{
    ConPlatWasm* p = xaAllocStruct(ConPlatWasm, XA_Zero);

    switch (kind) {
    case CON_Kind_Out:
        p->fd = 1;
        break;
    case CON_Kind_Err:
        p->fd = 2;
        break;
    case CON_Kind_In:
        p->fd = 0;
        break;
    default:
        p->fd = -1;
        break;
    }
    con->plat = p;

    _conDetectCapsAuto(&con->caps, false);

    con->linebuffered = false;
    con->autoflush    = kind == CON_Kind_Err;
}

_Use_decl_annotations_
bool _conPlatWrite(ConStream* con, const void* buf, size_t sz)
{
    ConPlatWasm* p     = (ConPlatWasm*)con->plat;
    const uint8* bufp = (const uint8*)buf;

    while (sz > 0) {
        ssize_t n = write(p->fd, bufp, sz);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        bufp += n;
        sz -= (size_t)n;
    }
    return true;
}

_Use_decl_annotations_
void _conPlatQuerySize(ConStream* con)
{
    (void)con;   // never a tty; nothing to query
}

_Use_decl_annotations_
void _conPlatShutdown(ConStream* con)
{
    if (con->plat) {
        xaFree(con->plat);
        con->plat = NULL;
    }
}

_Use_decl_annotations_
bool _conPlatSetStyleLegacy(ConStream* con, ConStyle style)
{
    (void)con;
    (void)style;
    // unreachable: wasm streams are never a tty, so caps.color is always CON_ColorNone.
    devAssertMsg(false, "wasm console never falls back to the legacy attribute backend");
    return false;
}

// unreachable: wasm streams are never a tty, so caps.cursor is always false and concursor.c
// never takes the legacy branch here -- see the contract comment in console_private.h.

_Use_decl_annotations_
bool _conPlatCursorSet(ConStream* con, uint16 row, uint16 col)
{
    (void)con;
    (void)row;
    (void)col;
    devAssertMsg(false, "wasm console never falls back to the legacy cursor backend");
    return false;
}

_Use_decl_annotations_
bool _conPlatCursorGet(ConStream* con, uint16* row, uint16* col)
{
    (void)con;
    (void)row;
    (void)col;
    devAssertMsg(false, "wasm console never falls back to the legacy cursor backend");
    return false;
}

_Use_decl_annotations_
bool _conPlatCursorShow(ConStream* con, bool show)
{
    (void)con;
    (void)show;
    devAssertMsg(false, "wasm console never falls back to the legacy cursor backend");
    return false;
}

_Use_decl_annotations_
bool _conPlatCursorSave(ConStream* con)
{
    (void)con;
    devAssertMsg(false, "wasm console never falls back to the legacy cursor backend");
    return false;
}

_Use_decl_annotations_
bool _conPlatCursorRestore(ConStream* con)
{
    (void)con;
    devAssertMsg(false, "wasm console never falls back to the legacy cursor backend");
    return false;
}

_Use_decl_annotations_
bool _conPlatEraseLine(ConStream* con, ConEraseMode mode)
{
    (void)con;
    (void)mode;
    devAssertMsg(false, "wasm console never falls back to the legacy cursor backend");
    return false;
}

_Use_decl_annotations_
bool _conPlatEraseScreen(ConStream* con, ConEraseMode mode)
{
    (void)con;
    (void)mode;
    devAssertMsg(false, "wasm console never falls back to the legacy cursor backend");
    return false;
}

_Use_decl_annotations_
bool _conPlatScroll(ConStream* con, int16 lines)
{
    (void)con;
    (void)lines;
    devAssertMsg(false, "wasm console never falls back to the legacy cursor backend");
    return false;
}

// --- input ---
//
// wasm streams are never a tty (see _conPlatInit above), so there is no real keyboard input
// to read -- unlike the legacy cursor/style backends, reaching these is not a broken
// assumption, just "no input available," so no devAssert.

_Use_decl_annotations_
bool _conPlatSetMode(ConStream* con, ConInputMode mode)
{
    (void)con;
    (void)mode;
    return false;
}

_Use_decl_annotations_
bool _conPlatSetEcho(ConStream* con, bool echo)
{
    (void)con;
    (void)echo;
    return false;
}

_Use_decl_annotations_
bool _conPlatInWait(ConStream* con, int64 timeout)
{
    (void)con;
    (void)timeout;
    return false;
}

_Use_decl_annotations_
bool _conPlatReadKey(ConStream* con, ConKeyEvent* out, int64 timeout)
{
    (void)con;
    (void)out;
    (void)timeout;
    return false;
}

_Use_decl_annotations_
bool _conPlatReadRawByte(ConStream* con, uint8* out)
{
    // Unlike the four hooks above, fd 0 can still be a real redirected file/pipe under the
    // node/emulation layer even though it is never a tty -- conin.c's fallback path reads it
    // the same way unix does.
    ConPlatWasm* p = (ConPlatWasm*)con->plat;
    ssize_t n;
    do {
        n = read(p->fd, out, 1);
    } while (n < 0 && errno == EINTR);
    return n == 1;
}
