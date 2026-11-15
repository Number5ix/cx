// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "core/cxuicanvas.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cxuidraw_private.h"

#include <math.h>

static bool canvasLive(CxUiCanvas* cv)
{
    return _cxuiLive(CxUiWidget(cv));
}

static bool isDip(CxUiCanvas* cv)
{
    return cv->_units == CXUI_Units_Dip;
}

static CxUiSize sizeInUnits(CxUiCanvas* cv)
{
    if (!isDip(cv))
        return cv->_size;
    float32 s = cxuiWidgetScale(cv);
    return (CxUiSize){ (int32)floor(cv->_size.w / s + 0.5), (int32)floor(cv->_size.h / s + 0.5) };
}

static void sendResize(CxUiCanvas* cv)
{
    CxUiResizeEvent ev = { .base.kind = CXUI_EV_Resize, .size = sizeInUnits(cv) };
    _cxuiDispatch(CxUiWidget(cv), &ev.base);
}

// Adds r to the region, unless it is already covered; false if it was.
static bool dirtyAdd(CxUiDirtyRegion* d, CxUiRect r)
{
    for (int32 i = 0; i < d->n; i++) {
        if (_cxuiRectContains(d->r[i], r))
            return false;
    }
    int32 n = 0;
    for (int32 i = 0; i < d->n; i++) {
        if (!_cxuiRectContains(r, d->r[i]))
            d->r[n++] = d->r[i];
    }
    if (n == CXUI_DIRTY_MAX) {
        for (int32 i = 0; i < n; i++)
            r = _cxuiRectUnion(r, d->r[i]);
        n = 0;
    }
    d->r[n++] = r;
    d->n      = n;
    return true;
}

// ---- runtime ----

_Use_decl_annotations_
void _cxuiCanvasInvalidatePx(CxUiCanvas* cv, CxUiRect px)
{
    px = _cxuiRectIntersect(px, (CxUiRect){ 0, 0, cv->_size.w, cv->_size.h });
    if (_cxuiRectEmpty(px) || !dirtyAdd(&cv->_dirty, px))
        return;
    if (cv->peer)
        _cxui.backend->canvasInvalidate(cv->peer, px);
}

_Use_decl_annotations_
void _cxuiCanvasRealize(CxUiCanvas* cv, CxUiPeer* parent)
{
    if (cv->peer || !_cxui.backend->canvasCreate)
        return;
    CxUiPeer* p = _cxui.backend->canvasCreate(cv, parent);
    if (!p)
        return;
    _cxuiPeerAttach(CxUiWidget(cv), p);
    if (cv->_cursor != CXUI_CURSOR_Arrow && _cxui.backend->canvasSetCursor)
        _cxui.backend->canvasSetCursor(p, cv->_cursor);
    if (cv->_cursorImage && _cxui.backend->canvasSetCursorImage)
        _cxui.backend->canvasSetCursorImage(p, cv->_cursorImage);
    if (cv->_textInput && _cxui.backend->canvasSetTextInput)
        _cxui.backend->canvasSetTextInput(p, cv->_textInput);
    if (cv->_hasCaret && _cxui.backend->canvasSetTextCaret)
        _cxui.backend->canvasSetTextCaret(p, cv->_caret);
    if (cv->_keyCapture && _cxui.backend->canvasSetKeyCapture)
        _cxui.backend->canvasSetKeyCapture(p, cv->_keyCapture);
    // Unknown until placed, so placing it repaints all of it.
    cv->_size    = (CxUiSize){ 0 };
    cv->_dirty.n = 0;
    // A render view's surface that comes with its peer; others come later, from the backend.
    CxUiNativeSurface s;
    if (objDynCast(CxUiRenderView, cv) && _cxui.backend->surfaceGet && _cxui.backend->surfaceGet(p, &s))
        _cxuiRenderViewReady(p);
}

_Use_decl_annotations_
void _cxuiCanvasSetBounds(CxUiCanvas* cv, CxUiRect px)
{
    CxUiCanvasPeer* p = (CxUiCanvasPeer*)cv->peer;
    if (!p)
        return;
    px.w = max(px.w, 0);
    px.h = max(px.h, 0);
    if (p->painting) {
        p->pendingBounds = px;
        p->boundsPending = true;
    } else {
        _cxui.backend->canvasSetBounds(&p->base, px);
    }

    if (px.w == cv->_size.w && px.h == cv->_size.h)
        return;
    cv->_size    = (CxUiSize){ px.w, px.h };
    cv->_dirty.n = 0;
    _cxuiCanvasInvalidatePx(cv, (CxUiRect){ 0, 0, px.w, px.h });
    sendResize(cv);
}

// ---- class ----

_objfactory_guaranteed CxUiCanvas* CxUiCanvas_createWith(_In_opt_ const CxUiCanvasDesc* desc)
{
    CxUiCanvas* self;
    self = objInstCreate(CxUiCanvas);
    if (desc) {
        strDup(&self->name, desc->name);
        self->_drawBackend = desc->draw <= CXUI_Draw_GDI ? desc->draw : CXUI_Draw_Auto;
        self->_units       = desc->units == CXUI_Units_Dip ? CXUI_Units_Dip : CXUI_Units_Pixels;
    }

    objInstInit(self);
    return self;
}

_objinit_guaranteed bool CxUiCanvas_init(_In_ CxUiCanvas* self)
{
    // A canvas has no natural size, so it takes whatever space there is.
    CxUiWidget(self)->_lp.expand = CXUI_EXPAND_Both;
    // Autogen begins -----
    return true;
    // Autogen ends -------
}

// An area in the canvas's units in physical pixels, outward, to cover every pixel it touches
static CxUiRect areaPx(CxUiCanvas* cv, int32 x, int32 y, int32 w, int32 h)
{
    if (!isDip(cv))
        return (CxUiRect){ x, y, w, h };
    float32 s = cxuiWidgetScale(cv);
    int32 x0  = (int32)floor(x * s), y0 = (int32)floor(y * s);
    return (CxUiRect){ x0, y0, (int32)ceil(((float64)x + w) * s) - x0, (int32)ceil(((float64)y + h) * s) - y0 };
}

void CxUiCanvas_invalidate(_In_ CxUiCanvas* self, int32 x, int32 y, int32 w, int32 h)
{
    if (canvasLive(self))
        _cxuiCanvasInvalidatePx(self, areaPx(self, x, y, w, h));
}

void CxUiCanvas_invalidateAll(_In_ CxUiCanvas* self)
{
    if (canvasLive(self))
        _cxuiCanvasInvalidatePx(self, (CxUiRect){ 0, 0, self->_size.w, self->_size.h });
}

void CxUiCanvas_paintNow(_In_ CxUiCanvas* self)
{
    if (!canvasLive(self) || !self->peer || self->_dirty.n == 0)
        return;
    _cxuiNativeEnter();
    _cxui.backend->canvasPaintNow(self->peer);
    _cxuiNativeLeave();
}

bool CxUiCanvas_size(_In_ CxUiCanvas* self, _Out_ CxUiSize* out)
{
    *out = (CxUiSize){ 0 };
    if (!canvasLive(self) || !self->peer)
        return false;
    *out = sizeInUnits(self);
    return true;
}

void CxUiCanvas_setCursor(_In_ CxUiCanvas* self, uint32 cursor)
{
    if (!canvasLive(self) || cursor >= CXUI_CURSOR_Count)
        return;
    CxUiCanvas_setCursorImage(self, NULL);
    if (cursor == self->_cursor)
        return;
    self->_cursor = cursor;
    if (self->peer && _cxui.backend->canvasSetCursor)
        _cxui.backend->canvasSetCursor(self->peer, cursor);
}

void CxUiCanvas_setCursorImage(_In_ CxUiCanvas* self, _In_opt_ CxUiCursor* cursor)
{
    if (!canvasLive(self) || cursor == self->_cursorImage)
        return;
    CxUiCursor* old    = self->_cursorImage;
    self->_cursorImage = cursor ? objAcquire(cursor) : NULL;
    if (self->peer && _cxui.backend->canvasSetCursorImage)
        _cxui.backend->canvasSetCursorImage(self->peer, self->_cursorImage);
    objRelease(&old);
}

bool CxUiCanvas_warpPointer(_In_ CxUiCanvas* self, int32 x, int32 y)
{
    if (!canvasLive(self) || !self->peer || self->peer->hidden || _cxui.lock == self ||
        !_cxui.backend->canvasWarpPointer)
        return false;
    CxUiPoint px = { x, y };
    if (isDip(self)) {
        float32 s = cxuiWidgetScale(self);
        px        = (CxUiPoint){ (int32)floor(x * s), (int32)floor(y * s) };
    }
    _cxuiNativeEnter();
    bool ok = _cxui.backend->canvasWarpPointer(self->peer, px);
    _cxuiNativeLeave();
    if (ok && canvasLive(self))
        _cxuiCanvasWarped(self, px);
    return ok;
}

uint32 CxUiCanvas_units(_In_ CxUiCanvas* self)
{
    return canvasLive(self) ? self->_units : CXUI_Units_Pixels;
}

void CxUiCanvas_setUnits(_In_ CxUiCanvas* self, uint32 units)
{
    units = units == CXUI_Units_Dip ? CXUI_Units_Dip : CXUI_Units_Pixels;
    if (!canvasLive(self) || units == self->_units)
        return;
    self->_units = units;
    if (self->peer) {
        _cxuiCanvasInvalidatePx(self, (CxUiRect){ 0, 0, self->_size.w, self->_size.h });
        sendResize(self);
    }
}

bool CxUiCanvas_setTextInput(_In_ CxUiCanvas* self, flags_t flags)
{
    if (!canvasLive(self))
        return false;
    flags &= CXUI_TEXTINPUT_On;
    self->_textInput = flags;
    if (!_cxui.backend->canvasSetTextInput)
        return !flags;
    if (!self->peer)
        return true;
    _cxuiNativeEnter();
    bool ok = _cxui.backend->canvasSetTextInput(self->peer, flags);
    _cxuiNativeLeave();
    return ok || !flags;
}

flags_t CxUiCanvas_textInput(_In_ CxUiCanvas* self)
{
    return canvasLive(self) ? self->_textInput : 0;
}

void CxUiCanvas_setTextCaret(_In_ CxUiCanvas* self, int32 x, int32 y, int32 w, int32 h)
{
    if (!canvasLive(self))
        return;
    self->_caret    = areaPx(self, x, y, max(w, 0), max(h, 0));
    self->_hasCaret = true;
    if (self->peer && _cxui.backend->canvasSetTextCaret)
        _cxui.backend->canvasSetTextCaret(self->peer, self->_caret);
}

bool CxUiCanvas_setKeyCapture(_In_ CxUiCanvas* self, flags_t flags)
{
    if (!canvasLive(self))
        return false;
    flags = (flags & CXUI_KEYCAPTURE_On) ? flags & (CXUI_KEYCAPTURE_On | CXUI_KEYCAPTURE_SystemKey) : 0;
    self->_keyCapture = flags;
    if (!_cxui.backend->canvasSetKeyCapture)
        return true;
    if (!self->peer)
        return true;
    _cxuiNativeEnter();
    bool ok = _cxui.backend->canvasSetKeyCapture(self->peer, flags);
    _cxuiNativeLeave();
    return ok;
}

flags_t CxUiCanvas_keyCapture(_In_ CxUiCanvas* self)
{
    return canvasLive(self) ? self->_keyCapture : 0;
}

bool CxUiCanvas_setPointerLock(_In_ CxUiCanvas* self, uint32 mode)
{
    if (!canvasLive(self) || mode > CXUI_POINTERLOCK_Raw)
        return false;
    return _cxuiPointerLockSet(self, mode);
}

uint32 CxUiCanvas_pointerLockMode(_In_ CxUiCanvas* self)
{
    return canvasLive(self) ? self->_lockMode : CXUI_POINTERLOCK_Off;
}

extern void CxUiWidget_realizePeer(_In_ CxUiWidget* self, _In_ CxUiPeer* host);   // parent
#define parent_realizePeer(host) CxUiWidget_realizePeer((CxUiWidget*)(self), host)
void CxUiCanvas_realizePeer(_In_ CxUiCanvas* self, _In_ CxUiPeer* host)
{
    _cxuiCanvasRealize(self, host);
}

extern void CxUiWidget_placePeer(_In_ CxUiWidget* self, CxUiRect px);   // parent
#define parent_placePeer(px) CxUiWidget_placePeer((CxUiWidget*)(self), px)
void CxUiCanvas_placePeer(_In_ CxUiCanvas* self, CxUiRect px)
{
    _cxuiCanvasSetBounds(self, px);
}

extern void CxUiWidget_restyle(_In_ CxUiWidget* self);   // parent
#define parent_restyle() CxUiWidget_restyle((CxUiWidget*)(self))
void CxUiCanvas_restyle(_In_ CxUiCanvas* self)
{
    if (self->peer)
        CxUiCanvas_invalidateAll(self);
}

extern bool CxUiWidget_canFocus(_In_ CxUiWidget* self);   // parent
#define parent_canFocus() CxUiWidget_canFocus((CxUiWidget*)(self))
bool CxUiCanvas_canFocus(_In_ CxUiCanvas* self)
{
    return true;
}

extern uint32 CxUiWidget_eventMask(_In_ CxUiWidget* self);   // parent
#define parent_eventMask() CxUiWidget_eventMask((CxUiWidget*)(self))
uint32 CxUiCanvas_eventMask(_In_ CxUiCanvas* self)
{
    return parent_eventMask() | (1 << CXUI_EV_Key) | (1 << CXUI_EV_Mouse) | (1 << CXUI_EV_Wheel) |
           (1 << CXUI_EV_Focus) | (1 << CXUI_EV_Paint) | (1 << CXUI_EV_Resize) | (1 << CXUI_EV_Text);
}

// ---- render views ----

_objfactory_guaranteed CxUiRenderView* CxUiRenderView_createWith(_In_opt_ const CxUiRenderViewDesc* desc)
{
    CxUiRenderView* self;
    self = objInstCreate(CxUiRenderView);
    if (desc) {
        strDup(&CxUiWidget(self)->name, desc->name);
        CxUiCanvas(self)->_units = desc->units == CXUI_Units_Dip ? CXUI_Units_Dip : CXUI_Units_Pixels;
    }

    objInstInit(self);
    return self;
}

bool CxUiRenderView_surface(_In_ CxUiRenderView* self, _Out_ CxUiNativeSurface* out)
{
    *out        = (CxUiNativeSurface){ 0 };
    CxUiPeer* p = CxUiWidget(self)->peer;
    if (!canvasLive(CxUiCanvas(self)) || !self->_available || !p || !_cxui.backend->surfaceGet ||
        !_cxui.backend->surfaceGet(p, out))
        return false;
    out->w = CxUiCanvas(self)->_size.w;
    out->h = CxUiCanvas(self)->_size.h;
    return true;
}

extern uint32 CxUiCanvas_eventMask(_In_ CxUiCanvas* self);   // parent
#undef parent_eventMask
#define parent_eventMask() CxUiCanvas_eventMask((CxUiCanvas*)(self))
uint32 CxUiRenderView_eventMask(_In_ CxUiRenderView* self)
{
    return parent_eventMask() | (1 << CXUI_EV_Surface);
}

// The surface reads back through the event that says it is going, for the renderer letting go.
static void surfaceEvent(CxUiRenderView* v, bool available)
{
    if (available)
        v->_available = true;
    v->_going = !available;
    _cxui.surfaces += available ? 1 : -1;
    CxUiSurfaceEvent ev = { .base.kind = CXUI_EV_Surface, .available = available };
    _cxuiDispatch(CxUiWidget(v), &ev.base);
    if (!available) {
        v->_available = false;
        v->_going     = false;
    }
}

// From the queue, so the event never comes in the middle of a layout pass
static bool surfaceReadyTask(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    ObjInst_WeakRef* wref = NULL;
    if (!stvlNext(cvars, weakref, &wref))
        return true;
    CxUiRenderView* v = objAcquireFromWeakDyn(CxUiRenderView, wref);
    CxUiNativeSurface s;
    if (v && canvasLive(CxUiCanvas(v)) && !v->_available && CxUiWidget(v)->peer && _cxui.backend->surfaceGet &&
        _cxui.backend->surfaceGet(CxUiWidget(v)->peer, &s))
        surfaceEvent(v, true);
    objRelease(&v);
    return true;
}

_Use_decl_annotations_
void _cxuiRenderViewReady(CxUiPeer* p)
{
    CxUiRenderView* v = p->w ? objDynCast(CxUiRenderView, p->w) : NULL;
    if (!v)
        return;
    Weak(CxUiRenderView)* wref = objGetWeak(CxUiRenderView, v);
    cxuiPost(closureCreate(surfaceReadyTask, stvar(weakref, objWeakRefBase(wref))));
    objDestroyWeak(&wref);
}

_Use_decl_annotations_
void _cxuiSurfacesLost(CxUiWidget* w)
{
    if (_cxui.surfaces <= 0)
        return;
    CxUiRenderView* v = objDynCast(CxUiRenderView, w);
    if (v && v->_available && !v->_going) {
        CxUiWidget* hold = objAcquire(w);
        surfaceEvent(v, false);
        objRelease(&hold);
    }
    for (int32 i = 0, n = _cxuiContainerChildCount(w); i < n; i++) {
        CxUiWidget* child = _cxuiContainerChild(w, i);
        if (child)
            _cxuiSurfacesLost(child);
    }
}

void CxUiCanvas_destroy(_In_ CxUiCanvas* self)
{
    // Autogen begins -----
    objRelease(&self->_cursorImage);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "core/cxuicanvas.auto.inc"
// clang-format on
// Autogen ends -------
