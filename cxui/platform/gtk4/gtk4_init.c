#include "gtk4_ui.h"

// The UI queue is ticked from a custom GSource on GLib's default main context, which every loop on
// the UI thread iterates, GTK's nested dialog loops included.
//
// GLib dispatches only the highest-priority sources that are ready, so a source at the default
// priority that is always ready while the queue is over budget would starve GTK's redraw, which
// runs at GDK_PRIORITY_REDRAW. The source sits just below it: input, layout and painting go first.
#define CXUI_GSOURCE_PRIORITY (GDK_PRIORITY_REDRAW + 10)

static GSource* queueSource;  // UI thread only

// Set by wake (any thread) and when over budget; cleared when the source dispatches. Static rather
// than in the source, so a late wake from another thread never touches a destroyed source.
static atomic(bool) sourceReady;

static gboolean gtk4SourcePrepare(GSource* s, gint* timeout)
{
    unused_noeval(s);
    *timeout = -1;
    return atomicLoad(bool, &sourceReady, Acquire);
}

static gboolean gtk4SourceCheck(GSource* s)
{
    unused_noeval(s);
    return atomicLoad(bool, &sourceReady, Acquire);
}

static gboolean gtk4SourceDispatch(GSource* s, GSourceFunc cb, gpointer ud)
{
    unused_noeval(cb);
    unused_noeval(ud);
    atomicStore(bool, &sourceReady, false, Release);
    g_source_set_ready_time(s, -1);   // GLib doesn't reset it after dispatching
    _cxuiTick();                      // re-arms through gtk4ArmTimer
    return G_SOURCE_CONTINUE;
}

static GSourceFuncs queueSourceFuncs = {
    .prepare  = gtk4SourcePrepare,
    .check    = gtk4SourceCheck,
    .dispatch = gtk4SourceDispatch,
};

static bool gtk4Init(flags_t flags)
{
    unused_noeval(flags);

    // Only the first call tries to open a display, and later ones return TRUE even if that
    // failed, so the display itself says whether GTK can be used.
    if (!gtk_init_check() || !gdk_display_get_default()) {
        logStr(Info, _SL("cxui couldn't open a display for GTK"));
        return false;
    }

    // A session before this one released the context when its thread finished.
    if (!g_main_context_acquire(NULL)) {
        logStr(Error, _SL("another thread owns GLib's default main context; cxui needs it for its UI thread"));
        return false;
    }

    atomicStore(bool, &sourceReady, false, Release);
    queueSource = g_source_new(&queueSourceFuncs, sizeof(GSource));
    g_source_set_priority(queueSource, CXUI_GSOURCE_PRIORITY);
    // Queued work opens popups and message boxes, whose nested loops run inside this source's
    // dispatch; GLib skips a source that is already dispatching unless it may recurse.
    g_source_set_can_recurse(queueSource, TRUE);
    g_source_set_name(queueSource, "cxui queue");
    g_source_attach(queueSource, NULL);
    _cxuiGtk4ThemeWatch(true);
    _cxuiGtk4DesktopInit();
    return true;
}

static void gtk4Shutdown(void)
{
    _cxuiGtk4ThemeWatch(false);
    _cxuiGtk4TextShutdown();
    _cxuiGtk4ControlsShutdown();
    _cxuiGtk4ClipboardStore();
    _cxuiGtk4DesktopShutdown();
    g_source_destroy(queueSource);
    g_source_unref(queueSource);
    queueSource = NULL;

    // Let GTK finish anything the destroyed windows left pending before the context moves on.
    while (g_main_context_iteration(NULL, FALSE))
        ;
    // A GL context is current on one thread at most. Left current on this thread, which is about
    // to exit, it can't be made current on the next session's UI thread, and GTK crashes.
    gdk_gl_context_clear_current();
    g_main_context_release(NULL);
}

static void gtk4Run(atomic(bool)* exitFlag)
{
    while (!_cxuiLoopDone(exitFlag))
        g_main_context_iteration(NULL, TRUE);
}

static bool gtk4Flush(void)
{
    // Bounded, so a source that is always ready can't hold the caller here.
    for (int32 i = 0; i < 64 && g_main_context_iteration(NULL, FALSE); i++)
        ;
    return g_main_context_pending(NULL);
}

static void gtk4Wake(void)
{
    atomicStore(bool, &sourceReady, true, Release);
    g_main_context_wakeup(NULL);
}

static void gtk4ArmTimer(int64 delay)
{
    if (delay == timeForever) {
        g_source_set_ready_time(queueSource, -1);
    } else if (delay <= 0) {
        atomicStore(bool, &sourceReady, true, Release);
    } else {
        // Both clocks are monotonic microseconds but may not share an epoch, so only the delay
        // crosses over.
        g_source_set_ready_time(queueSource, g_get_monotonic_time() + delay);
    }
}

const CxUiBackend _cxuiGtk4Backend = {
    .name      = _S"gtk4",
    .mnemonics = true,
    .tabMnemonics = true,
    .init      = gtk4Init,
    .shutdown  = gtk4Shutdown,
    .runOuter  = gtk4Run,
    .runNested = gtk4Run,
    .flush     = gtk4Flush,
    .wake      = gtk4Wake,
    .armTimer  = gtk4ArmTimer,

    .peerDestroy    = _cxuiGtk4PeerDestroy,
    .peerSetVisible = _cxuiGtk4PeerSetVisible,
    .peerSetEnabled = _cxuiGtk4PeerSetEnabled,

    .windowCreate       = _cxuiGtk4WindowCreate,
    .windowSetMinSize   = _cxuiGtk4WindowSetMinSize,
    .windowSetResizable = _cxuiGtk4WindowSetResizable,
    .windowBeginDrag    = _cxuiGtk4WindowBeginDrag,
    .windowShow     = _cxuiGtk4WindowShow,
    .windowHide     = _cxuiGtk4WindowHide,
    .windowSetTitle = _cxuiGtk4WindowSetTitle,
    .windowSetSize  = _cxuiGtk4WindowSetSize,
    .placementCaps      = CXUI_PLACE_Size | CXUI_PLACE_State,
    .windowGetPlacement = _cxuiGtk4WindowGetPlacement,
    .windowSetPlacement = _cxuiGtk4WindowSetPlacement,
    .windowSetDark  = _cxuiGtk4WindowSetDark,
    .systemTheme    = _cxuiGtk4SystemTheme,
    .windowBeginModal = _cxuiGtk4WindowBeginModal,
    .windowEndModal   = _cxuiGtk4WindowEndModal,
    .windowSetOwner   = _cxuiGtk4WindowSetOwner,
    .windowSetFullscreen = _cxuiGtk4WindowSetFullscreen,
    .windowMonitor       = _cxuiGtk4WindowMonitor,
    .windowSetTopmost    = _cxuiGtk4WindowSetTopmost,

    .systemScale      = _cxuiGtk4SystemScale,
    .canvasCreate     = _cxuiGtk4CanvasCreate,
    .surfaceGet       = _cxuiGtk4SurfaceGetPeer,
    .canvasSetBounds  = _cxuiGtk4CanvasSetBounds,
    .canvasInvalidate = _cxuiGtk4CanvasInvalidate,
    .canvasPaintNow   = _cxuiGtk4CanvasPaintNow,
    .canvasCapture    = _cxuiGtk4CanvasCapture,
    .peerFocus        = _cxuiGtk4PeerFocus,
    .peerRaise        = _cxuiGtk4PeerRaise,
    .peerSetTip       = _cxuiGtk4PeerSetTip,
    .canvasSetCursor  = _cxuiGtk4CanvasSetCursor,
    .canvasSetCursorImage = _cxuiGtk4CanvasSetCursorImage,
    .canvasWarpPointer    = _cxuiGtk4CanvasWarpPointer,
    .canvasSetTextInput  = _cxuiGtk4CanvasSetTextInput,
    .canvasSetTextCaret  = _cxuiGtk4CanvasSetTextCaret,
    .canvasSetKeyCapture = _cxuiGtk4CanvasSetKeyCapture,
    .canvasPointerLock   = _cxuiGtk4CanvasPointerLock,
    .doubleClick      = _cxuiGtk4DoubleClick,
    .keyLocks         = _cxuiGtk4KeyLocks,

    .panelCreate         = _cxuiGtk4PanelCreate,

    .panelSetBounds      = _cxuiGtk4PanelSetBounds,

    .panelSetColors      = _cxuiGtk4PanelSetColors,

    .panelSetTitle       = _cxuiGtk4PanelSetTitle,

    .panelSetContent     = _cxuiGtk4PanelSetContent,

    .panelScrollTo       = _cxuiGtk4PanelScrollTo,

    .windowSetBackground = _cxuiGtk4WindowSetBackground,
    .windowSetMenu       = _cxuiGtk4WindowSetMenu,
    .menuCreate          = _cxuiGtk4MenuCreate,
    .menuInsert          = _cxuiGtk4MenuInsert,
    .menuRemove          = _cxuiGtk4MenuRemove,
    .menuUpdate          = _cxuiGtk4MenuUpdate,


    .textCreate       = _cxuiGtk4TextCreate,
    .textSetBounds    = _cxuiGtk4TextSetBounds,
    .textMeasure      = _cxuiGtk4TextMeasure,
    .textSetText      = _cxuiGtk4TextSetText,
    .textGetSelection = _cxuiGtk4TextGetSelection,
    .textSetSelection = _cxuiGtk4TextSetSelection,
    .textSetCue       = _cxuiGtk4TextSetCue,
    .textSetMaxLength = _cxuiGtk4TextSetMaxLength,
    .textSetFont      = _cxuiGtk4TextSetFont,
    .textSetState     = _cxuiGtk4TextSetState,
    .textSetColors    = _cxuiGtk4TextSetColors,
    .textAreaMeasure  = _cxuiGtk4TextAreaMeasure,
    .textAppend       = _cxuiGtk4TextAppend,
    .textScrollToEnd  = _cxuiGtk4TextScrollToEnd,

    .labelCreate      = _cxuiGtk4LabelCreate,
    .labelSetText     = _cxuiGtk4LabelSetText,
    .labelSetStyle    = _cxuiGtk4LabelSetStyle,
    .controlSetColor    = _cxuiGtk4ControlSetColor,
    .labelMeasure     = _cxuiGtk4LabelMeasure,
    .labelHeightFor   = _cxuiGtk4LabelHeightFor,
    .buttonCreate     = _cxuiGtk4ButtonCreate,
    .buttonSetTitle   = _cxuiGtk4ButtonSetTitle,
    .buttonSetDefault = _cxuiGtk4ButtonSetDefault,
    .buttonMeasure    = _cxuiGtk4ButtonMeasure,
    .checkCreate      = _cxuiGtk4CheckCreate,
    .checkSetState    = _cxuiGtk4CheckSetState,
    .checkMeasure     = _cxuiGtk4CheckMeasure,
    .listCreate       = _cxuiGtk4ListCreate,
    .listSplice       = _cxuiGtk4ListSplice,
    .listSetValue     = _cxuiGtk4ListSetValue,
    .listShow         = _cxuiGtk4ListShow,
    .listMeasure      = _cxuiGtk4ListMeasure,
    .listSetColors    = _cxuiGtk4ListSetColors,
    .listSetMarks     = _cxuiGtk4ListSetMarks,
    .listGetMarks     = _cxuiGtk4ListGetMarks,
    .listTopItem      = _cxuiGtk4ListTopItem,
    .listSetTopItem   = _cxuiGtk4ListSetTopItem,
    .sliderCreate     = _cxuiGtk4SliderCreate,
    .sliderSetRange   = _cxuiGtk4SliderSetRange,
    .sliderSetPos     = _cxuiGtk4SliderSetPos,
    .sliderMeasure    = _cxuiGtk4SliderMeasure,
    .progressCreate   = _cxuiGtk4ProgressCreate,
    .progressSet      = _cxuiGtk4ProgressSet,
    .tabsCreate       = _cxuiGtk4TabsCreate,
    .tabsInsert       = _cxuiGtk4TabsInsert,
    .tabsRemove       = _cxuiGtk4TabsRemove,
    .tabsSetTitle     = _cxuiGtk4TabsSetTitle,
    .tabsSetValue     = _cxuiGtk4TabsSetValue,
    .tabsSetPage      = _cxuiGtk4TabsSetPage,
    .tabsSetColors    = _cxuiGtk4TabsSetColors,
    .tabsMeasure      = _cxuiGtk4TabsMeasure,
    .progressMeasure  = _cxuiGtk4ProgressMeasure,
    .controlSetBounds = _cxuiGtk4ControlSetBounds,
    .controlSetFont   = _cxuiGtk4ControlSetFont,

    .monitors         = _cxuiGtk4Monitors,
    .clipboardSetText = _cxuiGtk4ClipboardSetText,
    .clipboardGetText = _cxuiGtk4ClipboardGetText,

    .messageBox = _cxuiGtk4MessageBox,
    .fileDialog = _cxuiGtk4FileDialog,
    .colorDialog = _cxuiGtk4ColorDialog,

    .testFrameDark        = _cxuiGtk4TestFrameDark,
    .testBorderless       = _cxuiGtk4TestBorderless,
    .testClipboardForeign = _cxuiGtk4TestClipboardForeign,

    .testPeerShown = _cxuiGtk4TestPeerShown,
    .testFrontmost = _cxuiGtk4TestFrontmost,
    .testAnswerMessageBox = _cxuiGtk4TestAnswerMessageBox,
    .testAnswerFileDialog = _cxuiGtk4TestAnswerFileDialog,
    .testAnswerColorDialog = _cxuiGtk4TestAnswerColorDialog,
    .testOwnedBy = _cxuiGtk4TestOwnedBy,
    .testExpose           = _cxuiGtk4TestExpose,
    .testTextInfo         = _cxuiGtk4TestTextInfo,
    .testType             = _cxuiGtk4TestType,
    .testControlInfo      = _cxuiGtk4TestControlInfo,
    .testClick            = _cxuiGtk4TestClick,
    .testChoose           = _cxuiGtk4TestChoose,
    .testSurfaceInfo      = _cxuiGtk4TestSurfaceInfo,
    .testSurfaceMark      = _cxuiGtk4TestSurfaceMark,
    .testToggle           = _cxuiGtk4TestToggle,
    .testSlide            = _cxuiGtk4TestSlide,
    .testScrollPos        = _cxuiGtk4TestScrollPos,
    .testScrollWheel      = _cxuiGtk4TestScrollWheel,
    .testTip              = _cxuiGtk4TestTip,
    .testNativeKey        = _cxuiGtk4TestNativeKey,
    .testMenuCount        = _cxuiGtk4TestMenuCount,
    .testMenuItem         = _cxuiGtk4TestMenuItem,
    .testMenuChoose       = _cxuiGtk4TestMenuChoose,
    .testWindowMenu       = _cxuiGtk4TestWindowMenu,
    .testWindowActive     = _cxuiGtk4TestWindowActive,
};
