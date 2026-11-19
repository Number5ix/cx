#pragma once

#include <gtk/gtk.h>

#include "../../core/cxuidraw_private.h"

// gtk4_window.c
CxUiPeer* _cxuiGtk4WindowCreate(CxUiWindow* w);
void _cxuiGtk4WindowShow(CxUiPeer* p, uint32 mode);
void _cxuiGtk4WindowHide(CxUiPeer* p);
void _cxuiGtk4WindowSetTitle(CxUiPeer* p, strref title);
void _cxuiGtk4WindowSetSize(CxUiPeer* p, CxUiSize dips);
void _cxuiGtk4WindowSetMinSize(CxUiPeer* p, CxUiSize dips);
void _cxuiGtk4WindowSetResizable(CxUiPeer* p, bool resizable);
bool _cxuiGtk4WindowBeginDrag(CxUiPeer* p);
bool _cxuiGtk4TestBorderless(CxUiPeer* p, bool* borderless);
// The button press whose handler is running, or NULL
GdkEvent* _cxuiGtk4PressEvent(void);
void _cxuiGtk4PeerSetVisible(CxUiPeer* p, bool visible);
void _cxuiGtk4PeerRaise(CxUiPeer* p);
void _cxuiGtk4WindowSetBackground(CxUiPeer* p, CxUiColor c);
void _cxuiGtk4WindowSetMenu(CxUiPeer* p, CxUiPeer* menu);
CxUiPeer* _cxuiGtk4TestWindowMenu(CxUiPeer* p);
// The widget a window's content is placed in
GtkWidget* _cxuiGtk4WindowRoot(_In_ CxUiPeer* p);

// ---- menus ----
CxUiPeer* _cxuiGtk4MenuCreate(CxUiMenu* m, bool bar);
void _cxuiGtk4MenuInsert(CxUiPeer* p, int32 at, CxUiMenuItem* item);
void _cxuiGtk4MenuRemove(CxUiPeer* p, int32 at);
void _cxuiGtk4MenuUpdate(CxUiPeer* p, int32 at, CxUiMenuItem* item);
void _cxuiGtk4MenuDestroy(CxUiPeer* p);
GMenuModel* _cxuiGtk4MenuModel(_In_ CxUiPeer* menu);
// Gives the window the actions menu items are chosen through
void _cxuiGtk4MenuActionsTo(_In_ GtkWidget* win);
int32 _cxuiGtk4TestMenuCount(CxUiPeer* p);
bool _cxuiGtk4TestMenuItem(CxUiPeer* p, int32 at, CxUiTestMenuItemInfo* out);
bool _cxuiGtk4TestMenuChoose(CxUiPeer* p, int32 at);
bool _cxuiGtk4TestPeerShown(CxUiPeer* p, bool* shown);
bool _cxuiGtk4TestFrontmost(CxUiPeer* win, CxUiPeer* p, CxUiPoint px, bool* front);
bool _cxuiGtk4TestWindowActive(CxUiPeer* p, bool* active);
void _cxuiGtk4WindowSetDark(CxUiPeer* p, bool dark);
CxUiThemeMode _cxuiGtk4SystemTheme(void);
// Starts or stops following the system's light or dark setting.
void _cxuiGtk4ThemeWatch(bool on);
bool _cxuiGtk4TestFrameDark(CxUiPeer* p, bool* dark);
void _cxuiGtk4WindowBeginModal(CxUiPeer* p, CxUiPeer* owner);
void _cxuiGtk4WindowEndModal(CxUiPeer* p);
void _cxuiGtk4WindowSetOwner(CxUiPeer* p, CxUiPeer* owner);
bool _cxuiGtk4WindowSetFullscreen(CxUiPeer* p, uint32 mode, const CxUiMonitor* mon);
int32 _cxuiGtk4WindowMonitor(CxUiPeer* p);
bool _cxuiGtk4WindowSetTopmost(CxUiPeer* p, bool on);
// Whether GDK runs on X11, and asking its window manager to keep a window above others
bool _cxuiGtk4X11Display(void);
void _cxuiGtk4X11SetAbove(_In_ GtkWindow* win, bool on);
bool _cxuiGtk4TestOwnedBy(CxUiPeer* p, CxUiPeer* owner, bool* owned);
GtkWindow* _cxuiGtk4PeerWindow(CxUiPeer* p);
void _cxuiGtk4PeerDestroy(CxUiPeer* p);

float32 _cxuiGtk4SystemScale(void);
void _cxuiGtk4WindowGetPlacement(CxUiPeer* p, CxUiPlacement* out);
void _cxuiGtk4WindowSetPlacement(CxUiPeer* p, const CxUiPlacement* pl, bool withState);

// gtk4_desktop.c
void _cxuiGtk4Monitors(sa_CxUiMonitor* out);
// The index of the monitor the window is on in what _cxuiGtk4Monitors gives, or -1
int32 _cxuiGtk4MonitorOf(_In_ GtkWindow* win);
// Watches the monitors for changes, for the session
void _cxuiGtk4DesktopInit(void);
// The GDK monitor a CxUiMonitor describes, borrowed; NULL if it isn't connected
GdkMonitor* _cxuiGtk4MonitorFor(_In_ const CxUiMonitor* m);
bool _cxuiGtk4ClipboardSetText(_In_opt_ strref text);
bool _cxuiGtk4ClipboardGetText(_Inout_ string* out);
void _cxuiGtk4ClipboardStore(void);
void _cxuiGtk4DesktopShutdown(void);
bool _cxuiGtk4TestClipboardForeign(_In_opt_ strref text);

// gtk4_canvas.c
// Called as the root is allocated, with its size in logical pixels.
typedef void (*Gtk4RootAllocated)(void* ud, int width, int height);
GtkWidget* _cxuiGtk4RootNew(Gtk4RootAllocated allocated, _In_opt_ void* ud);
// Stops the root calling back, before its window's peer goes.
void _cxuiGtk4RootDetach(_In_ GtkWidget* root);
// The smallest and natural client sizes the root asks its window for, in DIPs
void _cxuiGtk4RootSetSizes(_In_ GtkWidget* root, CxUiSize min, CxUiSize natural);
CxUiSize _cxuiGtk4RootMinSize(_In_ GtkWidget* root);
CxUiSize _cxuiGtk4RootNaturalSize(_In_ GtkWidget* root);
GtkWidget* _cxuiGtk4CanvasWidget(_In_ CxUiPeer* p);
// The widget a canvas's, a panel's or a window's peer shows as
GtkWidget* _cxuiGtk4PeerWidget(_In_ CxUiPeer* p);
// A background and border under a root's children
void _cxuiGtk4RootSetColors(_In_ GtkWidget* root, CxUiColor bg, CxUiColor border, int32 borderPx);
CxUiPeer* _cxuiGtk4PanelCreate(CxUiWidget* w, CxUiPeer* host);
void _cxuiGtk4PanelSetBounds(CxUiPeer* p, CxUiRect px);
void _cxuiGtk4PanelSetContent(CxUiPeer* peer, CxUiSize px);
void _cxuiGtk4PeerSetTip(CxUiPeer* p, strref tip);
bool _cxuiGtk4TestTip(CxUiPeer* p, string* out);
void _cxuiGtk4PanelSetTitle(CxUiPeer* p, strref title, CxUiFont* f, CxUiColor color, int32 indent, int32 gap);
void _cxuiGtk4PanelScrollTo(CxUiPeer* peer, CxUiRect px);
bool _cxuiGtk4TestScrollPos(CxUiPeer* peer, CxUiPoint* out);
bool _cxuiGtk4TestScrollWheel(CxUiPeer* peer, int32 notches);
void _cxuiGtk4PanelSetColors(CxUiPeer* p, CxUiColor bg, CxUiColor border, int32 borderPx);
void _cxuiGtk4PanelDestroy(CxUiPeer* p);
// The scale of the surface w is on, or 1
float32 _cxuiGtk4WidgetScale(_In_ GtkWidget* w);
CxUiPeer* _cxuiGtk4CanvasCreate(CxUiCanvas* cv, CxUiPeer* parent);
void _cxuiGtk4CanvasSetBounds(CxUiPeer* p, CxUiRect px);
void _cxuiGtk4CanvasInvalidate(CxUiPeer* p, CxUiRect px);
void _cxuiGtk4CanvasPaintNow(CxUiPeer* p);
bool _cxuiGtk4TestExpose(CxUiPeer* p);
void _cxuiGtk4CanvasCapture(CxUiPeer* p, bool on);
void _cxuiGtk4PeerFocus(CxUiPeer* p);
void _cxuiGtk4CanvasSetCursor(CxUiPeer* p, uint32 cursor);
void _cxuiGtk4CanvasSetCursorImage(CxUiPeer* p, CxUiCursor* cursor);
bool _cxuiGtk4CanvasWarpPointer(CxUiPeer* p, CxUiPoint px);
bool _cxuiGtk4CanvasSetTextInput(CxUiPeer* p, flags_t flags);
void _cxuiGtk4CanvasSetTextCaret(CxUiPeer* p, CxUiRect px);
bool _cxuiGtk4CanvasSetKeyCapture(CxUiPeer* p, flags_t flags);
uint32 _cxuiGtk4CanvasPointerLock(CxUiPeer* p, uint32 mode, CxUiPoint px);
// The widget a container peer's children go in
GtkWidget* _cxuiGtk4HostWidget(_In_opt_ CxUiPeer* host);
// Puts a peer's widget in a root, placed at *bounds (physical pixels, kept by the peer)
void _cxuiGtk4ChildAttach(_In_ GtkWidget* w, _In_opt_ CxUiPeer* p, _In_ CxUiRect* bounds, _In_ GtkWidget* parent);
void _cxuiGtk4ChildDetach(_In_ GtkWidget* w);
// The child's bounds changed: lays out its parent again
void _cxuiGtk4ChildPlace(_In_ GtkWidget* w);
// Whether the window's focus is in a canvas's or text field's widget, which routes keys itself
bool _cxuiGtk4FocusInPeer(_In_ GtkWindow* win);
// Disconnects every handler with data from w's event controllers.
void _cxuiGtk4ControllersRelease(_In_ GtkWidget* w, _In_ void* data);

// gtk4_text.c
CxUiPeer* _cxuiGtk4TextCreate(CxUiTextField* t, CxUiPeer* host, bool framed);
void _cxuiGtk4TextDestroy(CxUiPeer* p);
// The entry, or the bare text widget
GtkWidget* _cxuiGtk4TextWidget(_In_ CxUiPeer* p);
GtkWidget* _cxuiGtk4TextFocusWidget(_In_ CxUiPeer* p);
CxUiSize _cxuiGtk4TextAreaMeasure(CxUiTextArea* ta, CxUiFont* f, int32 columns, int32 lines, bool framed);
void _cxuiGtk4TextAppend(CxUiPeer* p, strref text);
void _cxuiGtk4TextScrollToEnd(CxUiPeer* p);
void _cxuiGtk4TextShutdown(void);
void _cxuiGtk4TextSetBounds(CxUiPeer* p, CxUiRect px);
CxUiSize _cxuiGtk4TextMeasure(CxUiTextField* t, CxUiFont* f, int32 columns, bool framed);
void _cxuiGtk4TextSetText(CxUiPeer* p, strref text);
void _cxuiGtk4TextGetSelection(CxUiPeer* p, int32* start, int32* end, int32* caret);
void _cxuiGtk4TextSetSelection(CxUiPeer* p, int32 start, int32 end);
void _cxuiGtk4TextSetCue(CxUiPeer* p, strref cue);
void _cxuiGtk4TextSetMaxLength(CxUiPeer* p, int32 maxLength);
void _cxuiGtk4TextSetFont(CxUiPeer* p, CxUiFont* f);
void _cxuiGtk4TextSetState(CxUiPeer* p, bool readOnly, bool enabled, bool focusable);
void _cxuiGtk4TextSetColors(CxUiPeer* p, const CxUiTextColors* c);
bool _cxuiGtk4TestTextInfo(CxUiPeer* p, CxUiTestTextInfo* out);
bool _cxuiGtk4TestType(CxUiPeer* p, strref s);
// A font as CSS declarations, in logical pixels
void _cxuiGtk4FontCss(_Inout_ string* out, _In_ CxUiFont* f);
// Appends a CSS rgba() color
void _cxuiGtk4CssColor(_Inout_ string* out, CxUiColor c);

// gtk4_controls.c
CxUiPeer* _cxuiGtk4LabelCreate(CxUiLabel* l, CxUiPeer* host);
void _cxuiGtk4LabelSetText(CxUiPeer* p, strref text);
void _cxuiGtk4LabelSetStyle(CxUiPeer* p, uint32 align, uint32 ellipsis, bool wrap);
int32 _cxuiGtk4LabelHeightFor(CxUiLabel* l, CxUiFont* f, strref text, int32 width);
void _cxuiGtk4ControlSetColor(CxUiPeer* p, CxUiColor color);
CxUiPeer* _cxuiGtk4ButtonCreate(CxUiButton* b, CxUiPeer* host);
void _cxuiGtk4ButtonSetTitle(CxUiPeer* p, strref title);
void _cxuiGtk4ButtonSetDefault(CxUiPeer* p, bool isDefault);
CxUiSize _cxuiGtk4ButtonMeasure(CxUiButton* b, CxUiFont* f, strref title);
CxUiSize _cxuiGtk4LabelMeasure(CxUiLabel* l, CxUiFont* f, strref text);
CxUiPeer* _cxuiGtk4CheckCreate(CxUiWidget* w, CxUiPeer* host, bool radio);
void _cxuiGtk4CheckSetState(CxUiPeer* p, uint32 state);
CxUiSize _cxuiGtk4CheckMeasure(CxUiWidget* w, CxUiFont* f, strref title, bool radio);
CxUiPeer* _cxuiGtk4ListCreate(CxUiWidget* w, CxUiPeer* host, bool dropdown);
void _cxuiGtk4ListSplice(CxUiPeer* p, int32 at, int32 removed, const string* items, int32 added);
void _cxuiGtk4ListSetValue(CxUiPeer* p, int32 value);
void _cxuiGtk4ListShow(CxUiPeer* p, int32 index);
CxUiSize _cxuiGtk4ListMeasure(CxUiWidget* w, CxUiFont* f, int32 widest, int32 rows, bool dropdown);
void _cxuiGtk4ListSetColors(CxUiPeer* p, CxUiColor bg, CxUiColor text);
void _cxuiGtk4ListSetMarks(CxUiPeer* p, const uint8* marks, int32 count);
void _cxuiGtk4ListGetMarks(CxUiPeer* p, uint8* marks, int32 count);
int32 _cxuiGtk4ListTopItem(CxUiPeer* p);
void _cxuiGtk4ListSetTopItem(CxUiPeer* p, int32 index);
bool _cxuiGtk4TestToggle(CxUiPeer* p, int32 index);
bool _cxuiGtk4TestChoose(CxUiPeer* p, int32 index, bool activate);
CxUiPeer* _cxuiGtk4SliderCreate(CxUiWidget* w, CxUiPeer* host, bool vertical);
void _cxuiGtk4SliderSetRange(CxUiPeer* p, int32 range, int32 pageStep, int32 tickEvery);
void _cxuiGtk4SliderSetPos(CxUiPeer* p, int32 pos);
CxUiSize _cxuiGtk4SliderMeasure(CxUiWidget* w, int32 length, bool vertical, bool ticks);
CxUiPeer* _cxuiGtk4ProgressCreate(CxUiWidget* w, CxUiPeer* host);
void _cxuiGtk4ProgressSet(CxUiPeer* p, float64 fraction, bool indeterminate);
CxUiPeer* _cxuiGtk4TabsCreate(CxUiTabs* t, CxUiPeer* host);
void _cxuiGtk4TabsInsert(CxUiPeer* p, int32 at, strref title);
void _cxuiGtk4TabsRemove(CxUiPeer* p, int32 at);
void _cxuiGtk4TabsSetTitle(CxUiPeer* p, int32 at, strref title);
void _cxuiGtk4TabsSetValue(CxUiPeer* p, int32 index);
void _cxuiGtk4TabsSetPage(CxUiPeer* p, CxUiRect px);
void _cxuiGtk4TabsSetColors(CxUiPeer* p, const CxUiTabsColors* colors);
CxUiEdges _cxuiGtk4TabsMeasure(CxUiTabs* t, CxUiFont* f, int32 width, int32* stripWidth);
// The surface the tabs' pages are on
GtkWidget* _cxuiGtk4TabsPage(_In_ CxUiPeer* p);
CxUiSize _cxuiGtk4ProgressMeasure(CxUiWidget* w, int32 length);
bool _cxuiGtk4TestSlide(CxUiPeer* p, int32 pos, bool release);
void _cxuiGtk4ControlSetBounds(CxUiPeer* p, CxUiRect px);
void _cxuiGtk4ControlSetFont(CxUiPeer* p, CxUiFont* f);
void _cxuiGtk4PeerSetEnabled(CxUiPeer* p, bool enabled);
GtkWidget* _cxuiGtk4ControlWidget(_In_ CxUiPeer* p);
GtkWidget* _cxuiGtk4ControlFocusWidget(_In_ CxUiPeer* p);
void _cxuiGtk4ControlDestroy(CxUiPeer* p);
void _cxuiGtk4ControlsShutdown(void);
bool _cxuiGtk4TestControlInfo(CxUiPeer* p, CxUiTestControlInfo* out);
bool _cxuiGtk4TestClick(CxUiPeer* p);

// gtk4_input.c: each returns true if cxui handled the event
bool _cxuiGtk4Key(_In_ CxUiPeer* p, _In_ GdkEvent* ev);
bool _cxuiGtk4Pointer(_In_ CxUiPeer* p, _In_ GtkWidget* w, _In_ GdkEvent* ev, _Inout_ CxUiPoint* last);
// An event's position relative to w, in physical pixels; *last for an event without one
CxUiPoint _cxuiGtk4EventPx(_In_ GtkWidget* w, _In_ GdkEvent* ev, _Inout_ CxUiPoint* last);
// CxUiModifiers in a GDK modifier state
uint32 _cxuiGtk4StateMods(GdkModifierType st);
void _cxuiGtk4DoubleClick(int64* time, int32* distance);
uint32 _cxuiGtk4KeyLocks(void);
bool _cxuiGtk4TestNativeKey(CxUiPeer* p, CxUiKey key, uint32 mods);
void _cxuiGtk4CanvasDestroy(CxUiPeer* p);

// gtk4_draw_gsk.c
extern const CxUiDrawOps _cxuiGtk4GskOps;
// A copy of a font's description, at the font's scale; free with pango_font_description_free
PangoFontDescription* _cxuiGtk4FontDescription(_In_ CxUiFont* f);
// Appends a GSK target's nodes, in physical pixels.
void _cxuiGtk4TargetAppend(_In_ void* t, _In_ GtkSnapshot* snap);

// gtk4_dialog.c
int32 _cxuiGtk4MessageBox(const CxUiMessageBoxDesc* desc, CxUiPeer* parent);
bool _cxuiGtk4TestAnswerMessageBox(int32 button);
bool _cxuiGtk4FileDialog(const CxUiFileDialogDesc* desc, CxUiPeer* parent, string* out);
bool _cxuiGtk4TestAnswerFileDialog(strref path);
bool _cxuiGtk4ColorDialog(const CxUiColorDialogDesc* desc, CxUiPeer* parent, CxUiColor* color);
bool _cxuiGtk4TestAnswerColorDialog(const CxUiColor* color);

// ---- render views (gtk4_surface.c) ----

// A render view's native surface, made once its canvas widget is mapped, when it reports it to
// the core
typedef struct Gtk4Surface Gtk4Surface;
Gtk4Surface* _cxuiGtk4SurfaceCreate(_In_ CxUiPeer* peer, _In_ GtkWidget* view);
void _cxuiGtk4SurfaceDestroy(_In_ Gtk4Surface* s);
bool _cxuiGtk4SurfaceGet(_In_ Gtk4Surface* s, _Out_ CxUiNativeSurface* out);
bool _cxuiGtk4SurfaceTestInfo(_In_ Gtk4Surface* s, _Out_ CxUiTestSurfaceInfo* out);
bool _cxuiGtk4SurfaceTestMark(_In_ Gtk4Surface* s);
// The backend's calls, on a canvas peer
bool _cxuiGtk4SurfaceGetPeer(CxUiPeer* p, CxUiNativeSurface* out);
bool _cxuiGtk4TestSurfaceInfo(CxUiPeer* p, CxUiTestSurfaceInfo* out);
bool _cxuiGtk4TestSurfaceMark(CxUiPeer* p);
