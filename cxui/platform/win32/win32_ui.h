#pragma once

// Every Win32 backend file includes this first: cx refuses to be included after windows.h, and
// cx/platform/win.h defines WIN32_LEAN_AND_MEAN before including it.

#include <cx/platform/win.h>

#include "../../core/cxuidraw_private.h"

CX_C_BEGIN

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#define CXUI_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif

// A GDI canvas's back buffer: a top-down 32-bit DIB section selected into a memory DC. The GDI
// target starts with one, and its canvases present from it.
typedef struct Win32BackBuffer {
    HDC dc;
    HBITMAP bmp;
    HGDIOBJ oldBmp;
    void* bits;      // BGRX rows of alloc.w pixels
    CxUiSize size;   // in use
    CxUiSize alloc;  // allocated, with slack
} Win32BackBuffer;

// win32_window.c
bool _cxuiWin32WindowInit(HINSTANCE inst);
CxUiPeer* _cxuiWin32WindowCreate(CxUiWindow* w);
void _cxuiWin32WindowShow(CxUiPeer* p, uint32 mode);
void _cxuiWin32WindowHide(CxUiPeer* p);
void _cxuiWin32WindowSetTitle(CxUiPeer* p, strref title);
void _cxuiWin32WindowSetSize(CxUiPeer* p, CxUiSize dips);
void _cxuiWin32WindowSetMinSize(CxUiPeer* p, CxUiSize dips);
void _cxuiWin32WindowSetResizable(CxUiPeer* p, bool resizable);
bool _cxuiWin32WindowBeginDrag(CxUiPeer* p);
void _cxuiWin32WindowLayoutBegin(CxUiPeer* p);
void _cxuiWin32WindowLayoutEnd(CxUiPeer* p);
// Moves a child of one of our windows, inside the window's layout batch if one is open
void _cxuiWin32MoveChild(_In_opt_ HWND parent, HWND child, CxUiRect px);
void _cxuiWin32PeerSetVisible(CxUiPeer* p, bool visible);
void _cxuiWin32PeerRaise(CxUiPeer* p);
void _cxuiWin32WindowSetBackground(CxUiPeer* p, CxUiColor c);
CxUiColor _cxuiWin32SystemPanelColor(void);
bool _cxuiWin32TestPeerShown(CxUiPeer* p, bool* shown);
bool _cxuiWin32TestFrontmost(CxUiPeer* win, CxUiPeer* p, CxUiPoint px, bool* front);
bool _cxuiWin32TestWindowActive(CxUiPeer* p, bool* active);
bool _cxuiWin32WindowGetPosition(CxUiPeer* p, CxUiPoint* out);
bool _cxuiWin32WindowSetPosition(CxUiPeer* p, CxUiPoint pos);
void _cxuiWin32WindowSetDark(CxUiPeer* p, bool dark);
CxUiThemeMode _cxuiWin32SystemTheme(void);
bool _cxuiWin32TestFrameDark(CxUiPeer* p, bool* dark);
bool _cxuiWin32TestFocusCues(CxUiPeer* p, bool hide, bool* shown);
void _cxuiWin32WindowFocusCues(CxUiPeer* win);
bool _cxuiWin32TestBorderless(CxUiPeer* p, bool* borderless);
HWND _cxuiWin32PeerHwnd(CxUiPeer* p);
void _cxuiWin32WindowBeginModal(CxUiPeer* p, CxUiPeer* owner);
void _cxuiWin32WindowEndModal(CxUiPeer* p);
void _cxuiWin32WindowSetOwner(CxUiPeer* p, CxUiPeer* owner);
bool _cxuiWin32WindowMoveNear(CxUiPeer* p, CxUiPeer* other, uint32 where);
// Moves any top-level window by other (CxUiNearPlaces), or with other NULL or minimized centers it
// on the screen; frames are taken as they are seen.
void _cxuiWin32MoveNear(HWND hwnd, HWND other, uint32 where);
bool _cxuiWin32ColorDialog(const CxUiColorDialogDesc* desc, CxUiPeer* parent, CxUiColor* color);
bool _cxuiWin32TestAnswerColorDialog(const CxUiColor* color);
bool _cxuiWin32TestOwnedBy(CxUiPeer* p, CxUiPeer* owner, bool* owned);
void _cxuiWin32WindowSetEnabled(CxUiPeer* p, bool enabled);
void _cxuiWin32PeerDestroy(CxUiPeer* p);
float32 _cxuiWin32SystemScale(void);
float32 _cxuiWin32MonitorScale(HMONITOR mon);
void _cxuiWin32WindowGetPlacement(CxUiPeer* p, CxUiPlacement* out);
void _cxuiWin32WindowSetPlacement(CxUiPeer* p, const CxUiPlacement* pl, bool withState);
bool _cxuiWin32WindowSetFullscreen(CxUiPeer* p, uint32 mode, const CxUiMonitor* mon);
int32 _cxuiWin32WindowMonitorOf(CxUiPeer* p);
bool _cxuiWin32WindowSetTopmost(CxUiPeer* p, bool on);
bool _cxuiWin32TestTopmost(CxUiPeer* p, bool* topmost);

// win32_init.c
// The hidden window that wakes the loop; it also owns the clipboard
HWND _cxuiWin32DispatchWindow(void);

// win32_desktop.c
void _cxuiWin32Monitors(sa_CxUiMonitor* out);
// The index of the monitor with most of the window in what _cxuiWin32Monitors gives, or -1
int32 _cxuiWin32WindowMonitor(HWND hwnd);
// The displays changed; called by each top-level window, it notifies once
void _cxuiWin32DisplayChanged(void);
bool _cxuiWin32ClipboardSetText(_In_opt_ strref text);
bool _cxuiWin32ClipboardGetText(_Inout_ string* out);
bool _cxuiWin32TestClipboardForeign(_In_opt_ strref text);
bool _cxuiWin32TestClipboardRaw(_Inout_ string* out);
bool _cxuiWin32TestForceDpi(CxUiPeer* p, float32 scale);

// win32_canvas.c
bool _cxuiWin32CanvasInit(HINSTANCE inst);
CxUiPeer* _cxuiWin32CanvasCreate(CxUiCanvas* cv, CxUiPeer* parent);
void _cxuiWin32CanvasSetBounds(CxUiPeer* p, CxUiRect px);
void _cxuiWin32CanvasInvalidate(CxUiPeer* p, CxUiRect px);
void _cxuiWin32CanvasPaintNow(CxUiPeer* p);
void _cxuiWin32CanvasDestroy(CxUiPeer* p);
bool _cxuiWin32TestExpose(CxUiPeer* p);
bool _cxuiWin32TestScreenOrigin(CxUiPeer* p, CxUiPoint* out);
void _cxuiWin32CanvasCapture(CxUiPeer* p, bool on);
void _cxuiWin32CanvasSetCursor(CxUiPeer* p, uint32 cursor);
void _cxuiWin32CanvasSetCursorImage(CxUiPeer* p, CxUiCursor* cursor);
bool _cxuiWin32CanvasWarpPointer(CxUiPeer* p, CxUiPoint px);
bool _cxuiWin32CanvasSetTextInput(CxUiPeer* p, flags_t flags);
void _cxuiWin32CanvasSetTextCaret(CxUiPeer* p, CxUiRect px);
bool _cxuiWin32CanvasSetKeyCapture(CxUiPeer* p, flags_t flags);
uint32 _cxuiWin32CanvasPointerLock(CxUiPeer* p, uint32 mode, CxUiPoint px);
// Our canvas under a screen point, or NULL
CxUiPeer* _cxuiWin32CanvasAt(POINT screen);
HWND _cxuiWin32CanvasHwnd(_In_ CxUiPeer* p);
bool _cxuiWin32SurfaceGet(CxUiPeer* p, CxUiNativeSurface* out);
bool _cxuiWin32TestSurfaceInfo(CxUiPeer* p, CxUiTestSurfaceInfo* out);
bool _cxuiWin32TestSurfaceMark(CxUiPeer* p);
CxUiPeer* _cxuiWin32PanelCreate(CxUiWidget* w, CxUiPeer* host);
void _cxuiWin32PanelSetBounds(CxUiPeer* p, CxUiRect px);
void _cxuiWin32PanelSetColors(CxUiPeer* p, CxUiColor bg, CxUiColor border, int32 borderPx);
void _cxuiWin32PanelDestroy(CxUiPeer* p);
HWND _cxuiWin32PanelHwnd(_In_ CxUiPeer* p);
// A panel's surface inside parent for a control to hold its children on, which has no widget of
// its own; destroyed with _cxuiWin32PanelDestroy
_Ret_maybenull_ CxUiPeer* _cxuiWin32SurfaceCreate(_In_ HWND parent);

// win32_text.c
CxUiPeer* _cxuiWin32TextCreate(CxUiTextField* t, CxUiPeer* host, bool framed);
void _cxuiWin32TextDestroy(CxUiPeer* p);
HWND _cxuiWin32TextHwnd(_In_ CxUiPeer* p);
CxUiSize _cxuiWin32TextAreaMeasure(CxUiTextArea* ta, CxUiFont* f, int32 columns, int32 lines, bool framed);
void _cxuiWin32TextAppend(CxUiPeer* p, strref text);
void _cxuiWin32TextScrollToEnd(CxUiPeer* p);
void _cxuiWin32TextSetBounds(CxUiPeer* p, CxUiRect px);
CxUiSize _cxuiWin32TextMeasure(CxUiTextField* t, CxUiFont* f, int32 columns, bool framed);
void _cxuiWin32TextSetText(CxUiPeer* p, strref text);
void _cxuiWin32TextGetSelection(CxUiPeer* p, int32* start, int32* end, int32* caret);
void _cxuiWin32TextSetSelection(CxUiPeer* p, int32 start, int32 end);
void _cxuiWin32TextSetCue(CxUiPeer* p, strref cue);
void _cxuiWin32TextSetMaxLength(CxUiPeer* p, int32 maxLength);
void _cxuiWin32TextSetFont(CxUiPeer* p, CxUiFont* f);
void _cxuiWin32TextSetState(CxUiPeer* p, bool readOnly, bool enabled, bool focusable);
void _cxuiWin32TextSetColors(CxUiPeer* p, const CxUiTextColors* c);
void _cxuiWin32TextSetDarkColors(CxUiPeer* p, const CxUiTextColors* c);
bool _cxuiWin32TestTextInfo(CxUiPeer* p, CxUiTestTextInfo* out);
bool _cxuiWin32TestType(CxUiPeer* p, strref s);
// A parent's WM_COMMAND or WM_CTLCOLOR* from one of our text fields; true with *result if handled
bool _cxuiWin32TextParentMessage(UINT msg, WPARAM wp, LPARAM lp, _Out_ LRESULT* result);

// win32_controls.c
CxUiPeer* _cxuiWin32LabelCreate(CxUiLabel* l, CxUiPeer* host);
void _cxuiWin32LabelSetText(CxUiPeer* p, strref text);
void _cxuiWin32LabelSetStyle(CxUiPeer* p, uint32 align, uint32 ellipsis, bool wrap);
int32 _cxuiWin32LabelHeightFor(CxUiLabel* l, CxUiFont* f, strref text, int32 width);
void _cxuiWin32LabelSetColor(CxUiPeer* p, CxUiColor color);
CxUiSize _cxuiWin32LabelMeasure(CxUiLabel* l, CxUiFont* f, strref text);
CxUiPeer* _cxuiWin32ButtonCreate(CxUiButton* b, CxUiPeer* host);
void _cxuiWin32ButtonSetTitle(CxUiPeer* p, strref title);
void _cxuiWin32ButtonSetDefault(CxUiPeer* p, bool isDefault);
CxUiSize _cxuiWin32ButtonMeasure(CxUiButton* b, CxUiFont* f, strref title);
CxUiPeer* _cxuiWin32CheckCreate(CxUiWidget* w, CxUiPeer* host, bool radio);
void _cxuiWin32CheckSetState(CxUiPeer* p, uint32 state);
CxUiSize _cxuiWin32CheckMeasure(CxUiWidget* w, CxUiFont* f, strref title, bool radio);
CxUiPeer* _cxuiWin32ListCreate(CxUiWidget* w, CxUiPeer* host, bool dropdown);
void _cxuiWin32ListSplice(CxUiPeer* p, int32 at, int32 removed, const string* items, int32 added);
void _cxuiWin32ListSetValue(CxUiPeer* p, int32 value);
void _cxuiWin32ListShow(CxUiPeer* p, int32 index);
CxUiSize _cxuiWin32ListMeasure(CxUiWidget* w, CxUiFont* f, int32 widest, int32 rows, bool dropdown);
void _cxuiWin32ListSetColors(CxUiPeer* p, CxUiColor bg, CxUiColor text);
bool _cxuiWin32TestChoose(CxUiPeer* p, int32 index, bool activate);
CxUiPeer* _cxuiWin32SliderCreate(CxUiWidget* w, CxUiPeer* host, bool vertical);
void _cxuiWin32SliderSetRange(CxUiPeer* p, int32 range, int32 pageStep, int32 tickEvery);
void _cxuiWin32SliderSetPos(CxUiPeer* p, int32 pos);
CxUiSize _cxuiWin32SliderMeasure(CxUiWidget* w, int32 length, bool vertical, bool ticks);
CxUiPeer* _cxuiWin32ProgressCreate(CxUiWidget* w, CxUiPeer* host);
void _cxuiWin32ProgressSet(CxUiPeer* p, float64 fraction, bool indeterminate);
bool _cxuiWin32TestSlide(CxUiPeer* p, int32 pos, bool release);
void _cxuiWin32ListSetMarks(CxUiPeer* p, const uint8* marks, int32 count);
void _cxuiWin32ListGetMarks(CxUiPeer* p, uint8* marks, int32 count);
int32 _cxuiWin32ListTopItem(CxUiPeer* p);
void _cxuiWin32ListSetTopItem(CxUiPeer* p, int32 index);
bool _cxuiWin32TestToggle(CxUiPeer* p, int32 index);
CxUiPeer* _cxuiWin32TabsCreate(CxUiTabs* t, CxUiPeer* host);
void _cxuiWin32TabsInsert(CxUiPeer* p, int32 at, strref title);
void _cxuiWin32TabsRemove(CxUiPeer* p, int32 at);
void _cxuiWin32TabsSetTitle(CxUiPeer* p, int32 at, strref title);
void _cxuiWin32TabsSetValue(CxUiPeer* p, int32 index);
void _cxuiWin32TabsSetPage(CxUiPeer* p, CxUiRect px);
void _cxuiWin32TabsHold(CxUiPeer* p, bool hold);
void _cxuiWin32TabsSetColors(CxUiPeer* p, const CxUiTabsColors* colors);
CxUiEdges _cxuiWin32TabsMeasure(CxUiTabs* t, CxUiFont* f, int32 width, int32* stripWidth);
// The surface the tabs' pages are on
HWND _cxuiWin32TabsPageHwnd(_In_ CxUiPeer* p);
void _cxuiWin32ControlSetBounds(CxUiPeer* p, CxUiRect px);
void _cxuiWin32ControlSetFont(CxUiPeer* p, CxUiFont* f);
void _cxuiWin32PeerSetEnabled(CxUiPeer* p, bool enabled);
HWND _cxuiWin32ControlHwnd(_In_ CxUiPeer* p);
void _cxuiWin32ControlDestroy(CxUiPeer* p);
bool _cxuiWin32TestControlInfo(CxUiPeer* p, CxUiTestControlInfo* out);
bool _cxuiWin32TestClick(CxUiPeer* p);
// A control's message to its parent: colors, on parentBg, a button's clicks, the tabs' picks
bool _cxuiWin32ControlParentMessage(UINT msg, WPARAM wp, LPARAM lp, COLORREF parentBg, _Out_ LRESULT* result);
// The window a child is in turned dark or light
void _cxuiWin32ControlSetDark(_In_ HWND hwnd, bool dark);
// A scroll box's scroll bars follow its window's theme
void _cxuiWin32PanelSetDark(_In_ HWND hwnd, bool dark);
void _cxuiWin32PeerSetTip(CxUiPeer* peer, strref tip);
bool _cxuiWin32TestTip(CxUiPeer* peer, string* out);
// The window a peer's children are created in: a scroll box's content, or the peer's own
HWND _cxuiWin32HostHwnd(_In_opt_ CxUiPeer* host);
void _cxuiWin32PanelSetContent(CxUiPeer* peer, CxUiSize px);
void _cxuiWin32PanelSetTitle(CxUiPeer* peer, strref title, CxUiFont* f, CxUiColor color, int32 indent, int32 gap);
void _cxuiWin32PanelScrollTo(CxUiPeer* peer, CxUiRect px);
int32 _cxuiWin32ScrollBarSize(void);
bool _cxuiWin32TestScrollPos(CxUiPeer* peer, CxUiPoint* out);
bool _cxuiWin32TestScrollWheel(CxUiPeer* peer, int32 notches);
// The Explorer theme's dark look on a control (CFD's for a combo box); the platform's otherwise
void _cxuiWin32ApplyDarkTheme(_In_ HWND hwnd, bool dark);
// A text area's scroll bars follow its window's theme
void _cxuiWin32TextSetDark(_In_ HWND hwnd, bool dark);
// Whether the top-level window hwnd is in is dark (win32_window.c)
bool _cxuiWin32RootDark(_In_ HWND hwnd);
// The child its window gives focus back to when it is next activated (win32_window.c)
void _cxuiWin32RememberFocus(_In_ HWND child);

// win32_input.c: each returns true if cxui handled the message
bool _cxuiWin32Key(_In_ CxUiPeer* p, UINT msg, WPARAM wp, LPARAM lp);
// CxUiModifiers held now
uint32 _cxuiWin32KeyMods(void);
// The Windows key is held while a canvas takes it, which hides it from the system's state
bool _cxuiWin32SysKeyHeld(void);
bool _cxuiWin32Pointer(_In_ CxUiPeer* p, HWND hwnd, _Inout_ bool* tracking, UINT msg, WPARAM wp, LPARAM lp);
bool _cxuiWin32Wheel(UINT msg, WPARAM wp, LPARAM lp);
void _cxuiWin32DoubleClick(int64* time, int32* distance);
void _cxuiWin32PeerFocus(CxUiPeer* p);
bool _cxuiWin32TestNativeKey(CxUiPeer* p, CxUiKey key, uint32 mods);
bool _cxuiWin32TestNativeClick(CxUiPeer* p, CxUiPoint px);

// ---- menus ----
CxUiPeer* _cxuiWin32MenuCreate(CxUiMenu* m, bool bar);
void _cxuiWin32MenuInsert(CxUiPeer* p, int32 at, CxUiMenuItem* item);
void _cxuiWin32MenuRemove(CxUiPeer* p, int32 at);
void _cxuiWin32MenuUpdate(CxUiPeer* p, int32 at, CxUiMenuItem* item);
void _cxuiWin32MenuDestroy(CxUiPeer* p);
// Makes menu (or NULL for none) the window's menu bar, without keeping the client size
void _cxuiWin32WindowAttachMenu(_In_ HWND hwnd, _In_opt_ CxUiPeer* menu);
// Draws the menu bar dark and makes the menus that drop down follow the window; true if the message
// was handled, with its result in *out.
bool _cxuiWin32MenuMessage(_In_ HWND hwnd, bool dark, COLORREF bg, UINT msg, WPARAM wp, LPARAM lp,
                           _Out_ LRESULT* out);
// Repaints the menu bar and the line under it, leaving the rest of the frame alone
void _cxuiWin32MenuBarRedraw(_In_ HWND hwnd);
// WM_MENUCOMMAND: the item at position at of h was chosen. Outside _cxuiNativeEnter/Leave.
void _cxuiWin32MenuCommand(_In_ HMENU h, int32 at);
int32 _cxuiWin32TestMenuCount(CxUiPeer* p);
bool _cxuiWin32TestMenuItem(CxUiPeer* p, int32 at, CxUiTestMenuItemInfo* out);
bool _cxuiWin32TestMenuChoose(CxUiPeer* p, int32 at);
CxUiPeer* _cxuiWin32TestWindowMenu(CxUiPeer* win);
void _cxuiWin32WindowSetMenu(CxUiPeer* p, CxUiPeer* menu);
bool _cxuiWin32TestNativeWheel(CxUiPeer* p, CxUiPoint px, int32 notches);
// The draw ops for a CxUiDrawBackends value, as available
const CxUiDrawOps* _cxuiWin32DrawOps(uint32 which);

// win32_draw_d2d.cpp
extern const CxUiDrawOps _cxuiWin32D2dOps;
// Whether Direct2D, DirectWrite and WIC loaded
bool _cxuiWin32D2dAvailable(void);
// Copies part of a Direct2D target to a DC
void _cxuiWin32D2dPresent(_In_ void* t, HDC dc, _In_ const RECT* r);

// win32_draw_gdi.c
extern const CxUiDrawOps _cxuiWin32GdiOps;
// The system's message font, the "ui" face
void _cxuiWin32SystemUiFont(_Out_ LOGFONTW* out);
// A font's GDI form and its metrics
HFONT _cxuiWin32GdiFont(_In_ CxUiFont* f, _Out_opt_ TEXTMETRICW* tm);
bool _cxuiWin32BufferInit(_Out_ Win32BackBuffer* bb);
// Leaves the contents undefined when it reallocates.
void _cxuiWin32BufferResize(_Inout_ Win32BackBuffer* bb, CxUiSize px);
void _cxuiWin32BufferDestroy(_Inout_ Win32BackBuffer* bb);
// size.w * size.h RGBA pixels, alpha 255
void _cxuiWin32BufferRead(_In_ Win32BackBuffer* bb, _Out_ uint8* rgba);

// win32_dialog.c
// Activates cxui's comctl32 version 6 context around creating controls; false if there is none.
bool _cxuiWin32Comctl6Begin(_Out_ ULONG_PTR* cookie);
void _cxuiWin32Comctl6End(ULONG_PTR cookie);
// Whether the comctl32 that context loads is version 6
bool _cxuiWin32HasComctl6(void);
// Whether it is Vista's 6.10 or later, which has task dialogs; XP's is 6.0
bool _cxuiWin32HasComctl610(void);
int32 _cxuiWin32MessageBox(const CxUiMessageBoxDesc* desc, CxUiPeer* parent);
bool _cxuiWin32FileDialog(const CxUiFileDialogDesc* desc, CxUiPeer* parent, string* out);
bool _cxuiWin32TestAnswerFileDialog(strref path);
bool _cxuiWin32TestAnswerMessageBox(int32 button);

CX_C_END
