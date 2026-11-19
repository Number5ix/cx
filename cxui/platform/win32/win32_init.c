#include "win32_ui.h"

#pragma comment(lib, "user32.lib")

// Each session has a hidden message-only window on the UI thread. Wakes are posted to it and the
// queue's timer runs on it. Both reach it from every message loop, including the modal loops of
// message boxes, common dialogs, menus and window moves, so the UI queue keeps running there.

#define WM_CXUI_WAKE      (WM_APP + 0x100)
#define CXUI_TIMER_QUEUE  1

static const WCHAR dispatchClass[] = L"CxUiDispatch";
static atomic(ptr) dispatchWnd;   // read by wake on any thread

static LRESULT CALLBACK win32DispatchProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CXUI_WAKE:
        // Modal loops cxui doesn't own, such as a message box's, end on WM_QUIT and post it
        // again for the loop outside them; cxui's own loops end on exitRequested.
        if (atomicLoad(bool, &_cxui.exitRequested, Acquire))
            PostQuitMessage(0);
        _cxuiTick();
        return 0;
    case WM_TIMER:
        if (wp == CXUI_TIMER_QUEUE) {
            KillTimer(hwnd, CXUI_TIMER_QUEUE);
            _cxuiTick();
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool win32Init(flags_t flags)
{
    unused_noeval(flags);
    HINSTANCE inst = GetModuleHandleW(NULL);

    // Window classes outlive a session; a later session finds this one already registered.
    WNDCLASSEXW wc   = { sizeof(wc) };
    wc.lpfnWndProc   = win32DispatchProc;
    wc.hInstance     = inst;
    wc.lpszClassName = dispatchClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        logFmt(Error, _SL("cxui couldn't register its window class (error ${uint})"), stvar(uint32, GetLastError()));
        return false;
    }
    if (!_cxuiWin32WindowInit(inst) || !_cxuiWin32CanvasInit(inst))
        return false;

    HWND hwnd = CreateWindowExW(0, dispatchClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, inst, NULL);
    if (!hwnd) {
        logFmt(Error, _SL("cxui couldn't create its dispatch window (error ${uint})"), stvar(uint32, GetLastError()));
        return false;
    }
    atomicStore(ptr, &dispatchWnd, hwnd, Release);
    return true;
}

HWND _cxuiWin32DispatchWindow(void)
{
    return atomicLoad(ptr, &dispatchWnd, Acquire);
}

static void win32Shutdown(void)
{
    HWND hwnd = atomicExchange(ptr, &dispatchWnd, NULL, AcqRel);
    if (hwnd)
        DestroyWindow(hwnd);
}

// Returns false on WM_QUIT, which is put back for whoever runs the next loop out.
static bool win32Dispatch(MSG* msg)
{
    if (msg->message == WM_QUIT) {
        PostQuitMessage((int)msg->wParam);
        return false;
    }
    TranslateMessage(msg);
    DispatchMessageW(msg);
    return true;
}

static void win32RunOuter(atomic(bool)* exitFlag)
{
    // WM_QUIT only unwinds loops cxui doesn't own when shutting down; this one ends on exitFlag.
    MSG msg;
    while (!_cxuiLoopDone(exitFlag)) {
        BOOL got = GetMessageW(&msg, NULL, 0, 0);
        if (got == -1)
            break;
        if (got) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

static void win32RunNested(atomic(bool)* exitFlag)
{
    MSG msg;
    while (!_cxuiLoopDone(exitFlag)) {
        if (GetMessageW(&msg, NULL, 0, 0) == -1 || !win32Dispatch(&msg))
            break;
    }
}

static bool win32Flush(void)
{
    MSG msg;
    // Bounded, so a source of messages that never runs dry can't hold the caller here.
    for (int32 i = 0; i < 64 && PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE); i++) {
        if (!win32Dispatch(&msg))
            return false;
    }
    return PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE) != 0;
}

static void win32Wake(void)
{
    HWND hwnd = atomicLoad(ptr, &dispatchWnd, Acquire);
    if (hwnd)
        PostMessageW(hwnd, WM_CXUI_WAKE, 0, 0);
}

static void win32ArmTimer(int64 delay)
{
    HWND hwnd = atomicLoad(ptr, &dispatchWnd, Acquire);
    if (!hwnd)
        return;

    if (delay == timeForever) {
        KillTimer(hwnd, CXUI_TIMER_QUEUE);
        return;
    }

    if (delay <= 0) {
        // Over budget. Posted messages are retrieved before input and painting, so the next slice
        // is posted only when neither is waiting. Otherwise a timer, which comes after both, but
        // which Windows never fires sooner than USER_TIMER_MINIMUM.
        if (HIWORD(GetQueueStatus(QS_INPUT | QS_PAINT)) == 0) {
            KillTimer(hwnd, CXUI_TIMER_QUEUE);
            PostMessageW(hwnd, WM_CXUI_WAKE, 0, 0);
            return;
        }
        delay = 0;
    }

    int64 ms = (delay + 999) / 1000;
    SetTimer(hwnd, CXUI_TIMER_QUEUE, (UINT)min(ms, (int64)USER_TIMER_MAXIMUM), NULL);
}

const CxUiBackend _cxuiWin32Backend = {
    .name      = _S"win32",
    .mnemonics = true,
    .init      = win32Init,
    .shutdown  = win32Shutdown,
    .runOuter  = win32RunOuter,
    .runNested = win32RunNested,
    .flush     = win32Flush,
    .wake      = win32Wake,
    .armTimer  = win32ArmTimer,

    .peerDestroy    = _cxuiWin32PeerDestroy,
    .peerSetVisible = _cxuiWin32PeerSetVisible,
    .peerSetEnabled = _cxuiWin32PeerSetEnabled,

    .windowCreate       = _cxuiWin32WindowCreate,
    .windowSetMinSize   = _cxuiWin32WindowSetMinSize,
    .windowSetResizable = _cxuiWin32WindowSetResizable,
    .windowBeginDrag    = _cxuiWin32WindowBeginDrag,
    .windowLayoutBegin  = _cxuiWin32WindowLayoutBegin,
    .windowLayoutEnd    = _cxuiWin32WindowLayoutEnd,
    .windowShow        = _cxuiWin32WindowShow,
    .windowHide        = _cxuiWin32WindowHide,
    .windowSetTitle    = _cxuiWin32WindowSetTitle,
    .windowSetSize     = _cxuiWin32WindowSetSize,
    .windowGetPosition = _cxuiWin32WindowGetPosition,
    .windowSetPosition = _cxuiWin32WindowSetPosition,
    .placementCaps      = CXUI_PLACE_Position | CXUI_PLACE_Size | CXUI_PLACE_State,
    .windowGetPlacement = _cxuiWin32WindowGetPlacement,
    .windowSetPlacement = _cxuiWin32WindowSetPlacement,
    .windowSetDark     = _cxuiWin32WindowSetDark,
    .systemTheme       = _cxuiWin32SystemTheme,
    .windowBeginModal  = _cxuiWin32WindowBeginModal,
    .windowEndModal    = _cxuiWin32WindowEndModal,
    .windowSetOwner    = _cxuiWin32WindowSetOwner,
    .windowSetFullscreen = _cxuiWin32WindowSetFullscreen,
    .windowMonitor       = _cxuiWin32WindowMonitorOf,
    .windowSetTopmost    = _cxuiWin32WindowSetTopmost,
    .windowMoveNear    = _cxuiWin32WindowMoveNear,
    .windowSetEnabled  = _cxuiWin32WindowSetEnabled,

    .systemScale      = _cxuiWin32SystemScale,
    .canvasCreate     = _cxuiWin32CanvasCreate,
    .surfaceGet       = _cxuiWin32SurfaceGet,
    .canvasSetBounds  = _cxuiWin32CanvasSetBounds,
    .canvasInvalidate = _cxuiWin32CanvasInvalidate,
    .canvasPaintNow   = _cxuiWin32CanvasPaintNow,
    .canvasCapture    = _cxuiWin32CanvasCapture,
    .peerFocus        = _cxuiWin32PeerFocus,
    .peerRaise        = _cxuiWin32PeerRaise,
    .windowFocusCues  = _cxuiWin32WindowFocusCues,
    .peerSetTip       = _cxuiWin32PeerSetTip,
    .canvasSetCursor  = _cxuiWin32CanvasSetCursor,
    .canvasSetCursorImage = _cxuiWin32CanvasSetCursorImage,
    .canvasWarpPointer    = _cxuiWin32CanvasWarpPointer,
    .canvasSetTextInput  = _cxuiWin32CanvasSetTextInput,
    .canvasSetTextCaret  = _cxuiWin32CanvasSetTextCaret,
    .canvasSetKeyCapture = _cxuiWin32CanvasSetKeyCapture,
    .canvasPointerLock   = _cxuiWin32CanvasPointerLock,
    .doubleClick      = _cxuiWin32DoubleClick,
    .keyLocks         = _cxuiWin32KeyLocks,

    .panelCreate         = _cxuiWin32PanelCreate,
    .panelSetBounds      = _cxuiWin32PanelSetBounds,
    .panelSetColors      = _cxuiWin32PanelSetColors,
    .panelSetTitle       = _cxuiWin32PanelSetTitle,
    .panelSetContent     = _cxuiWin32PanelSetContent,
    .panelScrollTo       = _cxuiWin32PanelScrollTo,
    .scrollBarSize       = _cxuiWin32ScrollBarSize,
    .windowSetBackground = _cxuiWin32WindowSetBackground,
    .windowSetMenu       = _cxuiWin32WindowSetMenu,
    .menuCreate          = _cxuiWin32MenuCreate,
    .menuInsert          = _cxuiWin32MenuInsert,
    .menuRemove          = _cxuiWin32MenuRemove,
    .menuUpdate          = _cxuiWin32MenuUpdate,
    .systemPanelColor    = _cxuiWin32SystemPanelColor,

    .textCreate       = _cxuiWin32TextCreate,
    .textSetBounds    = _cxuiWin32TextSetBounds,
    .textMeasure      = _cxuiWin32TextMeasure,
    .textSetText      = _cxuiWin32TextSetText,
    .textGetSelection = _cxuiWin32TextGetSelection,
    .textSetSelection = _cxuiWin32TextSetSelection,
    .textSetCue       = _cxuiWin32TextSetCue,
    .textSetMaxLength = _cxuiWin32TextSetMaxLength,
    .textSetFont      = _cxuiWin32TextSetFont,
    .textSetState     = _cxuiWin32TextSetState,
    .textSetColors    = _cxuiWin32TextSetColors,
    .textSetDarkColors = _cxuiWin32TextSetDarkColors,
    .textAreaMeasure  = _cxuiWin32TextAreaMeasure,
    .textAppend       = _cxuiWin32TextAppend,
    .textScrollToEnd  = _cxuiWin32TextScrollToEnd,

    .labelCreate      = _cxuiWin32LabelCreate,
    .labelSetText     = _cxuiWin32LabelSetText,
    .labelSetStyle    = _cxuiWin32LabelSetStyle,
    .controlSetColor    = _cxuiWin32LabelSetColor,
    .labelMeasure     = _cxuiWin32LabelMeasure,
    .labelHeightFor   = _cxuiWin32LabelHeightFor,
    .buttonCreate     = _cxuiWin32ButtonCreate,
    .buttonSetTitle   = _cxuiWin32ButtonSetTitle,
    .buttonSetDefault = _cxuiWin32ButtonSetDefault,
    .buttonMeasure    = _cxuiWin32ButtonMeasure,
    .checkCreate      = _cxuiWin32CheckCreate,
    .checkSetState    = _cxuiWin32CheckSetState,
    .checkMeasure     = _cxuiWin32CheckMeasure,
    .listCreate       = _cxuiWin32ListCreate,
    .listSplice       = _cxuiWin32ListSplice,
    .listSetValue     = _cxuiWin32ListSetValue,
    .listShow         = _cxuiWin32ListShow,
    .listMeasure      = _cxuiWin32ListMeasure,
    .listSetColors    = _cxuiWin32ListSetColors,
    .listSetMarks     = _cxuiWin32ListSetMarks,
    .listGetMarks     = _cxuiWin32ListGetMarks,
    .listTopItem      = _cxuiWin32ListTopItem,
    .listSetTopItem   = _cxuiWin32ListSetTopItem,
    .sliderCreate     = _cxuiWin32SliderCreate,
    .sliderSetRange   = _cxuiWin32SliderSetRange,
    .sliderSetPos     = _cxuiWin32SliderSetPos,
    .sliderMeasure    = _cxuiWin32SliderMeasure,
    .progressCreate   = _cxuiWin32ProgressCreate,
    .progressSet      = _cxuiWin32ProgressSet,
    .tabsCreate       = _cxuiWin32TabsCreate,
    .tabsInsert       = _cxuiWin32TabsInsert,
    .tabsRemove       = _cxuiWin32TabsRemove,
    .tabsSetTitle     = _cxuiWin32TabsSetTitle,
    .tabsSetValue     = _cxuiWin32TabsSetValue,
    .tabsSetPage      = _cxuiWin32TabsSetPage,
    .tabsHold         = _cxuiWin32TabsHold,
    .tabsSetColors    = _cxuiWin32TabsSetColors,
    .tabsMeasure      = _cxuiWin32TabsMeasure,
    .controlSetBounds = _cxuiWin32ControlSetBounds,
    .controlSetFont   = _cxuiWin32ControlSetFont,

    .monitors         = _cxuiWin32Monitors,
    .clipboardSetText = _cxuiWin32ClipboardSetText,
    .clipboardGetText = _cxuiWin32ClipboardGetText,

    .messageBox = _cxuiWin32MessageBox,
    .fileDialog = _cxuiWin32FileDialog,
    .colorDialog = _cxuiWin32ColorDialog,

    .testFrameDark        = _cxuiWin32TestFrameDark,
    .testFocusCues        = _cxuiWin32TestFocusCues,
    .testBorderless       = _cxuiWin32TestBorderless,
    .testAnswerMessageBox = _cxuiWin32TestAnswerMessageBox,
    .testAnswerFileDialog = _cxuiWin32TestAnswerFileDialog,
    .testAnswerColorDialog = _cxuiWin32TestAnswerColorDialog,
    .testOwnedBy = _cxuiWin32TestOwnedBy,
    .testExpose           = _cxuiWin32TestExpose,
    .testScreenOrigin     = _cxuiWin32TestScreenOrigin,
    .testForceDpi         = _cxuiWin32TestForceDpi,
    .testPeerShown        = _cxuiWin32TestPeerShown,
    .testFrontmost        = _cxuiWin32TestFrontmost,
    .testTextInfo         = _cxuiWin32TestTextInfo,
    .testControlInfo      = _cxuiWin32TestControlInfo,
    .testClick            = _cxuiWin32TestClick,
    .testChoose           = _cxuiWin32TestChoose,
    .testSurfaceInfo      = _cxuiWin32TestSurfaceInfo,
    .testSurfaceMark      = _cxuiWin32TestSurfaceMark,
    .testToggle           = _cxuiWin32TestToggle,
    .testSlide            = _cxuiWin32TestSlide,
    .testScrollPos        = _cxuiWin32TestScrollPos,
    .testScrollWheel      = _cxuiWin32TestScrollWheel,
    .testTip              = _cxuiWin32TestTip,
    .testType             = _cxuiWin32TestType,
    .testNativeKey        = _cxuiWin32TestNativeKey,
    .testNativeClick      = _cxuiWin32TestNativeClick,
    .testNativeWheel      = _cxuiWin32TestNativeWheel,
    .testMenuCount        = _cxuiWin32TestMenuCount,
    .testMenuItem         = _cxuiWin32TestMenuItem,
    .testMenuChoose       = _cxuiWin32TestMenuChoose,
    .testWindowMenu       = _cxuiWin32TestWindowMenu,
    .testWindowActive     = _cxuiWin32TestWindowActive,
    .testTopmost = _cxuiWin32TestTopmost,
    .testClipboardForeign = _cxuiWin32TestClipboardForeign,
    .testClipboardRaw     = _cxuiWin32TestClipboardRaw,
};
