#pragma once

// Every AppKit backend file includes this first: Apple's AssertMacros.h would otherwise define
// check(), verify() and require() over cx's macros.

#define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
#import <AppKit/AppKit.h>

#include "../../core/cxuidraw_private.h"

// appkit_init.m

// Nested loops running on the main thread: cxui's own (popups) and AppKit's modal ones (alerts).
// While any is running, each tick posts an event so the loop notices a flag set by the tick, and
// shutdown stops them before tearing down.
extern int32 _cxuiAppKitLoopDepth;
void _cxuiAppKitLoopLeft(void);

// Gives the program its Dock icon and menu bar, the first time a session shows anything.
void _cxuiAppKitBecomeRegular(void);

NSString* _cxuiAppKitString(strref s);

// appkit_window.m
CxUiPeer* _cxuiAppKitWindowCreate(CxUiWindow* w);
void _cxuiAppKitWindowShow(CxUiPeer* p, uint32 mode);
void _cxuiAppKitWindowHide(CxUiPeer* p);
void _cxuiAppKitWindowSetTitle(CxUiPeer* p, strref title);
void _cxuiAppKitWindowSetSize(CxUiPeer* p, CxUiSize dips);
bool _cxuiAppKitWindowGetPosition(CxUiPeer* p, CxUiPoint* out);
bool _cxuiAppKitWindowSetPosition(CxUiPeer* p, CxUiPoint pos);
void _cxuiAppKitWindowGetPlacement(CxUiPeer* p, CxUiPlacement* out);
void _cxuiAppKitWindowSetPlacement(CxUiPeer* p, const CxUiPlacement* pl, bool withState);
void _cxuiAppKitWindowSetDark(CxUiPeer* p, bool dark);
void _cxuiAppKitWindowSetMinSize(CxUiPeer* p, CxUiSize dips);
void _cxuiAppKitWindowSetResizable(CxUiPeer* p, bool resizable);
bool _cxuiAppKitWindowBeginDrag(CxUiPeer* p);
bool _cxuiAppKitTestBorderless(CxUiPeer* p, bool* borderless);
void _cxuiAppKitWindowSetBackground(CxUiPeer* p, CxUiColor c);
bool _cxuiAppKitTestWindowActive(CxUiPeer* p, bool* active);
void _cxuiAppKitWindowBeginModal(CxUiPeer* p, CxUiPeer* owner);
void _cxuiAppKitWindowEndModal(CxUiPeer* p);
CxUiThemeMode _cxuiAppKitSystemTheme(void);
bool _cxuiAppKitTestFrameDark(CxUiPeer* p, bool* dark);
void _cxuiAppKitPeerDestroy(CxUiPeer* p);
// The innermost popup's window, or nil
NSWindow* _cxuiAppKitModalWindow(void);
void _cxuiAppKitWindowSetOwner(CxUiPeer* p, CxUiPeer* owner);
bool _cxuiAppKitWindowSetFullscreen(CxUiPeer* p, uint32 mode, const CxUiMonitor* mon);
int32 _cxuiAppKitWindowMonitorOf(CxUiPeer* p);
bool _cxuiAppKitWindowSetTopmost(CxUiPeer* p, bool on);
bool _cxuiAppKitTestTopmost(CxUiPeer* p, bool* topmost);
// Frames each Monitor window to its screen again, after the screens changed
void _cxuiAppKitMonitorsReframe(void);
bool _cxuiAppKitWindowMoveNear(CxUiPeer* p, CxUiPeer* other, uint32 where);
bool _cxuiAppKitTestOwnedBy(CxUiPeer* p, CxUiPeer* owner, bool* owned);
void _cxuiAppKitWindowSetMenu(CxUiPeer* p, CxUiPeer* menu);
// Shows the key window's menu bar
void _cxuiAppKitMenuRefresh(void);
CxUiPeer* _cxuiAppKitTestWindowMenu(CxUiPeer* win);

float32 _cxuiAppKitSystemScale(void);
// The view child peers go into: a window's content view, or a panel's view
NSView* _cxuiAppKitPeerContent(CxUiPeer* p);
// The window peer an NSWindow of cxui's belongs to, or NULL
CxUiPeer* _cxuiAppKitWindowPeerOf(NSWindow* w);

// appkit_menu.m
// The program's menu bar, the first time a session shows anything
void _cxuiAppKitMenuInstall(void);
// Shows a window's menu bar in the program's, or none
void _cxuiAppKitMenuShowBar(CxUiPeer* bar);
CxUiPeer* _cxuiAppKitMenuShownBar(void);
CxUiPeer* _cxuiAppKitMenuCreate(CxUiMenu* m, bool bar);
void _cxuiAppKitMenuInsert(CxUiPeer* menu, int32 at, CxUiMenuItem* item);
void _cxuiAppKitMenuRemove(CxUiPeer* menu, int32 at);
void _cxuiAppKitMenuUpdate(CxUiPeer* menu, int32 at, CxUiMenuItem* item);
void _cxuiAppKitMenuDestroy(CxUiPeer* menu);
int32 _cxuiAppKitTestMenuCount(CxUiPeer* menu);
bool _cxuiAppKitTestMenuItem(CxUiPeer* menu, int32 at, CxUiTestMenuItemInfo* out);
bool _cxuiAppKitTestMenuChoose(CxUiPeer* menu, int32 at);

// appkit_canvas.m
void _cxuiAppKitCanvasInit(void);
CxUiPeer* _cxuiAppKitCanvasCreate(CxUiCanvas* cv, CxUiPeer* parent);
void _cxuiAppKitCanvasSetBounds(CxUiPeer* p, CxUiRect px);
void _cxuiAppKitCanvasInvalidate(CxUiPeer* p, CxUiRect px);
void _cxuiAppKitCanvasPaintNow(CxUiPeer* p);
void _cxuiAppKitCanvasCapture(CxUiPeer* p, bool on);
void _cxuiAppKitCanvasSetCursor(CxUiPeer* p, uint32 cursor);
void _cxuiAppKitCanvasSetCursorImage(CxUiPeer* p, CxUiCursor* cursor);
bool _cxuiAppKitCanvasWarpPointer(CxUiPeer* p, CxUiPoint px);
bool _cxuiAppKitCanvasSetTextInput(CxUiPeer* p, flags_t flags);
void _cxuiAppKitCanvasSetTextCaret(CxUiPeer* p, CxUiRect px);
bool _cxuiAppKitCanvasSetKeyCapture(CxUiPeer* p, flags_t flags);
uint32 _cxuiAppKitCanvasPointerLock(CxUiPeer* p, uint32 mode, CxUiPoint px);
// Whether a responder is a canvas that captures keys, and hands it one; false for a key the
// system keeps, which goes on as usual
bool _cxuiAppKitCanvasTakesKeys(_In_opt_ id responder);
bool _cxuiAppKitCanvasCaptureKey(_In_ id responder, _In_ NSEvent* ev);
void _cxuiAppKitDoubleClick(int64* time, int32* distance);
uint32 _cxuiAppKitKeyLocks(void);
bool _cxuiAppKitTestExpose(CxUiPeer* p);
bool _cxuiAppKitTestScreenOrigin(CxUiPeer* p, CxUiPoint* out);
void _cxuiAppKitCanvasDestroy(CxUiPeer* p);
// A key event for a canvas or a window; true if cxui handled it
bool _cxuiAppKitKey(_In_ CxUiPeer* p, _In_ NSEvent* ev);
bool _cxuiAppKitIsCanvasView(_In_opt_ id responder);
NSView* _cxuiAppKitCanvasView(_In_ CxUiPeer* p);
bool _cxuiAppKitSurfaceGet(CxUiPeer* p, CxUiNativeSurface* out);
bool _cxuiAppKitTestSurfaceInfo(CxUiPeer* p, CxUiTestSurfaceInfo* out);

// appkit_panel.m
CxUiPeer* _cxuiAppKitPanelCreate(CxUiWidget* w, CxUiPeer* host);
void _cxuiAppKitPanelSetBounds(CxUiPeer* p, CxUiRect px);
void _cxuiAppKitPanelSetContent(CxUiPeer* p, CxUiSize px);
void _cxuiAppKitPeerSetTip(CxUiPeer* p, strref tip);
bool _cxuiAppKitTestTip(CxUiPeer* p, string* out);
void _cxuiAppKitPanelSetTitle(CxUiPeer* p, strref title, CxUiFont* f, CxUiColor color, int32 indent, int32 gap);
void _cxuiAppKitPanelScrollTo(CxUiPeer* p, CxUiRect px);
int32 _cxuiAppKitScrollBarSize(void);
bool _cxuiAppKitTestScrollPos(CxUiPeer* p, CxUiPoint* out);
bool _cxuiAppKitTestScrollWheel(CxUiPeer* p, int32 notches);
void _cxuiAppKitPanelSetColors(CxUiPeer* p, CxUiColor bg, CxUiColor border, int32 borderPx);
void _cxuiAppKitPanelDestroy(CxUiPeer* p);
NSView* _cxuiAppKitPanelView(_In_ CxUiPeer* p);
// A panel peer of no widget's inside parent, for a control's own surface
CxUiPeer* _cxuiAppKitSurfaceCreate(_In_ NSView* parent);
// Places a child view at bounds in physical pixels within its parent
void _cxuiAppKitPlaceView(_In_ NSView* v, CxUiRect px);
void _cxuiAppKitPeerSetVisible(CxUiPeer* p, bool visible);
// A child peer's view, or a window's content view
NSView* _cxuiAppKitPeerView(_In_ CxUiPeer* p);
// Gives a canvas or text field keyboard focus
void _cxuiAppKitPeerFocus(CxUiPeer* p);
NSColor* _cxuiAppKitColor(CxUiColor c);
bool _cxuiAppKitTestPeerShown(CxUiPeer* p, bool* shown);
void _cxuiAppKitPeerRaise(CxUiPeer* p);
bool _cxuiAppKitTestFrontmost(CxUiPeer* win, CxUiPeer* p, CxUiPoint px, bool* front);

// appkit_desktop.m
void _cxuiAppKitMonitors(sa_CxUiMonitor* out);
// The index of a window's screen in what _cxuiAppKitMonitors gives, or -1
int32 _cxuiAppKitWindowMonitor(_In_ NSWindow* w);
// Watches the screens for changes, once for the process
void _cxuiAppKitDesktopInit(void);
bool _cxuiAppKitClipboardSetText(strref text);
bool _cxuiAppKitClipboardGetText(string* out);
bool _cxuiAppKitKeepAwake(bool on, strref reason);

// appkit_text.m
void _cxuiAppKitTextInit(void);
void _cxuiAppKitTextShutdown(void);
CxUiPeer* _cxuiAppKitTextCreate(CxUiTextField* tf, CxUiPeer* host, bool framed);
void _cxuiAppKitTextDestroy(CxUiPeer* p);
NSView* _cxuiAppKitTextView(_In_ CxUiPeer* p);
// The view that takes focus: an area's text view, inside its scroll view
NSView* _cxuiAppKitTextFocusView(_In_ CxUiPeer* p);
// The text field a responder edits (the field itself, or the window's field editor while it
// edits one), or NULL
CxUiPeer* _cxuiAppKitTextPeerFor(_In_opt_ id responder);
void _cxuiAppKitTextSetBounds(CxUiPeer* p, CxUiRect px);
CxUiSize _cxuiAppKitTextMeasure(CxUiTextField* tf, CxUiFont* f, int32 columns, bool framed);
void _cxuiAppKitTextSetText(CxUiPeer* p, strref text);
void _cxuiAppKitTextGetSelection(CxUiPeer* p, int32* start, int32* end, int32* caret);
void _cxuiAppKitTextSetSelection(CxUiPeer* p, int32 start, int32 end);
void _cxuiAppKitTextSetCue(CxUiPeer* p, strref cue);
void _cxuiAppKitTextSetMaxLength(CxUiPeer* p, int32 maxLength);
void _cxuiAppKitTextSetFont(CxUiPeer* p, CxUiFont* f);
void _cxuiAppKitTextSetState(CxUiPeer* p, bool readOnly, bool enabled, bool focusable);
void _cxuiAppKitTextSetColors(CxUiPeer* p, const CxUiTextColors* c);
CxUiSize _cxuiAppKitTextAreaMeasure(CxUiTextArea* ta, CxUiFont* f, int32 columns, int32 lines, bool framed);
void _cxuiAppKitTextAppend(CxUiPeer* p, strref text);
void _cxuiAppKitTextScrollToEnd(CxUiPeer* p);
bool _cxuiAppKitTestTextInfo(CxUiPeer* p, CxUiTestTextInfo* out);
bool _cxuiAppKitTestType(CxUiPeer* p, strref s);
bool _cxuiAppKitTestNativeKey(CxUiPeer* p, CxUiKey key, uint32 mods);
bool _cxuiAppKitTestNativeClick(CxUiPeer* p, CxUiPoint px);
bool _cxuiAppKitTestNativeWheel(CxUiPeer* p, CxUiPoint px, int32 notches);

// appkit_controls.m
CxUiPeer* _cxuiAppKitLabelCreate(CxUiLabel* l, CxUiPeer* host);
void _cxuiAppKitLabelSetText(CxUiPeer* p, strref text);
void _cxuiAppKitLabelSetStyle(CxUiPeer* p, uint32 align, uint32 ellipsis, bool wrap);
int32 _cxuiAppKitLabelHeightFor(CxUiLabel* l, CxUiFont* f, strref text, int32 width);
void _cxuiAppKitControlSetColor(CxUiPeer* p, CxUiColor color);
CxUiSize _cxuiAppKitLabelMeasure(CxUiLabel* l, CxUiFont* f, strref text);
CxUiPeer* _cxuiAppKitButtonCreate(CxUiButton* b, CxUiPeer* host);
void _cxuiAppKitButtonSetTitle(CxUiPeer* p, strref title);
void _cxuiAppKitButtonSetDefault(CxUiPeer* p, bool isDefault);
CxUiSize _cxuiAppKitButtonMeasure(CxUiButton* b, CxUiFont* f, strref title);
bool _cxuiAppKitButtonsFocus(void);
CxUiPeer* _cxuiAppKitCheckCreate(CxUiWidget* w, CxUiPeer* host, bool radio);
void _cxuiAppKitCheckSetState(CxUiPeer* p, uint32 state);
CxUiSize _cxuiAppKitCheckMeasure(CxUiWidget* w, CxUiFont* f, strref title, bool radio);
CxUiPeer* _cxuiAppKitListCreate(CxUiWidget* w, CxUiPeer* host, bool dropdown);
void _cxuiAppKitListSplice(CxUiPeer* p, int32 at, int32 removed, const string* items, int32 added);
void _cxuiAppKitListSetValue(CxUiPeer* p, int32 value);
void _cxuiAppKitListShow(CxUiPeer* p, int32 index);
CxUiSize _cxuiAppKitListMeasure(CxUiWidget* w, CxUiFont* f, int32 widest, int32 rows, bool dropdown);
bool _cxuiAppKitTestChoose(CxUiPeer* p, int32 index, bool activate);
CxUiPeer* _cxuiAppKitSliderCreate(CxUiWidget* w, CxUiPeer* host, bool vertical);
void _cxuiAppKitSliderSetRange(CxUiPeer* p, int32 range, int32 pageStep, int32 tickEvery);
void _cxuiAppKitSliderSetPos(CxUiPeer* p, int32 pos);
CxUiSize _cxuiAppKitSliderMeasure(CxUiWidget* w, int32 length, bool vertical, bool ticks);
CxUiPeer* _cxuiAppKitProgressCreate(CxUiWidget* w, CxUiPeer* host);
void _cxuiAppKitProgressSet(CxUiPeer* p, float64 fraction, bool indeterminate);
bool _cxuiAppKitTestSlide(CxUiPeer* p, int32 pos, bool release);
CxUiPeer* _cxuiAppKitTabsCreate(CxUiTabs* t, CxUiPeer* host);
void _cxuiAppKitTabsInsert(CxUiPeer* p, int32 at, strref title);
void _cxuiAppKitTabsRemove(CxUiPeer* p, int32 at);
void _cxuiAppKitTabsSetTitle(CxUiPeer* p, int32 at, strref title);
void _cxuiAppKitTabsSetValue(CxUiPeer* p, int32 index);
void _cxuiAppKitTabsSetPage(CxUiPeer* p, CxUiRect px);
CxUiEdges _cxuiAppKitTabsMeasure(CxUiTabs* t, CxUiFont* f, int32 width, int32* stripWidth);
// The page surface the pages' peers go into
NSView* _cxuiAppKitTabsPage(_In_ CxUiPeer* p);
void _cxuiAppKitListSetMarks(CxUiPeer* p, const uint8* marks, int32 count);
void _cxuiAppKitListGetMarks(CxUiPeer* p, uint8* marks, int32 count);
int32 _cxuiAppKitListTopItem(CxUiPeer* p);
void _cxuiAppKitListSetTopItem(CxUiPeer* p, int32 index);
bool _cxuiAppKitTestToggle(CxUiPeer* p, int32 index);
void _cxuiAppKitControlSetBounds(CxUiPeer* p, CxUiRect px);
void _cxuiAppKitControlSetFont(CxUiPeer* p, CxUiFont* f);
void _cxuiAppKitPeerSetEnabled(CxUiPeer* p, bool enabled);
NSView* _cxuiAppKitControlView(_In_ CxUiPeer* p);
// The view inside a control's that takes focus: a list's table, inside its scroll view
NSView* _cxuiAppKitControlFocusView(_In_ CxUiPeer* p);
// The button a responder is, or NULL
CxUiPeer* _cxuiAppKitControlPeerFor(_In_opt_ id responder);
void _cxuiAppKitControlDestroy(CxUiPeer* p);
bool _cxuiAppKitTestControlInfo(CxUiPeer* p, CxUiTestControlInfo* out);
bool _cxuiAppKitTestClick(CxUiPeer* p);

// appkit_draw_cg.m
extern const CxUiDrawOps _cxuiAppKitCgOps;
// The font in points, as AppKit's controls take it; release it
CTFontRef _cxuiAppKitFontAtPoints(_In_ CxUiFont* f);
// The back buffer as an image (copied on write), or NULL; release it
CGImageRef _cxuiAppKitTargetImage(_In_ void* t);
CxUiSize _cxuiAppKitTargetAlloc(_In_ void* t);

// appkit_dialog.m
int32 _cxuiAppKitMessageBox(const CxUiMessageBoxDesc* desc, CxUiPeer* parent);
bool _cxuiAppKitTestAnswerMessageBox(int32 button);
bool _cxuiAppKitFileDialog(const CxUiFileDialogDesc* desc, CxUiPeer* parent, string* out);
bool _cxuiAppKitTestAnswerFileDialog(strref path);
bool _cxuiAppKitColorDialog(const CxUiColorDialogDesc* desc, CxUiPeer* parent, CxUiColor* color);
bool _cxuiAppKitTestAnswerColorDialog(const CxUiColor* color);
