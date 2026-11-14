#include "win32_ui.h"

#include <commctrl.h>
#include <math.h>

// A text field is an EDIT control created under cxui's comctl6 context, subclassed so keys reach
// the core before the control acts on them. Its parent (a window or a panel) passes on the
// control's change notifications and color requests. A framed field has no border, and colors
// from its frame; one without a frame takes the palette's colors in a dark window, as the system
// draws edits light there. Before Vista's comctl32 its cue is drawn here: XP's clips the cue's
// first letter.
//
// A text area is a multi-line EDIT, whose lines end in \r\n: text going to it has its \n made
// \r\n, text coming from it the reverse, and positions skip each \r.

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#define EM_GETCUEBANNER 0x1502
#endif

typedef struct TextColors {
    COLORREF text, bg, cue;
    HBRUSH brush;   // NULL for none
} TextColors;

typedef struct Win32Text {
    CxUiPeer base;
    HWND hwnd;
    WNDPROC oldProc;
    CxUiFont* font;     // a reference, so its HFONT outlives the control's use of it
    WCHAR* cue;         // NULL for none
    bool nativeCue;     // the control shows the cue itself
    bool framed;
    bool sized;         // has had a width: before that, keeping the caret in view scrolls wrongly
    bool eatChar;       // the core took the key down whose character follows
    bool focusable;
    bool area;
    bool toEnd;         // an area to scroll to its end once it has a size
    bool dark;          // in a dark window
    TextColors own;     // a framed field's
    TextColors darkColors;
} Win32Text;

static const WCHAR propName[] = L"CxUiText";

static Win32Text* textOf(HWND hwnd)
{
    return hwnd ? (Win32Text*)GetPropW(hwnd, propName) : NULL;
}

// The colors the field shows, or NULL for the system's
static TextColors* shownColors(Win32Text* t)
{
    if (t->own.brush)
        return &t->own;
    return t->dark && t->darkColors.brush ? &t->darkColors : NULL;
}

// The control's text, as UTF-16 owned by the caller
static WCHAR* wideText(HWND hwnd, int32* n)
{
    *n         = GetWindowTextLengthW(hwnd);
    WCHAR* buf = xaAlloc((*n + 1) * sizeof(WCHAR));
    *n         = GetWindowTextW(hwnd, buf, *n + 1);
    buf[*n]    = 0;
    return buf;
}

// A \r before \n, which an area's text has at each line end and the model doesn't
static bool lineCr(const WCHAR* s, int32 n, int32 i)
{
    return s[i] == '\r' && i + 1 < n && s[i + 1] == '\n';
}

// UTF-16 units to code points, and back, within the control's text
static int32 unitsToCodepoints(const WCHAR* s, int32 n, int32 units)
{
    int32 cps = 0;
    for (int32 i = 0; i < units && i < n; i++) {
        if (!(s[i] >= 0xDC00 && s[i] < 0xE000) && !lineCr(s, n, i))
            cps++;
    }
    return cps;
}

static int32 codepointsToUnits(const WCHAR* s, int32 n, int32 cps)
{
    int32 i = 0;
    for (; i < n && cps > 0; i++) {
        if (lineCr(s, n, i))
            i++;
        else if (s[i] >= 0xD800 && s[i] < 0xDC00 && i + 1 < n)
            i++;
        cps--;
    }
    return i;
}

// The model's text as the control takes it: an area's lines end in \r\n
static uint16* toNative(Win32Text* t, strref text)
{
    if (!t->area)
        return strToUTF16A(text);
    string crlf = 0;
    strReplace(&crlf, text, _S"\n", _S"\r\n", 0);
    uint16* w = strToUTF16A(crlf);
    strDestroy(&crlf);
    return w;
}

static void fromNative(string* out, const WCHAR* w, int32 n)
{
    strFromUTF16(out, (const uint16*)w, n);
    if (strFind(*out, 0, _S"\r") >= 0)
        strReplace(out, *out, _S"\r\n", _S"\n", 0);
}

static void reportChange(Win32Text* t)
{
    int32 n;
    WCHAR* w    = wideText(t->hwnd, &n);
    string text = 0;
    fromNative(&text, w, n);
    xaFree(w);
    _cxuiTextChanged(&t->base, text);
    strDestroy(&text);
}

// Before Vista's comctl32, the cue is drawn over an empty field that doesn't have focus.
static void paintCue(Win32Text* t, HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc, fmt;
    GetClientRect(hwnd, &rc);
    HBRUSH br = (HBRUSH)SendMessageW(GetParent(hwnd), (GetWindowLongW(hwnd, GWL_STYLE) & ES_READONLY) ? WM_CTLCOLORSTATIC : WM_CTLCOLOREDIT,
                                     (WPARAM)dc, (LPARAM)hwnd);
    FillRect(dc, &rc, br ? br : GetSysColorBrush(COLOR_WINDOW));
    SendMessageW(hwnd, EM_GETRECT, 0, (LPARAM)&fmt);
    HGDIOBJ old = t->font ? SelectObject(dc, _cxuiWin32GdiFont(t->font, NULL)) : NULL;
    SetBkMode(dc, TRANSPARENT);
    TextColors* tc = shownColors(t);
    SetTextColor(dc, tc ? tc->cue : GetSysColor(COLOR_GRAYTEXT));
    DrawTextW(dc, t->cue, -1, &fmt, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (old)
        SelectObject(dc, old);
    EndPaint(hwnd, &ps);
}

static bool showsDrawnCue(Win32Text* t, HWND hwnd)
{
    return t->cue && !t->nativeCue && GetWindowTextLengthW(hwnd) == 0 && GetFocus() != hwnd;
}

static LRESULT textMessage(Win32Text* t, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        if (down)
            t->eatChar = false;
        if (_cxuiWin32Key(&t->base, msg, wp, lp)) {
            // The character TranslateMessage made of it follows.
            if (down)
                t->eatChar = true;
            return 0;
        }
        // Select all, which an edit before Windows 10 1809 (and Wine's) doesn't do itself
        if (msg == WM_KEYDOWN && wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) &&
            !(GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
            SendMessageW(hwnd, EM_SETSEL, 0, -1);
            t->eatChar = true;
            return 0;
        }
        break;
    }
    case WM_CHAR:
    case WM_SYSCHAR:
        if (t->eatChar) {
            t->eatChar = false;
            return 0;
        }
        // A single-line edit beeps at these; an area takes a line break, but never a tab.
        if (msg == WM_CHAR && (wp == '\t' || wp == 0x1B || (!t->area && (wp == '\r' || wp == '\n'))))
            return 0;
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        LRESULT r = CallWindowProcW(t->oldProc, hwnd, msg, wp, lp);
        if (t->cue && !t->nativeCue)
            InvalidateRect(hwnd, NULL, TRUE);
        _cxuiFocusChanged(&t->base, msg == WM_SETFOCUS);
        return r;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
        // A disabled framed field keeps its colors, but can't be clicked into.
        if (!t->focusable)
            return 0;
        break;
    case WM_SIZE:
        if (!t->sized && LOWORD(lp) > 0 && HIWORD(lp) > 0) {
            LRESULT r = CallWindowProcW(t->oldProc, hwnd, msg, wp, lp);
            t->sized  = true;
            SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
            if (t->toEnd)
                SendMessageW(hwnd, WM_VSCROLL, SB_BOTTOM, 0);
            t->toEnd = false;
            return r;
        }
        break;
    case WM_PAINT:
        if (showsDrawnCue(t, hwnd)) {
            paintCue(t, hwnd);
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        if (_cxuiWin32Wheel(msg, wp, lp))
            return 0;
        break;
    }
    return CallWindowProcW(t->oldProc, hwnd, msg, wp, lp);
}

// The whole message is inside one wrapper, so a handler that destroys the field can't free t, or
// the control, before the control's own procedure has run.
static LRESULT CALLBACK textProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Win32Text* t = textOf(hwnd);
    if (!t)
        return DefWindowProcW(hwnd, msg, wp, lp);
    _cxuiNativeEnter();
    LRESULT r = textMessage(t, hwnd, msg, wp, lp);
    _cxuiNativeLeave();
    return r;
}

_Use_decl_annotations_
bool _cxuiWin32TextParentMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result)
{
    *result      = 0;
    Win32Text* t = textOf((HWND)lp);
    if (!t)
        return false;
    // Text set from code is reported too, and the core ignores it: the field has it already.
    if (msg == WM_COMMAND) {
        if (HIWORD(wp) == EN_CHANGE) {
            _cxuiNativeEnter();
            reportChange(t);
            _cxuiNativeLeave();
        }
        return true;
    }
    // WM_CTLCOLOREDIT, or WM_CTLCOLORSTATIC for a read-only field
    TextColors* tc = shownColors(t);
    if (!tc)
        return false;
    HDC dc = (HDC)wp;
    SetTextColor(dc, tc->text);
    SetBkColor(dc, tc->bg);
    *result = (LRESULT)tc->brush;
    return true;
}

CxUiPeer* _cxuiWin32TextCreate(CxUiTextField* tf, CxUiPeer* host, bool framed)
{
    CxUiTextArea* area = objDynCast(CxUiTextArea, tf);
    Win32Text* t       = xaAllocStruct(Win32Text, XA_Zero);
    DWORD style        = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP;
    if (area)
        style |= ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL |
                 (cxuiTextAreaWrap(area) ? 0 : ES_AUTOHSCROLL | WS_HSCROLL);
    else
        style |= ES_AUTOHSCROLL;
    ULONG_PTR cookie;
    bool v6   = _cxuiWin32Comctl6Begin(&cookie);
    HWND hwnd = CreateWindowExW(framed ? 0 : WS_EX_CLIENTEDGE, L"EDIT", L"", style, 0, 0, 0, 0,
                                _cxuiWin32HostHwnd(host), NULL, GetModuleHandleW(NULL), NULL);
    if (v6)
        _cxuiWin32Comctl6End(cookie);
    if (!hwnd) {
        logFmt(Error, _SL("cxui couldn't create a text field (error ${uint})"), stvar(uint32, GetLastError()));
        xaFree(t);
        return NULL;
    }
    t->hwnd      = hwnd;
    t->framed    = framed;
    t->area      = area != NULL;
    t->focusable = true;
    t->dark      = _cxuiWin32RootDark(hwnd);
    if (t->dark && (area || !framed))
        _cxuiWin32ApplyDarkTheme(hwnd, true);
    t->base.kind = CXUI_PEER_Text;
    SetPropW(hwnd, propName, t);
    t->oldProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)textProc);
    return &t->base;
}

void _cxuiWin32TextDestroy(CxUiPeer* p)
{
    Win32Text* t = (Win32Text*)p;
    SetWindowLongPtrW(t->hwnd, GWLP_WNDPROC, (LONG_PTR)t->oldProc);
    RemovePropW(t->hwnd, propName);
    DestroyWindow(t->hwnd);
    if (t->own.brush)
        DeleteObject(t->own.brush);
    if (t->darkColors.brush)
        DeleteObject(t->darkColors.brush);
    objRelease(&t->font);
    xaFree(t->cue);
    xaFree(t);
}

_Use_decl_annotations_
void _cxuiWin32TextSetDark(HWND hwnd, bool dark)
{
    Win32Text* t = textOf(hwnd);
    if (!t)
        return;
    t->dark = dark;
    if (t->area || !t->framed)
        _cxuiWin32ApplyDarkTheme(hwnd, dark);
    InvalidateRect(hwnd, NULL, TRUE);
}

_Use_decl_annotations_
HWND _cxuiWin32TextHwnd(CxUiPeer* p)
{
    return ((Win32Text*)p)->hwnd;
}

void _cxuiWin32TextSetBounds(CxUiPeer* p, CxUiRect px)
{
    HWND hwnd = ((Win32Text*)p)->hwnd;
    _cxuiWin32MoveChild(GetParent(hwnd), hwnd, px);
}

static int32 pxRound(float32 v)
{
    return (int32)(v + 0.5f);
}

// From the GDI font the control uses: a line, plus the client edge and a little padding for a
// field of its own, as Windows' own dialogs size an edit.
CxUiSize _cxuiWin32TextMeasure(CxUiTextField* tf, CxUiFont* f, int32 columns, bool framed)
{
    unused_noeval(tf);
    TEXTMETRICW tm;
    _cxuiWin32GdiFont(f, &tm);
    float32 s  = cxuiFontScale(f);
    int32 edge = pxRound(2 * s);
    int32 w    = columns * tm.tmAveCharWidth + pxRound(2 * s);
    int32 h    = tm.tmHeight;
    if (!framed) {
        // The margins EC_USEFONTINFO gives come to about one average character.
        w += 2 * edge + tm.tmAveCharWidth;
        h += 2 * edge + pxRound(3 * s);
    }
    return (CxUiSize){ (int32)ceilf(w / s), (int32)ceilf(h / s) };
}

// Lines of the GDI font beside a scroll bar, with an edge and margins for an area of its own
CxUiSize _cxuiWin32TextAreaMeasure(CxUiTextArea* ta, CxUiFont* f, int32 columns, int32 lines, bool framed)
{
    unused_noeval(ta);
    TEXTMETRICW tm;
    _cxuiWin32GdiFont(f, &tm);
    float32 s = cxuiFontScale(f);
    int32 w   = columns * tm.tmAveCharWidth + pxRound(17 * s) + pxRound(2 * s);
    int32 h   = lines * tm.tmHeight;
    if (!framed) {
        int32 edge = pxRound(2 * s);
        w += 2 * edge + tm.tmAveCharWidth;
        h += 2 * edge + pxRound(2 * s);
    }
    return (CxUiSize){ (int32)ceilf(w / s), (int32)ceilf(h / s) };
}

void _cxuiWin32TextSetText(CxUiPeer* p, strref text)
{
    Win32Text* t = (Win32Text*)p;
    uint16* w    = toNative(t, text);
    SetWindowTextW(t->hwnd, w ? (LPCWSTR)w : L"");
    xaFree(w);
}

// At the end, with the selection put back where it was
void _cxuiWin32TextAppend(CxUiPeer* p, strref text)
{
    Win32Text* t = (Win32Text*)p;
    DWORD s = 0, e = 0;
    SendMessageW(t->hwnd, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    int32 len = GetWindowTextLengthW(t->hwnd);
    uint16* w = toNative(t, text);
    SendMessageW(t->hwnd, EM_SETSEL, len, len);
    SendMessageW(t->hwnd, EM_REPLACESEL, FALSE, (LPARAM)(w ? (LPCWSTR)w : L""));
    SendMessageW(t->hwnd, EM_SETSEL, s, e);
    xaFree(w);
}

void _cxuiWin32TextScrollToEnd(CxUiPeer* p)
{
    Win32Text* t = (Win32Text*)p;
    if (t->sized)
        SendMessageW(t->hwnd, WM_VSCROLL, SB_BOTTOM, 0);
    else
        t->toEnd = true;
}

void _cxuiWin32TextGetSelection(CxUiPeer* p, int32* start, int32* end, int32* caret)
{
    Win32Text* t = (Win32Text*)p;
    DWORD s = 0, e = 0;
    SendMessageW(t->hwnd, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    int32 n;
    WCHAR* w = wideText(t->hwnd, &n);
    *start   = unitsToCodepoints(w, n, (int32)s);
    *end     = unitsToCodepoints(w, n, (int32)e);
    xaFree(w);
    // The control doesn't say which end the caret is at.
    *caret = *end;
}

void _cxuiWin32TextSetSelection(CxUiPeer* p, int32 start, int32 end)
{
    Win32Text* t = (Win32Text*)p;
    int32 n;
    WCHAR* w = wideText(t->hwnd, &n);
    SendMessageW(t->hwnd, EM_SETSEL, codepointsToUnits(w, n, start), codepointsToUnits(w, n, end));
    if (t->sized)
        SendMessageW(t->hwnd, EM_SCROLLCARET, 0, 0);
    xaFree(w);
}

void _cxuiWin32TextSetCue(CxUiPeer* p, strref cue)
{
    Win32Text* t = (Win32Text*)p;
    xaDestroy(&t->cue);
    if (!strEmpty(cue))
        t->cue = (WCHAR*)strToUTF16A(cue);
    t->nativeCue = _cxuiWin32HasComctl610() && SendMessageW(t->hwnd, EM_SETCUEBANNER, FALSE, (LPARAM)(t->cue ? t->cue : L""));
    InvalidateRect(t->hwnd, NULL, TRUE);
}

void _cxuiWin32TextSetMaxLength(CxUiPeer* p, int32 maxLength)
{
    SendMessageW(((Win32Text*)p)->hwnd, EM_SETLIMITTEXT, maxLength, 0);
}

void _cxuiWin32TextSetFont(CxUiPeer* p, CxUiFont* f)
{
    Win32Text* t = (Win32Text*)p;
    objAcquire(f);
    SendMessageW(t->hwnd, WM_SETFONT, (WPARAM)_cxuiWin32GdiFont(f, NULL), TRUE);
    objRelease(&t->font);
    t->font = f;
    // A framed field's frame has the padding; one of its own takes margins from the font.
    if (t->framed) {
        int32 m = pxRound(cxuiFontScale(f));
        SendMessageW(t->hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(m, m));
    } else {
        SendMessageW(t->hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(EC_USEFONTINFO, EC_USEFONTINFO));
    }
}

void _cxuiWin32TextSetState(CxUiPeer* p, bool readOnly, bool enabled, bool focusable)
{
    Win32Text* t = (Win32Text*)p;
    SendMessageW(t->hwnd, EM_SETREADONLY, readOnly, 0);
    EnableWindow(t->hwnd, enabled);
    t->focusable = focusable;
    if (!focusable && GetFocus() == t->hwnd)
        SetFocus(GetAncestor(t->hwnd, GA_ROOT));
}

static COLORREF colorRef(CxUiColor c)
{
    return RGB(c.r, c.g, c.b);
}

static void setColors(Win32Text* t, TextColors* tc, const CxUiTextColors* c)
{
    if (tc->brush)
        DeleteObject(tc->brush);
    tc->brush = NULL;
    if (c) {
        tc->text  = colorRef(c->text);
        tc->bg    = colorRef(c->bg);
        tc->cue   = colorRef(c->cue);
        tc->brush = CreateSolidBrush(tc->bg);
    }
    InvalidateRect(t->hwnd, NULL, TRUE);
}

void _cxuiWin32TextSetColors(CxUiPeer* p, const CxUiTextColors* c)
{
    Win32Text* t = (Win32Text*)p;
    setColors(t, &t->own, c);
}

void _cxuiWin32TextSetDarkColors(CxUiPeer* p, const CxUiTextColors* c)
{
    Win32Text* t = (Win32Text*)p;
    setColors(t, &t->darkColors, c);
}

// ---- tests ----

static CxUiColor fromRef(COLORREF c)
{
    return cxuiRGB(GetRValue(c), GetGValue(c), GetBValue(c));
}

bool _cxuiWin32TestTextInfo(CxUiPeer* p, CxUiTestTextInfo* out)
{
    Win32Text* t = (Win32Text*)p;
    int32 n;
    WCHAR* w = wideText(t->hwnd, &n);
    fromNative(&out->text, w, n);
    xaFree(w);
    if (t->area) {
        SCROLLINFO si  = { sizeof(si), SIF_ALL };
        out->multiline = true;
        out->wrap      = !(GetWindowLongW(t->hwnd, GWL_STYLE) & ES_AUTOHSCROLL);
        out->atEnd     = !GetScrollInfo(t->hwnd, SB_VERT, &si) || si.nMax <= 0 ||
                     si.nPos + (int)si.nPage >= si.nMax;
    }
    if (t->cue)
        strFromUTF16(&out->cue, (const uint16*)t->cue, wcslen(t->cue));
    _cxuiWin32TextGetSelection(p, &out->selStart, &out->selEnd, &out->caret);
    LRESULT limit  = SendMessageW(t->hwnd, EM_GETLIMITTEXT, 0, 0);
    out->maxLength = limit >= 0x7FFFFFFE ? 0 : (int32)limit;
    out->readOnly  = (GetWindowLongW(t->hwnd, GWL_STYLE) & ES_READONLY) != 0;
    out->enabled   = IsWindowEnabled(t->hwnd) != 0;
    out->focusable = t->focusable;
    out->focused   = GetFocus() == t->hwnd;
    out->framed    = t->framed;
    out->visible   = (GetWindowLongW(t->hwnd, GWL_STYLE) & WS_VISIBLE) != 0;
    TextColors* tc = shownColors(t);
    out->hasColors = tc != NULL;
    if (tc) {
        out->textColor = fromRef(tc->text);
        out->bg        = fromRef(tc->bg);
        out->cueColor  = fromRef(tc->cue);
    }
    return true;
}

// As the control takes typing: over the selection, up to its limit, refused when read-only.
bool _cxuiWin32TestType(CxUiPeer* p, strref s)
{
    Win32Text* t = (Win32Text*)p;
    if ((GetWindowLongW(t->hwnd, GWL_STYLE) & ES_READONLY) || !IsWindowEnabled(t->hwnd))
        return true;
    uint16* w = toNative(t, s);
    SendMessageW(t->hwnd, EM_REPLACESEL, TRUE, (LPARAM)(w ? (LPCWSTR)w : L""));
    xaFree(w);
    return true;
}
