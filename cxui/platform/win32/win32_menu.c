#include "win32_ui.h"

#include <cx/utils/lazyinit.h>

// Each CxUiMenu is an HMENU that reports choices by position (MNS_NOTIFYBYPOS, WM_MENUCOMMAND),
// so items need no command IDs; the menu's data points back to its peer.

typedef struct Win32Menu {
    CxUiPeer base;
    HMENU menu;
    bool bar;
    // The window it is the menu bar of
    HWND owner;
} Win32Menu;

static Win32Menu* menuOfHandle(HMENU h)
{
    MENUINFO mi = { .cbSize = sizeof(mi), .fMask = MIM_MENUDATA };
    return h && GetMenuInfo(h, &mi) ? (Win32Menu*)mi.dwMenuData : NULL;
}

CxUiPeer* _cxuiWin32MenuCreate(CxUiMenu* m, bool bar)
{
    unused_noeval(m);
    HMENU h = bar ? CreateMenu() : CreatePopupMenu();
    if (!h)
        return NULL;
    Win32Menu* p = xaAllocStruct(Win32Menu, XA_Zero);
    p->base.kind = CXUI_PEER_Menu;
    p->menu      = h;
    p->bar       = bar;
    MENUINFO mi  = { .cbSize = sizeof(mi), .fMask = MIM_STYLE | MIM_MENUDATA, .dwStyle = MNS_NOTIFYBYPOS,
                     .dwMenuData = (ULONG_PTR)p };
    SetMenuInfo(h, &mi);
    return &p->base;
}

// The title with its hint after a tab, which Windows shows at the right
static wchar_t* itemText(CxUiMenuItem* item)
{
    string s = 0;
    if (strEmpty(item->_hint))
        strDup(&s, item->_title);
    else
        strNConcat(&s, item->_title, _S"\t", item->_hint);
    uint16* w = strToUTF16A(s);
    strDestroy(&s);
    return (wchar_t*)w;
}

// Fills mii for the item; the text, if any, is the caller's to free
static void itemInfo(MENUITEMINFOW* mii, CxUiMenuItem* item, wchar_t** text)
{
    *mii   = (MENUITEMINFOW){ .cbSize = sizeof(*mii), .fMask = MIIM_FTYPE | MIIM_STATE | MIIM_SUBMENU | MIIM_DATA };
    *text  = NULL;
    // The kind, which Windows doesn't keep for check items, for tests to read back
    mii->dwItemData = item->_kind;
    if (item->_kind == CXUI_MENU_Separator) {
        mii->fType = MFT_SEPARATOR;
        return;
    }
    mii->fMask |= MIIM_STRING;
    *text           = itemText(item);
    mii->dwTypeData = *text;
    mii->fType      = item->_kind == CXUI_MENU_Radio ? MFT_RADIOCHECK : MFT_STRING;
    mii->fState     = (item->_checked ? MFS_CHECKED : 0) | ((CxUiWidget(item)->state & CXUI_WS_Inactive) ? MFS_DISABLED : 0);
    CxUiPeer* sub   = item->_submenu ? CxUiWidget(item->_submenu)->peer : NULL;
    mii->hSubMenu   = sub ? ((Win32Menu*)sub)->menu : NULL;
}

static void barChanged(Win32Menu* p)
{
    if (p->bar && p->owner)
        DrawMenuBar(p->owner);
}

void _cxuiWin32MenuInsert(CxUiPeer* peer, int32 at, CxUiMenuItem* item)
{
    Win32Menu* p = (Win32Menu*)peer;
    MENUITEMINFOW mii;
    wchar_t* text;
    itemInfo(&mii, item, &text);
    InsertMenuItemW(p->menu, (UINT)at, TRUE, &mii);
    xaFree(text);
    barChanged(p);
}

// RemoveMenu, not DeleteMenu: a submenu belongs to its own peer.
void _cxuiWin32MenuRemove(CxUiPeer* peer, int32 at)
{
    Win32Menu* p = (Win32Menu*)peer;
    RemoveMenu(p->menu, (UINT)at, MF_BYPOSITION);
    barChanged(p);
}

void _cxuiWin32MenuUpdate(CxUiPeer* peer, int32 at, CxUiMenuItem* item)
{
    Win32Menu* p = (Win32Menu*)peer;
    MENUITEMINFOW mii;
    wchar_t* text;
    itemInfo(&mii, item, &text);
    SetMenuItemInfoW(p->menu, (UINT)at, TRUE, &mii);
    xaFree(text);
    barChanged(p);
}

// DestroyMenu takes the submenus with it, which have peers of their own, so they are let go of
// first; and a window destroys the menu bar it still has, so it is taken off.
void _cxuiWin32MenuDestroy(CxUiPeer* peer)
{
    Win32Menu* p = (Win32Menu*)peer;
    for (int i = GetMenuItemCount(p->menu) - 1; i >= 0; i--)
        RemoveMenu(p->menu, (UINT)i, MF_BYPOSITION);
    if (p->owner && GetMenu(p->owner) == p->menu)
        SetMenu(p->owner, NULL);
    DestroyMenu(p->menu);
    xaFree(p);
}

_Use_decl_annotations_
void _cxuiWin32WindowAttachMenu(HWND hwnd, CxUiPeer* menu)
{
    Win32Menu* old = menuOfHandle(GetMenu(hwnd));
    if (old)
        old->owner = NULL;
    Win32Menu* p = (Win32Menu*)menu;
    if (p)
        p->owner = hwnd;
    SetMenu(hwnd, p ? p->menu : NULL);
}

_Use_decl_annotations_
void _cxuiWin32MenuCommand(HMENU h, int32 at)
{
    Win32Menu* p   = menuOfHandle(h);
    CxUiWidget* it = p && p->base.w ? _cxuiContainerChild(p->base.w, at) : NULL;
    CxUiMenuItem* item = it ? objDynCast(CxUiMenuItem, it) : NULL;
    if (item)
        _cxuiMenuChosen(item);
}

int32 _cxuiWin32TestMenuCount(CxUiPeer* peer)
{
    return GetMenuItemCount(((Win32Menu*)peer)->menu);
}

bool _cxuiWin32TestMenuItem(CxUiPeer* peer, int32 at, CxUiTestMenuItemInfo* out)
{
    HMENU h           = ((Win32Menu*)peer)->menu;
    MENUITEMINFOW mii = { .cbSize = sizeof(mii), .fMask = MIIM_FTYPE | MIIM_STATE | MIIM_SUBMENU | MIIM_DATA | MIIM_STRING };
    if (!GetMenuItemInfoW(h, (UINT)at, TRUE, &mii))
        return false;
    out->kind    = (uint32)mii.dwItemData;
    out->checked = (mii.fState & MFS_CHECKED) != 0;
    out->enabled = !(mii.fState & MFS_DISABLED);
    out->submenu = mii.hSubMenu != NULL;
    if (mii.fType & MFT_SEPARATOR)
        return true;
    wchar_t* text   = xaAlloc((mii.cch + 1) * sizeof(wchar_t), XA_Zero);
    mii.dwTypeData  = text;
    mii.cch        += 1;
    mii.fMask       = MIIM_STRING;
    GetMenuItemInfoW(h, (UINT)at, TRUE, &mii);
    string all = 0;
    strFromUTF16(&all, (const uint16*)text, wcslen(text));
    xaFree(text);
    int32 tab = strFind(all, 0, _S"\t");
    if (tab >= 0) {
        strSubStr(&out->title, all, 0, tab);
        strSubStr(&out->hint, all, tab + 1, strEnd);
    } else {
        strDup(&out->title, all);
    }
    strDestroy(&all);
    return true;
}

// WM_MENUCOMMAND as Windows sends it, to the window the menu is in, for an item it would send it
// for
bool _cxuiWin32TestMenuChoose(CxUiPeer* peer, int32 at)
{
    Win32Menu* p    = (Win32Menu*)peer;
    CxUiWindow* win = p->base.w ? _cxuiWindowOf(p->base.w) : NULL;
    HWND hwnd       = win && CxUiWidget(win)->peer ? _cxuiWin32PeerHwnd(CxUiWidget(win)->peer) : NULL;
    if (!hwnd)
        return false;
    MENUITEMINFOW mii = { .cbSize = sizeof(mii), .fMask = MIIM_FTYPE | MIIM_STATE | MIIM_SUBMENU };
    if (!GetMenuItemInfoW(p->menu, (UINT)at, TRUE, &mii))
        return false;
    if (!(mii.fState & MFS_DISABLED) && !(mii.fType & MFT_SEPARATOR) && !mii.hSubMenu)
        SendMessageW(hwnd, WM_MENUCOMMAND, (WPARAM)at, (LPARAM)p->menu);
    return true;
}

CxUiPeer* _cxuiWin32TestWindowMenu(CxUiPeer* win)
{
    Win32Menu* p = menuOfHandle(GetMenu(_cxuiWin32PeerHwnd(win)));
    return p ? &p->base : NULL;
}

// ---- dark menus ----

// The menu bar has no dark theme. Windows asks a window to draw its bar through the undocumented
// WM_UAHDRAWMENU and WM_UAHDRAWMENUITEM, with these structures, and paints a light line under it
// afterwards that is painted over. The menus that drop down follow uxtheme's undocumented
// preferred app mode (ordinal 135; AllowDarkModeForApp before 1903), set as each window's menus
// open, and FlushMenuThemes (ordinal 136). Both are Windows 10 1809 and later; the ordinals mean
// other things before, so nothing is looked up on older builds.
#define CXUI_WM_UAHDRAWMENU     0x0091
#define CXUI_WM_UAHDRAWMENUITEM 0x0092

typedef union UahMenuItemMetrics {
    struct {
        DWORD cx, cy;
    } rgsizeBar[2];
    struct {
        DWORD cx, cy;
    } rgsizePopup[4];
} UahMenuItemMetrics;

typedef struct UahMenuPopupMetrics {
    DWORD rgcx[4];
    DWORD fUpdateMaxWidths : 2;
} UahMenuPopupMetrics;

typedef struct UahMenu {
    HMENU hmenu;
    HDC hdc;
    DWORD dwFlags;
} UahMenu;

typedef struct UahMenuItem {
    int iPosition;
    UahMenuItemMetrics umim;
    UahMenuPopupMetrics umpm;
} UahMenuItem;

typedef struct UahDrawMenuItem {
    DRAWITEMSTRUCT dis;
    UahMenu um;
    UahMenuItem umi;
} UahDrawMenuItem;

typedef LONG(WINAPI* RtlGetVersionFn)(OSVERSIONINFOW* info);
typedef int(WINAPI* SetPreferredAppModeFn)(int mode);
typedef void(WINAPI* FlushMenuThemesFn)(void);

#define CXUI_APPMODE_FORCEDARK  2
#define CXUI_APPMODE_FORCELIGHT 3

static SetPreferredAppModeFn pSetPreferredAppMode;
static FlushMenuThemesFn pFlushMenuThemes;
static DWORD windowsBuild;
static LazyInitState darkMenuInit;

static void darkMenuInitOnce(void* unused)
{
    unused_noeval(unused);
    HMODULE nt                = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionFn getVersion = nt ? (RtlGetVersionFn)GetProcAddress(nt, "RtlGetVersion") : NULL;
    OSVERSIONINFOW info        = { .dwOSVersionInfoSize = sizeof(info) };
    if (!getVersion || getVersion(&info) != 0 || info.dwMajorVersion < 10 || info.dwBuildNumber < 17763)
        return;
    windowsBuild = info.dwBuildNumber;
    HMODULE ux   = LoadLibraryW(L"uxtheme.dll");
    if (!ux)
        return;
    pSetPreferredAppMode = (SetPreferredAppModeFn)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    pFlushMenuThemes     = (FlushMenuThemesFn)GetProcAddress(ux, MAKEINTRESOURCEA(136));
}

// Before 1903 the same ordinal takes a BOOL, which any mode but light turns on.
static void menusFollow(bool dark)
{
    lazyInit(&darkMenuInit, darkMenuInitOnce, NULL);
    if (!pSetPreferredAppMode || !pFlushMenuThemes)
        return;
    if (windowsBuild >= 18362)
        pSetPreferredAppMode(dark ? CXUI_APPMODE_FORCEDARK : CXUI_APPMODE_FORCELIGHT);
    else
        pSetPreferredAppMode(dark);
    pFlushMenuThemes();
}

// The light line Windows draws between the bar and the client area
static void barLine(HWND hwnd, COLORREF color)
{
    MENUBARINFO mbi = { .cbSize = sizeof(mbi) };
    if (!GetMenu(hwnd) || !GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi))
        return;
    RECT client, win;
    GetClientRect(hwnd, &client);
    MapWindowPoints(hwnd, NULL, (POINT*)&client, 2);
    GetWindowRect(hwnd, &win);
    OffsetRect(&client, -win.left, -win.top);
    RECT line   = { client.left, client.top - 1, client.right, client.top };
    HDC dc      = GetWindowDC(hwnd);
    HBRUSH b    = CreateSolidBrush(color);
    FillRect(dc, &line, b);
    DeleteObject(b);
    ReleaseDC(hwnd, dc);
}

_Use_decl_annotations_
void _cxuiWin32MenuBarRedraw(HWND hwnd)
{
    MENUBARINFO mbi = { .cbSize = sizeof(mbi) };
    if (!GetMenu(hwnd) || !GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi))
        return;
    // Client coordinates, so the bar is above zero; RDW_FRAME lets the rectangle reach it.
    MapWindowPoints(NULL, hwnd, (POINT*)&mbi.rcBar, 2);
    mbi.rcBar.bottom++;
    RedrawWindow(hwnd, &mbi.rcBar, NULL, RDW_INVALIDATE | RDW_FRAME);
}

static COLORREF mix(COLORREF a, COLORREF b, int bPercent)
{
    return RGB((GetRValue(a) * (100 - bPercent) + GetRValue(b) * bPercent) / 100,
               (GetGValue(a) * (100 - bPercent) + GetGValue(b) * bPercent) / 100,
               (GetBValue(a) * (100 - bPercent) + GetBValue(b) * bPercent) / 100);
}

_Use_decl_annotations_
bool _cxuiWin32MenuMessage(HWND hwnd, bool dark, COLORREF bg, UINT msg, WPARAM wp, LPARAM lp, LRESULT* out)
{
    switch (msg) {
    case WM_ENTERMENULOOP:
    case WM_INITMENUPOPUP:
        menusFollow(dark);
        return false;
    case CXUI_WM_UAHDRAWMENU: {
        if (!dark)
            return false;
        UahMenu* um     = (UahMenu*)lp;
        MENUBARINFO mbi = { .cbSize = sizeof(mbi) };
        RECT win;
        if (!GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi) || !GetWindowRect(hwnd, &win))
            return false;
        OffsetRect(&mbi.rcBar, -win.left, -win.top);
        HBRUSH b = CreateSolidBrush(bg);
        FillRect(um->hdc, &mbi.rcBar, b);
        DeleteObject(b);
        *out = TRUE;
        return true;
    }
    case CXUI_WM_UAHDRAWMENUITEM: {
        if (!dark)
            return false;
        UahDrawMenuItem* di = (UahDrawMenuItem*)lp;
        wchar_t text[256]   = { 0 };
        MENUITEMINFOW mii   = { .cbSize = sizeof(mii), .fMask = MIIM_STRING, .dwTypeData = text, .cch = 255 };
        GetMenuItemInfoW(di->um.hmenu, (UINT)di->umi.iPosition, TRUE, &mii);
        UINT state       = di->dis.itemState;
        COLORREF text_fg = (state & (ODS_GRAYED | ODS_DISABLED)) ? RGB(0x65, 0x68, 0x6c) : RGB(0xe6, 0xe6, 0xe6);
        COLORREF fill    = (state & ODS_SELECTED)   ? mix(bg, RGB(0xff, 0xff, 0xff), 18)
                           : (state & ODS_HOTLIGHT) ? mix(bg, RGB(0xff, 0xff, 0xff), 10)
                                                    : bg;
        HBRUSH b         = CreateSolidBrush(fill);
        FillRect(di->um.hdc, &di->dis.rcItem, b);
        DeleteObject(b);
        UINT flags = DT_CENTER | DT_SINGLELINE | DT_VCENTER | ((state & ODS_NOACCEL) ? DT_HIDEPREFIX : 0);
        SetBkMode(di->um.hdc, TRANSPARENT);
        SetTextColor(di->um.hdc, text_fg);
        DrawTextW(di->um.hdc, text, -1, &di->dis.rcItem, flags);
        *out = TRUE;
        return true;
    }
    case WM_NCPAINT:
    case WM_NCACTIVATE:
        if (!dark || !GetMenu(hwnd))
            return false;
        *out = DefWindowProcW(hwnd, msg, wp, lp);
        barLine(hwnd, bg);
        return true;
    default:
        return false;
    }
}
