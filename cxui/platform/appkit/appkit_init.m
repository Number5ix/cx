#include "appkit_ui.h"

#include <cx/utils/lazyinit.h>
#include <cxui/entry.h>
#include <pthread.h>

// AppKit runs on the process's main thread, which CXUI_DEFINE_ENTRY_POINT keeps in [NSApp run]
// while entryPoint() runs on another thread. A cxui session is cxui's own state on that thread;
// NSApp lasts for the process. The UI queue is ticked from a run loop source and a timer in the
// common modes, so it keeps running in modal panels, live resize and menu tracking.

#define CXUI_EVENT_SUBTYPE 0x4378   // cxui's own application-defined events

int32 _cxuiAppKitLoopDepth;

static atomic(bool) mainThreadFree;   // the entry runner holds the main thread in [NSApp run]
static int entryResult;
static bool sessionActive;            // main thread only
static bool appRegular;               // main thread only: a Dock icon and menu bar this session

// Created once and kept, so a wake from another thread after a session ends is harmless; added
// to the main run loop for each session.
static CFRunLoopSourceRef queueSource;
static CFRunLoopTimerRef queueTimer;
static LazyInitState queueSourceInit;

@interface CxUiAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation CxUiAppDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
    // The program's own thread decides when to exit.
    if (sessionActive) {
        _cxuiNativeEnter();
        cchainCall(&cxuiQuitRequestNotify, stvNone);
        _cxuiNativeLeave();
    }
    return NSTerminateCancel;
}
@end

static CxUiAppDelegate* appDelegate;

NSString* _cxuiAppKitString(strref s)
{
    NSString* ns = [[NSString alloc] initWithBytes:strC(s) length:strLen(s) encoding:NSUTF8StringEncoding];
    return ns ? ns : @"";
}

// An event for a nested loop that waits on events to wake up and check its flag.
static void postLoopEvent(void)
{
    NSEvent* e = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                    location:NSZeroPoint
                               modifierFlags:0
                                   timestamp:0
                                windowNumber:0
                                     context:nil
                                     subtype:CXUI_EVENT_SUBTYPE
                                       data1:0
                                       data2:0];
    [NSApp postEvent:e atStart:NO];
}

static void wakeMain(void)
{
    CFRunLoopSourceSignal(queueSource);
    CFRunLoopWakeUp(CFRunLoopGetMain());
}

static void queueRun(void)
{
    @autoreleasepool {
        if (!sessionActive)
            return;
        if (atomicLoad(bool, &_cxui.exitRequested, Acquire)) {
            if (_cxuiAppKitLoopDepth > 0) {
                // Unwind first: cxui's loops end on exitRequested, AppKit's modal ones are stopped.
                if (NSApp.modalWindow)
                    [NSApp stopModalWithCode:NSModalResponseAbort];
                postLoopEvent();
            } else {
                _cxuiHostedEnd();
            }
            return;
        }
        _cxuiTick();
        if (_cxuiAppKitLoopDepth > 0)
            postLoopEvent();
    }
}

static void sourcePerform(void* info)
{
    unused_noeval(info);
    queueRun();
}

// Only signals the source: CoreFoundation never fires a timer again while its callback is on the
// stack, and queued work run from here could open a modal loop that needs the timer.
static void timerFire(CFRunLoopTimerRef t, void* info)
{
    unused_noeval(t);
    unused_noeval(info);
    wakeMain();
}

static void queueSourceInitOnce(void* unused)
{
    unused_noeval(unused);
    CFRunLoopSourceContext ctx = { .perform = sourcePerform };
    queueSource                = CFRunLoopSourceCreate(NULL, 0, &ctx);
    // Repeating with a long interval, so it stays valid after firing; armTimer sets each deadline.
    queueTimer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 1.0e10, 1.0e10, 0, 0, timerFire, NULL);
}

void _cxuiAppKitLoopLeft(void)
{
    // An outer loop, or the session teardown, picks up the shutdown from here.
    if (atomicLoad(bool, &_cxui.exitRequested, Acquire))
        wakeMain();
}

// ---- backend ----

static bool appkitHostRun(void (*fn)(void* ctx), void* ctx)
{
    if (!atomicLoad(bool, &mainThreadFree, Acquire)) {
        logStr(Error, _SL("cxui needs the main thread on macOS: define the program's entry point with CXUI_DEFINE_ENTRY_POINT"));
        return false;
    }
    if (pthread_main_np()) {
        logStr(Error, _SL("cxuiStart can't be called on the main thread on macOS"));
        return false;
    }
    CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopCommonModes, ^{
        @autoreleasepool {
            fn(ctx);
        }
    });
    CFRunLoopWakeUp(CFRunLoopGetMain());
    return true;
}

// The system's light or dark setting, followed for the session
@interface CxUiThemeObserver : NSObject
@end

@implementation CxUiThemeObserver
- (void)observeValueForKeyPath:(NSString*)path ofObject:(id)obj change:(NSDictionary*)change context:(void*)ctx
{
    _cxuiNativeEnter();
    _cxuiSystemThemeChanged();
    _cxuiNativeLeave();
}
@end

static CxUiThemeObserver* themeObserver;

static bool appkitInit(flags_t flags)
{
    unused_noeval(flags);
    if (NSScreen.screens.count == 0) {
        logStr(Info, _SL("cxui couldn't find a display for AppKit"));
        return false;
    }

    lazyInit(&queueSourceInit, queueSourceInitOnce, NULL);
    CFRunLoopTimerSetNextFireDate(queueTimer, CFAbsoluteTimeGetCurrent() + 1.0e10);
    CFRunLoopAddSource(CFRunLoopGetMain(), queueSource, kCFRunLoopCommonModes);
    CFRunLoopAddTimer(CFRunLoopGetMain(), queueTimer, kCFRunLoopCommonModes);

    _cxuiAppKitLoopDepth = 0;
    sessionActive        = true;
    themeObserver        = [CxUiThemeObserver new];
    [NSApp addObserver:themeObserver forKeyPath:@"effectiveAppearance" options:0 context:NULL];
    _cxuiAppKitTextInit();
    return true;
}

void _cxuiAppKitBecomeRegular(void)
{
    // Not at the start of a session: switching the policy on and straight back off leaves stale
    // Dock tiles behind, and a session that shows nothing shouldn't have a Dock icon anyway.
    if (appRegular)
        return;
    appRegular = true;
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    _cxuiAppKitMenuInstall();
    [NSApp activate];
}

static void appkitShutdown(void)
{
    sessionActive = false;
    _cxuiAppKitMenuShowBar(NULL);
    _cxuiAppKitTextShutdown();
    [NSApp removeObserver:themeObserver forKeyPath:@"effectiveAppearance"];
    themeObserver = nil;
    CFRunLoopRemoveSource(CFRunLoopGetMain(), queueSource, kCFRunLoopCommonModes);
    CFRunLoopRemoveTimer(CFRunLoopGetMain(), queueTimer, kCFRunLoopCommonModes);
    if (appRegular) {
        appRegular = false;
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    }
}

static void appkitRunNested(atomic(bool)* exitFlag)
{
    // A popup runs as an AppKit modal session, so only its window takes input.
    NSWindow* modal        = _cxuiAppKitModalWindow();
    NSModalSession session = modal ? [NSApp beginModalSessionForWindow:modal] : NULL;
    _cxuiAppKitLoopDepth++;

    while (!_cxuiLoopDone(exitFlag)) {
        @autoreleasepool {
            if (session) {
                // Waits without taking the event, then lets the session handle it.
                [NSApp nextEventMatchingMask:NSEventMaskAny
                                   untilDate:NSDate.distantFuture
                                      inMode:NSModalPanelRunLoopMode
                                     dequeue:NO];
                if ([NSApp runModalSession:session] != NSModalResponseContinue)
                    break;
            } else {
                NSEvent* e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:NSDate.distantFuture
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
                if (e)
                    [NSApp sendEvent:e];
            }
        }
    }

    _cxuiAppKitLoopDepth--;
    if (session)
        [NSApp endModalSession:session];
    _cxuiAppKitLoopLeft();
}

static bool appkitFlush(void)
{
    // Bounded, so a source of events that never runs dry can't hold the caller here.
    for (int32 i = 0; i < 64; i++) {
        @autoreleasepool {
            NSEvent* e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:NSDate.distantPast
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
            if (!e)
                return false;
            [NSApp sendEvent:e];
        }
    }
    return [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:NSDate.distantPast inMode:NSDefaultRunLoopMode dequeue:NO] != nil;
}

static void appkitWake(void)
{
    lazyInit(&queueSourceInit, queueSourceInitOnce, NULL);
    wakeMain();
}

static void appkitArmTimer(int64 delay)
{
    if (delay == timeForever)
        CFRunLoopTimerSetNextFireDate(queueTimer, CFAbsoluteTimeGetCurrent() + 1.0e10);
    else if (delay <= 0)
        wakeMain();
    else
        CFRunLoopTimerSetNextFireDate(queueTimer, CFAbsoluteTimeGetCurrent() + (double)delay / 1.0e6);
}

// ---- the entry hand-off ----

static int entryThreadMain(Thread* self)
{
    int (*entry)(void) = (int (*)(void))stvlAtPtr(&self->args, 0);
    entryResult        = entry();

    // [NSApp run] notices a stop only when it handles the next event.
    CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopCommonModes, ^{
        [NSApp stop:nil];
        postLoopEvent();
    });
    CFRunLoopWakeUp(CFRunLoopGetMain());
    return 0;
}

int _cxuiEntryRunner(int (*entry)(void))
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        appDelegate     = [CxUiAppDelegate new];
        NSApp.delegate  = appDelegate;
        atomicStore(bool, &mainThreadFree, true, Release);

        Thread* thr = thrCreate(entryThreadMain, _S"main", stvar(ptr, (void*)entry));
        if (!thr)
            return 1;
        [NSApp run];
        atomicStore(bool, &mainThreadFree, false, Release);
        thrWait(thr, timeForever);
        thrRelease(&thr);
    }
    return entryResult;
}

const CxUiBackend _cxuiAppKitBackend = {
    .name      = _S"appkit",
    .init      = appkitInit,
    .shutdown  = appkitShutdown,
    .hostRun   = appkitHostRun,
    .runNested = appkitRunNested,
    .flush     = appkitFlush,
    .wake      = appkitWake,
    .armTimer  = appkitArmTimer,

    .peerDestroy    = _cxuiAppKitPeerDestroy,
    .peerSetVisible = _cxuiAppKitPeerSetVisible,
    .peerRaise      = _cxuiAppKitPeerRaise,
    .peerSetEnabled = _cxuiAppKitPeerSetEnabled,

    .windowCreate       = _cxuiAppKitWindowCreate,
    .windowSetMinSize   = _cxuiAppKitWindowSetMinSize,
    .windowSetResizable = _cxuiAppKitWindowSetResizable,
    .windowBeginDrag    = _cxuiAppKitWindowBeginDrag,
    .windowShow        = _cxuiAppKitWindowShow,
    .windowHide        = _cxuiAppKitWindowHide,
    .windowSetTitle    = _cxuiAppKitWindowSetTitle,
    .windowSetSize     = _cxuiAppKitWindowSetSize,
    .windowGetPosition = _cxuiAppKitWindowGetPosition,
    .windowSetPosition = _cxuiAppKitWindowSetPosition,
    .placementCaps      = CXUI_PLACE_Position | CXUI_PLACE_Size | CXUI_PLACE_State,
    .windowGetPlacement = _cxuiAppKitWindowGetPlacement,
    .windowSetPlacement = _cxuiAppKitWindowSetPlacement,
    .windowSetDark     = _cxuiAppKitWindowSetDark,
    .systemTheme       = _cxuiAppKitSystemTheme,
    .windowBeginModal  = _cxuiAppKitWindowBeginModal,
    .windowEndModal    = _cxuiAppKitWindowEndModal,
    .windowSetOwner    = _cxuiAppKitWindowSetOwner,
    .windowMoveNear    = _cxuiAppKitWindowMoveNear,

    .systemScale      = _cxuiAppKitSystemScale,
    .canvasCreate     = _cxuiAppKitCanvasCreate,
    .canvasSetBounds  = _cxuiAppKitCanvasSetBounds,
    .canvasInvalidate = _cxuiAppKitCanvasInvalidate,
    .canvasPaintNow   = _cxuiAppKitCanvasPaintNow,
    .canvasCapture    = _cxuiAppKitCanvasCapture,
    .peerFocus        = _cxuiAppKitPeerFocus,
    .peerSetTip       = _cxuiAppKitPeerSetTip,
    .canvasSetCursor  = _cxuiAppKitCanvasSetCursor,
    .surfaceGet       = _cxuiAppKitSurfaceGet,
    .doubleClick      = _cxuiAppKitDoubleClick,

    .panelCreate         = _cxuiAppKitPanelCreate,
    .panelSetBounds      = _cxuiAppKitPanelSetBounds,
    .panelSetColors      = _cxuiAppKitPanelSetColors,
    .panelSetTitle       = _cxuiAppKitPanelSetTitle,
    .panelSetContent     = _cxuiAppKitPanelSetContent,
    .panelScrollTo       = _cxuiAppKitPanelScrollTo,
    .scrollBarSize       = _cxuiAppKitScrollBarSize,
    .windowSetBackground = _cxuiAppKitWindowSetBackground,
    .windowSetMenu       = _cxuiAppKitWindowSetMenu,

    .menuCreate = _cxuiAppKitMenuCreate,
    .menuInsert = _cxuiAppKitMenuInsert,
    .menuRemove = _cxuiAppKitMenuRemove,
    .menuUpdate = _cxuiAppKitMenuUpdate,

    .textCreate       = _cxuiAppKitTextCreate,
    .textSetBounds    = _cxuiAppKitTextSetBounds,
    .textMeasure      = _cxuiAppKitTextMeasure,
    .textSetText      = _cxuiAppKitTextSetText,
    .textGetSelection = _cxuiAppKitTextGetSelection,
    .textSetSelection = _cxuiAppKitTextSetSelection,
    .textSetCue       = _cxuiAppKitTextSetCue,
    .textSetMaxLength = _cxuiAppKitTextSetMaxLength,
    .textSetFont      = _cxuiAppKitTextSetFont,
    .textSetState     = _cxuiAppKitTextSetState,
    .textSetColors    = _cxuiAppKitTextSetColors,
    .textAreaMeasure  = _cxuiAppKitTextAreaMeasure,
    .textAppend       = _cxuiAppKitTextAppend,
    .textScrollToEnd  = _cxuiAppKitTextScrollToEnd,

    .labelCreate      = _cxuiAppKitLabelCreate,
    .labelSetText     = _cxuiAppKitLabelSetText,
    .labelSetStyle    = _cxuiAppKitLabelSetStyle,
    .controlSetColor    = _cxuiAppKitControlSetColor,
    .labelMeasure     = _cxuiAppKitLabelMeasure,
    .labelHeightFor   = _cxuiAppKitLabelHeightFor,
    .buttonCreate     = _cxuiAppKitButtonCreate,
    .buttonSetTitle   = _cxuiAppKitButtonSetTitle,
    .buttonSetDefault = _cxuiAppKitButtonSetDefault,
    .buttonMeasure    = _cxuiAppKitButtonMeasure,
    .checkCreate      = _cxuiAppKitCheckCreate,
    .checkSetState    = _cxuiAppKitCheckSetState,
    .checkMeasure     = _cxuiAppKitCheckMeasure,
    .listCreate       = _cxuiAppKitListCreate,
    .listSplice       = _cxuiAppKitListSplice,
    .listSetValue     = _cxuiAppKitListSetValue,
    .listShow         = _cxuiAppKitListShow,
    .listMeasure      = _cxuiAppKitListMeasure,
    .listSetMarks     = _cxuiAppKitListSetMarks,
    .listGetMarks     = _cxuiAppKitListGetMarks,
    .listTopItem      = _cxuiAppKitListTopItem,
    .listSetTopItem   = _cxuiAppKitListSetTopItem,
    .sliderCreate     = _cxuiAppKitSliderCreate,
    .sliderSetRange   = _cxuiAppKitSliderSetRange,
    .sliderSetPos     = _cxuiAppKitSliderSetPos,
    .sliderMeasure    = _cxuiAppKitSliderMeasure,
    .progressCreate   = _cxuiAppKitProgressCreate,
    .progressSet      = _cxuiAppKitProgressSet,
    .tabsCreate       = _cxuiAppKitTabsCreate,
    .tabsInsert       = _cxuiAppKitTabsInsert,
    .tabsRemove       = _cxuiAppKitTabsRemove,
    .tabsSetTitle     = _cxuiAppKitTabsSetTitle,
    .tabsSetValue     = _cxuiAppKitTabsSetValue,
    .tabsSetPage      = _cxuiAppKitTabsSetPage,
    .tabsMeasure      = _cxuiAppKitTabsMeasure,
    .buttonsFocus     = _cxuiAppKitButtonsFocus,
    .controlSetBounds = _cxuiAppKitControlSetBounds,
    .controlSetFont   = _cxuiAppKitControlSetFont,

    .monitors         = _cxuiAppKitMonitors,
    .clipboardSetText = _cxuiAppKitClipboardSetText,
    .clipboardGetText = _cxuiAppKitClipboardGetText,

    .messageBox = _cxuiAppKitMessageBox,
    .fileDialog = _cxuiAppKitFileDialog,
    .colorDialog = _cxuiAppKitColorDialog,

    .testFrameDark        = _cxuiAppKitTestFrameDark,
    .testBorderless       = _cxuiAppKitTestBorderless,
    .testAnswerMessageBox = _cxuiAppKitTestAnswerMessageBox,
    .testAnswerFileDialog = _cxuiAppKitTestAnswerFileDialog,
    .testAnswerColorDialog = _cxuiAppKitTestAnswerColorDialog,
    .testExpose           = _cxuiAppKitTestExpose,
    .testScreenOrigin     = _cxuiAppKitTestScreenOrigin,
    .testPeerShown        = _cxuiAppKitTestPeerShown,
    .testFrontmost        = _cxuiAppKitTestFrontmost,
    .testWindowActive     = _cxuiAppKitTestWindowActive,
    .testTextInfo         = _cxuiAppKitTestTextInfo,
    .testType             = _cxuiAppKitTestType,
    .testNativeKey        = _cxuiAppKitTestNativeKey,
    .testNativeClick      = _cxuiAppKitTestNativeClick,
    .testNativeWheel      = _cxuiAppKitTestNativeWheel,
    .testControlInfo      = _cxuiAppKitTestControlInfo,
    .testClick            = _cxuiAppKitTestClick,
    .testChoose           = _cxuiAppKitTestChoose,
    .testToggle           = _cxuiAppKitTestToggle,
    .testSlide            = _cxuiAppKitTestSlide,
    .testScrollPos        = _cxuiAppKitTestScrollPos,
    .testScrollWheel      = _cxuiAppKitTestScrollWheel,
    .testTip              = _cxuiAppKitTestTip,
    .testOwnedBy          = _cxuiAppKitTestOwnedBy,
    .testSurfaceInfo      = _cxuiAppKitTestSurfaceInfo,
    .testMenuCount        = _cxuiAppKitTestMenuCount,
    .testMenuItem         = _cxuiAppKitTestMenuItem,
    .testMenuChoose       = _cxuiAppKitTestMenuChoose,
    .testWindowMenu       = _cxuiAppKitTestWindowMenu,
};
