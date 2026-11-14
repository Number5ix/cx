// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "core/cxuicontrols.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cxui_private.h"

#include <math.h>

#define DEFAULT_WRAP_COLUMNS 40

static bool live(void* w)
{
    return _cxuiLive((CxUiWidget*)w);
}

static CxUiPeer* peerOf(void* w)
{
    return ((CxUiWidget*)w)->peer;
}

static bool inactive(void* w)
{
    return (((CxUiWidget*)w)->state & CXUI_WS_Inactive) != 0;
}

// Buttons, check boxes and radio buttons don't take focus on macOS without Full Keyboard Access.
static bool buttonsFocus(void)
{
    return !_cxui.backend || !_cxui.backend->buttonsFocus || _cxui.backend->buttonsFocus();
}

// The font goes to the peer when it differs from the one it has; held, as a platform font may
// belong to it.
static void setPeerFont(CxUiFont** peerFont, CxUiPeer* p, CxUiFont* f)
{
    if (!p || f == *peerFont)
        return;
    objRelease(peerFont);
    *peerFont = objAcquire(f);
    if (_cxui.backend->controlSetFont)
        _cxui.backend->controlSetFont(p, f);
}

// A font's measure of text as shown, in DIPs
static CxUiSize fontMeasure(CxUiFont* f, strref text)
{
    string shown = 0;
    _cxuiMnemonicStrip(&shown, text);
    CxUiSize px = cxuiFontMeasure(f, shown);
    strDestroy(&shown);
    float32 s = cxuiFontScale(f);
    return (CxUiSize){ (int32)ceilf(px.w / s), (int32)ceilf(px.h / s) };
}

// Lines of text broken at its spaces to fit widthPx, as the platforms break them, and a word
// wider than that split across lines
static int32 wrapLines(CxUiFont* f, strref text, int32 widthPx)
{
    widthPx      = max(widthPx, 1);
    int32 space  = cxuiFontAdvance(f, _S" ");
    int32 lines  = 0;
    sa_string paras, words;
    saInit(&paras, string, 4);
    saInit(&words, string, 16);
    strSplit(&paras, text, _S"\n", false);
    for (int32 p = 0; p < max(saSize(paras), 1); p++) {
        lines++;
        int32 lineW = 0;
        saClear(&words);
        if (p < saSize(paras))
            strSplit(&words, paras.a[p], _S" ", false);
        for (int32 i = 0; i < saSize(words); i++) {
            int32 ww = cxuiFontAdvance(f, words.a[i]);
            if (lineW > 0 && lineW + space + ww <= widthPx) {
                lineW += space + ww;
                continue;
            }
            if (lineW > 0)
                lines++;
            int32 extra = ww > widthPx ? (ww - 1) / widthPx : 0;
            lines += extra;
            lineW = ww - extra * widthPx;
        }
    }
    saDestroy(&words);
    saDestroy(&paras);
    return lines;
}

// ---- CxUiLabel ----

_objfactory_guaranteed CxUiLabel* CxUiLabel_createWith(_In_opt_ const CxUiLabelDesc* desc)
{
    CxUiLabel* self;
    self = objInstCreate(CxUiLabel);
    if (desc) {
        strDup(&self->_text, desc->text);
        self->_align          = desc->align & (CXUI_TEXT_Center | CXUI_TEXT_Right);
        self->_ellipsis       = desc->ellipsis <= CXUI_ELLIPSIS_Path ? desc->ellipsis : CXUI_ELLIPSIS_None;
        self->_wrap           = desc->wrap;
        self->_columns        = desc->columns > 0 ? desc->columns : DEFAULT_WRAP_COLUMNS;
        self->_color          = desc->color;
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
        // A wrapping label takes the width it is given, so it breaks its lines there.
        if (self->_wrap) {
            self->_ellipsis = CXUI_ELLIPSIS_None;
            CxUiWidget(self)->_lp.expand |= CXUI_EXPAND_H;
        }
    }
    self->_mnemonic = _cxuiMnemonicKey(self->_text);

    objInstInit(self);
    return self;
}

strref CxUiLabel_text(_In_ CxUiLabel* self)
{
    return live(self) ? (strref)self->_text : NULL;
}

void CxUiLabel_setText(_In_ CxUiLabel* self, _In_ strref text)
{
    if (!live(self) || strEq(text, self->_text))
        return;
    strDup(&self->_text, text);
    self->_mnemonic = _cxuiMnemonicKey(self->_text);
    if (peerOf(self))
        _cxui.backend->labelSetText(peerOf(self), self->_text);
    _cxuiLayoutDirty(CxUiWidget(self));
}

void CxUiLabel_setColor(_In_ CxUiLabel* self, CxUiColor color)
{
    if (!live(self) || color.bits == self->_color.bits)
        return;
    self->_color = color;
    cxuiWidgetRestyle(self);
}

extern uint32 CxUiWidget_eventMask(_In_ CxUiWidget* self);   // parent
#define parent_eventMask() CxUiWidget_eventMask((CxUiWidget*)(self))
uint32 CxUiLabel_eventMask(_In_ CxUiLabel* self)
{
    return parent_eventMask();
}

int32 CxUiLabel_heightFor(_In_ CxUiLabel* self, int32 width);

extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiLabel_measure(_In_ CxUiLabel* self)
{
    CxUiFont* f = cxuiWidgetFont(self);
    setPeerFont(&self->_peerFont, peerOf(self), f);
    // A wrapping label is as wide as its text up to its columns, and as tall as the lines that
    // width takes; given another width, it asks again.
    if (self->_wrap) {
        CxUiFontMetrics fm;
        cxuiFontMetrics(f, &fm);
        CxUiSize line = fontMeasure(f, self->_text);
        int32 cap     = (int32)ceilf(self->_columns * fm.avgCharW / cxuiFontScale(f));
        int32 w       = max(min(line.w, cap), 1);
        CxUiWidget(self)->_hfw = true;
        return (CxUiSize){ w, CxUiLabel_heightFor(self, w) };
    }
    if (_cxui.backend->labelMeasure) {
        CxUiSize n = _cxui.backend->labelMeasure(self, f, self->_text);
        if (n.w > 0 || n.h > 0)
            return n;
    }
    return fontMeasure(f, self->_text);
}

extern int32 CxUiWidget_heightFor(_In_ CxUiWidget* self, int32 width);   // parent
#define parent_heightFor(width) CxUiWidget_heightFor((CxUiWidget*)(self), width)
int32 CxUiLabel_heightFor(_In_ CxUiLabel* self, int32 width)
{
    if (!self->_wrap)
        return CxUiWidget(self)->_measured.h;
    CxUiFont* f = cxuiWidgetFont(self);
    if (_cxui.backend->labelHeightFor) {
        int32 h = _cxui.backend->labelHeightFor(self, f, self->_text, width);
        if (h > 0)
            return h;
    }
    string shown = 0;
    _cxuiMnemonicStrip(&shown, self->_text);
    CxUiFontMetrics fm;
    cxuiFontMetrics(f, &fm);
    float32 s   = cxuiFontScale(f);
    int32 lines = wrapLines(f, shown, (int32)floorf(width * s));
    strDestroy(&shown);
    return (int32)ceilf(lines * fm.height / s);
}

extern void CxUiWidget_realizePeer(_In_ CxUiWidget* self, _In_ CxUiPeer* host);   // parent
#define parent_realizePeer(host) CxUiWidget_realizePeer((CxUiWidget*)(self), host)
void CxUiLabel_realizePeer(_In_ CxUiLabel* self, _In_ CxUiPeer* host)
{
    if (peerOf(self) || !_cxui.backend->labelCreate)
        return;
    CxUiPeer* p = _cxui.backend->labelCreate(self, host);
    if (!p)
        return;
    _cxuiPeerAttach(CxUiWidget(self), p);
    objRelease(&self->_peerFont);
    setPeerFont(&self->_peerFont, p, cxuiWidgetFont(self));
    _cxui.backend->labelSetText(p, self->_text);
    _cxui.backend->labelSetStyle(p, self->_align, self->_ellipsis, self->_wrap);
    // The platform's own measure can differ from the estimate the label was laid out with.
    if (_cxui.backend->labelMeasure) {
        CxUiSize n = CxUiLabel_measure(self);
        CxUiSize m = CxUiWidget(self)->_measured;
        if (n.w != m.w || n.h != m.h)
            _cxuiLayoutDirty(CxUiWidget(self));
    }
}

extern void CxUiWidget_placePeer(_In_ CxUiWidget* self, CxUiRect px);   // parent
#define parent_placePeer(px) CxUiWidget_placePeer((CxUiWidget*)(self), px)
void CxUiLabel_placePeer(_In_ CxUiLabel* self, CxUiRect px)
{
    if (peerOf(self))
        _cxui.backend->controlSetBounds(peerOf(self), px);
}

// A label is never disabled natively, which would bring the platform's colors.
extern void CxUiWidget_restyle(_In_ CxUiWidget* self);   // parent
#define parent_restyle() CxUiWidget_restyle((CxUiWidget*)(self))
void CxUiLabel_restyle(_In_ CxUiLabel* self)
{
    CxUiPeer* p = peerOf(self);
    if (!p)
        return;
    CxUiColor c = self->_color;
    if (inactive(self) || c.a == 0)
        c = cxuiPaletteColor(cxuiWidgetPalette(self), inactive(self) ? CXUI_PAL_TextDisabled : CXUI_PAL_Text);
    _cxui.backend->controlSetColor(p, c);
}

void CxUiLabel_destroy(_In_ CxUiLabel* self)
{
    // Autogen begins -----
    strDestroy(&self->_text);
    objRelease(&self->_peerFont);
    // Autogen ends -------
}

// ---- CxUiButton ----

_objfactory_guaranteed CxUiButton* CxUiButton_createWith(_In_opt_ const CxUiButtonDesc* desc)
{
    CxUiButton* self;
    self = objInstCreate(CxUiButton);
    if (desc) {
        strDup(&self->_title, desc->title);
        self->_padding        = (CxUiSize){ max(desc->padding.w, 0), max(desc->padding.h, 0) };
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }
    self->_mnemonic = _cxuiMnemonicKey(self->_title);

    objInstInit(self);
    return self;
}

strref CxUiButton_title(_In_ CxUiButton* self)
{
    return live(self) ? (strref)self->_title : NULL;
}

void CxUiButton_setTitle(_In_ CxUiButton* self, _In_ strref title)
{
    if (!live(self) || strEq(title, self->_title))
        return;
    strDup(&self->_title, title);
    self->_mnemonic = _cxuiMnemonicKey(self->_title);
    if (peerOf(self))
        _cxui.backend->buttonSetTitle(peerOf(self), self->_title);
    _cxuiLayoutDirty(CxUiWidget(self));
}

void CxUiButton_press(_In_ CxUiButton* self)
{
    _cxuiPress(CxUiWidget(self));
}

extern uint32 CxUiWidget_eventMask(_In_ CxUiWidget* self);   // parent
#undef parent_eventMask
#define parent_eventMask() CxUiWidget_eventMask((CxUiWidget*)(self))
uint32 CxUiButton_eventMask(_In_ CxUiButton* self)
{
    return parent_eventMask() | (1 << CXUI_EV_Activate) | (1 << CXUI_EV_Key) | (1 << CXUI_EV_Focus);
}

// Room for the title and the platform's frame, and the padding asked for
extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#undef parent_measure
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiButton_measure(_In_ CxUiButton* self)
{
    CxUiFont* f = cxuiWidgetFont(self);
    setPeerFont(&self->_peerFont, peerOf(self), f);
    CxUiSize n = { 0 };
    if (_cxui.backend->buttonMeasure)
        n = _cxui.backend->buttonMeasure(self, f, self->_title);
    if (n.w <= 0 && n.h <= 0) {
        n = fontMeasure(f, self->_title);
        n.w += 16;
        n.h += 8;
    }
    return (CxUiSize){ n.w + self->_padding.w * 2, n.h + self->_padding.h * 2 };
}

extern void CxUiWidget_realizePeer(_In_ CxUiWidget* self, _In_ CxUiPeer* host);   // parent
#undef parent_realizePeer
#define parent_realizePeer(host) CxUiWidget_realizePeer((CxUiWidget*)(self), host)
void CxUiButton_realizePeer(_In_ CxUiButton* self, _In_ CxUiPeer* host)
{
    if (peerOf(self) || !_cxui.backend->buttonCreate)
        return;
    CxUiPeer* p = _cxui.backend->buttonCreate(self, host);
    if (!p)
        return;
    _cxuiPeerAttach(CxUiWidget(self), p);
    objRelease(&self->_peerFont);
    setPeerFont(&self->_peerFont, p, cxuiWidgetFont(self));
    _cxui.backend->buttonSetTitle(p, self->_title);
    if (self->_isDefault)
        _cxui.backend->buttonSetDefault(p, true);
    // The platform's own measure can differ from the estimate the button was laid out with.
    if (_cxui.backend->buttonMeasure) {
        CxUiSize n = CxUiButton_measure(self);
        CxUiSize m = CxUiWidget(self)->_measured;
        if (n.w != m.w || n.h != m.h)
            _cxuiLayoutDirty(CxUiWidget(self));
    }
}

extern void CxUiWidget_placePeer(_In_ CxUiWidget* self, CxUiRect px);   // parent
#undef parent_placePeer
#define parent_placePeer(px) CxUiWidget_placePeer((CxUiWidget*)(self), px)
void CxUiButton_placePeer(_In_ CxUiButton* self, CxUiRect px)
{
    if (peerOf(self))
        _cxui.backend->controlSetBounds(peerOf(self), px);
}

extern void CxUiWidget_restyle(_In_ CxUiWidget* self);   // parent
#undef parent_restyle
#define parent_restyle() CxUiWidget_restyle((CxUiWidget*)(self))
void CxUiButton_restyle(_In_ CxUiButton* self)
{
}

extern bool CxUiWidget_canFocus(_In_ CxUiWidget* self);   // parent
#define parent_canFocus() CxUiWidget_canFocus((CxUiWidget*)(self))
bool CxUiButton_canFocus(_In_ CxUiButton* self)
{
    return buttonsFocus();
}

// Enter presses a button that has focus, rather than the window's default.
extern CxUiResult CxUiWidget_handleEvent(_In_ CxUiWidget* self, _Inout_ CxUiEvent* ev);   // parent
#define parent_handleEvent(ev) CxUiWidget_handleEvent((CxUiWidget*)(self), ev)
CxUiResult CxUiButton_handleEvent(_In_ CxUiButton* self, _Inout_ CxUiEvent* ev)
{
    if (ev->kind != CXUI_EV_Key || ev->target != CxUiWidget(self))
        return CXUI_Default;
    CxUiKeyEvent* k = (CxUiKeyEvent*)ev;
    if (k->down && !k->repeat && (k->key == CXUI_KEY_Enter || k->key == CXUI_KEY_PadEnter) && k->mods == 0 &&
        _cxuiPress(CxUiWidget(self)))
        return CXUI_Handled;
    return CXUI_Default;
}

void CxUiButton_destroy(_In_ CxUiButton* self)
{
    // Autogen begins -----
    strDestroy(&self->_title);
    objRelease(&self->_peerFont);
    // Autogen ends -------
}

// ---- check boxes and radio buttons ----

// The natural size of a check box or radio button: the platform's, or the title beside a box as
// tall as a line
static CxUiSize checkMeasure(CxUiWidget* w, CxUiFont** peerFont, strref title, bool radio)
{
    CxUiFont* f = cxuiWidgetFont(w);
    setPeerFont(peerFont, w->peer, f);
    CxUiSize n = { 0 };
    if (_cxui.backend->checkMeasure)
        n = _cxui.backend->checkMeasure(w, f, title, radio);
    if (n.w <= 0 && n.h <= 0) {
        n = fontMeasure(f, title);
        n.w += n.h + n.h / 2;
    }
    return n;
}

static void checkRealize(CxUiWidget* w, CxUiPeer* host, CxUiFont** peerFont, strref title, bool radio, uint32 state)
{
    if (w->peer || !_cxui.backend->checkCreate)
        return;
    CxUiPeer* p = _cxui.backend->checkCreate(w, host, radio);
    if (!p)
        return;
    _cxuiPeerAttach(w, p);
    objRelease(peerFont);
    setPeerFont(peerFont, p, cxuiWidgetFont(w));
    _cxui.backend->buttonSetTitle(p, title);
    _cxui.backend->checkSetState(p, state);
    if (_cxui.backend->checkMeasure) {
        CxUiSize n = checkMeasure(w, peerFont, title, radio);
        if (n.w != w->_measured.w || n.h != w->_measured.h)
            _cxuiLayoutDirty(w);
    }
}

// The title in the palette's text color, or its disabled one
static void checkRestyle(CxUiWidget* w)
{
    if (w->peer && _cxui.backend->controlSetColor)
        _cxui.backend->controlSetColor(w->peer, cxuiPaletteColor(cxuiWidgetPalette(w), inactive(w) ? CXUI_PAL_TextDisabled
                                                                                                  : CXUI_PAL_Text));
}

static void checkPush(CxUiCheck* c)
{
    if (peerOf(c))
        _cxui.backend->checkSetState(peerOf(c), c->_state);
}

_objfactory_guaranteed CxUiCheck* CxUiCheck_createWith(_In_opt_ const CxUiCheckDesc* desc)
{
    CxUiCheck* self;
    self = objInstCreate(CxUiCheck);
    if (desc) {
        strDup(&self->_title, desc->title);
        self->_state          = desc->state <= CXUI_CHECK_Mixed ? desc->state : CXUI_CHECK_Off;
        self->_threeState     = desc->threeState;
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }
    self->_mnemonic = _cxuiMnemonicKey(self->_title);

    objInstInit(self);
    return self;
}

strref CxUiCheck_title(_In_ CxUiCheck* self)
{
    return live(self) ? (strref)self->_title : NULL;
}

void CxUiCheck_setTitle(_In_ CxUiCheck* self, _In_ strref title)
{
    if (!live(self) || strEq(title, self->_title))
        return;
    strDup(&self->_title, title);
    self->_mnemonic = _cxuiMnemonicKey(self->_title);
    if (peerOf(self))
        _cxui.backend->buttonSetTitle(peerOf(self), self->_title);
    _cxuiLayoutDirty(CxUiWidget(self));
}

bool CxUiCheck_value(_In_ CxUiCheck* self)
{
    return live(self) && self->_state == CXUI_CHECK_On;
}

void CxUiCheck_setValue(_In_ CxUiCheck* self, bool value)
{
    CxUiCheck_setState(self, value ? CXUI_CHECK_On : CXUI_CHECK_Off);
}

uint32 CxUiCheck_state(_In_ CxUiCheck* self)
{
    return live(self) ? self->_state : CXUI_CHECK_Off;
}

void CxUiCheck_setState(_In_ CxUiCheck* self, uint32 state)
{
    if (!live(self) || state > CXUI_CHECK_Mixed || state == self->_state)
        return;
    self->_state = state;
    checkPush(self);
}

void CxUiCheck_press(_In_ CxUiCheck* self)
{
    _cxuiPress(CxUiWidget(self));
}

extern uint32 CxUiWidget_eventMask(_In_ CxUiWidget* self);   // parent
#undef parent_eventMask
#define parent_eventMask() CxUiWidget_eventMask((CxUiWidget*)(self))
uint32 CxUiCheck_eventMask(_In_ CxUiCheck* self)
{
    return parent_eventMask() | (1 << CXUI_EV_Change) | (1 << CXUI_EV_Key) | (1 << CXUI_EV_Focus);
}

extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#undef parent_measure
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiCheck_measure(_In_ CxUiCheck* self)
{
    return checkMeasure(CxUiWidget(self), &self->_peerFont, self->_title, false);
}

extern void CxUiWidget_realizePeer(_In_ CxUiWidget* self, _In_ CxUiPeer* host);   // parent
#undef parent_realizePeer
#define parent_realizePeer(host) CxUiWidget_realizePeer((CxUiWidget*)(self), host)
void CxUiCheck_realizePeer(_In_ CxUiCheck* self, _In_ CxUiPeer* host)
{
    checkRealize(CxUiWidget(self), host, &self->_peerFont, self->_title, false, self->_state);
}

extern void CxUiWidget_placePeer(_In_ CxUiWidget* self, CxUiRect px);   // parent
#undef parent_placePeer
#define parent_placePeer(px) CxUiWidget_placePeer((CxUiWidget*)(self), px)
void CxUiCheck_placePeer(_In_ CxUiCheck* self, CxUiRect px)
{
    if (peerOf(self))
        _cxui.backend->controlSetBounds(peerOf(self), px);
}

extern void CxUiWidget_restyle(_In_ CxUiWidget* self);   // parent
#undef parent_restyle
#define parent_restyle() CxUiWidget_restyle((CxUiWidget*)(self))
void CxUiCheck_restyle(_In_ CxUiCheck* self)
{
    checkRestyle(CxUiWidget(self));
}

extern bool CxUiWidget_canFocus(_In_ CxUiWidget* self);   // parent
#undef parent_canFocus
#define parent_canFocus() CxUiWidget_canFocus((CxUiWidget*)(self))
bool CxUiCheck_canFocus(_In_ CxUiCheck* self)
{
    return buttonsFocus();
}

void CxUiCheck_destroy(_In_ CxUiCheck* self)
{
    // Autogen begins -----
    strDestroy(&self->_title);
    objRelease(&self->_peerFont);
    // Autogen ends -------
}

// A click: off to on, on to mixed (three-state) or off, mixed to off (three-state) or on
static void checkClicked(CxUiCheck* c)
{
    static const uint8 next[2][3] = {
        { CXUI_CHECK_On, CXUI_CHECK_Off, CXUI_CHECK_On },
        { CXUI_CHECK_On, CXUI_CHECK_Mixed, CXUI_CHECK_Off },
    };
    c->_state = next[c->_threeState][c->_state];
    checkPush(c);
    CxUiChangeEvent ev = { .base.kind = CXUI_EV_Change };
    _cxuiDispatch(CxUiWidget(c), &ev.base);
}

// ---- radio buttons ----

// The nearest group around w
static CxUiRadioGroup* groupOf(CxUiWidget* w)
{
    for (CxUiWidget* x = w->parent; x; x = x->parent) {
        CxUiRadioGroup* g = objDynCast(CxUiRadioGroup, x);
        if (g)
            return g;
    }
    return NULL;
}

// The radios in w in tree order, borrowed, leaving out those of groups inside it
static void radiosIn(CxUiWidget* w, sa_ptr* out)
{
    for (int32 i = 0, n = _cxuiContainerChildCount(w); i < n; i++) {
        CxUiWidget* child = _cxuiContainerChild(w, i);
        if (!child || objDynCast(CxUiRadioGroup, child))
            continue;
        if (objDynCast(CxUiRadio, child))
            saPush(out, ptr, child);
        else
            radiosIn(child, out);
    }
}

static void radioPush(CxUiRadio* r)
{
    if (peerOf(r))
        _cxui.backend->checkSetState(peerOf(r), r->_selected ? CXUI_CHECK_On : CXUI_CHECK_Off);
}

// Selects r, or none, in g, or r on its own when it has no group; true if anything changed
static bool radioSelect(CxUiRadioGroup* g, CxUiRadio* r)
{
    if (!g) {
        if (!r || r->_selected)
            return false;
        r->_selected = true;
        radioPush(r);
        return true;
    }
    bool changed = false;
    sa_ptr all;
    saInit(&all, ptr, 8);
    radiosIn(CxUiWidget(g), &all);
    for (int32 i = 0; i < saSize(all); i++) {
        CxUiRadio* x = all.a[i];
        bool on      = x == r;
        if (x->_selected != on) {
            x->_selected = on;
            radioPush(x);
            changed = true;
        }
    }
    saDestroy(&all);
    return changed;
}

// The user chose r: a change on its group, if that changed anything
static void radioChosen(CxUiRadio* r)
{
    CxUiRadioGroup* g = groupOf(CxUiWidget(r));
    if (!radioSelect(g, r) || !g)
        return;
    CxUiChangeEvent ev = { .base.kind = CXUI_EV_Change };
    _cxuiDispatch(CxUiWidget(g), &ev.base);
}

// Takes input: realized, shown on its page and enabled
static bool available(CxUiWidget* w)
{
    return w->peer && !w->peer->hidden && !(w->state & ((uint32)CXUI_WS_Inactive | CXUI_WS_Dead));
}

_objfactory_guaranteed CxUiRadio* CxUiRadio_createWith(_In_opt_ const CxUiRadioDesc* desc)
{
    CxUiRadio* self;
    self = objInstCreate(CxUiRadio);
    if (desc) {
        strDup(&self->_title, desc->title);
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }
    self->_mnemonic = _cxuiMnemonicKey(self->_title);

    objInstInit(self);
    return self;
}

strref CxUiRadio_title(_In_ CxUiRadio* self)
{
    return live(self) ? (strref)self->_title : NULL;
}

void CxUiRadio_setTitle(_In_ CxUiRadio* self, _In_ strref title)
{
    if (!live(self) || strEq(title, self->_title))
        return;
    strDup(&self->_title, title);
    self->_mnemonic = _cxuiMnemonicKey(self->_title);
    if (peerOf(self))
        _cxui.backend->buttonSetTitle(peerOf(self), self->_title);
    _cxuiLayoutDirty(CxUiWidget(self));
}

bool CxUiRadio_selected(_In_ CxUiRadio* self)
{
    return live(self) && self->_selected;
}

_Ret_opt_valid_ CxUiRadioGroup* CxUiRadio_group(_In_ CxUiRadio* self)
{
    return live(self) ? groupOf(CxUiWidget(self)) : NULL;
}

extern uint32 CxUiWidget_eventMask(_In_ CxUiWidget* self);   // parent
#undef parent_eventMask
#define parent_eventMask() CxUiWidget_eventMask((CxUiWidget*)(self))
uint32 CxUiRadio_eventMask(_In_ CxUiRadio* self)
{
    return parent_eventMask() | (1 << CXUI_EV_Key) | (1 << CXUI_EV_Focus);
}

extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#undef parent_measure
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiRadio_measure(_In_ CxUiRadio* self)
{
    return checkMeasure(CxUiWidget(self), &self->_peerFont, self->_title, true);
}

extern void CxUiWidget_realizePeer(_In_ CxUiWidget* self, _In_ CxUiPeer* host);   // parent
#undef parent_realizePeer
#define parent_realizePeer(host) CxUiWidget_realizePeer((CxUiWidget*)(self), host)
void CxUiRadio_realizePeer(_In_ CxUiRadio* self, _In_ CxUiPeer* host)
{
    checkRealize(CxUiWidget(self), host, &self->_peerFont, self->_title, true,
                 self->_selected ? CXUI_CHECK_On : CXUI_CHECK_Off);
}

extern void CxUiWidget_placePeer(_In_ CxUiWidget* self, CxUiRect px);   // parent
#undef parent_placePeer
#define parent_placePeer(px) CxUiWidget_placePeer((CxUiWidget*)(self), px)
void CxUiRadio_placePeer(_In_ CxUiRadio* self, CxUiRect px)
{
    if (peerOf(self))
        _cxui.backend->controlSetBounds(peerOf(self), px);
}

extern void CxUiWidget_restyle(_In_ CxUiWidget* self);   // parent
#undef parent_restyle
#define parent_restyle() CxUiWidget_restyle((CxUiWidget*)(self))
void CxUiRadio_restyle(_In_ CxUiRadio* self)
{
    checkRestyle(CxUiWidget(self));
}

// Tab stops at a group's selected radio, or at each of them while none that takes input is.
extern bool CxUiWidget_canFocus(_In_ CxUiWidget* self);   // parent
#undef parent_canFocus
#define parent_canFocus() CxUiWidget_canFocus((CxUiWidget*)(self))
bool CxUiRadio_canFocus(_In_ CxUiRadio* self)
{
    if (!buttonsFocus())
        return false;
    CxUiRadioGroup* g = groupOf(CxUiWidget(self));
    if (!g || self->_selected)
        return true;
    CxUiRadio* sel = CxUiRadioGroup_value(g);
    return !sel || !available(CxUiWidget(sel));
}

// The arrow keys choose the next or previous radio in the group that takes input, and move focus
// to it.
extern CxUiResult CxUiWidget_handleEvent(_In_ CxUiWidget* self, _Inout_ CxUiEvent* ev);   // parent
#undef parent_handleEvent
#define parent_handleEvent(ev) CxUiWidget_handleEvent((CxUiWidget*)(self), ev)
CxUiResult CxUiRadio_handleEvent(_In_ CxUiRadio* self, _Inout_ CxUiEvent* ev)
{
    if (ev->kind != CXUI_EV_Key || ev->target != CxUiWidget(self))
        return CXUI_Default;
    CxUiKeyEvent* k = (CxUiKeyEvent*)ev;
    int32 step      = k->key == CXUI_KEY_Down || k->key == CXUI_KEY_Right ? 1
                      : k->key == CXUI_KEY_Up || k->key == CXUI_KEY_Left  ? -1
                                                                          : 0;
    CxUiRadioGroup* g = groupOf(CxUiWidget(self));
    if (!step || k->mods != 0 || !g)
        return CXUI_Default;
    if (!k->down)
        return CXUI_Handled;
    sa_ptr all;
    saInit(&all, ptr, 8);
    radiosIn(CxUiWidget(g), &all);
    int32 n = saSize(all), at = saFind(all, ptr, self);
    CxUiRadio* to = NULL;
    for (int32 i = 1; at >= 0 && i < n && !to; i++) {
        CxUiRadio* x = all.a[((at + step * i) % n + n) % n];
        if (available(CxUiWidget(x)))
            to = x;
    }
    saDestroy(&all);
    if (to) {
        objAcquire(to);
        radioChosen(to);
        _cxuiFocusByKey(CxUiWidget(to));
        objRelease(&to);
    }
    return CXUI_Handled;
}

void CxUiRadio_destroy(_In_ CxUiRadio* self)
{
    // Autogen begins -----
    strDestroy(&self->_title);
    objRelease(&self->_peerFont);
    // Autogen ends -------
}

// ---- CxUiRadioGroup ----

_objfactory_guaranteed CxUiRadioGroup* CxUiRadioGroup_createWith(_In_opt_ const CxUiBoxDesc* desc)
{
    CxUiRadioGroup* self;
    self = objInstCreate(CxUiRadioGroup);
    if (desc)
        _cxuiBoxSetup(CxUiBox(self), desc);

    objInstInit(self);
    return self;
}

_Ret_opt_valid_ CxUiRadio* CxUiRadioGroup_value(_In_ CxUiRadioGroup* self)
{
    if (!live(self))
        return NULL;
    sa_ptr all;
    saInit(&all, ptr, 8);
    radiosIn(CxUiWidget(self), &all);
    CxUiRadio* sel = NULL;
    for (int32 i = 0; i < saSize(all) && !sel; i++) {
        if (((CxUiRadio*)all.a[i])->_selected)
            sel = all.a[i];
    }
    saDestroy(&all);
    return sel;
}

void CxUiRadioGroup_setValue(_In_ CxUiRadioGroup* self, _In_opt_ CxUiRadio* radio)
{
    if (!live(self) || (radio && groupOf(CxUiWidget(radio)) != self))
        return;
    radioSelect(self, radio);
}

int32 CxUiRadioGroup_index(_In_ CxUiRadioGroup* self)
{
    if (!live(self))
        return -1;
    sa_ptr all;
    saInit(&all, ptr, 8);
    radiosIn(CxUiWidget(self), &all);
    int32 at = -1;
    for (int32 i = 0; i < saSize(all) && at < 0; i++) {
        if (((CxUiRadio*)all.a[i])->_selected)
            at = i;
    }
    saDestroy(&all);
    return at;
}

void CxUiRadioGroup_setIndex(_In_ CxUiRadioGroup* self, int32 index)
{
    if (!live(self))
        return;
    sa_ptr all;
    saInit(&all, ptr, 8);
    radiosIn(CxUiWidget(self), &all);
    radioSelect(self, index >= 0 && index < saSize(all) ? all.a[index] : NULL);
    saDestroy(&all);
}

extern uint32 CxUiWidget_eventMask(_In_ CxUiWidget* self);   // parent
#undef parent_eventMask
#define parent_eventMask() CxUiWidget_eventMask((CxUiWidget*)(self))
uint32 CxUiRadioGroup_eventMask(_In_ CxUiRadioGroup* self)
{
    return parent_eventMask() | (1 << CXUI_EV_Change);
}

// ---- lists and dropdowns ----

#define DEFAULT_LIST_ROWS 5

// What a list and a dropdown share: their items and selection, kept the same in the peer
typedef struct Items {
    CxUiWidget* w;
    sa_string* items;
    int32* value;
    CxUiList* list;   // a list's own state: several selected, held-back updates; NULL for a dropdown
} Items;

static bool itemsMulti(Items it)
{
    return it.list && it.list->_select == CXUI_SELECT_Multi;
}

// While a list is updating, the peer waits for the end, which gives it every item again.
static bool itemsHeld(Items it, int32 added, int32 removed)
{
    if (!it.list || it.list->_updating == 0)
        return false;
    if (it.list->_peerCount < 0)
        it.list->_peerCount = saSize(*it.items) - added + removed;
    return true;
}

static void itemsPushSelection(Items it)
{
    CxUiPeer* p = it.w->peer;
    if (!p)
        return;
    // A platform without several selected shows the first.
    if (itemsMulti(it) && _cxui.backend->listSetMarks)
        _cxui.backend->listSetMarks(p, it.list->_marks.a, saSize(it.list->_marks));
    else
        _cxui.backend->listSetValue(p, *it.value);
}

static void itemsSelectionChanged(Items it)
{
    if (!itemsHeld(it, 0, 0))
        itemsPushSelection(it);
}

static void itemsPush(Items it, int32 at, int32 removed, int32 added)
{
    if (itemsHeld(it, added, removed))
        return;
    CxUiPeer* p = it.w->peer;
    if (p) {
        _cxui.backend->listSplice(p, at, removed, it.items->a + at, added);
        itemsPushSelection(it);
    }
    _cxuiLayoutDirty(it.w);
}

// The first item marked, which a list where any number can be selected gives as its value
static void marksValue(Items it)
{
    int32 v = -1;
    for (int32 i = 0; i < saSize(it.list->_marks) && v < 0; i++) {
        if (it.list->_marks.a[i])
            v = i;
    }
    *it.value = v;
}

static void itemsInit(Items it, const strref* items, int32 value)
{
    for (; items && *items; items++)
        saPush(it.items, strref, *items);
    *it.value = value >= 0 && value < saSize(*it.items) ? value : -1;
    if (itemsMulti(it)) {
        for (int32 i = 0; i < saSize(*it.items); i++)
            saPush(&it.list->_marks, uint8, i == *it.value);
    }
}

static strref itemsAt(Items it, int32 i)
{
    return _cxuiLive(it.w) && i >= 0 && i < saSize(*it.items) ? (strref)it.items->a[i] : NULL;
}

static void itemsSet(Items it, sa_string items)
{
    if (!_cxuiLive(it.w))
        return;
    int32 old = saSize(*it.items);
    saClear(it.items);
    for (int32 i = 0; i < saSize(items); i++)
        saPush(it.items, string, items.a[i]);
    *it.value = -1;
    if (itemsMulti(it)) {
        saClear(&it.list->_marks);
        for (int32 i = 0; i < saSize(items); i++)
            saPush(&it.list->_marks, uint8, 0);
    }
    itemsPush(it, 0, old, saSize(*it.items));
}

static void itemsInsert(Items it, int32 at, strref text)
{
    if (!_cxuiLive(it.w))
        return;
    at = at < 0 || at > saSize(*it.items) ? saSize(*it.items) : at;
    saInsert(it.items, at, strref, text);
    if (*it.value >= at)
        (*it.value)++;
    if (itemsMulti(it))
        saInsert(&it.list->_marks, at, uint8, 0);
    itemsPush(it, at, 0, 1);
}

static void itemsSetItem(Items it, int32 at, strref text)
{
    if (!_cxuiLive(it.w) || at < 0 || at >= saSize(*it.items) || strEq(text, it.items->a[at]))
        return;
    strDup(&it.items->a[at], text);
    itemsPush(it, at, 1, 1);
}

static void itemsRemove(Items it, int32 at)
{
    if (!_cxuiLive(it.w) || at < 0 || at >= saSize(*it.items))
        return;
    saRemove(it.items, at);
    if (itemsMulti(it)) {
        saRemove(&it.list->_marks, at);
        marksValue(it);
    } else if (*it.value == at) {
        *it.value = -1;
    } else if (*it.value > at) {
        (*it.value)--;
    }
    itemsPush(it, at, 1, 0);
}

static void itemsClear(Items it)
{
    if (!_cxuiLive(it.w) || saSize(*it.items) == 0)
        return;
    int32 old = saSize(*it.items);
    saClear(it.items);
    *it.value = -1;
    if (itemsMulti(it))
        saClear(&it.list->_marks);
    itemsPush(it, 0, old, 0);
}

static int32 itemsValue(Items it)
{
    return _cxuiLive(it.w) ? *it.value : -1;
}

static void itemsSetValue(Items it, int32 value)
{
    if (!_cxuiLive(it.w))
        return;
    value = value >= 0 && value < saSize(*it.items) ? value : -1;
    if (itemsMulti(it)) {
        for (int32 i = 0; i < saSize(it.list->_marks); i++)
            it.list->_marks.a[i] = i == value;
    } else if (value == *it.value) {
        return;
    }
    *it.value = value;
    itemsSelectionChanged(it);
}

// The widest item's text, in DIPs, or columns average characters
static int32 itemsWidth(Items it, CxUiFont* f, int32 columns)
{
    float32 s = cxuiFontScale(f);
    if (columns > 0) {
        CxUiFontMetrics fm;
        cxuiFontMetrics(f, &fm);
        return (int32)ceilf(columns * fm.avgCharW / s);
    }
    int32 widest = 0;
    for (int32 i = 0; i < saSize(*it.items); i++)
        widest = max(widest, cxuiFontAdvance(f, it.items->a[i]));
    return max((int32)ceilf(widest / s), (int32)ceilf(cxuiFontAdvance(f, _S"MMMM") / s));
}

static CxUiSize itemsMeasure(Items it, CxUiFont** peerFont, int32 columns, int32 rows, bool dropdown)
{
    CxUiFont* f = cxuiWidgetFont(it.w);
    setPeerFont(peerFont, it.w->peer, f);
    int32 widest = itemsWidth(it, f, columns);
    CxUiSize n   = { 0 };
    if (_cxui.backend->listMeasure)
        n = _cxui.backend->listMeasure(it.w, f, widest, rows, dropdown);
    if (n.w <= 0 && n.h <= 0) {
        CxUiFontMetrics fm;
        cxuiFontMetrics(f, &fm);
        int32 line = (int32)ceilf(fm.height / cxuiFontScale(f));
        // A border and a scroll bar, or the dropdown's arrow
        n = dropdown ? (CxUiSize){ widest + 32, line + 10 } : (CxUiSize){ widest + 26, rows * (line + 4) + 4 };
    }
    return n;
}

static void itemsRealize(Items it, CxUiPeer* host, CxUiFont** peerFont, int32 columns, int32 rows, bool dropdown)
{
    if (it.w->peer || !_cxui.backend->listCreate)
        return;
    CxUiPeer* p = _cxui.backend->listCreate(it.w, host, dropdown);
    if (!p)
        return;
    _cxuiPeerAttach(it.w, p);
    objRelease(peerFont);
    setPeerFont(peerFont, p, cxuiWidgetFont(it.w));
    _cxui.backend->listSplice(p, 0, 0, it.items->a, saSize(*it.items));
    itemsPushSelection(it);
    if (it.list)
        it.list->_peerCount = -1;
    if (_cxui.backend->listMeasure) {
        CxUiSize n = itemsMeasure(it, peerFont, columns, rows, dropdown);
        if (n.w != it.w->_measured.w || n.h != it.w->_measured.h)
            _cxuiLayoutDirty(it.w);
    }
}

// A list's and a dropdown's colors are a text field's.
static void itemsRestyle(CxUiWidget* w)
{
    if (!w->peer || !_cxui.backend->listSetColors)
        return;
    CxUiPalette* pal = cxuiWidgetPalette(w);
    _cxui.backend->listSetColors(w->peer, cxuiPaletteColor(pal, CXUI_PAL_Input),
                                 cxuiPaletteColor(pal, inactive(w) ? CXUI_PAL_TextDisabled : CXUI_PAL_InputText));
}

static Items listItems(CxUiList* l)
{
    return (Items){ CxUiWidget(l), &l->_items, &l->_value, l };
}

static Items dropdownItems(CxUiDropdown* d)
{
    return (Items){ CxUiWidget(d), &d->_items, &d->_value };
}

_objfactory_guaranteed CxUiList* CxUiList_createWith(_In_opt_ const CxUiListDesc* desc)
{
    CxUiList* self;
    self                = objInstCreate(CxUiList);
    self->_visibleItems = DEFAULT_LIST_ROWS;

    objInstInit(self);
    self->_value     = -1;
    self->_top       = -1;
    self->_peerCount = -1;
    if (desc) {
        self->_select = desc->select == CXUI_SELECT_Multi ? CXUI_SELECT_Multi : CXUI_SELECT_Single;
        itemsInit(listItems(self), desc->items, desc->value);
        if (desc->visibleItems > 0)
            self->_visibleItems = desc->visibleItems;
        self->_columns        = max(desc->columns, 0);
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }
    return self;
}

int32 CxUiList_count(_In_ CxUiList* self)
{
    return live(self) ? saSize(self->_items) : 0;
}

strref CxUiList_item(_In_ CxUiList* self, int32 index)
{
    return itemsAt(listItems(self), index);
}

void CxUiList_setItems(_In_ CxUiList* self, _In_ sa_string items)
{
    itemsSet(listItems(self), items);
}

void CxUiList_append(_In_ CxUiList* self, _In_ strref text)
{
    itemsInsert(listItems(self), -1, text);
}

void CxUiList_insert(_In_ CxUiList* self, int32 index, _In_ strref text)
{
    itemsInsert(listItems(self), index, text);
}

void CxUiList_setItem(_In_ CxUiList* self, int32 index, _In_ strref text)
{
    itemsSetItem(listItems(self), index, text);
}

void CxUiList_remove(_In_ CxUiList* self, int32 index)
{
    itemsRemove(listItems(self), index);
}

void CxUiList_clear(_In_ CxUiList* self)
{
    itemsClear(listItems(self));
}

int32 CxUiList_value(_In_ CxUiList* self)
{
    return itemsValue(listItems(self));
}

void CxUiList_setValue(_In_ CxUiList* self, int32 index)
{
    itemsSetValue(listItems(self), index);
}

void CxUiList_show(_In_ CxUiList* self, int32 index)
{
    if (live(self) && peerOf(self) && index >= 0 && index < saSize(self->_items) && _cxui.backend->listShow)
        _cxui.backend->listShow(peerOf(self), index);
}

extern uint32 CxUiWidget_eventMask(_In_ CxUiWidget* self);   // parent
#undef parent_eventMask
#define parent_eventMask() CxUiWidget_eventMask((CxUiWidget*)(self))
uint32 CxUiList_eventMask(_In_ CxUiList* self)
{
    return parent_eventMask() | (1 << CXUI_EV_Change) | (1 << CXUI_EV_Activate) | (1 << CXUI_EV_Key) |
           (1 << CXUI_EV_Focus);
}

extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#undef parent_measure
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiList_measure(_In_ CxUiList* self)
{
    return itemsMeasure(listItems(self), &self->_peerFont, self->_columns, self->_visibleItems, false);
}

extern void CxUiWidget_realizePeer(_In_ CxUiWidget* self, _In_ CxUiPeer* host);   // parent
#undef parent_realizePeer
#define parent_realizePeer(host) CxUiWidget_realizePeer((CxUiWidget*)(self), host)
void CxUiList_realizePeer(_In_ CxUiList* self, _In_ CxUiPeer* host)
{
    itemsRealize(listItems(self), host, &self->_peerFont, self->_columns, self->_visibleItems, false);
    if (peerOf(self) && self->_top >= 0 && _cxui.backend->listSetTopItem) {
        _cxui.backend->listSetTopItem(peerOf(self), self->_top);
        self->_top = -1;
    }
}

extern void CxUiWidget_placePeer(_In_ CxUiWidget* self, CxUiRect px);   // parent
#undef parent_placePeer
#define parent_placePeer(px) CxUiWidget_placePeer((CxUiWidget*)(self), px)
void CxUiList_placePeer(_In_ CxUiList* self, CxUiRect px)
{
    if (peerOf(self))
        _cxui.backend->controlSetBounds(peerOf(self), px);
}

extern void CxUiWidget_restyle(_In_ CxUiWidget* self);   // parent
#undef parent_restyle
#define parent_restyle() CxUiWidget_restyle((CxUiWidget*)(self))
void CxUiList_restyle(_In_ CxUiList* self)
{
    itemsRestyle(CxUiWidget(self));
}

extern bool CxUiWidget_canFocus(_In_ CxUiWidget* self);   // parent
#undef parent_canFocus
#define parent_canFocus() CxUiWidget_canFocus((CxUiWidget*)(self))
bool CxUiList_canFocus(_In_ CxUiList* self)
{
    return true;
}

// Enter opens the selected item, as a double click does.
extern CxUiResult CxUiWidget_handleEvent(_In_ CxUiWidget* self, _Inout_ CxUiEvent* ev);   // parent
#undef parent_handleEvent
#define parent_handleEvent(ev) CxUiWidget_handleEvent((CxUiWidget*)(self), ev)
CxUiResult CxUiList_handleEvent(_In_ CxUiList* self, _Inout_ CxUiEvent* ev)
{
    if (ev->kind != CXUI_EV_Key || ev->target != CxUiWidget(self))
        return CXUI_Default;
    CxUiKeyEvent* k = (CxUiKeyEvent*)ev;
    if (!(k->key == CXUI_KEY_Enter || k->key == CXUI_KEY_PadEnter) || k->mods != 0 || self->_value < 0)
        return CXUI_Default;
    if (k->down && !k->repeat) {
        CxUiActivateEvent act = { .base.kind = CXUI_EV_Activate, .item = (uint64)self->_value };
        _cxuiDispatch(CxUiWidget(self), &act.base);
    }
    return CXUI_Handled;
}

void CxUiList_destroy(_In_ CxUiList* self)
{
    // Autogen begins -----
    saDestroy(&self->_items);
    objRelease(&self->_peerFont);
    saDestroy(&self->_marks);
    // Autogen ends -------
}

_objfactory_guaranteed CxUiDropdown* CxUiDropdown_createWith(_In_opt_ const CxUiDropdownDesc* desc)
{
    CxUiDropdown* self;
    self = objInstCreate(CxUiDropdown);

    objInstInit(self);
    self->_value = -1;
    if (desc) {
        itemsInit(dropdownItems(self), desc->items, desc->value);
        self->_columns        = max(desc->columns, 0);
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }
    return self;
}

int32 CxUiDropdown_count(_In_ CxUiDropdown* self)
{
    return live(self) ? saSize(self->_items) : 0;
}

strref CxUiDropdown_item(_In_ CxUiDropdown* self, int32 index)
{
    return itemsAt(dropdownItems(self), index);
}

void CxUiDropdown_setItems(_In_ CxUiDropdown* self, _In_ sa_string items)
{
    itemsSet(dropdownItems(self), items);
}

void CxUiDropdown_append(_In_ CxUiDropdown* self, _In_ strref text)
{
    itemsInsert(dropdownItems(self), -1, text);
}

void CxUiDropdown_insert(_In_ CxUiDropdown* self, int32 index, _In_ strref text)
{
    itemsInsert(dropdownItems(self), index, text);
}

void CxUiDropdown_setItem(_In_ CxUiDropdown* self, int32 index, _In_ strref text)
{
    itemsSetItem(dropdownItems(self), index, text);
}

void CxUiDropdown_remove(_In_ CxUiDropdown* self, int32 index)
{
    itemsRemove(dropdownItems(self), index);
}

void CxUiDropdown_clear(_In_ CxUiDropdown* self)
{
    itemsClear(dropdownItems(self));
}

int32 CxUiDropdown_value(_In_ CxUiDropdown* self)
{
    return itemsValue(dropdownItems(self));
}

void CxUiDropdown_setValue(_In_ CxUiDropdown* self, int32 index)
{
    itemsSetValue(dropdownItems(self), index);
}

extern uint32 CxUiWidget_eventMask(_In_ CxUiWidget* self);   // parent
#undef parent_eventMask
#define parent_eventMask() CxUiWidget_eventMask((CxUiWidget*)(self))
uint32 CxUiDropdown_eventMask(_In_ CxUiDropdown* self)
{
    return parent_eventMask() | (1 << CXUI_EV_Change) | (1 << CXUI_EV_Key) | (1 << CXUI_EV_Focus);
}

extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#undef parent_measure
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiDropdown_measure(_In_ CxUiDropdown* self)
{
    return itemsMeasure(dropdownItems(self), &self->_peerFont, self->_columns, 1, true);
}

extern void CxUiWidget_realizePeer(_In_ CxUiWidget* self, _In_ CxUiPeer* host);   // parent
#undef parent_realizePeer
#define parent_realizePeer(host) CxUiWidget_realizePeer((CxUiWidget*)(self), host)
void CxUiDropdown_realizePeer(_In_ CxUiDropdown* self, _In_ CxUiPeer* host)
{
    itemsRealize(dropdownItems(self), host, &self->_peerFont, self->_columns, 1, true);
}

extern void CxUiWidget_placePeer(_In_ CxUiWidget* self, CxUiRect px);   // parent
#undef parent_placePeer
#define parent_placePeer(px) CxUiWidget_placePeer((CxUiWidget*)(self), px)
void CxUiDropdown_placePeer(_In_ CxUiDropdown* self, CxUiRect px)
{
    if (peerOf(self))
        _cxui.backend->controlSetBounds(peerOf(self), px);
}

extern void CxUiWidget_restyle(_In_ CxUiWidget* self);   // parent
#undef parent_restyle
#define parent_restyle() CxUiWidget_restyle((CxUiWidget*)(self))
void CxUiDropdown_restyle(_In_ CxUiDropdown* self)
{
    itemsRestyle(CxUiWidget(self));
}

extern bool CxUiWidget_canFocus(_In_ CxUiWidget* self);   // parent
#undef parent_canFocus
#define parent_canFocus() CxUiWidget_canFocus((CxUiWidget*)(self))
bool CxUiDropdown_canFocus(_In_ CxUiDropdown* self)
{
    return buttonsFocus();
}

void CxUiDropdown_destroy(_In_ CxUiDropdown* self)
{
    // Autogen begins -----
    saDestroy(&self->_items);
    objRelease(&self->_peerFont);
    // Autogen ends -------
}

static Items itemsOf(CxUiWidget* w)
{
    CxUiList* l     = objDynCast(CxUiList, w);
    CxUiDropdown* d = objDynCast(CxUiDropdown, w);
    return l ? listItems(l) : d ? dropdownItems(d) : (Items){ 0 };
}

_Use_decl_annotations_
void _cxuiListChosen(CxUiPeer* p, int32 index)
{
    Items it = p->w ? itemsOf(p->w) : (Items){ 0 };
    if (!it.w || !_cxuiLive(it.w))
        return;
    index = index >= 0 && index < saSize(*it.items) ? index : -1;
    if (index == *it.value)
        return;
    *it.value         = index;
    CxUiChangeEvent ev = { .base.kind = CXUI_EV_Change };
    _cxuiDispatch(it.w, &ev.base);
}

_Use_decl_annotations_
void _cxuiListMarksChanged(CxUiPeer* p)
{
    CxUiList* l = p->w ? objDynCast(CxUiList, p->w) : NULL;
    if (!l || !_cxuiLive(CxUiWidget(l)) || l->_select != CXUI_SELECT_Multi || !_cxui.backend->listGetMarks)
        return;
    int32 n       = saSize(l->_marks);
    uint8* marks  = xaAlloc(max(n, 1), XA_Zero);
    _cxui.backend->listGetMarks(p, marks, n);
    bool changed  = false;
    for (int32 i = 0; i < n; i++) {
        uint8 on = marks[i] != 0;
        changed  = changed || on != l->_marks.a[i];
        l->_marks.a[i] = on;
    }
    xaFree(marks);
    if (!changed)
        return;
    marksValue(listItems(l));
    CxUiChangeEvent ev = { .base.kind = CXUI_EV_Change };
    _cxuiDispatch(CxUiWidget(l), &ev.base);
}

_Use_decl_annotations_
void _cxuiListActivated(CxUiPeer* p, int32 index)
{
    CxUiList* l = p->w ? objDynCast(CxUiList, p->w) : NULL;
    if (!l || !_cxuiLive(CxUiWidget(l)) || index < 0 || index >= saSize(l->_items))
        return;
    CxUiActivateEvent ev = { .base.kind = CXUI_EV_Activate, .item = (uint64)index };
    _cxuiDispatch(CxUiWidget(l), &ev.base);
}

// ---- sliders ----

#define DEFAULT_BAR_LENGTH 160

// The native control's positions: whole steps from the low end
static int32 sliderRange(CxUiSlider* s)
{
    return max((int32)floor((s->_max - s->_min) / s->_step + 0.5), 1);
}

static int32 sliderPos(CxUiSlider* s, float64 value)
{
    int32 pos = (int32)floor((value - s->_min) / s->_step + 0.5);
    return clamp(pos, 0, sliderRange(s));
}

static float64 sliderValue(CxUiSlider* s, int32 pos)
{
    float64 v = s->_min + pos * s->_step;
    return v > s->_max ? s->_max : v;
}

static void sliderSetup(CxUiSlider* s, float64 min, float64 max, float64 step)
{
    if (min == 0 && max == 0)
        max = 100;
    if (max < min) {
        float64 t = min;
        min       = max;
        max       = t;
    }
    if (max == min)
        max = min + 1;
    s->_min  = min;
    s->_max  = max;
    s->_step = step > 0 ? step : (max - min) / 100;
}

// The range, page and ticks, then the position
static void sliderPush(CxUiSlider* s)
{
    CxUiPeer* p = peerOf(s);
    if (!p)
        return;
    int32 range = sliderRange(s);
    int32 page  = max((int32)floor(s->_page / s->_step + 0.5), 1);
    int32 every = s->_ticks > 1 ? max(range / (s->_ticks - 1), 1) : 0;
    _cxui.backend->sliderSetRange(p, range, page, every);
    _cxui.backend->sliderSetPos(p, sliderPos(s, s->_value));
}

_objfactory_guaranteed CxUiSlider* CxUiSlider_createWith(_In_opt_ const CxUiSliderDesc* desc)
{
    CxUiSlider* self;
    self = objInstCreate(CxUiSlider);
    sliderSetup(self, desc ? desc->min : 0, desc ? desc->max : 0, desc ? desc->step : 0);
    self->_page   = desc && desc->pageStep > 0 ? desc->pageStep : self->_step * 10;
    self->_length = desc && desc->length > 0 ? desc->length : DEFAULT_BAR_LENGTH;
    if (desc) {
        self->_ticks          = max(desc->ticks, 0);
        self->_orient         = desc->orient == CXUI_ORIENT_V ? CXUI_ORIENT_V : CXUI_ORIENT_H;
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }
    self->_value     = sliderValue(self, sliderPos(self, desc ? desc->value : 0));
    self->_committed = self->_value;

    objInstInit(self);
    return self;
}

float64 CxUiSlider_value(_In_ CxUiSlider* self)
{
    return live(self) ? self->_value : 0;
}

void CxUiSlider_setValue(_In_ CxUiSlider* self, float64 value)
{
    if (!live(self))
        return;
    self->_value = self->_committed = sliderValue(self, sliderPos(self, value));
    if (peerOf(self))
        _cxui.backend->sliderSetPos(peerOf(self), sliderPos(self, self->_value));
}

void CxUiSlider_setRange(_In_ CxUiSlider* self, float64 min, float64 max, float64 step)
{
    if (!live(self))
        return;
    float64 pageSteps = self->_page / self->_step;
    sliderSetup(self, min, max, step);
    self->_page  = pageSteps * self->_step;
    self->_value = self->_committed = sliderValue(self, sliderPos(self, self->_value));
    sliderPush(self);
}

extern uint32 CxUiWidget_eventMask(_In_ CxUiWidget* self);   // parent
#undef parent_eventMask
#define parent_eventMask() CxUiWidget_eventMask((CxUiWidget*)(self))
uint32 CxUiSlider_eventMask(_In_ CxUiSlider* self)
{
    return parent_eventMask() | (1 << CXUI_EV_Change) | (1 << CXUI_EV_Commit) | (1 << CXUI_EV_Key) |
           (1 << CXUI_EV_Focus);
}

extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#undef parent_measure
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiSlider_measure(_In_ CxUiSlider* self)
{
    bool vertical = self->_orient == CXUI_ORIENT_V;
    CxUiSize n    = { 0 };
    if (_cxui.backend->sliderMeasure)
        n = _cxui.backend->sliderMeasure(CxUiWidget(self), self->_length, vertical, self->_ticks > 1);
    if (n.w <= 0 && n.h <= 0) {
        int32 across = self->_ticks > 1 ? 32 : 24;
        n            = vertical ? (CxUiSize){ across, self->_length } : (CxUiSize){ self->_length, across };
    }
    return n;
}

extern void CxUiWidget_realizePeer(_In_ CxUiWidget* self, _In_ CxUiPeer* host);   // parent
#undef parent_realizePeer
#define parent_realizePeer(host) CxUiWidget_realizePeer((CxUiWidget*)(self), host)
void CxUiSlider_realizePeer(_In_ CxUiSlider* self, _In_ CxUiPeer* host)
{
    if (peerOf(self) || !_cxui.backend->sliderCreate)
        return;
    CxUiPeer* p = _cxui.backend->sliderCreate(CxUiWidget(self), host, self->_orient == CXUI_ORIENT_V);
    if (!p)
        return;
    _cxuiPeerAttach(CxUiWidget(self), p);
    sliderPush(self);
}

extern void CxUiWidget_placePeer(_In_ CxUiWidget* self, CxUiRect px);   // parent
#undef parent_placePeer
#define parent_placePeer(px) CxUiWidget_placePeer((CxUiWidget*)(self), px)
void CxUiSlider_placePeer(_In_ CxUiSlider* self, CxUiRect px)
{
    if (peerOf(self))
        _cxui.backend->controlSetBounds(peerOf(self), px);
}

extern bool CxUiWidget_canFocus(_In_ CxUiWidget* self);   // parent
#undef parent_canFocus
#define parent_canFocus() CxUiWidget_canFocus((CxUiWidget*)(self))
bool CxUiSlider_canFocus(_In_ CxUiSlider* self)
{
    return buttonsFocus();
}

_Use_decl_annotations_
void _cxuiSliderMoved(CxUiPeer* p, int32 pos, bool done)
{
    CxUiSlider* s = p->w ? objDynCast(CxUiSlider, p->w) : NULL;
    if (!s || !live(s))
        return;
    objAcquire(s);
    float64 v = sliderValue(s, clamp(pos, 0, sliderRange(s)));
    if (v != s->_value) {
        s->_value          = v;
        CxUiChangeEvent ev = { .base.kind = CXUI_EV_Change };
        _cxuiDispatch(CxUiWidget(s), &ev.base);
    }
    if (done && live(s) && s->_value != s->_committed) {
        s->_committed      = s->_value;
        CxUiCommitEvent ev = { .base.kind = CXUI_EV_Commit };
        _cxuiDispatch(CxUiWidget(s), &ev.base);
    }
    objRelease(&s);
}

// ---- progress bars ----

static void progressPush(CxUiProgress* pr)
{
    if (peerOf(pr))
        _cxui.backend->progressSet(peerOf(pr), pr->_value / pr->_max, pr->_indeterminate);
}

_objfactory_guaranteed CxUiProgress* CxUiProgress_createWith(_In_opt_ const CxUiProgressDesc* desc)
{
    CxUiProgress* self;
    self          = objInstCreate(CxUiProgress);
    self->_max    = desc && desc->max > 0 ? desc->max : 100;
    self->_length = desc && desc->length > 0 ? desc->length : DEFAULT_BAR_LENGTH;
    if (desc) {
        self->_value          = desc->value < 0 ? 0 : desc->value > self->_max ? self->_max : desc->value;
        self->_indeterminate  = desc->indeterminate;
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }

    objInstInit(self);
    return self;
}

float64 CxUiProgress_value(_In_ CxUiProgress* self)
{
    return live(self) ? self->_value : 0;
}

void CxUiProgress_setValue(_In_ CxUiProgress* self, float64 value)
{
    if (!live(self))
        return;
    self->_value         = value < 0 ? 0 : value > self->_max ? self->_max : value;
    self->_indeterminate = false;
    progressPush(self);
}

void CxUiProgress_setMax(_In_ CxUiProgress* self, float64 max)
{
    if (!live(self))
        return;
    self->_max   = max > 0 ? max : 100;
    self->_value = self->_value > self->_max ? self->_max : self->_value;
    progressPush(self);
}

void CxUiProgress_setIndeterminate(_In_ CxUiProgress* self, bool indeterminate)
{
    if (!live(self) || indeterminate == self->_indeterminate)
        return;
    self->_indeterminate = indeterminate;
    progressPush(self);
}

extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#undef parent_measure
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiProgress_measure(_In_ CxUiProgress* self)
{
    CxUiSize n = { 0 };
    if (_cxui.backend->progressMeasure)
        n = _cxui.backend->progressMeasure(CxUiWidget(self), self->_length);
    return n.w > 0 || n.h > 0 ? n : (CxUiSize){ self->_length, 16 };
}

extern void CxUiWidget_realizePeer(_In_ CxUiWidget* self, _In_ CxUiPeer* host);   // parent
#undef parent_realizePeer
#define parent_realizePeer(host) CxUiWidget_realizePeer((CxUiWidget*)(self), host)
void CxUiProgress_realizePeer(_In_ CxUiProgress* self, _In_ CxUiPeer* host)
{
    if (peerOf(self) || !_cxui.backend->progressCreate)
        return;
    CxUiPeer* p = _cxui.backend->progressCreate(CxUiWidget(self), host);
    if (!p)
        return;
    _cxuiPeerAttach(CxUiWidget(self), p);
    progressPush(self);
}

extern void CxUiWidget_placePeer(_In_ CxUiWidget* self, CxUiRect px);   // parent
#undef parent_placePeer
#define parent_placePeer(px) CxUiWidget_placePeer((CxUiWidget*)(self), px)
void CxUiProgress_placePeer(_In_ CxUiProgress* self, CxUiRect px)
{
    if (peerOf(self))
        _cxui.backend->controlSetBounds(peerOf(self), px);
}

// ---- presses ----

// Shown in its window: it and every container above it shown, and each showing it (a zbox
// shows one page)
static bool shownInWindow(CxUiWidget* w)
{
    for (CxUiWidget* x = w; x; x = x->parent) {
        if (x->state & CXUI_WS_Hidden)
            return false;
        if (x->parent && !cxuiWidgetShowsChild(x->parent, x))
            return false;
    }
    return true;
}

_Use_decl_annotations_
bool _cxuiPress(CxUiWidget* w)
{
    if (!w || !_cxuiLive(w) || !w->peer || (w->state & CXUI_WS_Inactive) || !shownInWindow(w))
        return false;
    CxUiCheck* c = objDynCast(CxUiCheck, w);
    CxUiRadio* r = objDynCast(CxUiRadio, w);
    if (c) {
        checkClicked(c);
    } else if (r) {
        radioChosen(r);
    } else {
        CxUiActivateEvent ev = { .base.kind = CXUI_EV_Activate };
        _cxuiDispatch(w, &ev.base);
    }
    return true;
}

_Use_decl_annotations_
void _cxuiButtonClicked(CxUiPeer* p)
{
    if (p->w)
        _cxuiPress(p->w);
}

_objinit_guaranteed bool CxUiList_init(_In_ CxUiList* self)
{
    // Autogen begins -----
    saInit(&self->_items, string, 1);
    saInit(&self->_marks, uint8, 1);
    return true;
    // Autogen ends -------
}

_objinit_guaranteed bool CxUiDropdown_init(_In_ CxUiDropdown* self)
{
    // Autogen begins -----
    saInit(&self->_items, string, 1);
    return true;
    // Autogen ends -------
}

bool CxUiList_isSelected(_In_ CxUiList* self, int32 index)
{
    if (!live(self) || index < 0 || index >= saSize(self->_items))
        return false;
    return self->_select == CXUI_SELECT_Multi ? self->_marks.a[index] != 0 : self->_value == index;
}

int32 CxUiList_selectedCount(_In_ CxUiList* self)
{
    if (!live(self))
        return 0;
    if (self->_select != CXUI_SELECT_Multi)
        return self->_value >= 0 ? 1 : 0;
    int32 n = 0;
    for (int32 i = 0; i < saSize(self->_marks); i++)
        n += self->_marks.a[i] != 0;
    return n;
}

int32 CxUiList_nextSelected(_In_ CxUiList* self, int32 after)
{
    if (!live(self))
        return -1;
    if (self->_select != CXUI_SELECT_Multi)
        return self->_value > after ? self->_value : -1;
    for (int32 i = max(after + 1, 0); i < saSize(self->_marks); i++) {
        if (self->_marks.a[i])
            return i;
    }
    return -1;
}

void CxUiList_setSelected(_In_ CxUiList* self, int32 index, int32 n, bool selected)
{
    int32 count = live(self) ? saSize(self->_items) : 0;
    int32 first = max(index, 0), end = (int32)min((int64)index + max(n, 0), (int64)count);
    if (first >= end)
        return;
    Items it = listItems(self);
    if (self->_select != CXUI_SELECT_Multi) {
        if (selected)
            itemsSetValue(it, first);
        else if (self->_value >= first && self->_value < end)
            itemsSetValue(it, -1);
        return;
    }
    for (int32 i = first; i < end; i++)
        self->_marks.a[i] = selected;
    marksValue(it);
    itemsSelectionChanged(it);
}

void CxUiList_selectAll(_In_ CxUiList* self)
{
    if (live(self) && self->_select == CXUI_SELECT_Multi)
        CxUiList_setSelected(self, 0, saSize(self->_items), true);
}

void CxUiList_clearSelection(_In_ CxUiList* self)
{
    if (live(self))
        CxUiList_setSelected(self, 0, saSize(self->_items), false);
}

int32 CxUiList_topItem(_In_ CxUiList* self)
{
    if (!live(self))
        return 0;
    if (peerOf(self) && self->_top < 0 && self->_updating == 0 && _cxui.backend->listTopItem)
        return _cxui.backend->listTopItem(peerOf(self));
    return max(self->_top, 0);
}

void CxUiList_setTopItem(_In_ CxUiList* self, int32 index)
{
    if (!live(self))
        return;
    self->_top = clamp(index, 0, max(saSize(self->_items) - 1, 0));
    if (peerOf(self) && self->_updating == 0 && _cxui.backend->listSetTopItem) {
        _cxui.backend->listSetTopItem(peerOf(self), self->_top);
        self->_top = -1;
    }
}

void CxUiList_beginUpdate(_In_ CxUiList* self)
{
    if (live(self))
        self->_updating++;
}

void CxUiList_endUpdate(_In_ CxUiList* self)
{
    if (!live(self) || self->_updating == 0 || --self->_updating > 0)
        return;
    CxUiPeer* p = peerOf(self);
    if (self->_peerCount >= 0) {
        if (p) {
            // Giving the platform every item again scrolls it to the top; it stays where it was
            // unless a top item was set meanwhile.
            if (self->_top < 0 && _cxui.backend->listTopItem)
                self->_top = min(_cxui.backend->listTopItem(p), max(saSize(self->_items) - 1, 0));
            _cxui.backend->listSplice(p, 0, self->_peerCount, self->_items.a, saSize(self->_items));
            itemsPushSelection(listItems(self));
        }
        self->_peerCount = -1;
        _cxuiLayoutDirty(CxUiWidget(self));
    }
    if (p && self->_top >= 0 && _cxui.backend->listSetTopItem) {
        _cxui.backend->listSetTopItem(p, self->_top);
        self->_top = -1;
    }
}

// Autogen begins -----
// clang-format off
#include "core/cxuicontrols.auto.inc"
// clang-format on
// Autogen ends -------
