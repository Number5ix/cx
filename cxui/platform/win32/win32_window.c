#include "win32_ui.h"

#include <commctrl.h>

#include <cx/utils/lazyinit.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

typedef struct Win32Window {
    CxUiPeer base;
    HWND hwnd;
    HICON bigIcon;
    HICON smallIcon;
    CxUiSize lastPx;
    CxUiPoint lastPos;
    uint32 lastState;
    // Smallest client size the user may drag it to, in DIPs
    CxUiSize minDips;
    // Canvas moves gathered during a layout pass
    HDWP defer;
    // Client area background, once the core has given one
    COLORREF bg;
    bool hasBg;
    bool dark;
    // Inside WM_DPICHANGED's resize, whose size is reported along with the new scale
    bool dpiChanging;
    // Inside SetWindowPlacement, whose rectangle is already in the new monitor's pixels
    bool placing;
    // The child that had focus when the window was last deactivated
    HWND lastFocus;
    // A borderless window not placed by the program, which Windows would put at the top left
    bool centerOnShow;
    // Shown modally, like a dialog: no minimize or maximize box
    bool modal;
    // The window it belongs to, which a popup's owner stands in for while it runs
    HWND owner;
    // CxUiFullscreen, and the style and placement to go back to
    uint32 fsMode;
    DWORD fsStyle;
    WINDOWPLACEMENT fsPlace;
    // The program asked for it to stay above other windows
    bool topmost;
} Win32Window;

static const WCHAR windowClass[] = L"CxUiWindow";

// ---- APIs newer than XP, and the program's icon ----

typedef UINT(WINAPI* GetDpiForWindowFn)(HWND hwnd);
typedef HANDLE(WINAPI* SetThreadDpiAwarenessContextFn)(HANDLE ctx);
typedef BOOL(WINAPI* AdjustWindowRectExForDpiFn)(LPRECT r, DWORD style, BOOL menu, DWORD exStyle, UINT dpi);
typedef HRESULT(WINAPI* GetDpiForMonitorFn)(HMONITOR mon, int type, UINT* dpiX, UINT* dpiY);
typedef HRESULT(WINAPI* DwmSetWindowAttributeFn)(HWND hwnd, DWORD attr, LPCVOID val, DWORD size);
typedef HRESULT(WINAPI* DwmGetWindowAttributeFn)(HWND hwnd, DWORD attr, PVOID val, DWORD size);

static GetDpiForWindowFn pGetDpiForWindow;
static SetThreadDpiAwarenessContextFn pSetThreadDpiAwarenessContext;
static AdjustWindowRectExForDpiFn pAdjustWindowRectExForDpi;
static GetDpiForMonitorFn pGetDpiForMonitor;
static DwmSetWindowAttributeFn pDwmSetWindowAttribute;
static DwmGetWindowAttributeFn pDwmGetWindowAttribute;

// The executable's first icon group: an integer resource id, or a name that is never freed.
static LPCWSTR appIconName;

static LazyInitState apiInit;

// DWMWA_USE_IMMERSIVE_DARK_MODE, and the value it had before Windows 10 20H1
#define CXUI_DWMWA_DARK     20
#define CXUI_DWMWA_DARK_OLD 19
// DWMWA_EXTENDED_FRAME_BOUNDS
#define CXUI_DWMWA_FRAME 9

static BOOL CALLBACK findAppIcon(HMODULE mod, LPCWSTR type, LPWSTR name, LONG_PTR param)
{
    unused_noeval(mod);
    unused_noeval(type);
    unused_noeval(param);
    if (IS_INTRESOURCE(name)) {
        appIconName = name;
    } else {
        size_t bytes = (wcslen(name) + 1) * sizeof(WCHAR);
        WCHAR* copy  = xaAlloc(bytes);
        memcpy(copy, name, bytes);
        appIconName = copy;
    }
    return FALSE;   // the first is enough
}

static void apiInitOnce(void* unused)
{
    unused_noeval(unused);
    HMODULE user32   = GetModuleHandleW(L"user32.dll");
    pGetDpiForWindow = (GetDpiForWindowFn)GetProcAddress(user32, "GetDpiForWindow");
    pSetThreadDpiAwarenessContext =
        (SetThreadDpiAwarenessContextFn)GetProcAddress(user32, "SetThreadDpiAwarenessContext");
    pAdjustWindowRectExForDpi = (AdjustWindowRectExForDpiFn)GetProcAddress(user32, "AdjustWindowRectExForDpi");
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore)
        pGetDpiForMonitor = (GetDpiForMonitorFn)GetProcAddress(shcore, "GetDpiForMonitor");

    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm) {
        pDwmSetWindowAttribute = (DwmSetWindowAttributeFn)GetProcAddress(dwm, "DwmSetWindowAttribute");
        pDwmGetWindowAttribute = (DwmGetWindowAttributeFn)GetProcAddress(dwm, "DwmGetWindowAttribute");
    }

    EnumResourceNamesW(GetModuleHandleW(NULL), RT_GROUP_ICON, findAppIcon, 0);
}

static UINT win32Dpi(HWND hwnd)
{
    UINT dpi = pGetDpiForWindow ? pGetDpiForWindow(hwnd) : 0;
    if (dpi == 0) {
        HDC dc = GetDC(NULL);
        dpi    = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(NULL, dc);
    }
    return dpi ? dpi : 96;
}

float32 _cxuiWin32MonitorScale(HMONITOR mon)
{
    UINT dpi = 0, unused = 0;
    // MDT_EFFECTIVE_DPI
    if (!pGetDpiForMonitor || FAILED(pGetDpiForMonitor(mon, 0, &dpi, &unused)) || dpi == 0)
        dpi = win32Dpi(NULL);
    return dpi / 96.0f;
}

float32 _cxuiWin32SystemScale(void)
{
    UINT dpi = 0, unused = 0;
    // MDT_EFFECTIVE_DPI of the main monitor
    if (!pGetDpiForMonitor ||
        FAILED(pGetDpiForMonitor(MonitorFromPoint((POINT){ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), 0, &dpi, &unused)))
        dpi = win32Dpi(NULL);
    return dpi / 96.0f;
}

static CxUiSize pxToDips(HWND hwnd, CxUiSize px)
{
    UINT dpi = win32Dpi(hwnd);
    return (CxUiSize){ MulDiv(px.w, 96, dpi), MulDiv(px.h, 96, dpi) };
}

// The frame size around a client area of dips at the window's DPI
static SIZE frameFor(HWND hwnd, CxUiSize dips)
{
    UINT dpi = win32Dpi(hwnd);
    RECT r   = { 0, 0, MulDiv(dips.w, dpi, 96), MulDiv(dips.h, dpi, 96) };
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE), exStyle = (DWORD)GetWindowLongW(hwnd, GWL_EXSTYLE);
    BOOL menu = GetMenu(hwnd) != NULL;
    if (!pAdjustWindowRectExForDpi || !pAdjustWindowRectExForDpi(&r, style, menu, exStyle, dpi))
        AdjustWindowRectEx(&r, style, menu, exStyle);
    return (SIZE){ r.right - r.left, r.bottom - r.top };
}

CxUiColor _cxuiWin32SystemPanelColor(void)
{
    COLORREF c = GetSysColor(COLOR_3DFACE);
    return cxuiRGB(GetRValue(c), GetGValue(c), GetBValue(c));
}

// The color WM_ERASEBKGND paints the client area
static COLORREF clientBg(Win32Window* p)
{
    return p->hasBg ? p->bg : p->dark ? RGB(32, 32, 32) : GetSysColor(COLOR_3DFACE);
}

_Use_decl_annotations_
void _cxuiWin32RememberFocus(HWND child)
{
    HWND root      = GetAncestor(child, GA_ROOT);
    Win32Window* p = root && root != child ? (Win32Window*)GetWindowLongPtrW(root, GWLP_USERDATA) : NULL;
    if (p)
        p->lastFocus = child;
}

_Use_decl_annotations_
bool _cxuiWin32RootDark(HWND hwnd)
{
    HWND root      = GetAncestor(hwnd, GA_ROOT);
    Win32Window* p = root ? (Win32Window*)GetWindowLongPtrW(root, GWLP_USERDATA) : NULL;
    return p && p->dark;
}

static BOOL CALLBACK childSetDark(HWND hwnd, LPARAM dark)
{
    _cxuiWin32ControlSetDark(hwnd, dark != 0);
    _cxuiWin32TextSetDark(hwnd, dark != 0);
    _cxuiWin32PanelSetDark(hwnd, dark != 0);
    return TRUE;
}

static void win32ApplyDark(HWND hwnd, bool dark)
{
    if (!pDwmSetWindowAttribute)
        return;
    BOOL val = dark;
    if (FAILED(pDwmSetWindowAttribute(hwnd, CXUI_DWMWA_DARK, &val, sizeof(val))))
        pDwmSetWindowAttribute(hwnd, CXUI_DWMWA_DARK_OLD, &val, sizeof(val));
}

static void win32SetIcons(Win32Window* p)
{
    if (!appIconName)
        return;
    HINSTANCE inst = GetModuleHandleW(NULL);
    p->bigIcon     = (HICON)LoadImageW(inst, appIconName, IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                       GetSystemMetrics(SM_CYICON), 0);
    p->smallIcon   = (HICON)LoadImageW(inst, appIconName, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                       GetSystemMetrics(SM_CYSMICON), 0);
    if (p->bigIcon)
        SendMessageW(p->hwnd, WM_SETICON, ICON_BIG, (LPARAM)p->bigIcon);
    if (p->smallIcon)
        SendMessageW(p->hwnd, WM_SETICON, ICON_SMALL, (LPARAM)p->smallIcon);
}

CxUiThemeMode _cxuiWin32SystemTheme(void)
{
    // Windows before 10 1809 has no such setting, and is light.
    DWORD light = 1, size = sizeof(light), type = 0;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                      KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, L"AppsUseLightTheme", NULL, &type, (LPBYTE)&light, &size) != ERROR_SUCCESS ||
            type != REG_DWORD)
            light = 1;
        RegCloseKey(key);
    }
    return light ? CXUI_Theme_Light : CXUI_Theme_Dark;
}

// ---- window procedure ----

// Each core call is wrapped so a handler can't free p while this still uses it, and nothing
// touches p after the wrapper ends.
static LRESULT win32WindowMessage(Win32Window* p, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT menuResult;
    if (_cxuiWin32MenuMessage(hwnd, p->dark, clientBg(p), msg, wp, lp, &menuResult))
        return menuResult;

    switch (msg) {
    case WM_CLOSE:
        _cxuiNativeEnter();
        _cxuiWindowCloseRequested(&p->base, CXUI_CLOSE_User);
        _cxuiNativeLeave();
        return 0;

    case WM_SIZE: {
        if (wp == SIZE_MAXSHOW || wp == SIZE_MAXHIDE)
            break;
        uint32 state = wp == SIZE_MINIMIZED   ? CXUI_WINDOW_Minimized
                       : p->fsMode            ? CXUI_WINDOW_Fullscreen
                       : wp == SIZE_MAXIMIZED ? CXUI_WINDOW_Maximized
                                              : CXUI_WINDOW_Normal;
        CxUiSize px  = { LOWORD(lp), HIWORD(lp) };
        if (p->dpiChanging)
            return 0;
        bool stateChanged = state != p->lastState;
        bool sizeChanged  = state != CXUI_WINDOW_Minimized && (px.w != p->lastPx.w || px.h != p->lastPx.h);
        p->lastState      = state;
        if (sizeChanged)
            p->lastPx = px;

        _cxuiNativeEnter();
        if (stateChanged)
            _cxuiWindowStateChanged(&p->base, state);
        if (sizeChanged)
            _cxuiWindowResized(&p->base, px, pxToDips(hwnd, px));
        _cxuiNativeLeave();
        return 0;
    }

    case WM_MOVE: {
        RECT r;
        if (IsIconic(hwnd) || !GetWindowRect(hwnd, &r) || (r.left == p->lastPos.x && r.top == p->lastPos.y))
            return 0;
        p->lastPos = (CxUiPoint){ r.left, r.top };
        _cxuiNativeEnter();
        _cxuiWindowMoved(&p->base, p->lastPos);
        _cxuiNativeLeave();
        return 0;
    }

    case WM_ACTIVATE: {
        // Windows gives the window itself focus when it is activated; the child that had it gets
        // it back instead, as in a dialog.
        HWND focus  = GetFocus();
        if (LOWORD(wp) == WA_INACTIVE && focus && IsChild(hwnd, focus))
            p->lastFocus = focus;
        LRESULT ret = DefWindowProcW(hwnd, msg, wp, lp);
        // A Monitor window stays above everything, the taskbar included, only while active.
        if (p->fsMode == CXUI_FULLSCREEN_Monitor && !p->topmost)
            SetWindowPos(hwnd, LOWORD(wp) != WA_INACTIVE ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        HWND back   = p->lastFocus;
        if (LOWORD(wp) != WA_INACTIVE && !HIWORD(wp) && back && IsWindow(back) && IsChild(hwnd, back) &&
            IsWindowVisible(back) && IsWindowEnabled(back))
            SetFocus(back);
        _cxuiNativeEnter();
        _cxuiWindowActivated(&p->base, LOWORD(wp) != WA_INACTIVE);
        _cxuiNativeLeave();
        return ret;
    }

    // With no canvas focused, keys come to the window itself.
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        if (_cxuiWin32Key(&p->base, msg, wp, lp))
            return 0;
        break;

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        if (_cxuiWin32Wheel(msg, wp, lp))
            return 0;
        break;

    case WM_MENUCOMMAND:
        _cxuiWin32MenuCommand((HMENU)lp, (int32)wp);
        return 0;

    case WM_COMMAND:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN: {
        LRESULT r;
        if (_cxuiWin32TextParentMessage(msg, wp, lp, &r) ||
            _cxuiWin32ControlParentMessage(msg, wp, lp, clientBg(p), &r))
            return r;
        break;
    }

    case WM_HSCROLL:
    case WM_VSCROLL:
    case WM_DRAWITEM:
    case WM_NOTIFY: {
        LRESULT r;
        if (_cxuiWin32ControlParentMessage(msg, wp, lp, clientBg(p), &r))
            return r;
        break;
    }

    case WM_DPICHANGED: {
        // Windows suggests a rectangle that keeps the size in DIPs; a placement being applied
        // already has the size it wants.
        const RECT* r  = (const RECT*)lp;
        p->dpiChanging = true;
        if (!p->placing && !p->fsMode)
            SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        p->dpiChanging = false;
        RECT rc;
        GetClientRect(hwnd, &rc);
        p->lastPx = (CxUiSize){ rc.right, rc.bottom };
        _cxuiNativeEnter();
        _cxuiWindowDpiChanged(&p->base, LOWORD(wp) / 96.0f, p->lastPx);
        _cxuiNativeLeave();
        return 0;
    }

    case WM_DISPLAYCHANGE:
        _cxuiWin32DisplayChanged();
        break;

    // Sent to every top-level window, so each window that follows the system hears it.
    case WM_SETTINGCHANGE:
        if (wp == SPI_SETWORKAREA)
            _cxuiWin32DisplayChanged();
        if (lp && lstrcmpW((LPCWSTR)lp, L"ImmersiveColorSet") == 0) {
            _cxuiNativeEnter();
            _cxuiSystemThemeChanged();
            _cxuiNativeLeave();
        }
        break;

    case WM_GETMINMAXINFO: {
        if (p->minDips.w > 0 || p->minDips.h > 0) {
            MINMAXINFO* mm      = (MINMAXINFO*)lp;
            SIZE f              = frameFor(hwnd, p->minDips);
            mm->ptMinTrackSize.x = max(mm->ptMinTrackSize.x, f.cx);
            mm->ptMinTrackSize.y = max(mm->ptMinTrackSize.y, f.cy);
        }
        return 0;
    }

    case WM_ERASEBKGND: {
        HDC dc = (HDC)wp;
        RECT rc;
        GetClientRect(hwnd, &rc);
        SetDCBrushColor(dc, clientBg(p));
        FillRect(dc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
        return 1;
    }

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK win32WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE) {
        Win32Window* np = ((CREATESTRUCTW*)lp)->lpCreateParams;
        np->hwnd        = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)np);
    }

    Win32Window* p = (Win32Window*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    return p ? win32WindowMessage(p, hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- operations ----

bool _cxuiWin32WindowInit(HINSTANCE inst)
{
    lazyInit(&apiInit, apiInitOnce, NULL);
    // Per thread, so it works whatever the program's manifest says; every window cxui creates on
    // this thread is then per-monitor aware. Needs Windows 10 1703; before it, cxui uses the
    // process's awareness.
    if (pSetThreadDpiAwarenessContext)
        pSetThreadDpiAwarenessContext(CXUI_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW wc   = { sizeof(wc) };
    wc.lpfnWndProc   = win32WindowProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = windowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        logFmt(Error, _SL("cxui couldn't register its window class (error ${uint})"), stvar(uint32, GetLastError()));
        return false;
    }
    return true;
}

static void win32SetClientSize(HWND hwnd, CxUiSize dips, UINT extraFlags)
{
    SIZE f = frameFor(hwnd, dips);
    SetWindowPos(hwnd, NULL, 0, 0, f.cx, f.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | extraFlags);
}

static DWORD styleFor(bool resizable)
{
    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    return resizable ? style : style & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
}

// A popup with the system menu and minimize, and a taskbar button, which popups don't get by
// default
static const DWORD borderlessStyle = WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;

CxUiPeer* _cxuiWin32WindowCreate(CxUiWindow* w)
{
    Win32Window* p = xaAllocStruct(Win32Window, XA_Zero);
    p->dark        = w->_dark;

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (w->_hasPos) {
        x = w->_pos.x;
        y = w->_pos.y;
    }

    bool borderless = (w->_flags & CXUI_WINDOW_Borderless) != 0;
    DWORD style     = borderless ? borderlessStyle : styleFor(_cxuiWindowUserResizable(w));
    p->centerOnShow = borderless && !w->_hasPos;
    uint16* title   = strToUTF16A(w->_title);
    HWND hwnd = CreateWindowExW(borderless ? WS_EX_APPWINDOW : 0, windowClass, title ? (LPCWSTR)title : L"", style, x, y,
                                CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, GetModuleHandleW(NULL), p);
    xaFree(title);
    if (!hwnd) {
        logFmt(Error, _SL("cxui couldn't create a window (error ${uint})"), stvar(uint32, GetLastError()));
        xaFree(p);
        return NULL;
    }

    // Before the first show, so the frame is never drawn in the wrong mode
    win32ApplyDark(hwnd, p->dark);
    win32SetIcons(p);
    // Either dimension not given keeps the default size Windows chose.
    RECT rc;
    GetClientRect(hwnd, &rc);
    CxUiSize def = pxToDips(hwnd, (CxUiSize){ rc.right, rc.bottom });
    if (w->_size.w > 0 || w->_size.h > 0) {
        w->_size = (CxUiSize){ w->_size.w > 0 ? w->_size.w : def.w, w->_size.h > 0 ? w->_size.h : def.h };
        win32SetClientSize(hwnd, w->_size, 0);
    } else {
        w->_size = def;
    }

    GetClientRect(hwnd, &rc);
    p->lastPx = (CxUiSize){ rc.right, rc.bottom };
    GetWindowRect(hwnd, &rc);
    p->lastPos = (CxUiPoint){ rc.left, rc.top };
    w->_scale    = win32Dpi(hwnd) / 96.0f;
    w->_clientPx = p->lastPx;
    p->base.kind = CXUI_PEER_Window;
    return &p->base;
}

// Centered on the work area of the monitor the mouse is on, as Windows places a new window there
static void centerOnMonitor(HWND hwnd)
{
    POINT pt;
    GetCursorPos(&pt);
    MONITORINFO mi = { sizeof(mi) };
    RECT rc;
    if (!GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi) || !GetWindowRect(hwnd, &rc))
        return;
    int32 w = rc.right - rc.left, h = rc.bottom - rc.top;
    SetWindowPos(hwnd, NULL, (mi.rcWork.left + mi.rcWork.right - w) / 2, (mi.rcWork.top + mi.rcWork.bottom - h) / 2, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void _cxuiWin32WindowShow(CxUiPeer* p, uint32 mode)
{
    Win32Window* win = (Win32Window*)p;
    if (win->centerOnShow) {
        win->centerOnShow = false;
        centerOnMonitor(win->hwnd);
    }
    static const int cmds[] = {
        [CXUI_SHOW_Normal]     = SW_SHOWNORMAL,
        [CXUI_SHOW_NoActivate] = SW_SHOWNOACTIVATE,
        [CXUI_SHOW_Minimized]  = SW_SHOWMINNOACTIVE,
        [CXUI_SHOW_Maximized]  = SW_SHOWMAXIMIZED,
    };
    ShowWindow(((Win32Window*)p)->hwnd, mode < sizeof(cmds) / sizeof(cmds[0]) ? cmds[mode] : SW_SHOWNORMAL);
}

void _cxuiWin32WindowHide(CxUiPeer* p)
{
    ShowWindow(((Win32Window*)p)->hwnd, SW_HIDE);
}

void _cxuiWin32WindowSetTitle(CxUiPeer* p, strref title)
{
    uint16* wide = strToUTF16A(title);
    SetWindowTextW(((Win32Window*)p)->hwnd, wide ? (LPCWSTR)wide : L"");
    xaFree(wide);
}

void _cxuiWin32WindowSetSize(CxUiPeer* p, CxUiSize dips)
{
    win32SetClientSize(((Win32Window*)p)->hwnd, dips, 0);
}

void _cxuiWin32WindowSetMinSize(CxUiPeer* p, CxUiSize dips)
{
    ((Win32Window*)p)->minDips = dips;
}

// The frame grows or shrinks around the bar; the client area keeps its size.
void _cxuiWin32WindowSetMenu(CxUiPeer* peer, CxUiPeer* menu)
{
    Win32Window* p = (Win32Window*)peer;
    // Taken first: SetMenu resizes the client area at once.
    RECT rc;
    GetClientRect(p->hwnd, &rc);
    _cxuiWin32WindowAttachMenu(p->hwnd, menu);
    if (!IsZoomed(p->hwnd) && !IsIconic(p->hwnd))
        win32SetClientSize(p->hwnd, pxToDips(p->hwnd, (CxUiSize){ rc.right, rc.bottom }), SWP_FRAMECHANGED);
}

void _cxuiWin32WindowSetResizable(CxUiPeer* peer, bool resizable)
{
    Win32Window* p = (Win32Window*)peer;
    DWORD style    = (DWORD)GetWindowLongW(p->hwnd, GWL_STYLE);
    DWORD frame    = styleFor(true) & ~styleFor(false) & ~(p->modal ? WS_MAXIMIZEBOX : 0);
    SetWindowLongW(p->hwnd, GWL_STYLE, resizable ? style | frame : style & ~frame);
    // The frame's width changes; the client area keeps its size.
    win32SetClientSize(p->hwnd, pxToDips(p->hwnd, p->lastPx), SWP_FRAMECHANGED);
}

void _cxuiWin32WindowLayoutBegin(CxUiPeer* p)
{
    Win32Window* w = (Win32Window*)p;
    if (!w->defer)
        w->defer = BeginDeferWindowPos(8);
}

void _cxuiWin32WindowLayoutEnd(CxUiPeer* p)
{
    Win32Window* w = (Win32Window*)p;
    HDWP defer     = w->defer;
    w->defer       = NULL;
    if (defer)
        EndDeferWindowPos(defer);
}

void _cxuiWin32WindowSetBackground(CxUiPeer* peer, CxUiColor c)
{
    Win32Window* p = (Win32Window*)peer;
    COLORREF bg    = RGB(c.r, c.g, c.b);
    bool changed   = !p->hasBg || p->bg != bg;
    p->bg          = bg;
    p->hasBg       = true;
    // A dark menu bar is filled with this color.
    if (changed)
        _cxuiWin32MenuBarRedraw(p->hwnd);
    InvalidateRect(p->hwnd, NULL, TRUE);
}

// A batch holds windows with the same parent only, so moves inside panels go straight through.
_Use_decl_annotations_
void _cxuiWin32MoveChild(HWND parent, HWND child, CxUiRect px)
{
    HWND root      = parent ? GetAncestor(parent, GA_ROOT) : NULL;
    Win32Window* w = root == parent && root ? (Win32Window*)GetWindowLongPtrW(root, GWLP_USERDATA) : NULL;
    if (w && w->defer) {
        // On failure Windows has already freed the batch.
        w->defer = DeferWindowPos(w->defer, child, NULL, px.x, px.y, px.w, px.h, SWP_NOZORDER | SWP_NOACTIVATE);
        if (w->defer)
            return;
    }
    SetWindowPos(child, NULL, px.x, px.y, px.w, px.h, SWP_NOZORDER | SWP_NOACTIVATE);
}

bool _cxuiWin32WindowGetPosition(CxUiPeer* peer, CxUiPoint* out)
{
    Win32Window* p = (Win32Window*)peer;
    RECT r;
    if (!IsIconic(p->hwnd) && GetWindowRect(p->hwnd, &r))
        p->lastPos = (CxUiPoint){ r.left, r.top };
    *out = p->lastPos;
    return true;
}

bool _cxuiWin32WindowSetPosition(CxUiPeer* p, CxUiPoint pos)
{
    return SetWindowPos(((Win32Window*)p)->hwnd, NULL, pos.x, pos.y, 0, 0,
                        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) != 0;
}

// WINDOWPLACEMENT is in workspace coordinates, which leave out a taskbar at the top or left of
// the monitor.
static POINT workspaceOffset(HMONITOR mon)
{
    MONITORINFO mi = { sizeof(mi) };
    if (!mon || !GetMonitorInfoW(mon, &mi))
        return (POINT){ 0, 0 };
    return (POINT){ mi.rcWork.left - mi.rcMonitor.left, mi.rcWork.top - mi.rcMonitor.top };
}

void _cxuiWin32WindowGetPlacement(CxUiPeer* peer, CxUiPlacement* out)
{
    Win32Window* p     = (Win32Window*)peer;
    WINDOWPLACEMENT wp = { sizeof(wp) };
    *out               = (CxUiPlacement){ 0 };
    if (p->fsMode)
        wp = p->fsPlace;
    else if (!GetWindowPlacement(p->hwnd, &wp))
        return;
    POINT off   = workspaceOffset(MonitorFromWindow(p->hwnd, MONITOR_DEFAULTTONEAREST));
    RECT r      = wp.rcNormalPosition;
    out->normal = (CxUiRect){ r.left + off.x, r.top + off.y, r.right - r.left, r.bottom - r.top };
    out->state  = IsIconic(p->hwnd)  ? CXUI_WINDOW_Minimized
                  : p->fsMode        ? CXUI_WINDOW_Fullscreen
                  : IsZoomed(p->hwnd) ? CXUI_WINDOW_Maximized
                                     : CXUI_WINDOW_Normal;
    out->valid  = CXUI_PLACE_Position | CXUI_PLACE_Size | CXUI_PLACE_State;
}

static UINT showCmdFor(uint32 state)
{
    return state == CXUI_WINDOW_Maximized   ? SW_SHOWMAXIMIZED
           : state == CXUI_WINDOW_Minimized ? SW_SHOWMINNOACTIVE
                                            : SW_SHOWNOACTIVATE;
}

void _cxuiWin32WindowSetPlacement(CxUiPeer* peer, const CxUiPlacement* pl, bool withState)
{
    Win32Window* p     = (Win32Window*)peer;
    WINDOWPLACEMENT wp = { sizeof(wp) };
    if (!GetWindowPlacement(p->hwnd, &wp))
        return;
    if (pl->valid & (CXUI_PLACE_Position | CXUI_PLACE_Size)) {
        RECT r    = { pl->normal.x, pl->normal.y, pl->normal.x + pl->normal.w, pl->normal.y + pl->normal.h };
        POINT off = workspaceOffset(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST));
        OffsetRect(&r, -off.x, -off.y);
        wp.rcNormalPosition = r;
        // A fullscreen window goes back there when it leaves.
        if (p->fsMode) {
            p->fsPlace.rcNormalPosition = r;
            return;
        }
    }
    if (p->fsMode)
        return;
    uint32 cur = IsIconic(p->hwnd) ? CXUI_WINDOW_Minimized : IsZoomed(p->hwnd) ? CXUI_WINDOW_Maximized : CXUI_WINDOW_Normal;
    wp.flags   = 0;
    wp.showCmd = withState ? showCmdFor(pl->state) : IsWindowVisible(p->hwnd) ? showCmdFor(cur) : SW_HIDE;
    p->placing = true;
    SetWindowPlacement(p->hwnd, &wp);
    p->placing = false;
}

bool _cxuiWin32WindowSetTopmost(CxUiPeer* peer, bool on)
{
    Win32Window* p = (Win32Window*)peer;
    p->topmost     = on;
    // A Monitor window is above everything while active anyway.
    bool front = on || (p->fsMode == CXUI_FULLSCREEN_Monitor && GetActiveWindow() == p->hwnd);
    return SetWindowPos(p->hwnd, front ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER) != 0;
}

bool _cxuiWin32TestTopmost(CxUiPeer* p, bool* topmost)
{
    *topmost = (GetWindowLongW(((Win32Window*)p)->hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    return true;
}

int32 _cxuiWin32WindowMonitorOf(CxUiPeer* p)
{
    return _cxuiWin32WindowMonitor(((Win32Window*)p)->hwnd);
}

// Both modes are a window without a frame covering the monitor; Windows hides the taskbar while
// such a window is active. A Monitor window is also above everything while active.
bool _cxuiWin32WindowSetFullscreen(CxUiPeer* peer, uint32 mode, const CxUiMonitor* mon)
{
    Win32Window* p = (Win32Window*)peer;
    HWND hwnd      = p->hwnd;
    if (mode == CXUI_FULLSCREEN_None) {
        if (!p->fsMode)
            return true;
        p->fsMode = CXUI_FULLSCREEN_None;
        SetWindowLongW(hwnd, GWL_STYLE, (LONG)p->fsStyle);
        SetWindowPos(hwnd, p->topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        WINDOWPLACEMENT wp = p->fsPlace;
        if (wp.showCmd != SW_SHOWMAXIMIZED)
            wp.showCmd = SW_SHOWNOACTIVATE;
        p->placing = true;
        SetWindowPlacement(hwnd, &wp);
        p->placing = false;
    } else {
        RECT r;
        if (mon) {
            r = (RECT){ mon->bounds.x, mon->bounds.y, mon->bounds.x + mon->bounds.w, mon->bounds.y + mon->bounds.h };
        } else {
            MONITORINFO mi = { sizeof(mi) };
            if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
                return false;
            r = mi.rcMonitor;
        }
        if (!p->fsMode) {
            p->fsPlace.length = sizeof(p->fsPlace);
            GetWindowPlacement(hwnd, &p->fsPlace);
            p->fsStyle = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
        }
        p->fsMode = mode;
        if (IsZoomed(hwnd) || IsIconic(hwnd))
            ShowWindow(hwnd, SW_RESTORE);
        DWORD style = (p->fsStyle & ~(WS_OVERLAPPEDWINDOW | WS_MAXIMIZE | WS_MINIMIZE)) | WS_POPUP | WS_CLIPCHILDREN;
        SetWindowLongW(hwnd, GWL_STYLE, (LONG)style);
        bool front = p->topmost || (mode == CXUI_FULLSCREEN_Monitor && GetActiveWindow() == hwnd);
        p->placing = true;
        SetWindowPos(hwnd, front ? HWND_TOPMOST : HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        p->placing = false;
    }
    // WM_SIZE reports it, unless the size didn't change.
    uint32 state = IsIconic(hwnd) ? CXUI_WINDOW_Minimized
                   : p->fsMode    ? CXUI_WINDOW_Fullscreen
                   : IsZoomed(hwnd) ? CXUI_WINDOW_Maximized
                                    : CXUI_WINDOW_Normal;
    if (state != p->lastState) {
        p->lastState = state;
        _cxuiWindowStateChanged(&p->base, state);
    }
    return true;
}

static HWND tipsOf(HWND root, bool create);

void _cxuiWin32WindowSetDark(CxUiPeer* peer, bool dark)
{
    Win32Window* p = (Win32Window*)peer;
    if (p->dark == dark)
        return;
    p->dark = dark;
    win32ApplyDark(p->hwnd, dark);
    HWND tips = tipsOf(p->hwnd, false);
    if (tips)
        _cxuiWin32ApplyDarkTheme(tips, dark);
    EnumChildWindows(p->hwnd, childSetDark, dark);
    // A frame already drawn keeps its colors until it is recalculated, and the title bar until the
    // window's active state is drawn again.
    if (IsWindowVisible(p->hwnd)) {
        SetWindowPos(p->hwnd, NULL, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        BOOL active = GetActiveWindow() == p->hwnd;
        SendMessageW(p->hwnd, WM_NCACTIVATE, !active, 0);
        SendMessageW(p->hwnd, WM_NCACTIVATE, active, 0);
    }
    InvalidateRect(p->hwnd, NULL, TRUE);
}

HWND _cxuiWin32PeerHwnd(CxUiPeer* p)
{
    if (!p)
        return NULL;
    switch (p->kind) {
    case CXUI_PEER_Canvas:
        return _cxuiWin32CanvasHwnd(p);
    case CXUI_PEER_Panel:
        return _cxuiWin32PanelHwnd(p);
    case CXUI_PEER_Text:
        return _cxuiWin32TextHwnd(p);
    case CXUI_PEER_Window:
    case CXUI_PEER_Other:
        return ((Win32Window*)p)->hwnd;
    case CXUI_PEER_Menu:
        return NULL;
    default:
        return _cxuiWin32ControlHwnd(p);
    }
}

// The boxes a framed window has for its style, which a modal one goes without, as dialogs do
static void modalBoxes(Win32Window* p, bool modal)
{
    DWORD style = (DWORD)GetWindowLongW(p->hwnd, GWL_STYLE);
    p->modal    = modal;
    if (!(style & WS_CAPTION))
        return;
    DWORD boxes = WS_MINIMIZEBOX | ((style & WS_THICKFRAME) ? WS_MAXIMIZEBOX : 0);
    SetWindowLongW(p->hwnd, GWL_STYLE, modal ? style & ~(WS_MINIMIZEBOX | WS_MAXIMIZEBOX) : style | boxes);
    SetWindowPos(p->hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

// The frame as it is seen, without the invisible resize borders Windows 10 puts around it. A
// hidden window's is taken as its whole frame.
static RECT visibleFrame(HWND hwnd)
{
    RECT rc;
    GetWindowRect(hwnd, &rc);
    RECT ext;
    if (pDwmGetWindowAttribute && IsWindowVisible(hwnd) &&
        SUCCEEDED(pDwmGetWindowAttribute(hwnd, CXUI_DWMWA_FRAME, &ext, sizeof(ext))))
        return ext;
    return rc;
}

static CxUiRect rectOf(RECT r)
{
    return (CxUiRect){ r.left, r.top, r.right - r.left, r.bottom - r.top };
}

// By a minimized window, or with none, centered on the screen: the one with the mouse on it, as
// Windows does for new windows.
void _cxuiWin32MoveNear(HWND hwnd, HWND other, uint32 where)
{
    CxUiRect by;
    if (other && !IsIconic(other)) {
        by = rectOf(visibleFrame(other));
    } else {
        POINT pt;
        GetCursorPos(&pt);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(other ? MonitorFromWindow(other, MONITOR_DEFAULTTOPRIMARY)
                              : MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY),
                        &mi);
        by    = rectOf(mi.rcWork);
        where = CXUI_NEAR_Center;
    }
    RECT wr, vis = visibleFrame(hwnd);
    GetWindowRect(hwnd, &wr);
    CxUiPoint at = _cxuiNearPoint(rectOf(vis), by, where);
    SetWindowPos(hwnd, NULL, at.x - (vis.left - wr.left), at.y - (vis.top - wr.top), 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void moveNear(Win32Window* p, HWND other, uint32 where)
{
    p->centerOnShow = false;
    _cxuiWin32MoveNear(p->hwnd, other, where);
}

bool _cxuiWin32WindowMoveNear(CxUiPeer* p, CxUiPeer* other, uint32 where)
{
    moveNear((Win32Window*)p, other ? ((Win32Window*)other)->hwnd : NULL, where);
    return true;
}

// An owned window stays above its owner, is minimized with it, and has no taskbar button of its
// own.
void _cxuiWin32WindowSetOwner(CxUiPeer* peer, CxUiPeer* owner)
{
    Win32Window* p = (Win32Window*)peer;
    p->owner       = owner ? ((Win32Window*)owner)->hwnd : NULL;
    if (!p->modal)
        SetWindowLongPtrW(p->hwnd, GWLP_HWNDPARENT, (LONG_PTR)p->owner);
}

void _cxuiWin32WindowBeginModal(CxUiPeer* peer, CxUiPeer* owner)
{
    Win32Window* p = (Win32Window*)peer;
    HWND ownerHwnd = _cxuiWin32PeerHwnd(owner);
    SetWindowLongPtrW(p->hwnd, GWLP_HWNDPARENT, (LONG_PTR)ownerHwnd);
    modalBoxes(p, true);
    if (!peer->w || !objDynCast(CxUiWindow, peer->w)->_hasPos)
        moveNear(p, ownerHwnd, CXUI_NEAR_Center);
}

void _cxuiWin32WindowEndModal(CxUiPeer* p)
{
    SetWindowLongPtrW(((Win32Window*)p)->hwnd, GWLP_HWNDPARENT, (LONG_PTR)((Win32Window*)p)->owner);
    modalBoxes((Win32Window*)p, false);
}

bool _cxuiWin32TestOwnedBy(CxUiPeer* p, CxUiPeer* owner, bool* owned)
{
    *owned = GetWindow(((Win32Window*)p)->hwnd, GW_OWNER) == (owner ? ((Win32Window*)owner)->hwnd : NULL);
    return true;
}

// Windows destroys the windows a window owns along with it; cxui's are let go first, as they
// belong to their own peers.
static BOOL CALLBACK unown(HWND hwnd, LPARAM lp)
{
    WCHAR cls[16];
    if (GetWindow(hwnd, GW_OWNER) != (HWND)lp || !GetClassNameW(hwnd, cls, 16) || wcscmp(cls, windowClass) != 0)
        return TRUE;
    Win32Window* w = (Win32Window*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (w && w->owner == (HWND)lp)
        w->owner = NULL;
    SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, 0);
    return TRUE;
}

void _cxuiWin32WindowSetEnabled(CxUiPeer* p, bool enabled)
{
    EnableWindow(((Win32Window*)p)->hwnd, enabled);
}

// Windows moves the window as if the press were on a title bar, in a loop of its own that
// returns when the button is released. Only the left button moves windows there.
bool _cxuiWin32WindowBeginDrag(CxUiPeer* p)
{
    HWND hwnd = ((Win32Window*)p)->hwnd;
    if (!(GetKeyState(VK_LBUTTON) & 0x8000))
        return false;
    ReleaseCapture();
    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, (LPARAM)GetMessagePos());
    return true;
}

bool _cxuiWin32TestBorderless(CxUiPeer* p, bool* borderless)
{
    HWND hwnd   = ((Win32Window*)p)->hwnd;
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
    *borderless = !(style & (WS_CAPTION | WS_THICKFRAME)) && (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_APPWINDOW);
    return true;
}

// What the dialog manager does on Tab, which cxui handles itself: until then Windows may hide
// focus rectangles, leaving no sign of which control has focus.
void _cxuiWin32WindowFocusCues(CxUiPeer* win)
{
    SendMessageW(((Win32Window*)win)->hwnd, WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);
}

bool _cxuiWin32TestFocusCues(CxUiPeer* p, bool hide, bool* shown)
{
    HWND hwnd = ((Win32Window*)p)->hwnd;
    // Directly, since a request to hide them is ignored where the user has them always shown
    if (hide)
        SendMessageW(hwnd, WM_UPDATEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
    *shown = !(SendMessageW(hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);
    return true;
}

bool _cxuiWin32TestFrameDark(CxUiPeer* p, bool* dark)
{
    BOOL val = FALSE;
    if (!pDwmGetWindowAttribute)
        return false;
    HWND hwnd = ((Win32Window*)p)->hwnd;
    if (FAILED(pDwmGetWindowAttribute(hwnd, CXUI_DWMWA_DARK, &val, sizeof(val))) &&
        FAILED(pDwmGetWindowAttribute(hwnd, CXUI_DWMWA_DARK_OLD, &val, sizeof(val))))
        return false;
    *dark = val != FALSE;
    return true;
}

// What Windows sends when the window moves to a monitor with another DPI: the new DPI and a
// rectangle keeping the size in DIPs.
bool _cxuiWin32TestForceDpi(CxUiPeer* peer, float32 scale)
{
    Win32Window* p = (Win32Window*)peer;
    CxUiWindow* w  = peer->w ? objDynCast(CxUiWindow, peer->w) : NULL;
    float32 old    = w && w->_scale > 0 ? w->_scale : 1.0f;
    // The client area in proportion, around it the frame the window really has, since its real
    // DPI doesn't change
    RECT client, frame;
    GetClientRect(p->hwnd, &client);
    GetWindowRect(p->hwnd, &frame);
    RECT r = { 0, 0, (LONG)(client.right * scale / old + 0.5f), (LONG)(client.bottom * scale / old + 0.5f) };
    DWORD style = (DWORD)GetWindowLongW(p->hwnd, GWL_STYLE), exStyle = (DWORD)GetWindowLongW(p->hwnd, GWL_EXSTYLE);
    BOOL menu = GetMenu(p->hwnd) != NULL;
    if (!pAdjustWindowRectExForDpi || !pAdjustWindowRectExForDpi(&r, style, menu, exStyle, win32Dpi(p->hwnd)))
        AdjustWindowRectEx(&r, style, menu, exStyle);
    OffsetRect(&r, frame.left - r.left, frame.top - r.top);
    WORD dpi = (WORD)(scale * 96 + 0.5f);
    SendMessageW(p->hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), (LPARAM)&r);
    return true;
}

// ---- tips ----

// One tooltip control for each top-level window, its property, for the tips of the windows inside
static const WCHAR tipsProp[] = L"CxUiTips";

#ifndef TTTOOLINFOW_V2_SIZE
#define TTTOOLINFOW_V2_SIZE CCSIZEOF_STRUCT(TOOLINFOW, lParam)
#endif

static HWND tipsOf(HWND root, bool create)
{
    HWND tips = (HWND)GetPropW(root, tipsProp);
    if (tips || !create)
        return tips;
    ULONG_PTR cookie;
    bool v6 = _cxuiWin32Comctl6Begin(&cookie);
    tips    = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                              CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, root, NULL,
                              GetModuleHandleW(NULL), NULL);
    if (v6)
        _cxuiWin32Comctl6End(cookie);
    if (!tips)
        return NULL;
    // Several lines, broken where the text has \n and to fit this width
    SendMessageW(tips, TTM_SETMAXTIPWIDTH, 0, 480);
    if (_cxuiWin32RootDark(root))
        _cxuiWin32ApplyDarkTheme(tips, true);
    SetPropW(root, tipsProp, tips);
    return tips;
}

static TOOLINFOW toolFor(HWND hwnd)
{
    TOOLINFOW ti = { TTTOOLINFOW_V2_SIZE };
    ti.uFlags    = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd      = GetParent(hwnd);
    ti.uId       = (UINT_PTR)hwnd;
    return ti;
}

void _cxuiWin32PeerSetTip(CxUiPeer* peer, strref tip)
{
    HWND hwnd = _cxuiWin32PeerHwnd(peer);
    HWND root = GetAncestor(hwnd, GA_ROOT);
    HWND tips = root ? tipsOf(root, !strEmpty(tip)) : NULL;
    if (!tips)
        return;
    TOOLINFOW ti = toolFor(hwnd);
    SendMessageW(tips, TTM_DELTOOL, 0, (LPARAM)&ti);
    if (strEmpty(tip))
        return;
    uint16* w     = strToUTF16A(tip);
    ti.lpszText   = (LPWSTR)w;
    SendMessageW(tips, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    xaFree(w);
}

// The tool goes before the window: the tooltip subclasses it, which must be undone first.
static void tipRemove(CxUiPeer* peer)
{
    HWND hwnd = _cxuiWin32PeerHwnd(peer);
    HWND root = hwnd ? GetAncestor(hwnd, GA_ROOT) : NULL;
    HWND tips = root && root != hwnd ? tipsOf(root, false) : NULL;
    if (tips) {
        TOOLINFOW ti = toolFor(hwnd);
        SendMessageW(tips, TTM_DELTOOL, 0, (LPARAM)&ti);
    }
}

bool _cxuiWin32TestTip(CxUiPeer* peer, string* out)
{
    HWND hwnd = _cxuiWin32PeerHwnd(peer);
    HWND root = GetAncestor(hwnd, GA_ROOT);
    HWND tips = root ? tipsOf(root, false) : NULL;
    if (!tips)
        return true;
    WCHAR buf[1024]  = { 0 };
    TOOLINFOW ti     = toolFor(hwnd);
    ti.lpszText      = buf;
    if (SendMessageW(tips, TTM_GETTOOLINFOW, 0, (LPARAM)&ti))
        strFromUTF16(out, (const uint16*)buf, (int32)wcslen(buf));
    return true;
}

void _cxuiWin32PeerSetVisible(CxUiPeer* peer, bool visible)
{
    if (peer->kind != CXUI_PEER_Window)
        ShowWindow(_cxuiWin32PeerHwnd(peer), visible ? SW_SHOWNA : SW_HIDE);
}

void _cxuiWin32PeerRaise(CxUiPeer* peer)
{
    // A new child window goes behind its siblings.
    SetWindowPos(_cxuiWin32PeerHwnd(peer), HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

bool _cxuiWin32TestWindowActive(CxUiPeer* p, bool* active)
{
    *active = GetActiveWindow() == ((Win32Window*)p)->hwnd;
    return true;
}

bool _cxuiWin32TestFrontmost(CxUiPeer* win, CxUiPeer* p, CxUiPoint px, bool* front)
{
    HWND h   = _cxuiWin32PeerHwnd(win);
    HWND w   = _cxuiWin32PeerHwnd(p);
    POINT pt = { px.x, px.y };
    // Down through the children in front at the point, to the innermost
    for (;;) {
        HWND child = ChildWindowFromPointEx(h, pt, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
        if (!child || child == h)
            break;
        MapWindowPoints(h, child, &pt, 1);
        h = child;
    }
    *front = h == w || IsChild(w, h);
    return true;
}

bool _cxuiWin32TestPeerShown(CxUiPeer* p, bool* shown)
{
    *shown = (GetWindowLongW(_cxuiWin32PeerHwnd(p), GWL_STYLE) & WS_VISIBLE) != 0;
    return true;
}

void _cxuiWin32PeerDestroy(CxUiPeer* peer)
{
    if (peer->kind == CXUI_PEER_Menu) {
        _cxuiWin32MenuDestroy(peer);
        return;
    }
    if (peer->kind != CXUI_PEER_Window)
        tipRemove(peer);
    if (peer->kind == CXUI_PEER_Canvas) {
        _cxuiWin32CanvasDestroy(peer);
        return;
    }
    if (peer->kind == CXUI_PEER_Panel) {
        _cxuiWin32PanelDestroy(peer);
        return;
    }
    if (peer->kind == CXUI_PEER_Text) {
        _cxuiWin32TextDestroy(peer);
        return;
    }
    if (_cxuiPeerIsControl(peer->kind)) {
        _cxuiWin32ControlDestroy(peer);
        return;
    }
    Win32Window* p = (Win32Window*)peer;
    // DestroyWindow destroys the menu bar too, which is its own peer's to destroy.
    _cxuiWin32WindowAttachMenu(p->hwnd, NULL);
    HWND tips      = tipsOf(p->hwnd, false);
    RemovePropW(p->hwnd, tipsProp);
    if (tips)
        DestroyWindow(tips);
    EnumThreadWindows(GetCurrentThreadId(), unown, (LPARAM)p->hwnd);
    DestroyWindow(p->hwnd);
    if (p->bigIcon)
        DestroyIcon(p->bigIcon);
    if (p->smallIcon)
        DestroyIcon(p->smallIcon);
    xaFree(p);
}
