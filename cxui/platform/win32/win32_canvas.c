#include "win32_ui.h"

// A canvas is a child window that never erases. WM_PAINT paints what the core has marked dirty
// into the back buffer, then copies the area Windows asked for, so uncovering a canvas copies
// from the buffer without running the paint handler.

typedef struct Win32Canvas {
    CxUiCanvasPeer cp;
    HWND hwnd;
    uint32 cursor;   // CxUiCursors
    bool tracking;   // TrackMouseEvent will report the pointer leaving
    bool captured;   // the core asked for capture and Windows hasn't taken it away
} Win32Canvas;

static const WCHAR canvasClass[] = L"CxUiCanvas";
// A render view's: a DC of its own, which a GL renderer needs
static const WCHAR renderClass[] = L"CxUiRenderView";

// A render view is never painted here: its handler presents a frame, which a live resize needs
// done at once, and the area is then valid.
static void renderPaint(Win32Canvas* c, HWND hwnd)
{
    _cxuiNativeEnter();
    _cxuiLayoutNow(&c->cp.base);
    _cxuiCanvasPaint(&c->cp, NULL);
    ValidateRect(hwnd, NULL);
    _cxuiNativeLeave();
}

static void canvasPaint(Win32Canvas* c, HWND hwnd)
{
    if (!c->cp.ops) {
        renderPaint(c, hwnd);
        return;
    }
    // EndPaint comes before Leave, after which a handler that destroyed the canvas may have freed c.
    _cxuiNativeEnter();
    // Before BeginPaint, so a canvas that layout moves or grows paints its new area
    _cxuiLayoutNow(&c->cp.base);
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    _cxuiCanvasPaint(&c->cp, NULL);
    if (c->cp.ops == &_cxuiWin32D2dOps) {
        _cxuiWin32D2dPresent(c->cp.target, dc, &ps.rcPaint);
    } else {
        Win32BackBuffer* bb = c->cp.target;   // the GDI target starts with its buffer
        RECT r              = ps.rcPaint;
        if (bb->bmp)
            BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, bb->dc, r.left, r.top, SRCCOPY);
    }
    EndPaint(hwnd, &ps);
    _cxuiNativeLeave();
}

static HCURSOR cursorFor(uint32 cursor)
{
    static const LPCWSTR ids[CXUI_CURSOR_Count] = {
        [CXUI_CURSOR_Arrow] = IDC_ARROW,       [CXUI_CURSOR_IBeam] = IDC_IBEAM,
        [CXUI_CURSOR_Hand] = IDC_HAND,         [CXUI_CURSOR_Wait] = IDC_WAIT,
        [CXUI_CURSOR_Cross] = IDC_CROSS,       [CXUI_CURSOR_SizeWE] = IDC_SIZEWE,
        [CXUI_CURSOR_SizeNS] = IDC_SIZENS,     [CXUI_CURSOR_SizeNWSE] = IDC_SIZENWSE,
        [CXUI_CURSOR_SizeNESW] = IDC_SIZENESW, [CXUI_CURSOR_Move] = IDC_SIZEALL,
        [CXUI_CURSOR_NotAllowed] = IDC_NO,
    };
    // Stock cursors are shared and never freed.
    return cursor < CXUI_CURSOR_Count && ids[cursor] ? LoadCursorW(NULL, ids[cursor]) : NULL;
}

// Each core call is inside _cxuiNativeEnter/Leave, after which c may be gone; nothing here
// touches c after one.
static LRESULT canvasMessage(Win32Canvas* c, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        canvasPaint(c, hwnd);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(cursorFor(c->cursor));
            return TRUE;
        }
        break;
    case WM_CAPTURECHANGED:
        if (c->captured && (HWND)lp != hwnd) {
            c->captured = false;
            _cxuiNativeEnter();
            _cxuiCanvasCaptureLost(&c->cp.base);
            _cxuiNativeLeave();
        }
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        _cxuiNativeEnter();
        _cxuiFocusChanged(&c->cp.base, msg == WM_SETFOCUS);
        _cxuiNativeLeave();
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        if (_cxuiWin32Key(&c->cp.base, msg, wp, lp))
            return 0;
        break;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        if (_cxuiWin32Wheel(msg, wp, lp))
            return 0;
        break;
    default:
        if (_cxuiWin32Pointer(&c->cp.base, hwnd, &c->tracking, msg, wp, lp))
            return msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP;
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK win32CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE) {
        Win32Canvas* nc = ((CREATESTRUCTW*)lp)->lpCreateParams;
        nc->hwnd        = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)nc);
    }

    Win32Canvas* c = (Win32Canvas*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCDESTROY)
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    else if (c)
        return canvasMessage(c, hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- panels ----

// A container's surface: it paints its background and border, and holds its children's windows.
// A scroll box's is a view with scroll bars around a content window, moved to scroll, which
// holds the children.
typedef struct Win32Panel Win32Panel;
struct Win32Panel {
    CxUiPeer base;
    HWND hwnd;
    COLORREF bg, border;
    int32 borderPx;
    Win32Panel* content;   // a scroll box's content window, painted as a panel
    CxUiSize contentPx;
    CxUiPoint pos;         // how far the content is scrolled
    bool scrollH, scrollV;
    WCHAR* title;          // a frame's, or NULL
    CxUiFont* titleFont;   // a reference, for its HFONT
    COLORREF titleColor;
    int32 titleIndent, titleGap;
};

static const WCHAR panelClass[] = L"CxUiPanel";

// A title moves the top edge down to the middle of its line, with a gap for the text.
static void panelDraw(Win32Panel* p, HWND hwnd, HDC dc, const RECT* area)
{
    SetDCBrushColor(dc, p->bg);
    FillRect(dc, area, (HBRUSH)GetStockObject(DC_BRUSH));
    RECT rc;
    GetClientRect(hwnd, &rc);
    HGDIOBJ old  = NULL;
    SIZE text    = { 0, 0 };
    int32 top    = 0;
    int32 gapEnd = 0;
    if (p->title && p->titleFont) {
        old = SelectObject(dc, _cxuiWin32GdiFont(p->titleFont, NULL));
        GetTextExtentPoint32W(dc, p->title, (int)wcslen(p->title), &text);
        top    = max((text.cy - p->borderPx) / 2, 0);
        gapEnd = p->titleIndent + 2 * p->titleGap + text.cx;
    }
    if (p->borderPx > 0) {
        int32 b       = p->borderPx;
        RECT edges[5] = { { 0, top, p->title ? p->titleIndent : rc.right, top + b },
                          { gapEnd, top, p->title ? rc.right : 0, top + b },
                          { 0, rc.bottom - b, rc.right, rc.bottom },
                          { 0, top + b, b, rc.bottom - b },
                          { rc.right - b, top + b, rc.right, rc.bottom - b } };
        SetDCBrushColor(dc, p->border);
        for (int i = 0; i < 5; i++) {
            if (edges[i].right > edges[i].left)
                FillRect(dc, &edges[i], (HBRUSH)GetStockObject(DC_BRUSH));
        }
    }
    if (old) {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, p->titleColor);
        TextOutW(dc, p->titleIndent + p->titleGap, 0, p->title, (int)wcslen(p->title));
        SelectObject(dc, old);
    }
}

static void panelPaint(Win32Panel* p, HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    panelDraw(p, hwnd, dc, &ps.rcPaint);
    EndPaint(hwnd, &ps);
}

static void scrollUpdate(Win32Panel* p);
static void scrollBar(Win32Panel* p, bool across, WORD code);
static bool scrollWheel(Win32Panel* p, bool across, int32 delta);

static LRESULT CALLBACK win32PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE) {
        Win32Panel* np = ((CREATESTRUCTW*)lp)->lpCreateParams;
        np->hwnd       = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)np);
    }
    Win32Panel* p = (Win32Panel*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (p) {
            panelPaint(p, hwnd);
            return 0;
        }
        break;
    // A themed control asks for what is behind it, through DrawThemeParentBackground.
    case WM_PRINTCLIENT:
        if (p) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            panelDraw(p, hwnd, (HDC)wp, &rc);
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        if (_cxuiWin32Wheel(msg, wp, lp))
            return 0;
        // A view that can't scroll that way lets the one around it have the wheel.
        if (p && p->content && scrollWheel(p, msg == WM_MOUSEHWHEEL, GET_WHEEL_DELTA_WPARAM(wp)))
            return 0;
        break;
    case WM_SIZE:
        if (p && p->content)
            scrollUpdate(p);
        break;
    case WM_COMMAND:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN: {
        LRESULT r;
        if (_cxuiWin32TextParentMessage(msg, wp, lp, &r) ||
            (p && _cxuiWin32ControlParentMessage(msg, wp, lp, p->bg, &r)))
            return r;
        break;
    }
    case WM_NOTIFY: {
        LRESULT r;
        if (_cxuiWin32ControlParentMessage(msg, wp, lp, p ? p->bg : 0, &r))
            return r;
        break;
    }
    case WM_HSCROLL:
    case WM_VSCROLL:
        // A scroll box's own scroll bars
        if (p && p->content && lp == 0) {
            scrollBar(p, msg == WM_HSCROLL, LOWORD(wp));
            return 0;
        }
        // fall through
    case WM_DRAWITEM: {
        LRESULT r;
        if (p && _cxuiWin32ControlParentMessage(msg, wp, lp, p->bg, &r))
            return r;
        break;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

CxUiPeer* _cxuiWin32PanelCreate(CxUiWidget* w, CxUiPeer* host)
{
    CxUiScrollBox* sb = objDynCast(CxUiScrollBox, w);
    Win32Panel* p     = xaAllocStruct(Win32Panel, XA_Zero);
    DWORD style       = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    if (sb) {
        p->scrollH = (sb->_axes & CXUI_EXPAND_H) != 0;
        p->scrollV = (sb->_axes & CXUI_EXPAND_V) != 0;
        style |= (p->scrollH ? WS_HSCROLL : 0) | (p->scrollV ? WS_VSCROLL : 0);
    }
    HWND hwnd = CreateWindowExW(0, panelClass, L"", style, 0, 0, 0, 0, _cxuiWin32HostHwnd(host), NULL,
                                GetModuleHandleW(NULL), p);
    if (!hwnd) {
        logFmt(Error, _SL("cxui couldn't create a panel (error ${uint})"), stvar(uint32, GetLastError()));
        xaFree(p);
        return NULL;
    }
    p->base.kind = CXUI_PEER_Panel;
    if (sb) {
        p->content = xaAllocStruct(Win32Panel, XA_Zero);
        if (!CreateWindowExW(0, panelClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                             hwnd, NULL, GetModuleHandleW(NULL), p->content))
            xaDestroy(&p->content);
        if (_cxuiWin32RootDark(hwnd))
            _cxuiWin32ApplyDarkTheme(hwnd, true);
    }
    return &p->base;
}

_Use_decl_annotations_
CxUiPeer* _cxuiWin32SurfaceCreate(HWND parent)
{
    Win32Panel* p = xaAllocStruct(Win32Panel, XA_Zero);
    if (!CreateWindowExW(0, panelClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                         parent, NULL, GetModuleHandleW(NULL), p)) {
        xaFree(p);
        return NULL;
    }
    p->base.kind = CXUI_PEER_Panel;
    return &p->base;
}

void _cxuiWin32PanelSetBounds(CxUiPeer* p, CxUiRect px)
{
    HWND hwnd = ((Win32Panel*)p)->hwnd;
    _cxuiWin32MoveChild(GetParent(hwnd), hwnd, px);
}

void _cxuiWin32PanelSetColors(CxUiPeer* peer, CxUiColor bg, CxUiColor border, int32 borderPx)
{
    Win32Panel* p = (Win32Panel*)peer;
    p->bg         = RGB(bg.r, bg.g, bg.b);
    p->border     = RGB(border.r, border.g, border.b);
    p->borderPx   = borderPx;
    InvalidateRect(p->hwnd, NULL, FALSE);
    if (p->content) {
        p->content->bg = p->bg;
        InvalidateRect(p->content->hwnd, NULL, FALSE);
    }
}

_Use_decl_annotations_
HWND _cxuiWin32PanelHwnd(CxUiPeer* p)
{
    return ((Win32Panel*)p)->hwnd;
}

void _cxuiWin32PanelSetTitle(CxUiPeer* peer, strref title, CxUiFont* f, CxUiColor color, int32 indent, int32 gap)
{
    Win32Panel* p = (Win32Panel*)peer;
    xaDestroy(&p->title);
    if (!strEmpty(title))
        p->title = (WCHAR*)strToUTF16A(title);
    objAcquire(f);
    objRelease(&p->titleFont);
    p->titleFont   = f;
    p->titleColor  = RGB(color.r, color.g, color.b);
    p->titleIndent = indent;
    p->titleGap    = gap;
    InvalidateRect(p->hwnd, NULL, FALSE);
}

void _cxuiWin32PanelDestroy(CxUiPeer* peer)
{
    Win32Panel* p = (Win32Panel*)peer;
    DestroyWindow(p->hwnd);
    xaFree(p->title);
    objRelease(&p->titleFont);
    xaFree(p->content);
    xaFree(p);
}

// ---- scroll boxes ----

// A line, 20 DIPs at the scale of the view's monitor
static int32 scrollLine(Win32Panel* p)
{
    return (int32)(20 * _cxuiWin32MonitorScale(MonitorFromWindow(p->hwnd, MONITOR_DEFAULTTONEAREST)) + 0.5f);
}

// Moves the content to a position, kept within it
static void scrollSet(Win32Panel* p, CxUiPoint pos)
{
    RECT rc;
    GetClientRect(p->hwnd, &rc);
    pos.x = clamp(pos.x, 0, max(p->contentPx.w - rc.right, 0));
    pos.y = clamp(pos.y, 0, max(p->contentPx.h - rc.bottom, 0));
    if (pos.x == p->pos.x && pos.y == p->pos.y)
        return;
    p->pos = pos;
    SetScrollPos(p->hwnd, SB_HORZ, pos.x, TRUE);
    SetScrollPos(p->hwnd, SB_VERT, pos.y, TRUE);
    SetWindowPos(p->content->hwnd, NULL, -pos.x, -pos.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// The scroll bars show the content against the view, and hide where it fits.
static void scrollUpdate(Win32Panel* p)
{
    for (int32 i = 0; i < 2; i++) {
        RECT rc;
        GetClientRect(p->hwnd, &rc);
        SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE };
        si.nMax       = p->scrollH ? p->contentPx.w - 1 : 0;
        si.nPage      = p->scrollH ? rc.right : 0;
        SetScrollInfo(p->hwnd, SB_HORZ, &si, TRUE);
        si.nMax  = p->scrollV ? p->contentPx.h - 1 : 0;
        si.nPage = p->scrollV ? rc.bottom : 0;
        SetScrollInfo(p->hwnd, SB_VERT, &si, TRUE);
    }
    CxUiPoint pos = p->pos;
    p->pos        = (CxUiPoint){ -1, -1 };
    scrollSet(p, pos);
}

static void scrollBar(Win32Panel* p, bool across, WORD code)
{
    SCROLLINFO si = { sizeof(si), SIF_ALL };
    GetScrollInfo(p->hwnd, across ? SB_HORZ : SB_VERT, &si);
    int32 at = si.nPos;
    switch (code) {
    case SB_LINEUP: at -= scrollLine(p); break;
    case SB_LINEDOWN: at += scrollLine(p); break;
    case SB_PAGEUP: at -= (int32)si.nPage; break;
    case SB_PAGEDOWN: at += (int32)si.nPage; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: at = si.nTrackPos; break;
    case SB_TOP: at = 0; break;
    case SB_BOTTOM: at = si.nMax; break;
    default: return;
    }
    scrollSet(p, across ? (CxUiPoint){ at, p->pos.y } : (CxUiPoint){ p->pos.x, at });
}

// Three lines a notch, as Windows scrolls by default; false where the content fits that way
static bool scrollWheel(Win32Panel* p, bool across, int32 delta)
{
    RECT rc;
    GetClientRect(p->hwnd, &rc);
    bool h = across || !p->scrollV;
    if (h ? !p->scrollH || p->contentPx.w <= rc.right : p->contentPx.h <= rc.bottom)
        return false;
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    int32 by = -MulDiv(delta, (int32)min(lines, 20u) * scrollLine(p), WHEEL_DELTA);
    if (h)
        scrollSet(p, (CxUiPoint){ p->pos.x + (across ? -by : by), p->pos.y });
    else
        scrollSet(p, (CxUiPoint){ p->pos.x, p->pos.y + by });
    return true;
}

void _cxuiWin32PanelSetContent(CxUiPeer* peer, CxUiSize px)
{
    Win32Panel* p = (Win32Panel*)peer;
    if (!p->content)
        return;
    p->contentPx = px;
    SetWindowPos(p->content->hwnd, NULL, 0, 0, px.w, px.h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    scrollUpdate(p);
}

static int32 scrollToShow(int32 pos, int32 view, int32 at, int32 len)
{
    if (at < pos)
        return at;
    if (at + len > pos + view)
        return min(at, at + len - view);
    return pos;
}

void _cxuiWin32PanelScrollTo(CxUiPeer* peer, CxUiRect px)
{
    Win32Panel* p = (Win32Panel*)peer;
    if (!p->content)
        return;
    RECT rc;
    GetClientRect(p->hwnd, &rc);
    scrollSet(p, (CxUiPoint){ scrollToShow(p->pos.x, rc.right, px.x, px.w), scrollToShow(p->pos.y, rc.bottom, px.y, px.h) });
}

// The system's scroll bar width, in DIPs
int32 _cxuiWin32ScrollBarSize(void)
{
    return (int32)(GetSystemMetrics(SM_CXVSCROLL) / _cxuiWin32SystemScale() + 0.5f);
}

bool _cxuiWin32TestScrollPos(CxUiPeer* peer, CxUiPoint* out)
{
    Win32Panel* p = (Win32Panel*)peer;
    if (peer->kind != CXUI_PEER_Panel || !p->content)
        return false;
    *out = p->pos;
    return true;
}

// The wheel turned over the middle of the view
bool _cxuiWin32TestScrollWheel(CxUiPeer* peer, int32 notches)
{
    Win32Panel* p = (Win32Panel*)peer;
    if (peer->kind != CXUI_PEER_Panel || !p->content)
        return false;
    RECT rc;
    GetClientRect(p->hwnd, &rc);
    POINT pt = { rc.right / 2, rc.bottom / 2 };
    ClientToScreen(p->hwnd, &pt);
    SendMessageW(p->hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, -notches * WHEEL_DELTA), MAKELPARAM(pt.x, pt.y));
    return true;
}

_Use_decl_annotations_
void _cxuiWin32PanelSetDark(HWND hwnd, bool dark)
{
    WCHAR cls[16];
    Win32Panel* p = GetClassNameW(hwnd, cls, 16) && lstrcmpW(cls, panelClass) == 0
                        ? (Win32Panel*)GetWindowLongPtrW(hwnd, GWLP_USERDATA)
                        : NULL;
    if (p && p->content)
        _cxuiWin32ApplyDarkTheme(hwnd, dark);
}

_Use_decl_annotations_
HWND _cxuiWin32HostHwnd(CxUiPeer* host)
{
    if (host && host->kind == CXUI_PEER_Panel && ((Win32Panel*)host)->content)
        return ((Win32Panel*)host)->content->hwnd;
    if (host && host->kind == CXUI_PEER_Tabs)
        return _cxuiWin32TabsPageHwnd(host);
    return _cxuiWin32PeerHwnd(host);
}

_Use_decl_annotations_
bool _cxuiWin32CanvasInit(HINSTANCE inst)
{
    WNDCLASSEXW pc   = { sizeof(pc) };
    pc.lpfnWndProc   = win32PanelProc;
    pc.hInstance     = inst;
    pc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    pc.lpszClassName = panelClass;
    if (!RegisterClassExW(&pc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        logFmt(Error, _SL("cxui couldn't register its panel class (error ${uint})"), stvar(uint32, GetLastError()));
        return false;
    }

    WNDCLASSEXW wc   = { sizeof(wc) };
    wc.lpfnWndProc   = win32CanvasProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = canvasClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        logFmt(Error, _SL("cxui couldn't register its canvas class (error ${uint})"), stvar(uint32, GetLastError()));
        return false;
    }
    wc.style         = CS_OWNDC;
    wc.lpszClassName = renderClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        logFmt(Error, _SL("cxui couldn't register its render view class (error ${uint})"), stvar(uint32, GetLastError()));
        return false;
    }
    return true;
}

CxUiPeer* _cxuiWin32CanvasCreate(CxUiCanvas* cv, CxUiPeer* parent)
{
    Win32Canvas* c = xaAllocStruct(Win32Canvas, XA_Zero);
    if (objDynCast(CxUiRenderView, cv)) {
        HWND hwnd = CreateWindowExW(0, renderClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0,
                                    0, 0, _cxuiWin32HostHwnd(parent), NULL, GetModuleHandleW(NULL), c);
        if (!hwnd) {
            logFmt(Error, _SL("cxui couldn't create a render view (error ${uint})"), stvar(uint32, GetLastError()));
            xaFree(c);
            return NULL;
        }
        c->cp.base.kind = CXUI_PEER_Canvas;
        return &c->cp.base;
    }
    c->cp.ops      = _cxuiWin32DrawOps(cv->_drawBackend);
    c->cp.target   = c->cp.ops->targetCreate(&c->cp);
    if (!c->cp.target && c->cp.ops != &_cxuiWin32GdiOps) {
        logStr(Warn, _SL("Direct2D couldn't make a canvas; using GDI"));
        c->cp.ops    = &_cxuiWin32GdiOps;
        c->cp.target = c->cp.ops->targetCreate(&c->cp);
    }
    if (!c->cp.target) {
        xaFree(c);
        return NULL;
    }

    HWND hwnd = CreateWindowExW(0, canvasClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                                _cxuiWin32HostHwnd(parent), NULL, GetModuleHandleW(NULL), c);
    if (!hwnd) {
        logFmt(Error, _SL("cxui couldn't create a canvas (error ${uint})"), stvar(uint32, GetLastError()));
        c->cp.ops->targetDestroy(c->cp.target);
        xaFree(c);
        return NULL;
    }
    c->cp.base.kind = CXUI_PEER_Canvas;
    return &c->cp.base;
}

void _cxuiWin32CanvasSetBounds(CxUiPeer* p, CxUiRect px)
{
    Win32Canvas* c = (Win32Canvas*)p;
    if (c->cp.ops)
        c->cp.ops->targetResize(c->cp.target, (CxUiSize){ px.w, px.h });
    _cxuiWin32MoveChild(GetParent(c->hwnd), c->hwnd, px);
}

void _cxuiWin32CanvasInvalidate(CxUiPeer* p, CxUiRect px)
{
    RECT r = { px.x, px.y, px.x + px.w, px.y + px.h };
    InvalidateRect(((Win32Canvas*)p)->hwnd, &r, FALSE);
}

void _cxuiWin32CanvasPaintNow(CxUiPeer* p)
{
    UpdateWindow(((Win32Canvas*)p)->hwnd);
}

void _cxuiWin32CanvasCapture(CxUiPeer* p, bool on)
{
    Win32Canvas* c = (Win32Canvas*)p;
    // Cleared first, so the WM_CAPTURECHANGED from our own release isn't taken for a loss
    c->captured = on;
    if (on)
        SetCapture(c->hwnd);
    else if (GetCapture() == c->hwnd)
        ReleaseCapture();
}


void _cxuiWin32CanvasSetCursor(CxUiPeer* p, uint32 cursor)
{
    Win32Canvas* c = (Win32Canvas*)p;
    c->cursor      = cursor;
    // WM_SETCURSOR only comes when the pointer moves; show it now if it's over the canvas.
    POINT pt;
    if (GetCursorPos(&pt) && WindowFromPoint(pt) == c->hwnd)
        SetCursor(cursorFor(cursor));
}

_Use_decl_annotations_
CxUiPeer* _cxuiWin32CanvasAt(POINT screen)
{
    HWND hwnd = WindowFromPoint(screen);
    if (!hwnd || (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC) != win32CanvasProc)
        return NULL;
    Win32Canvas* c = (Win32Canvas*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    return c ? &c->cp.base : NULL;
}

bool _cxuiWin32SurfaceGet(CxUiPeer* p, CxUiNativeSurface* out)
{
    Win32Canvas* c = (Win32Canvas*)p;
    if (c->cp.ops)
        return false;
    *out = (CxUiNativeSurface){ .kind = CXUI_SURFACE_Win32, .native = c->hwnd, .connection = GetModuleHandleW(NULL) };
    return true;
}

#define SURFACE_MARK RGB(255, 0, 255)

// Where the view is in its window's client area, and whether the middle of it still shows the mark
bool _cxuiWin32TestSurfaceInfo(CxUiPeer* p, CxUiTestSurfaceInfo* out)
{
    Win32Canvas* c = (Win32Canvas*)p;
    if (c->cp.ops)
        return false;
    RECT r;
    GetWindowRect(c->hwnd, &r);
    MapWindowPoints(NULL, GetAncestor(c->hwnd, GA_ROOT), (POINT*)&r, 2);
    HDC dc       = GetDC(c->hwnd);
    COLORREF mid = GetPixel(dc, (r.right - r.left) / 2, (r.bottom - r.top) / 2);
    ReleaseDC(c->hwnd, dc);
    *out = (CxUiTestSurfaceInfo){ .bounds  = { r.left, r.top, r.right - r.left, r.bottom - r.top },
                                  .shown   = IsWindowVisible(c->hwnd) != 0,
                                  .canMark = true,
                                  .marked  = mid == SURFACE_MARK };
    return true;
}

// Fills the view, as a renderer presenting a frame would, outside any paint
bool _cxuiWin32TestSurfaceMark(CxUiPeer* p)
{
    Win32Canvas* c = (Win32Canvas*)p;
    if (c->cp.ops)
        return false;
    RECT rc;
    GetClientRect(c->hwnd, &rc);
    HDC dc = GetDC(c->hwnd);
    SetDCBrushColor(dc, SURFACE_MARK);
    FillRect(dc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
    GdiFlush();
    ReleaseDC(c->hwnd, dc);
    return true;
}

_Use_decl_annotations_
HWND _cxuiWin32CanvasHwnd(CxUiPeer* p)
{
    return ((Win32Canvas*)p)->hwnd;
}

bool _cxuiWin32TestScreenOrigin(CxUiPeer* p, CxUiPoint* out)
{
    POINT pt = { 0, 0 };
    if (!ClientToScreen(((Win32Canvas*)p)->hwnd, &pt))
        return false;
    *out = (CxUiPoint){ pt.x, pt.y };
    return true;
}

bool _cxuiWin32TestExpose(CxUiPeer* p)
{
    return RedrawWindow(((Win32Canvas*)p)->hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
}

void _cxuiWin32CanvasDestroy(CxUiPeer* p)
{
    Win32Canvas* c = (Win32Canvas*)p;
    DestroyWindow(c->hwnd);
    if (c->cp.ops)
        c->cp.ops->targetDestroy(c->cp.target);
    xaFree(c);
}

static bool underWine(void)
{
    return GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != NULL;
}

// Direct2D by default, but GDI under Wine, whose Direct2D is far slower; GDI wherever Direct2D
// isn't available.
const CxUiDrawOps* _cxuiWin32DrawOps(uint32 which)
{
    if (which == CXUI_Draw_GDI || (which == CXUI_Draw_Auto && underWine()) || !_cxuiWin32D2dAvailable())
        return &_cxuiWin32GdiOps;
    return &_cxuiWin32D2dOps;
}


const CxUiDrawOps* _cxuiPlatformDrawOps(uint32 which)
{
    return _cxuiWin32DrawOps(which);
}
