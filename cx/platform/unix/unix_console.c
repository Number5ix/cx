#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "cx/console/console_private.h"

#include <cx/debug/assert.h>
#include <cx/utils/compare.h>

#include <errno.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

typedef struct ConPlatUnix {
    int fd;
    bool istty;
    bool haveOrigTermios;   // conIn() only: termios as found before the first CON_Raw switch
    struct termios origTermios;
} ConPlatUnix;

_Use_decl_annotations_
void _conPlatQuerySize(ConStream* con)
{
    ConPlatUnix* p = (ConPlatUnix*)con->plat;
    if (!p->istty)
        return;

    struct winsize ws;
    if (ioctl(p->fd, TIOCGWINSZ, &ws) == 0) {
        con->caps.width  = ws.ws_col;
        con->caps.height = ws.ws_row;
    }
}

_Use_decl_annotations_
void _conPlatInit(ConStream* con, ConKind kind)
{
    ConPlatUnix* p = xaAllocStruct(ConPlatUnix, XA_Zero);

    switch (kind) {
    case CON_Kind_Out:
        p->fd = STDOUT_FILENO;
        break;
    case CON_Kind_Err:
        p->fd = STDERR_FILENO;
        break;
    case CON_Kind_In:
        p->fd = STDIN_FILENO;
        break;
    default:
        p->fd = -1;
        break;
    }
    p->istty = p->fd >= 0 && isatty(p->fd) != 0;
    con->plat = p;

    _conDetectCapsAuto(&con->caps, p->istty);
    _conPlatQuerySize(con);

    con->linebuffered = p->istty;
    con->autoflush    = kind == CON_Kind_Err;
}

_Use_decl_annotations_
bool _conPlatWrite(ConStream* con, const void* buf, size_t sz)
{
    ConPlatUnix* p = (ConPlatUnix*)con->plat;
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
void _conPlatShutdown(ConStream* con)
{
    if (con->plat) {
        ConPlatUnix* p = (ConPlatUnix*)con->plat;
        if (p->haveOrigTermios)
            tcsetattr(p->fd, TCSANOW, &p->origTermios);
        xaFree(con->plat);
        con->plat = NULL;
    }
}

_Use_decl_annotations_
bool _conPlatSetStyleLegacy(ConStream* con, ConStyle style)
{
    (void)con;
    (void)style;
    // unreachable: concaps.c never sets caps.color != CON_ColorNone alongside caps.vt ==
    // false on unix, so constyle.c never calls this here.
    devAssertMsg(false, "unix console never falls back to the legacy attribute backend");
    return false;
}

// unreachable: concaps.c sets caps.cursor = vt outright on unix, so concursor.c never takes
// the legacy branch here -- see the contract comment in console_private.h.

_Use_decl_annotations_
bool _conPlatCursorSet(ConStream* con, uint16 row, uint16 col)
{
    (void)con;
    (void)row;
    (void)col;
    devAssertMsg(false, "unix console never falls back to the legacy cursor backend");
    return false;
}

_Use_decl_annotations_
bool _conPlatCursorGet(ConStream* con, uint16* row, uint16* col)
{
    (void)con;
    (void)row;
    (void)col;
    devAssertMsg(false, "unix console never falls back to the legacy cursor backend");
    return false;
}

_Use_decl_annotations_
bool _conPlatCursorShow(ConStream* con, bool show)
{
    (void)con;
    (void)show;
    devAssertMsg(false, "unix console never falls back to the legacy cursor backend");
    return false;
}

_Use_decl_annotations_
bool _conPlatCursorSave(ConStream* con)
{
    (void)con;
    devAssertMsg(false, "unix console never falls back to the legacy cursor backend");
    return false;
}

_Use_decl_annotations_
bool _conPlatCursorRestore(ConStream* con)
{
    (void)con;
    devAssertMsg(false, "unix console never falls back to the legacy cursor backend");
    return false;
}

_Use_decl_annotations_
bool _conPlatEraseLine(ConStream* con, ConEraseMode mode)
{
    (void)con;
    (void)mode;
    devAssertMsg(false, "unix console never falls back to the legacy cursor backend");
    return false;
}

_Use_decl_annotations_
bool _conPlatEraseScreen(ConStream* con, ConEraseMode mode)
{
    (void)con;
    (void)mode;
    devAssertMsg(false, "unix console never falls back to the legacy cursor backend");
    return false;
}

_Use_decl_annotations_
bool _conPlatScroll(ConStream* con, int16 lines)
{
    (void)con;
    (void)lines;
    devAssertMsg(false, "unix console never falls back to the legacy cursor backend");
    return false;
}

// --- input ---

// timeoutUsec == timeForever blocks indefinitely (poll's -1); otherwise converts cx's
// microsecond time units to poll()'s milliseconds, clamped to what an int can hold.
static bool waitReadable(int fd, int64 timeoutUsec)
{
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int ms = timeoutUsec == timeForever ? -1 : (int)clamp(timeoutUsec / 1000, 0, 0x7FFFFFFFLL);

    int r;
    do {
        r = poll(&pfd, 1, ms);
    } while (r < 0 && errno == EINTR);

    return r > 0 && (pfd.revents & POLLIN) != 0;
}

static ssize_t readRetry(int fd, uint8* buf, size_t sz)
{
    ssize_t n;
    do {
        n = read(fd, buf, sz);
    } while (n < 0 && errno == EINTR);
    return n;
}

_Use_decl_annotations_
bool _conPlatSetMode(ConStream* con, ConInputMode mode)
{
    ConPlatUnix* p = (ConPlatUnix*)con->plat;
    if (!p->istty)
        return false;

    if (mode == CON_Cooked)
        return p->haveOrigTermios ? tcsetattr(p->fd, TCSANOW, &p->origTermios) == 0 : true;

    if (!p->haveOrigTermios) {
        if (tcgetattr(p->fd, &p->origTermios) != 0)
            return false;
        p->haveOrigTermios = true;
    }

    // cbreak, not full raw: ISIG is left alone so Ctrl+C/Ctrl+Z/Ctrl+\ keep raising their
    // usual signals -- see conSetMode()'s doc for why this module doesn't take that over.
    // IXON is cleared so Ctrl+S/Ctrl+Q reach the caller as ordinary keys instead of freezing
    // output via software flow control.
    struct termios raw = p->origTermios;
    raw.c_lflag &= (tcflag_t)~ICANON;
    raw.c_iflag &= (tcflag_t)~IXON;
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    return tcsetattr(p->fd, TCSANOW, &raw) == 0;
}

_Use_decl_annotations_
bool _conPlatSetEcho(ConStream* con, bool echo)
{
    ConPlatUnix* p = (ConPlatUnix*)con->plat;
    if (!p->istty)
        return false;

    struct termios t;
    if (tcgetattr(p->fd, &t) != 0)
        return false;

    if (echo)
        t.c_lflag |= ECHO;
    else
        t.c_lflag &= (tcflag_t)~ECHO;
    return tcsetattr(p->fd, TCSANOW, &t) == 0;
}

_Use_decl_annotations_
bool _conPlatInWait(ConStream* con, int64 timeout)
{
    ConPlatUnix* p = (ConPlatUnix*)con->plat;
    if (!p->istty)
        return false;
    return waitReadable(p->fd, timeout);
}

// Reads one already-lead-byte-known-multibyte UTF-8 codepoint: b0 was already consumed by
// the caller, so only the continuation bytes (if any) still need reading.
static int32 readUtf8Rest(int fd, uint8 b0)
{
    uint8 seq[4] = { b0, 0, 0, 0 };
    uint32 need  = (b0 & 0xF8) == 0xF0 ? 3 : (b0 & 0xF0) == 0xE0 ? 2 : (b0 & 0xE0) == 0xC0 ? 1 : 0;
    uint32 got   = 1;

    while (need > 0) {
        ssize_t n = readRetry(fd, seq + got, 1);
        if (n <= 0)
            break;
        got++;
        need--;
    }

    int32 cp;
    _conUtf8Decode(seq, got, &cp);
    return cp;
}

static bool decodeDirectByte(int fd, uint8 b0, ConKeyEvent* out)
{
    out->mods = 0;

    if (b0 == '\r' || b0 == '\n') {
        out->key = CON_Key_Enter;
        out->ch  = 0;
    } else if (b0 == '\t') {
        out->key = CON_Key_Tab;
        out->ch  = 0;
    } else if (b0 == 0x7F || b0 == 0x08) {
        out->key = CON_Key_Backspace;
        out->ch  = 0;
    } else if (b0 >= 1 && b0 <= 26) {
        // Ctrl+<letter>: the classic control-code encoding (Ctrl+A == 0x01, and so on).
        out->key  = CON_Key_Char;
        out->ch   = 'a' + (b0 - 1);
        out->mods = CON_Mod_Ctrl;
    } else if (b0 < 0x80) {
        out->key = CON_Key_Char;
        out->ch  = b0;
    } else {
        out->key = CON_Key_Char;
        out->ch  = readUtf8Rest(fd, b0);
    }
    return true;
}

_Use_decl_annotations_
bool _conPlatReadRawByte(ConStream* con, uint8* out)
{
    ConPlatUnix* p = (ConPlatUnix*)con->plat;
    return readRetry(p->fd, out, 1) == 1;
}

_Use_decl_annotations_
bool _conPlatReadKey(ConStream* con, ConKeyEvent* out, int64 timeout)
{
    ConPlatUnix* p = (ConPlatUnix*)con->plat;
    if (!p->istty)
        return false;

    if (!waitReadable(p->fd, timeout))
        return false;

    uint8 buf[16];
    ssize_t n = readRetry(p->fd, buf, 1);
    if (n <= 0)
        return false;
    uint32 len = 1;

    if (buf[0] != 0x1B)
        return decodeDirectByte(p->fd, buf[0], out);

    // A lone ESC is ambiguous with the start of a CSI/SS3 sequence or an Alt+<char> chord;
    // a short wait for a follow-up byte resolves it the same way every terminal-aware
    // line editor does (see conSetMode()'s doc).
    while (len < sizeof(buf) && waitReadable(p->fd, timeMS(30))) {
        ssize_t n2 = readRetry(p->fd, buf + len, 1);
        if (n2 <= 0)
            break;
        len++;

        uint32 consumed;
        ConDecodeResult r = _conDecodeEscape(buf, len, out, &consumed);
        if (r == CON_Decode_Matched)
            return true;
        if (r == CON_Decode_NoMatch)
            break;
        // Incomplete -- keep reading.
    }

    // Either nothing followed within the window, or what followed wasn't a sequence this
    // module recognizes. Either way, report a bare Escape; any extra bytes already read are
    // dropped -- an unrecognized-sequence edge case, not the common path.
    out->key  = CON_Key_Escape;
    out->ch   = 0;
    out->mods = 0;
    return true;
}
