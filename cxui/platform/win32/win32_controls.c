#include "win32_ui.h"

#include <commctrl.h>
#include <math.h>
#include <windowsx.h>

#include <cx/format.h>
#include <cx/utils/lazyinit.h>

#include "core/cxuicontrols.h"

// A label is a STATIC, and a button, check box or radio button a BUTTON, created under cxui's
// comctl6 context. Windows draws `&` mnemonics itself, so titles go to it as given. The parent (a
// window or a panel) passes on their color requests, clicks and owner drawing; a label paints on
// its parent's background in the core's text color. The others are subclassed so keys reach the
// core before they act on them, and take the dark Explorer theme in a dark window.
//
// A list is a LISTBOX and a dropdown a COMBOBOX (CBS_DROPDOWNLIST), in a text field's colors; a
// dropdown's list is a window of its own, which is given the same property so its color requests
// find the peer.
//
// A slider is a trackbar over whole positions, its notifications passed on by the parent; a
// vertical one is turned over, so its low end is at the bottom. A progress bar shows ten thousandths,
// and a marquee while it shows activity.
//
// Check boxes and radio buttons never change their own state (BS_3STATE and BS_RADIOBUTTON, not
// the automatic styles): the core sets it. A themed check box ignores the text color it is given,
// so in a dark window they are owner-drawn instead, with the theme's dark box and the core's
// color.

#ifndef BCM_GETIDEALSIZE
#define BCM_GETIDEALSIZE 0x1601
#endif

typedef struct Win32Control {
    CxUiPeer base;
    HWND hwnd;
    WNDPROC oldProc;   // the control's own, before cxui's
    CxUiFont* font;    // a reference, so its HFONT outlives the control's use of it
    COLORREF text;     // a label's
    COLORREF bgColor;  // the parent background the brush was made for
    HBRUSH bg;
    bool eatChar;      // the core took the key down whose character follows
    uint32 checkState; // a check box's or radio button's CxUiCheckState
    HWND dropList;     // a dropdown's list
    COLORREF listBgColor;
    HBRUSH listBg;     // a list's or dropdown's background
    bool ownerDraw;    // a check box or radio button drawn by cxui, in a dark window
    bool sized;        // a list has had a height
    int32 range;       // a slider's positions
    bool focusing;     // a radio button getting focus, which it would take as a click
    bool multi;        // a list where any number can be selected
    CxUiPeer* page;    // tabs' page surface, a panel of their own
    HWND measure;      // tabs on several lines: a hidden copy, resized to measure them
    CxUiTabsColors tabColors;
    int32 hotTab;      // tabs: the tab under the mouse, or -1
    bool tracking;     // tabs: waiting for the mouse to leave
} Win32Control;

static const WCHAR propName[] = L"CxUiControl";

static Win32Control* controlOf(HWND hwnd)
{
    return hwnd ? (Win32Control*)GetPropW(hwnd, propName) : NULL;
}

typedef HRESULT(WINAPI* SetWindowThemeFn)(HWND, LPCWSTR, LPCWSTR);
typedef HANDLE(WINAPI* OpenThemeDataFn)(HWND, LPCWSTR);
typedef HRESULT(WINAPI* CloseThemeDataFn)(HANDLE);
typedef HRESULT(WINAPI* DrawThemeBackgroundFn)(HANDLE, HDC, int, int, const RECT*, const RECT*);
static SetWindowThemeFn pSetWindowTheme;
static OpenThemeDataFn pOpenThemeData;
static CloseThemeDataFn pCloseThemeData;
static DrawThemeBackgroundFn pDrawThemeBackground;
static LazyInitState themeInit;

static void themeInitOnce(void* unused)
{
    unused_noeval(unused);
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux)
        return;
    pSetWindowTheme      = (SetWindowThemeFn)GetProcAddress(ux, "SetWindowTheme");
    pOpenThemeData       = (OpenThemeDataFn)GetProcAddress(ux, "OpenThemeData");
    pCloseThemeData      = (CloseThemeDataFn)GetProcAddress(ux, "CloseThemeData");
    pDrawThemeBackground = (DrawThemeBackgroundFn)GetProcAddress(ux, "DrawThemeBackground");
}

// The dark Explorer theme draws buttons, lists and scroll bars dark, and the dark CFD theme
// dropdowns and single-line edits (Windows 10 1809 and later); elsewhere they are ignored.
_Use_decl_annotations_
void _cxuiWin32ApplyDarkTheme(HWND hwnd, bool dark)
{
    lazyInit(&themeInit, themeInitOnce, NULL);
    WCHAR cls[16];
    bool named = GetClassNameW(hwnd, cls, 16) != 0;
    bool cfd   = named && (lstrcmpiW(cls, WC_COMBOBOXW) == 0 ||
                         (lstrcmpiW(cls, L"Edit") == 0 && !(GetWindowLongW(hwnd, GWL_STYLE) & ES_MULTILINE)));
    if (pSetWindowTheme)
        pSetWindowTheme(hwnd, dark ? (cfd ? L"DarkMode_CFD" : L"DarkMode_Explorer") : NULL, NULL);
}

static bool vertical(Win32Control* c)
{
    return (GetWindowLongW(c->hwnd, GWL_STYLE) & TBS_VERT) != 0;
}

// The trackbar's position counted from the low end
static int32 sliderPos(Win32Control* c)
{
    int32 pos = (int32)SendMessageW(c->hwnd, TBM_GETPOS, 0, 0);
    return vertical(c) ? c->range - pos : pos;
}

static bool isCheck(Win32Control* c)
{
    return c->base.kind == CXUI_PEER_Check || c->base.kind == CXUI_PEER_Radio;
}

// The check box's or radio button's own style, or owner drawing
static void setCheckLook(Win32Control* c, bool dark)
{
    DWORD style  = (DWORD)GetWindowLongW(c->hwnd, GWL_STYLE) & ~BS_TYPEMASK;
    c->ownerDraw = dark;
    style |= dark ? BS_OWNERDRAW : c->base.kind == CXUI_PEER_Radio ? BS_RADIOBUTTON : BS_3STATE;
    SetWindowLongW(c->hwnd, GWL_STYLE, (LONG)style);
    if (!dark)
        SendMessageW(c->hwnd, BM_SETCHECK,
                     c->checkState == CXUI_CHECK_On      ? BST_CHECKED
                     : c->checkState == CXUI_CHECK_Mixed ? BST_INDETERMINATE
                                                         : BST_UNCHECKED,
                     0);
    InvalidateRect(c->hwnd, NULL, TRUE);
}

_Use_decl_annotations_
void _cxuiWin32ControlSetDark(HWND hwnd, bool dark)
{
    Win32Control* c = controlOf(hwnd);
    if (!c || c->base.kind == CXUI_PEER_Label)
        return;
    _cxuiWin32ApplyDarkTheme(hwnd, dark);
    if (c->measure)
        _cxuiWin32ApplyDarkTheme(c->measure, dark);
    if (isCheck(c))
        setCheckLook(c, dark);
}

static HBRUSH parentBrush(Win32Control* c, COLORREF bg)
{
    if (!c->bg || c->bgColor != bg) {
        if (c->bg)
            DeleteObject(c->bg);
        c->bg      = CreateSolidBrush(bg);
        c->bgColor = bg;
    }
    return c->bg;
}

static int32 pxRound(float32 v);

// The box's side, and the gap after it, in the font's pixels
static void checkMetrics(CxUiFont* f, int32* box, int32* gap)
{
    float32 s = f ? cxuiFontScale(f) : 1.0f;
    *box      = pxRound(13 * s);
    *gap      = pxRound(5 * s);
}

// A check box or radio button in a dark window: the dark theme's box (the classic one where there
// is no theme), and the title in the core's color.
static void drawCheck(Win32Control* c, const DRAWITEMSTRUCT* dis, COLORREF parentBg)
{
    HDC dc   = dis->hDC;
    RECT rc  = dis->rcItem;
    bool radio = c->base.kind == CXUI_PEER_Radio;
    bool off   = (dis->itemState & ODS_DISABLED) != 0;
    bool down  = (dis->itemState & ODS_SELECTED) != 0;
    FillRect(dc, &rc, parentBrush(c, parentBg));

    int32 box, gap;
    checkMetrics(c->font, &box, &gap);
    int32 top = rc.top + ((rc.bottom - rc.top) - box) / 2;
    RECT br   = { rc.left, top, rc.left + box, top + box };
    lazyInit(&themeInit, themeInitOnce, NULL);
    HANDLE theme = pOpenThemeData ? pOpenThemeData(c->hwnd, L"Button") : NULL;
    if (theme) {
        // BP_RADIOBUTTON or BP_CHECKBOX; states run normal, hot, pressed, disabled for clear,
        // checked, then mixed
        int part  = radio ? 2 : 3;
        int state = 1 + (c->checkState == CXUI_CHECK_On ? 4 : c->checkState == CXUI_CHECK_Mixed ? 8 : 0) +
                    (off ? 3 : down ? 2 : 0);
        pDrawThemeBackground(theme, dc, part, state, &br, NULL);
        pCloseThemeData(theme);
    } else {
        UINT st = (radio ? DFCS_BUTTONRADIO : c->checkState == CXUI_CHECK_Mixed ? DFCS_BUTTON3STATE : DFCS_BUTTONCHECK) |
                  (c->checkState != CXUI_CHECK_Off ? DFCS_CHECKED : 0) | (off ? DFCS_INACTIVE : 0) |
                  (down ? DFCS_PUSHED : 0);
        DrawFrameControl(dc, &br, DFC_BUTTON, st);
    }

    int32 n    = GetWindowTextLengthW(c->hwnd);
    WCHAR* buf = xaAlloc((n + 1) * sizeof(WCHAR));
    n          = GetWindowTextW(c->hwnd, buf, n + 1);
    HGDIOBJ old = c->font ? SelectObject(dc, _cxuiWin32GdiFont(c->font, NULL)) : NULL;
    UINT fmt    = DT_SINGLELINE | DT_LEFT | DT_VCENTER | ((dis->itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0);
    RECT tr     = { rc.left + box + gap, rc.top, rc.right, rc.bottom };
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c->text);
    DrawTextW(dc, buf, n, &tr, fmt);
    if ((dis->itemState & ODS_FOCUS) && !(dis->itemState & ODS_NOFOCUSRECT)) {
        RECT fr = tr;
        DrawTextW(dc, buf, n, &fr, fmt | DT_CALCRECT);
        int32 h = fr.bottom - fr.top;
        fr.top  = tr.top + ((tr.bottom - tr.top) - h) / 2;
        fr.bottom = fr.top + h;
        InflateRect(&fr, 1, 1);
        DrawFocusRect(dc, &fr);
    }
    if (old)
        SelectObject(dc, old);
    xaFree(buf);
}

_Use_decl_annotations_
bool _cxuiWin32ControlParentMessage(UINT msg, WPARAM wp, LPARAM lp, COLORREF parentBg, LRESULT* result)
{
    *result         = 0;
    HWND from       = msg == WM_DRAWITEM ? ((DRAWITEMSTRUCT*)lp)->hwndItem
                      : msg == WM_NOTIFY ? ((NMHDR*)lp)->hwndFrom
                                         : (HWND)lp;
    Win32Control* c = controlOf(from);
    if (!c)
        return false;
    switch (msg) {
    case WM_NOTIFY:
        if (c->base.kind != CXUI_PEER_Tabs || ((NMHDR*)lp)->code != TCN_SELCHANGE)
            return false;
        _cxuiNativeEnter();
        _cxuiTabsPicked(&c->base, (int32)SendMessageW(c->hwnd, TCM_GETCURSEL, 0, 0));
        _cxuiNativeLeave();
        return true;
    case WM_COMMAND: {
        WORD code = HIWORD(wp);
        if (c->base.kind == CXUI_PEER_List || c->base.kind == CXUI_PEER_Dropdown) {
            bool list = c->base.kind == CXUI_PEER_List;
            // Where several can be selected, the item double-clicked is the one with the caret.
            int32 sel = (int32)SendMessageW(c->hwnd, c->multi ? LB_GETCARETINDEX : list ? LB_GETCURSEL : CB_GETCURSEL, 0, 0);
            _cxuiNativeEnter();
            if (code == (list ? LBN_SELCHANGE : CBN_SELCHANGE) && c->multi)
                _cxuiListMarksChanged(&c->base);
            else if (code == (list ? LBN_SELCHANGE : CBN_SELCHANGE))
                _cxuiListChosen(&c->base, sel);
            else if (list && code == LBN_DBLCLK)
                _cxuiListActivated(&c->base, sel);
            _cxuiNativeLeave();
            return true;
        }
        // A second click soon after the first comes as a double click to radio buttons and
        // owner-drawn buttons.
        if (c->base.kind != CXUI_PEER_Label && !c->focusing &&
            (code == BN_CLICKED || (code == BN_DOUBLECLICKED && isCheck(c)))) {
            _cxuiNativeEnter();
            _cxuiButtonClicked(&c->base);
            _cxuiNativeLeave();
        }
        return true;
    }
    case WM_HSCROLL:
    case WM_VSCROLL:
        if (c->base.kind != CXUI_PEER_Slider)
            return false;
        _cxuiNativeEnter();
        _cxuiSliderMoved(&c->base, sliderPos(c), LOWORD(wp) == TB_ENDTRACK);
        _cxuiNativeLeave();
        return true;
    case WM_DRAWITEM:
        if (!isCheck(c))
            return false;
        drawCheck(c, (DRAWITEMSTRUCT*)lp, parentBg);
        *result = TRUE;
        return true;
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT:
        if (c->base.kind != CXUI_PEER_List && c->base.kind != CXUI_PEER_Dropdown)
            return false;
        SetTextColor((HDC)wp, c->text);
        SetBkColor((HDC)wp, c->listBgColor);
        *result = (LRESULT)c->listBg;
        return c->listBg != NULL;
    case WM_CTLCOLORSTATIC:
        if (c->base.kind == CXUI_PEER_Dropdown && c->listBg) {
            SetTextColor((HDC)wp, c->text);
            SetBkColor((HDC)wp, c->listBgColor);
            *result = (LRESULT)c->listBg;
            return true;
        }
        SetTextColor((HDC)wp, c->text);
        SetBkColor((HDC)wp, parentBg);
        *result = (LRESULT)parentBrush(c, parentBg);
        return true;
    case WM_CTLCOLORBTN:
        // What shows around the button's rounded corners
        *result = (LRESULT)parentBrush(c, parentBg);
        return true;
    }
    return false;
}

static HWND createControl(Win32Control* c, LPCWSTR cls, DWORD style, DWORD exStyle, CxUiPeer* host)
{
    ULONG_PTR cookie;
    bool v6   = _cxuiWin32Comctl6Begin(&cookie);
    HWND hwnd = CreateWindowExW(exStyle, cls, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style, 0, 0, 0, 0,
                                _cxuiWin32HostHwnd(host), NULL, GetModuleHandleW(NULL), NULL);
    if (v6)
        _cxuiWin32Comctl6End(cookie);
    if (!hwnd) {
        logFmt(Error, _SL("cxui couldn't create a ${string} (error ${uint})"),
               stvar(strref, cls == WC_STATICW   ? _S"label"
                             : cls == WC_BUTTONW ? _S"button"
                             : cls == WC_TABCONTROLW ? _S"tab control"
                                                 : _S"list"),
               stvar(uint32, GetLastError()));
        return NULL;
    }
    c->hwnd = hwnd;
    SetPropW(hwnd, propName, c);
    return hwnd;
}

static void setText(HWND hwnd, strref text)
{
    uint16* w = strToUTF16A(text);
    SetWindowTextW(hwnd, w ? (LPCWSTR)w : L"");
    xaFree(w);
}

static int32 pxRound(float32 v)
{
    return (int32)(v + 0.5f);
}

// Text with its `&` handled as the controls handle it, in the font's pixels; broken into lines
// wrapPx across, unless that is 0
static SIZE textExtent(CxUiFont* f, strref text, int32 wrapPx)
{
    HDC dc      = GetDC(NULL);
    HGDIOBJ old = SelectObject(dc, _cxuiWin32GdiFont(f, NULL));
    uint16* w   = strToUTF16A(text);
    RECT rc     = { 0, 0, wrapPx, 0 };
    DrawTextW(dc, w ? (LPCWSTR)w : L"", -1, &rc, DT_CALCRECT | DT_LEFT | (wrapPx > 0 ? DT_WORDBREAK : 0));
    xaFree(w);
    if (rc.bottom == 0) {
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        rc.bottom = tm.tmHeight;
    }
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    return (SIZE){ rc.right, rc.bottom };
}

// ---- labels ----

// A STATIC repaints only what a resize uncovers, so centered, right-aligned or shortened text
// would leave its old self behind.
static LRESULT CALLBACK labelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Win32Control* c = controlOf(hwnd);
    if (!c)
        return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_SIZE)
        InvalidateRect(hwnd, NULL, TRUE);
    return CallWindowProcW(c->oldProc, hwnd, msg, wp, lp);
}

CxUiPeer* _cxuiWin32LabelCreate(CxUiLabel* l, CxUiPeer* host)
{
    unused_noeval(l);
    Win32Control* c = xaAllocStruct(Win32Control, XA_Zero);
    c->base.kind    = CXUI_PEER_Label;
    HWND hwnd       = createControl(c, WC_STATICW, SS_LEFTNOWORDWRAP, 0, host);
    if (!hwnd) {
        xaFree(c);
        return NULL;
    }
    c->oldProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)labelProc);
    return &c->base;
}

void _cxuiWin32LabelSetText(CxUiPeer* p, strref text)
{
    setText(((Win32Control*)p)->hwnd, text);
}

// SS_LEFT, SS_CENTER and SS_RIGHT break lines at words; only left alignment has a style that
// doesn't.
void _cxuiWin32LabelSetStyle(CxUiPeer* p, uint32 align, uint32 ellipsis, bool wrap)
{
    HWND hwnd   = ((Win32Control*)p)->hwnd;
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE) & ~(SS_TYPEMASK | SS_ELLIPSISMASK);
    if (align & CXUI_TEXT_Right)
        style |= SS_RIGHT;
    else if (align & CXUI_TEXT_Center)
        style |= SS_CENTER;
    else
        style |= wrap || ellipsis != CXUI_ELLIPSIS_None ? SS_LEFT : SS_LEFTNOWORDWRAP;
    if (ellipsis == CXUI_ELLIPSIS_End)
        style |= SS_ENDELLIPSIS;
    else if (ellipsis != CXUI_ELLIPSIS_None)
        style |= SS_PATHELLIPSIS;
    SetWindowLongW(hwnd, GWL_STYLE, (LONG)style);
    InvalidateRect(hwnd, NULL, TRUE);
}

void _cxuiWin32LabelSetColor(CxUiPeer* p, CxUiColor color)
{
    Win32Control* c = (Win32Control*)p;
    c->text         = RGB(color.r, color.g, color.b);
    InvalidateRect(c->hwnd, NULL, TRUE);
}

int32 _cxuiWin32LabelHeightFor(CxUiLabel* l, CxUiFont* f, strref text, int32 width)
{
    unused_noeval(l);
    float32 s = cxuiFontScale(f);
    SIZE ext  = textExtent(f, text, max((int32)floorf(width * s), 1));
    return (int32)ceilf(ext.cy / s);
}

CxUiSize _cxuiWin32LabelMeasure(CxUiLabel* l, CxUiFont* f, strref text)
{
    unused_noeval(l);
    SIZE ext  = textExtent(f, text, 0);
    float32 s = cxuiFontScale(f);
    return (CxUiSize){ (int32)ceilf(ext.cx / s), (int32)ceilf(ext.cy / s) };
}

// ---- buttons ----

static LRESULT buttonMessage(Win32Control* c, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        // An open dropdown's keys are its list's.
        if (c->base.kind == CXUI_PEER_Dropdown && SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0))
            break;
        bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        if (down)
            c->eatChar = false;
        if (_cxuiWin32Key(&c->base, msg, wp, lp)) {
            if (down)
                c->eatChar = true;
            return 0;
        }
        break;
    }
    case WM_CHAR:
    case WM_SYSCHAR:
        if (c->eatChar) {
            c->eatChar = false;
            return 0;
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        // A radio button clicks itself on getting focus, which only the core may do.
        c->focusing = msg == WM_SETFOCUS;
        LRESULT r   = CallWindowProcW(c->oldProc, hwnd, msg, wp, lp);
        c->focusing = false;
        _cxuiFocusChanged(&c->base, msg == WM_SETFOCUS);
        return r;
    }
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        if (_cxuiWin32Wheel(msg, wp, lp))
            return 0;
        break;
    case WM_SIZE:
        // A selection made while the list had no height scrolled it to the top; it starts from
        // the first item, scrolled only as far as the selection needs.
        if (c->base.kind == CXUI_PEER_List && !c->sized && HIWORD(lp) > 0) {
            LRESULT r = CallWindowProcW(c->oldProc, hwnd, msg, wp, lp);
            c->sized  = true;
            SendMessageW(hwnd, LB_SETTOPINDEX, 0, 0);
            int32 sel = (int32)SendMessageW(hwnd, LB_GETCURSEL, 0, 0);
            if (sel >= 0)
                _cxuiWin32ListShow(&c->base, sel);
            return r;
        }
        break;
    }
    return CallWindowProcW(c->oldProc, hwnd, msg, wp, lp);
}

// The whole message is inside one wrapper, so a handler that destroys the button can't free c, or
// the control, before the control's own procedure has run.
static LRESULT CALLBACK buttonProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Win32Control* c = controlOf(hwnd);
    if (!c)
        return DefWindowProcW(hwnd, msg, wp, lp);
    _cxuiNativeEnter();
    LRESULT r = buttonMessage(c, hwnd, msg, wp, lp);
    _cxuiNativeLeave();
    return r;
}

CxUiPeer* _cxuiWin32ButtonCreate(CxUiButton* b, CxUiPeer* host)
{
    unused_noeval(b);
    Win32Control* c = xaAllocStruct(Win32Control, XA_Zero);
    c->base.kind    = CXUI_PEER_Button;
    HWND hwnd       = createControl(c, WC_BUTTONW, WS_TABSTOP | BS_PUSHBUTTON, 0, host);
    if (!hwnd) {
        xaFree(c);
        return NULL;
    }
    c->oldProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)buttonProc);
    if (_cxuiWin32RootDark(hwnd))
        _cxuiWin32ApplyDarkTheme(hwnd, true);
    return &c->base;
}

void _cxuiWin32ButtonSetTitle(CxUiPeer* p, strref title)
{
    setText(((Win32Control*)p)->hwnd, title);
}

void _cxuiWin32ButtonSetDefault(CxUiPeer* p, bool isDefault)
{
    SendMessageW(((Win32Control*)p)->hwnd, BM_SETSTYLE, isDefault ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE);
}

// The control's ideal size where comctl6 gives one; otherwise the title and the margins a dialog's
// button has around it.
CxUiSize _cxuiWin32ButtonMeasure(CxUiButton* b, CxUiFont* f, strref title)
{
    float32 s   = cxuiFontScale(f);
    CxUiPeer* p = CxUiWidget(b)->peer;
    SIZE ideal  = { 0, 0 };
    if (!(p && _cxuiWin32HasComctl6() && SendMessageW(((Win32Control*)p)->hwnd, BCM_GETIDEALSIZE, 0, (LPARAM)&ideal) &&
          ideal.cx > 0)) {
        SIZE ext = textExtent(f, title, 0);
        ideal    = (SIZE){ ext.cx + pxRound(16 * s), ext.cy + pxRound(10 * s) };
    }
    return (CxUiSize){ (int32)ceilf(ideal.cx / s), (int32)ceilf(ideal.cy / s) };
}

// ---- check boxes and radio buttons ----

CxUiPeer* _cxuiWin32CheckCreate(CxUiWidget* w, CxUiPeer* host, bool radio)
{
    unused_noeval(w);
    Win32Control* c = xaAllocStruct(Win32Control, XA_Zero);
    c->base.kind    = radio ? CXUI_PEER_Radio : CXUI_PEER_Check;
    HWND hwnd       = createControl(c, WC_BUTTONW, WS_TABSTOP | (radio ? BS_RADIOBUTTON : BS_3STATE), 0, host);
    if (!hwnd) {
        xaFree(c);
        return NULL;
    }
    c->oldProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)buttonProc);
    if (_cxuiWin32RootDark(hwnd)) {
        _cxuiWin32ApplyDarkTheme(hwnd, true);
        setCheckLook(c, true);
    }
    return &c->base;
}

void _cxuiWin32CheckSetState(CxUiPeer* p, uint32 state)
{
    Win32Control* c = (Win32Control*)p;
    c->checkState   = state;
    if (c->ownerDraw)
        InvalidateRect(c->hwnd, NULL, TRUE);
    else
        SendMessageW(c->hwnd, BM_SETCHECK,
                     state == CXUI_CHECK_On ? BST_CHECKED : state == CXUI_CHECK_Mixed ? BST_INDETERMINATE : BST_UNCHECKED, 0);
}

// The box, a gap and the title, with room for the focus rectangle around the title
CxUiSize _cxuiWin32CheckMeasure(CxUiWidget* w, CxUiFont* f, strref title, bool radio)
{
    unused_noeval(w);
    unused_noeval(radio);
    float32 s = cxuiFontScale(f);
    SIZE ext  = textExtent(f, title, 0);
    int32 box, gap;
    checkMetrics(f, &box, &gap);
    int32 wpx = box + gap + ext.cx + 2;
    int32 hpx = max(ext.cy, box) + 2;
    return (CxUiSize){ (int32)ceilf(wpx / s), (int32)ceilf(hpx / s) };
}

// ---- lists and dropdowns ----

CxUiPeer* _cxuiWin32ListCreate(CxUiWidget* w, CxUiPeer* host, bool dropdown)
{
    CxUiList* l     = dropdown ? NULL : objDynCast(CxUiList, w);
    Win32Control* c = xaAllocStruct(Win32Control, XA_Zero);
    c->base.kind    = dropdown ? CXUI_PEER_Dropdown : CXUI_PEER_List;
    c->multi        = l && l->_select == CXUI_SELECT_Multi;
    DWORD style     = WS_TABSTOP | WS_VSCROLL |
                  (dropdown ? CBS_DROPDOWNLIST : LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | (c->multi ? LBS_EXTENDEDSEL : 0));
    HWND hwnd = createControl(c, dropdown ? WC_COMBOBOXW : WC_LISTBOXW, style, dropdown ? 0 : WS_EX_CLIENTEDGE, host);
    if (!hwnd) {
        xaFree(c);
        return NULL;
    }
    c->oldProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)buttonProc);
    COMBOBOXINFO ci = { sizeof(ci) };
    if (dropdown && GetComboBoxInfo(hwnd, &ci) && ci.hwndList) {
        c->dropList = ci.hwndList;
        SetPropW(c->dropList, propName, c);
    }
    if (_cxuiWin32RootDark(hwnd))
        _cxuiWin32ApplyDarkTheme(hwnd, true);
    return &c->base;
}

void _cxuiWin32ListSplice(CxUiPeer* p, int32 at, int32 removed, const string* items, int32 added)
{
    Win32Control* c = (Win32Control*)p;
    bool list       = c->base.kind == CXUI_PEER_List;
    bool many       = removed + added > 1;
    if (many)
        SendMessageW(c->hwnd, WM_SETREDRAW, FALSE, 0);
    for (int32 i = 0; i < removed; i++)
        SendMessageW(c->hwnd, list ? LB_DELETESTRING : CB_DELETESTRING, at, 0);
    for (int32 i = 0; i < added; i++) {
        uint16* w = strToUTF16A(items[i]);
        SendMessageW(c->hwnd, list ? LB_INSERTSTRING : CB_INSERTSTRING, at + i, (LPARAM)(w ? (LPCWSTR)w : L""));
        xaFree(w);
    }
    if (many) {
        SendMessageW(c->hwnd, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(c->hwnd, NULL, TRUE);
    }
}

void _cxuiWin32ListSetValue(CxUiPeer* p, int32 value)
{
    Win32Control* c = (Win32Control*)p;
    SendMessageW(c->hwnd, c->base.kind == CXUI_PEER_List ? LB_SETCURSEL : CB_SETCURSEL, value, 0);
}

void _cxuiWin32ListSetMarks(CxUiPeer* p, const uint8* marks, int32 count)
{
    HWND hwnd = ((Win32Control*)p)->hwnd;
    SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
    SendMessageW(hwnd, LB_SETSEL, FALSE, -1);
    for (int32 i = 0; i < count; i++) {
        if (marks[i])
            SendMessageW(hwnd, LB_SETSEL, TRUE, i);
    }
    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hwnd, NULL, TRUE);
}

void _cxuiWin32ListGetMarks(CxUiPeer* p, uint8* marks, int32 count)
{
    HWND hwnd = ((Win32Control*)p)->hwnd;
    memset(marks, 0, count);
    int32 n = (int32)SendMessageW(hwnd, LB_GETSELCOUNT, 0, 0);
    if (n <= 0)
        return;
    int* sel = xaAlloc(n * sizeof(int));
    n        = (int32)SendMessageW(hwnd, LB_GETSELITEMS, n, (LPARAM)sel);
    for (int32 i = 0; i < n; i++) {
        if (sel[i] >= 0 && sel[i] < count)
            marks[sel[i]] = 1;
    }
    xaFree(sel);
}

int32 _cxuiWin32ListTopItem(CxUiPeer* p)
{
    return max((int32)SendMessageW(((Win32Control*)p)->hwnd, LB_GETTOPINDEX, 0, 0), 0);
}

void _cxuiWin32ListSetTopItem(CxUiPeer* p, int32 index)
{
    SendMessageW(((Win32Control*)p)->hwnd, LB_SETTOPINDEX, index, 0);
}

void _cxuiWin32ListShow(CxUiPeer* p, int32 index)
{
    HWND hwnd   = ((Win32Control*)p)->hwnd;
    int32 top   = (int32)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
    int32 itemH = max((int32)SendMessageW(hwnd, LB_GETITEMHEIGHT, 0, 0), 1);
    RECT rc;
    GetClientRect(hwnd, &rc);
    int32 rows = max((rc.bottom - rc.top) / itemH, 1);
    if (index < top)
        SendMessageW(hwnd, LB_SETTOPINDEX, index, 0);
    else if (index >= top + rows)
        SendMessageW(hwnd, LB_SETTOPINDEX, index - rows + 1, 0);
}

// A list's rows are the font's height, inside a client edge beside a scroll bar. A dropdown is as
// tall as the control makes itself for its font, and as wide as its widest item beside the arrow.
CxUiSize _cxuiWin32ListMeasure(CxUiWidget* w, CxUiFont* f, int32 widest, int32 rows, bool dropdown)
{
    float32 s = cxuiFontScale(f);
    TEXTMETRICW tm;
    _cxuiWin32GdiFont(f, &tm);
    if (dropdown) {
        int32 h = 0;
        RECT rc;
        if (w->peer && GetWindowRect(((Win32Control*)w->peer)->hwnd, &rc))
            h = (int32)ceilf((rc.bottom - rc.top) / s);
        if (h <= 0)
            h = (int32)ceilf(tm.tmHeight / s) + 8;
        return (CxUiSize){ widest + 28, h };
    }
    int32 itemPx = tm.tmHeight;
    if (w->peer)
        itemPx = max((int32)SendMessageW(((Win32Control*)w->peer)->hwnd, LB_GETITEMHEIGHT, 0, 0), 1);
    return (CxUiSize){ widest + 27, (int32)ceilf((rows * itemPx) / s) + 4 };
}

void _cxuiWin32ListSetColors(CxUiPeer* p, CxUiColor bg, CxUiColor text)
{
    Win32Control* c   = (Win32Control*)p;
    COLORREF bgColor  = RGB(bg.r, bg.g, bg.b);
    c->text           = RGB(text.r, text.g, text.b);
    if (!c->listBg || c->listBgColor != bgColor) {
        if (c->listBg)
            DeleteObject(c->listBg);
        c->listBg      = CreateSolidBrush(bgColor);
        c->listBgColor = bgColor;
    }
    InvalidateRect(c->hwnd, NULL, TRUE);
}

// ---- sliders and progress bars ----

CxUiPeer* _cxuiWin32SliderCreate(CxUiWidget* w, CxUiPeer* host, bool vert)
{
    unused_noeval(w);
    Win32Control* c = xaAllocStruct(Win32Control, XA_Zero);
    c->base.kind    = CXUI_PEER_Slider;
    DWORD style     = WS_TABSTOP | TBS_AUTOTICKS | (vert ? TBS_VERT | TBS_RIGHT : TBS_HORZ | TBS_BOTTOM);
    HWND hwnd       = createControl(c, TRACKBAR_CLASSW, style, 0, host);
    if (!hwnd) {
        xaFree(c);
        return NULL;
    }
    c->oldProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)buttonProc);
    return &c->base;
}

void _cxuiWin32SliderSetRange(CxUiPeer* p, int32 range, int32 pageStep, int32 tickEvery)
{
    Win32Control* c = (Win32Control*)p;
    int32 pos       = sliderPos(c);
    c->range        = range;
    DWORD style     = (DWORD)GetWindowLongW(c->hwnd, GWL_STYLE) & ~TBS_NOTICKS;
    SetWindowLongW(c->hwnd, GWL_STYLE, (LONG)(style | (tickEvery > 0 ? 0 : TBS_NOTICKS)));
    SendMessageW(c->hwnd, TBM_SETRANGEMIN, FALSE, 0);
    SendMessageW(c->hwnd, TBM_SETRANGEMAX, FALSE, range);
    SendMessageW(c->hwnd, TBM_SETLINESIZE, 0, 1);
    SendMessageW(c->hwnd, TBM_SETPAGESIZE, 0, pageStep);
    if (tickEvery > 0)
        SendMessageW(c->hwnd, TBM_SETTICFREQ, tickEvery, 0);
    _cxuiWin32SliderSetPos(p, min(pos, range));
}

void _cxuiWin32SliderSetPos(CxUiPeer* p, int32 pos)
{
    Win32Control* c = (Win32Control*)p;
    SendMessageW(c->hwnd, TBM_SETPOS, TRUE, vertical(c) ? c->range - pos : pos);
}

// A dialog's trackbar, a little thicker with tick marks
CxUiSize _cxuiWin32SliderMeasure(CxUiWidget* w, int32 length, bool vert, bool ticks)
{
    unused_noeval(w);
    int32 across = ticks ? 30 : 24;
    return vert ? (CxUiSize){ across, length } : (CxUiSize){ length, across };
}

CxUiPeer* _cxuiWin32ProgressCreate(CxUiWidget* w, CxUiPeer* host)
{
    unused_noeval(w);
    Win32Control* c = xaAllocStruct(Win32Control, XA_Zero);
    c->base.kind    = CXUI_PEER_Progress;
    HWND hwnd       = createControl(c, PROGRESS_CLASSW, 0, 0, host);
    if (!hwnd) {
        xaFree(c);
        return NULL;
    }
    SendMessageW(hwnd, PBM_SETRANGE32, 0, 10000);
    return &c->base;
}

void _cxuiWin32ProgressSet(CxUiPeer* p, float64 fraction, bool indeterminate)
{
    Win32Control* c = (Win32Control*)p;
    DWORD style     = (DWORD)GetWindowLongW(c->hwnd, GWL_STYLE);
    if (indeterminate != ((style & PBS_MARQUEE) != 0)) {
        if (!indeterminate)
            SendMessageW(c->hwnd, PBM_SETMARQUEE, FALSE, 0);
        SetWindowLongW(c->hwnd, GWL_STYLE, (LONG)(indeterminate ? style | PBS_MARQUEE : style & ~PBS_MARQUEE));
        if (indeterminate)
            SendMessageW(c->hwnd, PBM_SETMARQUEE, TRUE, 30);
    }
    if (!indeterminate)
        SendMessageW(c->hwnd, PBM_SETPOS, (WPARAM)(fraction * 10000 + 0.5), 0);
}

// ---- all of them ----

// A dropdown's height is its own and its open list's, up to 12 items.
// ---- tabs ----

static COLORREF colorRef(CxUiColor c)
{
    return RGB(c.r, c.g, c.b);
}

static void fillRect(HDC dc, RECT r, COLORREF color)
{
    SetDCBrushColor(dc, color);
    FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}

// Lines along the left, top and right of r, and the bottom too when closed
static void edgeRect(HDC dc, RECT r, COLORREF color, bool closed)
{
    RECT edges[4] = { { r.left, r.top, r.left + 1, r.bottom },
                      { r.left, r.top, r.right, r.top + 1 },
                      { r.right - 1, r.top, r.right, r.bottom },
                      { r.left, r.bottom - 1, r.right, r.bottom } };
    for (int i = 0; i < (closed ? 4 : 3); i++)
        fillRect(dc, edges[i], color);
}

// The tab control has no dark look, so in a dark window it is drawn here: the strip on the color
// around it, the pages' border, the picked tab joined to it in the pages' color, and the tab under
// the mouse in the hover color.
static void tabsPaintDark(Win32Control* c, HDC dc)
{
    RECT client;
    GetClientRect(c->hwnd, &client);
    CxUiTabsColors* k = &c->tabColors;
    fillRect(dc, client, colorRef(k->around));
    int32 n   = (int32)SendMessageW(c->hwnd, TCM_GETITEMCOUNT, 0, 0);
    int32 sel = (int32)SendMessageW(c->hwnd, TCM_GETCURSEL, 0, 0);
    RECT first;
    RECT pane = client;
    if (n > 0 && SendMessageW(c->hwnd, TCM_GETITEMRECT, 0, (LPARAM)&first))
        pane.top = first.bottom;
    fillRect(dc, pane, colorRef(k->page));
    edgeRect(dc, pane, colorRef(k->border), true);

    HGDIOBJ oldFont = SelectObject(dc, (HFONT)SendMessageW(c->hwnd, WM_GETFONT, 0, 0));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, colorRef(k->text));
    for (int32 i = 0; i < n; i++) {
        RECT r;
        if (!SendMessageW(c->hwnd, TCM_GETITEMRECT, i, (LPARAM)&r))
            continue;
        bool picked = i == sel;
        if (picked) {
            // Larger than the others, as Windows draws it, and over the border below it
            InflateRect(&r, 2, 0);
            r.top -= 2;
            r.bottom += 1;
        }
        fillRect(dc, r, colorRef(picked ? k->page : i == c->hotTab ? k->hover : k->around));
        edgeRect(dc, r, colorRef(k->border), false);
        WCHAR buf[256] = { 0 };
        TCITEMW item   = { .mask = TCIF_TEXT, .pszText = buf, .cchTextMax = 255 };
        SendMessageW(c->hwnd, TCM_GETITEMW, i, (LPARAM)&item);
        DrawTextW(dc, item.pszText, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_HIDEPREFIX);
    }
    SelectObject(dc, oldFont);
}

// Drawn off screen, so the strip doesn't flicker as it is filled and drawn over
static void tabsPaint(Win32Control* c, HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem     = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, max(rc.right, 1), max(rc.bottom, 1));
    HGDIOBJ old = SelectObject(mem, bmp);
    tabsPaintDark(c, mem);
    BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top, mem,
           ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static void tabsHot(Win32Control* c, int32 hot)
{
    if (hot == c->hotTab)
        return;
    c->hotTab = hot;
    InvalidateRect(c->hwnd, NULL, FALSE);
}

static LRESULT buttonMessage(Win32Control* c, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

static LRESULT tabsMessage(Win32Control* c, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    bool dark = _cxuiWin32RootDark(hwnd);
    switch (msg) {
    case WM_ERASEBKGND: {
        // The strip beside the tabs shows what is around them.
        RECT rc;
        GetClientRect(hwnd, &rc);
        fillRect((HDC)wp, rc, colorRef(c->tabColors.around));
        return 1;
    }
    case WM_PAINT:
        if (!dark)
            break;
        tabsPaint(c, hwnd);
        return 0;
    case WM_PRINTCLIENT:
        if (!dark)
            break;
        tabsPaintDark(c, (HDC)wp);
        return 0;
    case WM_MOUSEMOVE: {
        if (!dark)
            break;
        TCHITTESTINFO hit = { .pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) } };
        tabsHot(c, (int32)SendMessageW(hwnd, TCM_HITTEST, 0, (LPARAM)&hit));
        if (!c->tracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            c->tracking         = TrackMouseEvent(&tme) != 0;
        }
        break;
    }
    case WM_MOUSELEAVE:
        c->tracking = false;
        tabsHot(c, -1);
        break;
    // The wheel goes on to the window around the pages, whatever the tab control would do.
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        if (_cxuiWin32Wheel(msg, wp, lp))
            return 0;
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return buttonMessage(c, hwnd, msg, wp, lp);
}

static LRESULT CALLBACK tabsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Win32Control* c = controlOf(hwnd);
    if (!c)
        return DefWindowProcW(hwnd, msg, wp, lp);
    _cxuiNativeEnter();
    LRESULT r = tabsMessage(c, hwnd, msg, wp, lp);
    _cxuiNativeLeave();
    return r;
}

CxUiPeer* _cxuiWin32TabsCreate(CxUiTabs* t, CxUiPeer* host)
{
    Win32Control* c = xaAllocStruct(Win32Control, XA_Zero);
    c->base.kind    = CXUI_PEER_Tabs;
    c->hotTab       = -1;
    DWORD style     = WS_TABSTOP | WS_CLIPCHILDREN | TCS_FOCUSONBUTTONDOWN;
    if (CxUiZBox(t)->_flags & CXUI_TABS_Multiline)
        style |= TCS_MULTILINE;
    HWND hwnd = createControl(c, WC_TABCONTROLW, style, 0, host);
    if (!hwnd) {
        xaFree(c);
        return NULL;
    }
    c->page = _cxuiWin32SurfaceCreate(hwnd);
    if (!c->page) {
        RemovePropW(hwnd, propName);
        DestroyWindow(hwnd);
        xaFree(c);
        return NULL;
    }
    // Resizing the control itself to measure it, even unseen, would lose what was waiting to be
    // drawn in the part cut off.
    if (style & TCS_MULTILINE) {
        ULONG_PTR cookie;
        bool v6    = _cxuiWin32Comctl6Begin(&cookie);
        c->measure = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | style & ~WS_TABSTOP, 0, 0, 0, 0, hwnd,
                                     NULL, GetModuleHandleW(NULL), NULL);
        if (v6)
            _cxuiWin32Comctl6End(cookie);
    }
    c->oldProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)tabsProc);
    return &c->base;
}

// A tab's width is measured when its text is set, in the font the control had then.
static void tabsRemeasure(HWND hwnd)
{
    int32 n = (int32)SendMessageW(hwnd, TCM_GETITEMCOUNT, 0, 0);
    for (int32 i = 0; i < n; i++) {
        WCHAR buf[256] = { 0 };
        TCITEMW item   = { .mask = TCIF_TEXT, .pszText = buf, .cchTextMax = 255 };
        SendMessageW(hwnd, TCM_GETITEMW, i, (LPARAM)&item);
        item.pszText = buf;
        SendMessageW(hwnd, TCM_SETITEMW, i, (LPARAM)&item);
    }
}

// The core gives titles without mnemonics; the control would take an `&` in one as a mnemonic,
// and places a title with one off center.
static void tabsItem(Win32Control* c, UINT msg, int32 at, strref title)
{
    string text = 0;
    strReplace(&text, title, _S"&", _S"&&", 0);
    uint16* w    = strToUTF16A(text);
    TCITEMW item = { .mask = TCIF_TEXT, .pszText = w ? (LPWSTR)w : L"" };
    SendMessageW(c->hwnd, msg, at, (LPARAM)&item);
    if (c->measure)
        SendMessageW(c->measure, msg, at, (LPARAM)&item);
    xaFree(w);
    strDestroy(&text);
}

void _cxuiWin32TabsInsert(CxUiPeer* p, int32 at, strref title)
{
    tabsItem((Win32Control*)p, TCM_INSERTITEMW, at, title);
}

void _cxuiWin32TabsRemove(CxUiPeer* p, int32 at)
{
    Win32Control* c = (Win32Control*)p;
    SendMessageW(c->hwnd, TCM_DELETEITEM, at, 0);
    if (c->measure)
        SendMessageW(c->measure, TCM_DELETEITEM, at, 0);
}

void _cxuiWin32TabsSetTitle(CxUiPeer* p, int32 at, strref title)
{
    tabsItem((Win32Control*)p, TCM_SETITEMW, at, title);
}

void _cxuiWin32TabsSetValue(CxUiPeer* p, int32 index)
{
    SendMessageW(((Win32Control*)p)->hwnd, TCM_SETCURSEL, index, 0);
}

void _cxuiWin32TabsSetPage(CxUiPeer* p, CxUiRect px)
{
    _cxuiWin32PanelSetBounds(((Win32Control*)p)->page, px);
}

// Controls shown or made would each be drawn as they come; held, the page is drawn once.
void _cxuiWin32TabsHold(CxUiPeer* p, bool hold)
{
    HWND page = _cxuiWin32PanelHwnd(((Win32Control*)p)->page);
    SendMessageW(page, WM_SETREDRAW, !hold, 0);
    if (!hold)
        RedrawWindow(page, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void _cxuiWin32TabsSetColors(CxUiPeer* p, const CxUiTabsColors* colors)
{
    Win32Control* c = (Win32Control*)p;
    c->tabColors    = *colors;
    _cxuiWin32PanelSetColors(c->page, colors->page, colors->border, 0);
    InvalidateRect(c->hwnd, NULL, TRUE);
}

_Use_decl_annotations_
HWND _cxuiWin32TabsPageHwnd(CxUiPeer* p)
{
    return _cxuiWin32PanelHwnd(((Win32Control*)p)->page);
}

// The control's own room around its display area, and its tabs' widths, once it exists; before,
// an estimate from the font, which realizing it puts right. Tabs on several lines are wrapped at
// `width` by the control itself, which works its lines out at its own width: its hidden copy is
// given that width.
CxUiEdges _cxuiWin32TabsMeasure(CxUiTabs* t, CxUiFont* f, int32 width, int32* stripWidth)
{
    float32 s   = cxuiFontScale(f);
    CxUiPeer* p = CxUiWidget(t)->peer;
    RECT in;
    int32 strip = 0;
    if (p) {
        HWND hwnd    = ((Win32Control*)p)->hwnd;
        HWND measure = ((Win32Control*)p)->measure;
        if (measure && width > 0) {
            RECT was;
            GetWindowRect(hwnd, &was);
            SetWindowPos(measure, NULL, 0, 0, pxRound(width * s), max(was.bottom - was.top, 1),
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        } else {
            measure = hwnd;
        }
        RECT r = { 0, 0, 1000, 1000 };
        SendMessageW(measure, TCM_ADJUSTRECT, FALSE, (LPARAM)&r);
        in = (RECT){ r.left, r.top, 1000 - r.right, 1000 - r.bottom };
        int32 n   = (int32)SendMessageW(hwnd, TCM_GETITEMCOUNT, 0, 0);
        for (int32 i = 0; i < n; i++) {
            RECT ir;
            if (SendMessageW(hwnd, TCM_GETITEMRECT, i, (LPARAM)&ir))
                strip += ir.right - ir.left;
        }
        strip += n > 0 ? pxRound(6 * s) : 0;
    } else {
        // Wrapped, each line takes the tabs that fit after those before
        SIZE ext    = textExtent(f, _S"Ag", 0);
        int32 lines = 1, line = 0, limit = pxRound(width * s);
        for (int32 i = 0; i < saSize(t->_titles); i++) {
            int32 tab = textExtent(f, t->_titles.a[i], 0).cx + pxRound(12 * s);
            if ((CxUiZBox(t)->_flags & CXUI_TABS_Multiline) && limit > 0 && line > 0 && line + tab > limit) {
                lines++;
                line = 0;
            }
            line += tab;
            strip += tab;
        }
        strip += strip > 0 ? pxRound(6 * s) : 0;
        in = (RECT){ pxRound(4 * s), lines * (ext.cy + pxRound(6 * s)) + pxRound(4 * s), pxRound(4 * s), pxRound(4 * s) };
    }
    *stripWidth = (int32)ceilf(strip / s);
    return (CxUiEdges){ (int32)ceilf(in.left / s), (int32)ceilf(in.top / s), (int32)ceilf(in.right / s),
                        (int32)ceilf(in.bottom / s) };
}

void _cxuiWin32ControlSetBounds(CxUiPeer* p, CxUiRect px)
{
    HWND hwnd = ((Win32Control*)p)->hwnd;
    if (p->kind == CXUI_PEER_Dropdown) {
        RECT rc;
        GetWindowRect(hwnd, &rc);
        int32 n     = (int32)SendMessageW(hwnd, CB_GETCOUNT, 0, 0);
        n           = clamp(n, 1, 12);
        int32 itemH = (int32)SendMessageW(hwnd, CB_GETITEMHEIGHT, 0, 0);
        px.h        = (rc.bottom - rc.top) + n * itemH + 2;
    }
    _cxuiWin32MoveChild(GetParent(hwnd), hwnd, px);
}

void _cxuiWin32ControlSetFont(CxUiPeer* p, CxUiFont* f)
{
    Win32Control* c = (Win32Control*)p;
    objAcquire(f);
    SendMessageW(c->hwnd, WM_SETFONT, (WPARAM)_cxuiWin32GdiFont(f, NULL), TRUE);
    if (p->kind == CXUI_PEER_Tabs)
        tabsRemeasure(c->hwnd);
    if (c->measure) {
        SendMessageW(c->measure, WM_SETFONT, (WPARAM)_cxuiWin32GdiFont(f, NULL), FALSE);
        tabsRemeasure(c->measure);
    }
    objRelease(&c->font);
    c->font = f;
}

// Labels are never disabled natively: the core gives them the disabled text color.
void _cxuiWin32PeerSetEnabled(CxUiPeer* p, bool enabled)
{
    if (_cxuiPeerIsControl(p->kind) && p->kind != CXUI_PEER_Label)
        EnableWindow(((Win32Control*)p)->hwnd, enabled);
}

_Use_decl_annotations_
HWND _cxuiWin32ControlHwnd(CxUiPeer* p)
{
    return ((Win32Control*)p)->hwnd;
}

void _cxuiWin32ControlDestroy(CxUiPeer* p)
{
    Win32Control* c = (Win32Control*)p;
    if (c->page)
        _cxuiWin32PanelDestroy(c->page);
    if (c->oldProc)
        SetWindowLongPtrW(c->hwnd, GWLP_WNDPROC, (LONG_PTR)c->oldProc);
    RemovePropW(c->hwnd, propName);
    if (c->dropList)
        RemovePropW(c->dropList, propName);
    DestroyWindow(c->hwnd);
    if (c->bg)
        DeleteObject(c->bg);
    if (c->listBg)
        DeleteObject(c->listBg);
    objRelease(&c->font);
    xaFree(c);
}

// ---- tests ----

bool _cxuiWin32TestControlInfo(CxUiPeer* p, CxUiTestControlInfo* out)
{
    Win32Control* c = (Win32Control*)p;
    int32 n         = GetWindowTextLengthW(c->hwnd);
    WCHAR* buf      = xaAlloc((n + 1) * sizeof(WCHAR));
    n               = GetWindowTextW(c->hwnd, buf, n + 1);
    strFromUTF16(&out->text, (const uint16*)buf, n);
    xaFree(buf);
    LONG style     = GetWindowLongW(c->hwnd, GWL_STYLE);
    out->isDefault = p->kind == CXUI_PEER_Button && (style & BS_TYPEMASK) == BS_DEFPUSHBUTTON;
    if (p->kind == CXUI_PEER_Slider) {
        out->pos      = sliderPos(c);
        out->range    = (int32)SendMessageW(c->hwnd, TBM_GETRANGEMAX, 0, 0);
        out->vertical = vertical(c);
    }
    if (p->kind == CXUI_PEER_Progress) {
        out->range         = 10000;
        out->pos           = (int32)SendMessageW(c->hwnd, PBM_GETPOS, 0, 0);
        out->indeterminate = (style & PBS_MARQUEE) != 0;
    }
    if (p->kind == CXUI_PEER_List || p->kind == CXUI_PEER_Dropdown) {
        bool list = p->kind == CXUI_PEER_List;
        int32 cnt = (int32)SendMessageW(c->hwnd, list ? LB_GETCOUNT : CB_GETCOUNT, 0, 0);
        for (int32 i = 0; i < cnt; i++) {
            int32 len  = (int32)SendMessageW(c->hwnd, list ? LB_GETTEXTLEN : CB_GETLBTEXTLEN, i, 0);
            WCHAR* buf = xaAlloc((max(len, 0) + 1) * sizeof(WCHAR));
            len        = (int32)SendMessageW(c->hwnd, list ? LB_GETTEXT : CB_GETLBTEXT, i, (LPARAM)buf);
            string item = 0;
            strFromUTF16(&item, (const uint16*)buf, max(len, 0));
            strAppend(&out->items, item);
            strAppend(&out->items, _S"\n");
            strDestroy(&item);
            xaFree(buf);
        }
        out->value = (int32)SendMessageW(c->hwnd, list ? LB_GETCURSEL : CB_GETCURSEL, 0, 0);
        if (list)
            out->top = (int32)SendMessageW(c->hwnd, LB_GETTOPINDEX, 0, 0);
        if (c->multi) {
            string at = 0;
            out->value = -1;
            for (int32 i = 0; i < cnt; i++) {
                if (SendMessageW(c->hwnd, LB_GETSEL, i, 0) <= 0)
                    continue;
                if (out->value < 0)
                    out->value = i;
                strFormat(&at, _S"${int} ", stvar(int32, i));
                strAppend(&out->selection, at);
            }
            strDestroy(&at);
        }
    }
    if (p->kind == CXUI_PEER_Tabs) {
        int32 cnt = (int32)SendMessageW(c->hwnd, TCM_GETITEMCOUNT, 0, 0);
        for (int32 i = 0; i < cnt; i++) {
            WCHAR buf[256] = { 0 };
            TCITEMW item   = { .mask = TCIF_TEXT, .pszText = buf, .cchTextMax = 255 };
            SendMessageW(c->hwnd, TCM_GETITEMW, i, (LPARAM)&item);
            string raw = 0, title = 0;
            strFromUTF16(&raw, (const uint16*)item.pszText, (int32)wcslen(item.pszText));
            _cxuiMnemonicStrip(&title, raw);
            strAppend(&out->items, title);
            strAppend(&out->items, _S"\n");
            strDestroy(&raw);
            strDestroy(&title);
        }
        out->value = (int32)SendMessageW(c->hwnd, TCM_GETCURSEL, 0, 0);
        RECT rc, pr;
        GetClientRect(c->hwnd, &rc);
        SendMessageW(c->hwnd, TCM_ADJUSTRECT, FALSE, (LPARAM)&rc);
        out->display = (CxUiRect){ rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top };
        HWND page    = _cxuiWin32PanelHwnd(c->page);
        GetWindowRect(page, &pr);
        MapWindowPoints(NULL, c->hwnd, (POINT*)&pr, 2);
        out->page = (CxUiRect){ pr.left, pr.top, pr.right - pr.left, pr.bottom - pr.top };
    }
    if (isCheck(c) && c->ownerDraw) {
        out->checkState = c->checkState;
    } else if (isCheck(c)) {
        LRESULT st      = SendMessageW(c->hwnd, BM_GETCHECK, 0, 0);
        out->checkState = st == BST_CHECKED ? CXUI_CHECK_On : st == BST_INDETERMINATE ? CXUI_CHECK_Mixed : CXUI_CHECK_Off;
    }
    out->enabled   = IsWindowEnabled(c->hwnd) != 0;
    out->visible   = (style & WS_VISIBLE) != 0;
    out->focused   = GetFocus() == c->hwnd;
    RECT rc;
    GetWindowRect(c->hwnd, &rc);
    MapWindowPoints(NULL, GetParent(c->hwnd), (POINT*)&rc, 2);
    out->bounds = (CxUiRect){ rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top };
    if (p->kind == CXUI_PEER_Label && c->font && (style & SS_TYPEMASK) != SS_LEFTNOWORDWRAP) {
        string text = 0;
        strDup(&text, out->text);
        out->neededHeight = textExtent(c->font, text, max(out->bounds.w, 1)).cy;
        strDestroy(&text);
    }
    return true;
}

// The click a mouse gives, which a disabled control doesn't take
bool _cxuiWin32TestClick(CxUiPeer* p)
{
    if (!_cxuiPeerIsControl(p->kind) || p->kind == CXUI_PEER_Label)
        return false;
    SendMessageW(((Win32Control*)p)->hwnd, BM_CLICK, 0, 0);
    return true;
}

// A selection as the user makes one, with the notification the control would send, and a
// double click's; a disabled control takes neither. A tab is clicked.
bool _cxuiWin32TestChoose(CxUiPeer* p, int32 index, bool activate)
{
    Win32Control* c = (Win32Control*)p;
    if (p->kind != CXUI_PEER_List && p->kind != CXUI_PEER_Dropdown && p->kind != CXUI_PEER_Tabs)
        return false;
    if (!IsWindowEnabled(c->hwnd) || !IsWindowVisible(c->hwnd))
        return true;
    if (p->kind == CXUI_PEER_Tabs) {
        RECT r;
        if (!SendMessageW(c->hwnd, TCM_GETITEMRECT, index, (LPARAM)&r))
            return true;
        LPARAM at = MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2);
        SendMessageW(c->hwnd, WM_LBUTTONDOWN, MK_LBUTTON, at);
        SendMessageW(c->hwnd, WM_LBUTTONUP, 0, at);
        return true;
    }
    bool list   = p->kind == CXUI_PEER_List;
    HWND parent = GetParent(c->hwnd);
    WORD id     = (WORD)GetDlgCtrlID(c->hwnd);
    if (c->multi) {
        // A plain click selects the item alone, with the caret on it.
        SendMessageW(c->hwnd, LB_SETSEL, FALSE, -1);
        SendMessageW(c->hwnd, LB_SETSEL, TRUE, index);
        SendMessageW(c->hwnd, LB_SETCARETINDEX, index, FALSE);
    } else {
        SendMessageW(c->hwnd, list ? LB_SETCURSEL : CB_SETCURSEL, index, 0);
    }
    SendMessageW(parent, WM_COMMAND, MAKEWPARAM(id, list ? LBN_SELCHANGE : CBN_SELCHANGE), (LPARAM)c->hwnd);
    if (activate && list)
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(id, LBN_DBLCLK), (LPARAM)c->hwnd);
    return true;
}

// A Ctrl-click: the item's selection changes and the caret goes to it, with the notification
bool _cxuiWin32TestToggle(CxUiPeer* p, int32 index)
{
    Win32Control* c = (Win32Control*)p;
    if (p->kind != CXUI_PEER_List || !c->multi)
        return false;
    if (!IsWindowEnabled(c->hwnd) || !IsWindowVisible(c->hwnd))
        return true;
    LRESULT on = SendMessageW(c->hwnd, LB_GETSEL, index, 0);
    SendMessageW(c->hwnd, LB_SETSEL, on <= 0, index);
    SendMessageW(c->hwnd, LB_SETCARETINDEX, index, FALSE);
    SendMessageW(GetParent(c->hwnd), WM_COMMAND, MAKEWPARAM((WORD)GetDlgCtrlID(c->hwnd), LBN_SELCHANGE), (LPARAM)c->hwnd);
    return true;
}

// A drag of the thumb as the mouse makes one, with the notifications the trackbar sends, letting go
// with release; a disabled trackbar takes none.
bool _cxuiWin32TestSlide(CxUiPeer* p, int32 pos, bool release)
{
    Win32Control* c = (Win32Control*)p;
    if (p->kind != CXUI_PEER_Slider)
        return false;
    if (!IsWindowEnabled(c->hwnd) || !IsWindowVisible(c->hwnd))
        return true;
    int32 native = vertical(c) ? c->range - pos : pos;
    UINT msg     = vertical(c) ? WM_VSCROLL : WM_HSCROLL;
    HWND parent  = GetParent(c->hwnd);
    SendMessageW(c->hwnd, TBM_SETPOS, TRUE, native);
    SendMessageW(parent, msg, MAKEWPARAM(TB_THUMBTRACK, native), (LPARAM)c->hwnd);
    if (release) {
        SendMessageW(parent, msg, MAKEWPARAM(TB_THUMBPOSITION, native), (LPARAM)c->hwnd);
        SendMessageW(parent, msg, MAKEWPARAM(TB_ENDTRACK, 0), (LPARAM)c->hwnd);
    }
    return true;
}
