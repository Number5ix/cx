#include "cxui_private.h"

// Each slot's function type, as closureCreateAs records it, so a hand-made closure of the wrong
// type is caught when it is set rather than when it is first called.
static const char* const slotSig[CXUI_EV_Count] = {
    [CXUI_EV_Activate]   = "CxUiActivateFn",
    [CXUI_EV_Change]     = "CxUiChangeFn",
    [CXUI_EV_Commit]     = "CxUiCommitFn",
    [CXUI_EV_Key]        = "CxUiKeyFn",
    [CXUI_EV_Mouse]      = "CxUiMouseFn",
    [CXUI_EV_Wheel]      = "CxUiWheelFn",
    [CXUI_EV_Focus]      = "CxUiFocusFn",
    [CXUI_EV_Paint]      = "CxUiPaintFn",
    [CXUI_EV_Resize]     = "CxUiResizeFn",
    [CXUI_EV_Close]      = "CxUiCloseFn",
    [CXUI_EV_ShowState]  = "CxUiShowStateFn",
    [CXUI_EV_Move]       = "CxUiMoveFn",
    [CXUI_EV_DpiChanged] = "CxUiDpiChangedFn",
    [CXUI_EV_ThemeChanged] = "CxUiThemeChangedFn",
    [CXUI_EV_Scroll] = "CxUiScrollFn",
    [CXUI_EV_Sort] = "CxUiSortFn",
    [CXUI_EV_Context] = "CxUiContextFn",
    [CXUI_EV_Expand] = "CxUiExpandFn",
    [CXUI_EV_Check] = "CxUiCheckFn",
    [CXUI_EV_Edit] = "CxUiEditFn",
    [CXUI_EV_Surface] = "CxUiSurfaceFn",
};

#define BUBBLES ((1 << CXUI_EV_Key) | (1 << CXUI_EV_Wheel))
// Input a disabled widget never sees
#define INPUT ((1 << CXUI_EV_Key) | (1 << CXUI_EV_Wheel) | (1 << CXUI_EV_Mouse))

static void handlerSlotDtor(stype st, stgeneric* gen, uint32 flags)
{
    CxUiHandlerSlot* slot = (CxUiHandlerSlot*)gen->st_opaque;
    closureDestroy(&slot->fn);
}

stDefine(CxUiHandlerSlot) {
    .id    = stTypeId(opaque),
    .size  = sizeof(CxUiHandlerSlot),
    .flags = stFlag(PassPtr),
    .ops   = { .dtor = handlerSlotDtor },
};

// Handlers currently running, innermost last (UI thread only). A handler replaced or cleared while
// it runs is marked here instead of being destroyed, and destroyed when it returns. Managed by hand:
// it is pushed and popped on every handler call, where sarray's generic calls doubled the cost.
typedef struct RunningHandler {
    closure fn;
    bool orphaned;
} RunningHandler;

static RunningHandler* running;
static int32 nrunning, runningCap;

static void retireHandler(closure fn)
{
    if (!fn)
        return;
    for (int32 i = nrunning - 1; i >= 0; i--) {
        if (running[i].fn == fn) {
            running[i].orphaned = true;
            return;
        }
    }
    closureDestroy(&fn);
}

_Use_decl_annotations_
void _cxuiSetHandler(CxUiWidget* w, CxUiEventKind kind, closure fn)
{
    if (!_cxuiLive(w) || (uint32)kind >= CXUI_EV_Count) {
        closureDestroy(&fn);
        return;
    }
    devAssertMsg(cxuiWidgetEventMask(w) & (1 << kind), "the widget's class never raises this event");
    devAssertMsg(!fn || _closureFuncAs(fn, slotSig[kind]), "handler closure has the wrong type for its slot");

    int32 i = 0;
    while (i < saSize(w->handlers) && w->handlers.a[i].kind != (uint32)kind)
        i++;

    if (i < saSize(w->handlers)) {
        CxUiHandlerSlot old;
        if (fn) {
            old                 = w->handlers.a[i];
            w->handlers.a[i].fn = fn;
        } else {
            saExtract(&w->handlers, i, CxUiHandlerSlot, &old, SA_Fast);
        }
        retireHandler(old.fn);
    } else if (fn) {
        if (!w->handlers.a)
            saInit(&w->handlers, CxUiHandlerSlot, 2);
        saPush(&w->handlers, CxUiHandlerSlot, ((CxUiHandlerSlot){ .kind = kind, .fn = fn }));
    }
}

_Use_decl_annotations_
void _cxuiClearHandlers(CxUiWidget* w)
{
    CxUiHandlerSlot slot;
    while (saSize(w->handlers) > 0) {
        saExtract(&w->handlers, -1, CxUiHandlerSlot, &slot);
        retireHandler(slot.fn);
    }
    saDestroy(&w->handlers);
}

static closure handlerFor(CxUiWidget* w, uint32 kind)
{
    for (int32 i = 0; i < saSize(w->handlers); i++) {
        if (w->handlers.a[i].kind == kind)
            return w->handlers.a[i].fn;
    }
    return NULL;
}

#define CALL_SLOT(Kind) \
    case CXUI_EV_##Kind: return closureCallAs(CxUi##Kind##Fn, fn, (CxUi##Kind##Event*)ev)

static CxUiResult callHandler(closure fn, CxUiEvent* ev)
{
    switch (ev->kind) {
        CALL_SLOT(Activate);
        CALL_SLOT(Change);
        CALL_SLOT(Commit);
        CALL_SLOT(Key);
        CALL_SLOT(Mouse);
        CALL_SLOT(Wheel);
        CALL_SLOT(Focus);
        CALL_SLOT(Paint);
        CALL_SLOT(Resize);
        CALL_SLOT(Close);
        CALL_SLOT(ShowState);
        CALL_SLOT(Move);
        CALL_SLOT(DpiChanged);
        CALL_SLOT(ThemeChanged);
        CALL_SLOT(Scroll);
        CALL_SLOT(Sort);
        CALL_SLOT(Context);
        CALL_SLOT(Expand);
        CALL_SLOT(Check);
        CALL_SLOT(Edit);
        CALL_SLOT(Surface);
    default:
        return CXUI_Default;
    }
}

// Runs w's handler for the event, if it has one. The caller holds a reference to w.
static CxUiResult runHandler(CxUiWidget* w, CxUiEvent* ev)
{
    if (!_cxuiLive(w))
        return CXUI_Default;
    if ((w->state & CXUI_WS_Inactive) && (INPUT & (1 << ev->kind)))
        return CXUI_Default;
    CxUiResult result = CXUI_Default;
    closure fn        = handlerFor(w, ev->kind);
    if (fn) {
        if (nrunning == runningCap) {
            runningCap = runningCap ? runningCap * 2 : 8;
            xaResize(&running, runningCap * sizeof(RunningHandler));
        }
        running[nrunning++] = (RunningHandler){ .fn = fn };

        ev->current = w;
        result      = callHandler(fn, ev);

        // By index, after the call: a handler that opened a modal loop may have grown the stack.
        RunningHandler done = running[--nrunning];
        if (done.orphaned)
            closureDestroy(&done.fn);
    }

    // The widget's own handling, which the program's handler can take over by handling the event
    if (result == CXUI_Default && _cxuiLive(w)) {
        ev->current = w;
        result      = cxuiWidgetHandleEvent(w, ev);
    }
    return result;
}

#define PATH_INLINE 32

_Use_decl_annotations_
CxUiResult _cxuiDispatch(CxUiWidget* target, CxUiEvent* ev)
{
    devAssert((uint32)ev->kind < CXUI_EV_Count);
    ev->target  = target;
    ev->current = target;

    _cxuiNativeEnter();
    CxUiResult result = CXUI_Default;

    if (!(BUBBLES & (1 << ev->kind))) {
        ev->window = _cxuiWindowOf(target);
        objAcquire(target);
        result = runHandler(target, ev);
        objRelease(&target);
    } else {
        // The path is fixed, and held, before any handler runs, so a handler that destroys or
        // moves a widget can't change which widgets the event visits.
        CxUiWidget* inlinePath[PATH_INLINE];
        CxUiWidget** path = inlinePath;
        int32 npath = 0, cap = PATH_INLINE;
        for (CxUiWidget* w = target; w; w = w->parent) {
            if (npath == cap) {
                cap *= 2;
                if (path == inlinePath) {
                    path = xaAlloc(cap * sizeof(CxUiWidget*));
                    memcpy(path, inlinePath, sizeof(inlinePath));
                } else {
                    xaResize(&path, cap * sizeof(CxUiWidget*));
                }
            }
            path[npath++] = objAcquire(w);
        }
        ev->window = _cxuiWindowOf(path[npath - 1]);

        for (int32 i = 0; i < npath && result == CXUI_Default; i++)
            result = runHandler(path[i], ev);

        for (int32 i = 0; i < npath; i++)
            objRelease(&path[i]);
        if (path != inlinePath)
            xaFree(path);
    }

    _cxuiNativeLeave();
    return result;
}

void _cxuiNativeLeave(void)
{
    if (--_cxui.dispatchDepth == 0)
        _cxuiFlushDeferred();
}
