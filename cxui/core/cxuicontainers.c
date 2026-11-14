// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "core/cxuicontainers.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cxui_private.h"

#include <math.h>

// A frame's background role: whatever is around it
#define BG_AROUND CXUI_PAL_Count

static bool live(void* w)
{
    return _cxuiLive((CxUiWidget*)w);
}

static int32 axisLen(CxUiSize s, int a)
{
    return a == 0 ? s.w : s.h;
}

static uint32 axisBit(int a)
{
    return a == 0 ? CXUI_EXPAND_H : CXUI_EXPAND_V;
}

static int32 marginStart(CxUiBoxBase* b, int a)
{
    return (a == 0 ? b->_margin.left : b->_margin.top + b->_insetTop) + b->_inset;
}

static int32 marginEnd(CxUiBoxBase* b, int a)
{
    return (a == 0 ? b->_margin.right : b->_margin.bottom) + b->_inset;
}

// The area inside a container's margins, in CXUI_FIX units: position and length per axis
static void innerOf(CxUiBoxBase* b, int32 pos[2], int32 len[2])
{
    CxUiLayoutRect r = CxUiWidget(b)->_lrect;
    int32 rp[2] = { r.x, r.y }, rl[2] = { r.w, r.h };
    for (int a = 0; a < 2; a++) {
        int32 s0 = marginStart(b, a) * CXUI_FIX, s1 = marginEnd(b, a) * CXUI_FIX;
        pos[a]   = rp[a] + s0;
        len[a]   = max(rl[a] - s0 - s1, 0);
    }
}

static CxUiLayoutRect rectOf(int32 pos[2], int32 len[2])
{
    return (CxUiLayoutRect){ pos[0], pos[1], len[0], len[1] };
}

// ---- background ----

_Use_decl_annotations_
CxUiColor _cxuiBackgroundOf(CxUiWidget* w)
{
    for (CxUiWidget* a = w; a; a = a->parent) {
        CxUiPanel* p = objDynCast(CxUiPanel, a);
        if (p && p->_bg.bits)
            return p->_bg;
        if (p && p->_bgRole != BG_AROUND)
            return cxuiPaletteColor(cxuiWidgetPalette(a), p->_bgRole);
        // The tabs' pages have a background of their own; their strip shows what is around it.
        CxUiTabs* t = a != w ? objDynCast(CxUiTabs, a) : NULL;
        if (t)
            return cxuiPaletteColor(cxuiWidgetPalette(a), t->_bgRole);
    }
    return cxuiPaletteColor(cxuiWidgetPalette(w), CXUI_PAL_Panel);
}

// ---- CxUiPanel ----

static void panelSetup(CxUiPanel* self, const CxUiPanelDesc* desc, bool frame)
{
    CxUiBox* box      = CxUiBox(self);
    CxUiBoxBase* base = CxUiBoxBase(self);
    self->_bgRole     = frame ? BG_AROUND : CXUI_PAL_Panel;
    self->_border     = frame ? 1 : 0;
    if (desc) {
        box->_orient    = desc->orient == CXUI_ORIENT_V ? CXUI_ORIENT_V : CXUI_ORIENT_H;
        box->_align[0]  = desc->align[0];
        box->_align[1]  = desc->align[1];
        box->_normalize = desc->normalize & CXUI_EXPAND_Both;
        base->_margin   = desc->margin;
        base->_gap      = max(desc->gap, 0);
        if (desc->bgRole && desc->bgRole < CXUI_PAL_Count)
            self->_bgRole = desc->bgRole;
        self->_bg = desc->bg;
        if (desc->border)
            self->_border = max(desc->border, 0);
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }
    base->_inset = self->_border;
}

_objfactory_guaranteed CxUiPanel* CxUiPanel_createWith(_In_opt_ const CxUiPanelDesc* desc)
{
    CxUiPanel* self;
    self = objInstCreate(CxUiPanel);
    panelSetup(self, desc, false);

    objInstInit(self);
    return self;
}

void CxUiPanel_setBgRole(_In_ CxUiPanel* self, uint32 role)
{
    if (!live(self) || role >= CXUI_PAL_Count)
        return;
    self->_bgRole = role;
    _cxuiRestyleTree(CxUiWidget(self));
}

void CxUiPanel_setBg(_In_ CxUiPanel* self, CxUiColor c)
{
    if (!live(self))
        return;
    self->_bg = c;
    _cxuiRestyleTree(CxUiWidget(self));
}

extern void CxUiWidget_realizePeer(_In_ CxUiWidget* self, _In_ CxUiPeer* host);   // parent
#define parent_realizePeer(host) CxUiWidget_realizePeer((CxUiWidget*)(self), host)
void CxUiPanel_realizePeer(_In_ CxUiPanel* self, _In_ CxUiPeer* host)
{
    if (CxUiWidget(self)->peer || !_cxui.backend->panelCreate)
        return;
    CxUiPeer* p = _cxui.backend->panelCreate(CxUiWidget(self), host);
    if (p)
        _cxuiPeerAttach(CxUiWidget(self), p);
}

void CxUiPanel_restyle(_In_ CxUiPanel* self);

extern void CxUiWidget_placePeer(_In_ CxUiWidget* self, CxUiRect px);   // parent
#define parent_placePeer(px) CxUiWidget_placePeer((CxUiWidget*)(self), px)
void CxUiPanel_placePeer(_In_ CxUiPanel* self, CxUiRect px)
{
    CxUiPeer* p = CxUiWidget(self)->peer;
    if (p) {
        _cxui.backend->panelSetBounds(p, px);
        // The border's width in pixels follows the scale.
        cxuiWidgetRestyle(self);
    }
}

extern void CxUiWidget_restyle(_In_ CxUiWidget* self);   // parent
#define parent_restyle() CxUiWidget_restyle((CxUiWidget*)(self))
void CxUiPanel_restyle(_In_ CxUiPanel* self)
{
    CxUiPeer* p = CxUiWidget(self)->peer;
    if (!p)
        return;
    CxUiColor border = cxuiPaletteColor(cxuiWidgetPalette(self), CXUI_PAL_Border);
    int32 borderPx   = self->_border > 0 ? max(cxuiPx(self, self->_border), 1) : 0;
    _cxui.backend->panelSetColors(p, _cxuiBackgroundOf(CxUiWidget(self)), border, borderPx);
}

// ---- CxUiFrame ----

// The title's gap starts this far in from the frame's left edge, with this much room each side of
// the text, in DIPs.
#define TITLE_INDENT 8
#define TITLE_GAP    3

_objfactory_guaranteed CxUiFrame* CxUiFrame_createWith(_In_opt_ const CxUiPanelDesc* desc)
{
    CxUiFrame* self;
    self = objInstCreate(CxUiFrame);
    panelSetup(CxUiPanel(self), desc, true);
    if (desc)
        strDup(&self->_title, desc->title);

    objInstInit(self);
    return self;
}

strref CxUiFrame_title(_In_ CxUiFrame* self)
{
    return live(self) && !strEmpty(self->_title) ? (strref)self->_title : NULL;
}

void CxUiFrame_setTitle(_In_ CxUiFrame* self, _In_opt_ strref title)
{
    if (!live(self) || strEq(title, self->_title))
        return;
    strDup(&self->_title, title);
    cxuiWidgetRestyle(self);
    _cxuiLayoutDirty(CxUiWidget(self));
}

// A title takes a line at the top, the border running through the middle of it, and at least its
// width with the gap around it.
extern CxUiSize CxUiBox_measure(_In_ CxUiBox* self);   // parent
#define parent_measure() CxUiBox_measure((CxUiBox*)(self))
CxUiSize CxUiFrame_measure(_In_ CxUiFrame* self)
{
    CxUiBoxBase* base = CxUiBoxBase(self);
    CxUiSize title    = { 0 };
    if (!strEmpty(self->_title)) {
        CxUiFont* f = cxuiWidgetFont(self);
        float32 s   = cxuiFontScale(f);
        CxUiFontMetrics fm;
        cxuiFontMetrics(f, &fm);
        title = (CxUiSize){ (int32)ceilf(cxuiFontAdvance(f, self->_title) / s), (int32)ceilf(fm.height / s) };
    }
    base->_insetTop = max(title.h - base->_inset, 0);
    CxUiSize n      = parent_measure();
    if (title.w > 0)
        n.w = max(n.w, title.w + 2 * (TITLE_INDENT + TITLE_GAP));
    return n;
}

extern void CxUiPanel_restyle(_In_ CxUiPanel* self);   // parent
#undef parent_restyle
#define parent_restyle() CxUiPanel_restyle((CxUiPanel*)(self))
void CxUiFrame_restyle(_In_ CxUiFrame* self)
{
    parent_restyle();
    CxUiPeer* p = CxUiWidget(self)->peer;
    if (!p || !_cxui.backend->panelSetTitle)
        return;
    CxUiFont* f = cxuiWidgetFont(self);
    if (f != self->_peerFont) {
        objRelease(&self->_peerFont);
        self->_peerFont = objAcquire(f);
    }
    bool off    = (CxUiWidget(self)->state & CXUI_WS_Inactive) != 0;
    CxUiColor c = cxuiPaletteColor(cxuiWidgetPalette(self), off ? CXUI_PAL_TextDisabled : CXUI_PAL_Text);
    _cxui.backend->panelSetTitle(p, self->_title, f, c, cxuiPx(self, TITLE_INDENT), cxuiPx(self, TITLE_GAP));
}

void CxUiFrame_destroy(_In_ CxUiFrame* self)
{
    // Autogen begins -----
    strDestroy(&self->_title);
    objRelease(&self->_peerFont);
    // Autogen ends -------
}

// ---- CxUiScrollBox ----

_objfactory_guaranteed CxUiScrollBox* CxUiScrollBox_createWith(_In_opt_ const CxUiScrollBoxDesc* desc)
{
    CxUiScrollBox* self;
    self              = objInstCreate(CxUiScrollBox);
    CxUiPanel* panel  = CxUiPanel(self);
    CxUiBox* box      = CxUiBox(self);
    CxUiBoxBase* base = CxUiBoxBase(self);
    panel->_bgRole    = BG_AROUND;
    self->_axes       = CXUI_EXPAND_V;
    if (desc) {
        box->_orient   = desc->orient == CXUI_ORIENT_V ? CXUI_ORIENT_V : CXUI_ORIENT_H;
        box->_align[0] = desc->align[0];
        box->_align[1] = desc->align[1];
        base->_margin  = desc->margin;
        base->_gap     = max(desc->gap, 0);
        if (desc->scroll & CXUI_EXPAND_Both)
            self->_axes = desc->scroll & CXUI_EXPAND_Both;
        self->_view = (CxUiSize){ max(desc->view.w, 0), max(desc->view.h, 0) };
        if (desc->bgRole && desc->bgRole < CXUI_PAL_Count)
            panel->_bgRole = desc->bgRole;
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }

    objInstInit(self);
    return self;
}

// Where a widget inside is on the content, in physical pixels
static bool contentRect(CxUiScrollBox* self, CxUiWidget* w, CxUiRect* out)
{
    CxUiWidget* a = w;
    while (a && a != CxUiWidget(self))
        a = a->parent;
    if (!a || !(w->state & CXUI_WS_Placed))
        return false;
    CxUiRect o = CxUiWidget(self)->_bounds;
    *out       = (CxUiRect){ w->_bounds.x - o.x, w->_bounds.y - o.y, w->_bounds.w, w->_bounds.h };
    return true;
}

void CxUiScrollBox_show(_In_ CxUiScrollBox* self, _In_ CxUiWidget* w)
{
    CxUiRect r;
    CxUiPeer* p = CxUiWidget(self)->peer;
    if (!live(self) || !p || !_cxui.backend->panelScrollTo)
        return;
    _cxuiRefresh(CxUiWidget(self));
    if (_cxuiLive(w) && contentRect(self, w, &r))
        _cxui.backend->panelScrollTo(p, r);
}

// Along the axes that scroll, the view asked for, or the content's natural size; along the
// others, the content's.
extern CxUiSize CxUiBox_measure(_In_ CxUiBox* self);   // parent
#undef parent_measure
#define parent_measure() CxUiBox_measure((CxUiBox*)(self))
CxUiSize CxUiScrollBox_measure(_In_ CxUiScrollBox* self)
{
    CxUiSize n            = parent_measure();
    self->_contentNatural = n;
    if ((self->_axes & CXUI_EXPAND_H) && self->_view.w > 0)
        n.w = self->_view.w;
    if ((self->_axes & CXUI_EXPAND_V) && self->_view.h > 0) {
        n.h                    = self->_view.h;
        CxUiWidget(self)->_hfw = false;
    }
    return n;
}

extern int32 CxUiBox_heightFor(_In_ CxUiBox* self, int32 width);   // parent
#define parent_heightFor(width) CxUiBox_heightFor((CxUiBox*)(self), width)
int32 CxUiScrollBox_heightFor(_In_ CxUiScrollBox* self, int32 width)
{
    if ((self->_axes & CXUI_EXPAND_V) && self->_view.h > 0)
        return self->_view.h;
    return parent_heightFor(width);
}

// The content's height at a width, in DIPs
static int32 contentHeight(CxUiScrollBox* self, int32 width)
{
    return CxUiWidget(self)->_hfw ? CxUiBox_heightFor(CxUiBox(self), width) : self->_contentNatural.h;
}

// The children are laid out over the content, which starts where the box does: their peers are
// on the content's surface, which scrolls without moving them.
extern void CxUiBox_arrange(_In_ CxUiBox* self);   // parent
#define parent_arrange() CxUiBox_arrange((CxUiBox*)(self))
void CxUiScrollBox_arrange(_In_ CxUiScrollBox* self)
{
    CxUiWidget* w    = CxUiWidget(self);
    CxUiLayoutRect r = w->_lrect;
    bool sh = (self->_axes & CXUI_EXPAND_H) != 0, sv = (self->_axes & CXUI_EXPAND_V) != 0;
    int32 vw = r.w / CXUI_FIX, vh = r.h / CXUI_FIX;
    int32 bar = _cxui.backend->scrollBarSize ? _cxui.backend->scrollBarSize() : 0;
    // A vertical scroll bar takes room across, which can make the content taller still.
    int32 cw = sh ? max(self->_contentNatural.w, vw) : vw;
    int32 ch = contentHeight(self, cw);
    if (sv && ch > vh && bar > 0) {
        cw = sh ? max(self->_contentNatural.w, vw - bar) : vw - bar;
        ch = contentHeight(self, cw);
    }
    if (sh && cw > vw && bar > 0)
        vh -= bar;
    ch = sv ? max(ch, vh) : vh;
    cw = max(cw, 0);
    ch = max(ch, 0);

    w->_lrect = (CxUiLayoutRect){ r.x, r.y, cw * CXUI_FIX, ch * CXUI_FIX };
    parent_arrange();
    w->_lrect        = r;
    self->_contentPx = (CxUiSize){ cxuiPx(self, cw), cxuiPx(self, ch) };
    if (w->peer && _cxui.backend->panelSetContent)
        _cxui.backend->panelSetContent(w->peer, self->_contentPx);
}

// Laid out before the peer exists, the content's size goes to it once it does.
extern void CxUiPanel_realizePeer(_In_ CxUiPanel* self, _In_ CxUiPeer* host);   // parent
#undef parent_realizePeer
#define parent_realizePeer(host) CxUiPanel_realizePeer((CxUiPanel*)(self), host)
void CxUiScrollBox_realizePeer(_In_ CxUiScrollBox* self, _In_ CxUiPeer* host)
{
    parent_realizePeer(host);
    CxUiPeer* p = CxUiWidget(self)->peer;
    if (p && _cxui.backend->panelSetContent)
        _cxui.backend->panelSetContent(p, self->_contentPx);
}

_Use_decl_annotations_
void _cxuiScrollIntoView(CxUiWidget* w)
{
    for (CxUiWidget* a = w->parent; a; a = a->parent) {
        CxUiScrollBox* sb = objDynCast(CxUiScrollBox, a);
        CxUiRect r;
        if (sb && a->peer && _cxui.backend->panelScrollTo && contentRect(sb, w, &r))
            _cxui.backend->panelScrollTo(a->peer, r);
    }
}

// ---- CxUiGridBox ----

_objfactory_guaranteed CxUiGridBox* CxUiGridBox_createWith(_In_opt_ const CxUiGridBoxDesc* desc)
{
    CxUiGridBox* self;
    self         = objInstCreate(CxUiGridBox);
    self->_ncols = 1;
    if (desc) {
        self->_ncols                  = max(desc->ncols, 1);
        CxUiBoxBase(self)->_gap       = max(desc->colGap, 0);
        self->_rowGap                 = max(desc->rowGap, 0);
        CxUiBoxBase(self)->_margin    = desc->margin;
        CxUiWidget(self)->_lp         = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }

    objInstInit(self);
    return self;
}

// Column widths and row heights in DIPs from the visible children, and the axes each column and
// row expands in. The caller frees the arrays.
typedef struct GridDims {
    int32 ncols, nrows, n;
    int32* size[2];       // per column (0) and per row (1)
    bool* expands[2];
} GridDims;

static void gridMeasure(CxUiGridBox* self, GridDims* g)
{
    CxUiBoxBase* base = CxUiBoxBase(self);
    g->n              = 0;
    for (int32 i = 0; i < saSize(base->_children); i++)
        g->n += _cxuiLayoutTakesSpace(base->_children.a[i]) ? 1 : 0;
    g->ncols   = min(self->_ncols, max(g->n, 1));
    g->nrows   = (g->n + g->ncols - 1) / g->ncols;
    int32 cnt[2] = { g->ncols, max(g->nrows, 1) };
    for (int a = 0; a < 2; a++) {
        g->size[a]    = xaAlloc(cnt[a] * sizeof(int32), XA_Zero);
        g->expands[a] = xaAlloc(cnt[a] * sizeof(bool), XA_Zero);
    }
    int32 k = 0;
    for (int32 i = 0; i < saSize(base->_children); i++) {
        CxUiWidget* child = base->_children.a[i];
        if (!_cxuiLayoutTakesSpace(child))
            continue;
        CxUiSize s = _cxuiLayoutNatural(child);
        int32 cell[2] = { k % g->ncols, k / g->ncols };
        for (int a = 0; a < 2; a++) {
            g->size[a][cell[a]] = max(g->size[a][cell[a]], axisLen(s, a));
            if (_cxuiLayoutExpand(child, 0) & axisBit(a))
                g->expands[a][cell[a]] = true;
        }
        k++;
    }
}

static void gridFree(GridDims* g)
{
    for (int a = 0; a < 2; a++) {
        xaFree(g->size[a]);
        xaFree(g->expands[a]);
    }
}

extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#undef parent_measure
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiGridBox_measure(_In_ CxUiGridBox* self)
{
    CxUiBoxBase* base = CxUiBoxBase(self);
    GridDims g;
    gridMeasure(self, &g);
    int32 out[2];
    int32 cnt[2] = { g.ncols, g.nrows }, gaps[2] = { base->_gap, self->_rowGap };
    for (int a = 0; a < 2; a++) {
        int32 sum = 0;
        for (int32 i = 0; i < cnt[a]; i++) {
            sum += g.size[a][i];
            if (g.expands[a][i])
                CxUiWidget(self)->_expand |= axisBit(a);
        }
        out[a] = marginStart(base, a) + marginEnd(base, a) + sum + (cnt[a] > 0 ? gaps[a] * (cnt[a] - 1) : 0);
    }
    gridFree(&g);
    return (CxUiSize){ g.n ? out[0] : marginStart(base, 0) + marginEnd(base, 0),
                       g.n ? out[1] : marginStart(base, 1) + marginEnd(base, 1) };
}

// One axis's sizes, in DIPs, made CXUI_FIX units with the spare space of len shared by the
// columns or rows that expand
static void gridShare(GridDims* g, int a, int32 len, int32 gap)
{
    int32 cnt   = a == 0 ? g->ncols : g->nrows;
    int32 total = cnt > 0 ? gap * (cnt - 1) : 0, nexp = 0, last = -1;
    for (int32 i = 0; i < cnt; i++) {
        g->size[a][i] *= CXUI_FIX;
        total += g->size[a][i];
        if (g->expands[a][i]) {
            nexp++;
            last = i;
        }
    }
    int32 spare = len - total;
    if (spare > 0 && nexp > 0) {
        int32 share = spare / nexp;
        for (int32 i = 0; i < cnt; i++) {
            if (g->expands[a][i])
                g->size[a][i] += share;
        }
        g->size[a][last] += spare - share * nexp;
    }
}

// The length a child gets in its cell along an axis, in CXUI_FIX units
static int32 cellLen(CxUiWidget* child, CxUiSize nat, int a, int32 slot)
{
    uint32 align = child->_lp.align[a];
    return (_cxuiLayoutExpand(child, 0) & axisBit(a)) || align == CXUI_ALIGN_Fill ? slot
                                                                                 : min(axisLen(nat, a) * CXUI_FIX, slot);
}

// Row heights in DIPs again once the columns are shared out, with children whose height depends
// on their width measured at their column's width
static void gridRowsFor(CxUiGridBox* self, GridDims* g)
{
    CxUiBoxBase* base = CxUiBoxBase(self);
    memset(g->size[1], 0, max(g->nrows, 1) * sizeof(int32));
    int32 k = 0;
    for (int32 i = 0; i < saSize(base->_children); i++) {
        CxUiWidget* child = base->_children.a[i];
        if (!_cxuiLayoutTakesSpace(child))
            continue;
        int32 col = k % g->ncols, row = k / g->ncols;
        CxUiSize nat = _cxuiLayoutNatural(child);
        int32 h      = child->_hfw ? _cxuiLayoutHeightFor(child, cellLen(child, nat, 0, g->size[0][col]) / CXUI_FIX) : nat.h;
        g->size[1][row] = max(g->size[1][row], h);
        k++;
    }
}

extern int32 CxUiWidget_heightFor(_In_ CxUiWidget* self, int32 width);   // parent
#undef parent_heightFor
#define parent_heightFor(width) CxUiWidget_heightFor((CxUiWidget*)(self), width)
int32 CxUiGridBox_heightFor(_In_ CxUiGridBox* self, int32 width)
{
    CxUiBoxBase* base = CxUiBoxBase(self);
    GridDims g;
    gridMeasure(self, &g);
    int32 inner = max(width - marginStart(base, 0) - marginEnd(base, 0), 0) * CXUI_FIX;
    gridShare(&g, 0, inner, base->_gap * CXUI_FIX);
    gridRowsFor(self, &g);
    int32 h = marginStart(base, 1) + marginEnd(base, 1);
    for (int32 i = 0; i < g.nrows; i++)
        h += g.size[1][i];
    h += g.nrows > 0 ? self->_rowGap * (g.nrows - 1) : 0;
    gridFree(&g);
    return h;
}

extern void CxUiWidget_arrange(_In_ CxUiWidget* self);   // parent
#undef parent_arrange
#define parent_arrange() CxUiWidget_arrange((CxUiWidget*)(self))
void CxUiGridBox_arrange(_In_ CxUiGridBox* self)
{
    CxUiBoxBase* base = CxUiBoxBase(self);
    GridDims g;
    gridMeasure(self, &g);
    int32 pos[2], len[2];
    innerOf(base, pos, len);
    int32 cnt[2] = { g.ncols, g.nrows }, gaps[2] = { base->_gap * CXUI_FIX, self->_rowGap * CXUI_FIX };

    // Columns first, then rows: a child whose height depends on its width is measured at its
    // column's width. Sizes are then in CXUI_FIX units; where each starts follows.
    gridShare(&g, 0, len[0], gaps[0]);
    if (CxUiWidget(self)->_hfw)
        gridRowsFor(self, &g);
    gridShare(&g, 1, len[1], gaps[1]);
    int32* at[2];
    for (int a = 0; a < 2; a++) {
        at[a]   = xaAlloc(max(cnt[a], 1) * sizeof(int32));
        int32 p = pos[a];
        for (int32 i = 0; i < cnt[a]; i++) {
            at[a][i] = p;
            p += g.size[a][i] + gaps[a];
        }
    }

    int32 k = 0;
    for (int32 i = 0; i < saSize(base->_children); i++) {
        CxUiWidget* child = base->_children.a[i];
        if (!_cxuiLayoutTakesSpace(child))
            continue;
        CxUiSize nat  = _cxuiLayoutNatural(child);
        int32 cell[2] = { k % g.ncols, k / g.ncols };
        int32 cpos[2], clen[2];
        for (int a = 0; a < 2; a++) {
            int32 slot   = g.size[a][cell[a]];
            uint32 align = child->_lp.align[a];
            if (a == 1 && child->_hfw)
                nat.h = _cxuiLayoutHeightFor(child, clen[0] / CXUI_FIX);
            clen[a] = cellLen(child, nat, a, slot);
            cpos[a] = at[a][cell[a]];
            if (align == CXUI_ALIGN_Center)
                cpos[a] += (slot - clen[a]) / 2;
            else if (align == CXUI_ALIGN_End)
                cpos[a] += slot - clen[a];
        }
        _cxuiLayoutPlace(child, rectOf(cpos, clen));
        k++;
    }

    xaFree(at[0]);
    xaFree(at[1]);
    gridFree(&g);
}

bool CxUiGridBox_append(_In_ CxUiGridBox* self, _In_ CxUiWidget* child)
{
    return _cxuiBoxBaseInsert(CxUiBoxBase(self), child, saSize(CxUiBoxBase(self)->_children));
}

bool CxUiGridBox_insert(_In_ CxUiGridBox* self, _In_ CxUiWidget* child, int32 at)
{
    return _cxuiBoxBaseInsert(CxUiBoxBase(self), child, at);
}

bool CxUiGridBox_detach(_In_ CxUiGridBox* self, _In_ CxUiWidget* child)
{
    return _cxuiBoxBaseDetach(CxUiBoxBase(self), child);
}

int32 CxUiGridBox_childCount(_In_ CxUiGridBox* self)
{
    return saSize(CxUiBoxBase(self)->_children);
}

_Ret_opt_valid_ CxUiWidget* CxUiGridBox_child(_In_ CxUiGridBox* self, int32 i)
{
    return _cxuiBoxBaseChild(CxUiBoxBase(self), i);
}

// ---- CxUiZBox ----

_objfactory_guaranteed CxUiZBox* CxUiZBox_createWith(_In_opt_ const CxUiZBoxDesc* desc)
{
    CxUiZBox* self;
    self = objInstCreate(CxUiZBox);
    if (desc) {
        self->_flags          = desc->flags;
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }

    objInstInit(self);
    return self;
}

static int32 zboxCount(CxUiZBox* self)
{
    return saSize(CxUiBoxBase(self)->_children);
}

void CxUiZBox_setActive(_In_ CxUiZBox* self, int32 index)
{
    if (!live(self) || index < 0 || index >= zboxCount(self) || index == self->_active)
        return;
    self->_active = index;
    _cxuiLayoutNeedWalk(CxUiWidget(self));
    CxUiTabs* tabs = objDynCast(CxUiTabs, self);
    if (tabs)
        _cxuiTabsSync(tabs);
}

int32 CxUiZBox_active(_In_ CxUiZBox* self)
{
    return live(self) && zboxCount(self) > 0 ? self->_active : -1;
}

extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#undef parent_measure
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiZBox_measure(_In_ CxUiZBox* self)
{
    CxUiBoxBase* base = CxUiBoxBase(self);
    CxUiSize out      = { 0 };
    for (int32 i = 0; i < saSize(base->_children); i++) {
        CxUiWidget* child = base->_children.a[i];
        if (!_cxuiLayoutTakesSpace(child) || ((self->_flags & CXUI_ZBOX_SizeToActive) && i != self->_active))
            continue;
        CxUiSize s = _cxuiLayoutNatural(child);
        out.w      = max(out.w, s.w);
        out.h      = max(out.h, s.h);
        CxUiWidget(self)->_expand |= child->_expand;
    }
    return out;
}

// The pages measure counts, at the width they all get
extern int32 CxUiWidget_heightFor(_In_ CxUiWidget* self, int32 width);   // parent
#undef parent_heightFor
#define parent_heightFor(width) CxUiWidget_heightFor((CxUiWidget*)(self), width)
int32 CxUiZBox_heightFor(_In_ CxUiZBox* self, int32 width)
{
    CxUiBoxBase* base = CxUiBoxBase(self);
    int32 h           = 0;
    for (int32 i = 0; i < saSize(base->_children); i++) {
        CxUiWidget* child = base->_children.a[i];
        if (!_cxuiLayoutTakesSpace(child) || ((self->_flags & CXUI_ZBOX_SizeToActive) && i != self->_active))
            continue;
        h = max(h, _cxuiLayoutHeightFor(child, width));
    }
    return h;
}

// Every child is placed, shown or not, so a page is ready the moment it is shown, and one marked
// to be created early can be.
extern void CxUiWidget_arrange(_In_ CxUiWidget* self);   // parent
#undef parent_arrange
#define parent_arrange() CxUiWidget_arrange((CxUiWidget*)(self))
void CxUiZBox_arrange(_In_ CxUiZBox* self)
{
    CxUiBoxBase* base = CxUiBoxBase(self);
    for (int32 i = 0; i < saSize(base->_children); i++) {
        CxUiWidget* child = base->_children.a[i];
        if (_cxuiLayoutTakesSpace(child))
            _cxuiLayoutPlace(child, CxUiWidget(self)->_lrect);
    }
}

extern bool CxUiWidget_showsChild(_In_ CxUiWidget* self, _In_ CxUiWidget* child);   // parent
#define parent_showsChild(child) CxUiWidget_showsChild((CxUiWidget*)(self), child)
bool CxUiZBox_showsChild(_In_ CxUiZBox* self, _In_ CxUiWidget* child)
{
    int32 a = self->_active;
    return a >= 0 && a < zboxCount(self) && CxUiBoxBase(self)->_children.a[a] == child;
}

bool CxUiZBox_append(_In_ CxUiZBox* self, _In_ CxUiWidget* child)
{
    return _cxuiBoxBaseInsert(CxUiBoxBase(self), child, zboxCount(self));
}

bool CxUiZBox_insert(_In_ CxUiZBox* self, _In_ CxUiWidget* child, int32 at)
{
    int32 n = zboxCount(self);
    at      = clamp(at, 0, n);
    if (!_cxuiBoxBaseInsert(CxUiBoxBase(self), child, at))
        return false;
    // The shown child stays shown.
    if (n > 0 && at <= self->_active)
        self->_active++;
    return true;
}

bool CxUiZBox_detach(_In_ CxUiZBox* self, _In_ CxUiWidget* child)
{
    int32 i = saFind(CxUiBoxBase(self)->_children, object, child);
    if (i < 0 || !_cxuiBoxBaseDetach(CxUiBoxBase(self), child))
        return false;
    // The one after it takes over, or the last.
    if (i < self->_active || self->_active >= zboxCount(self))
        self->_active = max(self->_active - 1, 0);
    _cxuiLayoutNeedWalk(CxUiWidget(self));
    return true;
}

int32 CxUiZBox_childCount(_In_ CxUiZBox* self)
{
    return zboxCount(self);
}

_Ret_opt_valid_ CxUiWidget* CxUiZBox_child(_In_ CxUiZBox* self, int32 i)
{
    return _cxuiBoxBaseChild(CxUiBoxBase(self), i);
}

// ---- CxUiSplit ----

_objfactory_guaranteed CxUiSplit* CxUiSplit_createWith(_In_opt_ const CxUiSplitDesc* desc)
{
    CxUiSplit* self;
    self            = objInstCreate(CxUiSplit);
    self->_value    = 0.5f;
    self->_sashSize = 5;
    bool grip       = false;
    if (desc) {
        self->_orient = desc->orient == CXUI_ORIENT_V ? CXUI_ORIENT_V : CXUI_ORIENT_H;
        if (desc->value > 0)
            self->_value = min(desc->value, 1.0f);
        for (int i = 0; i < 2; i++) {
            self->_minPane[i] = max(desc->minPane[i], 0);
            self->_maxPane[i] = max(desc->maxPane[i], 0);
        }
        if (desc->sash > 0)
            self->_sashSize = desc->sash;
        grip                  = desc->grip;
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }

    objInstInit(self);
    self->_sash                   = cxuiSashCreate(self->_orient, grip);
    CxUiWidget(self->_sash)->parent = CxUiWidget(self);
    _cxuiLayoutAttached(CxUiWidget(self), CxUiWidget(self->_sash));
    return self;
}

void CxUiSplit_setValue(_In_ CxUiSplit* self, float32 value)
{
    if (!live(self))
        return;
    self->_value = clamp(value, 0.0f, 1.0f);
    _cxuiLayoutDirty(CxUiWidget(self));
}

float32 CxUiSplit_value(_In_ CxUiSplit* self)
{
    return live(self) ? self->_value : 0;
}

CxUiSash* CxUiSplit_sash(_In_ CxUiSplit* self)
{
    return self->_sash;
}

extern uint32 CxUiWidget_eventMask(_In_ CxUiWidget* self);   // parent
#define parent_eventMask() CxUiWidget_eventMask((CxUiWidget*)(self))
uint32 CxUiSplit_eventMask(_In_ CxUiSplit* self)
{
    return parent_eventMask() | (1 << CXUI_EV_Change);
}

// The panes taking space, up to two
static int32 splitPanes(CxUiSplit* self, CxUiWidget* panes[2])
{
    CxUiBoxBase* base = CxUiBoxBase(self);
    int32 n           = 0;
    for (int32 i = 0; i < saSize(base->_children); i++) {
        if (_cxuiLayoutTakesSpace(base->_children.a[i]))
            panes[n++] = base->_children.a[i];
    }
    return n;
}

// The first pane's size when the panes share avail (all in 1/256 DIPs): the split's fraction,
// within the first pane's limits and the second's turned into limits on the first; a minimum
// wins over a maximum.
static int32 splitFirstFor(CxUiSplit* self, CxUiWidget* panes[2], int m, int32 avail)
{
    int32 lo[2], hi[2];
    for (int i = 0; i < 2; i++) {
        lo[i] = max(self->_minPane[i], _cxuiLenDip(panes[i], panes[i]->_lp.minSize[m], m)) * CXUI_FIX;
        hi[i] = self->_maxPane[i] > 0 ? self->_maxPane[i] * CXUI_FIX : avail;
    }
    int32 first = (int32)floor((float64)avail * self->_value + 0.5);
    first       = min(first, min(hi[0], avail - lo[1]));
    first       = max(first, max(lo[0], avail - hi[1]));
    return clamp(first, 0, avail);
}

// The least space for the panes, in DIPs, at which each gets the size it needs with the space
// shared as arrange shares it; the plain total where no size does that (a fraction at an end, or
// a maximum in the way).
static int32 splitFitAvail(CxUiSplit* self, CxUiWidget* panes[2], int m, const int32 need[2])
{
    int32 total = need[0] + need[1];
    float64 v   = self->_value;
    float64 most = total;
    if (v > 0)
        most = max(most, need[0] / v);
    if (v < 1)
        most = max(most, need[1] / (1 - v));
    int32 hiA = (int32)min(ceil(most) + 2, 1000000.0);
#define SPLIT_FITS(a)                                                                              \
    (splitFirstFor(self, panes, m, (a) * CXUI_FIX) >= need[0] * CXUI_FIX &&                        \
     (a) * CXUI_FIX - splitFirstFor(self, panes, m, (a) * CXUI_FIX) >= need[1] * CXUI_FIX)
    if (!SPLIT_FITS(hiA))
        return total;
    int32 loA = total;
    while (loA < hiA) {
        int32 mid = loA + (hiA - loA) / 2;
        if (SPLIT_FITS(mid))
            hiA = mid;
        else
            loA = mid + 1;
    }
#undef SPLIT_FITS
    return hiA;
}

extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#undef parent_measure
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiSplit_measure(_In_ CxUiSplit* self)
{
    CxUiBoxBase* base = CxUiBoxBase(self);
    int m = self->_orient == CXUI_ORIENT_V ? 1 : 0, c = 1 - m;
    CxUiWidget* panes[2];
    int32 n = splitPanes(self, panes), need[2] = { 0, 0 }, maxC = 0;
    for (int32 i = 0; i < n; i++) {
        CxUiSize s = _cxuiLayoutNatural(panes[i]);
        need[i]    = max(axisLen(s, m), self->_minPane[i]);
        maxC       = max(maxC, axisLen(s, c));
        CxUiWidget(self)->_expand |= panes[i]->_expand;
    }
    // The natural size, which is also a window's minimum, adds up the panes. Sized to fit, the
    // split is big enough that its fraction gives each pane its own.
    int32 sumM = need[0] + need[1];
    if (n == 2) {
        _cxuiLayoutSplitSeen();
        if (_cxuiLayoutFitting())
            sumM = splitFitAvail(self, panes, m, need);
    }
    int32 out[2];
    out[m] = marginStart(base, m) + marginEnd(base, m) + sumM + (n == 2 ? self->_sashSize : 0);
    out[c] = marginStart(base, c) + marginEnd(base, c) + maxC;
    return (CxUiSize){ out[0], out[1] };
}

// Side by side, the panes take the widths the fraction gives them; one over the other, the whole
// width, and their heights add up.
extern int32 CxUiWidget_heightFor(_In_ CxUiWidget* self, int32 width);   // parent
#undef parent_heightFor
#define parent_heightFor(width) CxUiWidget_heightFor((CxUiWidget*)(self), width)
int32 CxUiSplit_heightFor(_In_ CxUiSplit* self, int32 width)
{
    CxUiBoxBase* base = CxUiBoxBase(self);
    int m = self->_orient == CXUI_ORIENT_V ? 1 : 0;
    CxUiWidget* panes[2];
    int32 n     = splitPanes(self, panes), h = 0;
    int32 inner = max(width - marginStart(base, 0) - marginEnd(base, 0), 0);
    if (m == 0 && n == 2) {
        int32 avail = max(inner * CXUI_FIX - self->_sashSize * CXUI_FIX, 0);
        int32 first = splitFirstFor(self, panes, 0, avail);
        h           = max(_cxuiLayoutHeightFor(panes[0], first / CXUI_FIX),
                          _cxuiLayoutHeightFor(panes[1], (avail - first) / CXUI_FIX));
    } else {
        for (int32 i = 0; i < n; i++) {
            int32 ph = _cxuiLayoutHeightFor(panes[i], inner);
            h        = m == 0 ? max(h, ph) : h + max(ph, self->_minPane[i]);
        }
        if (m == 1 && n == 2)
            h += self->_sashSize;
    }
    return h + marginStart(base, 1) + marginEnd(base, 1);
}

extern void CxUiWidget_arrange(_In_ CxUiWidget* self);   // parent
#undef parent_arrange
#define parent_arrange() CxUiWidget_arrange((CxUiWidget*)(self))
void CxUiSplit_arrange(_In_ CxUiSplit* self)
{
    int m = self->_orient == CXUI_ORIENT_V ? 1 : 0;
    int32 pos[2], len[2];
    innerOf(CxUiBoxBase(self), pos, len);
    CxUiWidget* panes[2];
    int32 n = splitPanes(self, panes);
    CxUiWidget* sash = CxUiWidget(self->_sash);

    if (n < 2) {
        // One pane takes everything; the sash has nothing to divide.
        if (n == 1)
            _cxuiLayoutPlace(panes[0], rectOf(pos, len));
        if (sash) {
            int32 zlen[2] = { len[0], len[1] };
            zlen[m]       = 0;
            _cxuiLayoutPlace(sash, rectOf(pos, zlen));
        }
        self->_avail = 0;
        return;
    }

    int32 sashFix = self->_sashSize * CXUI_FIX;
    int32 avail   = max(len[m] - sashFix, 0);
    self->_avail  = avail / CXUI_FIX;
    int32 first   = splitFirstFor(self, panes, m, avail);

    int32 plen[2] = { len[0], len[1] }, ppos[2] = { pos[0], pos[1] };
    plen[m]       = first;
    _cxuiLayoutPlace(panes[0], rectOf(ppos, plen));
    ppos[m] += first;
    plen[m] = sashFix;
    if (sash)
        _cxuiLayoutPlace(sash, rectOf(ppos, plen));
    ppos[m] += sashFix;
    plen[m] = avail - first;
    _cxuiLayoutPlace(panes[1], rectOf(ppos, plen));
}

// The panes are the first two children; the sash is always last.
bool CxUiSplit_append(_In_ CxUiSplit* self, _In_ CxUiWidget* child)
{
    if (saSize(CxUiBoxBase(self)->_children) >= 2)
        return false;
    return _cxuiBoxBaseInsert(CxUiBoxBase(self), child, saSize(CxUiBoxBase(self)->_children));
}

bool CxUiSplit_insert(_In_ CxUiSplit* self, _In_ CxUiWidget* child, int32 at)
{
    if (saSize(CxUiBoxBase(self)->_children) >= 2)
        return false;
    return _cxuiBoxBaseInsert(CxUiBoxBase(self), child, at);
}

bool CxUiSplit_detach(_In_ CxUiSplit* self, _In_ CxUiWidget* child)
{
    if (!child || child != CxUiWidget(self->_sash))
        return _cxuiBoxBaseDetach(CxUiBoxBase(self), child);
    CxUiSash* sash = self->_sash;
    self->_sash    = NULL;
    child->parent  = NULL;
    _cxuiLayoutDetached(CxUiWidget(self), child);
    objRelease(&sash);
    return true;
}

int32 CxUiSplit_childCount(_In_ CxUiSplit* self)
{
    return saSize(CxUiBoxBase(self)->_children) + (self->_sash ? 1 : 0);
}

_Ret_opt_valid_ CxUiWidget* CxUiSplit_child(_In_ CxUiSplit* self, int32 i)
{
    int32 n = saSize(CxUiBoxBase(self)->_children);
    return i < n ? _cxuiBoxBaseChild(CxUiBoxBase(self), i) : i == n && self->_sash ? CxUiWidget(self->_sash) : NULL;
}

// The sash calls this as it is dragged: the first pane's new size in DIPs.
_Use_decl_annotations_
void _cxuiSplitDragTo(CxUiSplit* self, int32 first)
{
    if (self->_avail <= 0)
        return;
    self->_value = clamp((float32)first / self->_avail, 0.0f, 1.0f);
    _cxuiLayoutDirty(CxUiWidget(self));
    cxuiRefresh(self);
}

// The first pane's size now, in DIPs
_Use_decl_annotations_
int32 _cxuiSplitFirst(CxUiSplit* self)
{
    CxUiWidget* panes[2];
    if (splitPanes(self, panes) < 2)
        return 0;
    CxUiLayoutRect r = panes[0]->_lrect;
    return (self->_orient == CXUI_ORIENT_V ? r.h : r.w) / CXUI_FIX;
}

void CxUiSplit_destroy(_In_ CxUiSplit* self)
{
    // Autogen begins -----
    objRelease(&self->_sash);
    // Autogen ends -------
}

// ---- CxUiTabs ----

bool CxUiTabs_insert(_In_ CxUiTabs* self, _In_ CxUiWidget* child, int32 at);

_objfactory_guaranteed CxUiTabs* CxUiTabs_createWith(_In_opt_ const CxUiTabsDesc* desc)
{
    CxUiTabs* self;
    self          = objInstCreate(CxUiTabs);
    self->_bgRole = CXUI_PAL_Content;
    if (desc) {
        CxUiZBox(self)->_flags = desc->flags;
        if (desc->bgRole && desc->bgRole < CXUI_PAL_Count)
            self->_bgRole = desc->bgRole;
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }

    objInstInit(self);
    return self;
}

_objinit_guaranteed bool CxUiTabs_init(_In_ CxUiTabs* self)
{
    // Autogen begins -----
    saInit(&self->_titles, string, 1);
    return true;
    // Autogen ends -------
}

static int32 tabsActive(CxUiTabs* self)
{
    return zboxCount(CxUiZBox(self)) > 0 ? CxUiZBox(self)->_active : -1;
}

// A title as the platform's tabs show it
static void tabsShown(string* out, strref title)
{
    if (_cxui.backend->tabMnemonics)
        strDup(out, title);
    else
        _cxuiMnemonicStrip(out, title);
}

_Use_decl_annotations_
void _cxuiTabsSync(CxUiTabs* self)
{
    CxUiPeer* p = CxUiWidget(self)->peer;
    if (p)
        _cxui.backend->tabsSetValue(p, tabsActive(self));
}

bool CxUiTabs_add(_In_ CxUiTabs* self, _In_ CxUiWidget* page, _In_ strref title)
{
    int32 at = zboxCount(CxUiZBox(self));
    if (!CxUiTabs_insert(self, page, at))
        return false;
    CxUiTabs_setTitle(self, at, title);
    return true;
}

strref CxUiTabs_title(_In_ CxUiTabs* self, int32 index)
{
    if (!live(self) || index < 0 || index >= saSize(self->_titles) || strEmpty(self->_titles.a[index]))
        return NULL;
    return self->_titles.a[index];
}

void CxUiTabs_setTitle(_In_ CxUiTabs* self, int32 index, _In_opt_ strref title)
{
    if (!live(self) || index < 0 || index >= saSize(self->_titles) || strEq(title, self->_titles.a[index]))
        return;
    strDup(&self->_titles.a[index], title);
    if (CxUiWidget(self)->peer) {
        string shown = 0;
        tabsShown(&shown, title);
        _cxui.backend->tabsSetTitle(CxUiWidget(self)->peer, index, shown);
        strDestroy(&shown);
    }
    _cxuiLayoutDirty(CxUiWidget(self));
}

void CxUiTabs_setBgRole(_In_ CxUiTabs* self, uint32 role)
{
    if (!live(self) || role >= CXUI_PAL_Count)
        return;
    self->_bgRole = role;
    _cxuiRestyleTree(CxUiWidget(self));
}

extern bool CxUiZBox_append(_In_ CxUiZBox* self, _In_ CxUiWidget* child);   // parent
#define parent_append(child) CxUiZBox_append((CxUiZBox*)(self), child)
bool CxUiTabs_append(_In_ CxUiTabs* self, _In_ CxUiWidget* child)
{
    return CxUiTabs_insert(self, child, zboxCount(CxUiZBox(self)));
}

extern bool CxUiZBox_insert(_In_ CxUiZBox* self, _In_ CxUiWidget* child, int32 at);   // parent
#define parent_insert(child, at) CxUiZBox_insert((CxUiZBox*)(self), child, at)
bool CxUiTabs_insert(_In_ CxUiTabs* self, _In_ CxUiWidget* child, int32 at)
{
    at = clamp(at, 0, zboxCount(CxUiZBox(self)));
    if (!parent_insert(child, at))
        return false;
    saInsert(&self->_titles, at, strref, _S"");
    CxUiPeer* p = CxUiWidget(self)->peer;
    if (p) {
        _cxui.backend->tabsInsert(p, at, NULL);
        _cxuiTabsSync(self);
    }
    return true;
}

extern bool CxUiZBox_detach(_In_ CxUiZBox* self, _In_ CxUiWidget* child);   // parent
#define parent_detach(child) CxUiZBox_detach((CxUiZBox*)(self), child)
bool CxUiTabs_detach(_In_ CxUiTabs* self, _In_ CxUiWidget* child)
{
    int32 i = saFind(CxUiBoxBase(self)->_children, object, child);
    if (i < 0 || !parent_detach(child))
        return false;
    saRemove(&self->_titles, i);
    CxUiPeer* p = CxUiWidget(self)->peer;
    if (p) {
        _cxui.backend->tabsRemove(p, i);
        _cxuiTabsSync(self);
    }
    return true;
}

extern uint32 CxUiWidget_eventMask(_In_ CxUiWidget* self);   // parent
#undef parent_eventMask
#define parent_eventMask() CxUiWidget_eventMask((CxUiWidget*)(self))
uint32 CxUiTabs_eventMask(_In_ CxUiTabs* self)
{
    return parent_eventMask() | (1 << CXUI_EV_Change) | (1 << CXUI_EV_Key) | (1 << CXUI_EV_Focus);
}

static bool tabsMultiline(CxUiTabs* self)
{
    return (CxUiZBox(self)->_flags & CXUI_TABS_Multiline) != 0;
}

// The room around the pages, from the platform when it can say, or else an estimate from the
// font. Tabs on several lines wrap within `width` (DIPs; 0 for one line).
static CxUiEdges tabsInsets(CxUiTabs* self, int32 width, int32* stripWidth)
{
    CxUiFont* f = cxuiWidgetFont(self);
    CxUiPeer* p = CxUiWidget(self)->peer;
    if (p && f != self->_peerFont) {
        objRelease(&self->_peerFont);
        self->_peerFont = objAcquire(f);
        if (_cxui.backend->controlSetFont)
            _cxui.backend->controlSetFont(p, f);
    }
    if (_cxui.backend->tabsMeasure)
        return _cxui.backend->tabsMeasure(self, f, tabsMultiline(self) ? width : 0, stripWidth);
    string shown = 0;
    *stripWidth  = 0;
    float32 s    = cxuiFontScale(f);
    // Wrapped, each line takes the tabs that fit after those before
    int32 lines = 1, line = 0;
    for (int32 i = 0; i < saSize(self->_titles); i++) {
        _cxuiMnemonicStrip(&shown, self->_titles.a[i]);
        int32 tab = (int32)ceilf(cxuiFontAdvance(f, shown) / s) + 16;
        *stripWidth += tab;
        if (tabsMultiline(self) && width > 0 && line > 0 && line + tab > width) {
            lines++;
            line = 0;
        }
        line += tab;
    }
    strDestroy(&shown);
    CxUiFontMetrics fm;
    cxuiFontMetrics(f, &fm);
    return (CxUiEdges){ 1, lines * ((int32)ceilf(fm.height / s) + 8), 1, 1 };
}

// Room for the largest page, and for every tab unless the platform scrolls them
extern CxUiSize CxUiZBox_measure(_In_ CxUiZBox* self);   // parent
#undef parent_measure
#define parent_measure() CxUiZBox_measure((CxUiZBox*)(self))
CxUiSize CxUiTabs_measure(_In_ CxUiTabs* self)
{
    CxUiSize pages = parent_measure();
    int32 strip    = 0;
    if (!tabsMultiline(self)) {
        CxUiEdges in  = tabsInsets(self, 0, &strip);
        self->_insets = in;
        return (CxUiSize){ max(pages.w + in.left + in.right, strip), pages.h + in.top + in.bottom };
    }
    // Wrapped tabs take the pages' width, and as many lines as that needs
    CxUiEdges in            = tabsInsets(self, 0, &strip);
    int32 width             = pages.w + in.left + in.right;
    in                      = tabsInsets(self, width, &strip);
    self->_insets           = in;
    CxUiWidget(self)->_hfw  = true;
    return (CxUiSize){ width, pages.h + in.top + in.bottom };
}

extern int32 CxUiZBox_heightFor(_In_ CxUiZBox* self, int32 width);   // parent
#undef parent_heightFor
#define parent_heightFor(width) CxUiZBox_heightFor((CxUiZBox*)(self), width)
int32 CxUiTabs_heightFor(_In_ CxUiTabs* self, int32 width)
{
    int32 strip  = 0;
    CxUiEdges in = tabsMultiline(self) ? tabsInsets(self, width, &strip) : self->_insets;
    return parent_heightFor(max(width - in.left - in.right, 0)) + in.top + in.bottom;
}

// The pages fill the room inside the strip and border, a surface the peers inside them are placed
// on; when that moves within the window, so do they.
extern void CxUiZBox_arrange(_In_ CxUiZBox* self);   // parent
#undef parent_arrange
#define parent_arrange() CxUiZBox_arrange((CxUiZBox*)(self))
void CxUiTabs_arrange(_In_ CxUiTabs* self)
{
    CxUiWidget* w    = CxUiWidget(self);
    CxUiLayoutRect r = w->_lrect;
    if (tabsMultiline(self)) {
        int32 strip   = 0;
        self->_insets = tabsInsets(self, r.w / CXUI_FIX, &strip);
    }
    CxUiEdges in        = self->_insets;
    CxUiLayoutRect page = { r.x + in.left * CXUI_FIX, r.y + in.top * CXUI_FIX,
                            max(r.w - (in.left + in.right) * CXUI_FIX, 0),
                            max(r.h - (in.top + in.bottom) * CXUI_FIX, 0) };
    CxUiRect px         = _cxuiLayoutPx(page);
    CxUiRect was        = self->_pagePx;
    self->_pagePx       = px;
    if (px.x != was.x || px.y != was.y)
        _cxuiLayoutHostMoved(w);
    if (w->peer && (px.x != was.x || px.y != was.y || px.w != was.w || px.h != was.h))
        _cxui.backend->tabsSetPage(w->peer, (CxUiRect){ px.x - w->_bounds.x, px.y - w->_bounds.y, px.w, px.h });

    CxUiBoxBase* base = CxUiBoxBase(self);
    for (int32 i = 0; i < saSize(base->_children); i++) {
        CxUiWidget* child = base->_children.a[i];
        if (_cxuiLayoutTakesSpace(child))
            _cxuiLayoutPlace(child, page);
    }
}

extern void CxUiWidget_realizePeer(_In_ CxUiWidget* self, _In_ CxUiPeer* host);   // parent
#undef parent_realizePeer
#define parent_realizePeer(host) CxUiWidget_realizePeer((CxUiWidget*)(self), host)
void CxUiTabs_realizePeer(_In_ CxUiTabs* self, _In_ CxUiPeer* host)
{
    if (CxUiWidget(self)->peer || !_cxui.backend->tabsCreate)
        return;
    CxUiPeer* p = _cxui.backend->tabsCreate(self, host);
    if (!p)
        return;
    _cxuiPeerAttach(CxUiWidget(self), p);
    objRelease(&self->_peerFont);
    // The font first, so the tabs are measured in it; and the platform's own measure can differ
    // from the estimate the tabs were laid out with.
    int32 strip = 0;
    tabsInsets(self, 0, &strip);
    string shown = 0;
    for (int32 i = 0; i < saSize(self->_titles); i++) {
        tabsShown(&shown, self->_titles.a[i]);
        _cxui.backend->tabsInsert(p, i, shown);
    }
    strDestroy(&shown);
    _cxuiTabsSync(self);
    bool wraps   = tabsMultiline(self);
    CxUiEdges in = tabsInsets(self, wraps ? CxUiWidget(self)->_lrect.w / CXUI_FIX : 0, &strip);
    CxUiEdges m  = self->_insets;
    if (in.left != m.left || in.top != m.top || in.right != m.right || in.bottom != m.bottom ||
        (!wraps && strip > CxUiWidget(self)->_measured.w))
        _cxuiLayoutDirty(CxUiWidget(self));
}

extern void CxUiWidget_placePeer(_In_ CxUiWidget* self, CxUiRect px);   // parent
#undef parent_placePeer
#define parent_placePeer(px) CxUiWidget_placePeer((CxUiWidget*)(self), px)
void CxUiTabs_placePeer(_In_ CxUiTabs* self, CxUiRect px)
{
    CxUiWidget* w = CxUiWidget(self);
    if (!w->peer)
        return;
    _cxui.backend->controlSetBounds(w->peer, px);
    CxUiRect page = self->_pagePx;
    _cxui.backend->tabsSetPage(w->peer, (CxUiRect){ page.x - w->_bounds.x, page.y - w->_bounds.y, page.w, page.h });
}

extern void CxUiWidget_restyle(_In_ CxUiWidget* self);   // parent
#undef parent_restyle
#define parent_restyle() CxUiWidget_restyle((CxUiWidget*)(self))
void CxUiTabs_restyle(_In_ CxUiTabs* self)
{
    CxUiWidget* w = CxUiWidget(self);
    if (!w->peer || !_cxui.backend->tabsSetColors)
        return;
    CxUiPalette* pal = cxuiWidgetPalette(self);
    CxUiTabsColors c = {
        .around = _cxuiBackgroundOf(w),
        .page   = cxuiPaletteColor(pal, self->_bgRole),
        .border = cxuiPaletteColor(pal, CXUI_PAL_Border),
        .text   = cxuiPaletteColor(pal, (w->state & CXUI_WS_Inactive) ? CXUI_PAL_TextDisabled : CXUI_PAL_Text),
        .hover  = cxuiPaletteColor(pal, CXUI_PAL_Hover),
    };
    _cxui.backend->tabsSetColors(w->peer, &c);
}

static bool inside(CxUiWidget* w, CxUiWidget* ancestor)
{
    for (; w; w = w->parent) {
        if (w == ancestor)
            return true;
    }
    return false;
}

// The user picked a tab, on the control or with a key cxui handles: it is shown once the handler
// has had the chance to fill it. Focus left on the page that was shown goes to the strip.
static void tabsPick(CxUiTabs* self, int32 index, bool byKey)
{
    CxUiZBox* z = CxUiZBox(self);
    if (!live(self) || index < 0 || index >= zboxCount(z) || index == z->_active)
        return;
    z->_active = index;
    if (byKey)
        _cxuiTabsSync(self);
    _cxuiLayoutNeedWalk(CxUiWidget(self));
    CxUiTabs* hold     = objAcquire(self);
    // The peer outlives the change even if a handler destroys the tabs.
    _cxuiNativeEnter();
    CxUiPeer* peer = CxUiWidget(self)->peer;
    if (peer && _cxui.backend->tabsHold)
        _cxui.backend->tabsHold(peer, true);
    CxUiChangeEvent ev = { .base.kind = CXUI_EV_Change };
    _cxuiDispatch(CxUiWidget(self), &ev.base);
    CxUiWindow* win = live(self) ? _cxuiWindowOf(CxUiWidget(self)) : NULL;
    if (win)
        _cxuiLayoutFlush(win);
    if (peer && _cxui.backend->tabsHold)
        _cxui.backend->tabsHold(peer, false);
    if (win) {
        CxUiWidget* f = win->_focus;
        if (f && f != CxUiWidget(self) && inside(f, CxUiWidget(self)) && (!f->peer || f->peer->hidden)) {
            if (byKey)
                _cxuiFocusByKey(CxUiWidget(self));
            else
                _cxuiFocus(CxUiWidget(self));
        }
    }
    _cxuiNativeLeave();
    objRelease(&hold);
}

_Use_decl_annotations_
void _cxuiTabsPicked(CxUiPeer* p, int32 index)
{
    CxUiTabs* t = p->w ? objDynCast(CxUiTabs, p->w) : NULL;
    if (t)
        tabsPick(t, index, false);
}

_Use_decl_annotations_
bool _cxuiTabsMnemonic(CxUiTabs* self, CxUiKey key)
{
    for (int32 i = 0; i < saSize(self->_titles); i++) {
        if (_cxuiMnemonicKey(self->_titles.a[i]) == key) {
            tabsPick(self, i, true);
            _cxuiFocusByKey(CxUiWidget(self));
            return true;
        }
    }
    return false;
}

_Use_decl_annotations_
bool _cxuiTabsHasMnemonic(CxUiTabs* self, CxUiKey key)
{
    if (!_cxui.backend->tabMnemonics)
        return false;
    for (int32 i = 0; i < saSize(self->_titles); i++) {
        if (_cxuiMnemonicKey(self->_titles.a[i]) == key)
            return true;
    }
    return false;
}

// Ctrl+Tab and Ctrl+Page Down go to the next tab from anywhere in the tabs, round to the first;
// with Shift, or Ctrl+Page Up, to the one before.
extern CxUiResult CxUiWidget_handleEvent(_In_ CxUiWidget* self, _Inout_ CxUiEvent* ev);   // parent
#define parent_handleEvent(ev) CxUiWidget_handleEvent((CxUiWidget*)(self), ev)
CxUiResult CxUiTabs_handleEvent(_In_ CxUiTabs* self, _Inout_ CxUiEvent* ev)
{
    if (ev->kind != CXUI_EV_Key)
        return CXUI_Default;
    CxUiKeyEvent* k = (CxUiKeyEvent*)ev;
    int32 n         = zboxCount(CxUiZBox(self));
    if (!k->down || n < 2)
        return CXUI_Default;
    int32 step = 0;
    if (k->key == CXUI_KEY_Tab && (k->mods & ~CXUI_MOD_Shift) == CXUI_MOD_Ctrl)
        step = (k->mods & CXUI_MOD_Shift) ? -1 : 1;
    else if (k->mods == CXUI_MOD_Ctrl && (k->key == CXUI_KEY_PageDown || k->key == CXUI_KEY_PageUp))
        step = k->key == CXUI_KEY_PageDown ? 1 : -1;
    if (!step)
        return CXUI_Default;
    tabsPick(self, (CxUiZBox(self)->_active + step + n) % n, true);
    return CXUI_Handled;
}

void CxUiTabs_destroy(_In_ CxUiTabs* self)
{
    // Autogen begins -----
    saDestroy(&self->_titles);
    objRelease(&self->_peerFont);
    // Autogen ends -------
}

extern bool CxUiWidget_canFocus(_In_ CxUiWidget* self);   // parent
#define parent_canFocus() CxUiWidget_canFocus((CxUiWidget*)(self))
bool CxUiTabs_canFocus(_In_ CxUiTabs* self)
{
    return zboxCount(CxUiZBox(self)) > 0;
}

// Autogen begins -----
// clang-format off
#include "core/cxuicontainers.auto.inc"
// clang-format on
// Autogen ends -------
