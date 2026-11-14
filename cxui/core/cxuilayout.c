#include "cxuidraw_private.h"
#include "core/cxuibox.h"

#include <math.h>

// The layout pass running, if any. A pass can start inside another (a resize handler calling
// cxuiRefresh on a second window), so each saves and restores the one it found. UI thread only.
typedef struct LayoutCtx {
    CxUiWindow* win;   // NULL when measuring a widget that isn't in a window
    float32 scale;
    bool force;        // place everything again: the scale changed
    bool fit;          // measuring a fit size: a split makes room for its fraction to fit both panes
} LayoutCtx;

static LayoutCtx* ctx;

static bool layoutLive(CxUiWidget* w)
{
    return !(w->state & CXUI_WS_Dead);
}

static float32 systemScale(void)
{
    return _cxui.backend->systemScale ? _cxui.backend->systemScale() : 1.0f;
}

// ---- units ----

_Use_decl_annotations_
int32 _cxuiLenDip(CxUiWidget* w, CxUiLen len, int axis)
{
    if (len.v <= 0)
        return 0;
    if (len.unit != CXUI_UNIT_Chars)
        return len.v;
    // The widget's font is made at its window's scale, the pass's.
    CxUiFont* f = cxuiWidgetFont(w);
    CxUiFontMetrics m;
    cxuiFontMetrics(f, &m);
    int32 unit = axis == 0 ? m.avgCharW : m.height;
    return (int32)floor((float64)len.v * unit / cxuiFontScale(f) + 0.5);
}

static int32 fixToPx(int32 fix)
{
    return (int32)floor((float64)fix * ctx->scale / CXUI_FIX + 0.5);
}

static int32 pxToFix(int32 px)
{
    return (int32)floor((float64)px * CXUI_FIX / ctx->scale + 0.5);
}

// ---- dirty marks ----

static void scheduleFlush(CxUiWindow* win);

// Stops at a dirty ancestor, whose own ancestors are dirty too. w itself may be dirty without
// that holding: a widget that has been hidden since it was last laid out.
static void markUp(CxUiWidget* w, bool schedule)
{
    const uint32 both = CXUI_WS_MeasureDirty | CXUI_WS_ArrangeDirty;
    for (CxUiWidget* first = w; w; w = w->parent) {
        if (w != first && (w->state & both) == both)
            return;
        w->state |= both;
        if (schedule && (w->state & CXUI_WS_IsWindow))
            scheduleFlush((CxUiWindow*)w);
    }
}

_Use_decl_annotations_
void _cxuiLayoutDirty(CxUiWidget* w)
{
    markUp(w, true);
}

static void markTree(CxUiWidget* w)
{
    w->state |= CXUI_WS_MeasureDirty | CXUI_WS_ArrangeDirty;
    for (int32 i = 0, n = _cxuiContainerChildCount(w); i < n; i++) {
        CxUiWidget* child = _cxuiContainerChild(w, i);
        if (child)
            markTree(child);
    }
}

static void needWalk(CxUiWidget* w)
{
    CxUiWindow* win = _cxuiWindowOf(w);
    if (win)
        win->_needWalk = true;
}

// ---- enabled state ----

static void enabledUpdate(CxUiWidget* w, bool parentOn)
{
    bool on = parentOn && !(w->state & CXUI_WS_Disabled);
    if (on != !(w->state & CXUI_WS_Inactive)) {
        w->state ^= CXUI_WS_Inactive;
        // A menu's items show the state; the menu itself has no surface to gray.
        if (w->peer && w->peer->kind != CXUI_PEER_Menu && _cxui.backend->peerSetEnabled)
            _cxui.backend->peerSetEnabled(w->peer, on);
        // Drawn widgets show the state themselves.
        cxuiWidgetRestyle(w);
    }
    for (int32 i = 0, n = _cxuiContainerChildCount(w); i < n; i++) {
        CxUiWidget* child = _cxuiContainerChild(w, i);
        if (child)
            enabledUpdate(child, on);
    }
}

_Use_decl_annotations_
void _cxuiLayoutEnabledChanged(CxUiWidget* w)
{
    enabledUpdate(w, !w->parent || !(w->parent->state & CXUI_WS_Inactive));
}

_Use_decl_annotations_
void _cxuiLayoutVisibilityChanged(CxUiWidget* w)
{
    if (w->state & CXUI_WS_Hidden) {
        if (w->parent)
            markUp(w->parent, true);
    } else {
        markUp(w, true);
    }
    needWalk(w);
}

// ---- attaching ----

static void unrealizeTree(CxUiWidget* w)
{
    for (int32 i = 0, n = _cxuiContainerChildCount(w); i < n; i++) {
        CxUiWidget* child = _cxuiContainerChild(w, i);
        if (child)
            unrealizeTree(child);
    }
    _cxuiUnrealize(w);
}

_Use_decl_annotations_
void _cxuiLayoutDirtyTree(CxUiWidget* w)
{
    markTree(w);
    if (w->parent)
        markUp(w->parent, true);
    else if (w->state & CXUI_WS_IsWindow)
        scheduleFlush((CxUiWindow*)w);
}

_Use_decl_annotations_
void _cxuiLayoutAttached(CxUiWidget* parent, CxUiWidget* child)
{
    // The interface font it kept at the top of its own tree now comes from above.
    if (!child->_hasFont)
        objRelease(&child->_font);
    markTree(child);
    child->state &= ~CXUI_WS_Placed;
    enabledUpdate(child, !(parent->state & CXUI_WS_Inactive));
    markUp(parent, true);
    needWalk(parent);
}

_Use_decl_annotations_
void _cxuiLayoutDetached(CxUiWidget* parent, CxUiWidget* child)
{
    unrealizeTree(child);
    enabledUpdate(child, true);
    markUp(parent, true);
}

// ---- measure ----

_Use_decl_annotations_
bool _cxuiLayoutTakesSpace(CxUiWidget* child)
{
    return !(child->state & ((uint32)CXUI_WS_Hidden | CXUI_WS_Dead)) && !(child->_lp.flags & CXUI_LP_Floating);
}

// The axes a member of a normalize group shares its size in: those its box normalizes, or across.
static uint32 groupAxes(CxUiWidget* w)
{
    CxUiBox* box = w->parent ? objDynCast(CxUiBox, w->parent) : NULL;
    return box && box->_normalize ? box->_normalize : CXUI_EXPAND_H;
}

// What a widget measured, with its own size and minimum applied
static void setNatural(CxUiWidget* w, CxUiSize m)
{
    w->_measured = m;
    w->_hfwValid = false;
    int32 n[2]   = { m.w, m.h };
    for (int a = 0; a < 2; a++) {
        int32 size = _cxuiLenDip(w, w->_lp.size[a], a);
        if (size > 0)
            n[a] = size;
        n[a] = max(n[a], _cxuiLenDip(w, w->_lp.minSize[a], a));
    }
    w->_natural = (CxUiSize){ n[0], n[1] };
    w->state &= ~CXUI_WS_MeasureDirty;
}

static void measure(CxUiWidget* w)
{
    uint8 own    = w->_lp.expand & CXUI_EXPAND_Both;
    w->_expand   = own;
    w->_hfw      = false;
    CxUiSize m   = cxuiWidgetMeasure(w);
    // What the children added, less the axes the widget stops
    w->_expand   = (w->_expand & ~(w->_lp.expand >> 2)) | own;
    // A container's height depends on its width when any child's does; a fixed height doesn't.
    for (int32 i = 0, n = _cxuiContainerChildCount(w); i < n && !w->_hfw; i++) {
        CxUiWidget* child = _cxuiContainerChild(w, i);
        if (child && _cxuiLayoutTakesSpace(child) && child->_hfw)
            w->_hfw = true;
    }
    if (_cxuiLenDip(w, w->_lp.size[1], 1) > 0)
        w->_hfw = false;
    setNatural(w, m);

    // The window keeps group sizes from the first time it has a group member.
    if (w->_lp.normalizeGroup && ctx && ctx->win && !ctx->win->_groups)
        ctx->win->_groups = xaAlloc(256 * sizeof(CxUiSize), XA_Zero);
}

_Use_decl_annotations_
CxUiSize _cxuiLayoutNatural(CxUiWidget* w)
{
    if (w->state & CXUI_WS_MeasureDirty)
        measure(w);
    CxUiSize n = w->_natural;
    uint8 g    = w->_lp.normalizeGroup;
    if (g && ctx && ctx->win && ctx->win->_groups) {
        uint32 axes = groupAxes(w);
        CxUiSize gm = ctx->win->_groups[g];
        if (axes & CXUI_EXPAND_H)
            n.w = max(n.w, gm.w);
        if (axes & CXUI_EXPAND_V)
            n.h = max(n.h, gm.h);
    }
    return n;
}

_Use_decl_annotations_
int32 _cxuiLayoutHeightFor(CxUiWidget* w, int32 width)
{
    CxUiSize n = _cxuiLayoutNatural(w);
    if (!w->_hfw)
        return n.h;
    width = max(width, 0);
    if (!w->_hfwValid || w->_hfwWidth != width) {
        w->_hfwHeight = max(cxuiWidgetHeightFor(w, width), _cxuiLenDip(w, w->_lp.minSize[1], 1));
        w->_hfwWidth  = width;
        w->_hfwValid  = true;
    }
    return w->_hfwHeight;
}

_Use_decl_annotations_
uint32 _cxuiLayoutExpand(CxUiWidget* child, uint32 mainAxis)
{
    if (!(child->_lp.expand & CXUI_EXPAND_Both) && objDynCast(CxUiFill, child))
        return mainAxis;
    return child->_expand;
}

// Collects the largest natural size of each normalize group's visible members.
static bool groupCollect(CxUiWidget* w, CxUiSize* maxes)
{
    bool any = false;
    for (int32 i = 0, n = _cxuiContainerChildCount(w); i < n; i++) {
        CxUiWidget* child = _cxuiContainerChild(w, i);
        if (!child || !_cxuiLayoutTakesSpace(child))
            continue;
        uint8 g = child->_lp.normalizeGroup;
        if (g) {
            if (child->state & CXUI_WS_MeasureDirty)
                measure(child);
            maxes[g].w = max(maxes[g].w, child->_natural.w);
            maxes[g].h = max(maxes[g].h, child->_natural.h);
            any        = true;
        }
        any |= groupCollect(child, maxes);
    }
    return any;
}

// Marks the containers of a group's members, so they measure again with its new size.
static void groupMark(CxUiWidget* w, CxUiSize* old, CxUiSize* now)
{
    for (int32 i = 0, n = _cxuiContainerChildCount(w); i < n; i++) {
        CxUiWidget* child = _cxuiContainerChild(w, i);
        if (!child)
            continue;
        uint8 g = child->_lp.normalizeGroup;
        if (g && (old[g].w != now[g].w || old[g].h != now[g].h))
            markUp(w, false);
        groupMark(child, old, now);
    }
}

static CxUiSize measureContent(CxUiWindow* win)
{
    CxUiWidget* content = win->_content;
    if (!content || !_cxuiLayoutTakesSpace(content))
        return (CxUiSize){ 0 };
    _cxuiLayoutNatural(content);
    if (!win->_groups)
        return content->_natural;

    // Members measured above with the old group sizes; their containers measure again.
    CxUiSize maxes[256] = { 0 };
    groupCollect(CxUiWidget(win), maxes);
    if (memcmp(win->_groups, maxes, sizeof(maxes)) != 0) {
        groupMark(CxUiWidget(win), win->_groups, maxes);
        memcpy(win->_groups, maxes, sizeof(maxes));
    }
    return _cxuiLayoutNatural(content);
}

// Measures a window's content: its natural size, with the normalize groups settled. The window
// is then clean, so a mark made later (by a widget created in the walk) shows.
static CxUiSize measureWindow(CxUiWindow* win)
{
    CxUiSize n = measureContent(win);
    setNatural(CxUiWidget(win), n);
    return n;
}

// The window's fit size, measuring again for it; the natural sizes are measured again after, so
// layout goes on using those.
static CxUiSize fitMeasure(CxUiWindow* win)
{
    ctx->fit = true;
    markTree(CxUiWidget(win));
    CxUiSize n = measureWindow(win);
    ctx->fit   = false;
    markTree(CxUiWidget(win));
    measureWindow(win);
    return n;
}

// ---- arrange ----

static bool isHost(CxUiPeer* p)
{
    return p && (p->kind == CXUI_PEER_Window || p->kind == CXUI_PEER_Panel || p->kind == CXUI_PEER_Tabs);
}

// The peer a widget's own peer is created in: its nearest container with a native surface, or
// its window. origin is where that is in the window.
static CxUiPeer* hostOf(CxUiWidget* w, CxUiPoint* origin)
{
    *origin = (CxUiPoint){ 0 };
    for (CxUiWidget* a = w->parent; a; a = a->parent) {
        if (isHost(a->peer)) {
            if (a->peer->kind == CXUI_PEER_Panel)
                *origin = (CxUiPoint){ a->_bounds.x, a->_bounds.y };
            else if (a->peer->kind == CXUI_PEER_Tabs)
                *origin = (CxUiPoint){ ((CxUiTabs*)a)->_pagePx.x, ((CxUiTabs*)a)->_pagePx.y };
            return a->peer;
        }
    }
    return NULL;
}

// Unless force, only where the peer's place in its host has changed
static void placePeer(CxUiWidget* w, bool force)
{
    CxUiPoint o;
    if (!hostOf(w, &o))
        return;
    CxUiRect b = w->_bounds;
    b.x -= o.x;
    b.y -= o.y;
    CxUiRect* last = &w->_peerBounds;
    if (!force && b.x == last->x && b.y == last->y && b.w == last->w && b.h == last->h)
        return;
    *last = b;
    cxuiWidgetPlacePeer(w, b);
}

static void placeFloating(CxUiWidget* w)
{
    for (int32 i = 0, n = _cxuiContainerChildCount(w); i < n; i++) {
        CxUiWidget* child = _cxuiContainerChild(w, i);
        if (!child || !(child->_lp.flags & CXUI_LP_Floating) || (child->state & ((uint32)CXUI_WS_Hidden | CXUI_WS_Dead)))
            continue;
        if (child->state & CXUI_WS_MeasureDirty)
            measure(child);
        CxUiRect f = child->_floatBounds;
        _cxuiLayoutPlace(child, (CxUiLayoutRect){ w->_lrect.x + f.x * CXUI_FIX, w->_lrect.y + f.y * CXUI_FIX,
                                                  f.w * CXUI_FIX, f.h * CXUI_FIX });
    }
}

// A panel moved in the window: the peers inside it sit relative to it, so they need placing even
// where their own place in the window is the same. A panel inside it carries its own along.
static void markHostedMoved(CxUiWidget* w)
{
    for (int32 i = 0, n = _cxuiContainerChildCount(w); i < n; i++) {
        CxUiWidget* child = _cxuiContainerChild(w, i);
        if (!child)
            continue;
        child->state |= CXUI_WS_ArrangeDirty;
        if (!isHost(child->peer))
            markHostedMoved(child);
    }
}

_Use_decl_annotations_
void _cxuiLayoutHostMoved(CxUiWidget* w)
{
    markHostedMoved(w);
}

CxUiRect _cxuiLayoutPx(CxUiLayoutRect r)
{
    int32 x0 = fixToPx(r.x), y0 = fixToPx(r.y);
    return (CxUiRect){ x0, y0, fixToPx(r.x + max(r.w, 0)) - x0, fixToPx(r.y + max(r.h, 0)) - y0 };
}

_Use_decl_annotations_
void _cxuiLayoutPlace(CxUiWidget* w, CxUiLayoutRect r)
{
    r.w = max(r.w, 0);
    r.h = max(r.h, 0);
    bool moved = r.x != w->_lrect.x || r.y != w->_lrect.y || r.w != w->_lrect.w || r.h != w->_lrect.h ||
                 !(w->state & CXUI_WS_Placed);
    if (!moved && !(w->state & CXUI_WS_ArrangeDirty) && !ctx->force)
        return;

    // Each edge is snapped on its own, so neighbors that share an edge share its pixel.
    int32 x0 = fixToPx(r.x), y0 = fixToPx(r.y);
    CxUiRect px = { x0, y0, fixToPx(r.x + r.w) - x0, fixToPx(r.y + r.h) - y0 };
    bool hostMoved = isHost(w->peer) && (px.x != w->_bounds.x || px.y != w->_bounds.y);
    w->_lrect  = r;
    w->_bounds = px;
    w->state   = (w->state & ~CXUI_WS_ArrangeDirty) | CXUI_WS_Placed;
    if (w->peer)
        placePeer(w, ctx->force);
    if (hostMoved)
        markHostedMoved(w);

    cxuiWidgetArrange(w);
    placeFloating(w);
}

// ---- realization ----

static void setPeerShown(CxUiPeer* p, bool shown)
{
    if (p->hidden == shown && _cxui.backend->peerSetVisible) {
        p->hidden = !shown;
        _cxui.backend->peerSetVisible(p, shown);
    }
}

// Creates the peers of widgets that should have one, and shows or hides the peers whose
// visibility changed; parents come before their children. A widget is realized when it and its
// containers are visible and it is on an active page of every zbox around it, or a page it is in
// carries CXUI_LP_RealizeEager; it is shown only on active pages.
static void walk(CxUiWidget* w, CxUiPeer* host, bool visible, bool active, bool eager)
{
    visible = visible && !(w->state & CXUI_WS_Hidden);
    eager   = eager || (w->_lp.flags & CXUI_LP_RealizeEager);
    bool shown = visible && active;
    if (w->peer) {
        setPeerShown(w->peer, shown);
    } else if (visible && (active || eager) && (w->state & CXUI_WS_Placed)) {
        cxuiWidgetRealizePeer(w, host);
        if (w->peer) {
            if ((w->state & CXUI_WS_Inactive) && _cxui.backend->peerSetEnabled)
                _cxui.backend->peerSetEnabled(w->peer, false);
            if (!strEmpty(w->_tip) && _cxui.backend->peerSetTip)
                _cxui.backend->peerSetTip(w->peer, w->_tip);
            placePeer(w, true);
            cxuiWidgetRestyle(w);
            setPeerShown(w->peer, shown);
        }
    }

    CxUiPeer* inner = isHost(w->peer) ? w->peer : host;
    for (int32 i = 0, n = _cxuiContainerChildCount(w); i < n; i++) {
        CxUiWidget* child = _cxuiContainerChild(w, i);
        if (child)
            walk(child, inner, visible, active && cxuiWidgetShowsChild(w, child), eager);
    }
}

_Use_decl_annotations_
void _cxuiLayoutNeedWalk(CxUiWidget* w)
{
    needWalk(w);
    _cxuiLayoutDirty(w);
}

// ---- window passes ----

static bool sizeEq(CxUiSize a, CxUiSize b)
{
    return a.w == b.w && a.h == b.h;
}

static CxUiSize minFor(CxUiWindow* win, CxUiSize natural)
{
    CxUiLayoutParams* lp = &CxUiWidget(win)->_lp;
    CxUiSize m           = { _cxuiLenDip(CxUiWidget(win), lp->minSize[0], 0), _cxuiLenDip(CxUiWidget(win), lp->minSize[1], 1) };
    if (!(win->_flags & CXUI_WINDOW_Shrink))
        m = (CxUiSize){ max(m.w, natural.w), max(m.h, natural.h) };
    return m;
}

// The client size layout wants for the window; 0 in an axis leaves it as it is (the platform's
// default size, when starting).
// fit is what a window sized to its content takes; natural, which can be smaller, sets its minimum.
static CxUiSize targetFor(CxUiWindow* win, CxUiSize natural, CxUiSize fit, bool starting)
{
    CxUiSize ask = win->_askedSize;
    CxUiSize t   = { 0 };
    if (win->_sizing == CXUI_SIZE_Natural) {
        if (starting || !win->_userSized)
            t = (CxUiSize){ max(ask.w, fit.w), max(ask.h, fit.h) };
    } else if (starting || win->_sizing == CXUI_SIZE_Fixed) {
        t = (CxUiSize){ ask.w ? ask.w : natural.w, ask.h ? ask.h : natural.h };
    }
    if (win->_sizing == CXUI_SIZE_Fixed)
        return t;

    // Never below the minimum, including a size chosen before the content grew
    CxUiSize m   = minFor(win, natural);
    CxUiSize cur = { t.w ? t.w : (starting ? 0 : win->_size.w), t.h ? t.h : (starting ? 0 : win->_size.h) };
    if (cur.w && cur.w < m.w)
        t.w = m.w;
    if (cur.h && cur.h < m.h)
        t.h = m.h;
    return t;
}

// With content whose height depends on its width, the heights are the content's at the width the
// window will have: the one layout gives it, or the one it has.
static void heightsFor(CxUiWindow* win, CxUiSize* natural, CxUiSize* fit, bool starting)
{
    CxUiWidget* content = win->_content;
    if (!content || !_cxuiLayoutTakesSpace(content) || !content->_hfw)
        return;
    CxUiSize t  = targetFor(win, *natural, *fit, starting);
    int32 width = t.w ? t.w : win->_size.w;
    if (width <= 0)
        width = fit->w;
    // The fit height, measured at the natural widths, moves by as much as the natural height.
    int32 was  = natural->h;
    natural->h = _cxuiLayoutHeightFor(content, width);
    fit->h     = max(fit->h + natural->h - was, natural->h);
}

static void sizeWindow(CxUiWindow* win, CxUiSize natural, CxUiSize fit)
{
    CxUiSize m = minFor(win, natural);
    if (win->_sizing == CXUI_SIZE_Fixed)
        m = (CxUiSize){ 0 };
    if (!sizeEq(m, win->_appliedMin) && _cxui.backend->windowSetMinSize) {
        win->_appliedMin = m;
        _cxui.backend->windowSetMinSize(win->peer, m);
    }

    CxUiSize t = targetFor(win, natural, fit, false);
    CxUiSize s = { t.w ? t.w : win->_size.w, t.h ? t.h : win->_size.h };
    if (!sizeEq(s, win->_size)) {
        win->_fitSize = s;
        _cxuiNativeEnter();
        _cxui.backend->windowSetSize(win->peer, s);
        _cxuiNativeLeave();
    }
}

static void arrangeWindow(CxUiWindow* win)
{
    CxUiWidget* self = CxUiWidget(win);
    CxUiLayoutRect r = { 0, 0, pxToFix(win->_clientPx.w), pxToFix(win->_clientPx.h) };
    self->_lrect     = r;
    self->_bounds    = (CxUiRect){ 0, 0, win->_clientPx.w, win->_clientPx.h };
    self->state      = (self->state & ~CXUI_WS_ArrangeDirty) | CXUI_WS_Placed;
    CxUiWidget* c    = win->_content;
    if (c && _cxuiLayoutTakesSpace(c))
        _cxuiLayoutPlace(c, r);
    placeFloating(self);
}

static void beginCtx(LayoutCtx* c, CxUiWindow* win, float32 scale)
{
    *c = (LayoutCtx){ .win = win, .scale = scale > 0 ? scale : 1.0f };
    if (win && win->_measureScale != c->scale) {
        markTree(CxUiWidget(win));
        win->_measureScale = c->scale;
        c->force           = true;
    }
}

_Use_decl_annotations_
void _cxuiLayoutFlush(CxUiWindow* win)
{
    if (!_cxuiLive(CxUiWidget(win)) || !win->peer || win->_flushing)
        return;
    win->_flushQueued = false;
    win->_flushing    = true;
    win->_flushes++;
    objAcquire(win);
    // Keeps peers alive for the whole pass, even if a handler destroys their widgets.
    _cxuiNativeEnter();

    LayoutCtx c, *saved = ctx;
    beginCtx(&c, win, win->_scale);
    ctx = &c;

    // A native control realized by the walk can measure differently from the estimate it was
    // laid out with, so a second round follows at once rather than after the window is shown.
    for (int32 round = 0; round < 2; round++) {
        CxUiSize natural = measureWindow(win);
        CxUiSize fit     = natural;
        if (win->_hasSplit && win->_sizing == CXUI_SIZE_Natural && !win->_userSized)
            fit = fitMeasure(win);
        heightsFor(win, &natural, &fit, false);
        sizeWindow(win, natural, fit);
        CxUiPeer* peer = win->peer;
        if (!layoutLive(CxUiWidget(win)) || !peer)
            break;
        if (_cxui.backend->windowLayoutBegin)
            _cxui.backend->windowLayoutBegin(peer);
        arrangeWindow(win);
        if (win->_needWalk) {
            win->_needWalk = false;
            if (win->_content)
                walk(win->_content, peer, true, true, false);
        }
        if (_cxui.backend->windowLayoutEnd)
            _cxui.backend->windowLayoutEnd(peer);
        if (!(CxUiWidget(win)->state & CXUI_WS_MeasureDirty)) {
            // Nothing is left for the pass a realized widget's new size queued.
            win->_flushQueued = false;
            break;
        }
    }

    ctx            = saved;
    win->_flushing = false;
    _cxuiNativeLeave();
    objRelease(&win);
}

_Use_decl_annotations_
void _cxuiLayoutNow(CxUiPeer* p)
{
    CxUiWidget* w   = p->w;
    CxUiWindow* win = w && _cxuiLive(w) ? _cxuiWindowOf(w) : NULL;
    if (win && win->_flushQueued)
        _cxuiLayoutFlush(win);
}

static bool flushTask(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiWindow* win = stvlAtObj(cvars, 0, CxUiWindow);
    if (win->_flushQueued)
        _cxuiLayoutFlush(win);
    return true;
}

static void scheduleFlush(CxUiWindow* win)
{
    if (!win->peer || win->_flushQueued || !layoutLive(CxUiWidget(win)))
        return;
    win->_flushQueued = true;
    cxuiPost(closureCreate(flushTask, stvar(object, win)));
}

bool _cxuiLayoutFitting(void)
{
    return ctx && ctx->fit;
}

void _cxuiLayoutSplitSeen(void)
{
    if (ctx && ctx->win)
        ctx->win->_hasSplit = true;
}

_Use_decl_annotations_
CxUiSize _cxuiLayoutFitSize(CxUiWindow* win)
{
    if (!win->peer)
        return _cxuiNaturalSize(CxUiWidget(win));
    _cxuiLayoutFlush(win);
    LayoutCtx c, *saved = ctx;
    beginCtx(&c, win, win->_scale);
    ctx        = &c;
    CxUiSize n = fitMeasure(win);
    ctx        = saved;
    return n;
}

_Use_decl_annotations_
CxUiSize _cxuiLayoutStartSize(CxUiWindow* win)
{
    LayoutCtx c, *saved = ctx;
    beginCtx(&c, win, systemScale());
    ctx              = &c;
    CxUiSize natural = measureWindow(win);
    CxUiSize fit     = win->_hasSplit ? fitMeasure(win) : natural;
    heightsFor(win, &natural, &fit, true);
    CxUiSize t       = targetFor(win, natural, fit, true);
    ctx              = saved;
    return t;
}

// ---- public ----

_Use_decl_annotations_
void _cxuiRefresh(CxUiWidget* w)
{
    if (!_cxuiLive(w))
        return;
    CxUiWindow* win = _cxuiWindowOf(w);
    if (win)
        _cxuiLayoutFlush(win);
}

_Use_decl_annotations_
CxUiSize _cxuiNaturalSize(CxUiWidget* w)
{
    if (!_cxuiLive(w))
        return (CxUiSize){ 0 };
    CxUiWindow* win = _cxuiWindowOf(w);
    if (win && win->peer)
        _cxuiLayoutFlush(win);
    if (ctx && ctx->win == win)
        return _cxuiLayoutNatural(w);

    LayoutCtx c, *saved = ctx;
    beginCtx(&c, win, win && win->peer ? win->_scale : systemScale());
    ctx = &c;
    if (win && !win->peer)
        measureWindow(win);
    CxUiSize n = _cxuiLayoutNatural(w);
    ctx        = saved;
    return n;
}
