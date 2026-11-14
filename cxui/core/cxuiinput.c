#include "cxuidraw_private.h"
#include "core/cxuicontrols.h"
#include "core/cxuitext.h"

#include <math.h>

// A pause this long between turns of the wheel starts a new gesture.
#define WHEEL_GESTURE timeMS(500)

static CxUiCanvas* peerCanvas(CxUiPeer* p)
{
    CxUiCanvas* cv = p->w ? objDynCast(CxUiCanvas, p->w) : NULL;
    return cv && _cxuiLive(CxUiWidget(cv)) ? cv : NULL;
}

// A pixel position in the canvas's units; floor, so positions left of or above it stay negative.
static CxUiPoint toUnits(CxUiCanvas* cv, CxUiPoint px)
{
    if (cv->_units != CXUI_Units_Dip)
        return px;
    float32 s = cxuiWidgetScale(cv);
    return (CxUiPoint){ (int32)floor(px.x / s), (int32)floor(px.y / s) };
}

static bool inside(CxUiCanvas* cv, CxUiPoint px)
{
    return px.x >= 0 && px.y >= 0 && px.x < cv->_size.w && px.y < cv->_size.h;
}

// False if the handler left the canvas dead.
static bool sendMouse(CxUiCanvas* cv, uint32 action, uint32 button, uint32 mods, uint32 flags, CxUiPoint px)
{
    CxUiPoint at       = toUnits(cv, px);
    CxUiMouseEvent ev  = { .base.kind = CXUI_EV_Mouse,
                           .action    = action,
                           .button    = button,
                           .buttons   = cv->_buttons,
                           .mods      = mods,
                           .flags     = flags,
                           .x         = at.x,
                           .y         = at.y };
    _cxuiDispatch(CxUiWidget(cv), &ev.base);
    return _cxuiLive(CxUiWidget(cv));
}

static void captureEnd(CxUiCanvas* cv)
{
    if (_cxui.capture != cv)
        return;
    _cxui.capture = NULL;
    if (cv->peer && _cxui.backend->canvasCapture)
        _cxui.backend->canvasCapture(cv->peer, false);
}

static bool isDoubleClick(CxUiCanvas* cv, uint32 button, CxUiPoint px, int64 now)
{
    int64 time     = timeMS(500);
    int32 distance = 4;
    if (_cxui.backend->doubleClick)
        _cxui.backend->doubleClick(&time, &distance);
    return button == cv->_lastButton && now - cv->_lastPress < time && abs(px.x - cv->_lastPressAt.x) <= distance &&
           abs(px.y - cv->_lastPressAt.y) <= distance;
}

_Use_decl_annotations_
void _cxuiCanvasPointer(CxUiPeer* p, CxUiPointerKind kind, uint32 button, uint32 buttons, uint32 mods, CxUiPoint px)
{
    CxUiCanvas* cv = peerCanvas(p);
    // While another canvas has the mouse, input meant for this one is stale.
    if (!cv || (_cxui.capture && _cxui.capture != cv))
        return;
    objAcquire(cv);
    bool captured = _cxui.capture == cv;

    switch (kind) {
    case CXUI_PTR_Left:
        // A captured canvas keeps the pointer until release.
        if (!captured && cv->_hover) {
            cv->_hover = false;
            sendMouse(cv, CXUI_MOUSE_Leave, 0, mods, 0, px);
        }
        break;

    case CXUI_PTR_Move:
        if (!captured && !inside(cv, px)) {
            if (cv->_hover) {
                cv->_hover = false;
                sendMouse(cv, CXUI_MOUSE_Leave, 0, mods, 0, px);
            }
            break;
        }
        if (!cv->_hover) {
            cv->_hover = true;
            if (!sendMouse(cv, CXUI_MOUSE_Enter, 0, mods, 0, px))
                break;
        }
        sendMouse(cv, CXUI_MOUSE_Move, 0, mods, 0, px);
        break;

    case CXUI_PTR_Press: {
        if (!captured) {
            _cxui.capture = cv;
            if (_cxui.backend->canvasCapture)
                _cxui.backend->canvasCapture(p, true);
            // Click to focus
            if (_cxui.backend->peerFocus && cxuiWidgetCanFocus(cv) && !(CxUiWidget(cv)->state & CXUI_WS_Inactive)) {
                _cxui.focusInPlace = true;
                _cxui.backend->peerFocus(p);
                _cxui.focusInPlace = false;
            }
            if (!_cxuiLive(CxUiWidget(cv)))
                break;
        }
        if (!cv->_hover) {
            cv->_hover   = true;
            cv->_buttons = buttons & ~button;
            if (!sendMouse(cv, CXUI_MOUSE_Enter, 0, mods, 0, px))
                break;
        }
        cv->_buttons = buttons | button;
        // A third press starts counting again.
        int64 now     = clockTimer();
        bool dbl      = isDoubleClick(cv, button, px, now);
        cv->_lastPress   = dbl ? 0 : now;
        cv->_lastButton  = button;
        cv->_lastPressAt = px;
        sendMouse(cv, dbl ? CXUI_MOUSE_DoubleClick : CXUI_MOUSE_Down, button, mods, 0, px);
        break;
    }

    case CXUI_PTR_Release:
        if (!captured)
            break;
        cv->_buttons = buttons & ~button;
        if (!sendMouse(cv, CXUI_MOUSE_Up, button, mods, 0, px) || cv->_buttons)
            break;
        captureEnd(cv);
        if (!inside(cv, px) && cv->_hover) {
            cv->_hover = false;
            sendMouse(cv, CXUI_MOUSE_Leave, 0, mods, 0, px);
        }
        break;
    }
    objRelease(&cv);
}

static void loseCapture(CxUiCanvas* cv)
{
    objAcquire(cv);
    _cxui.capture = NULL;
    // One Up for each button still held; where the pointer is isn't known, so it leaves too.
    for (uint32 b = 1; b <= CXUI_BTN_X2 && cv->_buttons; b <<= 1) {
        if (!(cv->_buttons & b))
            continue;
        cv->_buttons &= ~b;
        if (!sendMouse(cv, CXUI_MOUSE_Up, b, 0, CXUI_MOUSE_CaptureLost, (CxUiPoint){ -1, -1 }))
            break;
    }
    if (_cxuiLive(CxUiWidget(cv)) && cv->_hover) {
        cv->_hover = false;
        sendMouse(cv, CXUI_MOUSE_Leave, 0, 0, 0, (CxUiPoint){ -1, -1 });
    }
    objRelease(&cv);
}

_Use_decl_annotations_
void _cxuiCanvasCaptureLost(CxUiPeer* p)
{
    CxUiCanvas* cv = peerCanvas(p);
    if (cv && _cxui.capture == cv)
        loseCapture(cv);
}

_Use_decl_annotations_
bool _cxuiCaptureCancel(CxUiWindow* win)
{
    CxUiCanvas* cv = _cxui.capture;
    if (!cv || _cxuiWindowOf(CxUiWidget(cv)) != win)
        return false;
    if (cv->peer && _cxui.backend->canvasCapture)
        _cxui.backend->canvasCapture(cv->peer, false);
    loseCapture(cv);
    return true;
}

_Use_decl_annotations_
CxUiResult _cxuiCanvasWheel(CxUiPeer* p, float64 dx, float64 dy, uint32 mods, CxUiPoint px)
{
    CxUiCanvas* cv = peerCanvas(p);
    if (!cv)
        return CXUI_Default;
    // A gesture stays with what took its first turn: a list that reaches its end doesn't scroll
    // the page with the rest of it, and one the page brings under the pointer doesn't take it over.
    int64 now       = clockTimer();
    CxUiWindow* win = _cxuiWindowOf(CxUiWidget(cv));
    bool going      = now - _cxui.wheelAt < WHEEL_GESTURE && _cxui.wheelWin == win &&
                 (!_cxui.wheelTo || _cxui.wheelTo == cv);
    _cxui.wheelAt = now;
    if (going && !_cxui.wheelTo)
        return CXUI_Default;
    CxUiPoint at      = toUnits(cv, px);
    CxUiWheelEvent ev = { .base.kind = CXUI_EV_Wheel, .dx = dx, .dy = dy, .mods = mods, .x = at.x, .y = at.y };
    CxUiResult r      = _cxuiDispatch(CxUiWidget(cv), &ev.base);
    if (going)
        return CXUI_Handled;
    _cxui.wheelWin = win;
    _cxui.wheelTo  = r == CXUI_Handled ? cv : NULL;
    return r;
}

_Use_decl_annotations_
CxUiResult _cxuiKey(CxUiPeer* p, CxUiKey key, uint32 mods, uint32 scancode, bool down)
{
    if (!p->w || !_cxuiLive(p->w) || (uint32)key >= CXUI_KEY_Count)
        return CXUI_Default;
    uint32* word = &_cxui.keysDown[key / 32];
    uint32 bit   = 1u << (key % 32);
    bool repeat  = down && (*word & bit);
    if (down)
        *word |= bit;
    else
        *word &= ~bit;
    CxUiKeyEvent ev = { .base.kind = CXUI_EV_Key, .key = key, .mods = mods, .scancode = scancode, .down = down,
                        .repeat = repeat };
    CxUiWidget* w   = objAcquire(p->w);
    CxUiResult r    = _cxuiDispatch(w, &ev.base);
    // Focus moves only when nothing along the way took the key.
    if (r == CXUI_Default && down && key == CXUI_KEY_Tab && !(mods & ~CXUI_MOD_Shift) && _cxuiLive(w)) {
        if (_cxuiFocusStep(w, (mods & CXUI_MOD_Shift) != 0))
            r = CXUI_Handled;
    }
    // A text area's Enter is a new line, not the window's default.
    bool line       = (key == CXUI_KEY_Enter || key == CXUI_KEY_PadEnter) && _cxuiTakesEnter(w);
    CxUiWindow* win = r == CXUI_Default && down && !line && _cxuiLive(w) ? _cxuiWindowOf(w) : NULL;
    if (win && _cxuiWindowKey(win, key, mods, repeat))
        r = CXUI_Handled;
    objRelease(&w);
    return r;
}

_Use_decl_annotations_
void _cxuiFocusChanged(CxUiPeer* p, bool gained)
{
    CxUiWidget* w = p->w;
    if (!w || !_cxuiLive(w))
        return;
    CxUiWindow* win = _cxuiWindowOf(w);
    // Scrolling what was clicked would move it from under the pointer.
    if (gained && !_cxui.focusInPlace)
        _cxuiScrollIntoView(w);
    if (win) {
        if (gained)
            win->_focus = w;
        else if (win->_focus == w)
            win->_focus = NULL;
    }
    // Keys released while focus was elsewhere are never reported.
    memset(_cxui.keysDown, 0, sizeof(_cxui.keysDown));
    CxUiFocusEvent ev = { .base.kind = CXUI_EV_Focus, .gained = gained };
    _cxuiDispatch(w, &ev.base);
}

_Use_decl_annotations_
void _cxuiInputForget(CxUiWidget* w)
{
    CxUiWindow* win = _cxuiWindowOf(w);
    if (win && win->_focus == w)
        win->_focus = NULL;
    CxUiCanvas* cv = objDynCast(CxUiCanvas, w);
    // Another widget made at the same address isn't part of the gesture.
    if (_cxui.wheelWin == (CxUiWindow*)w || (cv && _cxui.wheelTo == cv)) {
        _cxui.wheelWin = NULL;
        _cxui.wheelTo  = NULL;
        _cxui.wheelAt  = 0;
    }
    if (cv) {
        captureEnd(cv);
        cv->_buttons = 0;
        cv->_hover   = false;
    }
}

static bool focusable(CxUiWidget* w)
{
    return w->peer && !w->peer->hidden && !(w->state & ((uint32)CXUI_WS_Inactive | CXUI_WS_Dead)) && cxuiWidgetCanFocus(w);
}

// The widgets that can take focus, in the order Tab visits them: as they appear in the tree. A
// widget off screen (on a page not shown, say) has its peer hidden.
static void focusChain(CxUiWidget* w, sa_CxUiWidget* out)
{
    if (w->state & CXUI_WS_Hidden)
        return;
    if (focusable(w))
        saPush(out, object, w);
    for (int32 i = 0, n = _cxuiContainerChildCount(w); i < n; i++) {
        CxUiWidget* child = _cxuiContainerChild(w, i);
        if (child)
            focusChain(child, out);
    }
}

_Use_decl_annotations_
void _cxuiFocus(CxUiWidget* w)
{
    if (!_cxuiLive(w) || !_cxui.backend->peerFocus)
        return;
    CxUiWidget* target = objAcquire(w);
    if (!focusable(w)) {
        // A container passes focus to the first widget in it that takes it.
        sa_CxUiWidget chain;
        saInit(&chain, object, 4);
        focusChain(w, &chain);
        objRelease(&target);
        if (saSize(chain) > 0)
            target = objAcquire(chain.a[0]);
        saDestroy(&chain);
    }
    if (target) {
        _cxuiNativeEnter();
        _cxui.backend->peerFocus(target->peer);
        _cxuiNativeLeave();
    }
    objRelease(&target);
}

_Use_decl_annotations_
void _cxuiFocusByKey(CxUiWidget* w)
{
    CxUiWindow* win = _cxuiLive(w) ? _cxuiWindowOf(w) : NULL;
    _cxuiFocus(w);
    if (win && CxUiWidget(win)->peer && _cxui.backend->windowFocusCues) {
        _cxuiNativeEnter();
        _cxui.backend->windowFocusCues(CxUiWidget(win)->peer);
        _cxuiNativeLeave();
    }
}

_Use_decl_annotations_
bool _cxuiFocusStep(CxUiWidget* from, bool backward)
{
    CxUiWindow* win = _cxuiWindowOf(from);
    if (!win || !_cxui.backend->peerFocus)
        return false;
    sa_CxUiWidget chain;
    saInit(&chain, object, 16);
    focusChain(CxUiWidget(win), &chain);
    int32 n    = saSize(chain);
    bool moved = false;
    if (n > 0) {
        int32 at = saFind(chain, object, from);
        // From the window itself, or a widget that can't take focus, Tab starts at either end.
        int32 next = at < 0 ? (backward ? n - 1 : 0) : (at + (backward ? n - 1 : 1)) % n;
        if (chain.a[next] != from) {
            _cxuiFocusByKey(chain.a[next]);
            moved = true;
        }
    }
    saDestroy(&chain);
    return moved;
}

// The first widget in tree order with the mnemonic: a button, or a label, which hands it to the
// first widget after it that can take focus.
typedef struct MnemonicFind {
    CxUiKey key;
    bool afterLabel;
    bool press;
    CxUiWidget* found;
} MnemonicFind;

static bool shownPeer(CxUiWidget* w)
{
    return w->peer && !w->peer->hidden && !(w->state & ((uint32)CXUI_WS_Inactive | CXUI_WS_Dead));
}

// The mnemonic of something it presses: a button, check box or radio button
static uint32 pressMnemonic(CxUiWidget* w)
{
    CxUiButton* b = objDynCast(CxUiButton, w);
    CxUiCheck* c  = objDynCast(CxUiCheck, w);
    CxUiRadio* r  = objDynCast(CxUiRadio, w);
    return b ? b->_mnemonic : c ? c->_mnemonic : r ? r->_mnemonic : CXUI_KEY_None;
}

static bool mnemonicFind(CxUiWidget* w, MnemonicFind* m)
{
    if (w->state & CXUI_WS_Hidden)
        return false;
    if (m->afterLabel) {
        if (focusable(w)) {
            m->found = w;
            return true;
        }
    } else {
        CxUiLabel* l = objDynCast(CxUiLabel, w);
        CxUiTabs* t  = objDynCast(CxUiTabs, w);
        if ((pressMnemonic(w) == (uint32)m->key || (t && _cxuiTabsHasMnemonic(t, m->key))) && shownPeer(w)) {
            m->found = w;
            m->press = true;
            return true;
        }
        if (l && l->_mnemonic == (uint32)m->key && w->peer && !w->peer->hidden)
            m->afterLabel = true;
    }
    for (int32 i = 0, n = _cxuiContainerChildCount(w); i < n; i++) {
        CxUiWidget* child = _cxuiContainerChild(w, i);
        if (child && mnemonicFind(child, m))
            return true;
    }
    return false;
}

_Use_decl_annotations_
bool _cxuiMnemonicRun(CxUiWindow* win, CxUiKey key)
{
    MnemonicFind m = { .key = key };
    if (key == CXUI_KEY_None || !mnemonicFind(CxUiWidget(win), &m))
        return false;
    CxUiTabs* tabs = objDynCast(CxUiTabs, m.found);
    if (tabs)
        return _cxuiTabsMnemonic(tabs, key);
    // A check box or radio button also takes focus, as it would from a click.
    if (m.press) {
        CxUiWidget* found = objAcquire(m.found);
        bool pressed      = _cxuiPress(found);
        if (pressed && !objDynCast(CxUiButton, found))
            _cxuiFocusByKey(found);
        objRelease(&found);
        return pressed;
    }
    _cxuiFocusByKey(m.found);
    return true;
}

_Use_decl_annotations_
CxUiWidget* cxuiFocused(CxUiWindow* win)
{
    return _cxuiLive(CxUiWidget(win)) ? win->_focus : NULL;
}

_Use_decl_annotations_
bool _cxuiIsTextInput(CxUiWidget* w)
{
    return _cxuiLive(w) && objDynCast(CxUiTextField, w);
}
