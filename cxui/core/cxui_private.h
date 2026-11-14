#pragma once

#include <cxui.h>

#include <cx/log.h>
#include <cx/thread.h>
#include <cx/time.h>

#include "cxuibackend.h"

CX_C_BEGIN

extern LogChannel* CxUiLogChannel;

#undef LOG_CHANNEL
#define LOG_CHANNEL CxUiLogChannel

// State of the running UI session. Set by cxuiStart before the UI thread exists and cleared after
// it has exited, so the UI thread can read it without locking.
typedef struct CxUiRuntime {
    const CxUiBackend* backend;
    TaskQueue* queue;
    int64 tickBudget;
    // Handler calls on the stack (UI thread only). Peer teardown waits until it is back to 0.
    int32 dispatchDepth;
    // Counts sessions, so state left from an earlier one (a timer's running flag) can be told apart.
    uint32 session;
    // Set by cxuiShutdown; every loop, nested ones included, unwinds when it sees it.
    atomic(bool) exitRequested;
    // A wake has been requested and _cxuiTick hasn't started since.
    atomic(bool) wakePending;
    // Message boxes open (UI thread only)
    int32 messageBoxes;
    int32 fileDialogs;
    int32 colorDialogs;
    // Message boxes built from cxui widgets, innermost first
    struct BuiltBox* builtBoxes;
    // The canvas the mouse is captured by, while a button is held over it; not a reference,
    // cleared when it is unrealized (UI thread only)
    CxUiCanvas* capture;
    // Focus is going to a widget already on screen, such as one clicked, which needs no
    // scrolling into view (UI thread only)
    bool focusInPlace;
    // The wheel gesture under way: when its last turn came, the window, and the canvas that took
    // it, or NULL if it was passed on to the platform. Compared, never used (UI thread only).
    int64 wheelAt;
    CxUiWindow* wheelWin;
    CxUiCanvas* wheelTo;
    // Keys down, by CxUiKey, so repeats can be told apart on every platform (UI thread only)
    uint32 keysDown[(CXUI_KEY_Count + 31) / 32];
    // Render views whose surface event said the surface is there (UI thread only)
    int32 surfaces;
    // Times the UI queue has run this session, and the wait it asked for last, for tests; written
    // on the UI thread, read from any
    atomic(uint64) ticks;
    atomic(int64) nextWake;
} CxUiRuntime;

extern CxUiRuntime _cxui;

// Tears down a session hosted on the main thread (CxUiBackend::hostRun). UI thread, from the
// platform's own loop with no nested loop running.
void _cxuiHostedEnd(void);

// Runs queued work for up to the tick budget, then arms the backend's timer for whatever is next.
// Backends call it when woken and when their timer fires; nested calls are fine.
void _cxuiTick(void);

// The UI queue's wake hook. ctx is the backend.
void _cxuiWakeHook(_In_ TaskQueue* tq, _In_ void* ctx);

// True when a loop running until *flag should return: its own flag, or cxui shutting down.
_meta_inline bool _cxuiLoopDone(_In_ atomic(bool)* flag)
{
    return atomicLoad(bool, flag, Acquire) || atomicLoad(bool, &_cxui.exitRequested, Acquire);
}

// ---- widgets and peers (cxuiwidget.c) ----

// Every backend's peer struct starts with this. w, prev, next and live are guarded by the peer
// lock, because a widget's last reference can drop on any thread.
struct CxUiPeer {
    CxUiWidget* w;   // NULL once the widget lets go; events for it are then dropped
    CxUiPeer* prev;
    CxUiPeer* next;
    uint8 kind;      // CxUiPeerKind, set by the backend
    bool live;       // on the core's list of peers that still need destroying
    bool hidden;     // the core hid it through peerSetVisible
};

typedef enum CxUiPeerKind {
    CXUI_PEER_Other = 0,
    CXUI_PEER_Window,
    CXUI_PEER_Canvas,
    // A container's own native surface, which holds its children's peers
    CXUI_PEER_Panel,
    CXUI_PEER_Text,
    // A menu bar, or a menu an item opens
    CXUI_PEER_Menu,
    // Native controls, from here to the end; each backend has one peer struct for them
    CXUI_PEER_Label,
    CXUI_PEER_Button,
    CXUI_PEER_Check,
    CXUI_PEER_Radio,
    CXUI_PEER_List,
    CXUI_PEER_Dropdown,
    CXUI_PEER_Slider,
    CXUI_PEER_Progress,
    // A tab control, whose page surface holds its pages' peers
    CXUI_PEER_Tabs,
} CxUiPeerKind;

#define _cxuiPeerIsControl(kind) ((kind) >= CXUI_PEER_Label)

// True for a widget that public calls may act on. UI thread only.
bool _cxuiLive(_In_opt_ CxUiWidget* w);

// Takes ownership of a new peer for w, which must not have one. UI thread only.
void _cxuiPeerAttach(_In_ CxUiWidget* w, _In_ CxUiPeer* p);

// Destroys w's peer, or queues that until no handler is running. UI thread only.
void _cxuiUnrealize(_In_ CxUiWidget* w);

// Peer teardown deferred while handlers ran. UI thread, with no handler running.
void _cxuiFlushDeferred(void);

// Destroys every peer left at the end of a session. UI thread, after the queue has stopped.
void _cxuiPeersDestroyAll(void);

// Lazily created state the widget code shares with the runtime.
void _cxuiWidgetInit(void);

// Backends wrap each call into the core from a native event in these. Peer teardown waits for
// the outermost Leave, so a handler that destroys a widget can't free the peer while the backend
// is still using it; after Leave, the backend must not touch the peer again.
_meta_inline void _cxuiNativeEnter(void)
{
    _cxui.dispatchDepth++;
}
void _cxuiNativeLeave(void);

// ---- windows (cxuiwindow.c) ----

// Applies a window descriptor, for a subclass's factory before objInstInit
void _cxuiWindowSetup(_Inout_ CxUiWindow* self, _In_ const CxUiWindowDesc* desc);

// Private CxUiWidget::state bits, above the public CxUiWidgetStates
enum {
    // Set on every CxUiWindow, so dispatch can find a widget's window without a class check.
    CXUI_WS_IsWindow = 0x80000000,
    // The widget's own visible and enabled settings, off
    CXUI_WS_Hidden   = 0x00000100,
    CXUI_WS_Disabled = 0x00000200,
    // Disabled itself or inside a disabled container: gets no input
    CXUI_WS_Inactive = 0x00000400,
    // Its natural size must be measured again; it must be arranged again. Both are set on every
    // ancestor up to the window, except under a hidden widget, whose subtree layout skips.
    CXUI_WS_MeasureDirty = 0x00000800,
    CXUI_WS_ArrangeDirty = 0x00001000,
    // Layout has placed it at least once
    CXUI_WS_Placed = 0x00002000,
};

// The window w is in, or NULL. UI thread only.
_meta_inline CxUiWindow* _cxuiWindowOf(_In_ CxUiWidget* w)
{
    while (w->parent)
        w = w->parent;
    return (w->state & CXUI_WS_IsWindow) ? (CxUiWindow*)w : NULL;
}

// Takes w off the screen at once, ahead of its deferred unrealize, and drops the runtime's
// reference. Part of cxuiDestroy.
void _cxuiWindowDestroyed(_In_ CxUiWindow* w);
// Destroys the window's menu bar, as it is destroyed.
void _cxuiWindowDestroyMenu(_In_ CxUiWindow* w);

// Creates the native menus for m and the submenus inside it, and puts its items in. UI thread.
void _cxuiMenuRealize(_In_ CxUiMenu* m, bool bar);
// Destroys the native menus of m and the submenus inside it. UI thread.
void _cxuiMenuUnrealize(_In_ CxUiMenu* m);
// The user chose a menu item; backends call it outside _cxuiNativeEnter/Leave.
void _cxuiMenuChosen(_In_ CxUiMenuItem* item);

// Call just before a modal loop starts. The task opening it was queued with a notify that leaves
// the queue's manager tick for after the task returns, and coalesces later wakes into that one,
// so work the task queued (a timer it started) would wait until the modal ended. This makes the
// modal loop tick at once.
_meta_inline void _cxuiModalLoopStarting(void)
{
    _cxui.backend->armTimer(0);
}

// The windows one modal disables: its parent, or every other realized window. Each window counts
// the modals disabling it and is enabled again when the last ends.
typedef struct CxUiModalLock {
    sa_CxUiWindow windows;
} CxUiModalLock;

// UI thread. except is the modal's own window, or NULL.
void _cxuiModalBegin(_Out_ CxUiModalLock* lock, _In_opt_ CxUiWindow* parent, _In_opt_ CxUiWindow* except);
void _cxuiModalEnd(_Inout_ CxUiModalLock* lock);

// The system's light or dark setting changed; backends call this inside _cxuiNativeEnter/Leave.
// Windows that follow the system resolve again.
void _cxuiSystemThemeChanged(void);

// Tells every widget in w's subtree that the palette or theme it resolves may have changed.
void _cxuiRestyleTree(_In_ CxUiWidget* w);

// The built-in light or dark palette, borrowed; it lasts for the process.
_Ret_valid_ CxUiPalette* _cxuiPaletteBuiltin(bool dark);

// Destroys the realized windows that closing destroys, calling destroyNotify throughout; the
// others outlive the session. UI thread, at the end of a session, after the shutdown listeners
// and while the backend still works.
void _cxuiWindowsEndSession(void);
// Runs the layout passes queued for realized windows now (UI thread only)
void _cxuiWindowsLayoutQueued(void);
// Drops the runtime's reference to every realized window. UI thread, at the end of a session,
// after the peers are gone.
void _cxuiWindowsRelease(void);

// ---- dialogs (cxuidialog.c) ----

// What a backend's messageBox returns for a box it can't show, which the core then builds
#define CXUI_MB_Build (-1)

// Presses a button of the innermost message box, if the core built it.
bool _cxuiTestAnswerBuiltBox(int32 button);

// ---- desktop (cxuidesktop.c) ----

// Moves a placement's rectangle onto the nearest monitor if it would be out of reach; with only a
// size, shrinks it to fit the largest monitor.
void _cxuiPlacementClamp(_Inout_ CxUiPlacement* pl);

// Where a window win's size goes by other (CxUiNearPlaces), kept on the work area of the monitor
// nearest other's center. In the platform's screen units.
CxUiPoint _cxuiNearPoint(CxUiRect win, CxUiRect other, uint32 where);

// Native window events. Backends call these from inside _cxuiNativeEnter/Leave. A peer's widget
// stays valid while realized, because the runtime holds a reference to each realized window.
void _cxuiWindowCloseRequested(_In_ CxUiPeer* p, uint32 reason);
void _cxuiWindowResized(_In_ CxUiPeer* p, CxUiSize px, CxUiSize dips);
void _cxuiWindowMoved(_In_ CxUiPeer* p, CxUiPoint pos);
void _cxuiWindowStateChanged(_In_ CxUiPeer* p, uint32 state);
void _cxuiWindowActivated(_In_ CxUiPeer* p, bool active);
// The window's scale changed, and its client area is now px. Report the new size here rather
// than through _cxuiWindowResized.
void _cxuiWindowDpiChanged(_In_ CxUiPeer* p, float32 scale, CxUiSize px);

// ---- layout (cxuilayout.c) ----

// 1/256 DIP, the unit of CxUiLayoutRect
#define CXUI_FIX 256

// A container calls Attached after linking child to itself (child->parent set), and Detached after
// unlinking it (child->parent cleared). Detached destroys the peers in child's subtree.
void _cxuiLayoutAttached(_In_ CxUiWidget* parent, _In_ CxUiWidget* child);
void _cxuiLayoutDetached(_In_ CxUiWidget* parent, _In_ CxUiWidget* child);

// Something that can change w's natural size changed. Marks it and its ancestors, and queues a
// layout pass for a realized window.
void _cxuiLayoutDirty(_In_ CxUiWidget* w);

// The list containers' shared CxUiContainer methods (cxuibox.c)
bool _cxuiBoxBaseInsert(_In_ CxUiBoxBase* self, _In_ CxUiWidget* child, int32 at);
bool _cxuiBoxBaseDetach(_In_ CxUiBoxBase* self, _In_ CxUiWidget* child);
_Ret_maybenull_ CxUiWidget* _cxuiBoxBaseChild(_In_ CxUiBoxBase* self, int32 i);
// Scrolls every scroll box around w just enough for w to be seen
void _cxuiScrollIntoView(_In_ CxUiWidget* w);
// A box's settings from its descriptor, for it and its subclasses
void _cxuiBoxSetup(_Inout_ CxUiBox* self, _In_ const CxUiBoxDesc* desc);

// A grid starts or stops showing a list store, which tells it of changes (cxuigridstore.c)
void _cxuiListStoreWatch(_In_ CxUiListStore* self, _In_ CxUiGrid* g);
void _cxuiListStoreUnwatch(_In_ CxUiListStore* self, _In_ CxUiGrid* g);
void _cxuiTreeStoreWatch(_In_ CxUiTreeStore* self, _In_ CxUiGrid* g);
void _cxuiTreeStoreUnwatch(_In_ CxUiTreeStore* self, _In_ CxUiGrid* g);

// The tabs' native control: tells it the page shown (cxuicontainers.c)
void _cxuiTabsSync(_In_ CxUiTabs* self);
// A render view's native surface now exists; backends call it when it does, after the peer is
// created. The surface event follows from the UI queue.
void _cxuiRenderViewReady(_In_ CxUiPeer* p);
// Raises the surface event for every render view in w's subtree whose surface is going, before its
// peer is destroyed. UI thread.
void _cxuiSurfacesLost(_In_ CxUiWidget* w);

// The user picked a tab; backends call it inside _cxuiNativeEnter/Leave.
void _cxuiTabsPicked(_In_ CxUiPeer* p, int32 index);
// Alt with a mnemonic: whether a tab has it, and picking that tab as the user would
bool _cxuiTabsHasMnemonic(_In_ CxUiTabs* self, CxUiKey key);
bool _cxuiTabsMnemonic(_In_ CxUiTabs* self, CxUiKey key);

// What is behind w: the background of the nearest panel around it, or its palette's window
// color (cxuicontainers.c)
CxUiColor _cxuiBackgroundOf(_In_ CxUiWidget* w);
// For the sash: the first pane's size in DIPs, and moving it to first DIPs as the user drags
int32 _cxuiSplitFirst(_In_ CxUiSplit* self);
void _cxuiSplitDragTo(_In_ CxUiSplit* self, int32 first);

// Which children w shows changed (a zbox switched pages): the next pass realizes, shows and
// hides peers to match.
void _cxuiLayoutNeedWalk(_In_ CxUiWidget* w);

// w's own visible or enabled setting changed.
void _cxuiLayoutVisibilityChanged(_In_ CxUiWidget* w);
void _cxuiLayoutEnabledChanged(_In_ CxUiWidget* w);

// For measure() and arrange() implementations:
// - whether a child takes part in layout (visible and not floating)
// - a child's natural size in DIPs, measured if needed
// - a child's height in DIPs when given width DIPs across: its natural height unless its height
//   depends on its width (_hfw), remembered for the last width asked about
// - the axes a child expands in, given its container's main axis (CXUI_EXPAND_H or _V; 0 for none)
// - places a child, then arranges its own children
// - a layout rectangle in physical pixels within the window, rounded as children's edges are
// - the peers inside w's surface must be placed again: it moved within the window
bool _cxuiLayoutTakesSpace(_In_ CxUiWidget* child);
CxUiSize _cxuiLayoutNatural(_In_ CxUiWidget* child);
int32 _cxuiLayoutHeightFor(_In_ CxUiWidget* child, int32 width);
uint32 _cxuiLayoutExpand(_In_ CxUiWidget* child, uint32 mainAxis);
void _cxuiLayoutPlace(_In_ CxUiWidget* child, CxUiLayoutRect r);
CxUiRect _cxuiLayoutPx(CxUiLayoutRect r);
void _cxuiLayoutHostMoved(_In_ CxUiWidget* w);

// A length in DIPs, at the current layout pass's scale; characters are w's font's. Axis 0 is
// across, 1 down.
int32 _cxuiLenDip(_In_ CxUiWidget* w, CxUiLen len, int axis);

// Runs a window's pending layout now, unless it isn't realized or a pass is already running.
void _cxuiLayoutFlush(_In_ CxUiWindow* w);

// A widget's font changed: it and everything inside it measure again.
void _cxuiLayoutDirtyTree(_In_ CxUiWidget* w);
// The size cxuiWindowFit sizes a window to: its content's natural size, except that a split is
// big enough for its fraction to give each pane its natural size.
CxUiSize _cxuiLayoutFitSize(_In_ CxUiWindow* win);
// Measuring for _cxuiLayoutFitSize
bool _cxuiLayoutFitting(void);
// A split was measured: from now on, sizing its window to its content measures the fit size.
void _cxuiLayoutSplitSeen(void);

// Runs a layout pass still queued for p's window. Backends call it, inside
// _cxuiNativeEnter/Leave, before painting a canvas and when the platform lays out the window, so
// neither shows stale geometry.
void _cxuiLayoutNow(_In_ CxUiPeer* p);

// The client size a window should start at, measuring its content at the main monitor's scale.
CxUiSize _cxuiLayoutStartSize(_In_ CxUiWindow* w);


// ---- input (cxuiinput.c) ----

// Raw pointer input on a canvas, as the platform reports it. Backends call these inside
// _cxuiNativeEnter/Leave; the core makes capture, Enter and Leave, and double clicks of them.
typedef enum CxUiPointerKind {
    CXUI_PTR_Press,     // button went down
    CXUI_PTR_Release,   // button went up
    CXUI_PTR_Move,
    CXUI_PTR_Left,      // the pointer left the canvas; position unused
} CxUiPointerKind;

// px is relative to the canvas, in physical pixels; buttons are those held after the change.
void _cxuiCanvasPointer(_In_ CxUiPeer* p, CxUiPointerKind kind, uint32 button, uint32 buttons, uint32 mods,
                        CxUiPoint px);
// The platform took the mouse away while buttons were held; the backend's capture is already
// gone, so the core doesn't release it.
void _cxuiCanvasCaptureLost(_In_ CxUiPeer* p);
// Ends the capture of a canvas in win as if the platform took the mouse away; false if no canvas
// there has it.
bool _cxuiCaptureCancel(_In_ CxUiWindow* win);
// The window's frame lets the user resize it.
bool _cxuiWindowUserResizable(_In_ CxUiWindow* w);
// dx, dy in notches, positive right and down. CXUI_Handled means the platform's own handling
// should be skipped.
CxUiResult _cxuiCanvasWheel(_In_ CxUiPeer* p, float64 dx, float64 dy, uint32 mods, CxUiPoint px);
// A key for the focused canvas, or for a window with nothing focused.
CxUiResult _cxuiKey(_In_ CxUiPeer* p, CxUiKey key, uint32 mods, uint32 scancode, bool down);
// A canvas gained or lost keyboard focus.
void _cxuiFocusChanged(_In_ CxUiPeer* p, bool gained);
// Forgets input state for a widget that is being unrealized.
void _cxuiInputForget(_In_ CxUiWidget* w);
// Moves focus to the next widget that can take it after from (a widget or a window), or the one
// before; false if there was nowhere else to go.
bool _cxuiFocusStep(_In_ CxUiWidget* from, bool backward);
// Focuses a widget for a key, such as Tab, so the window shows focus from then on.
void _cxuiFocusByKey(_In_ CxUiWidget* w);

// ---- text fields (cxuitext.c) ----

// The user changed a text field's text; backends call it inside _cxuiNativeEnter/Leave.
void _cxuiTextChanged(_In_ CxUiPeer* p, _In_ strref text);
// The widget starts a new line on Enter, which then goes no further: an editable text area
bool _cxuiTakesEnter(_In_ CxUiWidget* w);

// The user clicked a button, check box or radio button, or pressed it from the keyboard natively;
// backends call it inside _cxuiNativeEnter/Leave.
void _cxuiButtonClicked(_In_ CxUiPeer* p);

// The user selected an item of a list or dropdown (-1 for none); backends call it inside
// _cxuiNativeEnter/Leave, and may for selections from code too, which change nothing.
void _cxuiListChosen(_In_ CxUiPeer* p, int32 index);
// The user changed which items of a list where any number can be selected are; backends call it
// inside _cxuiNativeEnter/Leave, and the core reads the selection back through listGetMarks.
void _cxuiListMarksChanged(_In_ CxUiPeer* p);
// The user double-clicked a list's item; backends call it inside _cxuiNativeEnter/Leave.
void _cxuiListActivated(_In_ CxUiPeer* p, int32 index);

// The user moved a slider to a position; done once they let go of it, or for a move with a key.
// Backends call it inside _cxuiNativeEnter/Leave, and may for moves from code too, which change
// nothing.
void _cxuiSliderMoved(_In_ CxUiPeer* p, int32 pos, bool done);

// ---- mnemonics (cxuikeys.c, cxuiinput.c) ----

// Text as shown where there are no mnemonics: each `&` dropped, `&&` shown as one
void _cxuiMnemonicStrip(_Inout_ string* out, _In_opt_ strref text);
// The key after a single `&`, if it is a letter or digit; CXUI_KEY_None otherwise
CxUiKey _cxuiMnemonicKey(_In_opt_ strref text);
// Runs the mnemonic Alt+key in win: presses its button, or focuses what follows its label. False
// if nothing in the window has it.
bool _cxuiMnemonicRun(_In_ CxUiWindow* win, CxUiKey key);
// Presses a button as the default Enter or Esc does, or changes a check box or radio button as a
// click does: not if it is disabled, hidden or gone
bool _cxuiPress(_In_opt_ CxUiWidget* w);
// The window's own handling of a key nothing took (cxuiwindow.c)
bool _cxuiWindowKey(_In_ CxUiWindow* w, CxUiKey key, uint32 mods, bool repeat);

// ---- events (cxuievent.c) ----

// Destroying a slot destroys its closure, so one that may still be running is moved out with
// saExtract instead.
struct CxUiHandlerSlot {
    uint32 kind;
    closure fn;
};
stDeclare(CxUiHandlerSlot);
#define SType_CxUiHandlerSlot                         CxUiHandlerSlot*
#define STStorageType_CxUiHandlerSlot                 CxUiHandlerSlot
#define STypeArg_CxUiHandlerSlot(type, val)           stgeneric(opaque, &(val))
#define STypeArgPtr_CxUiHandlerSlot(type, val)        &stgeneric(opaque, (val))
#define STypeCheckedArg_CxUiHandlerSlot(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_CxUiHandlerSlot(type, val) stType(type), stArgPtr(type, val)

// Delivers ev to target's handler for ev->kind and, for kinds that bubble, to each ancestor in
// turn until one handles it. Fills in the header. UI thread only.
CxUiResult _cxuiDispatch(_In_ CxUiWidget* target, _Inout_ CxUiEvent* ev);

// Destroys every handler on w; one that is running is destroyed when it returns.
void _cxuiClearHandlers(_Inout_ CxUiWidget* w);

CX_C_END
