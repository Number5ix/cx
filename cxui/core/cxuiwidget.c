// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "core/cxuiwidget.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cxui_private.h"

#include <cx/utils/lazyinit.h>

// A widget's last reference can drop on any thread, so the link between a widget and its peer,
// and the list of peers still to destroy, are guarded by this lock. Peers are only created and
// destroyed on the UI thread.
static Mutex peerLock;
static CxUiPeer* livePeers;

// Widgets whose peer teardown waits for the running handlers to return.
static sa_CxUiWidget deferred;

static LazyInitState widgetInitState;

static void cxuiWidgetInitOnce(void* unused)
{
    unused_noeval(unused);
    mutexInit(&peerLock);
}

void _cxuiWidgetInit(void)
{
    lazyInit(&widgetInitState, cxuiWidgetInitOnce, NULL);
}

_Use_decl_annotations_
bool _cxuiLive(CxUiWidget* w)
{
    if (!w)
        return false;
    devAssertMsg(cxuiIsUIThread(), "cxui widget called off the UI thread");
    return !(w->state & CXUI_WS_Dead);
}

// ---- peers ----

static void peerUnlink(CxUiPeer* p)
{
    if (p->prev)
        p->prev->next = p->next;
    else
        livePeers = p->next;
    if (p->next)
        p->next->prev = p->prev;
    p->prev = p->next = NULL;
    p->live = false;
}

_Use_decl_annotations_
void _cxuiPeerAttach(CxUiWidget* w, CxUiPeer* p)
{
    _cxuiWidgetInit();
    mutexAcquire(&peerLock);
    devAssert(!w->peer);
    p->w    = w;
    p->prev = NULL;
    p->next = livePeers;
    if (livePeers)
        livePeers->prev = p;
    livePeers = p;
    p->live   = true;
    w->peer   = p;
    mutexRelease(&peerLock);
}

// UI thread. Destroys p unless the end of the session already has.
static void peerDestroy(CxUiPeer* p)
{
    mutexAcquire(&peerLock);
    bool live = p->live;
    if (live) {
        if (p->w)
            p->w->peer = NULL;
        p->w = NULL;
        peerUnlink(p);
    }
    mutexRelease(&peerLock);

    if (live)
        _cxui.backend->peerDestroy(p);
}

static void unrealizeNow(CxUiWidget* w)
{
    CxUiPeer* p = w->peer;   // only the UI thread changes it while the widget is referenced
    if (p)
        peerDestroy(p);
}

_Use_decl_annotations_
void _cxuiUnrealize(CxUiWidget* w)
{
    if (!w->peer)
        return;
    _cxuiSurfacesLost(w);
    _cxuiInputForget(w);

    if (_cxui.dispatchDepth == 0) {
        unrealizeNow(w);
        return;
    }

    if (!deferred.a)
        saInit(&deferred, object, 16);
    saPush(&deferred, object, w);
}

void _cxuiFlushDeferred(void)
{
    devAssert(_cxui.dispatchDepth == 0);
    // An entry's teardown can't add another: no handler runs during it.
    for (int32 i = 0; i < saSize(deferred); i++)
        unrealizeNow(deferred.a[i]);
    saClear(&deferred);
}

void _cxuiPeersDestroyAll(void)
{
    _cxuiWidgetInit();
    for (;;) {
        mutexAcquire(&peerLock);
        CxUiPeer* p = livePeers;
        if (p) {
            if (p->w)
                p->w->peer = NULL;
            p->w = NULL;
            peerUnlink(p);
        }
        mutexRelease(&peerLock);

        if (!p)
            break;
        _cxui.backend->peerDestroy(p);
    }
}

static bool peerTeardownTask(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    peerDestroy(stvlAtPtr(cvars, 0));
    return true;
}

// ---- destroy ----

_Use_decl_annotations_
void _cxuiDestroy(CxUiWidget* w)
{
    if (!_cxuiLive(w))
        return;

    objAcquire(w);   // the parent's reference goes when it is detached below
    // While handlers are still there to hear it
    _cxuiSurfacesLost(w);

    CxUiContainer* cont = objInstIf(w, CxUiContainer);
    if (cont) {
        // Each child detaches itself from w as it is destroyed.
        for (int32 i = cont->childCount(w) - 1; i >= 0; i--) {
            CxUiWidget* child = cont->child(w, i);
            if (child)
                _cxuiDestroy(child);
        }
    }

    // Dead first, so anything a notification does to this widget is a harmless no-op.
    w->state |= CXUI_WS_Dead;
    cchainCallOnce(&w->destroyNotify, stvar(object, w));
    _cxuiClearHandlers(w);
    // Once dead: taking the bar off resizes the client area, which is no news by then.
    if (w->state & CXUI_WS_IsWindow)
        _cxuiWindowDestroyMenu((CxUiWindow*)w);
    _cxuiUnrealize(w);
    if (w->state & CXUI_WS_IsWindow)
        _cxuiWindowDestroyed((CxUiWindow*)w);

    if (w->parent) {
        CxUiContainer* pcont = objInstIf(w->parent, CxUiContainer);
        if (pcont)
            pcont->detach(w->parent, w);
    }

    objRelease(&w);
}

// ---- containers ----

static CxUiContainer* containerOf(CxUiWidget* c)
{
    return _cxuiLive(c) ? objInstIf(c, CxUiContainer) : NULL;
}

_Use_decl_annotations_
bool _cxuiContainerInsert(CxUiWidget* c, CxUiWidget* child, int32 at)
{
    CxUiContainer* cont = containerOf(c);
    if (!cont || !_cxuiLive(child) || child->parent)
        return false;
    return at < 0 ? cont->append(c, child) : cont->insert(c, child, at);
}

_Use_decl_annotations_
bool _cxuiContainerDetach(CxUiWidget* c, CxUiWidget* child)
{
    CxUiContainer* cont = containerOf(c);
    return cont && child && child->parent == c && cont->detach(c, child);
}

_Use_decl_annotations_
int32 _cxuiContainerChildCount(CxUiWidget* c)
{
    CxUiContainer* cont = containerOf(c);
    return cont ? cont->childCount(c) : 0;
}

_Use_decl_annotations_
CxUiWidget* _cxuiContainerChild(CxUiWidget* c, int32 i)
{
    CxUiContainer* cont = containerOf(c);
    return cont ? cont->child(c, i) : NULL;
}

// ---- class ----

_objinit_guaranteed bool CxUiWidget_init(_In_ CxUiWidget* self)
{
    self->state |= CXUI_WS_MeasureDirty | CXUI_WS_ArrangeDirty;
    // Autogen begins -----
    return true;
    // Autogen ends -------
}

uint32 CxUiWidget_eventMask(_In_ CxUiWidget* self)
{
    return (1 << CXUI_EV_Key) | (1 << CXUI_EV_Focus);
}

CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self)
{
    CxUiSize out = { 0 };
    for (int32 i = 0, n = _cxuiContainerChildCount(self); i < n; i++) {
        CxUiWidget* child = _cxuiContainerChild(self, i);
        if (!child || !_cxuiLayoutTakesSpace(child))
            continue;
        CxUiSize s = _cxuiLayoutNatural(child);
        out.w      = max(out.w, s.w);
        out.h      = max(out.h, s.h);
        self->_expand |= child->_expand;
    }
    return out;
}

int32 CxUiWidget_heightFor(_In_ CxUiWidget* self, int32 width)
{
    int32 n = _cxuiContainerChildCount(self), h = 0;
    if (n == 0)
        return self->_measured.h;
    for (int32 i = 0; i < n; i++) {
        CxUiWidget* child = _cxuiContainerChild(self, i);
        if (child && _cxuiLayoutTakesSpace(child))
            h = max(h, _cxuiLayoutHeightFor(child, width));
    }
    return h;
}

void CxUiWidget_arrange(_In_ CxUiWidget* self)
{
    for (int32 i = 0, n = _cxuiContainerChildCount(self); i < n; i++) {
        CxUiWidget* child = _cxuiContainerChild(self, i);
        if (child && _cxuiLayoutTakesSpace(child))
            _cxuiLayoutPlace(child, self->_lrect);
    }
}

void CxUiWidget_realizePeer(_In_ CxUiWidget* self, _In_ CxUiPeer* host)
{
}

void CxUiWidget_placePeer(_In_ CxUiWidget* self, CxUiRect px)
{
}

bool CxUiWidget_showsChild(_In_ CxUiWidget* self, _In_ CxUiWidget* child)
{
    return true;
}

void CxUiWidget_restyle(_In_ CxUiWidget* self)
{
}

CxUiResult CxUiWidget_handleEvent(_In_ CxUiWidget* self, _Inout_ CxUiEvent* ev)
{
    return CXUI_Default;
}

bool CxUiWidget_canFocus(_In_ CxUiWidget* self)
{
    return false;
}

// ---- visibility, enabled state, layout parameters ----

void CxUiWidget_setVisible(_In_ CxUiWidget* self, bool visible)
{
    if (!_cxuiLive(self) || visible == !(self->state & CXUI_WS_Hidden))
        return;
    if (self->state & CXUI_WS_IsWindow) {
        if (visible)
            cxuiWindowShow((CxUiWindow*)self, CXUI_SHOW_Normal);
        else
            cxuiWindowHide((CxUiWindow*)self);
        return;
    }
    self->state ^= CXUI_WS_Hidden;
    _cxuiLayoutVisibilityChanged(self);
}

bool CxUiWidget_visible(_In_ CxUiWidget* self)
{
    if (!_cxuiLive(self))
        return false;
    if (self->state & CXUI_WS_IsWindow)
        return cxuiWindowIsVisible((CxUiWindow*)self);
    return !(self->state & CXUI_WS_Hidden);
}

void CxUiWidget_setEnabled(_In_ CxUiWidget* self, bool enabled)
{
    if (!_cxuiLive(self) || enabled == !(self->state & CXUI_WS_Disabled))
        return;
    self->state ^= CXUI_WS_Disabled;
    _cxuiLayoutEnabledChanged(self);
}

bool CxUiWidget_enabled(_In_ CxUiWidget* self)
{
    return _cxuiLive(self) && !(self->state & CXUI_WS_Disabled);
}

bool CxUiWidget_effectiveEnabled(_In_ CxUiWidget* self)
{
    return _cxuiLive(self) && !(self->state & CXUI_WS_Inactive);
}

static void lpChanged(CxUiWidget* self, const CxUiLayoutParams* lp)
{
    uint8 changed = lp->flags ^ self->_lp.flags;
    bool floating = changed & CXUI_LP_Floating;
    self->_lp     = *lp;
    _cxuiLayoutDirty(self);
    if (changed & CXUI_LP_RealizeEager)
        _cxuiLayoutNeedWalk(self);
    // A widget that starts or stops floating changes its container's layout as well.
    if (floating && self->parent)
        _cxuiLayoutDirty(self->parent);
}

void CxUiWidget_setExpand(_In_ CxUiWidget* self, uint32 expand)
{
    if (!_cxuiLive(self))
        return;
    CxUiLayoutParams lp = self->_lp;
    lp.expand           = (uint8)(expand & (CXUI_EXPAND_Both | CXUI_EXPAND_Stop));
    lpChanged(self, &lp);
}

void CxUiWidget_setAlign(_In_ CxUiWidget* self, uint32 h, uint32 v)
{
    if (!_cxuiLive(self))
        return;
    CxUiLayoutParams lp = self->_lp;
    lp.align[0]         = (uint8)min(h, CXUI_ALIGN_Fill);
    lp.align[1]         = (uint8)min(v, CXUI_ALIGN_Fill);
    lpChanged(self, &lp);
}

void CxUiWidget_setNaturalSize(_In_ CxUiWidget* self, CxUiLen width, CxUiLen height)
{
    if (!_cxuiLive(self))
        return;
    CxUiLayoutParams lp = self->_lp;
    lp.size[0]          = width;
    lp.size[1]          = height;
    lpChanged(self, &lp);
}

void CxUiWidget_setMinSize(_In_ CxUiWidget* self, CxUiLen width, CxUiLen height)
{
    if (!_cxuiLive(self))
        return;
    CxUiLayoutParams lp = self->_lp;
    lp.minSize[0]       = width;
    lp.minSize[1]       = height;
    lpChanged(self, &lp);
}

void CxUiWidget_setLayoutWith(_In_ CxUiWidget* self, _In_ const CxUiLayoutParams* lp)
{
    if (_cxuiLive(self))
        lpChanged(self, lp);
}

void CxUiWidget_layout(_In_ CxUiWidget* self, _Out_ CxUiLayoutParams* out)
{
    *out = _cxuiLive(self) ? self->_lp : (CxUiLayoutParams){ 0 };
}

void CxUiWidget_setBounds(_In_ CxUiWidget* self, int32 x, int32 y, int32 width, int32 height)
{
    if (!_cxuiLive(self))
        return;
    self->_floatBounds = (CxUiRect){ x, y, max(width, 0), max(height, 0) };
    if (self->_lp.flags & CXUI_LP_Floating)
        _cxuiLayoutDirty(self);
}

void CxUiWidget_setTip(_In_ CxUiWidget* self, _In_opt_ strref tip)
{
    if (!_cxuiLive(self) || strEq(tip, self->_tip))
        return;
    strDup(&self->_tip, tip);
    if (self->peer && self->peer->kind != CXUI_PEER_Menu && _cxui.backend->peerSetTip)
        _cxui.backend->peerSetTip(self->peer, self->_tip);
}

strref CxUiWidget_tip(_In_ CxUiWidget* self)
{
    return _cxuiLive(self) && !strEmpty(self->_tip) ? (strref)self->_tip : NULL;
}

// ---- palette and font ----

_Use_decl_annotations_
void _cxuiRestyleTree(CxUiWidget* w)
{
    cxuiWidgetRestyle(w);
    for (int32 i = 0, n = _cxuiContainerChildCount(w); i < n; i++) {
        CxUiWidget* child = _cxuiContainerChild(w, i);
        if (child)
            _cxuiRestyleTree(child);
    }
}

static CxUiWidget* topOf(CxUiWidget* w)
{
    while (w->parent)
        w = w->parent;
    return w;
}

void CxUiWidget_setPalette(_In_ CxUiWidget* self, _In_opt_ CxUiPalette* pal)
{
    if (!_cxuiLive(self) || pal == self->_palette)
        return;
    objRelease(&self->_palette);
    if (pal) {
        pal->_frozen   = true;
        self->_palette = objAcquire(pal);
    }
    _cxuiRestyleTree(self);
}

CxUiPalette* CxUiWidget_palette(_In_ CxUiWidget* self)
{
    if (!_cxuiLive(self))
        return _cxuiPaletteBuiltin(false);
    for (CxUiWidget* w = self; w; w = w->parent) {
        if (w->_palette)
            return w->_palette;
    }
    CxUiWidget* top = topOf(self);
    uint32 theme    = (top->state & CXUI_WS_IsWindow) ? cxuiWindowResolvedTheme((CxUiWindow*)top) : cxuiSystemTheme();
    return _cxuiPaletteBuiltin(theme == CXUI_Theme_Dark);
}

void CxUiWidget_setFontWith(_In_ CxUiWidget* self, _In_opt_ const CxUiFontDesc* desc)
{
    if (!_cxuiLive(self))
        return;
    if (desc) {
        strDup(&self->_fontFaces, desc->faces);
        self->_fontSize  = desc->size;
        self->_fontFlags = desc->flags;
    } else {
        strDestroy(&self->_fontFaces);
        self->_fontSize = 0;
        self->_fontFlags = 0;
    }
    self->_hasFont = desc != NULL;
    objRelease(&self->_font);
    _cxuiLayoutDirtyTree(self);
    _cxuiRestyleTree(self);
}

CxUiFont* CxUiWidget_font(_In_ CxUiWidget* self)
{
    // The widget that sets the font keeps it, made for the widget's scale in this session.
    CxUiWidget* owner = self;
    while (!owner->_hasFont && owner->parent)
        owner = owner->parent;
    float32 scale = cxuiWidgetScale(self);
    if (!owner->_font || owner->_fontSession != _cxui.session || cxuiFontScale(owner->_font) != scale) {
        objRelease(&owner->_font);
        CxUiFontDesc desc   = { .faces = owner->_fontFaces, .size = owner->_fontSize, .flags = owner->_fontFlags };
        owner->_font        = cxuiFontCreate(owner->_hasFont ? &desc : NULL, scale);
        owner->_fontSession = _cxui.session;
    }
    return owner->_font;
}

void CxUiWidget_destroy(_In_ CxUiWidget* self)
{
    // The peer must be destroyed on the UI thread, and this can run anywhere. An unlocked look is
    // enough to skip the lock: only the UI thread sets the link, and only while it holds a
    // reference, which by now nobody does.
    if (self->peer) {
        mutexAcquire(&peerLock);
        CxUiPeer* p = self->peer;
        self->peer  = NULL;
        if (p)
            p->w = NULL;
        mutexRelease(&peerLock);

        if (p) {
            if (cxuiIsUIThread())
                peerDestroy(p);
            else
                // If the session ends first, the queue drops this and the end of the session
                // destroys the peer instead.
                cxuiPost(closureCreate(peerTeardownTask, stvar(ptr, p)));
        }
    }

    // Autogen begins -----
    strDestroy(&self->name);
    stvarDestroy(&self->userdata);
    strDestroy(&self->_tip);
    cchainDestroy(&self->destroyNotify);
    saDestroy(&self->handlers);
    objRelease(&self->_palette);
    strDestroy(&self->_fontFaces);
    objRelease(&self->_font);
    // Autogen ends -------
}


// Autogen begins -----
// clang-format off
#include "core/cxuiwidget.auto.inc"
// clang-format on
// Autogen ends -------
