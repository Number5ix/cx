// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "core/cxuiwindow.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cxuidraw_private.h"
#include "core/cxuicontrols.h"
#include <cxui/test/cxuitest.h>

#include <math.h>
#include <stdlib.h>

// Realized windows, each held by a reference until it is destroyed or the session ends. This is
// also what keeps a realized window's widget valid for its peer's native events. UI thread only.
static sa_CxUiWindow realized;

// One running cxuiWindowPopup(), on its caller's stack.
typedef struct PopupFrame {
    atomic(bool) done;
    int32 result;
} PopupFrame;

static void popupEnd(CxUiWindow* w, int32 result)
{
    PopupFrame* f = w->_popup;
    if (!f || atomicLoad(bool, &f->done, Acquire))
        return;
    f->result = result;
    atomicStore(bool, &f->done, true, Release);
    // The loop waiting on it may be idle in the platform's wait.
    _cxui.backend->wake();
}

// One cxuiWindowShowModal() that hasn't ended
typedef struct ModalShow {
    CxUiModalLock lock;
    closure onDone;
} ModalShow;

// The first half of ending a modal show: the parent works again before the window hides, so
// activation goes back to it.
static ModalShow* modalRelease(CxUiWindow* w)
{
    ModalShow* m = w->_modalShow;
    if (m) {
        w->_modalShow = NULL;
        _cxuiModalEnd(&m->lock);
    }
    return m;
}

// The second half, once the window has hidden
static void modalFinish(CxUiWindow* w, ModalShow* m, int32 result)
{
    if (!m)
        return;
    if (w->peer) {
        _cxuiNativeEnter();
        _cxui.backend->windowEndModal(w->peer);
        _cxuiNativeLeave();
    }
    if (m->onDone) {
        closureCall(m->onDone, stvar(object, w), stvar(int32, result));
        closureDestroy(&m->onDone);
    }
    xaFree(m);
}

enum WindowOps {
    WINOP_Show,
    WINOP_Hide,
    WINOP_Close,
};

static bool windowOpTask(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiWindow* w = stvlAtObj(cvars, 0, CxUiWindow);
    uint32 mode   = stvlAt(cvars, 2, uint32);
    switch (stvlAt(cvars, 1, uint32)) {
    case WINOP_Show:
        CxUiWindow_show(w, mode);
        break;
    case WINOP_Hide:
        CxUiWindow_hide(w);
        break;
    case WINOP_Close:
        CxUiWindow_close(w);
        break;
    }
    return true;
}

// Off the UI thread, posts op to run there and returns true.
static bool windowOffThread(CxUiWindow* w, uint32 op, uint32 mode)
{
    if (cxuiIsUIThread())
        return false;
    if (w)
        cxuiPost(closureCreate(windowOpTask, stvar(object, w), stvar(uint32, op), stvar(uint32, mode)));
    return true;
}

static bool windowLive(CxUiWindow* w)
{
    return _cxuiLive(CxUiWidget(w));
}

static bool themeDark(uint32 mode)
{
    if (mode == CXUI_Theme_System)
        mode = _cxui.backend->systemTheme();
    return mode == CXUI_Theme_Dark;
}

static void requestClose(CxUiWindow* w, uint32 reason)
{
    objAcquire(w);
    CxUiCloseEvent ev = { .base.kind = CXUI_EV_Close, .reason = reason };
    if (_cxuiDispatch(CxUiWidget(w), &ev.base) == CXUI_Default && windowLive(w)) {
        switch (w->closeAction) {
        case CXUI_OnClose_Hide:
            CxUiWindow_hide(w);
            break;
        case CXUI_OnClose_Destroy:
            _cxuiDestroy(CxUiWidget(w));
            break;
        }
    }
    objRelease(&w);
}

// ---- content ----

// The window's theme resolved differently: its drawn widgets repaint, and it is told.
static void themeChanged(CxUiWindow* w)
{
    _cxuiRestyleTree(CxUiWidget(w));
    CxUiThemeChangedEvent ev = { .base.kind = CXUI_EV_ThemeChanged,
                                 .theme     = w->_dark ? CXUI_Theme_Dark : CXUI_Theme_Light };
    _cxuiDispatch(CxUiWidget(w), &ev.base);
}

static void applyPlacement(CxUiWindow* self, const CxUiPlacement* p);

// ---- class ----

_Use_decl_annotations_
void _cxuiWindowSetup(CxUiWindow* self, const CxUiWindowDesc* desc)
{
    strDup(&self->_title, desc->title);
    strDup(&self->name, desc->name);
    self->_askedSize  = (CxUiSize){ max(desc->size.w, 0), max(desc->size.h, 0) };
    self->_size       = self->_askedSize;
    self->_sizing     = desc->sizing <= CXUI_SIZE_Start ? desc->sizing : CXUI_SIZE_Natural;
    self->_flags      = desc->flags;
    self->closeAction = desc->closeAction;
    self->_theme      = desc->theme <= CXUI_Theme_Dark ? desc->theme : CXUI_Theme_System;
    CxUiWidget(self)->_lp.minSize[0] = cxuiLen(max(desc->minSize.w, 0));
    CxUiWidget(self)->_lp.minSize[1] = cxuiLen(max(desc->minSize.h, 0));
}

_objfactory_guaranteed CxUiWindow* CxUiWindow_createWith(_In_opt_ const CxUiWindowDesc* desc)
{
    CxUiWindow* self;
    self = objInstCreate(CxUiWindow);
    if (desc)
        _cxuiWindowSetup(self, desc);

    objInstInit(self);
    return self;
}

_objinit_guaranteed bool CxUiWindow_init(_In_ CxUiWindow* self)
{
    self->state |= CXUI_WS_IsWindow;
    // Autogen begins -----
    return true;
    // Autogen ends -------
}

void CxUiWindow_show(_In_ CxUiWindow* self, uint32 mode)
{
    if (windowOffThread(self, WINOP_Show, mode) || !CxUiWindow_realize(self))
        return;
    if (self->_placeStatePending && (mode == CXUI_SHOW_Normal || mode == CXUI_SHOW_NoActivate)) {
        if (self->_placeState == CXUI_WINDOW_Maximized)
            mode = CXUI_SHOW_Maximized;
        else if (self->_placeState == CXUI_WINDOW_Minimized)
            mode = CXUI_SHOW_Minimized;
    }
    self->_placeStatePending = false;
    self->_visible           = true;
    _cxuiNativeEnter();
    _cxui.backend->windowShow(self->peer, mode);
    _cxuiNativeLeave();
    if (!self->_shownOnce) {
        self->_shownOnce = true;
        // Focus set in a window that isn't being activated would activate it on Windows.
        if (mode == CXUI_SHOW_Normal || mode == CXUI_SHOW_Maximized) {
            CxUiWidget* start = objAcquireFromWeak(CxUiWidget, self->_startFocus);
            if (start && _cxuiWindowOf(start) == self)
                _cxuiFocus(start);
            objRelease(&start);
        }
    }
}

void CxUiWindow_hide(_In_ CxUiWindow* self)
{
    if (windowOffThread(self, WINOP_Hide, 0) || !windowLive(self))
        return;
    popupEnd(self, 0);
    ModalShow* m   = modalRelease(self);
    self->_visible = false;
    if (self->peer) {
        _cxuiNativeEnter();
        _cxui.backend->windowHide(self->peer);
        _cxuiNativeLeave();
    }
    modalFinish(self, m, 0);
}

void CxUiWindow_close(_In_ CxUiWindow* self)
{
    if (windowOffThread(self, WINOP_Close, 0) || !windowLive(self))
        return;
    requestClose(self, CXUI_CLOSE_App);
}

static void menuShow(CxUiWindow* self);

// The owner while it is alive, borrowed
static CxUiWindow* ownerOf(CxUiWindow* w)
{
    CxUiWindow* o = objAcquireFromWeak(CxUiWindow, w->_owner);
    CxUiWindow* r = o && windowLive(o) ? o : NULL;
    objRelease(&o);
    return r;
}

// The owner at the top of w's owners, or w itself
static CxUiWindow* ownerRoot(CxUiWindow* w)
{
    for (CxUiWindow* o = ownerOf(w); o; o = ownerOf(o))
        w = o;
    return w;
}

static void ownerApply(CxUiWindow* self)
{
    if (!self->peer || !_cxui.backend->windowSetOwner)
        return;
    CxUiWindow* o = ownerOf(self);
    if (o && !CxUiWindow_realize(o))
        o = NULL;
    _cxuiNativeEnter();
    _cxui.backend->windowSetOwner(self->peer, o ? o->peer : NULL);
    _cxuiNativeLeave();
}

// other NULL is the owner, or the screen.
static bool moveNearNow(CxUiWindow* self, CxUiWindow* other, uint32 where)
{
    if (!other)
        other = ownerOf(self);
    if (other && !CxUiWindow_realize(other))
        other = NULL;
    if (!other && where != CXUI_NEAR_Center)
        return false;
    // At the size its content needs now
    if (self->_flushQueued)
        _cxuiLayoutFlush(self);
    _cxuiNativeEnter();
    bool ok = _cxui.backend->windowMoveNear(self->peer, other ? other->peer : NULL, where);
    _cxuiNativeLeave();
    if (ok && _cxui.backend->windowGetPosition)
        self->_hasPos = _cxui.backend->windowGetPosition(self->peer, &self->_pos);
    return ok;
}

bool CxUiWindow_realize(_In_ CxUiWindow* self)
{
    if (!windowLive(self))
        return false;
    if (self->peer)
        return true;

    self->_dark    = themeDark(self->_theme);
    self->_size    = _cxuiLayoutStartSize(self);
    CxUiPeer* p    = _cxui.backend->windowCreate(self);
    if (!p)
        return false;
    self->_fitSize = self->_size;
    _cxuiPeerAttach(CxUiWidget(self), p);
    menuShow(self);
    cxuiWidgetRestyle(self);
    self->_needWalk = true;
    _cxuiLayoutFlush(self);
    if (self->_place.valid) {
        CxUiPlacement pl   = self->_place;
        self->_place.valid = 0;
        applyPlacement(self, &pl);
    }
    ownerApply(self);
    if (self->_nearPending) {
        self->_nearPending = false;
        CxUiWindow* other  = self->_nearGiven ? objAcquireFromWeak(CxUiWindow, self->_nearOther) : NULL;
        if (!self->_nearGiven || other)
            moveNearNow(self, other, self->_near);
        objRelease(&other);
    }

    if (!self->_registered) {
        if (!realized.a)
            saInit(&realized, object, 8);
        saPush(&realized, object, self);
        self->_registered = true;
    }
    return true;
}

// The menu bar on the window's frame, realized as it gets there
static void menuShow(CxUiWindow* self)
{
    if (!self->peer || !self->_menu || !_cxui.backend->windowSetMenu)
        return;
    _cxuiMenuRealize(self->_menu, true);
    if (CxUiWidget(self->_menu)->peer)
        _cxui.backend->windowSetMenu(self->peer, CxUiWidget(self->_menu)->peer);
}

// Lets go of the menu bar: the frame first, then the menu's own native menus
static void menuDrop(CxUiWindow* self)
{
    CxUiMenu* old = self->_menu;
    if (!old)
        return;
    self->_menu = NULL;
    if (self->peer && _cxui.backend->windowSetMenu)
        _cxui.backend->windowSetMenu(self->peer, NULL);
    CxUiWidget(old)->parent = NULL;
    _cxuiMenuUnrealize(old);
    _cxuiLayoutEnabledChanged(CxUiWidget(old));
    objRelease(&old);
}

_Use_decl_annotations_
void _cxuiWindowDestroyMenu(CxUiWindow* w)
{
    if (w->_menu)
        _cxuiDestroy(CxUiWidget(w->_menu));
}

bool CxUiWindow_isVisible(_In_ CxUiWindow* self)
{
    return windowLive(self) && self->_visible;
}

strref CxUiWindow_title(_In_ CxUiWindow* self)
{
    return windowLive(self) ? self->_title : NULL;
}

void CxUiWindow_setTitle(_In_ CxUiWindow* self, _In_ strref title)
{
    if (!windowLive(self))
        return;
    strDup(&self->_title, title);
    if (self->peer)
        _cxui.backend->windowSetTitle(self->peer, self->_title);
}

CxUiMenu* CxUiWindow_menu(_In_ CxUiWindow* self)
{
    return windowLive(self) ? self->_menu : NULL;
}

void CxUiWindow_setMenu(_In_ CxUiWindow* self, _In_opt_ CxUiMenu* menu)
{
    if (!windowLive(self) || menu == self->_menu ||
        (menu && (!_cxuiLive(CxUiWidget(menu)) || CxUiWidget(menu)->parent)))
        return;
    menuDrop(self);
    if (!menu)
        return;
    self->_menu              = objAcquire(menu);
    CxUiWidget(menu)->parent = CxUiWidget(self);
    _cxuiLayoutEnabledChanged(CxUiWidget(menu));
    menuShow(self);
}

bool CxUiWindow_size(_In_ CxUiWindow* self, _Out_ CxUiSize* out)
{
    *out = (CxUiSize){ 0 };
    if (!windowLive(self) || (self->_size.w == 0 && self->_size.h == 0))
        return false;
    *out = self->_size;
    return true;
}

void CxUiWindow_setSize(_In_ CxUiWindow* self, int32 width, int32 height)
{
    if (!windowLive(self))
        return;
    self->_askedSize = (CxUiSize){ max(width, 0), max(height, 0) };
    self->_size      = self->_askedSize;
    self->_userSized = true;
    if (self->peer) {
        self->_fitSize = self->_size;
        _cxuiNativeEnter();
        _cxui.backend->windowSetSize(self->peer, self->_size);
        _cxuiNativeLeave();
        // The minimum may have kept it larger.
        _cxuiLayoutDirty(CxUiWidget(self));
    }
}

bool CxUiWindow_position(_In_ CxUiWindow* self, _Out_ CxUiPoint* out)
{
    *out = (CxUiPoint){ 0 };
    if (!windowLive(self) || !_cxui.backend->windowGetPosition)
        return false;
    if (self->peer)
        return _cxui.backend->windowGetPosition(self->peer, out);
    if (!self->_hasPos)
        return false;
    *out = self->_pos;
    return true;
}

bool CxUiWindow_setPosition(_In_ CxUiWindow* self, int32 x, int32 y)
{
    if (!windowLive(self) || !_cxui.backend->windowSetPosition)
        return false;
    self->_pos         = (CxUiPoint){ x, y };
    self->_hasPos      = true;
    self->_nearPending = false;
    if (!self->peer)
        return true;

    _cxuiNativeEnter();
    bool ok = _cxui.backend->windowSetPosition(self->peer, self->_pos);
    _cxuiNativeLeave();
    return ok;
}

flags_t cxuiPlacementCaps(void)
{
    return _cxui.backend ? _cxui.backend->placementCaps : 0;
}

bool CxUiWindow_placement(_In_ CxUiWindow* self, _Out_ CxUiPlacement* out)
{
    *out = (CxUiPlacement){ 0 };
    if (!windowLive(self))
        return false;
    if (!self->peer) {
        *out = self->_place;
        return out->valid != 0;
    }
    if (!_cxui.backend->windowGetPlacement)
        return false;

    _cxui.backend->windowGetPlacement(self->peer, out);
    out->valid &= _cxui.backend->placementCaps;
    if (self->_placeStatePending && !self->_visible && (out->valid & CXUI_PLACE_State))
        out->state = self->_placeState;
    return true;
}

// The parts not given are the window's current ones, so the whole rectangle can be checked
// against the monitors.
static void applyPlacement(CxUiWindow* self, const CxUiPlacement* p)
{
    flags_t caps     = _cxui.backend->placementCaps;
    CxUiPlacement pl = *p;
    pl.valid &= caps;
    if (pl.normal.w <= 0 || pl.normal.h <= 0)
        pl.valid &= ~CXUI_PLACE_Size;
    if (pl.valid & (CXUI_PLACE_Position | CXUI_PLACE_Size)) {
        CxUiPlacement cur = { 0 };
        _cxui.backend->windowGetPlacement(self->peer, &cur);
        if (!(pl.valid & CXUI_PLACE_Position)) {
            pl.normal.x = cur.normal.x;
            pl.normal.y = cur.normal.y;
        }
        if (!(pl.valid & CXUI_PLACE_Size)) {
            pl.normal.w = cur.normal.w;
            pl.normal.h = cur.normal.h;
        }
        pl.valid |= cur.valid & caps & (CXUI_PLACE_Position | CXUI_PLACE_Size);
        _cxuiPlacementClamp(&pl);
        if (pl.valid & CXUI_PLACE_Size)
            self->_userSized = true;
    }

    bool withState = (pl.valid & CXUI_PLACE_State) && self->_visible;
    if ((pl.valid & CXUI_PLACE_State) && !self->_visible) {
        self->_placeState        = pl.state;
        self->_placeStatePending = true;
    }
    _cxuiNativeEnter();
    _cxui.backend->windowSetPlacement(self->peer, &pl, withState);
    _cxuiNativeLeave();
    _cxuiLayoutDirty(CxUiWidget(self));
}

void CxUiWindow_setPlacement(_In_ CxUiWindow* self, _In_ const CxUiPlacement* p)
{
    if (!windowLive(self) || !_cxui.backend || !_cxui.backend->windowSetPlacement)
        return;
    if (self->peer) {
        applyPlacement(self, p);
        return;
    }

    CxUiPlacement* stored = &self->_place;
    flags_t valid         = p->valid & _cxui.backend->placementCaps;
    if (valid & CXUI_PLACE_Position) {
        stored->normal.x = p->normal.x;
        stored->normal.y = p->normal.y;
    }
    if (valid & CXUI_PLACE_Size) {
        stored->normal.w = p->normal.w;
        stored->normal.h = p->normal.h;
    }
    if (valid & CXUI_PLACE_State)
        stored->state = p->state;
    stored->valid |= valid;
    if (valid & CXUI_PLACE_Position)
        self->_nearPending = false;
}

bool CxUiWindow_moveNear(_In_ CxUiWindow* self, _In_opt_ CxUiWindow* other, uint32 where)
{
    if (!windowLive(self) || !_cxui.backend || !_cxui.backend->windowMoveNear || other == self ||
        where > CXUI_NEAR_Left || (other && !windowLive(other)))
        return false;
    if (self->peer)
        return moveNearNow(self, other, where);

    // A later position replaces it, and it replaces an earlier one.
    self->_near        = where;
    self->_nearGiven   = other != NULL;
    self->_nearPending = true;
    self->_hasPos      = false;
    self->_place.valid &= ~CXUI_PLACE_Position;
    objDestroyWeak(&self->_nearOther);
    if (other)
        self->_nearOther = objGetWeak(CxUiWindow, other);
    return true;
}

CxUiWindow* CxUiWindow_owner(_In_ CxUiWindow* self)
{
    return windowLive(self) ? ownerOf(self) : NULL;
}

void CxUiWindow_setOwner(_In_ CxUiWindow* self, _In_opt_ CxUiWindow* owner)
{
    if (!windowLive(self) || (owner && !windowLive(owner)))
        return;
    for (CxUiWindow* o = owner; o; o = ownerOf(o)) {
        if (o == self)
            return;
    }
    objDestroyWeak(&self->_owner);
    if (owner)
        self->_owner = objGetWeak(CxUiWindow, owner);
    ownerApply(self);
}

_Use_decl_annotations_
bool cxuiTestOwnedBy(CxUiWindow* w, CxUiWindow* owner, bool* owned)
{
    *owned = false;
    return _cxui.backend && _cxui.backend->testOwnedBy && w->peer && (!owner || owner->peer) &&
           _cxui.backend->testOwnedBy(w->peer, owner ? owner->peer : NULL, owned);
}

int32 CxUiWindow_popup(_In_ CxUiWindow* self, _In_opt_ CxUiWindow* parent)
{
    if (!windowLive(self) || self->_popup || parent == self || atomicLoad(bool, &_cxui.exitRequested, Acquire))
        return 0;
    if (parent && !(windowLive(parent) && CxUiWindow_realize(parent)))
        parent = NULL;
    if (!CxUiWindow_realize(self))
        return 0;

    objAcquire(self);
    PopupFrame f = { 0 };
    self->_popup = &f;
    CxUiModalLock lock;
    _cxuiModalBegin(&lock, parent, self);
    _cxuiNativeEnter();
    _cxui.backend->windowBeginModal(self->peer, parent ? parent->peer : NULL);
    _cxuiNativeLeave();
    CxUiWindow_show(self, CXUI_SHOW_Normal);

    // An outer popup ended while this one runs keeps waiting here until this one ends.
    _cxuiModalLoopStarting();
    _cxui.backend->runNested(&f.done);

    self->_popup = NULL;
    // The owner first, so activation goes back to it when the popup hides
    _cxuiModalEnd(&lock);
    CxUiWindow_hide(self);
    if (self->peer) {
        _cxuiNativeEnter();
        _cxui.backend->windowEndModal(self->peer);
        _cxuiNativeLeave();
    }
    objRelease(&self);
    return f.result;
}

bool CxUiWindow_showModal(_In_ CxUiWindow* self, _In_opt_ CxUiWindow* parent, _In_opt_ closure onDone)
{
    if (!windowLive(self) || self->_popup || self->_modalShow || parent == self ||
        atomicLoad(bool, &_cxui.exitRequested, Acquire) || !CxUiWindow_realize(self)) {
        closureDestroy(&onDone);
        return false;
    }
    if (parent && !(windowLive(parent) && CxUiWindow_realize(parent)))
        parent = NULL;

    ModalShow* m     = xaAllocStruct(ModalShow, XA_Zero);
    m->onDone        = onDone;
    self->_modalShow = m;
    _cxuiModalBegin(&m->lock, parent, self);
    _cxuiNativeEnter();
    _cxui.backend->windowBeginModal(self->peer, parent ? parent->peer : NULL);
    _cxuiNativeLeave();
    CxUiWindow_show(self, CXUI_SHOW_Normal);
    return true;
}

void CxUiWindow_endPopup(_In_ CxUiWindow* self, int32 result)
{
    if (!windowLive(self))
        return;
    popupEnd(self, result);
    ModalShow* m = modalRelease(self);
    if (m) {
        CxUiWindow_hide(self);
        modalFinish(self, m, result);
    }
}

uint32 CxUiWindow_themeMode(_In_ CxUiWindow* self)
{
    return windowLive(self) ? self->_theme : CXUI_Theme_System;
}

void CxUiWindow_setThemeMode(_In_ CxUiWindow* self, uint32 mode)
{
    if (!windowLive(self) || mode > CXUI_Theme_Dark)
        return;
    self->_theme = mode;
    if (self->peer) {
        bool dark   = themeDark(mode);
        bool change = dark != self->_dark;
        self->_dark = dark;
        _cxui.backend->windowSetDark(self->peer, dark);
        if (change)
            themeChanged(self);
    }
}

uint32 CxUiWindow_resolvedTheme(_In_ CxUiWindow* self)
{
    if (!windowLive(self))
        return CXUI_Theme_Light;
    bool dark = self->peer ? self->_dark : themeDark(self->_theme);
    return dark ? CXUI_Theme_Dark : CXUI_Theme_Light;
}

extern void CxUiWidget_restyle(_In_ CxUiWidget* self);   // parent
#define parent_restyle() CxUiWidget_restyle((CxUiWidget*)(self))
void CxUiWindow_restyle(_In_ CxUiWindow* self)
{
    if (self->peer && _cxui.backend->windowSetBackground)
        _cxui.backend->windowSetBackground(self->peer, cxuiPaletteColor(cxuiWidgetPalette(self), CXUI_PAL_Panel));
}

extern uint32 CxUiWidget_eventMask(_In_ CxUiWidget* self);   // parent
#define parent_eventMask() CxUiWidget_eventMask((CxUiWidget*)(self))
uint32 CxUiWindow_eventMask(_In_ CxUiWindow* self)
{
    return parent_eventMask() | (1 << CXUI_EV_Wheel) | (1 << CXUI_EV_Resize) | (1 << CXUI_EV_Close) |
           (1 << CXUI_EV_ShowState) | (1 << CXUI_EV_Move) | (1 << CXUI_EV_DpiChanged) |
           (1 << CXUI_EV_ThemeChanged);
}

static bool windowSetContent(CxUiWindow* self, CxUiWidget* child)
{
    if (!windowLive(self) || !_cxuiLive(child) || child->parent || self->_content ||
        objDynCast(CxUiWindow, child))
        return false;
    self->_content = objAcquire(child);
    child->parent  = CxUiWidget(self);
    _cxuiLayoutAttached(CxUiWidget(self), child);
    return true;
}

bool CxUiWindow_append(_In_ CxUiWindow* self, _In_ CxUiWidget* child)
{
    return windowSetContent(self, child);
}

bool CxUiWindow_insert(_In_ CxUiWindow* self, _In_ CxUiWidget* child, int32 at)
{
    unused_noeval(at);
    return windowSetContent(self, child);
}

bool CxUiWindow_detach(_In_ CxUiWindow* self, _In_ CxUiWidget* child)
{
    if (child && child == CxUiWidget(self->_menu)) {
        menuDrop(self);
        return true;
    }
    if (!child || child != self->_content)
        return false;
    CxUiWidget* c  = self->_content;
    self->_content = NULL;
    c->parent      = NULL;
    _cxuiLayoutDetached(CxUiWidget(self), c);
    objRelease(&c);
    return true;
}

int32 CxUiWindow_childCount(_In_ CxUiWindow* self)
{
    return self->_content ? 1 : 0;
}

_Ret_opt_valid_ CxUiWidget* CxUiWindow_child(_In_ CxUiWindow* self, int32 i)
{
    return i == 0 ? self->_content : NULL;
}

void CxUiWindow_destroy(_In_ CxUiWindow* self)
{
    xaFree(self->_groups);
    // Autogen begins -----
    objRelease(&self->_content);
    objRelease(&self->_menu);
    strDestroy(&self->_title);
    objDestroyWeak(&self->_owner);
    objDestroyWeak(&self->_nearOther);
    objDestroyWeak(&self->_defaultEnter);
    objDestroyWeak(&self->_defaultEsc);
    objDestroyWeak(&self->_startFocus);
    // Autogen ends -------
}

// ---- runtime ----

_Use_decl_annotations_
void _cxuiWindowDestroyed(CxUiWindow* w)
{
    popupEnd(w, 0);
    ModalShow* m = modalRelease(w);
    w->_visible  = false;
    if (w->peer) {
        _cxuiNativeEnter();
        _cxui.backend->windowHide(w->peer);
        _cxuiNativeLeave();
    }
    modalFinish(w, m, 0);

    if (w->_registered) {
        w->_registered = false;
        int32 i        = saFind(realized, object, w);
        if (i >= 0)
            saRemove(&realized, i);
    }
}

static void windowLock(CxUiWindow* w)
{
    if (w->_modalLocks++ == 0 && w->peer && _cxui.backend->windowSetEnabled)
        _cxui.backend->windowSetEnabled(w->peer, false);
}

static void windowUnlock(CxUiWindow* w)
{
    if (--w->_modalLocks == 0 && w->peer && _cxui.backend->windowSetEnabled)
        _cxui.backend->windowSetEnabled(w->peer, true);
}

_Use_decl_annotations_
void _cxuiModalBegin(CxUiModalLock* lock, CxUiWindow* parent, CxUiWindow* except)
{
    saInit(&lock->windows, object, saSize(realized) + 1);
    if (parent) {
        // The parent, and the windows it belongs with: those with the same owner at the top
        CxUiWindow* root = ownerRoot(parent);
        saPush(&lock->windows, object, parent);
        for (int32 i = 0; i < saSize(realized); i++) {
            CxUiWindow* w = realized.a[i];
            if (w != parent && w != except && ownerRoot(w) == root)
                saPush(&lock->windows, object, w);
        }
    } else {
        for (int32 i = 0; i < saSize(realized); i++) {
            if (realized.a[i] != except)
                saPush(&lock->windows, object, realized.a[i]);
        }
    }
    for (int32 i = 0; i < saSize(lock->windows); i++)
        windowLock(lock->windows.a[i]);
}

_Use_decl_annotations_
void _cxuiModalEnd(CxUiModalLock* lock)
{
    for (int32 i = 0; i < saSize(lock->windows); i++)
        windowUnlock(lock->windows.a[i]);
    saDestroy(&lock->windows);
}

void _cxuiSystemThemeChanged(void)
{
    // A handler may destroy windows, which takes them off the list.
    sa_CxUiWindow wins;
    saClone(&wins, realized);
    for (int32 i = 0; i < saSize(wins); i++) {
        CxUiWindow* w = wins.a[i];
        if (!windowLive(w) || w->_theme != CXUI_Theme_System || !w->peer)
            continue;
        bool dark = themeDark(CXUI_Theme_System);
        if (dark == w->_dark)
            continue;
        w->_dark = dark;
        _cxui.backend->windowSetDark(w->peer, dark);
        themeChanged(w);
    }
    saDestroy(&wins);
}

CxUiThemeMode cxuiSystemTheme(void)
{
    return _cxui.backend ? _cxui.backend->systemTheme() : CXUI_Theme_Light;
}

void _cxuiWindowsEndSession(void)
{
    // Modal shows end with 0, as popups do. Copies, as a window leaves the list when destroyed,
    // which an onDone handler may do.
    sa_CxUiWindow closing;
    saInit(&closing, object, saSize(realized) + 1);
    for (int32 i = 0; i < saSize(realized); i++) {
        if (realized.a[i]->_modalShow)
            saPush(&closing, object, realized.a[i]);
    }
    for (int32 i = 0; i < saSize(closing); i++)
        modalFinish(closing.a[i], modalRelease(closing.a[i]), 0);
    saClear(&closing);

    for (int32 i = 0; i < saSize(realized); i++) {
        if (realized.a[i]->closeAction == CXUI_OnClose_Destroy)
            saPush(&closing, object, realized.a[i]);
    }
    for (int32 i = 0; i < saSize(closing); i++)
        _cxuiDestroy(CxUiWidget(closing.a[i]));
    saDestroy(&closing);
}

void _cxuiWindowsLayoutQueued(void)
{
    // A copy, as a pass can realize or destroy windows
    sa_CxUiWindow queued;
    saInit(&queued, object, 4);
    for (int32 i = 0; i < saSize(realized); i++) {
        if (realized.a[i]->_flushQueued)
            saPush(&queued, object, realized.a[i]);
    }
    for (int32 i = 0; i < saSize(queued); i++)
        _cxuiLayoutFlush(queued.a[i]);
    saDestroy(&queued);
}

void _cxuiWindowsRelease(void)
{
    for (int32 i = 0; i < saSize(realized); i++) {
        realized.a[i]->_registered = false;
        realized.a[i]->_visible    = false;
    }
    saDestroy(&realized);
}

// ---- scale ----

_Use_decl_annotations_
float32 _cxuiWidgetScale(CxUiWidget* w)
{
    CxUiWindow* win = w && _cxuiLive(w) ? _cxuiWindowOf(w) : NULL;
    if (win && win->peer && win->_scale > 0)
        return win->_scale;
    if (_cxui.backend && _cxui.backend->systemScale)
        return _cxui.backend->systemScale();
    return 1.0f;
}

_Use_decl_annotations_
int32 _cxuiDip(CxUiWidget* w, int32 px)
{
    return (int32)floor(px / _cxuiWidgetScale(w) + 0.5);
}

_Use_decl_annotations_
int32 _cxuiPx(CxUiWidget* w, int32 dip)
{
    return (int32)floor(dip * _cxuiWidgetScale(w) + 0.5);
}

// ---- native events ----

static CxUiWindow* peerWindow(CxUiPeer* p)
{
    return p->w ? objDynCast(CxUiWindow, p->w) : NULL;
}

_Use_decl_annotations_
void _cxuiWindowCloseRequested(CxUiPeer* p, uint32 reason)
{
    // A window a modal belongs to stays open; Windows disables its close box, other platforms don't.
    CxUiWindow* w = peerWindow(p);
    if (w && windowLive(w) && w->_modalLocks == 0)
        requestClose(w, reason);
}

_Use_decl_annotations_
void _cxuiWindowResized(CxUiPeer* p, CxUiSize px, CxUiSize dips)
{
    CxUiWindow* w = peerWindow(p);
    if (!w || (CxUiWidget(w)->state & CXUI_WS_Dead))
        return;
    // A size layout didn't ask for is the user's (or a maximize): natural sizing stops refitting.
    if (abs(dips.w - w->_fitSize.w) > 1 || abs(dips.h - w->_fitSize.h) > 1)
        w->_userSized = true;
    w->_size     = dips;
    w->_clientPx = px;
    objAcquire(w);
    _cxuiLayoutFlush(w);
    if (windowLive(w)) {
        CxUiResizeEvent ev = { .base.kind = CXUI_EV_Resize, .size = px };
        _cxuiDispatch(CxUiWidget(w), &ev.base);
    }
    objRelease(&w);
}

_Use_decl_annotations_
void _cxuiWindowDpiChanged(CxUiPeer* p, float32 scale, CxUiSize px)
{
    CxUiWindow* w = peerWindow(p);
    if (!w || scale <= 0)
        return;
    float32 old    = w->_scale;
    bool resized   = px.w != w->_clientPx.w || px.h != w->_clientPx.h;
    w->_scale      = scale;
    w->_clientPx   = px;
    w->_size       = (CxUiSize){ (int32)floor(px.w / scale + 0.5), (int32)floor(px.h / scale + 0.5) };

    objAcquire(w);
    w->_fitSize = w->_size;
    _cxuiLayoutFlush(w);
    _cxuiRestyleTree(CxUiWidget(w));
    if (resized) {
        CxUiResizeEvent rev = { .base.kind = CXUI_EV_Resize, .size = px };
        _cxuiDispatch(CxUiWidget(w), &rev.base);
    }
    if (scale != old && windowLive(w)) {
        CxUiDpiChangedEvent ev = { .base.kind = CXUI_EV_DpiChanged, .scale = scale, .oldScale = old };
        _cxuiDispatch(CxUiWidget(w), &ev.base);
    }
    objRelease(&w);
}

_Use_decl_annotations_
void _cxuiWindowMoved(CxUiPeer* p, CxUiPoint pos)
{
    CxUiWindow* w = peerWindow(p);
    if (!w)
        return;
    w->_pos          = pos;
    w->_hasPos       = true;
    CxUiMoveEvent ev = { .base.kind = CXUI_EV_Move, .pos = pos };
    _cxuiDispatch(CxUiWidget(w), &ev.base);
}

_Use_decl_annotations_
void _cxuiWindowStateChanged(CxUiPeer* p, uint32 state)
{
    CxUiWindow* w = peerWindow(p);
    if (!w)
        return;
    CxUiShowStateEvent ev = { .base.kind = CXUI_EV_ShowState, .state = state };
    _cxuiDispatch(CxUiWidget(w), &ev.base);
}

_Use_decl_annotations_
void _cxuiWindowActivated(CxUiPeer* p, bool active)
{
    CxUiWindow* w = peerWindow(p);
    if (!w)
        return;
    CxUiFocusEvent ev = { .base.kind = CXUI_EV_Focus, .gained = active };
    _cxuiDispatch(CxUiWidget(w), &ev.base);
}

_Use_decl_annotations_
bool _cxuiWindowUserResizable(CxUiWindow* w)
{
    return w->_sizing != CXUI_SIZE_Fixed && !(w->_flags & CXUI_WINDOW_Borderless);
}

void CxUiWindow_setSizing(_In_ CxUiWindow* self, uint32 sizing)
{
    if (!windowLive(self) || sizing > CXUI_SIZE_Start || sizing == self->_sizing)
        return;
    bool was      = _cxuiWindowUserResizable(self);
    self->_sizing = sizing;
    bool now      = _cxuiWindowUserResizable(self);
    if (self->peer && was != now && _cxui.backend->windowSetResizable)
        _cxui.backend->windowSetResizable(self->peer, now);
    _cxuiLayoutDirty(CxUiWidget(self));
}

bool CxUiWindow_beginDrag(_In_ CxUiWindow* self)
{
    if (!windowLive(self) || !self->peer || !_cxui.backend->windowBeginDrag || !_cxuiCaptureCancel(self))
        return false;
    _cxuiNativeEnter();
    bool ok = _cxui.backend->windowBeginDrag(self->peer);
    _cxuiNativeLeave();
    return ok;
}

void CxUiWindow_fit(_In_ CxUiWindow* self, flags_t flags)
{
    if (!windowLive(self))
        return;
    CxUiSize n = _cxuiLayoutFitSize(self);
    CxUiSize s = (flags & CXUI_FIT_Shrink) ? n : (CxUiSize){ max(self->_size.w, n.w), max(self->_size.h, n.h) };
    if (s.w == self->_size.w && s.h == self->_size.h)
        return;
    self->_size = s;
    if (self->peer) {
        self->_fitSize = s;
        _cxuiNativeEnter();
        _cxui.backend->windowSetSize(self->peer, s);
        _cxuiNativeLeave();
    }
}

static void setWeak(Weak(CxUiWidget)** ref, CxUiWidget* w)
{
    objDestroyWeak(ref);
    if (w)
        *ref = objGetWeak(CxUiWidget, w);
}

static void showDefault(CxUiWidget* w, bool isDefault)
{
    CxUiButton* b = w ? objDynCast(CxUiButton, w) : NULL;
    if (!b || !_cxuiLive(w))
        return;
    b->_isDefault = isDefault;
    if (w->peer)
        _cxui.backend->buttonSetDefault(w->peer, isDefault);
}

void CxUiWindow_setDefaultEnter(_In_ CxUiWindow* self, _In_opt_ CxUiWidget* w)
{
    if (!windowLive(self))
        return;
    CxUiWidget* old = objAcquireFromWeak(CxUiWidget, self->_defaultEnter);
    if (old != w) {
        showDefault(old, false);
        setWeak(&self->_defaultEnter, w);
        showDefault(w, true);
    }
    objRelease(&old);
}

void CxUiWindow_setDefaultEsc(_In_ CxUiWindow* self, _In_opt_ CxUiWidget* w)
{
    if (windowLive(self))
        setWeak(&self->_defaultEsc, w);
}

void CxUiWindow_setStartFocus(_In_ CxUiWindow* self, _In_opt_ CxUiWidget* w)
{
    if (windowLive(self))
        setWeak(&self->_startFocus, w);
}

// What a window does with a key nothing along the way took: the default Enter and Esc buttons,
// Esc closing a popup, and mnemonics.
_Use_decl_annotations_
bool _cxuiWindowKey(CxUiWindow* self, CxUiKey key, uint32 mods, bool repeat)
{
    if (!windowLive(self) || repeat)
        return false;
    if (mods == 0 && (key == CXUI_KEY_Enter || key == CXUI_KEY_PadEnter || key == CXUI_KEY_Escape)) {
        CxUiWidget* d = objAcquireFromWeak(CxUiWidget, key == CXUI_KEY_Escape ? self->_defaultEsc : self->_defaultEnter);
        bool done     = _cxuiPress(d);
        objRelease(&d);
        if (!done && key == CXUI_KEY_Escape && (self->_popup || self->_modalShow) && self->peer) {
            _cxuiWindowCloseRequested(self->peer, CXUI_CLOSE_User);
            done = true;
        }
        return done;
    }
    if (mods == CXUI_MOD_Alt && _cxui.backend->mnemonics)
        return _cxuiMnemonicRun(self, key);
    return false;
}


// Autogen begins -----
// clang-format off
#include "core/cxuiwindow.auto.inc"
// clang-format on
// Autogen ends -------
