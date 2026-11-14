// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "core/cxuibox.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cxui_private.h"

static bool boxLive(void* self)
{
    return _cxuiLive((CxUiWidget*)self);
}

// ---- CxUiBoxBase ----

_Use_decl_annotations_
bool _cxuiBoxBaseInsert(CxUiBoxBase* self, CxUiWidget* child, int32 at)
{
    if (!boxLive(self) || !_cxuiLive(child) || child->parent || objDynCast(CxUiWindow, child))
        return false;
    at = clamp(at, 0, saSize(self->_children));
    saInsert(&self->_children, at, object, child);
    child->parent = CxUiWidget(self);
    _cxuiLayoutAttached(CxUiWidget(self), child);
    return true;
}

_Use_decl_annotations_
bool _cxuiBoxBaseDetach(CxUiBoxBase* self, CxUiWidget* child)
{
    int32 i = saFind(self->_children, object, child);
    if (i < 0)
        return false;
    // The array's reference keeps the child alive until it is unlinked.
    objAcquire(child);
    saRemove(&self->_children, i);
    child->parent = NULL;
    _cxuiLayoutDetached(CxUiWidget(self), child);
    objRelease(&child);
    return true;
}

_Use_decl_annotations_
CxUiWidget* _cxuiBoxBaseChild(CxUiBoxBase* self, int32 i)
{
    return (i >= 0 && i < saSize(self->_children)) ? self->_children.a[i] : NULL;
}

static void baseRelayout(void* self)
{
    if (boxLive(self))
        _cxuiLayoutDirty((CxUiWidget*)self);
}

_objinit_guaranteed bool CxUiBoxBase_init(_In_ CxUiBoxBase* self)
{
    saInit(&self->_children, object, 4);
    // Autogen begins -----
    return true;
    // Autogen ends -------
}

void CxUiBoxBase_destroy(_In_ CxUiBoxBase* self)
{
    // Autogen begins -----
    saDestroy(&self->_children);
    // Autogen ends -------
}

// ---- CxUiBox ----

_Use_decl_annotations_
void _cxuiBoxSetup(CxUiBox* self, const CxUiBoxDesc* desc)
{
    CxUiBoxBase* base = CxUiBoxBase(self);
    self->_orient     = desc->orient == CXUI_ORIENT_V ? CXUI_ORIENT_V : CXUI_ORIENT_H;
    self->_align[0]   = desc->align[0];
    self->_align[1]   = desc->align[1];
    self->_normalize  = desc->normalize & CXUI_EXPAND_Both;
    base->_margin     = desc->margin;
    base->_gap        = max(desc->gap, 0);
    CxUiWidget(self)->_lp = desc->lp;
    strDup(&CxUiWidget(self)->name, desc->name);
}

_objfactory_guaranteed CxUiBox* CxUiBox_createWith(_In_opt_ const CxUiBoxDesc* desc)
{
    CxUiBox* self;
    self = objInstCreate(CxUiBox);
    if (desc)
        _cxuiBoxSetup(self, desc);

    objInstInit(self);
    return self;
}

_objfactory_guaranteed CxUiBox* CxUiBox_createH()
{
    return CxUiBox_createWith(NULL);
}

_objfactory_guaranteed CxUiBox* CxUiBox_createV()
{
    CxUiBox* self;
    self          = objInstCreate(CxUiBox);
    self->_orient = CXUI_ORIENT_V;

    objInstInit(self);
    return self;
}

void CxUiBox_setMargin(_In_ CxUiBox* self, int32 left, int32 top, int32 right, int32 bottom)
{
    CxUiBoxBase(self)->_margin = (CxUiEdges){ left, top, right, bottom };
    baseRelayout(self);
}

void CxUiBox_setGap(_In_ CxUiBox* self, int32 gap)
{
    CxUiBoxBase(self)->_gap = max(gap, 0);
    baseRelayout(self);
}

void CxUiBox_setChildAlign(_In_ CxUiBox* self, uint32 h, uint32 v)
{
    self->_align[0] = (uint8)min(h, CXUI_ALIGN_Fill);
    self->_align[1] = (uint8)min(v, CXUI_ALIGN_Fill);
    baseRelayout(self);
}

void CxUiBox_setNormalize(_In_ CxUiBox* self, uint32 axes)
{
    self->_normalize = (uint8)(axes & CXUI_EXPAND_Both);
    baseRelayout(self);
}

static int32 sizeAxis(CxUiSize s, int a)
{
    return a == 0 ? s.w : s.h;
}

static int32 edgeStart(CxUiBoxBase* b, int a)
{
    return (a == 0 ? b->_margin.left : b->_margin.top + b->_insetTop) + b->_inset;
}

static int32 edgeEnd(CxUiBoxBase* b, int a)
{
    return (a == 0 ? b->_margin.right : b->_margin.bottom) + b->_inset;
}

static uint32 axisBit(int a)
{
    return a == 0 ? CXUI_EXPAND_H : CXUI_EXPAND_V;
}

extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiBox_measure(_In_ CxUiBox* self)
{
    CxUiBoxBase* base = CxUiBoxBase(self);
    int m = self->_orient == CXUI_ORIENT_V ? 1 : 0, c = 1 - m;
    int32 n = 0, sumM = 0, maxM = 0, maxC = 0;
    uint32 expand = 0;
    for (int32 i = 0; i < saSize(base->_children); i++) {
        CxUiWidget* child = base->_children.a[i];
        if (!_cxuiLayoutTakesSpace(child))
            continue;
        CxUiSize s = _cxuiLayoutNatural(child);
        sumM += sizeAxis(s, m);
        maxM = max(maxM, sizeAxis(s, m));
        maxC = max(maxC, sizeAxis(s, c));
        expand |= _cxuiLayoutExpand(child, axisBit(m));
        n++;
    }
    if (self->_normalize & axisBit(m))
        sumM = maxM * n;
    CxUiWidget(self)->_expand |= expand;

    int32 out[2];
    out[m] = edgeStart(base, m) + edgeEnd(base, m) + sumM + (n > 0 ? base->_gap * (n - 1) : 0);
    out[c] = edgeStart(base, c) + edgeEnd(base, c) + maxC;
    return (CxUiSize){ out[0], out[1] };
}

typedef struct BoxSlot {
    CxUiWidget* w;
    int32 size;   // along the box, in CXUI_FIX units
    int32 min;
    CxUiSize natural;
    uint32 expand;
} BoxSlot;

#define SLOTS_INLINE 32

static int32 rectPos(CxUiLayoutRect r, int a)
{
    return a == 0 ? r.x : r.y;
}

static int32 rectLen(CxUiLayoutRect r, int a)
{
    return a == 0 ? r.w : r.h;
}

static void rectSet(CxUiLayoutRect* r, int a, int32 pos, int32 len)
{
    if (a == 0) {
        r->x = pos;
        r->w = len;
    } else {
        r->y = pos;
        r->h = len;
    }
}

// The visible children as slots, with their natural sizes; the largest along and across
static int32 boxSlots(CxUiBox* self, BoxSlot* slots, int m, int32* maxM, int32* maxC)
{
    CxUiBoxBase* base = CxUiBoxBase(self);
    int c             = 1 - m;
    int32 n           = 0;
    *maxM = *maxC = 0;
    for (int32 i = 0; i < saSize(base->_children); i++) {
        CxUiWidget* child = base->_children.a[i];
        if (!_cxuiLayoutTakesSpace(child))
            continue;
        CxUiSize nat = _cxuiLayoutNatural(child);
        slots[n]     = (BoxSlot){ .w       = child,
                                  .natural = nat,
                                  .min     = _cxuiLenDip(child, child->_lp.minSize[m], m) * CXUI_FIX,
                                  .expand  = _cxuiLayoutExpand(child, axisBit(m)) };
        *maxM        = max(*maxM, sizeAxis(nat, m));
        *maxC        = max(*maxC, sizeAxis(nat, c));
        n++;
    }
    return n;
}

static uint32 slotAlign(CxUiBox* self, BoxSlot* s, int a)
{
    return s->w->_lp.align[a] ? s->w->_lp.align[a] : self->_align[a];
}

// The length a child gets across the box, in CXUI_FIX units
static int32 crossLen(CxUiBox* self, BoxSlot* s, int c, int32 inner, int32 maxC)
{
    if ((s->expand & axisBit(c)) || slotAlign(self, s, c) == CXUI_ALIGN_Fill)
        return inner;
    int32 natC = self->_normalize & axisBit(c) ? maxC : sizeAxis(s->natural, c);
    return min(natC * CXUI_FIX, inner);
}

// A height that depends on the width a child gets in place of its natural one; the tallest again
static void slotHeights(BoxSlot* slots, int32 n, const int32* widths, int32* maxH)
{
    for (int32 i = 0; i < n; i++) {
        if (slots[i].w->_hfw)
            slots[i].natural.h = _cxuiLayoutHeightFor(slots[i].w, widths[i] / CXUI_FIX);
        *maxH = max(*maxH, slots[i].natural.h);
    }
}

// Shares inner (CXUI_FIX units) out along the box: each child its natural size (the largest,
// normalized), the spare to the expanding children, a shortfall taken from them first, down to
// their minimum, then from the end. Returns where the group starts when none expands.
static int32 boxShare(CxUiBox* self, BoxSlot* slots, int32 n, int m, int32 maxM, int32 inner)
{
    int32 total = n > 0 ? CxUiBoxBase(self)->_gap * CXUI_FIX * (n - 1) : 0;
    int32 nexp  = 0;
    for (int32 i = 0; i < n; i++) {
        slots[i].size = (self->_normalize & axisBit(m) ? maxM : sizeAxis(slots[i].natural, m)) * CXUI_FIX;
        total += slots[i].size;
        if (slots[i].expand & axisBit(m))
            nexp++;
    }

    int32 spare = inner - total, offset = 0;
    if (spare > 0 && nexp > 0) {
        int32 share = spare / nexp, last = -1;
        for (int32 i = 0; i < n; i++) {
            if (slots[i].expand & axisBit(m)) {
                slots[i].size += share;
                last = i;
            }
        }
        slots[last].size += spare - share * nexp;
    } else if (spare > 0) {
        uint32 align = self->_align[m];
        if (align == CXUI_ALIGN_Center)
            offset = spare / 2;
        else if (align == CXUI_ALIGN_End)
            offset = spare;
    } else if (spare < 0) {
        int32 need = -spare;
        for (int pass = 0; pass < 2 && need > 0; pass++) {
            for (int32 i = n - 1; i >= 0 && need > 0; i--) {
                if (pass == 0 && !(slots[i].expand & axisBit(m)))
                    continue;
                int32 give = min(need, max(slots[i].size - slots[i].min, 0));
                slots[i].size -= give;
                need -= give;
            }
        }
    }
    return offset;
}

typedef struct BoxWork {
    BoxSlot inlineSlots[SLOTS_INLINE];
    int32 inlineWidths[SLOTS_INLINE];
    BoxSlot* slots;
    int32* widths;
} BoxWork;

static void workInit(BoxWork* bw, int32 count)
{
    bw->slots  = count > SLOTS_INLINE ? xaAlloc(count * sizeof(BoxSlot)) : bw->inlineSlots;
    bw->widths = count > SLOTS_INLINE ? xaAlloc(count * sizeof(int32)) : bw->inlineWidths;
}

static void workFree(BoxWork* bw)
{
    if (bw->slots != bw->inlineSlots) {
        xaFree(bw->slots);
        xaFree(bw->widths);
    }
}

// Widths first, as arrange decides them, then the heights children need at those widths
extern int32 CxUiWidget_heightFor(_In_ CxUiWidget* self, int32 width);   // parent
#define parent_heightFor(width) CxUiWidget_heightFor((CxUiWidget*)(self), width)
int32 CxUiBox_heightFor(_In_ CxUiBox* self, int32 width)
{
    CxUiBoxBase* base = CxUiBoxBase(self);
    int m = self->_orient == CXUI_ORIENT_V ? 1 : 0, c = 1 - m;
    BoxWork bw;
    workInit(&bw, saSize(base->_children));
    int32 maxM, maxC;
    int32 n     = boxSlots(self, bw.slots, m, &maxM, &maxC);
    int32 inner = max(width - edgeStart(base, 0) - edgeEnd(base, 0), 0) * CXUI_FIX;
    int32 h     = 0;
    if (m == 0) {
        boxShare(self, bw.slots, n, 0, maxM, inner);
        for (int32 i = 0; i < n; i++)
            bw.widths[i] = bw.slots[i].size;
        slotHeights(bw.slots, n, bw.widths, &h);
    } else {
        int32 maxH = 0;
        for (int32 i = 0; i < n; i++)
            bw.widths[i] = crossLen(self, &bw.slots[i], c, inner, maxC);
        slotHeights(bw.slots, n, bw.widths, &maxH);
        for (int32 i = 0; i < n; i++)
            h += self->_normalize & CXUI_EXPAND_V ? maxH : bw.slots[i].natural.h;
        h += n > 0 ? base->_gap * (n - 1) : 0;
    }
    workFree(&bw);
    return h + edgeStart(base, 1) + edgeEnd(base, 1);
}

extern void CxUiWidget_arrange(_In_ CxUiWidget* self);   // parent
#define parent_arrange() CxUiWidget_arrange((CxUiWidget*)(self))
void CxUiBox_arrange(_In_ CxUiBox* self)
{
    CxUiBoxBase* base = CxUiBoxBase(self);
    CxUiLayoutRect r  = CxUiWidget(self)->_lrect;
    int m = self->_orient == CXUI_ORIENT_V ? 1 : 0, c = 1 - m;
    int32 innerPos[2], innerLen[2];
    for (int a = 0; a < 2; a++) {
        int32 s0    = edgeStart(base, a) * CXUI_FIX;
        int32 s1    = edgeEnd(base, a) * CXUI_FIX;
        innerPos[a] = rectPos(r, a) + s0;
        innerLen[a] = max(rectLen(r, a) - s0 - s1, 0);
    }

    BoxWork bw;
    workInit(&bw, saSize(base->_children));
    BoxSlot* slots = bw.slots;
    int32 maxM, maxC;
    int32 n = boxSlots(self, slots, m, &maxM, &maxC);

    // A child whose height depends on its width is measured at the width it gets: in a column
    // that is known first; in a row, once the row is shared out.
    if (m == 1) {
        for (int32 i = 0; i < n; i++)
            bw.widths[i] = crossLen(self, &slots[i], c, innerLen[c], maxC);
        maxM = 0;
        slotHeights(slots, n, bw.widths, &maxM);
    }
    int32 offset = boxShare(self, slots, n, m, maxM, innerLen[m]);
    if (m == 0) {
        for (int32 i = 0; i < n; i++)
            bw.widths[i] = slots[i].size;
        maxC = 0;
        slotHeights(slots, n, bw.widths, &maxC);
    }

    int32 pos = innerPos[m] + offset;
    for (int32 i = 0; i < n; i++) {
        CxUiLayoutRect cr = { 0 };
        rectSet(&cr, m, pos, slots[i].size);
        pos += slots[i].size + base->_gap * CXUI_FIX;

        uint32 align = slotAlign(self, &slots[i], c);
        int32 len    = crossLen(self, &slots[i], c, innerLen[c], maxC);
        int32 at     = innerPos[c];
        if (align == CXUI_ALIGN_Center)
            at += (innerLen[c] - len) / 2;
        else if (align == CXUI_ALIGN_End)
            at += innerLen[c] - len;
        rectSet(&cr, c, at, len);
        _cxuiLayoutPlace(slots[i].w, cr);
    }
    workFree(&bw);
}

bool CxUiBox_append(_In_ CxUiBox* self, _In_ CxUiWidget* child)
{
    return _cxuiBoxBaseInsert(CxUiBoxBase(self), child, saSize(CxUiBoxBase(self)->_children));
}

bool CxUiBox_insert(_In_ CxUiBox* self, _In_ CxUiWidget* child, int32 at)
{
    return _cxuiBoxBaseInsert(CxUiBoxBase(self), child, at);
}

bool CxUiBox_detach(_In_ CxUiBox* self, _In_ CxUiWidget* child)
{
    return _cxuiBoxBaseDetach(CxUiBoxBase(self), child);
}

int32 CxUiBox_childCount(_In_ CxUiBox* self)
{
    return saSize(CxUiBoxBase(self)->_children);
}

_Ret_opt_valid_ CxUiWidget* CxUiBox_child(_In_ CxUiBox* self, int32 i)
{
    return _cxuiBoxBaseChild(CxUiBoxBase(self), i);
}

// ---- CxUiFill, CxUiSpace ----

_objfactory_guaranteed CxUiFill* CxUiFill_create()
{
    CxUiFill* self;
    self = objInstCreate(CxUiFill);

    objInstInit(self);
    return self;
}

_objfactory_guaranteed CxUiSpace* CxUiSpace_create(CxUiLen width, CxUiLen height)
{
    CxUiSpace* self;
    self                         = objInstCreate(CxUiSpace);
    CxUiWidget(self)->_lp.size[0] = width;
    CxUiWidget(self)->_lp.size[1] = height;

    objInstInit(self);
    return self;
}

// Autogen begins -----
// clang-format off
#include "core/cxuibox.auto.inc"
// clang-format on
// Autogen ends -------
