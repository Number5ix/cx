#pragma once

// The private interface between the shared core and each platform backend. One static table per
// platform; the core picks one in cxuiStart. Every function except wake is called on the UI
// thread.

#include <cx/cx.h>
#include <cx/string.h>
#include <cx/thread/atomic.h>
#include <cxui/cxui_shared.h>
#include <cxui/dialog.h>
#include <cxui/keys.h>
#include <cxui/test/cxuitest.h>

CX_C_BEGIN

// A framed text field's colors
typedef struct CxUiTextColors {
    CxUiColor text, bg, cue, selection, selectionText;
} CxUiTextColors;

// The tabs' colors: what is around the strip, the pages' background, the border, the titles, and
// a tab under the mouse
typedef struct CxUiTabsColors {
    CxUiColor around, page, border, text, hover;
} CxUiTabsColors;

typedef struct CxUiBackend {
    strref name;
    // Alt+letter presses a button or focuses what follows a label with that mnemonic; false where
    // the platform has no mnemonics (macOS), whose titles show them stripped.
    bool mnemonics;
    // Tabs' titles have mnemonics too; where they don't (Windows), the core gives them stripped.
    bool tabMnemonics;

    // ---- lifecycle ----
    bool (*init)(flags_t flags);
    void (*shutdown)(void);
    // For a platform whose UI thread is the process's main thread, already running its own loop
    // (AppKit); NULL elsewhere, where cxui runs a thread of its own with runOuter. Runs fn(ctx) on
    // the main thread soon, without waiting; false if the main thread isn't available to cxui.
    // Such a backend has no runOuter: it calls _cxuiHostedEnd from its loop once a shutdown has
    // been requested and no nested loop is running.
    bool (*hostRun)(void (*fn)(void* ctx), void* ctx);

    // ---- loop ----
    // The UI thread's outer loop. Returns once _cxuiLoopDone(exitFlag) is true and every nested
    // loop has unwound.
    void (*runOuter)(atomic(bool)* exitFlag);
    // A nested (modal) loop that runs until _cxuiLoopDone(exitFlag). It must keep ticking the UI
    // queue when woken and when the timer fires.
    void (*runNested)(atomic(bool)* exitFlag);
    // Handles platform events that are already pending, without blocking. True if more remain.
    bool (*flush)(void);
    // ANY THREAD. Must not block: makes the loop call _cxuiTick soon. The core coalesces calls.
    void (*wake)(void);
    // One timer for the UI queue's next scheduled work, in microseconds from now; timeForever
    // disarms. Re-arming replaces the previous deadline.
    void (*armTimer)(int64 delay);

    // ---- peers ----
    // Destroys a peer the core has already unlinked from its widget.
    void (*peerDestroy)(CxUiPeer* p);
    // Shows or hides a peer inside its window; peers start shown.
    void (*peerSetVisible)(CxUiPeer* p, bool visible);
    // A peer's effective enabled state changed; NULL where the core's input filtering is enough.
    void (*peerSetEnabled)(CxUiPeer* p, bool enabled);
    // Gives a canvas, text field or button keyboard focus; the platform reports the change
    // through _cxuiFocusChanged.
    void (*peerFocus)(CxUiPeer* p);
    // The keyboard moved focus in a window: from now on its native controls show which has focus,
    // where the platform hides that until the keyboard is used. NULL where nothing is needed.
    void (*windowFocusCues)(CxUiPeer* win);
    // Puts a peer in front of the others in its host, where it overlaps them. NULL where a new
    // peer already goes in front.
    void (*peerRaise)(CxUiPeer* p);
    // The text shown while the mouse rests over a peer, which may have several lines; NULL for
    // none. Not for windows.
    void (*peerSetTip)(CxUiPeer* p, strref tip);

    // ---- windows ----
    // Creates a hidden window from the model: title, size, position, the program's icon, and the
    // resolved theme (w->_dark), which must be in place before the window can first appear. Fills
    // in either dimension of w->_size that is 0 with the platform's default, and w->_scale and
    // w->_clientPx. Resizable unless w->_sizing is CXUI_SIZE_Fixed.
    CxUiPeer* (*windowCreate)(CxUiWindow* w);
    // The smallest client size the user may give the window, in DIPs.
    void (*windowSetMinSize)(CxUiPeer* p, CxUiSize dips);
    void (*windowSetResizable)(CxUiPeer* p, bool resizable);
    // Starts the platform's window move with the button of the mouse press being handled; the
    // core has already ended its capture. NULL where windows can't be moved.
    bool (*windowBeginDrag)(CxUiPeer* p);
    // Around a layout pass that may move several peers in the window, so the platform can move
    // them together; NULL where it already does.
    void (*windowLayoutBegin)(CxUiPeer* p);
    void (*windowLayoutEnd)(CxUiPeer* p);
    void (*windowShow)(CxUiPeer* p, uint32 mode);
    void (*windowHide)(CxUiPeer* p);
    void (*windowSetTitle)(CxUiPeer* p, strref title);
    void (*windowSetSize)(CxUiPeer* p, CxUiSize dips);
    // Both NULL where the platform can't place windows.
    bool (*windowGetPosition)(CxUiPeer* p, CxUiPoint* out);
    bool (*windowSetPosition)(CxUiPeer* p, CxUiPoint pos);
    // CxUiPlacementCaps the two below support
    flags_t placementCaps;
    // Reads the placement, setting out->valid to what holds.
    void (*windowGetPlacement)(CxUiPeer* p, CxUiPlacement* out);
    // Applies the parts of pl->valid the platform supports, the state only if withState; never
    // shows or hides the window. The rectangle is already clamped to the monitors.
    void (*windowSetPlacement)(CxUiPeer* p, const CxUiPlacement* pl, bool withState);
    // A theme change after creation; also reads w->_theme, for platforms where it is process-wide.
    void (*windowSetDark)(CxUiPeer* p, bool dark);
    // CXUI_Theme_Light or CXUI_Theme_Dark
    CxUiThemeMode (*systemTheme)(void);
    // A popup starts or ends: owner (may be NULL) for stacking, and centering on it or the screen
    // unless the window was placed (w->_hasPos). End is called after the window is hidden.
    void (*windowBeginModal)(CxUiPeer* p, CxUiPeer* owner);
    void (*windowEndModal)(CxUiPeer* p);
    // The window p belongs to (may be NULL), realized; a popup's owner stands in for it while the
    // popup runs, and it comes back at the end. When an owner's peer is destroyed, the windows it
    // owns are let go first.
    void (*windowSetOwner)(CxUiPeer* p, CxUiPeer* owner);
    // Moves p by other (CxUiNearPlaces), or with other NULL centers it on the screen, keeping it
    // on other's monitor; NULL where windows can't be placed.
    bool (*windowMoveNear)(CxUiPeer* p, CxUiPeer* other, uint32 where);
    // Takes input away from a window while a modal belongs to it; NULL where the platform's own
    // modality already does that.
    void (*windowSetEnabled)(CxUiPeer* p, bool enabled);
    // Enters a CxUiFullscreen mode on mon (NULL: the monitor the window is on, already matched
    // against the platform's list), or leaves fullscreen with CXUI_FULLSCREEN_None; the window is
    // shown. Reports entering and leaving through _cxuiWindowStateChanged with
    // CXUI_WINDOW_Fullscreen, now or once the platform has done it; leaving puts back the frame,
    // rectangle and state the window had before. False if the platform can't; NULL where it has no
    // fullscreen.
    bool (*windowSetFullscreen)(CxUiPeer* p, uint32 mode, const CxUiMonitor* mon);
    // The index, in what monitors gives now, of the monitor with most of the window; -1 if
    // unknown. NULL where the platform can't tell.
    int32 (*windowMonitor)(CxUiPeer* p);
    // Keeps the window above other programs' windows, or not; false if the platform can't. NULL
    // where it never can.
    bool (*windowSetTopmost)(CxUiPeer* p, bool on);

    // ---- scale and canvases ----
    // The main monitor's scale, for widgets not in a realized window; NULL for 1.0.
    float32 (*systemScale)(void);
    // Creates a canvas inside a window's or a panel's peer, with a back buffer from the draw ops for
    // c->_drawBackend; NULL where canvases aren't supported. Its bounds come from
    // canvasSetBounds.
    CxUiPeer* (*canvasCreate)(CxUiCanvas* c, CxUiPeer* parent);
    // Places the canvas within its parent's client area and resizes its back buffer. Never called
    // during the canvas's paint pass.
    void (*canvasSetBounds)(CxUiPeer* p, CxUiRect px);
    // Has the platform ask for a paint soon, covering at least px; the core keeps the dirty
    // region. Painting goes through _cxuiCanvasPaint.
    void (*canvasInvalidate)(CxUiPeer* p, CxUiRect px);
    // Paints the dirty region and shows it before returning.
    void (*canvasPaintNow)(CxUiPeer* p);
    // Keeps pointer input coming to the canvas while a button is held, even outside it; the
    // core starts it on the first press and ends it when the last button is released.
    void (*canvasCapture)(CxUiPeer* p, bool on);
    void (*canvasSetCursor)(CxUiPeer* p, uint32 cursor);
    // A cursor made from an image, which the core holds while it is set; NULL goes back to the
    // cursor canvasSetCursor gave. The backend makes its own native cursor from the image, at the
    // image's size in physical pixels. NULL where cursors can't be made from images.
    void (*canvasSetCursorImage)(CxUiPeer* p, CxUiCursor* cursor);
    // Moves the pointer to px, in physical pixels relative to the canvas; false where the platform
    // can't. NULL where it never can.
    bool (*canvasWarpPointer)(CxUiPeer* p, CxUiPoint px);
    // CxUiTextInputFlags: while on, what is typed into the canvas goes to _cxuiCanvasText after its
    // key, never control characters. False if the platform can't; NULL where it has no text input.
    bool (*canvasSetTextInput)(CxUiPeer* p, flags_t flags);
    // Where the canvas draws its caret, in physical pixels, for the system's typing windows; NULL
    // where nothing uses it.
    void (*canvasSetTextCaret)(CxUiPeer* p, CxUiRect px);
    // CxUiKeyCaptureFlags; the core already keeps the window's own handling of the canvas's keys
    // away. While on and the canvas has focus, every key and key-up reaches _cxuiKey before the
    // platform's shortcuts, except those the system keeps and the ones that close the window or
    // quit. False if a flag isn't supported; NULL where nothing is needed beyond the core's part.
    bool (*canvasSetKeyCapture)(CxUiPeer* p, flags_t flags);
    // Locks the pointer to the canvas in a CxUiPointerLocks mode at px (physical pixels, relative
    // to the canvas), or with CXUI_POINTERLOCK_Off lets it go, showing it at px. Returns the mode
    // achieved, Off if none; a call while locked changes the mode, keeping px. While locked, motion
    // goes to _cxuiCanvasPointerMotion, and presses and releases to _cxuiCanvasPointer as before.
    // The mouse stays with the canvas until both the lock and the core's capture (canvasCapture)
    // let go of it. NULL where the pointer can't be locked.
    uint32 (*canvasPointerLock)(CxUiPeer* p, uint32 mode, CxUiPoint px);
    // A render view's native surface, once it exists (a canvas peer created for a CxUiRenderView,
    // which has no draw target); its size is filled in by the core. A surface that comes after the
    // peer is reported through _cxuiRenderViewReady; it is destroyed only with the peer.
    bool (*surfaceGet)(CxUiPeer* p, CxUiNativeSurface* out);
    // The longest time between the presses of a double click, in microseconds, and how far apart
    // they may be, in physical pixels.
    void (*doubleClick)(int64* time, int32* distance);

    // ---- panels ----
    // A container's own native surface (kind CXUI_PEER_Panel) inside host, a window or another
    // panel; the peers of the widgets inside the container are created in it. Starts shown.
    CxUiPeer* (*panelCreate)(CxUiWidget* w, CxUiPeer* host);
    // Places the panel within its host, in physical pixels.
    void (*panelSetBounds)(CxUiPeer* p, CxUiRect px);
    // Its background, and a border borderPx pixels wide inside its edges (0 for none).
    void (*panelSetColors)(CxUiPeer* p, CxUiColor bg, CxUiColor border, int32 borderPx);
    // A frame's title in f and color, or none for an empty one: the top edge of the border runs
    // through the middle of the title's line, broken from indent pixels in for the text's width
    // and gap pixels each side of it. The frame is laid out with the line's room at the top.
    void (*panelSetTitle)(CxUiPeer* p, strref title, CxUiFont* f, CxUiColor color, int32 indent, int32 gap);
    // The background of a window's client area, around and behind its content.
    void (*windowSetBackground)(CxUiPeer* p, CxUiColor c);
    // The window's menu bar (a menu created as one), or NULL to take it away. The client area
    // keeps its size.
    void (*windowSetMenu)(CxUiPeer* p, CxUiPeer* menu);

    // ---- menus ----
    // A native menu for m (kind CXUI_PEER_Menu): a window's menu bar, or a menu an item opens.
    // Items are given by position; the item's title, hint, kind, check mark, effective enabled
    // state and submenu are read from it. Peers are destroyed with peerDestroy: a menu's own
    // items let go of their submenus first, which have peers of their own.
    CxUiPeer* (*menuCreate)(CxUiMenu* m, bool bar);
    // Puts an item at position at; an item that opens a menu has its realized already.
    void (*menuInsert)(CxUiPeer* menu, int32 at, CxUiMenuItem* item);
    // Takes out the item at position at, letting go of any submenu without destroying it.
    void (*menuRemove)(CxUiPeer* menu, int32 at);
    // The item at position at changed.
    void (*menuUpdate)(CxUiPeer* menu, int32 at, CxUiMenuItem* item);
    // The background of the platform's own dialogs in its light theme, which the built-in light
    // palette's CXUI_PAL_Panel takes; NULL to keep cxui's.
    CxUiColor (*systemPanelColor)(void);
    // A panel created for a CxUiScrollBox is a view that scrolls a content surface inside it,
    // where its children's peers are; it is placed and colored as any panel. Scroll bars show
    // only when the content is larger than the view.
    // The content's size, in physical pixels; the scroll position stays within it.
    void (*panelSetContent)(CxUiPeer* p, CxUiSize px);
    // Scrolls the content just enough for px, a rectangle on it, to be seen.
    void (*panelScrollTo)(CxUiPeer* p, CxUiRect px);
    // The room a scroll bar takes beside the content, in DIPs; NULL where they float over it.
    int32 (*scrollBarSize)(void);

    // ---- text fields ----
    // A native single-line edit inside host. framed: no border or background of its own, inside a
    // CxUiTextFrame that draws them. It reports edits through _cxuiTextChanged.
    CxUiPeer* (*textCreate)(CxUiTextField* t, CxUiPeer* host, bool framed);
    void (*textSetBounds)(CxUiPeer* p, CxUiRect px);
    // The natural size of a field showing columns characters of f, in DIPs; t->peer may be NULL.
    // NULL where the core's estimate from the font will do.
    CxUiSize (*textMeasure)(CxUiTextField* t, CxUiFont* f, int32 columns, bool framed);
    // Replaces the text; an edit the platform reports for it is ignored, as the field has the
    // text already.
    void (*textSetText)(CxUiPeer* p, strref text);
    // In code points, start <= end; caret at one of them.
    void (*textGetSelection)(CxUiPeer* p, int32* start, int32* end, int32* caret);
    // Selects start to end (code points, clamped), with the caret at end.
    void (*textSetSelection)(CxUiPeer* p, int32 start, int32 end);
    void (*textSetCue)(CxUiPeer* p, strref cue);
    void (*textSetMaxLength)(CxUiPeer* p, int32 maxLength);
    void (*textSetFont)(CxUiPeer* p, CxUiFont* f);
    // readOnly: the user can't edit. enabled false: the platform's disabled look, no focus.
    // focusable false: no focus, but the field's own colors (a disabled framed field).
    void (*textSetState)(CxUiPeer* p, bool readOnly, bool enabled, bool focusable);
    // A framed field's colors; NULL for the platform's.
    void (*textSetColors)(CxUiPeer* p, const CxUiTextColors* c);
    // A field without a frame takes these colors while its window is dark; NULL where the
    // platform's own fields follow a dark window.
    void (*textSetDarkColors)(CxUiPeer* p, const CxUiTextColors* c);
    // A text area (t is a CxUiTextArea) has several lines with `\n` between them, scroll bars, and
    // no cue; Enter inserts a line break, and Tab never inserts anything. Its natural size, in DIPs;
    // t->peer may be NULL. NULL where the core's estimate will do.
    CxUiSize (*textAreaMeasure)(CxUiTextArea* t, CxUiFont* f, int32 columns, int32 lines, bool framed);
    // Adds text at the end of an area, keeping the selection where it is; NULL where setting the
    // whole text will do.
    void (*textAppend)(CxUiPeer* p, strref text);
    // Scrolls an area to its last line; it may not have its size yet.
    void (*textScrollToEnd)(CxUiPeer* p);

    // ---- labels and buttons ----
    // Text and titles come as given, with `&` mnemonics and `\n` line breaks, for the backend to
    // show its platform's way. Buttons report clicks through _cxuiButtonClicked; labels never
    // take focus or input.
    CxUiPeer* (*labelCreate)(CxUiLabel* l, CxUiPeer* host);
    void (*labelSetText)(CxUiPeer* p, strref text);
    // align: CXUI_TEXT_Left, _Center or _Right; ellipsis: CxUiEllipsis, never with wrap
    void (*labelSetStyle)(CxUiPeer* p, uint32 align, uint32 ellipsis, bool wrap);
    // The text color of a label, check box or radio button: a label's own, or the palette's text
    // color (disabled text when inactive). NULL where the platform's colors are kept for check boxes
    // and radio buttons and labels have none.
    void (*controlSetColor)(CxUiPeer* p, CxUiColor c);
    // The natural size of a label with text in f, in DIPs, on one line for each `\n`; l->peer may
    // be NULL. NULL where the font's own measure will do.
    CxUiSize (*labelMeasure)(CxUiLabel* l, CxUiFont* f, strref text);
    // The height in DIPs of a wrapping label's text in f, broken into lines width DIPs across; 0
    // (or NULL) where the core's estimate will do, which l->peer being NULL may be.
    int32 (*labelHeightFor)(CxUiLabel* l, CxUiFont* f, strref text, int32 width);
    CxUiPeer* (*buttonCreate)(CxUiButton* b, CxUiPeer* host);
    void (*buttonSetTitle)(CxUiPeer* p, strref title);
    // Shows the button as the one Enter presses
    void (*buttonSetDefault)(CxUiPeer* p, bool isDefault);
    // The natural size of a button with title in f, in DIPs, before padding; b->peer may be NULL.
    CxUiSize (*buttonMeasure)(CxUiButton* b, CxUiFont* f, strref title);
    // Whether buttons take keyboard focus now; NULL for always. On macOS only with Full Keyboard
    // Access turned on.
    bool (*buttonsFocus)(void);

    // ---- check boxes and radio buttons ----
    // A check box, or a radio button when radio (each on its own: cxui keeps one radio of a group
    // selected). Clicks are reported through _cxuiButtonClicked; a check box's click always changes
    // its state, which the core then sets, and a selected radio button's changes nothing, so a
    // platform that changes the state itself is put right. Titles go through buttonSetTitle, colors
    // through controlSetColor.
    CxUiPeer* (*checkCreate)(CxUiWidget* w, CxUiPeer* host, bool radio);
    // CxUiCheckState; a radio button is only ever on or off
    void (*checkSetState)(CxUiPeer* p, uint32 state);
    // The natural size of a check box or radio button with title in f, in DIPs; w->peer may be NULL.
    // NULL where the core's estimate will do.
    CxUiSize (*checkMeasure)(CxUiWidget* w, CxUiFont* f, strref title, bool radio);

    // ---- lists and dropdowns ----
    // A list box, or a dropdown when dropdown. The user's selections are reported through
    // _cxuiListChosen, and a list's double clicks through _cxuiListActivated; keys reach the core
    // first, as a button's do, except while a dropdown's list is open.
    CxUiPeer* (*listCreate)(CxUiWidget* w, CxUiPeer* host, bool dropdown);
    // Replaces removed items from at with the added ones in items (either count may be 0); the
    // selection is set again afterwards.
    void (*listSplice)(CxUiPeer* p, int32 at, int32 removed, const string* items, int32 added);
    // Selects an item, or none for -1, without reporting it
    void (*listSetValue)(CxUiPeer* p, int32 value);
    // Scrolls a list so the item shows
    void (*listShow)(CxUiPeer* p, int32 index);
    // The natural size, in DIPs, of a list rows items high, or of a dropdown, whose widest item is
    // widest DIPs across in f; w->peer may be NULL. NULL where the core's estimate will do.
    CxUiSize (*listMeasure)(CxUiWidget* w, CxUiFont* f, int32 widest, int32 rows, bool dropdown);
    // The background and text colors, a text field's; NULL where the platform's are kept.
    void (*listSetColors)(CxUiPeer* p, CxUiColor bg, CxUiColor text);
    // A list where any number can be selected (its widget's _select): selects the items whose
    // mark isn't 0 and unselects the others, without reporting it; and reads them back. Changes
    // the user makes are reported through _cxuiListMarksChanged.
    void (*listSetMarks)(CxUiPeer* p, const uint8* marks, int32 count);
    void (*listGetMarks)(CxUiPeer* p, uint8* marks, int32 count);
    // The first item a list shows, and scrolling to make one the first, as far as it goes
    int32 (*listTopItem)(CxUiPeer* p);
    void (*listSetTopItem)(CxUiPeer* p, int32 index);

    // ---- sliders and progress bars ----
    // A slider works in whole positions from 0 to its range, from its low end: the left, or the
    // bottom when vertical. The user's moves are reported through _cxuiSliderMoved, done once
    // the user lets go of the thumb and for each move with a key.
    CxUiPeer* (*sliderCreate)(CxUiWidget* w, CxUiPeer* host, bool vertical);
    // range positions; pageStep of them for Page Up and Down; a tick mark every tickEvery of them
    // from the low end, or none for 0
    void (*sliderSetRange)(CxUiPeer* p, int32 range, int32 pageStep, int32 tickEvery);
    // Never reported
    void (*sliderSetPos)(CxUiPeer* p, int32 pos);
    // The natural size, in DIPs, of a slider length DIPs long; w->peer may be NULL. NULL where the
    // core's estimate will do.
    CxUiSize (*sliderMeasure)(CxUiWidget* w, int32 length, bool vertical, bool ticks);
    CxUiPeer* (*progressCreate)(CxUiWidget* w, CxUiPeer* host);
    // fraction from 0 to 1, or activity with no value when indeterminate
    void (*progressSet)(CxUiPeer* p, float64 fraction, bool indeterminate);
    // The natural size, in DIPs, of a progress bar length DIPs long; NULL for the core's estimate
    CxUiSize (*progressMeasure)(CxUiWidget* w, int32 length);

    // ---- tabs ----
    // A tab control (kind CXUI_PEER_Tabs) with a page surface inside it, which holds the pages'
    // peers. Titles come with `&` mnemonics. The user's picks, by mouse or by the control's own
    // keys, are reported through _cxuiTabsPicked; it takes focus as a button does.
    CxUiPeer* (*tabsCreate)(CxUiTabs* t, CxUiPeer* host);
    void (*tabsInsert)(CxUiPeer* p, int32 at, strref title);
    void (*tabsRemove)(CxUiPeer* p, int32 at);
    void (*tabsSetTitle)(CxUiPeer* p, int32 at, strref title);
    // Shows a tab as picked, or none for -1, without reporting it
    void (*tabsSetValue)(CxUiPeer* p, int32 index);
    // Where the page surface is within the control, in physical pixels
    void (*tabsSetPage)(CxUiPeer* p, CxUiRect px);
    // Holds the page surface's drawing while the page shown changes, and with hold false draws it
    // all again. NULL where the platform draws the change at once anyway.
    void (*tabsHold)(CxUiPeer* p, bool hold);
    // The page surface's background, and colors for a platform whose tabs have no dark look and
    // are drawn by the backend in a dark window
    void (*tabsSetColors)(CxUiPeer* p, const CxUiTabsColors* c);
    // The room around the pages for f's strip and the border, in DIPs, and in *stripWidth the
    // width that shows every tab; t->peer may be NULL. For tabs with CXUI_TABS_Multiline, `width`
    // is the control's, in DIPs, which the tabs wrap within (0 when it isn't known yet); others
    // ignore it, as does a platform that never wraps tabs. NULL where the core's estimate will do.
    CxUiEdges (*tabsMeasure)(CxUiTabs* t, CxUiFont* f, int32 width, int32* stripWidth);

    // For all of the native controls above
    void (*controlSetBounds)(CxUiPeer* p, CxUiRect px);
    void (*controlSetFont)(CxUiPeer* p, CxUiFont* f);

    // ---- desktop ----
    // Fills out (already cleared) with the monitors, the primary first.
    void (*monitors)(sa_CxUiMonitor* out);
    // Text with \n line ends; the backend converts them where the platform's differ.
    bool (*clipboardSetText)(strref text);
    // May run a nested loop while another program answers; must return when cxui shuts down.
    bool (*clipboardGetText)(string* out);

    // ---- dialogs ----
    // Runs a message box in a loop that keeps the UI queue running; parent may be NULL. Returns
    // the button from 1, or 0. Must return when cxui shuts down. A custom box has 1 to 3 titles,
    // and its cancel button in range or 0; one the platform can't show returns CXUI_MB_Build, for
    // the core to build.
    int32 (*messageBox)(const CxUiMessageBoxDesc* desc, CxUiPeer* parent);
    // Runs a file or folder dialog the same way. The descriptor's paths are the platform's, its
    // mode and filter index in range; *out receives the platform path chosen. False if nothing
    // was.
    bool (*fileDialog)(const CxUiFileDialogDesc* desc, CxUiPeer* parent, string* out);
    // Runs a color dialog the same way, starting at *color, which receives the color chosen with
    // the alpha it came with. False if none was.
    bool (*colorDialog)(const CxUiColorDialogDesc* desc, CxUiPeer* parent, CxUiColor* color);

    // ---- tests ----
    // Reads back whether the native frame is dark; false if the platform can't tell.
    bool (*testFrameDark)(CxUiPeer* p, bool* dark);
    // Reads back whether the native window has no frame; false if the platform can't tell.
    bool (*testBorderless)(CxUiPeer* p, bool* borderless);
    // Reads back whether a window's native controls show focus, hiding it first if asked; false
    // if the platform can't tell.
    bool (*testFocusCues)(CxUiPeer* p, bool hide, bool* shown);
    // Answers the innermost open message box with a button from 1; false if there is none.
    bool (*testAnswerMessageBox)(int32 button);
    // Answers the innermost open file dialog with a platform path, or cancels it with NULL;
    // false if there is none.
    bool (*testAnswerFileDialog)(strref path);
    // Picks a color in the innermost open color dialog and presses OK, or Cancel for NULL; false
    // if none is open.
    bool (*testAnswerColorDialog)(const CxUiColor* color);
    // Reads back whether the native window belongs to owner (NULL: to none); false if the platform
    // can't tell.
    bool (*testOwnedBy)(CxUiPeer* p, CxUiPeer* owner, bool* owned);
    // Where a render view's surface is and whether a mark drawn on it survives; drawing the mark
    // as a renderer would. NULL where they can't be done.
    bool (*testSurfaceInfo)(CxUiPeer* p, CxUiTestSurfaceInfo* out);
    bool (*testSurfaceMark)(CxUiPeer* p);
    // Has the platform repaint the whole canvas, as when it is uncovered, without marking it
    // dirty; false if the platform can't.
    bool (*testExpose)(CxUiPeer* p);
    // Where the canvas's top left is on the screen, in physical pixels; NULL where unknown.
    bool (*testScreenOrigin)(CxUiPeer* p, CxUiPoint* out);
    // Whether the platform shows a peer inside its window; false if it can't tell.
    bool (*testPeerShown)(CxUiPeer* p, bool* shown);
    // Whether p, or a peer inside it, is in front at a point of win's client area, in physical
    // pixels.
    bool (*testFrontmost)(CxUiPeer* win, CxUiPeer* p, CxUiPoint px, bool* front);
    // Whether the window is the active one, which keyboard input goes to; NULL where unknown.
    bool (*testWindowActive)(CxUiPeer* p, bool* active);
    // Whether the native window is kept above others; NULL where it can't be read back.
    bool (*testTopmost)(CxUiPeer* p, bool* topmost);
    // A text field's state as the platform has it; NULL where it can't be read back.
    bool (*testTextInfo)(CxUiPeer* p, CxUiTestTextInfo* out);
    // A label's or button's state as the platform has it; NULL where it can't be read back.
    bool (*testControlInfo)(CxUiPeer* p, CxUiTestControlInfo* out);
    // Clicks a button through the platform, as the mouse would; NULL where that can't be done.
    bool (*testClick)(CxUiPeer* p);
    // Selects a list's or dropdown's item as the user would, and double-clicks a list's with
    // activate; NULL where that can't be done.
    bool (*testChoose)(CxUiPeer* p, int32 index, bool activate);
    // Ctrl-clicks an item of a list where any number can be selected, as the user would; NULL
    // where that can't be done.
    bool (*testToggle)(CxUiPeer* p, int32 index);
    // Drags a slider's thumb to a position as the user would, letting go of it with release;
    // NULL where that can't be done.
    bool (*testSlide)(CxUiPeer* p, int32 pos, bool release);
    // How far a scroll box's content is scrolled, in physical pixels; NULL where unknown.
    bool (*testScrollPos)(CxUiPeer* p, CxUiPoint* out);
    // The tip the platform has for a peer; false where it can't be read back.
    bool (*testTip)(CxUiPeer* p, string* out);
    // Turns the mouse wheel over a scroll box's view, as the user would; NULL where that can't be
    // done.
    bool (*testScrollWheel)(CxUiPeer* p, int32 notches);
    // Types s over the selection as the user would; NULL where that can't be done.
    bool (*testType)(CxUiPeer* p, strref s);
    // Presses and releases a key through the platform's input handling; NULL where that can't be
    // done.
    bool (*testNativeKey)(CxUiPeer* p, CxUiKey key, uint32 mods);
    // Clicks the left button on a canvas at px, in physical pixels, through the platform's input
    // handling; NULL where that can't be done.
    bool (*testNativeClick)(CxUiPeer* p, CxUiPoint px);
    // Turns the wheel over a canvas at px, through the platform's input handling, which passes on
    // what cxui doesn't take; notches down are positive. NULL where that can't be done.
    bool (*testNativeWheel)(CxUiPeer* p, CxUiPoint px, int32 notches);
    // What a menu shows: how many items, and the one at a position; NULL where it can't be read.
    int32 (*testMenuCount)(CxUiPeer* menu);
    bool (*testMenuItem)(CxUiPeer* menu, int32 at, CxUiTestMenuItemInfo* out);
    // Chooses the item at a position as the user would; NULL where that can't be done.
    bool (*testMenuChoose)(CxUiPeer* menu, int32 at);
    // The menu peer the window shows as its menu bar, or NULL.
    CxUiPeer* (*testWindowMenu)(CxUiPeer* win);
    // Puts text on the clipboard as another program would, unconverted; NULL where that can't be
    // done.
    bool (*testClipboardForeign)(strref text);
    // Reads the clipboard's text unconverted; NULL where that is what clipboardGetText gives.
    bool (*testClipboardRaw)(string* out);
    // Tells the window its scale changed, as the platform does when it moves to another monitor;
    // NULL where that can't be done.
    bool (*testForceDpi)(CxUiPeer* p, float32 scale);
} CxUiBackend;

extern const CxUiBackend _cxuiNullBackend;
extern const CxUiBackend _cxuiWin32Backend;
extern const CxUiBackend _cxuiGtk4Backend;
extern const CxUiBackend _cxuiAppKitBackend;

CX_C_END
