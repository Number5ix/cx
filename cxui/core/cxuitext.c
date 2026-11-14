// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "core/cxuitext.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cxuidraw_private.h"

#include <math.h>

#define DEFAULT_AREA_COLUMNS 40
#define DEFAULT_AREA_LINES   4
#define DEFAULT_COLUMNS 20

static bool live(void* w)
{
    return _cxuiLive((CxUiWidget*)w);
}

static CxUiTextFrame* frameOf(CxUiTextField* self)
{
    CxUiWidget* parent = CxUiWidget(self)->parent;
    return parent ? objDynCast(CxUiTextFrame, parent) : NULL;
}

// The frame's background: its own role or color (the input color unless the program picks
// another), or the bad-value color while marked bad
static CxUiColor frameBg(CxUiTextFrame* frame)
{
    if (frame->_bad)
        return cxuiPaletteColor(cxuiWidgetPalette(frame), CXUI_PAL_InputBad);
    return _cxuiBackgroundOf(CxUiWidget(frame));
}

static CxUiPeer* peerOf(CxUiTextField* self)
{
    return CxUiWidget(self)->peer;
}

static int32 textLen(CxUiTextField* self)
{
    return _cxuiCodepoints(self->_text);
}

static void pushFont(CxUiTextField* self, CxUiFont* f)
{
    if (!peerOf(self) || f == self->_peerFont)
        return;
    objRelease(&self->_peerFont);
    self->_peerFont = objAcquire(f);
    _cxui.backend->textSetFont(peerOf(self), f);
}

static void commit(CxUiTextField* self)
{
    self->_edited      = false;
    CxUiCommitEvent ev = { .base.kind = CXUI_EV_Commit };
    _cxuiDispatch(CxUiWidget(self), &ev.base);
}

// Edits the platform reports for text set from code are dropped here, as the field has it already.
_Use_decl_annotations_
void _cxuiTextChanged(CxUiPeer* p, strref text)
{
    CxUiTextField* self = p->w ? objDynCast(CxUiTextField, p->w) : NULL;
    if (!self || !live(self) || strEq(text, self->_text))
        return;
    strDup(&self->_text, text);
    self->_edited      = true;
    CxUiChangeEvent ev = { .base.kind = CXUI_EV_Change };
    _cxuiDispatch(CxUiWidget(self), &ev.base);
}

// ---- CxUiTextField ----

_objfactory_guaranteed CxUiTextField* CxUiTextField_createWith(_In_opt_ const CxUiTextFieldDesc* desc)
{
    CxUiTextField* self;
    self           = objInstCreate(CxUiTextField);
    self->_columns = DEFAULT_COLUMNS;
    if (desc) {
        strDup(&self->_text, desc->text);
        strDup(&self->_cue, desc->cue);
        if (desc->columns > 0)
            self->_columns = desc->columns;
        self->_maxLength      = max(desc->maxLength, 0);
        self->_readOnly       = desc->readOnly;
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }

    objInstInit(self);
    return self;
}

strref CxUiTextField_text(_In_ CxUiTextField* self)
{
    return live(self) ? (strref)self->_text : NULL;
}

void CxUiTextField_setText(_In_ CxUiTextField* self, _In_ strref text)
{
    if (!live(self))
        return;
    strDup(&self->_text, text);
    self->_edited   = false;
    self->_selStart = self->_selEnd = textLen(self);
    if (peerOf(self)) {
        _cxui.backend->textSetText(peerOf(self), self->_text);
        _cxui.backend->textSetSelection(peerOf(self), self->_selEnd, self->_selEnd);
    }
}

void CxUiTextField_setCue(_In_ CxUiTextField* self, _In_opt_ strref cue)
{
    if (!live(self))
        return;
    strDup(&self->_cue, cue);
    if (peerOf(self))
        _cxui.backend->textSetCue(peerOf(self), self->_cue);
}

bool CxUiTextField_readOnly(_In_ CxUiTextField* self)
{
    return live(self) && self->_readOnly;
}

void CxUiTextField_setReadOnly(_In_ CxUiTextField* self, bool readOnly)
{
    if (!live(self) || readOnly == self->_readOnly)
        return;
    self->_readOnly = readOnly;
    cxuiWidgetRestyle(self);
}

void CxUiTextField_setMaxLength(_In_ CxUiTextField* self, int32 maxLength)
{
    if (!live(self))
        return;
    self->_maxLength = max(maxLength, 0);
    if (peerOf(self))
        _cxui.backend->textSetMaxLength(peerOf(self), self->_maxLength);
}

int32 CxUiTextField_caret(_In_ CxUiTextField* self)
{
    if (!live(self))
        return 0;
    if (!peerOf(self))
        return self->_selEnd;
    int32 start, end, caret;
    _cxui.backend->textGetSelection(peerOf(self), &start, &end, &caret);
    return caret;
}

void CxUiTextField_setCaret(_In_ CxUiTextField* self, int32 pos)
{
    CxUiTextField_select(self, pos, pos);
}

void CxUiTextField_selection(_In_ CxUiTextField* self, _Out_ int32* start, _Out_ int32* end)
{
    *start = *end = 0;
    if (!live(self))
        return;
    if (peerOf(self)) {
        int32 caret;
        _cxui.backend->textGetSelection(peerOf(self), start, end, &caret);
        return;
    }
    *start = min(self->_selStart, self->_selEnd);
    *end   = max(self->_selStart, self->_selEnd);
}

void CxUiTextField_select(_In_ CxUiTextField* self, int32 start, int32 end)
{
    if (!live(self))
        return;
    int32 len       = textLen(self);
    self->_selStart = clamp(start, 0, len);
    self->_selEnd   = end < 0 ? len : clamp(end, 0, len);
    if (peerOf(self))
        _cxui.backend->textSetSelection(peerOf(self), self->_selStart, self->_selEnd);
}

void CxUiTextField_selectAll(_In_ CxUiTextField* self)
{
    CxUiTextField_select(self, 0, -1);
}

extern uint32 CxUiWidget_eventMask(_In_ CxUiWidget* self);   // parent
#define parent_eventMask() CxUiWidget_eventMask((CxUiWidget*)(self))
uint32 CxUiTextField_eventMask(_In_ CxUiTextField* self)
{
    return parent_eventMask() | (1 << CXUI_EV_Change) | (1 << CXUI_EV_Commit) | (1 << CXUI_EV_Key) |
           (1 << CXUI_EV_Focus);
}

extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiTextField_measure(_In_ CxUiTextField* self)
{
    CxUiFont* f = cxuiWidgetFont(self);
    pushFont(self, f);
    bool framed = frameOf(self) != NULL;
    if (_cxui.backend->textMeasure) {
        CxUiSize n = _cxui.backend->textMeasure(self, f, self->_columns, framed);
        if (n.w > 0)
            return n;
    }

    // Room for the characters and the caret; a field of its own has a border and some padding.
    CxUiFontMetrics m;
    cxuiFontMetrics(f, &m);
    float32 s   = cxuiFontScale(f);
    int32 chrome = framed ? 0 : 4;
    return (CxUiSize){ (int32)ceilf(self->_columns * m.avgCharW / s) + 2 + chrome * 2,
                       (int32)ceilf(m.height / s) + chrome * 2 };
}

extern void CxUiWidget_realizePeer(_In_ CxUiWidget* self, _In_ CxUiPeer* host);   // parent
#define parent_realizePeer(host) CxUiWidget_realizePeer((CxUiWidget*)(self), host)
void CxUiTextField_realizePeer(_In_ CxUiTextField* self, _In_ CxUiPeer* host)
{
    if (peerOf(self) || !_cxui.backend->textCreate)
        return;
    CxUiPeer* p = _cxui.backend->textCreate(self, host, frameOf(self) != NULL);
    if (!p)
        return;
    _cxuiPeerAttach(CxUiWidget(self), p);
    objRelease(&self->_peerFont);
    pushFont(self, cxuiWidgetFont(self));
    _cxui.backend->textSetText(p, self->_text);
    _cxui.backend->textSetCue(p, self->_cue);
    _cxui.backend->textSetMaxLength(p, self->_maxLength);
    _cxui.backend->textSetSelection(p, self->_selStart, self->_selEnd);
    // The platform's own measure can differ from the estimate the field was laid out with.
    if (_cxui.backend->textMeasure && !objDynCast(CxUiTextArea, self)) {
        CxUiSize n = _cxui.backend->textMeasure(self, cxuiWidgetFont(self), self->_columns, frameOf(self) != NULL);
        CxUiSize m = CxUiWidget(self)->_measured;
        if (n.w > 0 && (n.w != m.w || n.h != m.h))
            _cxuiLayoutDirty(CxUiWidget(self));
    }
}

extern void CxUiWidget_placePeer(_In_ CxUiWidget* self, CxUiRect px);   // parent
#define parent_placePeer(px) CxUiWidget_placePeer((CxUiWidget*)(self), px)
void CxUiTextField_placePeer(_In_ CxUiTextField* self, CxUiRect px)
{
    if (peerOf(self))
        _cxui.backend->textSetBounds(peerOf(self), px);
}

extern void CxUiWidget_restyle(_In_ CxUiWidget* self);   // parent
#define parent_restyle() CxUiWidget_restyle((CxUiWidget*)(self))
void CxUiTextField_restyle(_In_ CxUiTextField* self)
{
    CxUiPeer* p = peerOf(self);
    if (!p)
        return;
    bool inactive        = (CxUiWidget(self)->state & CXUI_WS_Inactive) != 0;
    CxUiTextFrame* frame = frameOf(self);
    CxUiPalette* pal     = cxuiWidgetPalette(self);
    CxUiTextColors c     = {
        .text          = cxuiPaletteColor(pal, inactive ? CXUI_PAL_TextDisabled : CXUI_PAL_InputText),
        .bg            = frame ? frameBg(frame) : cxuiPaletteColor(pal, CXUI_PAL_Input),
        .cue           = cxuiPaletteColor(pal, CXUI_PAL_InputCue),
        .selection     = cxuiPaletteColor(pal, CXUI_PAL_Selection),
        .selectionText = cxuiPaletteColor(pal, CXUI_PAL_SelectionText),
    };
    if (!frame) {
        if (_cxui.backend->textSetDarkColors)
            _cxui.backend->textSetDarkColors(p, &c);
        _cxui.backend->textSetState(p, self->_readOnly, !inactive, true);
        return;
    }

    // A framed field is never disabled natively, which would bring the platform's colors.
    _cxui.backend->textSetColors(p, &c);
    _cxui.backend->textSetState(p, self->_readOnly || inactive, true, !inactive);
}

extern bool CxUiWidget_canFocus(_In_ CxUiWidget* self);   // parent
#define parent_canFocus() CxUiWidget_canFocus((CxUiWidget*)(self))
bool CxUiTextField_canFocus(_In_ CxUiTextField* self)
{
    return true;
}

extern CxUiResult CxUiWidget_handleEvent(_In_ CxUiWidget* self, _Inout_ CxUiEvent* ev);   // parent
#define parent_handleEvent(ev) CxUiWidget_handleEvent((CxUiWidget*)(self), ev)
CxUiResult CxUiTextField_handleEvent(_In_ CxUiTextField* self, _Inout_ CxUiEvent* ev)
{
    if (ev->kind == CXUI_EV_Key) {
        // Enter commits, and goes on to the containers, for a default button.
        CxUiKeyEvent* k = (CxUiKeyEvent*)ev;
        if (k->down && !k->repeat && (k->key == CXUI_KEY_Enter || k->key == CXUI_KEY_PadEnter) && !self->_readOnly)
            commit(self);
    } else if (ev->kind == CXUI_EV_Focus) {
        CxUiFocusEvent* f    = (CxUiFocusEvent*)ev;
        CxUiTextFrame* frame = frameOf(self);
        if (frame)
            cxuiWidgetRestyle(frame);
        if (!f->gained && self->_edited && live(self))
            commit(self);
    }
    return CXUI_Default;
}

void CxUiTextField_destroy(_In_ CxUiTextField* self)
{
    // Autogen begins -----
    strDestroy(&self->_text);
    strDestroy(&self->_cue);
    objRelease(&self->_peerFont);
    // Autogen ends -------
}

// ---- CxUiTextArea ----

_objfactory_guaranteed CxUiTextArea* CxUiTextArea_createWith(_In_opt_ const CxUiTextAreaDesc* desc)
{
    CxUiTextArea* self;
    self                        = objInstCreate(CxUiTextArea);
    CxUiTextField* field        = CxUiTextField(self);
    field->_columns             = DEFAULT_AREA_COLUMNS;
    self->_lines                = DEFAULT_AREA_LINES;
    if (desc) {
        strDup(&field->_text, desc->text);
        if (desc->columns > 0)
            field->_columns = desc->columns;
        if (desc->lines > 0)
            self->_lines = desc->lines;
        self->_wrap           = desc->wrap;
        field->_maxLength     = max(desc->maxLength, 0);
        field->_readOnly      = desc->readOnly;
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }
    // It takes what room its container has to give.
    CxUiWidget(self)->_lp.expand |= CXUI_EXPAND_Both;

    objInstInit(self);
    return self;
}

void CxUiTextArea_append(_In_ CxUiTextArea* self, _In_ strref text)
{
    CxUiTextField* field = CxUiTextField(self);
    if (!live(self) || strEmpty(text))
        return;
    strAppend(&field->_text, text);
    if (!peerOf(field))
        return;
    if (_cxui.backend->textAppend) {
        _cxui.backend->textAppend(peerOf(field), text);
    } else {
        int32 start, end, caret;
        _cxui.backend->textGetSelection(peerOf(field), &start, &end, &caret);
        _cxui.backend->textSetText(peerOf(field), field->_text);
        _cxui.backend->textSetSelection(peerOf(field), start, end);
    }
}

void CxUiTextArea_scrollToEnd(_In_ CxUiTextArea* self)
{
    if (!live(self))
        return;
    CxUiPeer* p = peerOf(CxUiTextField(self));
    if (p && _cxui.backend->textScrollToEnd)
        _cxui.backend->textScrollToEnd(p);
    else
        self->_toEnd = true;
}

bool CxUiTextArea_wrap(_In_ CxUiTextArea* self)
{
    return live(self) && self->_wrap;
}

// Its columns across and lines down, with a scroll bar and the border of an area of its own
extern CxUiSize CxUiTextField_measure(_In_ CxUiTextField* self);   // parent
#undef parent_measure
#define parent_measure() CxUiTextField_measure((CxUiTextField*)(self))
CxUiSize CxUiTextArea_measure(_In_ CxUiTextArea* self)
{
    CxUiTextField* field = CxUiTextField(self);
    CxUiFont* f          = cxuiWidgetFont(self);
    pushFont(field, f);
    bool framed = frameOf(field) != NULL;
    if (_cxui.backend->textAreaMeasure) {
        CxUiSize n = _cxui.backend->textAreaMeasure(self, f, field->_columns, self->_lines, framed);
        if (n.w > 0)
            return n;
    }
    CxUiFontMetrics m;
    cxuiFontMetrics(f, &m);
    float32 s    = cxuiFontScale(f);
    int32 chrome = framed ? 0 : 4;
    return (CxUiSize){ (int32)ceilf(field->_columns * m.avgCharW / s) + 16 + chrome * 2,
                       (int32)ceilf(self->_lines * m.height / s) + chrome * 2 };
}

extern void CxUiTextField_realizePeer(_In_ CxUiTextField* self, _In_ CxUiPeer* host);   // parent
#undef parent_realizePeer
#define parent_realizePeer(host) CxUiTextField_realizePeer((CxUiTextField*)(self), host)
void CxUiTextArea_realizePeer(_In_ CxUiTextArea* self, _In_ CxUiPeer* host)
{
    parent_realizePeer(host);
    CxUiPeer* p = peerOf(CxUiTextField(self));
    if (p && _cxui.backend->textAreaMeasure) {
        CxUiSize n = CxUiTextArea_measure(self);
        CxUiSize m = CxUiWidget(self)->_measured;
        if (n.w != m.w || n.h != m.h)
            _cxuiLayoutDirty(CxUiWidget(self));
    }
    if (p && self->_toEnd && _cxui.backend->textScrollToEnd) {
        self->_toEnd = false;
        _cxui.backend->textScrollToEnd(p);
    }
}

// Enter starts a line natively, so it neither commits nor goes on to a default button; leaving
// still commits.
extern CxUiResult CxUiTextField_handleEvent(_In_ CxUiTextField* self, _Inout_ CxUiEvent* ev);   // parent
#undef parent_handleEvent
#define parent_handleEvent(ev) CxUiTextField_handleEvent((CxUiTextField*)(self), ev)
CxUiResult CxUiTextArea_handleEvent(_In_ CxUiTextArea* self, _Inout_ CxUiEvent* ev)
{
    if (ev->kind == CXUI_EV_Key)
        return CXUI_Default;
    return parent_handleEvent(ev);
}

_Use_decl_annotations_
bool _cxuiTakesEnter(CxUiWidget* w)
{
    CxUiTextArea* a = objDynCast(CxUiTextArea, w);
    return a && !CxUiTextField(a)->_readOnly && !(w->state & CXUI_WS_Inactive);
}

// ---- CxUiTextFrame ----

_objfactory_guaranteed CxUiTextFrame* CxUiTextFrame_createWith(_In_ const CxUiTextFrameDesc* desc)
{
    relAssertMsg(desc && desc->field, "a text frame needs a field");
    CxUiTextFrame* self;
    self              = objInstCreate(CxUiTextFrame);
    CxUiBox* box      = CxUiBox(self);
    CxUiBoxBase* base = CxUiBoxBase(self);
    CxUiPanel* panel  = CxUiPanel(self);
    // The field fills the width and sits in the middle of any extra height.
    box->_orient     = CXUI_ORIENT_V;
    box->_align[0]   = CXUI_ALIGN_Fill;
    box->_align[1]   = CXUI_ALIGN_Center;
    CxUiSize pad     = { desc->padding.w ? max(desc->padding.w, 0) : 3, desc->padding.h ? max(desc->padding.h, 0) : 2 };
    base->_margin    = (CxUiEdges){ pad.w, pad.h, pad.w, pad.h };
    panel->_bgRole   = CXUI_PAL_Input;
    panel->_border   = desc->noBorder ? 0 : desc->border > 0 ? desc->border : 1;
    base->_inset     = panel->_border;
    CxUiWidget(self)->_lp = desc->lp;
    strDup(&CxUiWidget(self)->name, desc->name);

    objInstInit(self);
    self->_field = objAcquire(desc->field);
    cxuiContainerAppend(self, desc->field);
    return self;
}

CxUiTextField* CxUiTextFrame_field(_In_ CxUiTextFrame* self)
{
    return live(self) ? self->_field : NULL;
}

void CxUiTextFrame_setBad(_In_ CxUiTextFrame* self, bool bad)
{
    if (!live(self) || bad == self->_bad)
        return;
    self->_bad = bad;
    _cxuiRestyleTree(CxUiWidget(self));
}

extern void CxUiPanel_restyle(_In_ CxUiPanel* self);   // parent
#undef parent_restyle
#define parent_restyle() CxUiPanel_restyle((CxUiPanel*)(self))
void CxUiTextFrame_restyle(_In_ CxUiTextFrame* self)
{
    CxUiPeer* p = CxUiWidget(self)->peer;
    if (!p)
        return;
    CxUiPalette* pal = cxuiWidgetPalette(self);
    CxUiWindow* win  = _cxuiWindowOf(CxUiWidget(self));
    bool focused     = win && self->_field && win->_focus == CxUiWidget(self->_field);
    int32 border     = CxUiPanel(self)->_border;
    int32 borderPx   = border > 0 ? max(cxuiPx(self, border), 1) : 0;
    _cxui.backend->panelSetColors(p, frameBg(self), cxuiPaletteColor(pal, focused ? CXUI_PAL_Focus : CXUI_PAL_InputBorder),
                                  borderPx);
}

void CxUiTextFrame_destroy(_In_ CxUiTextFrame* self)
{
    // Autogen begins -----
    objRelease(&self->_field);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "core/cxuitext.auto.inc"
// clang-format on
// Autogen ends -------
