// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "core/cxuisash.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cxui_private.h"

_objfactory_guaranteed CxUiSash* CxUiSash_create(uint32 orient, bool grip)
{
    CxUiSash* self;
    self          = objInstCreate(CxUiSash);
    self->_orient = orient == CXUI_ORIENT_V ? CXUI_ORIENT_V : CXUI_ORIENT_H;
    self->_grip   = grip;

    objInstInit(self);
    // A side-by-side split's sash moves left and right.
    cxuiCanvasSetCursor(self, self->_orient == CXUI_ORIENT_H ? CXUI_CURSOR_SizeWE : CXUI_CURSOR_SizeNS);
    return self;
}

void CxUiSash_setColor(_In_ CxUiSash* self, CxUiColor c)
{
    if (!_cxuiLive(CxUiWidget(self)))
        return;
    self->_color = c;
    cxuiWidgetRestyle(self);
}

static void sashPaint(CxUiSash* self, CxUiDraw* d)
{
    CxUiPalette* pal = cxuiWidgetPalette(self);
    CxUiSize sz      = cxuiDrawSize(d);
    cxuiDrawFill(d, 0, 0, sz.w, sz.h, self->_color.bits ? self->_color : cxuiPaletteColor(pal, CXUI_PAL_Border));
    if (!self->_grip)
        return;
    // Three dots across the middle
    CxUiColor dot = cxuiPaletteColor(pal, CXUI_PAL_TextDim);
    int32 r       = max(cxuiPx(self, 2), 1);
    bool across   = self->_orient == CXUI_ORIENT_V;
    for (int32 i = -1; i <= 1; i++) {
        int32 off = i * r * 3;
        int32 x   = across ? sz.w / 2 + off - r / 2 : (sz.w - r) / 2;
        int32 y   = across ? (sz.h - r) / 2 : sz.h / 2 + off - r / 2;
        cxuiDrawFill(d, x, y, r, r, dot);
    }
}

// Where the pointer is along the split, in pixels from the window's edge, so it stays put while
// the sash itself moves.
static int32 along(CxUiSash* self, CxUiMouseEvent* ev)
{
    CxUiRect b = CxUiWidget(self)->_bounds;
    return self->_orient == CXUI_ORIENT_H ? b.x + ev->x : b.y + ev->y;
}

static CxUiResult sashMouse(CxUiSash* self, CxUiMouseEvent* ev)
{
    CxUiSplit* split = CxUiWidget(self)->parent ? objDynCast(CxUiSplit, CxUiWidget(self)->parent) : NULL;
    if (!split)
        return CXUI_Default;

    if (ev->action == CXUI_MOUSE_Down && ev->button == CXUI_BTN_Left) {
        self->_dragging  = true;
        self->_dragFrom  = along(self, ev);
        self->_dragFirst = _cxuiSplitFirst(split);
    } else if (self->_dragging && ev->action == CXUI_MOUSE_Move) {
        float32 scale = cxuiWidgetScale(self);
        int32 moved   = (int32)((along(self, ev) - self->_dragFrom) / scale);
        _cxuiSplitDragTo(split, self->_dragFirst + moved);
    } else if (self->_dragging && ((ev->action == CXUI_MOUSE_Up && ev->button == CXUI_BTN_Left) ||
                                   (ev->flags & CXUI_MOUSE_CaptureLost))) {
        self->_dragging = false;
        objAcquire(split);
        CxUiChangeEvent change = { .base.kind = CXUI_EV_Change };
        _cxuiDispatch(CxUiWidget(split), &change.base);
        objRelease(&split);
    }
    return CXUI_Handled;
}

extern CxUiResult CxUiWidget_handleEvent(_In_ CxUiWidget* self, _Inout_ CxUiEvent* ev);   // parent
#define parent_handleEvent(ev) CxUiWidget_handleEvent((CxUiWidget*)(self), ev)
CxUiResult CxUiSash_handleEvent(_In_ CxUiSash* self, _Inout_ CxUiEvent* ev)
{
    switch (ev->kind) {
    case CXUI_EV_Paint:
        sashPaint(self, ((CxUiPaintEvent*)ev)->draw);
        return CXUI_Handled;
    case CXUI_EV_Mouse:
        return sashMouse(self, (CxUiMouseEvent*)ev);
    default:
        return parent_handleEvent(ev);
    }
}

// Autogen begins -----
// clang-format off
#include "core/cxuisash.auto.inc"
// clang-format on
// Autogen ends -------
