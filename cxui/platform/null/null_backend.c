// The null backend: no operating system UI. Used by CXUI_Start_Null, for tests everywhere and
// for headless runs.

#include "../../core/cxuidraw_private.h"
#include "core/cxuicontrols.h"

#include <math.h>

#include <cx/format.h>
#include <cx/utils/lazyinit.h>
#include <cxui/test/cxuitest.h>

// Never destroyed: a wake from another thread can arrive after the session has ended.
static Event nullWakeEv;
static LazyInitState nullWakeInit;
static int64 nullDeadline;   // UI thread only; timeForever when the timer is disarmed
static CxUiThemeMode nullSystemTheme;   // UI thread only
static float32 nullSystemScale;         // UI thread only
static int64 nullDoubleTime;            // UI thread only
static int32 nullDoubleDistance;
static int32 nullMeasures;              // text measured; UI thread only
static CxUiPeer* nullFocus;             // the canvas with focus; UI thread only
static CxUiSize nullTextNative;         // what a realized text field measures natively; 0 for none
static string nullClipboard;            // UI thread only
static bool nullClipboardHasText;
static CxUiMonitor nullMonitors[4];     // UI thread only
static int32 nullMonitorCount;
static uint32 nullLockMax;              // the best pointer lock the backend gives; UI thread only
static uint32 nullKeyLocks;             // CxUiKeyLocks; UI thread only
static string nullAwakeReason;          // while the display is kept awake; UI thread only

static void nullWakeInitOnce(void* unused)
{
    unused_noeval(unused);
    eventInit(&nullWakeEv);
}

static hashtable nullTips;   // CxUiPeer* to its tip

static bool nullInit(flags_t flags)
{
    unused_noeval(flags);
    lazyInit(&nullWakeInit, nullWakeInitOnce, NULL);
    eventReset(&nullWakeEv);
    nullDeadline    = timeForever;
    nullSystemTheme = CXUI_Theme_Light;
    nullSystemScale = 1.0f;
    nullDoubleTime     = timeMS(500);
    nullDoubleDistance = 4;
    nullFocus          = NULL;
    nullMeasures       = 0;
    nullTextNative     = (CxUiSize){ 0 };
    nullLockMax        = CXUI_POINTERLOCK_Raw;
    nullKeyLocks       = 0;
    for (int32 i = 0; i < nullMonitorCount; i++)
        cxuiMonitorDestroy(&nullMonitors[i]);
    nullMonitorCount   = 1;
    nullMonitors[0]    = (CxUiMonitor){ .bounds    = { 0, 0, 1920, 1080 },
                                        .workArea  = { 0, 0, 1920, 1040 },
                                        .scale     = 1.0f,
                                        .flags     = CXUI_MON_Primary | CXUI_MON_WorkAreaKnown | CXUI_MON_PositionKnown,
                                        .refreshHz = 60.0f };
    strDup(&nullMonitors[0].id, _S"null-0");
    strDup(&nullMonitors[0].name, _S"Null Monitor");
    return true;
}

static void nullShutdown(void)
{
    for (int32 i = 0; i < nullMonitorCount; i++)
        cxuiMonitorDestroy(&nullMonitors[i]);
    nullMonitorCount = 0;
    htDestroy(&nullTips);
    strDestroy(&nullClipboard);
    nullClipboardHasText = false;
}

static void nullLoop(atomic(bool)* exitFlag)
{
    while (!_cxuiLoopDone(exitFlag)) {
        int64 wait = timeForever;
        if (nullDeadline != timeForever) {
            int64 now = clockTimer();
            if (now >= nullDeadline) {
                nullDeadline = timeForever;
                _cxuiTick();
                continue;
            }
            wait = nullDeadline - now;
        }
        if (eventWaitTimeout(&nullWakeEv, wait))
            _cxuiTick();
    }
}

static bool nullFlush(void)
{
    return false;
}

static void nullWake(void)
{
    eventSignal(&nullWakeEv);
}

static void nullArmTimer(int64 delay)
{
    nullDeadline = (delay == timeForever) ? timeForever : clockTimer() + delay;
}

// Tests check that each peer is destroyed exactly once, on the UI thread. A window's peer records
// what it was last told, for tests to read back.
typedef struct NullPeer {
    CxUiPeer base;
    string title;
    CxUiSize size;
    CxUiSize created;
    CxUiPoint pos;
    uint32 mode;
    uint32 state;   // CxUiWindowStates
    // CxUiFullscreen, the monitor it covers, and the state to go back to
    uint32 fullscreen;
    CxUiRect fsMonitor;
    uint32 fsFrom;
    bool topmost;
    bool visible;
    bool dark;
    bool disabled;
    bool modal;
    bool resizable;
    bool borderless;
    bool focusCues;
    int32 drags;
    // The native owner: a popup's parent while it runs, otherwise ownedBy
    CxUiPeer* owner;
    CxUiPeer* ownedBy;
    struct NullPeer* nextWindow;
    CxUiSize minSize;
    CxUiColor bg;
    CxUiPeer* menu;
} NullPeer;

// A menu records its items as it was told them, and the menus they open.
typedef struct NullMenuEntry {
    string title;
    string hint;
    uint32 kind;
    bool checked;
    bool enabled;
    CxUiPeer* sub;
} NullMenuEntry;

typedef struct NullMenu {
    CxUiPeer base;
    bool bar;
    NullMenuEntry* entries;
    int32 n;
} NullMenu;

typedef struct NullPanel {
    CxUiPeer base;
    CxUiTestPanelInfo info;
    string title;
    CxUiPoint scroll;   // a scroll box's
    bool scrolls;
} NullPanel;

static atomic(int32) nullPeersAlive;
static atomic(int32) nullPeersOffThread;

static NullPeer* nullPeerCreate(void)
{
    atomicFetchAdd(int32, &nullPeersAlive, 1, AcqRel);
    return xaAllocStruct(NullPeer, XA_Zero);
}

static NullPeer* nullWindows;   // UI thread only

static void nullCanvasDestroy(CxUiPeer* p);
static void nullMenuDestroy(CxUiPeer* p);
static void nullTextDestroy(CxUiPeer* p);
static void nullControlDestroy(CxUiPeer* p);

static void nullPeerSetTip(CxUiPeer* p, strref tip)
{
    if (!nullTips)
        htInit(&nullTips, ptr, string, 8);
    if (strEmpty(tip))
        htRemove(&nullTips, ptr, p);
    else
        htInsert(&nullTips, ptr, p, strref, tip);
}

static bool nullTestTip(CxUiPeer* p, string* out)
{
    strDestroy(out);
    if (nullTips)
        htFind(nullTips, ptr, p, string, out);
    return true;
}

static void nullPeerDestroy(CxUiPeer* p)
{
    if (nullTips)
        htRemove(&nullTips, ptr, p);
    if (!cxuiIsUIThread())
        atomicFetchAdd(int32, &nullPeersOffThread, 1, AcqRel);
    atomicFetchSub(int32, &nullPeersAlive, 1, AcqRel);
    if (p->kind == CXUI_PEER_Canvas) {
        nullCanvasDestroy(p);
        return;
    }
    if (p->kind == CXUI_PEER_Panel) {
        strDestroy(&((NullPanel*)p)->title);
        xaFree(p);
        return;
    }
    if (p->kind == CXUI_PEER_Text) {
        nullTextDestroy(p);
        return;
    }
    if (p->kind == CXUI_PEER_Menu) {
        nullMenuDestroy(p);
        return;
    }
    if (_cxuiPeerIsControl(p->kind)) {
        nullControlDestroy(p);
        return;
    }
    // Windows it owns are let go, as on Windows, where they would go with it.
    for (NullPeer** link = &nullWindows; *link;) {
        NullPeer* w = *link;
        if (w->owner == p)
            w->owner = NULL;
        if (w->ownedBy == p)
            w->ownedBy = NULL;
        if (w == (NullPeer*)p)
            *link = w->nextWindow;
        else
            link = &w->nextWindow;
    }
    strDestroy(&((NullPeer*)p)->title);
    xaFree(p);
}

static CxUiSize dipsToPx(CxUiSize dips, float32 scale)
{
    return (CxUiSize){ (int32)floor(dips.w * scale + 0.5), (int32)floor(dips.h * scale + 0.5) };
}

// ---- windows ----

static CxUiPeer* nullWindowCreate(CxUiWindow* w)
{
    NullPeer* p  = nullPeerCreate();
    p->base.kind = CXUI_PEER_Window;
    strDup(&p->title, w->_title);
    if (w->_size.w == 0)
        w->_size.w = 640;
    if (w->_size.h == 0)
        w->_size.h = 480;
    w->_scale    = nullSystemScale;
    w->_clientPx = dipsToPx(w->_size, w->_scale);
    p->size      = w->_size;
    p->created   = w->_size;
    p->pos  = w->_hasPos ? w->_pos : (CxUiPoint){ 0 };
    p->dark      = w->_dark;
    p->resizable  = _cxuiWindowUserResizable(w);
    p->borderless = (w->_flags & CXUI_WINDOW_Borderless) != 0;
    p->nextWindow = nullWindows;
    nullWindows   = p;
    return &p->base;
}

static void nullWindowShow(CxUiPeer* p, uint32 mode)
{
    NullPeer* np = (NullPeer*)p;
    np->visible  = true;
    np->mode     = mode;
    // The core takes a window out of fullscreen itself.
    if (np->fullscreen && mode != CXUI_SHOW_Minimized)
        return;
    np->state = mode == CXUI_SHOW_Maximized   ? CXUI_WINDOW_Maximized
                : mode == CXUI_SHOW_Minimized ? CXUI_WINDOW_Minimized
                                              : CXUI_WINDOW_Normal;
}

// Reports the change, as a platform does.
static bool nullWindowSetFullscreen(CxUiPeer* peer, uint32 mode, const CxUiMonitor* mon)
{
    NullPeer* p = (NullPeer*)peer;
    devAssertMsg(p->visible, "fullscreen on a hidden window");
    if (mode != CXUI_FULLSCREEN_None && p->fullscreen == CXUI_FULLSCREEN_None)
        p->fsFrom = p->state == CXUI_WINDOW_Minimized ? CXUI_WINDOW_Normal : p->state;
    p->fullscreen = mode;
    p->fsMonitor  = mode != CXUI_FULLSCREEN_None && mon ? mon->bounds : (CxUiRect){ 0 };
    uint32 state  = mode != CXUI_FULLSCREEN_None ? CXUI_WINDOW_Fullscreen : p->fsFrom;
    if (state != p->state) {
        p->state = state;
        _cxuiWindowStateChanged(peer, state);
    }
    return true;
}

static void nullWindowHide(CxUiPeer* p)
{
    ((NullPeer*)p)->visible = false;
}

static void nullWindowSetTitle(CxUiPeer* p, strref title)
{
    strDup(&((NullPeer*)p)->title, title);
}

static void nullWindowSetSize(CxUiPeer* p, CxUiSize dips)
{
    ((NullPeer*)p)->size = dips;
    CxUiWindow* w        = objDynCast(CxUiWindow, p->w);
    _cxuiWindowResized(p, dipsToPx(dips, w ? w->_scale : 1.0f), dips);
}

static bool nullWindowGetPosition(CxUiPeer* p, CxUiPoint* out)
{
    *out = ((NullPeer*)p)->pos;
    return true;
}

static bool nullWindowSetPosition(CxUiPeer* p, CxUiPoint pos)
{
    ((NullPeer*)p)->pos = pos;
    return true;
}

// The rectangle is the window's position and its size in DIPs.
static void nullWindowGetPlacement(CxUiPeer* peer, CxUiPlacement* out)
{
    NullPeer* p = (NullPeer*)peer;
    *out        = (CxUiPlacement){ .normal = { p->pos.x, p->pos.y, p->size.w, p->size.h },
                                   .state  = p->state,
                                   .valid  = CXUI_PLACE_Position | CXUI_PLACE_Size | CXUI_PLACE_State };
}

static void nullWindowSetPlacement(CxUiPeer* peer, const CxUiPlacement* pl, bool withState)
{
    NullPeer* p = (NullPeer*)peer;
    // Platforms show a window to change its state.
    devAssertMsg(!withState || p->visible, "a placement's state applied to a hidden window");
    if (pl->valid & CXUI_PLACE_Position)
        p->pos = (CxUiPoint){ pl->normal.x, pl->normal.y };
    if (withState)
        p->state = pl->state;
    if ((pl->valid & CXUI_PLACE_Size) && (pl->normal.w != p->size.w || pl->normal.h != p->size.h))
        nullWindowSetSize(peer, (CxUiSize){ pl->normal.w, pl->normal.h });
}

static void nullWindowSetDark(CxUiPeer* p, bool dark)
{
    ((NullPeer*)p)->dark = dark;
}

static bool nullWindowMoveNear(CxUiPeer* peer, CxUiPeer* other, uint32 where);

static void nullWindowBeginModal(CxUiPeer* p, CxUiPeer* owner)
{
    ((NullPeer*)p)->modal = true;
    ((NullPeer*)p)->owner = owner;
    if (!p->w || !objDynCast(CxUiWindow, p->w)->_hasPos)
        nullWindowMoveNear(p, owner, CXUI_NEAR_Center);
}

static void nullWindowEndModal(CxUiPeer* p)
{
    ((NullPeer*)p)->modal = false;
    ((NullPeer*)p)->owner = ((NullPeer*)p)->ownedBy;
}

static bool nullTestOwnedBy(CxUiPeer* p, CxUiPeer* owner, bool* owned)
{
    *owned = ((NullPeer*)p)->owner == owner;
    return true;
}

static void nullWindowSetOwner(CxUiPeer* peer, CxUiPeer* owner)
{
    NullPeer* p = (NullPeer*)peer;
    p->ownedBy  = owner;
    if (!p->modal)
        p->owner = owner;
}

static CxUiRect nullFrame(NullPeer* p)
{
    return (CxUiRect){ p->pos.x, p->pos.y, p->size.w, p->size.h };
}

// Screen centering is on the main monitor.
static bool nullWindowMoveNear(CxUiPeer* peer, CxUiPeer* other, uint32 where)
{
    NullPeer* p    = (NullPeer*)peer;
    CxUiRect by    = other ? nullFrame((NullPeer*)other) : nullMonitors[0].workArea;
    p->pos         = _cxuiNearPoint(nullFrame(p), by, where);
    return true;
}

static void nullWindowSetEnabled(CxUiPeer* p, bool enabled)
{
    ((NullPeer*)p)->disabled = !enabled;
}

static void nullWindowSetBackground(CxUiPeer* p, CxUiColor c)
{
    ((NullPeer*)p)->bg = c;
}

static void nullWindowSetMenu(CxUiPeer* p, CxUiPeer* menu)
{
    ((NullPeer*)p)->menu = menu;
}

// ---- menus ----

static CxUiPeer* nullMenuCreate(CxUiMenu* m, bool bar)
{
    unused_noeval(m);
    atomicFetchAdd(int32, &nullPeersAlive, 1, AcqRel);
    NullMenu* nm  = xaAllocStruct(NullMenu, XA_Zero);
    nm->base.kind = CXUI_PEER_Menu;
    nm->bar       = bar;
    return &nm->base;
}

static void nullMenuEntrySet(NullMenuEntry* e, CxUiMenuItem* item)
{
    strDup(&e->title, item->_title);
    strDup(&e->hint, item->_hint);
    e->kind    = item->_kind;
    e->checked = item->_checked;
    e->enabled = !(CxUiWidget(item)->state & CXUI_WS_Inactive);
    e->sub     = item->_submenu ? CxUiWidget(item->_submenu)->peer : NULL;
}

static void nullMenuInsert(CxUiPeer* p, int32 at, CxUiMenuItem* item)
{
    NullMenu* nm = (NullMenu*)p;
    xaResize(&nm->entries, sizeof(NullMenuEntry) * (nm->n + 1));
    memmove(&nm->entries[at + 1], &nm->entries[at], sizeof(NullMenuEntry) * (nm->n - at));
    nm->entries[at] = (NullMenuEntry){ 0 };
    nm->n++;
    nullMenuEntrySet(&nm->entries[at], item);
}

static void nullMenuRemove(CxUiPeer* p, int32 at)
{
    NullMenu* nm = (NullMenu*)p;
    strDestroy(&nm->entries[at].title);
    strDestroy(&nm->entries[at].hint);
    memmove(&nm->entries[at], &nm->entries[at + 1], sizeof(NullMenuEntry) * (nm->n - at - 1));
    nm->n--;
}

static void nullMenuUpdate(CxUiPeer* p, int32 at, CxUiMenuItem* item)
{
    nullMenuEntrySet(&((NullMenu*)p)->entries[at], item);
}

// A menu still holding a submenu when it goes is fine: a platform lets go of it with the menu.
static void nullMenuDestroy(CxUiPeer* p)
{
    NullMenu* nm = (NullMenu*)p;
    for (int32 i = 0; i < nm->n; i++) {
        strDestroy(&nm->entries[i].title);
        strDestroy(&nm->entries[i].hint);
    }
    xaFree(nm->entries);
    xaFree(nm);
}

static int32 nullTestMenuCount(CxUiPeer* p)
{
    return ((NullMenu*)p)->n;
}

static bool nullTestMenuItem(CxUiPeer* p, int32 at, CxUiTestMenuItemInfo* out)
{
    NullMenu* nm = (NullMenu*)p;
    if (at < 0 || at >= nm->n)
        return false;
    NullMenuEntry* e = &nm->entries[at];
    strDup(&out->title, e->title);
    strDup(&out->hint, e->hint);
    out->kind    = e->kind;
    out->checked = e->checked;
    out->enabled = e->enabled;
    out->submenu = e->sub != NULL;
    return true;
}

static bool nullTestMenuChoose(CxUiPeer* p, int32 at)
{
    NullMenu* nm = (NullMenu*)p;
    if (at < 0 || at >= nm->n || !p->w)
        return false;
    // As a platform does: nothing for a grayed item or a separator
    if (!nm->entries[at].enabled || nm->entries[at].kind == CXUI_MENU_Separator || nm->entries[at].sub)
        return true;
    CxUiWidget* item = _cxuiContainerChild(p->w, at);
    if (item)
        _cxuiMenuChosen((CxUiMenuItem*)item);
    return true;
}

static CxUiPeer* nullTestWindowMenu(CxUiPeer* p)
{
    return ((NullPeer*)p)->menu;
}

// ---- panels ----

static CxUiPeer* nullPanelCreate(CxUiWidget* w, CxUiPeer* host)
{
    unused_noeval(host);
    atomicFetchAdd(int32, &nullPeersAlive, 1, AcqRel);
    NullPanel* p        = xaAllocStruct(NullPanel, XA_Zero);
    p->base.kind        = CXUI_PEER_Panel;
    p->info.visible     = true;
    p->scrolls          = objDynCast(CxUiScrollBox, w) != NULL;
    return &p->base;
}

// A scroll box's position stays where the content reaches the end of the view, or less.
static void nullScrollClamp(NullPanel* p)
{
    p->scroll.x = clamp(p->scroll.x, 0, max(p->info.content.w - p->info.bounds.w, 0));
    p->scroll.y = clamp(p->scroll.y, 0, max(p->info.content.h - p->info.bounds.h, 0));
}

static void nullPanelSetBounds(CxUiPeer* p, CxUiRect px)
{
    ((NullPanel*)p)->info.bounds = px;
    nullScrollClamp((NullPanel*)p);
}

static void nullPanelSetTitle(CxUiPeer* peer, strref title, CxUiFont* f, CxUiColor color, int32 indent, int32 gap)
{
    unused_noeval(f);
    unused_noeval(indent);
    unused_noeval(gap);
    NullPanel* p = (NullPanel*)peer;
    strDup(&p->title, title);
    p->info.title      = p->title;
    p->info.titleColor = color;
}

static void nullPanelSetContent(CxUiPeer* p, CxUiSize px)
{
    ((NullPanel*)p)->info.content = px;
    nullScrollClamp((NullPanel*)p);
}

static int32 scrollToShow(int32 pos, int32 view, int32 at, int32 len)
{
    if (at < pos)
        return at;
    if (at + len > pos + view)
        return min(at, at + len - view);
    return pos;
}

static void nullPanelScrollTo(CxUiPeer* peer, CxUiRect px)
{
    NullPanel* p = (NullPanel*)peer;
    p->scroll.x  = scrollToShow(p->scroll.x, p->info.bounds.w, px.x, px.w);
    p->scroll.y  = scrollToShow(p->scroll.y, p->info.bounds.h, px.y, px.h);
    nullScrollClamp(p);
}

static bool nullTestScrollPos(CxUiPeer* p, CxUiPoint* out)
{
    *out = ((NullPanel*)p)->scroll;
    return ((NullPanel*)p)->scrolls;
}

// A notch scrolls 40 pixels.
static bool nullTestScrollWheel(CxUiPeer* p, int32 notches)
{
    ((NullPanel*)p)->scroll.y += notches * 40;
    nullScrollClamp((NullPanel*)p);
    return true;
}

static void nullPanelSetColors(CxUiPeer* p, CxUiColor bg, CxUiColor border, int32 borderPx)
{
    CxUiTestPanelInfo* i = &((NullPanel*)p)->info;
    i->bg                = bg;
    i->border            = border;
    i->borderPx          = borderPx;
}

static void nullWindowSetMinSize(CxUiPeer* p, CxUiSize dips)
{
    ((NullPeer*)p)->minSize = dips;
}

static bool nullWindowBeginDrag(CxUiPeer* p)
{
    ((NullPeer*)p)->drags++;
    return true;
}

static void nullWindowSetResizable(CxUiPeer* p, bool resizable)
{
    ((NullPeer*)p)->resizable = resizable;
}

// ---- canvases ----

// A canvas peer is its own draw target: each paint pass records what was drawn.
typedef struct NullCanvas {
    CxUiCanvasPeer cp;
    CxUiRect bounds;
    bool hidden;
    bool captured;
    uint32 cursor;
    CxUiCursor* cursorImage;   // not a reference: the core holds it while it is set
    CxUiPoint warpedTo;
    int32 warps;
    flags_t textInput;
    CxUiRect caret;
    flags_t keyCapture;
    uint32 lockMode;
    CxUiPoint lockAt;
    CxUiRect lastDirty;
    int32 paints;
    bool paintQueued;
    CxUiTestDrawOp* ops;
    int32 nops;
    int32 capops;
    bool marked;   // a render view's, by cxuiTestSurfaceMark()
} NullCanvas;

static void record(NullCanvas* nc, CxUiTestDrawOp op)
{
    if (nc->nops == nc->capops) {
        nc->capops = nc->capops ? nc->capops * 2 : 32;
        xaResize(&nc->ops, nc->capops * sizeof(CxUiTestDrawOp));
    }
    nc->ops[nc->nops++] = op;
}

static void* nullTargetCreate(CxUiCanvasPeer* p)
{
    return p;
}

static void nullTargetResize(void* t, CxUiSize px)
{
    (void)px;
    relAssertMsg(!((NullCanvas*)t)->cp.painting, "back buffer resized during a paint pass");
}

static void clearOps(NullCanvas* nc)
{
    for (int32 i = 0; i < nc->nops; i++) {
        string s = (string)nc->ops[i].text;
        strDestroy(&s);
    }
    nc->nops = 0;
}

static void nullTargetDestroy(void* t)
{
    clearOps(t);
    xaDestroy(&((NullCanvas*)t)->ops);
}

static void nullBegin(void* t, CxUiRect dirty)
{
    NullCanvas* nc = t;
    clearOps(nc);
    nc->lastDirty  = dirty;
    nc->paints++;
}

static void nullEnd(void* t)
{
    unused_noeval(t);
}

static void nullFill(void* t, CxUiRect r, CxUiColor c)
{
    record(t, (CxUiTestDrawOp){ .kind = CXUI_TESTOP_Fill, .r = r, .c = c });
}

static void nullLine(void* t, CxUiPoint a, CxUiPoint b, int32 width, CxUiColor c)
{
    record(t, (CxUiTestDrawOp){ .kind = CXUI_TESTOP_Line, .a = a, .b = b, .width = width, .c = c });
}

static void nullClipSet(void* t, CxUiRect r)
{
    record(t, (CxUiTestDrawOp){ .kind = CXUI_TESTOP_Clip, .r = r });
}

// Fonts with fixed proportions of the em size, the same on every machine: a 10-point font at
// scale 1 has 8x16 cells, ascent 12 and an average character width of 7.
typedef struct NullFont {
    float32 px;
    CxUiFontMetrics m;
} NullFont;

static int32 roundPx(float32 v)
{
    return (int32)floor(v + 0.5f);
}

static void* nullFontCreate(strref faces, float32 px, float32 scale, uint32 flags)
{
    unused_noeval(faces);
    unused_noeval(flags);
    NullFont* f = xaAllocStruct(NullFont, XA_Zero);
    f->px       = px > 0 ? px : 10 * scale * 96.0f / 72.0f;
    f->m.height = max(roundPx(f->px * 1.2f), 1);
    f->m.ascent = roundPx(f->px * 0.9f);
    f->m.descent = f->m.height - f->m.ascent;
    f->m.avgCharW = max(roundPx(f->px * 0.5f), 1);
    f->m.cellW    = max(roundPx(f->px * 0.6f), 1);
    f->m.cellH    = f->m.height;
    return f;
}

static void nullFontDestroy(void* f)
{
    xaFree(f);
}

static void nullFontMetrics(void* f, CxUiFontMetrics* out)
{
    *out = ((NullFont*)f)->m;
}

static int32 advanceOf(void* f, strref s)
{
    return _cxuiCodepoints(s) * ((NullFont*)f)->m.cellW;
}

static int32 nullFontAdvance(void* f, strref s)
{
    nullMeasures++;
    return advanceOf(f, s);
}

static int32 nullText(void* t, void* f, CxUiPoint at, strref s, CxUiColor c)
{
    NullFont* nf      = f;
    int32 adv         = advanceOf(f, s);
    CxUiTestDrawOp op = { .kind = CXUI_TESTOP_Text, .a = at, .r = { at.x, at.y, adv, nf->m.height }, .c = c,
                          .font = roundPx(nf->px) };
    strDup((string*)&op.text, s);
    record(t, op);
    return adv;
}

static void nullTextCells(void* t, void* f, CxUiPoint at, strref s, const int32* cellw, CxUiColor c)
{
    int32 n = _cxuiCodepoints(s), total = 0;
    for (int32 i = 0; i < n; i++)
        total += cellw[i];
    CxUiTestDrawOp op = { .kind = CXUI_TESTOP_Cells, .a = at, .width = cellw[0], .b = { total, n }, .c = c,
                          .font = roundPx(((NullFont*)f)->px) };
    strDup((string*)&op.text, s);
    record(t, op);
}

static void* nullImageUpload(const void* px, int32 w, int32 h)
{
    unused_noeval(px);
    CxUiSize* sz = xaAllocStruct(CxUiSize);
    *sz          = (CxUiSize){ w, h };
    return sz;
}

static void nullImageFree(void* img)
{
    xaFree(img);
}

static void nullImage(void* t, void* img, CxUiRect src, CxUiRect dst)
{
    CxUiSize* sz = img;
    record(t, (CxUiTestDrawOp){ .kind = CXUI_TESTOP_Image, .r = dst, .src = src, .b = { sz->w, sz->h } });
}

const CxUiDrawOps _cxuiNullDrawOps = {
    .name          = _S"null",
    .targetCreate  = nullTargetCreate,
    .targetResize  = nullTargetResize,
    .targetDestroy = nullTargetDestroy,
    .begin         = nullBegin,
    .end           = nullEnd,
    .fill          = nullFill,
    .line          = nullLine,
    .clipSet       = nullClipSet,
    .fontCreate    = nullFontCreate,
    .fontDestroy   = nullFontDestroy,
    .fontMetrics   = nullFontMetrics,
    .fontAdvance   = nullFontAdvance,
    .text          = nullText,
    .textCells     = nullTextCells,
    .imageUpload   = nullImageUpload,
    .imageFree     = nullImageFree,
    .image         = nullImage,
};

// A render view has no target; its surface is there as soon as it is.
static CxUiPeer* nullCanvasCreate(CxUiCanvas* c, CxUiPeer* parent)
{
    unused_noeval(parent);
    atomicFetchAdd(int32, &nullPeersAlive, 1, AcqRel);
    NullCanvas* nc   = xaAllocStruct(NullCanvas, XA_Zero);
    nc->cp.base.kind = CXUI_PEER_Canvas;
    if (objDynCast(CxUiRenderView, c))
        return &nc->cp.base;
    nc->cp.ops    = &_cxuiNullDrawOps;
    nc->cp.target = _cxuiNullDrawOps.targetCreate(&nc->cp);
    return &nc->cp.base;
}

static void nullCanvasDestroy(CxUiPeer* p)
{
    NullCanvas* nc = (NullCanvas*)p;
    if (nullFocus == p)
        nullFocus = NULL;
    if (nc->cp.ops)
        nc->cp.ops->targetDestroy(nc->cp.target);
    xaFree(nc);
}

static void nullCanvasSetBounds(CxUiPeer* p, CxUiRect px)
{
    NullCanvas* nc = (NullCanvas*)p;
    nc->bounds     = px;
    if (nc->cp.ops)
        nc->cp.ops->targetResize(nc->cp.target, (CxUiSize){ px.w, px.h });
}

static bool nullSurfaceGet(CxUiPeer* p, CxUiNativeSurface* out)
{
    *out = (CxUiNativeSurface){ .kind = CXUI_SURFACE_None, .native = p };
    return true;
}

// Nothing draws over a render view here, so a mark stays.
static bool nullTestSurfaceInfo(CxUiPeer* p, CxUiTestSurfaceInfo* out)
{
    NullCanvas* nc = (NullCanvas*)p;
    CxUiPoint o    = { 0 };
    for (CxUiWidget* a = p->w ? p->w->parent : NULL; a; a = a->parent) {
        if (a->peer && a->peer->kind == CXUI_PEER_Panel) {
            o = (CxUiPoint){ a->_bounds.x, a->_bounds.y };
            break;
        }
    }
    *out = (CxUiTestSurfaceInfo){ .bounds  = { nc->bounds.x + o.x, nc->bounds.y + o.y, nc->bounds.w, nc->bounds.h },
                                  .shown   = !nc->hidden,
                                  .canMark = true,
                                  .marked  = nc->marked };
    return true;
}

static bool nullTestSurfaceMark(CxUiPeer* p)
{
    ((NullCanvas*)p)->marked = true;
    return true;
}

static bool nullPaintTask(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiCanvas* cv = stvlAtObj(cvars, 0, CxUiCanvas);
    if (!_cxuiLive(CxUiWidget(cv)) || !cv->peer)
        return true;
    NullCanvas* nc  = (NullCanvas*)cv->peer;
    nc->paintQueued = false;
    _cxuiNativeEnter();
    _cxuiCanvasPaint(&nc->cp, NULL);
    _cxuiNativeLeave();
    return true;
}

// Paints from the UI queue, as a platform paints at its next chance.
static void nullCanvasInvalidate(CxUiPeer* p, CxUiRect px)
{
    (void)px;
    NullCanvas* nc = (NullCanvas*)p;
    if (nc->paintQueued || !p->w)
        return;
    nc->paintQueued = true;
    cxuiPost(closureCreate(nullPaintTask, stvar(object, p->w)));
}

// ---- text fields ----

// A text field's peer edits its own copy of the text, as a native control does, and reports the
// user's edits to the core.
typedef struct NullText {
    CxUiPeer base;
    CxUiTestTextInfo info;   // text and cue are owned
    bool setting;            // replacing the text from code: no report
} NullText;

static CxUiPeer* nullTextCreate(CxUiTextField* t, CxUiPeer* host, bool framed)
{
    unused_noeval(host);
    atomicFetchAdd(int32, &nullPeersAlive, 1, AcqRel);
    NullText* nt       = xaAllocStruct(NullText, XA_Zero);
    CxUiTextArea* area = objDynCast(CxUiTextArea, t);
    nt->base.kind      = CXUI_PEER_Text;
    nt->info.multiline = area != NULL;
    nt->info.wrap      = area && cxuiTextAreaWrap(area);
    nt->info.framed    = framed;
    nt->info.visible   = true;
    nt->info.enabled   = true;
    nt->info.focusable = true;
    return &nt->base;
}

static void nullTextDestroy(CxUiPeer* p)
{
    NullText* nt = (NullText*)p;
    if (nullFocus == p)
        nullFocus = NULL;
    strDestroy(&nt->info.text);
    strDestroy(&nt->info.cue);
    xaFree(nt);
}

static void nullTextSetBounds(CxUiPeer* p, CxUiRect px)
{
    ((NullText*)p)->info.bounds = px;
}

// Realized fields measure as a test says, as native controls measure differently from the
// estimate they were first laid out with.
static CxUiSize nullTextMeasure(CxUiTextField* t, CxUiFont* f, int32 columns, bool framed)
{
    unused_noeval(f);
    unused_noeval(columns);
    unused_noeval(framed);
    return CxUiWidget(t)->peer ? nullTextNative : (CxUiSize){ 0 };
}

void cxuiTestSetTextNative(int32 w, int32 h)
{
    nullTextNative = (CxUiSize){ w, h };
}

static void nullTextSetText(CxUiPeer* p, strref text)
{
    NullText* nt = (NullText*)p;
    strDup(&nt->info.text, text);
    nt->info.atEnd = false;
    int32 n          = _cxuiCodepoints(text);
    nt->info.selStart = min(nt->info.selStart, n);
    nt->info.selEnd   = min(nt->info.selEnd, n);
    nt->info.caret    = min(nt->info.caret, n);
}

static void nullTextAppend(CxUiPeer* p, strref text)
{
    NullText* nt = (NullText*)p;
    strAppend(&nt->info.text, text);
    nt->info.atEnd = false;
}

static void nullTextScrollToEnd(CxUiPeer* p)
{
    ((NullText*)p)->info.atEnd = true;
}

static void nullTextGetSelection(CxUiPeer* p, int32* start, int32* end, int32* caret)
{
    NullText* nt = (NullText*)p;
    *start       = nt->info.selStart;
    *end         = nt->info.selEnd;
    *caret       = nt->info.caret;
}

static void nullTextSetSelection(CxUiPeer* p, int32 start, int32 end)
{
    NullText* nt      = (NullText*)p;
    int32 n           = _cxuiCodepoints(nt->info.text);
    start             = clamp(start, 0, n);
    end               = clamp(end, 0, n);
    nt->info.selStart = min(start, end);
    nt->info.selEnd   = max(start, end);
    nt->info.caret    = end;
}

static void nullTextSetCue(CxUiPeer* p, strref cue)
{
    strDup(&((NullText*)p)->info.cue, cue);
}

static void nullTextSetMaxLength(CxUiPeer* p, int32 maxLength)
{
    ((NullText*)p)->info.maxLength = maxLength;
}

static void nullTextSetFont(CxUiPeer* p, CxUiFont* f)
{
    ((NullText*)p)->info.fontPx = roundPx(((NullFont*)_cxuiFontNative(f, &_cxuiNullDrawOps))->px);
}

static void nullTextSetState(CxUiPeer* p, bool readOnly, bool enabled, bool focusable)
{
    NullText* nt       = (NullText*)p;
    nt->info.readOnly  = readOnly;
    nt->info.enabled   = enabled;
    nt->info.focusable = focusable;
}

static void nullTextSetColors(CxUiPeer* p, const CxUiTextColors* c)
{
    NullText* nt       = (NullText*)p;
    nt->info.hasColors = c != NULL;
    nt->info.textColor = c ? c->text : (CxUiColor){ 0 };
    nt->info.bg        = c ? c->bg : (CxUiColor){ 0 };
    nt->info.cueColor  = c ? c->cue : (CxUiColor){ 0 };
}

static bool nullTestTextInfo(CxUiPeer* p, CxUiTestTextInfo* out)
{
    NullText* nt = (NullText*)p;
    *out         = nt->info;
    out->text    = NULL;
    out->cue     = NULL;
    strDup(&out->text, nt->info.text);
    strDup(&out->cue, nt->info.cue);
    out->focused = nullFocus == p;
    return true;
}

// The selection is replaced by as much of s as the limit leaves room for; read-only and
// disabled fields refuse it, as a native one would.
static bool nullTestType(CxUiPeer* p, strref s)
{
    NullText* nt = (NullText*)p;
    if (nt->info.readOnly || !nt->info.enabled)
        return true;
    string before = 0, after = 0, ins = 0, all = 0;
    int32 n     = _cxuiCodepoints(nt->info.text);
    int32 room  = nt->info.maxLength > 0 ? max(nt->info.maxLength - (n - (nt->info.selEnd - nt->info.selStart)), 0)
                                         : INT32_MAX;
    int32 count = min(_cxuiCodepoints(s), room);
    strSubStr(&ins, s, 0, _cxuiByteOffset(s, count));
    int32 bs = _cxuiByteOffset(nt->info.text, nt->info.selStart);
    int32 be = _cxuiByteOffset(nt->info.text, nt->info.selEnd);
    strSubStr(&before, nt->info.text, 0, bs);
    strSubStr(&after, nt->info.text, be, strEnd);
    strNConcat(&all, before, ins, after);
    strDup(&nt->info.text, all);
    nt->info.selStart = nt->info.selEnd = nt->info.caret = nt->info.selStart + count;
    strDestroy(&before);
    strDestroy(&after);
    strDestroy(&ins);
    _cxuiTextChanged(p, all);
    strDestroy(&all);
    return true;
}

// ---- labels and buttons ----

// What a label or button was given, as it was given
typedef struct NullControl {
    CxUiPeer base;
    CxUiTestControlInfo info;   // text is owned
    sa_string items;            // a list's or dropdown's
    sa_uint8 marks;             // a list's where any number can be selected
    bool multi;
} NullControl;

static NullControl* nullControlNew(CxUiPeerKind kind)
{
    NullControl* c   = xaAllocStruct(NullControl, XA_Zero);
    c->base.kind     = kind;
    c->info.enabled  = true;
    c->info.visible  = true;
    atomicFetchAdd(int32, &nullPeersAlive, 1, AcqRel);
    return c;
}

static void nullControlDestroy(CxUiPeer* p)
{
    if (nullFocus == p)
        nullFocus = NULL;
    strDestroy(&((NullControl*)p)->info.text);
    saDestroy(&((NullControl*)p)->items);
    saDestroy(&((NullControl*)p)->marks);
    xaFree(p);
}

static CxUiPeer* nullLabelCreate(CxUiLabel* l, CxUiPeer* host)
{
    unused_noeval(l);
    unused_noeval(host);
    return &nullControlNew(CXUI_PEER_Label)->base;
}

static void nullControlSetText(CxUiPeer* p, strref text)
{
    strDup(&((NullControl*)p)->info.text, text);
}

static void nullLabelSetStyle(CxUiPeer* p, uint32 align, uint32 ellipsis, bool wrap)
{
    ((NullControl*)p)->info.align    = align;
    ((NullControl*)p)->info.ellipsis = ellipsis;
    ((NullControl*)p)->info.wrap     = wrap;
}

static void nullLabelSetColor(CxUiPeer* p, CxUiColor c)
{
    ((NullControl*)p)->info.color = c;
}

static CxUiPeer* nullButtonCreate(CxUiButton* b, CxUiPeer* host)
{
    unused_noeval(b);
    unused_noeval(host);
    return &nullControlNew(CXUI_PEER_Button)->base;
}

static CxUiPeer* nullCheckCreate(CxUiWidget* w, CxUiPeer* host, bool radio)
{
    unused_noeval(w);
    unused_noeval(host);
    return &nullControlNew(radio ? CXUI_PEER_Radio : CXUI_PEER_Check)->base;
}

static void nullCheckSetState(CxUiPeer* p, uint32 state)
{
    ((NullControl*)p)->info.checkState = state;
}

static CxUiPeer* nullListCreate(CxUiWidget* w, CxUiPeer* host, bool dropdown)
{
    unused_noeval(w);
    unused_noeval(host);
    NullControl* c = nullControlNew(dropdown ? CXUI_PEER_Dropdown : CXUI_PEER_List);
    saInit(&c->items, string, 8);
    saInit(&c->marks, uint8, 8);
    CxUiList* l   = dropdown ? NULL : objDynCast(CxUiList, w);
    c->multi      = l && l->_select == CXUI_SELECT_Multi;
    c->info.value = -1;
    return &c->base;
}

static void nullListSplice(CxUiPeer* p, int32 at, int32 removed, const string* items, int32 added)
{
    NullControl* c = (NullControl*)p;
    for (int32 i = 0; i < removed; i++) {
        saRemove(&c->items, at);
        if (c->multi)
            saRemove(&c->marks, at);
    }
    for (int32 i = 0; i < added; i++) {
        saInsert(&c->items, at + i, strref, items[i]);
        if (c->multi)
            saInsert(&c->marks, at + i, uint8, 0);
    }
    // A native list clears or moves its selection, which the core sets again, and goes back to the
    // top when every item goes.
    c->info.value = -1;
    c->info.top   = saSize(c->items) == added ? 0 : min(c->info.top, max(saSize(c->items) - 1, 0));
}

static void nullListSetMarks(CxUiPeer* p, const uint8* marks, int32 count)
{
    NullControl* c = (NullControl*)p;
    for (int32 i = 0; i < saSize(c->marks); i++)
        c->marks.a[i] = i < count && marks[i];
}

static void nullListGetMarks(CxUiPeer* p, uint8* marks, int32 count)
{
    NullControl* c = (NullControl*)p;
    for (int32 i = 0; i < count; i++)
        marks[i] = i < saSize(c->marks) && c->marks.a[i];
}

static int32 nullListTopItem(CxUiPeer* p)
{
    return ((NullControl*)p)->info.top;
}

static void nullListSetTopItem(CxUiPeer* p, int32 index)
{
    NullControl* c = (NullControl*)p;
    c->info.top    = clamp(index, 0, max(saSize(c->items) - 1, 0));
}

// A Ctrl-click: the item changes, the others stay
static bool nullTestToggle(CxUiPeer* p, int32 index)
{
    NullControl* c = (NullControl*)p;
    if (p->kind != CXUI_PEER_List || !c->multi)
        return false;
    if (c->info.enabled && c->info.visible && index >= 0 && index < saSize(c->marks)) {
        c->marks.a[index] = !c->marks.a[index];
        _cxuiListMarksChanged(p);
    }
    return true;
}

static void nullTabsSetValue(CxUiPeer* p, int32 value)
{
    ((NullControl*)p)->info.value = value;
}

static void nullListSetValue(CxUiPeer* p, int32 value)
{
    ((NullControl*)p)->info.value = value;
}

static void nullListSetColors(CxUiPeer* p, CxUiColor bg, CxUiColor text)
{
    (void)bg;
    ((NullControl*)p)->info.color = text;
}

// ---- tabs ----

static CxUiPeer* nullTabsCreate(CxUiTabs* t, CxUiPeer* host)
{
    unused_noeval(t);
    unused_noeval(host);
    NullControl* c = nullControlNew(CXUI_PEER_Tabs);
    saInit(&c->items, string, 8);
    c->info.value = -1;
    return &c->base;
}

static void nullTabsInsert(CxUiPeer* p, int32 at, strref title)
{
    NullControl* c = (NullControl*)p;
    saInsert(&c->items, at, strref, title ? title : _S"");
    // A native tab control keeps the same tab picked, or picks the first; the core sets it again.
    if (c->info.value >= at)
        c->info.value++;
    else if (c->info.value < 0)
        c->info.value = 0;
}

static void nullTabsRemove(CxUiPeer* p, int32 at)
{
    NullControl* c = (NullControl*)p;
    saRemove(&c->items, at);
    c->info.value = -1;
}

static void nullTabsSetTitle(CxUiPeer* p, int32 at, strref title)
{
    NullControl* c = (NullControl*)p;
    strDup(&c->items.a[at], title);
}

static void nullTabsSetPage(CxUiPeer* p, CxUiRect px)
{
    ((NullControl*)p)->info.page = px;
}

static void nullTabsSetColors(CxUiPeer* p, const CxUiTabsColors* colors)
{
    ((NullControl*)p)->info.bg    = colors->page;
    ((NullControl*)p)->info.color = colors->around;
}

// A disabled or hidden list doesn't take the user's choice, as a native one wouldn't.
static bool nullTestChoose(CxUiPeer* p, int32 index, bool activate)
{
    NullControl* c = (NullControl*)p;
    if (p->kind == CXUI_PEER_Tabs) {
        if (c->info.enabled && c->info.visible && index >= 0 && index < saSize(c->items) && index != c->info.value) {
            c->info.value = index;
            _cxuiTabsPicked(p, index);
        }
        return true;
    }
    if (p->kind != CXUI_PEER_List && p->kind != CXUI_PEER_Dropdown)
        return false;
    if (!c->info.enabled || !c->info.visible || index >= saSize(c->items))
        return true;
    if (c->multi) {
        for (int32 i = 0; i < saSize(c->marks); i++)
            c->marks.a[i] = i == index;
        _cxuiListMarksChanged(p);
        if (activate)
            _cxuiListActivated(p, index);
        return true;
    }
    c->info.value = index;
    _cxuiListChosen(p, index);
    if (activate && p->kind == CXUI_PEER_List)
        _cxuiListActivated(p, index);
    return true;
}

static CxUiPeer* nullSliderCreate(CxUiWidget* w, CxUiPeer* host, bool vertical)
{
    unused_noeval(w);
    unused_noeval(host);
    NullControl* c   = nullControlNew(CXUI_PEER_Slider);
    c->info.vertical = vertical;
    return &c->base;
}

static void nullSliderSetRange(CxUiPeer* p, int32 range, int32 pageStep, int32 tickEvery)
{
    NullControl* c    = (NullControl*)p;
    c->info.range     = range;
    c->info.tickEvery = tickEvery;
    c->info.pos       = min(c->info.pos, range);
    unused_noeval(pageStep);
}

static void nullSliderSetPos(CxUiPeer* p, int32 pos)
{
    ((NullControl*)p)->info.pos = pos;
}

static CxUiPeer* nullProgressCreate(CxUiWidget* w, CxUiPeer* host)
{
    unused_noeval(w);
    unused_noeval(host);
    NullControl* c = nullControlNew(CXUI_PEER_Progress);
    c->info.range  = 10000;
    return &c->base;
}

static void nullProgressSet(CxUiPeer* p, float64 fraction, bool indeterminate)
{
    NullControl* c        = (NullControl*)p;
    c->info.pos           = (int32)floor(fraction * 10000 + 0.5);
    c->info.indeterminate = indeterminate;
}

// A disabled or hidden slider doesn't move, as a native one wouldn't.
static bool nullTestSlide(CxUiPeer* p, int32 pos, bool release)
{
    NullControl* c = (NullControl*)p;
    if (p->kind != CXUI_PEER_Slider)
        return false;
    if (!c->info.enabled || !c->info.visible)
        return true;
    c->info.pos = clamp(pos, 0, c->info.range);
    _cxuiSliderMoved(p, c->info.pos, release);
    return true;
}

static void nullButtonSetDefault(CxUiPeer* p, bool isDefault)
{
    ((NullControl*)p)->info.isDefault = isDefault;
}

static void nullControlSetBounds(CxUiPeer* p, CxUiRect px)
{
    ((NullControl*)p)->info.bounds = px;
}

static void nullControlSetFont(CxUiPeer* p, CxUiFont* f)
{
    ((NullControl*)p)->info.fontPx = roundPx(((NullFont*)_cxuiFontNative(f, &_cxuiNullDrawOps))->px);
}

// Labels are never disabled natively; the other controls are.
static void nullPeerSetEnabled(CxUiPeer* p, bool enabled)
{
    if (_cxuiPeerIsControl(p->kind) && p->kind != CXUI_PEER_Label)
        ((NullControl*)p)->info.enabled = enabled;
}

static bool nullTestControlInfo(CxUiPeer* p, CxUiTestControlInfo* out)
{
    NullControl* c = (NullControl*)p;
    *out           = c->info;
    out->text      = NULL;
    out->items     = NULL;
    out->selection = NULL;
    strDup(&out->text, c->info.text);
    for (int32 i = 0; i < saSize(c->items); i++) {
        strAppend(&out->items, c->items.a[i]);
        strAppend(&out->items, _S"\n");
    }
    string at = 0;
    for (int32 i = 0; i < saSize(c->marks); i++) {
        if (!c->marks.a[i])
            continue;
        strFormat(&at, _S"${int} ", stvar(int32, i));
        strAppend(&out->selection, at);
    }
    strDestroy(&at);
    out->focused = nullFocus == p;
    return true;
}

// A disabled or hidden control doesn't take the click, as a native one wouldn't.
static bool nullTestClick(CxUiPeer* p)
{
    NullControl* c = (NullControl*)p;
    if (p->kind != CXUI_PEER_Button && p->kind != CXUI_PEER_Check && p->kind != CXUI_PEER_Radio)
        return false;
    if (c->info.enabled && c->info.visible)
        _cxuiButtonClicked(p);
    return true;
}

static void nullPeerSetVisible(CxUiPeer* p, bool visible)
{
    if (p->kind == CXUI_PEER_Canvas)
        ((NullCanvas*)p)->hidden = !visible;
    else if (p->kind == CXUI_PEER_Panel)
        ((NullPanel*)p)->info.visible = visible;
    else if (p->kind == CXUI_PEER_Text)
        ((NullText*)p)->info.visible = visible;
    else if (_cxuiPeerIsControl(p->kind))
        ((NullControl*)p)->info.visible = visible;
}

static void nullCanvasPaintNow(CxUiPeer* p)
{
    _cxuiCanvasPaint(&((NullCanvas*)p)->cp, NULL);
}

static void nullCanvasCapture(CxUiPeer* p, bool on)
{
    ((NullCanvas*)p)->captured = on;
}

// Focus moves at once, with the events a platform would send.
static void nullPeerFocus(CxUiPeer* p)
{
    if (nullFocus == p)
        return;
    CxUiPeer* old = nullFocus;
    nullFocus     = p;
    if (old)
        _cxuiFocusChanged(old, false);
    if (nullFocus == p)
        _cxuiFocusChanged(p, true);
}

static void nullCanvasSetCursor(CxUiPeer* p, uint32 cursor)
{
    ((NullCanvas*)p)->cursor = cursor;
}

static void nullCanvasSetCursorImage(CxUiPeer* p, CxUiCursor* cursor)
{
    ((NullCanvas*)p)->cursorImage = cursor;
}

static bool nullCanvasWarpPointer(CxUiPeer* p, CxUiPoint px)
{
    ((NullCanvas*)p)->warpedTo = px;
    ((NullCanvas*)p)->warps++;
    return true;
}

static bool nullCanvasSetTextInput(CxUiPeer* p, flags_t flags)
{
    ((NullCanvas*)p)->textInput = flags;
    return true;
}

static void nullCanvasSetTextCaret(CxUiPeer* p, CxUiRect px)
{
    ((NullCanvas*)p)->caret = px;
}

static bool nullCanvasSetKeyCapture(CxUiPeer* p, flags_t flags)
{
    ((NullCanvas*)p)->keyCapture = flags;
    return true;
}

static uint32 nullCanvasPointerLock(CxUiPeer* p, uint32 mode, CxUiPoint px)
{
    NullCanvas* nc = (NullCanvas*)p;
    nc->lockMode   = min(mode, nullLockMax);
    nc->lockAt     = px;
    return nc->lockMode;
}

static uint32 nullKeyLocksGet(void)
{
    return nullKeyLocks;
}

static void nullDoubleClick(int64* time, int32* distance)
{
    *time     = nullDoubleTime;
    *distance = nullDoubleDistance;
}

static float32 nullSystemScaleGet(void)
{
    return nullSystemScale;
}

// ---- desktop ----

static void nullMonitorsGet(sa_CxUiMonitor* out)
{
    for (int32 i = 0; i < nullMonitorCount; i++)
        saPush(out, CxUiMonitor, nullMonitors[i]);
}

static bool nullWindowSetTopmost(CxUiPeer* p, bool on)
{
    ((NullPeer*)p)->topmost = on;
    return true;
}

static bool nullTestTopmost(CxUiPeer* p, bool* topmost)
{
    *topmost = ((NullPeer*)p)->topmost;
    return true;
}

// The monitor that holds the middle of the window, which is placed in DIPs
static int32 nullWindowMonitor(CxUiPeer* peer)
{
    NullPeer* p = (NullPeer*)peer;
    int32 x = p->pos.x + p->size.w / 2, y = p->pos.y + p->size.h / 2;
    if (p->fullscreen && p->fsMonitor.w > 0) {
        x = p->fsMonitor.x + p->fsMonitor.w / 2;
        y = p->fsMonitor.y + p->fsMonitor.h / 2;
    }
    for (int32 i = 0; i < nullMonitorCount; i++) {
        CxUiRect b = nullMonitors[i].bounds;
        if (x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h)
            return i;
    }
    return -1;
}

static bool nullClipboardSetText(strref text)
{
    strDup(&nullClipboard, text);
    nullClipboardHasText = true;
    return true;
}

static bool nullClipboardGetText(string* out)
{
    if (!nullClipboardHasText)
        return false;
    strDup(out, nullClipboard);
    return true;
}

static bool nullKeepAwake(bool on, strref reason)
{
    if (on)
        strDup(&nullAwakeReason, reason);
    else
        strDestroy(&nullAwakeReason);
    return true;
}

// ---- message boxes ----

// Open boxes, innermost first; each waits until a test answers it.
typedef struct NullBox {
    atomic(bool) done;
    int32 result;
    int32 nbuttons;
    struct NullBox* outer;
} NullBox;

static NullBox* nullBoxes;   // UI thread only

static int32 nullMessageBox(const CxUiMessageBoxDesc* desc, CxUiPeer* parent)
{
    unused_noeval(parent);
    // The core builds custom boxes, as on a platform without its own.
    if (desc->buttons == CXUI_MB_Custom)
        return CXUI_MB_Build;
    static const int32 nbuttons[] = { 1, 2, 2, 3 };
    NullBox box = { .nbuttons = desc->buttons < 4 ? nbuttons[desc->buttons] : 1, .outer = nullBoxes };
    nullBoxes   = &box;
    nullLoop(&box.done);
    nullBoxes = box.outer;
    return box.result;
}

static bool nullTestAnswerMessageBox(int32 button)
{
    NullBox* box = nullBoxes;
    if (!box || button < 1 || button > box->nbuttons || atomicLoad(bool, &box->done, Acquire))
        return false;
    box->result = button;
    atomicStore(bool, &box->done, true, Release);
    return true;
}

static bool nullRunning(void);

typedef struct NullFileBox {
    atomic(bool) done;
    bool chosen;
    string path;
    CxUiTestFileDialogInfo info;
    struct NullFileBox* outer;
} NullFileBox;

static NullFileBox* nullFileBoxes;   // UI thread only

static bool nullFileDialog(const CxUiFileDialogDesc* desc, CxUiPeer* parent, string* out)
{
    unused_noeval(parent);
    NullFileBox box = { .outer = nullFileBoxes };
    box.info        = (CxUiTestFileDialogInfo){ .mode = desc->mode, .nfilters = desc->nfilters,
                                                .filterIndex = desc->filterIndex, .flags = desc->flags };
    strDup(&box.info.title, desc->title);
    strDup(&box.info.initialDir, desc->initialDir);
    strDup(&box.info.initialFile, desc->initialFile);
    nullFileBoxes = &box;
    nullLoop(&box.done);
    nullFileBoxes = box.outer;
    if (box.chosen)
        strDup(out, box.path);
    strDestroy(&box.path);
    cxuiTestFileDialogInfoDestroy(&box.info);
    return box.chosen;
}

static bool nullTestAnswerFileDialog(strref path)
{
    NullFileBox* box = nullFileBoxes;
    if (!box || atomicLoad(bool, &box->done, Acquire))
        return false;
    box->chosen = path != NULL;
    strDup(&box->path, path);
    atomicStore(bool, &box->done, true, Release);
    return true;
}

typedef struct NullColorBox {
    atomic(bool) done;
    bool chosen;
    CxUiColor color;
    CxUiColor start;
    string title;
    struct NullColorBox* outer;
} NullColorBox;

static NullColorBox* nullColorBoxes;   // UI thread only

static bool nullColorDialog(const CxUiColorDialogDesc* desc, CxUiPeer* parent, CxUiColor* color)
{
    unused_noeval(parent);
    NullColorBox box = { .start = *color, .outer = nullColorBoxes };
    strDup(&box.title, desc->title);
    nullColorBoxes = &box;
    nullLoop(&box.done);
    nullColorBoxes = box.outer;
    strDestroy(&box.title);
    // The platforms' dialogs know nothing of alpha.
    if (box.chosen)
        *color = (CxUiColor){ .r = box.color.r, .g = box.color.g, .b = box.color.b, .a = 255 };
    return box.chosen;
}

static bool nullTestAnswerColorDialog(const CxUiColor* color)
{
    NullColorBox* box = nullColorBoxes;
    if (!box || atomicLoad(bool, &box->done, Acquire))
        return false;
    box->chosen = color != NULL;
    if (color)
        box->color = *color;
    atomicStore(bool, &box->done, true, Release);
    return true;
}

_Use_decl_annotations_
bool cxuiTestColorDialogInfo(CxUiColor* start, string* title)
{
    *start            = (CxUiColor){ 0 };
    NullColorBox* box = nullRunning() ? nullColorBoxes : NULL;
    if (!box)
        return false;
    *start = box->start;
    strDup(title, box->title);
    return true;
}

_Use_decl_annotations_
bool cxuiTestFileDialogInfo(CxUiTestFileDialogInfo* out)
{
    *out = (CxUiTestFileDialogInfo){ 0 };
    NullFileBox* box = nullRunning() ? nullFileBoxes : NULL;
    if (!box)
        return false;
    *out = box->info;
    out->title = out->initialDir = out->initialFile = NULL;
    strDup(&out->title, box->info.title);
    strDup(&out->initialDir, box->info.initialDir);
    strDup(&out->initialFile, box->info.initialFile);
    return true;
}

_Use_decl_annotations_
void cxuiTestFileDialogInfoDestroy(CxUiTestFileDialogInfo* info)
{
    strDestroy(&info->title);
    strDestroy(&info->initialDir);
    strDestroy(&info->initialFile);
}

static CxUiThemeMode nullSystemThemeGet(void)
{
    return nullSystemTheme;
}

static bool nullTestFrameDark(CxUiPeer* p, bool* dark)
{
    *dark = ((NullPeer*)p)->dark;
    return true;
}

static void nullPeerRaise(CxUiPeer* p)
{
    if (p->kind == CXUI_PEER_Panel)
        ((NullPanel*)p)->info.raised = true;
}

static void nullWindowFocusCues(CxUiPeer* win)
{
    ((NullPeer*)win)->focusCues = true;
}

static bool nullTestFocusCues(CxUiPeer* p, bool hide, bool* shown)
{
    if (hide)
        ((NullPeer*)p)->focusCues = false;
    *shown = ((NullPeer*)p)->focusCues;
    return true;
}

static bool nullTestBorderless(CxUiPeer* p, bool* borderless)
{
    *borderless = ((NullPeer*)p)->borderless;
    return true;
}

// ---- test hooks ----

static bool nullRunning(void)
{
    return _cxui.backend == &_cxuiNullBackend;
}

_Use_decl_annotations_
bool cxuiTestRealize(CxUiWidget* w)
{
    if (!_cxuiLive(w) || w->peer || !nullRunning())
        return false;
    _cxuiPeerAttach(w, &nullPeerCreate()->base);
    return true;
}

int32 cxuiTestPeersAlive(void)
{
    return atomicLoad(int32, &nullPeersAlive, Acquire);
}

int32 cxuiTestPeersOffThread(void)
{
    return atomicLoad(int32, &nullPeersOffThread, Acquire);
}

_Use_decl_annotations_
CxUiResult cxuiTestDispatch(CxUiWidget* target, CxUiEvent* ev)
{
    return _cxuiDispatch(target, ev);
}

_Use_decl_annotations_
bool cxuiTestWindowInfo(CxUiWindow* w, CxUiTestWindowInfo* out)
{
    *out = (CxUiTestWindowInfo){ 0 };
    if (!w || !w->peer || !nullRunning())
        return false;
    NullPeer* p  = (NullPeer*)w->peer;
    out->title   = p->title;
    out->size    = p->size;
    out->created = p->created;
    out->pos     = p->pos;
    out->mode    = p->mode;
    out->state      = p->state;
    out->fullscreen = p->fullscreen;
    out->fsMonitor  = p->fsMonitor;
    out->visible = p->visible;
    out->dark    = p->dark;
    out->enabled = !p->disabled;
    out->modal     = p->modal;
    out->owner     = p->owner ? objDynCast(CxUiWindow, p->owner->w) : NULL;
    out->minSize   = p->minSize;
    out->resizable  = p->resizable;
    out->borderless = p->borderless;
    out->drags      = p->drags;
    out->bg         = p->bg;
    return true;
}

_Use_decl_annotations_
void cxuiTestClose(CxUiWindow* w)
{
    if (!w || !w->peer)
        return;
    _cxuiNativeEnter();
    _cxuiWindowCloseRequested(w->peer, CXUI_CLOSE_User);
    _cxuiNativeLeave();
}

// The setting changes, and windows are told as a platform would tell them.
void cxuiTestSetSystemTheme(CxUiThemeMode mode)
{
    if (mode == nullSystemTheme)
        return;
    nullSystemTheme = mode;
    _cxuiNativeEnter();
    _cxuiSystemThemeChanged();
    _cxuiNativeLeave();
}

bool cxuiTestAnswerMessageBox(int32 button)
{
    return _cxuiTestAnswerBuiltBox(button) ||
           (_cxui.backend->testAnswerMessageBox && _cxui.backend->testAnswerMessageBox(button));
}

int32 cxuiTestMessageBoxes(void)
{
    return _cxui.messageBoxes;
}

_Use_decl_annotations_
bool cxuiTestBorderless(CxUiWindow* w, bool* borderless)
{
    *borderless = false;
    if (!w || !w->peer || !_cxui.backend->testBorderless)
        return false;
    return _cxui.backend->testBorderless(w->peer, borderless);
}

_Use_decl_annotations_
bool cxuiTestFocusCues(CxUiWindow* w, bool hide, bool* shown)
{
    *shown = false;
    if (!w || !w->peer || !_cxui.backend->testFocusCues)
        return false;
    return _cxui.backend->testFocusCues(w->peer, hide, shown);
}

int32 cxuiTestFontMeasures(void)
{
    return nullMeasures;
}

void cxuiTestBlur(void)
{
    CxUiPeer* old = nullFocus;
    if (!old)
        return;
    nullFocus = NULL;
    _cxuiNativeEnter();
    _cxuiFocusChanged(old, false);
    _cxuiNativeLeave();
}

_Use_decl_annotations_
bool cxuiTestFrameDark(CxUiWindow* w, bool* dark)
{
    *dark = false;
    if (!w || !w->peer || !_cxui.backend->testFrameDark)
        return false;
    return _cxui.backend->testFrameDark(w->peer, dark);
}

_Use_decl_annotations_
bool cxuiTestCanvasInfo(CxUiCanvas* cv, CxUiTestCanvasInfo* out)
{
    *out = (CxUiTestCanvasInfo){ 0 };
    if (!cv || !cv->peer || !nullRunning())
        return false;
    NullCanvas* nc  = (NullCanvas*)cv->peer;
    out->bounds     = nc->bounds;
    out->visible    = !nc->hidden;
    out->captured   = nc->captured;
    out->focused    = nullFocus == cv->peer;
    out->cursor     = nc->cursor;
    out->cursorImage = nc->cursorImage;
    out->warpedTo    = nc->warpedTo;
    out->warps       = nc->warps;
    out->textInput  = nc->textInput;
    out->caret      = nc->caret;
    out->keyCapture = nc->keyCapture;
    out->lockMode   = nc->lockMode;
    out->lockAt     = nc->lockAt;
    out->paints     = nc->paints;
    out->lastDirty  = nc->lastDirty;
    out->ops        = nc->ops;
    out->nops       = nc->nops;
    return true;
}

_Use_decl_annotations_
bool cxuiTestSurfaceInfo(CxUiRenderView* v, CxUiTestSurfaceInfo* out)
{
    *out        = (CxUiTestSurfaceInfo){ 0 };
    CxUiPeer* p = v ? CxUiWidget(v)->peer : NULL;
    return p && _cxui.backend->testSurfaceInfo && _cxui.backend->testSurfaceInfo(p, out);
}

_Use_decl_annotations_
bool cxuiTestSurfaceMark(CxUiRenderView* v)
{
    CxUiPeer* p = v ? CxUiWidget(v)->peer : NULL;
    return p && _cxui.backend->testSurfaceMark && _cxui.backend->testSurfaceMark(p);
}

_Use_decl_annotations_
bool cxuiTestBounds(CxUiWidget* w, CxUiRect* out)
{
    *out = w ? w->_bounds : (CxUiRect){ 0 };
    return w && (w->state & CXUI_WS_Placed);
}

_Use_decl_annotations_
bool cxuiTestFrontmost(CxUiWidget* w, int32 x, int32 y, bool* front)
{
    *front          = false;
    CxUiWindow* win = w ? _cxuiWindowOf(w) : NULL;
    if (!w || !w->peer || !win || !CxUiWidget(win)->peer || !_cxui.backend->testFrontmost)
        return false;
    _cxuiNativeEnter();
    bool ok = _cxui.backend->testFrontmost(CxUiWidget(win)->peer, w->peer, (CxUiPoint){ x, y }, front);
    _cxuiNativeLeave();
    return ok;
}

_Use_decl_annotations_
bool cxuiTestShown(CxUiWidget* w, bool* shown)
{
    *shown = false;
    return w && w->peer && _cxui.backend->testPeerShown && _cxui.backend->testPeerShown(w->peer, shown);
}

static bool nullTestPeerShown(CxUiPeer* p, bool* shown)
{
    *shown = p->kind == CXUI_PEER_Canvas  ? !((NullCanvas*)p)->hidden
             : p->kind == CXUI_PEER_Panel ? ((NullPanel*)p)->info.visible
             : p->kind == CXUI_PEER_Text  ? ((NullText*)p)->info.visible
             : _cxuiPeerIsControl(p->kind) ? ((NullControl*)p)->info.visible
                                          : ((NullPeer*)p)->visible;
    return true;
}

_Use_decl_annotations_
bool cxuiTestTextInfo(CxUiTextField* t, CxUiTestTextInfo* out)
{
    *out = (CxUiTestTextInfo){ 0 };
    CxUiPeer* p = t ? CxUiWidget(t)->peer : NULL;
    return p && _cxui.backend->testTextInfo && _cxui.backend->testTextInfo(p, out);
}

_Use_decl_annotations_
void cxuiTestLoopStats(CxUiTestLoopStats* out)
{
    *out = (CxUiTestLoopStats){ .ticks = atomicLoad(uint64, &_cxui.ticks, Relaxed),
                                .next  = atomicLoad(int64, &_cxui.nextWake, Relaxed) };
}

_Use_decl_annotations_
int32 cxuiTestMenuCount(CxUiMenu* m)
{
    CxUiPeer* p = m ? CxUiWidget(m)->peer : NULL;
    return p && _cxui.backend->testMenuCount ? _cxui.backend->testMenuCount(p) : -1;
}

_Use_decl_annotations_
bool cxuiTestMenuItem(CxUiMenu* m, int32 at, CxUiTestMenuItemInfo* out)
{
    *out        = (CxUiTestMenuItemInfo){ 0 };
    CxUiPeer* p = m ? CxUiWidget(m)->peer : NULL;
    return p && _cxui.backend->testMenuItem && _cxui.backend->testMenuItem(p, at, out);
}

_Use_decl_annotations_
void cxuiTestMenuItemInfoDestroy(CxUiTestMenuItemInfo* info)
{
    strDestroy(&info->title);
    strDestroy(&info->hint);
}

_Use_decl_annotations_
bool cxuiTestMenuChoose(CxUiMenuItem* item)
{
    CxUiWidget* parent = item ? CxUiWidget(item)->parent : NULL;
    CxUiMenu* m        = parent ? objDynCast(CxUiMenu, parent) : NULL;
    CxUiPeer* p        = m ? CxUiWidget(m)->peer : NULL;
    int32 at           = m ? saFind(m->_items, object, item) : -1;
    return p && at >= 0 && _cxui.backend->testMenuChoose && _cxui.backend->testMenuChoose(p, at);
}

_Use_decl_annotations_
bool cxuiTestWindowMenu(CxUiWindow* w, CxUiMenu* m)
{
    CxUiPeer* p = w ? CxUiWidget(w)->peer : NULL;
    if (!p || !_cxui.backend->testWindowMenu)
        return false;
    return _cxui.backend->testWindowMenu(p) == (m ? CxUiWidget(m)->peer : NULL);
}

_Use_decl_annotations_
void cxuiTestTextInfoDestroy(CxUiTestTextInfo* info)
{
    strDestroy(&info->text);
    strDestroy(&info->cue);
}

_Use_decl_annotations_
bool cxuiTestControlInfo(CxUiWidget* w, CxUiTestControlInfo* out)
{
    *out        = (CxUiTestControlInfo){ 0 };
    CxUiPeer* p = w ? w->peer : NULL;
    return p && _cxuiPeerIsControl(p->kind) && _cxui.backend->testControlInfo &&
           _cxui.backend->testControlInfo(p, out);
}

_Use_decl_annotations_
void cxuiTestControlInfoDestroy(CxUiTestControlInfo* info)
{
    strDestroy(&info->text);
    strDestroy(&info->items);
    strDestroy(&info->selection);
}

_Use_decl_annotations_
bool cxuiTestToggle(CxUiList* l, int32 index)
{
    CxUiPeer* p = l ? CxUiWidget(l)->peer : NULL;
    if (!p || !_cxui.backend->testToggle)
        return false;
    _cxuiNativeEnter();
    bool ok = _cxui.backend->testToggle(p, index);
    _cxuiNativeLeave();
    return ok;
}

_Use_decl_annotations_
bool cxuiTestClick(CxUiWidget* w)
{
    CxUiPeer* p = w ? w->peer : NULL;
    if (!p || !_cxui.backend->testClick)
        return false;
    _cxuiNativeEnter();
    bool ok = _cxui.backend->testClick(p);
    _cxuiNativeLeave();
    return ok;
}

_Use_decl_annotations_
bool cxuiTestChoose(CxUiWidget* w, int32 index, bool activate)
{
    CxUiPeer* p = w ? w->peer : NULL;
    if (!p || !_cxui.backend->testChoose)
        return false;
    _cxuiNativeEnter();
    bool ok = _cxui.backend->testChoose(p, index, activate);
    _cxuiNativeLeave();
    return ok;
}

_Use_decl_annotations_
bool cxuiTestSlide(CxUiSlider* s, float64 value, bool release)
{
    CxUiPeer* p = s ? CxUiWidget(s)->peer : NULL;
    if (!p || !_cxui.backend->testSlide)
        return false;
    int32 pos = (int32)floor((value - s->_min) / s->_step + 0.5);
    _cxuiNativeEnter();
    bool ok = _cxui.backend->testSlide(p, pos, release);
    _cxuiNativeLeave();
    return ok;
}

_Use_decl_annotations_
bool cxuiTestScrollPos(CxUiScrollBox* sb, CxUiPoint* out)
{
    *out        = (CxUiPoint){ 0 };
    CxUiPeer* p = sb ? CxUiWidget(sb)->peer : NULL;
    return p && _cxui.backend->testScrollPos && _cxui.backend->testScrollPos(p, out);
}

_Use_decl_annotations_
bool cxuiTestScrollWheel(CxUiScrollBox* sb, int32 notches)
{
    CxUiPeer* p = sb ? CxUiWidget(sb)->peer : NULL;
    if (!p || !_cxui.backend->testScrollWheel)
        return false;
    _cxuiNativeEnter();
    bool ok = _cxui.backend->testScrollWheel(p, notches);
    _cxuiNativeLeave();
    return ok;
}

_Use_decl_annotations_
bool cxuiTestTip(CxUiWidget* w, string* out)
{
    strDestroy(out);
    CxUiPeer* p = w ? w->peer : NULL;
    return p && _cxui.backend->testTip && _cxui.backend->testTip(p, out);
}

_Use_decl_annotations_
bool cxuiTestType(CxUiTextField* t, strref s)
{
    CxUiPeer* p = t ? CxUiWidget(t)->peer : NULL;
    if (!p || !_cxui.backend->testType)
        return false;
    _cxuiNativeEnter();
    bool ok = _cxui.backend->testType(p, s);
    _cxuiNativeLeave();
    return ok;
}

_Use_decl_annotations_
bool cxuiTestWindowActive(CxUiWindow* w, bool* active)
{
    *active = false;
    CxUiPeer* p = w ? CxUiWidget(w)->peer : NULL;
    return p && _cxui.backend->testWindowActive && _cxui.backend->testWindowActive(p, active);
}

_Use_decl_annotations_
bool cxuiTestNativeKey(CxUiWidget* target, CxUiKey key, uint32 mods)
{
    CxUiPeer* p = target ? target->peer : NULL;
    if (!p || !_cxui.backend->testNativeKey)
        return false;
    _cxuiNativeEnter();
    bool ok = _cxui.backend->testNativeKey(p, key, mods);
    _cxuiNativeLeave();
    return ok;
}

_Use_decl_annotations_
bool cxuiTestNativeClick(CxUiCanvas* cv, int32 x, int32 y)
{
    if (!cv || !cv->peer || !_cxui.backend->testNativeClick)
        return false;
    _cxuiNativeEnter();
    bool ok = _cxui.backend->testNativeClick(cv->peer, (CxUiPoint){ x, y });
    _cxuiNativeLeave();
    return ok;
}

_Use_decl_annotations_
bool cxuiTestNativeWheel(CxUiCanvas* cv, int32 x, int32 y, int32 notches)
{
    if (!cv || !cv->peer || !_cxui.backend->testNativeWheel)
        return false;
    _cxuiNativeEnter();
    bool ok = _cxui.backend->testNativeWheel(cv->peer, (CxUiPoint){ x, y }, notches);
    _cxuiNativeLeave();
    return ok;
}

_Use_decl_annotations_
bool cxuiTestPanelInfo(CxUiWidget* w, CxUiTestPanelInfo* out)
{
    *out = (CxUiTestPanelInfo){ 0 };
    if (!w || !w->peer || w->peer->kind != CXUI_PEER_Panel || !nullRunning())
        return false;
    *out = ((NullPanel*)w->peer)->info;
    return true;
}

_Use_decl_annotations_
int32 cxuiTestLayoutPasses(CxUiWindow* w)
{
    return w ? w->_flushes : 0;
}

void cxuiTestSetSystemScale(float32 scale)
{
    nullSystemScale = scale;
}

_Use_decl_annotations_
bool cxuiTestClipboardForeign(strref text)
{
    if (!_cxui.backend || !_cxui.backend->testClipboardForeign)
        return false;
    _cxuiNativeEnter();
    bool ok = _cxui.backend->testClipboardForeign(text);
    _cxuiNativeLeave();
    return ok;
}

_Use_decl_annotations_
bool cxuiTestClipboardRaw(string* out)
{
    strClear(out);
    if (!_cxui.backend)
        return false;
    if (_cxui.backend->testClipboardRaw)
        return _cxui.backend->testClipboardRaw(out);
    return _cxui.backend->clipboardGetText && _cxui.backend->clipboardGetText(out);
}

_Use_decl_annotations_
void cxuiTestSetMonitors(const CxUiMonitor* mons, int32 count)
{
    for (int32 i = 0; i < nullMonitorCount; i++)
        cxuiMonitorDestroy(&nullMonitors[i]);
    nullMonitorCount = clamp(count, 0, (int32)(sizeof(nullMonitors) / sizeof(nullMonitors[0])));
    for (int32 i = 0; i < nullMonitorCount; i++)
        cxuiMonitorCopy(&nullMonitors[i], &mons[i]);
}

void cxuiTestMonitorsChanged(void)
{
    _cxuiNativeEnter();
    _cxuiMonitorsChanged();
    _cxuiNativeLeave();
}

_Use_decl_annotations_
void cxuiTestResize(CxUiWindow* w, int32 width, int32 height)
{
    if (!w || !w->peer || !nullRunning())
        return;
    CxUiSize dips                 = { width, height };
    ((NullPeer*)w->peer)->size    = dips;
    _cxuiNativeEnter();
    _cxuiWindowResized(w->peer, dipsToPx(dips, w->_scale), dips);
    _cxuiNativeLeave();
}

_Use_decl_annotations_
bool cxuiTestExpose(CxUiCanvas* cv)
{
    if (!cv || !cv->peer || !_cxui.backend->testExpose)
        return false;
    _cxuiNativeEnter();
    bool ok = _cxui.backend->testExpose(cv->peer);
    _cxuiNativeLeave();
    return ok;
}

_Use_decl_annotations_
void cxuiTestPointer(CxUiCanvas* cv, uint32 kind, uint32 button, uint32 buttons, uint32 mods, int32 x, int32 y)
{
    if (!cv || !cv->peer)
        return;
    _cxuiNativeEnter();
    _cxuiCanvasPointer(cv->peer, (CxUiPointerKind)kind, button, buttons, mods, (CxUiPoint){ x, y });
    _cxuiNativeLeave();
}

_Use_decl_annotations_
void cxuiTestCaptureLost(CxUiCanvas* cv)
{
    if (!cv || !cv->peer)
        return;
    ((NullCanvas*)cv->peer)->captured = false;   // the platform's doing, not the core's
    _cxuiNativeEnter();
    _cxuiCanvasCaptureLost(cv->peer);
    _cxuiNativeLeave();
}

_Use_decl_annotations_
CxUiResult cxuiTestWheel(CxUiCanvas* cv, float64 dx, float64 dy, uint32 mods, int32 x, int32 y)
{
    if (!cv || !cv->peer)
        return CXUI_Default;
    _cxuiNativeEnter();
    CxUiResult r = _cxuiCanvasWheel(cv->peer, dx, dy, mods, (CxUiPoint){ x, y });
    _cxuiNativeLeave();
    return r;
}

void cxuiTestWheelPause(void)
{
    _cxui.wheelWin = NULL;
    _cxui.wheelTo  = NULL;
    _cxui.wheelAt  = 0;
}

_Use_decl_annotations_
CxUiResult cxuiTestKey(CxUiWidget* target, CxUiKey key, uint32 mods, bool down)
{
    return cxuiTestKeyScancode(target, key, mods, 0, down);
}

_Use_decl_annotations_
CxUiResult cxuiTestKeyScancode(CxUiWidget* target, CxUiKey key, uint32 mods, uint32 scancode, bool down)
{
    if (!target || !target->peer)
        return CXUI_Default;
    _cxuiNativeEnter();
    CxUiResult r = _cxuiKey(target->peer, key, mods, scancode, down);
    _cxuiNativeLeave();
    return r;
}

_Use_decl_annotations_
void cxuiTestText(CxUiCanvas* cv, strref text)
{
    if (!cv || !cv->peer)
        return;
    _cxuiNativeEnter();
    _cxuiCanvasText(cv->peer, text);
    _cxuiNativeLeave();
}

_Use_decl_annotations_
void cxuiTestPointerMotion(CxUiCanvas* cv, float32 dx, float32 dy, uint32 mods)
{
    if (!cv || !cv->peer)
        return;
    _cxuiNativeEnter();
    _cxuiCanvasPointerMotion(cv->peer, dx, dy, mods);
    _cxuiNativeLeave();
}

_Use_decl_annotations_
void cxuiTestActivate(CxUiWindow* w, bool active)
{
    if (!w || !w->peer)
        return;
    _cxuiNativeEnter();
    _cxuiWindowActivated(w->peer, active);
    _cxuiNativeLeave();
}

_Use_decl_annotations_
void cxuiTestOccluded(CxUiWindow* w, bool occluded)
{
    if (!w || !w->peer)
        return;
    _cxuiNativeEnter();
    _cxuiWindowOccluded(w->peer, occluded);
    _cxuiNativeLeave();
}

_Use_decl_annotations_
bool cxuiTestTopmost(CxUiWindow* w, bool* topmost)
{
    *topmost = false;
    return w && w->peer && _cxui.backend->testTopmost && _cxui.backend->testTopmost(w->peer, topmost);
}

void cxuiTestSetPointerLockMax(uint32 mode)
{
    nullLockMax = mode;
}

_Use_decl_annotations_
bool cxuiTestKeepAwake(string* reason)
{
    strDup(reason, nullAwakeReason);
    return nullAwakeReason != NULL;
}

void cxuiTestSetKeyLocks(uint32 locks)
{
    nullKeyLocks = locks;
}

void cxuiTestSetDoubleClick(int64 time, int32 distance)
{
    nullDoubleTime     = time;
    nullDoubleDistance = distance;
}

_Use_decl_annotations_
bool cxuiTestCanvasScreenOrigin(CxUiCanvas* cv, CxUiPoint* out)
{
    *out = (CxUiPoint){ 0 };
    return cv && cv->peer && _cxui.backend->testScreenOrigin && _cxui.backend->testScreenOrigin(cv->peer, out);
}

_Use_decl_annotations_
strref cxuiTestCanvasEngine(CxUiCanvas* cv)
{
    return cv && cv->peer ? ((CxUiCanvasPeer*)cv->peer)->ops->name : NULL;
}

_Use_decl_annotations_
bool cxuiTestDpi(CxUiWindow* w, float32 scale)
{
    if (!w || !w->peer)
        return false;
    if (!nullRunning()) {
        _cxuiNativeEnter();
        bool ok = _cxui.backend->testForceDpi && _cxui.backend->testForceDpi(w->peer, scale);
        _cxuiNativeLeave();
        return ok;
    }
    _cxuiNativeEnter();
    _cxuiWindowDpiChanged(w->peer, scale, dipsToPx(w->_size, scale));
    _cxuiNativeLeave();
    return true;
}

const CxUiBackend _cxuiNullBackend = {
    .name      = _S"null",
    .mnemonics = true,
    .tabMnemonics = true,
    .init      = nullInit,
    .shutdown  = nullShutdown,
    .runOuter  = nullLoop,
    .runNested = nullLoop,
    .flush     = nullFlush,
    .wake      = nullWake,
    .armTimer  = nullArmTimer,
    .peerDestroy    = nullPeerDestroy,
    .peerSetVisible = nullPeerSetVisible,
    .peerSetEnabled = nullPeerSetEnabled,

    .windowCreate       = nullWindowCreate,
    .windowSetMinSize   = nullWindowSetMinSize,
    .windowSetResizable = nullWindowSetResizable,
    .windowBeginDrag    = nullWindowBeginDrag,
    .windowShow        = nullWindowShow,
    .windowHide        = nullWindowHide,
    .windowSetTitle    = nullWindowSetTitle,
    .windowSetSize     = nullWindowSetSize,
    .windowGetPosition = nullWindowGetPosition,
    .windowSetPosition = nullWindowSetPosition,
    .placementCaps      = CXUI_PLACE_Position | CXUI_PLACE_Size | CXUI_PLACE_State,
    .windowGetPlacement = nullWindowGetPlacement,
    .windowSetPlacement = nullWindowSetPlacement,
    .windowSetDark     = nullWindowSetDark,
    .systemTheme       = nullSystemThemeGet,
    .windowBeginModal  = nullWindowBeginModal,
    .windowEndModal    = nullWindowEndModal,
    .windowSetOwner    = nullWindowSetOwner,
    .windowMoveNear    = nullWindowMoveNear,
    .windowSetEnabled  = nullWindowSetEnabled,
    .windowSetFullscreen = nullWindowSetFullscreen,
    .windowMonitor       = nullWindowMonitor,
    .windowSetTopmost    = nullWindowSetTopmost,
    .testTopmost         = nullTestTopmost,

    .systemScale      = nullSystemScaleGet,
    .canvasCreate     = nullCanvasCreate,
    .surfaceGet       = nullSurfaceGet,
    .testSurfaceInfo  = nullTestSurfaceInfo,
    .testSurfaceMark  = nullTestSurfaceMark,
    .canvasSetBounds  = nullCanvasSetBounds,
    .canvasInvalidate = nullCanvasInvalidate,
    .canvasPaintNow   = nullCanvasPaintNow,
    .canvasCapture    = nullCanvasCapture,
    .peerFocus        = nullPeerFocus,
    .windowFocusCues  = nullWindowFocusCues,
    .peerRaise        = nullPeerRaise,
    .peerSetTip       = nullPeerSetTip,
    .canvasSetCursor  = nullCanvasSetCursor,
    .canvasSetCursorImage = nullCanvasSetCursorImage,
    .canvasWarpPointer    = nullCanvasWarpPointer,
    .canvasSetTextInput  = nullCanvasSetTextInput,
    .canvasSetTextCaret  = nullCanvasSetTextCaret,
    .canvasSetKeyCapture = nullCanvasSetKeyCapture,
    .canvasPointerLock   = nullCanvasPointerLock,
    .doubleClick      = nullDoubleClick,
    .keyLocks         = nullKeyLocksGet,

    .panelCreate         = nullPanelCreate,
    .panelSetBounds      = nullPanelSetBounds,
    .panelSetColors      = nullPanelSetColors,
    .panelSetTitle       = nullPanelSetTitle,
    .panelSetContent     = nullPanelSetContent,
    .panelScrollTo       = nullPanelScrollTo,
    .windowSetBackground = nullWindowSetBackground,
    .windowSetMenu       = nullWindowSetMenu,
    .menuCreate          = nullMenuCreate,
    .menuInsert          = nullMenuInsert,
    .menuRemove          = nullMenuRemove,
    .menuUpdate          = nullMenuUpdate,
    .testMenuCount       = nullTestMenuCount,
    .testMenuItem        = nullTestMenuItem,
    .testMenuChoose      = nullTestMenuChoose,
    .testWindowMenu      = nullTestWindowMenu,

    .textCreate       = nullTextCreate,
    .textSetBounds    = nullTextSetBounds,
    .textMeasure      = nullTextMeasure,
    .textSetText      = nullTextSetText,
    .textGetSelection = nullTextGetSelection,
    .textSetSelection = nullTextSetSelection,
    .textSetCue       = nullTextSetCue,
    .textSetMaxLength = nullTextSetMaxLength,
    .textSetFont      = nullTextSetFont,
    .textSetState     = nullTextSetState,
    .textSetColors    = nullTextSetColors,
    .textAppend       = nullTextAppend,
    .textScrollToEnd  = nullTextScrollToEnd,

    .labelCreate      = nullLabelCreate,
    .labelSetText     = nullControlSetText,
    .labelSetStyle    = nullLabelSetStyle,
    .controlSetColor    = nullLabelSetColor,
    .buttonCreate     = nullButtonCreate,
    .buttonSetTitle   = nullControlSetText,
    .buttonSetDefault = nullButtonSetDefault,
    .checkCreate      = nullCheckCreate,
    .checkSetState    = nullCheckSetState,
    .listCreate       = nullListCreate,
    .listSplice       = nullListSplice,
    .listSetValue     = nullListSetValue,
    .listSetColors    = nullListSetColors,
    .listSetMarks     = nullListSetMarks,
    .listGetMarks     = nullListGetMarks,
    .listTopItem      = nullListTopItem,
    .listSetTopItem   = nullListSetTopItem,
    .sliderCreate     = nullSliderCreate,
    .sliderSetRange   = nullSliderSetRange,
    .sliderSetPos     = nullSliderSetPos,
    .progressCreate   = nullProgressCreate,
    .progressSet      = nullProgressSet,
    .tabsCreate       = nullTabsCreate,
    .tabsInsert       = nullTabsInsert,
    .tabsRemove       = nullTabsRemove,
    .tabsSetTitle     = nullTabsSetTitle,
    .tabsSetValue     = nullTabsSetValue,
    .tabsSetPage      = nullTabsSetPage,
    .tabsSetColors    = nullTabsSetColors,
    .controlSetBounds = nullControlSetBounds,
    .controlSetFont   = nullControlSetFont,

    .monitors         = nullMonitorsGet,
    .clipboardSetText = nullClipboardSetText,
    .clipboardGetText = nullClipboardGetText,
    .keepAwake        = nullKeepAwake,

    .messageBox = nullMessageBox,
    .fileDialog = nullFileDialog,
    .colorDialog = nullColorDialog,

    .testFrameDark        = nullTestFrameDark,
    .testBorderless       = nullTestBorderless,
    .testFocusCues        = nullTestFocusCues,
    .testAnswerMessageBox = nullTestAnswerMessageBox,
    .testAnswerFileDialog = nullTestAnswerFileDialog,
    .testAnswerColorDialog = nullTestAnswerColorDialog,
    .testOwnedBy           = nullTestOwnedBy,
    .testPeerShown        = nullTestPeerShown,
    .testTextInfo         = nullTestTextInfo,
    .testType             = nullTestType,
    .testControlInfo      = nullTestControlInfo,
    .testClick            = nullTestClick,
    .testChoose           = nullTestChoose,
    .testToggle           = nullTestToggle,
    .testSlide            = nullTestSlide,
    .testScrollPos        = nullTestScrollPos,
    .testScrollWheel      = nullTestScrollWheel,
    .testTip              = nullTestTip,
    .testClipboardForeign = nullClipboardSetText,
};
