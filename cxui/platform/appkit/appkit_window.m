#include "appkit_ui.h"

// Positions are the top-left of the frame with y growing downwards from the top of the primary
// screen, as on the other platforms; AppKit's own origin is the bottom-left.

typedef struct AppKitWindow {
    CxUiPeer base;
    void* win;        // CxUiNSWindow, retained (__bridge_retained)
    void* delegate;   // CxUiWindowDelegate, retained; the window holds it only weakly
    CxUiSize lastPx;
    CxUiPoint lastPos;
    uint32 lastState;
    float32 lastScale;
    // The frame to go back to from zoomed or full screen, which AppKit doesn't say; saved as the
    // window leaves its normal state, and whenever the normal state is read or placed
    NSRect normal;
    bool hasNormal;
    CxUiPeer* menu;   // the menu bar's peer, or NULL
    // The window it belongs to, and the one a popup is over while it runs, which stands in for it;
    // it is a child window of that one while both are shown.
    struct AppKitWindow* owner;
    struct AppKitWindow* modalOwner;
    // Fullscreen: the CxUiFullscreen mode wanted and its display (0 for the window's own); AppKit
    // entering or leaving its own fullscreen, and whether cxui asked it to
    uint32 fsWant;
    CGDirectDisplayID fsDisplay;
    bool fsMoving;
    bool fsDriving;
    // A Monitor window's frame and style to go back to
    bool monitorMode;
    NSWindowStyleMask monStyle;
    NSRect monFrame;
} AppKitWindow;

@interface CxUiNSWindow : NSWindow
@end

@implementation CxUiNSWindow
- (BOOL)canBecomeKeyWindow
{
    return YES;
}
// Keys come here when nothing in the window has taken them. A canvas, text field or button has
// already bubbled its own up to the window through cxui.
- (void)keyDown:(NSEvent*)ev
{
    if (![self keyForWindow:ev])
        [super keyDown:ev];
}
- (void)keyUp:(NSEvent*)ev
{
    if (![self keyForWindow:ev])
        [super keyUp:ev];
}
- (BOOL)keyForWindow:(NSEvent*)ev
{
    CxUiPeer* p = _cxuiAppKitWindowPeerOf(self);
    id fr = self.firstResponder;
    return p && !_cxuiAppKitIsCanvasView(fr) && !_cxuiAppKitTextPeerFor(fr) && !_cxuiAppKitControlPeerFor(fr) &&
           _cxuiAppKitKey(p, ev);
}
- (BOOL)canBecomeMainWindow
{
    return YES;
}
@end

// The content view, top-left origin like the other platforms, in the window's background color
// once the core has given one
@interface CxUiRootView : NSView {
  @public
    NSColor* bg;
}
@end

@implementation CxUiRootView
- (BOOL)isFlipped
{
    return YES;
}
- (BOOL)isOpaque
{
    return bg != nil;
}
- (void)drawRect:(NSRect)dirty
{
    if (bg) {
        [bg setFill];
        NSRectFill(dirty);
    }
}
@end

static NSWindow* nsWindow(CxUiPeer* p)
{
    return (__bridge NSWindow*)((AppKitWindow*)p)->win;
}

static CGFloat primaryHeight(void)
{
    NSScreen* s = NSScreen.screens.firstObject;
    return s ? s.frame.size.height : 0;
}

static CxUiPoint topLeftOf(NSWindow* w)
{
    NSRect f = w.frame;
    return (CxUiPoint){ (int32)lround(f.origin.x), (int32)lround(primaryHeight() - (f.origin.y + f.size.height)) };
}

// AppKit's own fullscreen, which the green title bar button enters
static bool fullScreen(NSWindow* w)
{
    return (w.styleMask & NSWindowStyleMaskFullScreen) != 0;
}

// cxui's fullscreen without a frame, covering a monitor
static bool monitorMode(NSWindow* w)
{
    AppKitWindow* p = (AppKitWindow*)_cxuiAppKitWindowPeerOf(w);
    return p && p->monitorMode;
}

// The rectangle in cxui's screen units: points, from the top left of the primary screen
static CxUiRect topLeftRect(NSRect f)
{
    return (CxUiRect){ (int32)lround(f.origin.x), (int32)lround(primaryHeight() - (f.origin.y + f.size.height)),
                       (int32)lround(f.size.width), (int32)lround(f.size.height) };
}

// The work area of the screen a window is on, or of the one under the mouse
static CxUiRect workAreaFor(NSWindow* w)
{
    NSScreen* s = w ? w.screen : nil;
    for (NSScreen* each in NSScreen.screens) {
        if (!s && NSMouseInRect(NSEvent.mouseLocation, each.frame, NO))
            s = each;
    }
    if (!s)
        s = NSScreen.mainScreen;
    return topLeftRect(s.visibleFrame);
}

static AppKitWindow* ownerNow(AppKitWindow* p)
{
    return p->modalOwner ? p->modalOwner : p->owner;
}

// A child window moves with its parent and stays above it. AppKit orders a parent's children in
// with it, so only a shown window is a child, and only of a shown one.
static void ownerAttach(AppKitWindow* p, bool shown)
{
    NSWindow* win   = nsWindow(&p->base);
    AppKitWindow* o = ownerNow(p);
    NSWindow* want  = shown && o && nsWindow(&o->base).visible ? nsWindow(&o->base) : nil;
    if (win.parentWindow == want)
        return;
    if (win.parentWindow)
        [win.parentWindow removeChildWindow:win];
    if (want)
        [want addChildWindow:win ordered:NSWindowAbove];
}

static void saveNormal(AppKitWindow* p)
{
    p->normal    = nsWindow(&p->base).frame;
    p->hasNormal = true;
}

static uint32 stateOf(NSWindow* w)
{
    return w.miniaturized                       ? CXUI_WINDOW_Minimized
           : fullScreen(w) || monitorMode(w)    ? CXUI_WINDOW_Fullscreen
           : w.zoomed                           ? CXUI_WINDOW_Maximized
                                                : CXUI_WINDOW_Normal;
}

// Zoomed or fullscreen, where the frame isn't the normal one
static bool heldFrame(NSWindow* w)
{
    return w.zoomed || fullScreen(w) || monitorMode(w);
}

// Back to the normal frame; a full screen window goes back on its own once out of full screen.
static void restoreNormal(AppKitWindow* p)
{
    NSWindow* win = nsWindow(&p->base);
    if (win.miniaturized)
        [win deminiaturize:nil];
    if (fullScreen(win))
        [win toggleFullScreen:nil];
    else if (win.zoomed && p->hasNormal)
        [win setFrame:p->normal display:YES];
    else if (win.zoomed)
        [win zoom:nil];
}

// Reports a change of state and of size together, touching nothing after the core calls, which
// may free p.
static void reportChanges(AppKitWindow* p);

// A scale change comes with the window's new pixel size, reported together.
static bool reportScale(AppKitWindow* p)
{
    NSWindow* w   = nsWindow(&p->base);
    float32 scale = (float32)w.backingScaleFactor;
    if (scale == p->lastScale)
        return false;
    p->lastScale  = scale;
    NSSize pts    = w.contentView.frame.size;
    p->lastPx     = (CxUiSize){ (int32)lround(pts.width * scale), (int32)lround(pts.height * scale) };
    _cxuiNativeEnter();
    _cxuiWindowDpiChanged(&p->base, scale, p->lastPx);
    _cxuiNativeLeave();
    return true;
}

static void reportChanges(AppKitWindow* p)
{
    NSWindow* w   = nsWindow(&p->base);
    uint32 state  = stateOf(w);
    NSSize pts    = w.contentView.frame.size;
    CGFloat scale = w.backingScaleFactor;
    CxUiSize dips = { (int32)lround(pts.width), (int32)lround(pts.height) };
    CxUiSize px   = { (int32)lround(pts.width * scale), (int32)lround(pts.height * scale) };

    bool stateChanged = state != p->lastState;
    bool sizeChanged  = !w.miniaturized && (px.w != p->lastPx.w || px.h != p->lastPx.h);
    p->lastState      = state;
    if (sizeChanged)
        p->lastPx = px;
    if (!stateChanged && !sizeChanged)
        return;

    _cxuiNativeEnter();
    if (stateChanged)
        _cxuiWindowStateChanged(&p->base, state);
    if (sizeChanged)
        _cxuiWindowResized(&p->base, px, dips);
    _cxuiNativeLeave();
}

// ---- fullscreen ----

static CGDirectDisplayID displayOf(NSScreen* s)
{
    return [s.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
}

static NSScreen* screenWithDisplay(CGDirectDisplayID d)
{
    for (NSScreen* s in NSScreen.screens) {
        if (displayOf(s) == d)
            return s;
    }
    return nil;
}

// The screen a monitor from cxuiMonitors() describes
static NSScreen* screenFor(const CxUiMonitor* m)
{
    for (NSScreen* s in NSScreen.screens) {
        CxUiRect r = topLeftRect(s.frame);
        if (r.x == m->bounds.x && r.y == m->bounds.y && r.w == m->bounds.w && r.h == m->bounds.h)
            return s;
    }
    return nil;
}

static uint32 fsNow(AppKitWindow* p)
{
    NSWindow* win = nsWindow(&p->base);
    return p->monitorMode ? CXUI_FULLSCREEN_Monitor : fullScreen(win) ? CXUI_FULLSCREEN_System : CXUI_FULLSCREEN_None;
}

// The Dock and menu bar are hidden while a Monitor window is the key window. AppKit applies the
// options only while the program is active.
static bool hidBars;

static void presentation(AppKitWindow* p)
{
    bool hide = p->monitorMode && nsWindow(&p->base).isKeyWindow;
    if (hide == hidBars)
        return;
    hidBars                  = hide;
    NSApp.presentationOptions = hide ? NSApplicationPresentationHideDock | NSApplicationPresentationHideMenuBar
                                     : NSApplicationPresentationDefault;
}

static NSScreen* fsScreen(AppKitWindow* p)
{
    NSScreen* s = p->fsDisplay ? screenWithDisplay(p->fsDisplay) : nil;
    if (!s)
        s = nsWindow(&p->base).screen;
    return s ? s : NSScreen.mainScreen;
}

static void monitorEnter(AppKitWindow* p)
{
    NSWindow* win = nsWindow(&p->base);
    if (!heldFrame(win))
        saveNormal(p);
    p->monStyle    = win.styleMask;
    p->monFrame    = win.frame;
    p->monitorMode = true;
    win.styleMask  = NSWindowStyleMaskBorderless | NSWindowStyleMaskMiniaturizable;
    [win setFrame:fsScreen(p).frame display:YES];
    presentation(p);
}

static void monitorLeave(AppKitWindow* p)
{
    if (!p->monitorMode)
        return;
    NSWindow* win  = nsWindow(&p->base);
    p->monitorMode = false;
    win.styleMask  = p->monStyle;
    [win setFrame:p->monFrame display:YES];
    presentation(p);
}

// Moves toward the fullscreen mode wanted. AppKit's own fullscreen comes and goes in an animation,
// after which this runs again; the caller reports the change.
static void fsApply(AppKitWindow* p)
{
    NSWindow* win = nsWindow(&p->base);
    if (p->fsMoving)
        return;
    uint32 now      = fsNow(p);
    NSScreen* want  = p->fsDisplay ? screenWithDisplay(p->fsDisplay) : nil;
    bool onScreen   = !want || win.screen == want;
    if (now == p->fsWant && (now == CXUI_FULLSCREEN_None || onScreen))
        return;
    if (now == CXUI_FULLSCREEN_System) {
        p->fsMoving = p->fsDriving = true;
        [win toggleFullScreen:nil];
        return;
    }
    monitorLeave(p);
    if (p->fsWant == CXUI_FULLSCREEN_Monitor) {
        monitorEnter(p);
    } else if (p->fsWant == CXUI_FULLSCREEN_System) {
        // AppKit makes a window fullscreen on the screen it is on.
        if (want && win.screen != want) {
            NSRect area = want.visibleFrame, f = win.frame;
            f.origin    = NSMakePoint(NSMidX(area) - f.size.width / 2, NSMidY(area) - f.size.height / 2);
            [win setFrame:f display:YES];
        }
        win.collectionBehavior |= NSWindowCollectionBehaviorFullScreenPrimary;
        p->fsMoving = p->fsDriving = true;
        [win toggleFullScreen:nil];
    }
}

// AppKit started entering or leaving its fullscreen; when the user did that, it is what is wanted.
static void fsWillMove(AppKitWindow* p, bool entering)
{
    if (!p->fsDriving)
        p->fsWant = entering ? CXUI_FULLSCREEN_System : CXUI_FULLSCREEN_None;
    p->fsMoving = true;
}

// AppKit finished, or failed; what is still wanted goes on, and the state is reported even if it
// didn't change, since cxui expected it to.
static void fsMoved(AppKitWindow* p, bool failed)
{
    p->fsMoving  = false;
    p->fsDriving = false;
    if (failed) {
        p->fsWant    = fsNow(p);
        p->lastState = (uint32)-1;
    }
    fsApply(p);
    reportChanges(p);
}

// A floating window stays above other programs' ordinary windows.
bool _cxuiAppKitWindowSetTopmost(CxUiPeer* p, bool on)
{
    nsWindow(p).level = on ? NSFloatingWindowLevel : NSNormalWindowLevel;
    return true;
}

bool _cxuiAppKitTestTopmost(CxUiPeer* p, bool* topmost)
{
    *topmost = nsWindow(p).level > NSNormalWindowLevel;
    return true;
}

int32 _cxuiAppKitWindowMonitorOf(CxUiPeer* p)
{
    return _cxuiAppKitWindowMonitor(nsWindow(p));
}

bool _cxuiAppKitWindowSetFullscreen(CxUiPeer* peer, uint32 mode, const CxUiMonitor* mon)
{
    AppKitWindow* p  = (AppKitWindow*)peer;
    NSScreen* screen = mon ? screenFor(mon) : nil;
    if (mon && !screen)
        return false;
    p->fsWant    = mode;
    p->fsDisplay = screen ? displayOf(screen) : 0;
    fsApply(p);
    reportChanges(p);
    return true;
}

// A Monitor window covers its screen whatever happens to the screens.
void _cxuiAppKitMonitorsReframe(void)
{
    for (NSWindow* each in NSApp.windows) {
        AppKitWindow* p = (AppKitWindow*)_cxuiAppKitWindowPeerOf(each);
        if (p && p->monitorMode)
            [each setFrame:fsScreen(p).frame display:YES];
    }
}

@interface CxUiWindowDelegate : NSObject <NSWindowDelegate> {
  @public
    AppKitWindow* peer;   // NULL once the peer is destroyed
}
@end

@implementation CxUiWindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)sender
{
    if (peer) {
        _cxuiNativeEnter();
        _cxuiWindowCloseRequested(&peer->base, CXUI_CLOSE_User);
        _cxuiNativeLeave();
    }
    return NO;   // cxui decides what closing does
}

- (void)windowDidResize:(NSNotification*)n
{
    if (peer)
        reportChanges(peer);
}

- (void)windowDidChangeBackingProperties:(NSNotification*)n
{
    if (peer && !reportScale(peer))
        reportChanges(peer);
}

- (void)windowDidMove:(NSNotification*)n
{
    if (!peer || nsWindow(&peer->base).miniaturized)
        return;
    CxUiPoint pos = topLeftOf(nsWindow(&peer->base));
    if (pos.x == peer->lastPos.x && pos.y == peer->lastPos.y)
        return;
    peer->lastPos = pos;
    _cxuiNativeEnter();
    _cxuiWindowMoved(&peer->base, pos);
    _cxuiNativeLeave();
}

- (BOOL)windowShouldZoom:(NSWindow*)w toFrame:(NSRect)frame
{
    if (peer && !heldFrame(w))
        saveNormal(peer);
    return YES;
}

- (void)windowWillEnterFullScreen:(NSNotification*)n
{
    if (!peer)
        return;
    if (!nsWindow(&peer->base).zoomed)
        saveNormal(peer);
    fsWillMove(peer, true);
}

- (void)windowWillExitFullScreen:(NSNotification*)n
{
    if (peer)
        fsWillMove(peer, false);
}

- (void)windowDidFailToEnterFullScreen:(NSWindow*)w
{
    if (peer)
        fsMoved(peer, true);
}

- (void)windowDidFailToExitFullScreen:(NSWindow*)w
{
    if (peer)
        fsMoved(peer, true);
}

- (void)windowDidChangeScreen:(NSNotification*)n
{
    if (peer && peer->monitorMode)
        _cxuiAppKitMonitorsReframe();
}

- (void)windowDidMiniaturize:(NSNotification*)n
{
    if (peer)
        reportChanges(peer);
}

- (void)windowDidDeminiaturize:(NSNotification*)n
{
    if (peer)
        reportChanges(peer);
}

- (void)windowDidEnterFullScreen:(NSNotification*)n
{
    if (peer)
        fsMoved(peer, false);
}

- (void)windowDidExitFullScreen:(NSNotification*)n
{
    if (peer)
        fsMoved(peer, false);
}

- (void)windowDidChangeOcclusionState:(NSNotification*)n
{
    if (!peer)
        return;
    bool hidden = !(nsWindow(&peer->base).occlusionState & NSWindowOcclusionStateVisible);
    _cxuiNativeEnter();
    _cxuiWindowOccluded(&peer->base, hidden);
    _cxuiNativeLeave();
}

- (void)windowDidBecomeKey:(NSNotification*)n
{
    _cxuiAppKitMenuRefresh();
    if (peer) {
        presentation(peer);
        _cxuiNativeEnter();
        _cxuiWindowActivated(&peer->base, true);
        _cxuiNativeLeave();
    }
}

- (void)windowDidResignKey:(NSNotification*)n
{
    if (peer) {
        presentation(peer);
        _cxuiNativeEnter();
        _cxuiWindowActivated(&peer->base, false);
        _cxuiNativeLeave();
    }
}
@end

// Popups, innermost last (main thread only)
static NSMutableArray<NSWindow*>* modalWindows;

NSWindow* _cxuiAppKitModalWindow(void)
{
    return modalWindows.lastObject;
}

// ---- theme ----

static bool appearanceDark(NSAppearance* a)
{
    return [[a bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]]
        isEqualToString:NSAppearanceNameDarkAqua];
}

static void applyAppearance(NSWindow* win, CxUiWindow* w, bool dark)
{
    // nil follows the system, including when it changes.
    if (!w || w->_theme == CXUI_Theme_System)
        win.appearance = nil;
    else
        win.appearance = [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
}

CxUiThemeMode _cxuiAppKitSystemTheme(void)
{
    return appearanceDark(NSApp.effectiveAppearance) ? CXUI_Theme_Dark : CXUI_Theme_Light;
}

void _cxuiAppKitWindowSetDark(CxUiPeer* p, bool dark)
{
    applyAppearance(nsWindow(p), p->w ? objDynCast(CxUiWindow, p->w) : NULL, dark);
}

bool _cxuiAppKitTestFrameDark(CxUiPeer* p, bool* dark)
{
    *dark = appearanceDark(nsWindow(p).effectiveAppearance);
    return true;
}

// ---- operations ----

static NSWindowStyleMask styleFor(CxUiWindow* w)
{
    if (w->_flags & CXUI_WINDOW_Borderless)
        return NSWindowStyleMaskBorderless | NSWindowStyleMaskMiniaturizable;
    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
    return _cxuiWindowUserResizable(w) ? style | NSWindowStyleMaskResizable : style;
}

CxUiPeer* _cxuiAppKitWindowCreate(CxUiWindow* w)
{
    // AppKit has no default size of its own.
    if (w->_size.w == 0)
        w->_size.w = 640;
    if (w->_size.h == 0)
        w->_size.h = 480;
    NSRect rect = NSMakeRect(0, 0, w->_size.w, w->_size.h);

    CxUiNSWindow* win = [[CxUiNSWindow alloc] initWithContentRect:rect
                                                        styleMask:styleFor(w)
                                                          backing:NSBackingStoreBuffered
                                                            defer:NO];
    win.releasedWhenClosed = NO;
    win.hasShadow          = YES;
    win.contentView        = [[CxUiRootView alloc] initWithFrame:rect];
    win.title              = _cxuiAppKitString(w->_title);
    // Before the window can first appear
    applyAppearance(win, w, w->_dark);
    if (w->_hasPos)
        [win setFrameTopLeftPoint:NSMakePoint(w->_pos.x, primaryHeight() - w->_pos.y)];
    else
        [win center];

    AppKitWindow* p        = xaAllocStruct(AppKitWindow, XA_Zero);
    CxUiWindowDelegate* d  = [CxUiWindowDelegate new];
    d->peer                = p;
    win.delegate           = d;
    p->win                 = (__bridge_retained void*)win;
    p->delegate            = (__bridge_retained void*)d;

    CGFloat scale = win.backingScaleFactor;
    NSSize pts    = win.contentView.frame.size;
    p->lastPx     = (CxUiSize){ (int32)lround(pts.width * scale), (int32)lround(pts.height * scale) };
    p->lastPos    = topLeftOf(win);
    p->lastScale  = (float32)scale;
    p->base.kind  = CXUI_PEER_Window;
    w->_scale     = (float32)scale;
    w->_clientPx  = p->lastPx;
    return &p->base;
}

void _cxuiAppKitWindowShow(CxUiPeer* p, uint32 mode)
{
    _cxuiAppKitBecomeRegular();
    NSWindow* win = nsWindow(p);
    switch (mode) {
    case CXUI_SHOW_NoActivate:
        if (win.miniaturized)
            [win deminiaturize:nil];
        [win orderFront:nil];
        break;
    case CXUI_SHOW_Minimized:
        [win orderFront:nil];
        [win miniaturize:nil];
        break;
    case CXUI_SHOW_Maximized:
        if (win.miniaturized)
            [win deminiaturize:nil];
        [win makeKeyAndOrderFront:nil];
        if (!win.zoomed)
            [win zoom:nil];
        [NSApp activate];
        break;
    default:
        restoreNormal((AppKitWindow*)p);
        [win makeKeyAndOrderFront:nil];
        [NSApp activate];
        break;
    }
    ownerAttach((AppKitWindow*)p, true);
    _cxuiAppKitMenuRefresh();
    // The windows it owns come in with it.
    for (NSWindow* each in NSApp.windows) {
        AppKitWindow* o = (AppKitWindow*)_cxuiAppKitWindowPeerOf(each);
        if (o && ownerNow(o) == (AppKitWindow*)p && each.visible)
            ownerAttach(o, true);
    }
}

// AppKit would take the windows it owns away with it.
void _cxuiAppKitWindowHide(CxUiPeer* p)
{
    NSWindow* win = nsWindow(p);
    ownerAttach((AppKitWindow*)p, false);
    for (NSWindow* each in win.childWindows)
        [win removeChildWindow:each];
    [win orderOut:nil];
}

void _cxuiAppKitWindowSetTitle(CxUiPeer* p, strref title)
{
    nsWindow(p).title = _cxuiAppKitString(title);
}

void _cxuiAppKitWindowSetSize(CxUiPeer* p, CxUiSize dips)
{
    [nsWindow(p) setContentSize:NSMakeSize(dips.w, dips.h)];
}

void _cxuiAppKitWindowSetMinSize(CxUiPeer* p, CxUiSize dips)
{
    nsWindow(p).contentMinSize = NSMakeSize(dips.w, dips.h);
}

// The frame's width doesn't change with it, so the content keeps its size.
void _cxuiAppKitWindowSetResizable(CxUiPeer* p, bool resizable)
{
    NSWindow* win = nsWindow(p);
    win.styleMask = (win.styleMask & ~NSWindowStyleMaskResizable) | (resizable ? NSWindowStyleMaskResizable : 0);
    [win standardWindowButton:NSWindowZoomButton].enabled = resizable;
}

bool _cxuiAppKitWindowBeginDrag(CxUiPeer* p)
{
    NSEvent* ev = NSApp.currentEvent;
    if (ev.type != NSEventTypeLeftMouseDown && ev.type != NSEventTypeRightMouseDown && ev.type != NSEventTypeOtherMouseDown)
        return false;
    [nsWindow(p) performWindowDragWithEvent:ev];
    return true;
}

bool _cxuiAppKitTestBorderless(CxUiPeer* p, bool* borderless)
{
    *borderless = !(nsWindow(p).styleMask & NSWindowStyleMaskTitled);
    return true;
}

void _cxuiAppKitWindowSetBackground(CxUiPeer* p, CxUiColor c)
{
    CxUiRootView* root = (CxUiRootView*)nsWindow(p).contentView;
    root->bg           = [NSColor colorWithSRGBRed:c.r / 255.0 green:c.g / 255.0 blue:c.b / 255.0 alpha:1.0];
    [root setNeedsDisplay:YES];
}

bool _cxuiAppKitTestWindowActive(CxUiPeer* p, bool* active)
{
    *active = nsWindow(p).isKeyWindow;
    return true;
}

bool _cxuiAppKitWindowGetPosition(CxUiPeer* peer, CxUiPoint* out)
{
    AppKitWindow* p = (AppKitWindow*)peer;
    NSWindow* win   = nsWindow(peer);
    if (!win.miniaturized)
        p->lastPos = topLeftOf(win);
    *out = p->lastPos;
    return true;
}

bool _cxuiAppKitWindowSetPosition(CxUiPeer* p, CxUiPoint pos)
{
    [nsWindow(p) setFrameTopLeftPoint:NSMakePoint(pos.x, primaryHeight() - pos.y)];
    return true;
}

void _cxuiAppKitWindowGetPlacement(CxUiPeer* peer, CxUiPlacement* out)
{
    AppKitWindow* p = (AppKitWindow*)peer;
    NSWindow* win   = nsWindow(peer);
    if (!heldFrame(win) || !p->hasNormal)
        saveNormal(p);
    *out = (CxUiPlacement){ .normal = topLeftRect(p->normal),
                            .state  = stateOf(win),
                            .valid  = CXUI_PLACE_Position | CXUI_PLACE_Size | CXUI_PLACE_State };
}

// A zoomed or full screen window keeps its frame, and takes the rectangle when restored.
void _cxuiAppKitWindowSetPlacement(CxUiPeer* peer, const CxUiPlacement* pl, bool withState)
{
    AppKitWindow* p = (AppKitWindow*)peer;
    NSWindow* win   = nsWindow(peer);
    if (pl->valid & (CXUI_PLACE_Position | CXUI_PLACE_Size)) {
        CxUiRect r   = pl->normal;
        p->normal    = NSMakeRect(r.x, primaryHeight() - (r.y + r.h), r.w, r.h);
        p->hasNormal = true;
        if (!heldFrame(win))
            [win setFrame:p->normal display:YES];
    }
    if (!withState)
        return;
    switch (pl->state) {
    case CXUI_WINDOW_Maximized:
        if (win.miniaturized)
            [win deminiaturize:nil];
        if (!heldFrame(win))
            [win zoom:nil];
        break;
    case CXUI_WINDOW_Minimized:
        if (!win.miniaturized)
            [win miniaturize:nil];
        break;
    default:
        restoreNormal(p);
        break;
    }
}

void _cxuiAppKitWindowBeginModal(CxUiPeer* p, CxUiPeer* owner)
{
    NSWindow* win = nsWindow(p);
    if (!modalWindows)
        modalWindows = [NSMutableArray new];
    [modalWindows addObject:win];
    ((AppKitWindow*)p)->modalOwner = (AppKitWindow*)owner;
    if (!p->w || !objDynCast(CxUiWindow, p->w)->_hasPos)
        _cxuiAppKitWindowMoveNear(p, owner, CXUI_NEAR_Center);
}

void _cxuiAppKitWindowEndModal(CxUiPeer* p)
{
    [modalWindows removeObjectIdenticalTo:nsWindow(p)];
    ((AppKitWindow*)p)->modalOwner = NULL;
    ownerAttach((AppKitWindow*)p, nsWindow(p).visible);
}

void _cxuiAppKitWindowSetOwner(CxUiPeer* p, CxUiPeer* owner)
{
    ((AppKitWindow*)p)->owner = (AppKitWindow*)owner;
    ownerAttach((AppKitWindow*)p, nsWindow(p).visible);
    _cxuiAppKitMenuRefresh();
}

// By the other window's frame, or centered on the work area of its screen, or of the one under the
// mouse
bool _cxuiAppKitWindowMoveNear(CxUiPeer* p, CxUiPeer* other, uint32 where)
{
    NSWindow* win = nsWindow(p);
    NSWindow* by  = other ? nsWindow(other) : nil;
    CxUiRect area;
    if (by && !by.miniaturized) {
        area = topLeftRect(by.frame);
    } else {
        area  = workAreaFor(by);
        where = CXUI_NEAR_Center;
    }
    CxUiPoint at = _cxuiNearPoint(topLeftRect(win.frame), area, where);
    [win setFrameTopLeftPoint:NSMakePoint(at.x, primaryHeight() - at.y)];
    return true;
}

// A shown window's parent, or for a hidden one the window it will be a child of
bool _cxuiAppKitTestOwnedBy(CxUiPeer* p, CxUiPeer* owner, bool* owned)
{
    NSWindow* win = nsWindow(p);
    if (win.visible)
        *owned = win.parentWindow == (owner ? nsWindow(owner) : nil);
    else
        *owned = ownerNow((AppKitWindow*)p) == (AppKitWindow*)owner;
    return true;
}

// ---- menu bars ----

void _cxuiAppKitWindowSetMenu(CxUiPeer* p, CxUiPeer* menu)
{
    ((AppKitWindow*)p)->menu = menu;
    _cxuiAppKitMenuRefresh();
}

// The key window's bar, or that of the nearest window it belongs to; a window that isn't cxui's,
// such as the color panel, keeps the bar there is. Before the program is active there is no key
// window, and the frontmost of cxui's stands in.
void _cxuiAppKitMenuRefresh(void)
{
    NSWindow* key = NSApp.keyWindow;
    for (NSWindow* each in key ? @[] : NSApp.orderedWindows) {
        if (each.visible && _cxuiAppKitWindowPeerOf(each)) {
            key = each;
            break;
        }
    }
    if (!key)
        return;
    AppKitWindow* p = (AppKitWindow*)_cxuiAppKitWindowPeerOf(key);
    if (!p)
        return;
    for (int32 depth = 0; p && !p->menu && depth < 32; depth++)
        p = ownerNow(p);
    _cxuiAppKitMenuShowBar(p ? p->menu : NULL);
}

CxUiPeer* _cxuiAppKitTestWindowMenu(CxUiPeer* win)
{
    CxUiPeer* bar = _cxuiAppKitMenuShownBar();
    return bar && bar->w && CxUiWidget(_cxuiWindowOf(bar->w)) == win->w ? bar : NULL;
}

float32 _cxuiAppKitSystemScale(void)
{
    NSScreen* s = NSScreen.mainScreen;
    return s ? (float32)s.backingScaleFactor : 1.0f;
}

NSView* _cxuiAppKitPeerContent(CxUiPeer* p)
{
    if (p && p->kind == CXUI_PEER_Panel)
        return _cxuiAppKitPanelView(p);
    if (p && p->kind == CXUI_PEER_Tabs)
        return _cxuiAppKitTabsPage(p);
    return p && p->kind == CXUI_PEER_Window ? nsWindow(p).contentView : nil;
}

CxUiPeer* _cxuiAppKitWindowPeerOf(NSWindow* w)
{
    id d = w.delegate;
    if (![d isKindOfClass:CxUiWindowDelegate.class] || !((CxUiWindowDelegate*)d)->peer)
        return NULL;
    return &((CxUiWindowDelegate*)d)->peer->base;
}

void _cxuiAppKitPeerDestroy(CxUiPeer* peer)
{
    if (peer->kind == CXUI_PEER_Canvas) {
        _cxuiAppKitCanvasDestroy(peer);
        return;
    }
    if (peer->kind == CXUI_PEER_Panel) {
        _cxuiAppKitPanelDestroy(peer);
        return;
    }
    if (peer->kind == CXUI_PEER_Text) {
        _cxuiAppKitTextDestroy(peer);
        return;
    }
    if (_cxuiPeerIsControl(peer->kind)) {
        _cxuiAppKitControlDestroy(peer);
        return;
    }
    if (peer->kind == CXUI_PEER_Menu) {
        _cxuiAppKitMenuDestroy(peer);
        return;
    }
    AppKitWindow* p = (AppKitWindow*)peer;
    // The windows it owns stay open, owned by nothing.
    for (NSWindow* each in NSApp.windows) {
        AppKitWindow* o = (AppKitWindow*)_cxuiAppKitWindowPeerOf(each);
        if (!o || o == p)
            continue;
        if (o->owner == p)
            o->owner = NULL;
        if (o->modalOwner == p)
            o->modalOwner = NULL;
        if (each.parentWindow == nsWindow(peer))
            [nsWindow(peer) removeChildWindow:each];
    }
    p->owner      = NULL;
    p->modalOwner = NULL;
    ownerAttach(p, false);
    if (p->monitorMode) {
        p->monitorMode = false;
        presentation(p);
    }
    NSWindow* win         = CFBridgingRelease(p->win);
    CxUiWindowDelegate* d = CFBridgingRelease(p->delegate);
    d->peer               = NULL;
    win.delegate          = nil;
    [modalWindows removeObjectIdenticalTo:win];
    [win orderOut:nil];
    [win close];

    // AppKit may still be calling into either from further up the stack, so they go when the
    // current autorelease pool drains rather than now.
    __autoreleasing id keepWin      = win;
    __autoreleasing id keepDelegate = d;
    (void)keepWin;
    (void)keepDelegate;
    xaFree(p);
}
