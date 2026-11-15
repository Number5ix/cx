#pragma once

// Test fixtures for cxui's own tests, built on the null backend. Not for programs.

#include <cxui.h>
#include <cxui/platform/null/cxuitestwidget.h>

CX_C_BEGIN

// Runs the core dispatcher, as a backend does for a native event: ev->kind must be set, and
// the header's other fields are filled in. UI thread only.
CxUiResult cxuiTestDispatch(_In_ CxUiWidget* target, _Inout_ CxUiEvent* ev);

// Gives w a null-backend peer, as if it had been realized. False unless cxui is running on the
// null backend and w is live without a peer. UI thread only.
bool cxuiTestRealize(_In_ CxUiWidget* w);

// Null-backend peers not yet destroyed.
int32 cxuiTestPeersAlive(void);

// Null-backend peers ever destroyed off the UI thread; anything but 0 is a bug.
int32 cxuiTestPeersOffThread(void);

// What a null-backend window peer was last told.
typedef struct CxUiTestWindowInfo {
    strref title;   // borrowed from the peer
    CxUiSize size;
    CxUiSize created;   // size the window was created at
    CxUiPoint pos;
    uint32 mode;    // the last CxUiShowModes
    uint32 state;        // CxUiWindowStates
    uint32 fullscreen;   // CxUiFullscreen
    CxUiRect fsMonitor;  // the bounds of the monitor fullscreen was asked for on; 0 for the current one
    bool visible;
    bool dark;
    bool enabled;
    bool modal;            // between the start and end of a popup
    CxUiWindow* owner;     // the native owner: the popup's parent while modal, else the window's owner
    CxUiSize minSize;      // smallest size the user can make it
    bool resizable;
    bool borderless;
    int32 drags;           // moves started by cxuiWindowBeginDrag()
    CxUiColor bg;          // client area background
} CxUiTestWindowInfo;

// False unless w has a null-backend peer. UI thread only.
bool cxuiTestWindowInfo(_In_ CxUiWindow* w, _Out_ CxUiTestWindowInfo* out);

// Does what the window's close box does. Any backend; UI thread only.
void cxuiTestClose(_In_ CxUiWindow* w);

// Sets what the null backend reports as the system's theme. UI thread only.
void cxuiTestSetSystemTheme(CxUiThemeMode mode);

// Answers the innermost open message box as if button (from 1) were pressed. False if no box is
// open or the button doesn't exist. Any backend; UI thread only, so usually from a timer.
bool cxuiTestAnswerMessageBox(int32 button);

// Message boxes open now. UI thread only.
int32 cxuiTestMessageBoxes(void);

// Answers the innermost open file dialog with a cx path, as if the user chose it, or cancels it
// with NULL. False if none is open. Any backend; UI thread only, so usually from a timer.
bool cxuiTestAnswerFileDialog(_In_opt_ strref path);

// File dialogs open now. UI thread only.
int32 cxuiTestFileDialogs(void);

// Answers the innermost open color dialog with a color, as if the user chose it, or cancels it
// with NULL. False if none is open. Any backend; UI thread only, so usually from a timer.
bool cxuiTestAnswerColorDialog(_In_opt_ const CxUiColor* color);

// Color dialogs open now. UI thread only.
int32 cxuiTestColorDialogs(void);

// Reads back from the platform whether w's native window belongs to owner's, or with
// CxUiWindowNone to none. False if either isn't realized or the platform can't tell. Any backend;
// UI thread only.
bool cxuiTestOwnedBy(_In_ CxUiWindow* w, _In_opt_ CxUiWindow* owner, _Out_ bool* owned);

// What the innermost null-backend file dialog was asked for, with platform paths.
typedef struct CxUiTestFileDialogInfo {
    uint32 mode;
    string title;
    string initialDir;
    string initialFile;
    int32 nfilters;
    int32 filterIndex;
    flags_t flags;
} CxUiTestFileDialogInfo;

// False unless a null-backend file dialog is open. UI thread only.
bool cxuiTestFileDialogInfo(_Out_ CxUiTestFileDialogInfo* out);
void cxuiTestFileDialogInfoDestroy(_Inout_ CxUiTestFileDialogInfo* info);

// What the innermost null-backend color dialog started with and its title, which out receives.
// False unless one is open. UI thread only.
bool cxuiTestColorDialogInfo(_Out_ CxUiColor* start, _Inout_ string* title);

// The innermost message box's window, if cxui built it rather than the platform; else NULL.
// Borrowed. UI thread only.
_Ret_maybenull_ CxUiWindow* cxuiTestBuiltMessageBox(void);

// Reads back from the native window whether it has no frame or title bar. False if w isn't
// realized or the platform can't tell. Any backend; UI thread only.
bool cxuiTestBorderless(_In_ CxUiWindow* w, _Out_ bool* borderless);

// Reads back from the native window whether its frame is dark. False if w isn't realized or the
// platform can't tell. Any backend; UI thread only.
bool cxuiTestFrameDark(_In_ CxUiWindow* w, _Out_ bool* dark);

// Reads back whether the window's native controls show which has focus, after hiding that first
// if hide is set. False if w isn't realized or the platform can't tell. Any backend; UI thread only.
bool cxuiTestFocusCues(_In_ CxUiWindow* w, bool hide, _Out_ bool* shown);

// How long the last paint pass, of any canvas or offscreen target, took in each part, in
// microseconds: the engine starting it, the drawing calls, and the engine finishing it
typedef struct CxUiTestPaintTimes {
    int64 begin, draw, end;
} CxUiTestPaintTimes;

// UI thread only.
void cxuiTestPaintTimes(_Out_ CxUiTestPaintTimes* out);

// Takes keyboard focus from every widget, as switching to another program does. Null backend
// only.
void cxuiTestBlur(void);

// How many times text has been measured, from the start of the null backend. UI thread only.
int32 cxuiTestFontMeasures(void);

// One operation a null-backend canvas was asked to draw, in physical pixels after the draw front
// end has converted units, clipped fills, and turned frames and straight lines into fills.
enum CxUiTestDrawOpKinds {
    CXUI_TESTOP_Fill,   // r, c
    CXUI_TESTOP_Line,   // a, b, width, c: a line at an angle
    CXUI_TESTOP_Clip,   // r: the new clip
    CXUI_TESTOP_Text,   // a (top left), r (a and the run's advance and line height), text, font, c
    CXUI_TESTOP_Cells,  // a, width (the first cell's width), b (all cells' width, and count), text, font, c
    CXUI_TESTOP_Image,  // r (destination), src (within the image holding the pixels), b (that image's size)
};

typedef struct CxUiTestDrawOp {
    uint32 kind;
    CxUiRect r;
    CxUiPoint a, b;
    int32 width;
    CxUiColor c;
    strref text;   // borrowed, like the operations
    int32 font;    // the null font's em size in pixels, which tells fonts apart in tests
    CxUiRect src;
} CxUiTestDrawOp;

typedef struct CxUiTestCanvasInfo {
    CxUiRect bounds;              // where the canvas was placed, in physical pixels
    bool visible;
    bool captured;                // has the mouse captured
    bool focused;                 // has keyboard focus
    uint32 cursor;                // CxUiCursors
    CxUiCursor* cursorImage;      // the image cursor shown, or NULL
    CxUiPoint warpedTo;           // where the pointer was last moved to, in physical pixels
    int32 warps;                  // times it was moved
    flags_t textInput;            // CxUiTextInputFlags
    CxUiRect caret;               // the text caret, in physical pixels
    flags_t keyCapture;           // CxUiKeyCaptureFlags
    uint32 lockMode;              // CxUiPointerLocks
    CxUiPoint lockAt;             // where the pointer lock began, or where the pointer was let go
    int32 paints;                 // paint passes so far
    CxUiRect lastDirty;           // the last pass's area, in physical pixels
    const CxUiTestDrawOp* ops;    // the last pass's operations, borrowed until the next pass
    int32 nops;
} CxUiTestCanvasInfo;

// Where the platform has a render view's native surface, read back from it
typedef struct CxUiTestSurfaceInfo {
    CxUiRect bounds;   // in physical pixels within the window's client area
    bool shown;
    bool canMark;      // the platform can draw a mark and read it back
    bool marked;       // what cxuiTestSurfaceMark() drew is still there
} CxUiTestSurfaceInfo;

// False unless the render view has its surface on a backend that can read it back. UI thread only.
bool cxuiTestSurfaceInfo(_In_ CxUiRenderView* v, _Out_ CxUiTestSurfaceInfo* out);
// Draws a mark on a render view's surface, as a renderer would. False where the platform can't.
// UI thread only.
bool cxuiTestSurfaceMark(_In_ CxUiRenderView* v);

// False unless cv has a null-backend peer. UI thread only.
bool cxuiTestCanvasInfo(_In_ CxUiCanvas* cv, _Out_ CxUiTestCanvasInfo* out);

// What a null-backend panel peer (a panel's or frame's) was last told
typedef struct CxUiTestPanelInfo {
    CxUiRect bounds;   // in physical pixels, within its host
    CxUiColor bg;
    CxUiColor border;
    int32 borderPx;
    bool visible;
    CxUiSize content;   // a scroll box's content, in physical pixels
    strref title;       // a frame's, borrowed
    CxUiColor titleColor;
    bool raised;        // put in front of the peers it overlaps
} CxUiTestPanelInfo;

// False unless w has a null-backend panel peer. UI thread only.
bool cxuiTestPanelInfo(_In_ CxUiWidget* w, _Out_ CxUiTestPanelInfo* out);

// What the platform shows for a menu item
typedef struct CxUiTestMenuItemInfo {
    string title;   // with its & mnemonic as cxui gave it, or without on macOS, which has none
    string hint;
    uint32 kind;    // CxUiMenuItemKind, as far as the platform shows it
    bool checked;
    bool enabled;
    bool submenu;   // opens a menu
} CxUiTestMenuItemInfo;

// How many items the platform's menu for m has; -1 if m has no native menu. UI thread only.
int32 cxuiTestMenuCount(_In_ CxUiMenu* m);
// The item the platform shows at position at of m. False if there is none. Free with
// cxuiTestMenuItemInfoDestroy. UI thread only.
bool cxuiTestMenuItem(_In_ CxUiMenu* m, int32 at, _Out_ CxUiTestMenuItemInfo* out);
void cxuiTestMenuItemInfoDestroy(_Inout_ CxUiTestMenuItemInfo* info);
// The user chooses an item through the platform, as from the menu; it may arrive after this
// returns. False where the platform can't do that. UI thread only.
bool cxuiTestMenuChoose(_In_ CxUiMenuItem* item);
// Whether the platform shows m as w's menu bar; m NULL asks whether w shows none. UI thread only.
bool cxuiTestWindowMenu(_In_ CxUiWindow* w, _In_opt_ CxUiMenu* m);

// What a text field's peer shows, read back from the platform
typedef struct CxUiTestTextInfo {
    string text;
    string cue;
    int32 selStart, selEnd, caret;   // code points
    int32 maxLength;
    bool readOnly;
    bool enabled;     // not the platform's disabled look
    bool focusable;
    bool focused;
    bool framed;
    bool visible;
    bool multiline;   // a text area
    bool wrap;        // a text area's lines break to fit
    bool atEnd;       // a text area is scrolled to its last line, as far as the platform can tell
    bool hasColors;   // a framed field's colors were set; the colors below are those (null backend)
    CxUiColor textColor, bg, cueColor;
    CxUiRect bounds;  // in physical pixels, within its host (null backend)
    int32 fontPx;     // the null font's em size (null backend)
} CxUiTestTextInfo;

// False unless the field is realized on a backend that can read it back. Free with
// cxuiTestTextInfoDestroy. UI thread only.
bool cxuiTestTextInfo(_In_ CxUiTextField* t, _Out_ CxUiTestTextInfo* out);
void cxuiTestTextInfoDestroy(_Inout_ CxUiTestTextInfo* info);
// The user types s over the selection, through the platform, as if from the keyboard; an empty s
// deletes the selection. False where the platform can't be driven. UI thread only.
bool cxuiTestType(_In_ CxUiTextField* t, _In_ strref s);
// Realized null-backend text fields measure w x h DIPs, as the platform's own measure would say;
// 0 for the estimate. Reset when cxui starts. UI thread only.
void cxuiTestSetTextNative(int32 w, int32 h);
// What a native control's peer shows, read back from the platform
typedef struct CxUiTestControlInfo {
    string text;       // as the platform has it: Windows keeps the `&`, GTK and macOS show it without
    bool isDefault;    // a button shown as the window's default
    uint32 checkState; // a check box's or radio button's CxUiCheckState
    string items;      // a list's or dropdown's items, or tabs' titles as shown, each followed by a line feed
    int32 value;       // a list's or dropdown's selected item, or the tab shown; -1 for none
    string selection;  // a list's selected items where any number can be, each followed by a space
    int32 top;         // the first item a list shows
    int32 pos, range;  // a slider's position of its range, from its low end; a progress bar's of 10000
    int32 tickEvery;   // a slider's positions between tick marks, or 0 (null backend)
    bool vertical;     // a slider's orientation
    bool indeterminate;   // a progress bar showing activity without a value
    bool enabled;
    bool visible;
    bool focused;
    uint32 align, ellipsis;   // a label's (null backend)
    bool wrap;                // a label's lines break to fit (null backend)
    // A label's height for its width as the platform would lay it out, in physical pixels like
    // bounds; 0 where unknown
    int32 neededHeight;
    CxUiColor color;   // a label's text color; what is around tabs' strip (null backend)
    CxUiRect bounds;   // in physical pixels, within its host (null backend)
    CxUiRect page;     // tabs' page surface, in physical pixels within the control (null, Windows)
    CxUiRect display;  // where the platform's tab control puts its pages, the same way (Windows)
    CxUiColor bg;      // tabs' page background (null backend)
    int32 fontPx;      // the null font's em size (null backend)
} CxUiTestControlInfo;

// False unless the native control is realized on a backend that can read it back. Free with
// cxuiTestControlInfoDestroy. UI thread only.
bool cxuiTestControlInfo(_In_ CxUiWidget* w, _Out_ CxUiTestControlInfo* out);
void cxuiTestControlInfoDestroy(_Inout_ CxUiTestControlInfo* info);
// Clicks a button, check box or radio button through the platform, as the mouse would. False
// where the platform can't be driven. UI thread only.
bool cxuiTestClick(_In_ CxUiWidget* w);
// Selects a list's or dropdown's item through the platform, as the user would, and with activate
// double-clicks a list's; picks a tab of CxUiTabs. False where the platform can't be driven. UI
// thread only.
bool cxuiTestChoose(_In_ CxUiWidget* w, int32 index, bool activate);
// Ctrl-clicks an item of a list where any number can be selected, through the platform, as the
// user would. False where the platform can't be driven. UI thread only.
bool cxuiTestToggle(_In_ CxUiList* l, int32 index);
// Drags a slider's thumb to the position for a value through the platform, as the user would,
// letting go of it with release. False where the platform can't be driven. UI thread only.
bool cxuiTestSlide(_In_ CxUiSlider* s, float64 value, bool release);
// How far a scroll box's content is scrolled, in physical pixels. False where the platform can't
// tell. UI thread only.
bool cxuiTestScrollPos(_In_ CxUiScrollBox* sb, _Out_ CxUiPoint* out);
// Turns the mouse wheel over a scroll box, toward the end for positive notches. False where the
// platform can't be driven. UI thread only.
bool cxuiTestScrollWheel(_In_ CxUiScrollBox* sb, int32 notches);
// The tip the platform has for a widget's peer, empty for none. False where the widget has no
// peer or the platform can't tell. UI thread only.
bool cxuiTestTip(_In_ CxUiWidget* w, _Inout_ string* out);

// Whether a window is the active one, which takes keyboard input: a display with no keyboard
// (headless Wayland) never activates one. False where the platform can't tell. UI thread only.
bool cxuiTestWindowActive(_In_ CxUiWindow* w, _Out_ bool* active);
// A key pressed and released through the platform's own input handling, which a focused native
// control also sees: the target is the focused widget or a window. False where the platform can't
// do that. UI thread only.
bool cxuiTestNativeKey(_In_ CxUiWidget* target, CxUiKey key, uint32 mods);
// A left click on a canvas through the platform's own input handling, at x, y in physical pixels
// relative to the canvas; it may arrive after this returns. False where the platform can't do
// that. UI thread only.
bool cxuiTestNativeClick(_In_ CxUiCanvas* cv, int32 x, int32 y);
// The wheel turned over a canvas through the platform's own input handling, so what cxui passes on
// reaches a scroll box around it; notches down are positive. False where the platform can't do
// that. UI thread only.
bool cxuiTestNativeWheel(_In_ CxUiCanvas* cv, int32 x, int32 y, int32 notches);

// Where layout last put a widget, in physical pixels relative to its window's client area. False
// if it has never been placed. Any backend; UI thread only.
bool cxuiTestBounds(_In_ CxUiWidget* w, _Out_ CxUiRect* out);

// Reads back from the platform whether a realized widget is shown inside its window. False if it
// isn't realized or the platform can't tell. Any backend; UI thread only.
bool cxuiTestShown(_In_ CxUiWidget* w, _Out_ bool* shown);

// Whether w's peer, or one inside it, is in front of the others at a point of its window's client
// area, in physical pixels. False if w isn't realized or the platform can't tell. UI thread only.
bool cxuiTestFrontmost(_In_ CxUiWidget* w, int32 x, int32 y, _Out_ bool* front);

// Layout passes a window has run. Any backend; UI thread only.
int32 cxuiTestLayoutPasses(_In_ CxUiWindow* w);

// Sets the null backend's main-monitor scale, used for windows realized after it. UI thread only.
void cxuiTestSetSystemScale(float32 scale);

// Puts text on the clipboard as another program would, with line ends unchanged. False where the
// platform can't. UI thread only.
bool cxuiTestClipboardForeign(_In_opt_ strref text);

// Reads the clipboard's text with line ends unchanged. UI thread only.
bool cxuiTestClipboardRaw(_Inout_ string* out);

// Replaces the null backend's monitors (at most 4), copying them, until it next starts, when it goes
// back to one 1920x1080 monitor at 60 Hz with a 40-pixel taskbar at the bottom. UI thread only.
void cxuiTestSetMonitors(_In_reads_(count) const CxUiMonitor* mons, int32 count);
// The platform says the monitors changed. Any backend; UI thread only.
void cxuiTestMonitorsChanged(void);

// Resizes a null-backend window as the user would, in device-independent pixels. UI thread only.
void cxuiTestResize(_In_ CxUiWindow* w, int32 width, int32 height);

// Has the platform repaint the whole canvas, as when it is uncovered, without marking anything
// dirty: the paint handler shouldn't run. False if the platform can't. Any backend; UI thread only.
bool cxuiTestExpose(_In_ CxUiCanvas* cv);

// The drawing engine a realized canvas uses, such as "gdi", "d2d" or "gsk"; NULL if it isn't
// realized. Any backend; UI thread only.
strref cxuiTestCanvasEngine(_In_ CxUiCanvas* cv);

// Pointer input as a platform reports it, in physical pixels relative to the canvas: cxui makes
// capture, Enter, Leave and double clicks of it. buttons are those held after the change. Any
// backend; UI thread only.
enum CxUiTestPointerKinds {
    CXUI_TESTPTR_Press,
    CXUI_TESTPTR_Release,
    CXUI_TESTPTR_Move,
    CXUI_TESTPTR_Left,   // the pointer left the canvas
};
void cxuiTestPointer(_In_ CxUiCanvas* cv, uint32 kind, uint32 button, uint32 buttons, uint32 mods, int32 x, int32 y);
// The platform took the mouse away while a button was held.
void cxuiTestCaptureLost(_In_ CxUiCanvas* cv);
CxUiResult cxuiTestWheel(_In_ CxUiCanvas* cv, float64 dx, float64 dy, uint32 mods, int32 x, int32 y);
// Ends the wheel gesture under way, as a pause in turning the wheel does. Any backend; UI thread
// only.
void cxuiTestWheelPause(void);
// Text typed into a canvas, as a platform reports it after the key. Any backend; UI thread only.
void cxuiTestText(_In_ CxUiCanvas* cv, _In_ strref text);
// Mouse motion while the pointer is locked to the canvas, in physical pixels. Any backend; UI
// thread only.
void cxuiTestPointerMotion(_In_ CxUiCanvas* cv, float32 dx, float32 dy, uint32 mods);
// The platform says the window became active or inactive. Any backend; UI thread only.
void cxuiTestActivate(_In_ CxUiWindow* w, bool active);
// The platform says nothing of the window can be seen, or something can again. Any backend; UI
// thread only.
void cxuiTestOccluded(_In_ CxUiWindow* w, bool occluded);
// Reads back from the platform whether the window is kept above others. False if it isn't
// realized or the platform can't tell. Any backend; UI thread only.
bool cxuiTestTopmost(_In_ CxUiWindow* w, _Out_ bool* topmost);
// The best CxUiPointerLocks mode the null backend gives, as a remote session limits it; reset to
// Raw when cxui starts. UI thread only.
void cxuiTestSetPointerLockMax(uint32 mode);

// How the UI thread's loop has run this session
typedef struct CxUiTestLoopStats {
    uint64 ticks;   // times the UI queue ran
    int64 next;     // the wait it last asked for, in microseconds; timeForever for none
} CxUiTestLoopStats;

// Reads the loop's counts without waking it. Any thread.
void cxuiTestLoopStats(_Out_ CxUiTestLoopStats* out);
// A key, as if pressed in target: a focused canvas, or a window. Any backend; UI thread only.
CxUiResult cxuiTestKey(_In_ CxUiWidget* target, CxUiKey key, uint32 mods, bool down);
// The null backend's double-click time, in microseconds, and distance in pixels.
void cxuiTestSetDoubleClick(int64 time, int32 distance);

// Where a realized canvas's top left is on the screen, in physical pixels. False where the platform
// can't tell (Linux). Any backend; UI thread only.
bool cxuiTestCanvasScreenOrigin(_In_ CxUiCanvas* cv, _Out_ CxUiPoint* out);

// Tells a window it has moved to a monitor with another scale. On Windows the window gets a real
// WM_DPICHANGED; false where the platform can't be told (Linux). UI thread only.
bool cxuiTestDpi(_In_ CxUiWindow* w, float32 scale);

// An offscreen target for a platform drawing engine, drawn through the same front end as a canvas,
// for pixel tests. Needs no running session; use it from one thread.
typedef struct CxUiTestOffscreen CxUiTestOffscreen;
typedef void (*CxUiTestDrawFn)(CxUiDraw* d, void* ctx);

// NULL if the platform has no engine for that CxUiDrawBackends value. Its contents start undefined.
CxUiTestOffscreen* cxuiTestOffscreenCreate(uint32 drawBackend, int32 w, int32 h);
// The engine's name, such as "gdi"
strref cxuiTestOffscreenEngine(_In_ CxUiTestOffscreen* o);
// One paint pass at scale 1, in pixels, clipped to dirty.
void cxuiTestOffscreenDraw(_In_ CxUiTestOffscreen* o, CxUiRect dirty, _In_ CxUiTestDrawFn fn, _In_opt_ void* ctx);
// A font at scale 1 measured by the target's engine
CxUiFont* cxuiTestOffscreenFont(_In_ CxUiTestOffscreen* o, _In_opt_ const CxUiFontDesc* desc);
// w * h top-down RGBA pixels; alpha is undefined.
void cxuiTestOffscreenRead(_In_ CxUiTestOffscreen* o, _Out_ uint8* rgba);
void cxuiTestOffscreenDestroy(_Inout_ CxUiTestOffscreen** o);

CX_C_END
