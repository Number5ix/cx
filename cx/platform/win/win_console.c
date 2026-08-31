#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "cx/console/console_private.h"
#include "cx/platform/win.h"
#include "cx/utils/compare.h"

typedef struct ConPlatWin {
    HANDLE handle;
    bool istty;          // handle is a real console, not redirected to a file/pipe
    bool haveOrigMode;
    DWORD origMode;
    bool haveOrigAttrs;   // legacy backend only: text attributes as found at stream init
    WORD origAttrs;
    bool haveSavedPos;    // legacy backend only: conSaveCursor() target -- there is no
    COORD savedPos;       // terminal-side save slot like VT's DECSC, so this module remembers
} ConPlatWin;

_Use_decl_annotations_
void _conPlatQuerySize(ConStream* con)
{
    ConPlatWin* p = (ConPlatWin*)con->plat;
    if (!p->istty)
        return;

    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(p->handle, &info)) {
        con->caps.width  = (uint16)(info.srWindow.Right - info.srWindow.Left + 1);
        con->caps.height = (uint16)(info.srWindow.Bottom - info.srWindow.Top + 1);
    }
}

_Use_decl_annotations_
void _conPlatInit(ConStream* con, ConKind kind)
{
    ConPlatWin* p = xaAllocStruct(ConPlatWin, XA_Zero);

    DWORD which;
    switch (kind) {
    case CON_Kind_Out:
        which = STD_OUTPUT_HANDLE;
        break;
    case CON_Kind_Err:
        which = STD_ERROR_HANDLE;
        break;
    case CON_Kind_In:
        which = STD_INPUT_HANDLE;
        break;
    default:
        which = STD_OUTPUT_HANDLE;
        break;
    }
    p->handle = GetStdHandle(which);

    DWORD mode = 0;
    p->istty   = p->handle != NULL && p->handle != INVALID_HANDLE_VALUE &&
              GetConsoleMode(p->handle, &mode) != 0;
    con->plat = p;

    bool vtEnabled = false;
    if (p->istty) {
        // Saved unconditionally (not just for output streams) so conShutdown() can put
        // conIn() back to its original mode too, in case conSetMode(CON_Raw) ever changed it.
        p->origMode     = mode;
        p->haveOrigMode = true;

#ifndef CX_XP_COMPAT
        if (kind != CON_Kind_In) {
            if (SetConsoleMode(p->handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
                vtEnabled = true;
            else
                SetConsoleMode(p->handle, mode);   // unsupported (pre-1511); fall back to legacy attributes
        }
#endif
    }

    // Windows consoles do not set TERM, so the console-mode probe above is the only real
    // evidence of what this host can do -- it is what an unset TERM resolves to here. A VT
    // host is conhost 1511 or newer, which takes 24-bit SGR (exactly from 1703 on, and
    // approximated against its 16-color table before that); everything else gets the legacy
    // SetConsoleTextAttribute backend, which is 16 colors and nothing more. Passing this
    // through _conDetectCaps() rather than patching caps.color afterwards is what keeps
    // NO_COLOR and an explicitly set TERM=dumb able to override it.
    _conDetectCapsAuto(&con->caps, p->istty, vtEnabled ? CON_ColorTrue : CON_Color16);

    if (p->istty) {
        con->caps.vt          = vtEnabled;
        con->caps.unicode     = true;    // output always goes through WriteConsoleW
        con->caps.cursor      = true;    // available via VT or the legacy console API either way
        con->caps.altscreen   = vtEnabled;
        con->caps.cursorquery = !vtEnabled;   // legacy GetConsoleScreenBufferInfo is exact; a VT
                                              // DSR query races with input and is not attempted

        // An explicitly set TERM can ask for more than SetConsoleTextAttribute can render
        if (!vtEnabled && con->caps.color > CON_Color16)
            con->caps.color = CON_Color16;

        if (!vtEnabled) {
            CONSOLE_SCREEN_BUFFER_INFO info;
            if (GetConsoleScreenBufferInfo(p->handle, &info)) {
                p->origAttrs     = info.wAttributes;
                p->haveOrigAttrs = true;
            }
        }
    }

    _conPlatQuerySize(con);

    con->linebuffered = p->istty;
    con->autoflush    = kind == CON_Kind_Err;
}

_Use_decl_annotations_
bool _conPlatWrite(ConStream* con, const void* buf, size_t sz)
{
    ConPlatWin* p = (ConPlatWin*)con->plat;
    if (sz == 0)
        return true;

    if (!p->istty) {
        // redirected to a file or pipe -- write raw UTF-8 bytes, no console API involved
        const uint8* bufp = (const uint8*)buf;
        while (sz > 0) {
            DWORD n       = 0;
            DWORD chunk = (DWORD)clamphigh(sz, (size_t)0x10000000u);
            if (!WriteFile(p->handle, bufp, chunk, &n, NULL))
                return false;
            if (n == 0)
                return false;
            bufp += n;
            sz -= n;
        }
        return true;
    }

    // NOTE: converting each write() call independently means a UTF-8 sequence that
    // straddles two conWrite/conPuts calls (e.g. a rope chunk boundary) will be mangled.
    // conout.c does not yet carry over trailing partial sequences across writes. Whole
    // lines and ASCII text, the overwhelming common case, are unaffected.
    int wlen = MultiByteToWideChar(CP_UTF8, 0, (const char*)buf, (int)sz, NULL, 0);
    if (wlen <= 0)
        return true;

    uint16* wbuf = (uint16*)xaAlloc((size_t)wlen * sizeof(uint16));
    MultiByteToWideChar(CP_UTF8, 0, (const char*)buf, (int)sz, (wchar_t*)wbuf, wlen);

    bool ok            = true;
    const wchar_t* wp = (const wchar_t*)wbuf;
    int remaining       = wlen;
    while (remaining > 0) {
        DWORD written = 0;
        if (!WriteConsoleW(p->handle, wp, (DWORD)remaining, &written, NULL) || written == 0) {
            ok = false;
            break;
        }
        wp += written;
        remaining -= (int)written;
    }

    xaFree(wbuf);
    return ok;
}

_Use_decl_annotations_
void _conPlatShutdown(ConStream* con)
{
    ConPlatWin* p = (ConPlatWin*)con->plat;
    if (!p)
        return;

    if (p->haveOrigMode)
        SetConsoleMode(p->handle, p->origMode);

    xaFree(p);
    con->plat = NULL;
}

// ANSI 0-7 -> Windows FOREGROUND_* bits. BACKGROUND_* is the same bit pattern shifted left 4
// (BACKGROUND_BLUE == FOREGROUND_BLUE << 4, and so on), so one table serves both.
static const uint8 kAnsiToWinBits[8] = {
    0,
    FOREGROUND_RED,
    FOREGROUND_GREEN,
    FOREGROUND_RED | FOREGROUND_GREEN,
    FOREGROUND_BLUE,
    FOREGROUND_RED | FOREGROUND_BLUE,
    FOREGROUND_GREEN | FOREGROUND_BLUE,
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
};

_Use_decl_annotations_
bool _conPlatSetStyleLegacy(ConStream* con, ConStyle style)
{
    ConPlatWin* p = (ConPlatWin*)con->plat;
    if (!p->istty)
        return true;   // redirected to a file/pipe -- nothing to color

    // style.fg/bg arrive as either CON_ColorDefault or CON_Idx(0-15); see the contract on
    // _conPlatSetStyleLegacy() in console_private.h.
    WORD attrs = p->haveOrigAttrs ? p->origAttrs
                                  : (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    WORD fg = attrs & 0x000F;
    WORD bg = attrs & 0x00F0;

    if (style.fg != CON_ColorDefault) {
        uint8 n = (uint8)style.fg;
        fg      = kAnsiToWinBits[n & 7];
        if (n & 8)
            fg |= FOREGROUND_INTENSITY;
    }
    if (style.bg != CON_ColorDefault) {
        uint8 n = (uint8)style.bg;
        bg      = (WORD)(kAnsiToWinBits[n & 7] << 4);
        if (n & 8)
            bg |= BACKGROUND_INTENSITY;
    }

    WORD result = (WORD)(fg | bg);
    if (style.attr & CON_Bold)
        result |= FOREGROUND_INTENSITY;
    // COMMON_LVB_* attributes render inconsistently across conhost versions, but this is the
    // best the legacy console API offers -- Italic/Blink/Strike have no legacy equivalent and
    // are dropped silently, per this module's general policy on unsupported attributes.
    if (style.attr & CON_Underline)
        result |= COMMON_LVB_UNDERSCORE;
    if (style.attr & CON_Reverse)
        result |= COMMON_LVB_REVERSE_VIDEO;

    return SetConsoleTextAttribute(p->handle, result) != 0;
}

// --- legacy cursor/screen backend -- used when the VT probe in _conPlatInit failed ---
//
// row/col and all COORD fields here are screen-buffer-relative (the same coordinate space
// SetConsoleCursorPosition/GetConsoleScreenBufferInfo's dwCursorPosition use), not
// window-relative like VT addressing. That's why conGetCursor() followed by conSetCursor()
// round-trips exactly even when the buffer has scrollback above the visible window -- both
// calls agree on what "row 0" means. No caller in this codebase depends on the two schemes
// matching bit-for-bit.

_Use_decl_annotations_
bool _conPlatCursorSet(ConStream* con, uint16 row, uint16 col)
{
    ConPlatWin* p = (ConPlatWin*)con->plat;
    if (!p->istty)
        return false;

    COORD pos = { (SHORT)col, (SHORT)row };
    return SetConsoleCursorPosition(p->handle, pos) != 0;
}

_Use_decl_annotations_
bool _conPlatCursorGet(ConStream* con, uint16* row, uint16* col)
{
    ConPlatWin* p = (ConPlatWin*)con->plat;
    if (!p->istty)
        return false;

    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(p->handle, &info))
        return false;

    *row = (uint16)info.dwCursorPosition.Y;
    *col = (uint16)info.dwCursorPosition.X;
    return true;
}

_Use_decl_annotations_
bool _conPlatCursorShow(ConStream* con, bool show)
{
    ConPlatWin* p = (ConPlatWin*)con->plat;
    if (!p->istty)
        return false;

    CONSOLE_CURSOR_INFO info;
    if (!GetConsoleCursorInfo(p->handle, &info))
        return false;

    info.bVisible = show;
    return SetConsoleCursorInfo(p->handle, &info) != 0;
}

_Use_decl_annotations_
bool _conPlatCursorSave(ConStream* con)
{
    ConPlatWin* p = (ConPlatWin*)con->plat;
    if (!p->istty)
        return false;

    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(p->handle, &info))
        return false;

    p->savedPos     = info.dwCursorPosition;
    p->haveSavedPos = true;
    return true;
}

_Use_decl_annotations_
bool _conPlatCursorRestore(ConStream* con)
{
    ConPlatWin* p = (ConPlatWin*)con->plat;
    if (!p->haveSavedPos)
        return false;

    return SetConsoleCursorPosition(p->handle, p->savedPos) != 0;
}

_Use_decl_annotations_
bool _conPlatEraseLine(ConStream* con, ConEraseMode mode)
{
    ConPlatWin* p = (ConPlatWin*)con->plat;
    if (!p->istty)
        return false;

    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(p->handle, &info))
        return false;

    COORD start = info.dwCursorPosition;
    DWORD count;
    switch (mode) {
    case CON_EraseToStart:
        start.X = 0;
        count   = (DWORD)info.dwCursorPosition.X + 1;
        break;
    case CON_EraseAll:
        start.X = 0;
        count   = (DWORD)info.dwSize.X;
        break;
    case CON_EraseToEnd:
    default:
        count = (DWORD)(info.dwSize.X - info.dwCursorPosition.X);
        break;
    }

    WORD attrs = p->haveOrigAttrs ? p->origAttrs : info.wAttributes;
    DWORD written;
    return FillConsoleOutputCharacterW(p->handle, L' ', count, start, &written) != 0 &&
           FillConsoleOutputAttribute(p->handle, attrs, count, start, &written) != 0;
}

_Use_decl_annotations_
bool _conPlatEraseScreen(ConStream* con, ConEraseMode mode)
{
    ConPlatWin* p = (ConPlatWin*)con->plat;
    if (!p->istty)
        return false;

    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(p->handle, &info))
        return false;

    SHORT winWidth = (SHORT)(info.srWindow.Right - info.srWindow.Left + 1);

    COORD start;
    DWORD count;
    switch (mode) {
    case CON_EraseToStart:
        start = (COORD){ info.srWindow.Left, info.srWindow.Top };
        count = (DWORD)(info.dwCursorPosition.Y - info.srWindow.Top) * (DWORD)winWidth +
                (DWORD)(info.dwCursorPosition.X - info.srWindow.Left) + 1;
        break;
    case CON_EraseAll: {
        SHORT winHeight = (SHORT)(info.srWindow.Bottom - info.srWindow.Top + 1);
        start           = (COORD){ info.srWindow.Left, info.srWindow.Top };
        count           = (DWORD)winWidth * (DWORD)winHeight;
        break;
    }
    case CON_EraseToEnd:
    default:
        start = info.dwCursorPosition;
        count = (DWORD)(info.srWindow.Bottom - info.dwCursorPosition.Y) * (DWORD)winWidth +
                (DWORD)(info.srWindow.Right - info.dwCursorPosition.X + 1);
        break;
    }

    WORD attrs = p->haveOrigAttrs ? p->origAttrs : info.wAttributes;
    DWORD written;
    return FillConsoleOutputCharacterW(p->handle, L' ', count, start, &written) != 0 &&
           FillConsoleOutputAttribute(p->handle, attrs, count, start, &written) != 0;
}

_Use_decl_annotations_
bool _conPlatScroll(ConStream* con, int16 lines)
{
    ConPlatWin* p = (ConPlatWin*)con->plat;
    if (!p->istty)
        return false;

    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(p->handle, &info))
        return false;

    SMALL_RECT rect = info.srWindow;
    COORD dest      = { rect.Left, (SHORT)(rect.Top - lines) };

    CHAR_INFO fill        = { 0 };
    fill.Char.UnicodeChar = L' ';
    fill.Attributes       = p->haveOrigAttrs ? p->origAttrs : info.wAttributes;

    return ScrollConsoleScreenBuffer(p->handle, &rect, &rect, dest, &fill) != 0;
}

// --- input ---
//
// The legacy console API already delivers structured key events (virtual-key code, Unicode
// character, and modifier state) via ReadConsoleInputW -- there is no byte-level escape
// sequence to decode here the way there is on unix, VT input mode or not.

static DWORD timeoutToMs(int64 timeoutUsec)
{
    if (timeoutUsec == timeForever)
        return INFINITE;
    int64 ms = timeoutUsec / 1000;
    return (DWORD)clamp(ms, 0, (int64)(INFINITE - 1));
}

_Use_decl_annotations_
bool _conPlatSetMode(ConStream* con, ConInputMode mode)
{
    ConPlatWin* p = (ConPlatWin*)con->plat;
    if (!p->istty)
        return false;

    DWORD cur;
    if (!GetConsoleMode(p->handle, &cur))
        return false;

    DWORD next = mode == CON_Raw ? cur & ~(DWORD)(ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT)
                                 : cur | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT;
    return SetConsoleMode(p->handle, next) != 0;
}

_Use_decl_annotations_
bool _conPlatSetEcho(ConStream* con, bool echo)
{
    ConPlatWin* p = (ConPlatWin*)con->plat;
    if (!p->istty)
        return false;

    DWORD cur;
    if (!GetConsoleMode(p->handle, &cur))
        return false;

    DWORD next = echo ? cur | ENABLE_ECHO_INPUT : cur & ~(DWORD)ENABLE_ECHO_INPUT;
    return SetConsoleMode(p->handle, next) != 0;
}

_Use_decl_annotations_
bool _conPlatInWait(ConStream* con, int64 timeout)
{
    ConPlatWin* p = (ConPlatWin*)con->plat;
    if (!p->istty)
        return false;

    return WaitForSingleObject(p->handle, timeoutToMs(timeout)) == WAIT_OBJECT_0;
}

_Use_decl_annotations_
bool _conPlatReadKey(ConStream* con, ConKeyEvent* out, int64 timeout)
{
    ConPlatWin* p = (ConPlatWin*)con->plat;
    if (!p->istty)
        return false;

    for (;;) {
        if (WaitForSingleObject(p->handle, timeoutToMs(timeout)) != WAIT_OBJECT_0)
            return false;

        INPUT_RECORD rec;
        DWORD nread = 0;
        if (!ReadConsoleInputW(p->handle, &rec, 1, &nread) || nread == 0)
            return false;

        if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            out->key  = CON_Key_Resize;
            out->ch   = 0;
            out->mods = 0;
            return true;
        }

        if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown)
            continue;   // ignore key-up and every other event kind (mouse, focus, menu)

        const KEY_EVENT_RECORD* ke = &rec.Event.KeyEvent;
        flags_t mods = 0;
        if (ke->dwControlKeyState & SHIFT_PRESSED)
            mods |= CON_Mod_Shift;
        if (ke->dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
            mods |= CON_Mod_Alt;
        if (ke->dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
            mods |= CON_Mod_Ctrl;

        ConKey key = CON_Key_None;
        switch (ke->wVirtualKeyCode) {
        case VK_RETURN: key = CON_Key_Enter; break;
        case VK_TAB: key = CON_Key_Tab; break;
        case VK_BACK: key = CON_Key_Backspace; break;
        case VK_ESCAPE: key = CON_Key_Escape; break;
        case VK_UP: key = CON_Key_Up; break;
        case VK_DOWN: key = CON_Key_Down; break;
        case VK_LEFT: key = CON_Key_Left; break;
        case VK_RIGHT: key = CON_Key_Right; break;
        case VK_HOME: key = CON_Key_Home; break;
        case VK_END: key = CON_Key_End; break;
        case VK_PRIOR: key = CON_Key_PageUp; break;
        case VK_NEXT: key = CON_Key_PageDown; break;
        case VK_INSERT: key = CON_Key_Insert; break;
        case VK_DELETE: key = CON_Key_Delete; break;
        default:
            if (ke->wVirtualKeyCode >= VK_F1 && ke->wVirtualKeyCode <= VK_F12) {
                key = (ConKey)(CON_Key_F1 + (ke->wVirtualKeyCode - VK_F1));
            } else if (ke->uChar.UnicodeChar != 0) {
                // BMP only -- surrogate pairs are not combined across events. A console
                // reading a supplementary-plane character is rare enough not to chase here.
                out->key  = CON_Key_Char;
                out->ch   = ke->uChar.UnicodeChar;
                out->mods = mods;
                return true;
            } else {
                continue;   // a bare modifier keypress (Shift alone, etc.) -- wait for more
            }
            break;
        }

        out->key  = key;
        out->ch   = 0;
        out->mods = mods;
        return true;
    }
}

_Use_decl_annotations_
bool _conPlatReadRawByte(ConStream* con, uint8* out)
{
    ConPlatWin* p = (ConPlatWin*)con->plat;
    DWORD nread = 0;
    // ReadFile works for a redirected file/pipe handle the same way it would for any other
    // handle -- this path is only ever reached when p->istty is false (see conin.c).
    return ReadFile(p->handle, out, 1, &nread, NULL) && nread == 1;
}
