// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "core/cxuidrawn.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cxui_private.h"

#include <math.h>

static bool live(void* w)
{
    return _cxuiLive((CxUiWidget*)w);
}

static void repaint(void* w)
{
    CxUiCanvas* cv = w;
    if (CxUiWidget(cv)->peer)
        CxUiCanvas_invalidateAll(cv);
}

// The size text needs in the widget's font, in DIPs
static CxUiSize textSize(CxUiWidget* w, strref text)
{
    CxUiFont* f = cxuiWidgetFont(w);
    CxUiSize px = cxuiFontMeasure(f, text);
    float32 s   = cxuiFontScale(f);
    return (CxUiSize){ (int32)ceilf(px.w / s), (int32)ceilf(px.h / s) };
}

static CxUiColor pal(void* w, uint32 role)
{
    return cxuiPaletteColor(cxuiWidgetPalette((CxUiWidget*)w), role);
}

static CxUiColor orPal(void* w, CxUiColor own, uint32 role)
{
    return own.bits ? own : pal(w, role);
}

static bool enabledNow(void* w)
{
    return cxuiWidgetEffectiveEnabled((CxUiWidget*)w);
}

// Whether a mouse event is over the widget. While a button is held the mouse is captured and the
// widget hears of it leaving only once the button is let go, so a release is checked by position.
static bool inside(void* w, CxUiMouseEvent* m)
{
    CxUiSize sz = { 0 };
    cxuiCanvasSize((CxUiCanvas*)w, &sz);
    return m->x >= 0 && m->y >= 0 && m->x < sz.w && m->y < sz.h;
}

// The whole payload a handler reads, zeroed
static void raiseEvent(void* w, uint32 kind)
{
    union {
        CxUiEvent base;
        CxUiActivateEvent activate;
        CxUiChangeEvent change;
    } ev         = { 0 };
    ev.base.kind = kind;
    _cxuiDispatch((CxUiWidget*)w, &ev.base);
}

// ---- CxUiDrawn ----

_objinit_guaranteed bool CxUiDrawn_init(_In_ CxUiDrawn* self)
{
    CxUiCanvas(self)->_units     = CXUI_Units_Dip;
    CxUiWidget(self)->_lp.expand = CXUI_EXPAND_None;
    // Autogen begins -----
    return true;
    // Autogen ends -------
}

extern bool CxUiCanvas_canFocus(_In_ CxUiCanvas* self);   // parent
#define parent_canFocus() CxUiCanvas_canFocus((CxUiCanvas*)(self))
bool CxUiDrawn_canFocus(_In_ CxUiDrawn* self)
{
    return self->_canFocus;
}

// Hover, press and focus, which every drawn widget shows; the subclass carries on after.
extern CxUiResult CxUiWidget_handleEvent(_In_ CxUiWidget* self, _Inout_ CxUiEvent* ev);   // parent
#define parent_handleEvent(ev) CxUiWidget_handleEvent((CxUiWidget*)(self), ev)
CxUiResult CxUiDrawn_handleEvent(_In_ CxUiDrawn* self, _Inout_ CxUiEvent* ev)
{
    if (ev->kind == CXUI_EV_Mouse) {
        CxUiMouseEvent* m = (CxUiMouseEvent*)ev;
        switch (m->action) {
        case CXUI_MOUSE_Enter:
            self->_hot = true;
            break;
        case CXUI_MOUSE_Leave:
            self->_hot = false;
            break;
        // A quick second click is a click too.
        case CXUI_MOUSE_Down:
        case CXUI_MOUSE_DoubleClick:
            if (m->button != CXUI_BTN_Left)
                return CXUI_Default;
            self->_down = true;
            if (self->_canFocus)
                cxuiFocus(self);
            break;
        case CXUI_MOUSE_Up:
            if (m->button != CXUI_BTN_Left)
                return CXUI_Default;
            self->_down = false;
            break;
        case CXUI_MOUSE_Move:
            // While held, it shows pressed only with the pointer over it.
            if (self->_down && inside(self, m) != self->_hot) {
                self->_hot = !self->_hot;
                break;
            }
            return CXUI_Default;
        default:
            if (m->flags & CXUI_MOUSE_CaptureLost)
                self->_down = false;
            return CXUI_Default;
        }
        repaint(self);
    } else if (ev->kind == CXUI_EV_Focus) {
        self->_focused = ((CxUiFocusEvent*)ev)->gained;
        repaint(self);
    }
    return CXUI_Default;
}

// ---- CxUiDLabel ----

_objfactory_guaranteed CxUiDLabel* CxUiDLabel_createWith(_In_opt_ const CxUiDLabelDesc* desc)
{
    CxUiDLabel* self;
    self = objInstCreate(CxUiDLabel);

    objInstInit(self);
    if (desc) {
        strDup(&self->_text, desc->text);
        self->_align    = desc->align;
        self->_ellipsis = desc->ellipsis;
        self->_padding  = (CxUiSize){ max(desc->padding.w, 0), max(desc->padding.h, 0) };
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }
    return self;
}

strref CxUiDLabel_text(_In_ CxUiDLabel* self)
{
    return live(self) ? self->_text : NULL;
}

void CxUiDLabel_setText(_In_ CxUiDLabel* self, _In_ strref text)
{
    if (!live(self) || strEq(text, self->_text))
        return;
    strDup(&self->_text, text);
    _cxuiLayoutDirty(CxUiWidget(self));
    repaint(self);
}

void CxUiDLabel_setColor(_In_ CxUiDLabel* self, CxUiColor c)
{
    if (!live(self))
        return;
    self->_color = c;
    repaint(self);
}

extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiDLabel_measure(_In_ CxUiDLabel* self)
{
    CxUiSize t = textSize(CxUiWidget(self), self->_text);
    return (CxUiSize){ t.w + self->_padding.w * 2, t.h + self->_padding.h * 2 };
}

// The longest start of a single line that fits in width pixels with an ellipsis after it
static void ellipsize(string* out, CxUiFont* f, strref text, int32 width)
{
    static const char ell[] = "\xE2\x80\xA6";
    strDup(out, text);
    if (cxuiFontAdvance(f, text) <= width)
        return;
    int32 len = strLen(text);
    while (len > 0) {
        // Back to the start of a UTF-8 sequence
        do
            len--;
        while (len > 0 && (strGetChar(text, len) & 0xC0) == 0x80);
        strSubStr(out, text, 0, len);
        strAppend(out, (strref)ell);
        if (cxuiFontAdvance(f, *out) <= width)
            return;
    }
    strDup(out, (strref)ell);
}

static void labelPaint(CxUiDLabel* self, CxUiDraw* d)
{
    CxUiSize sz = cxuiDrawSize(d);
    cxuiDrawFill(d, 0, 0, sz.w, sz.h, _cxuiBackgroundOf(CxUiWidget(self)));
    CxUiFont* f  = cxuiWidgetFont(self);
    CxUiRect box = { self->_padding.w, self->_padding.h, sz.w - self->_padding.w * 2, sz.h - self->_padding.h * 2 };
    CxUiColor c  = enabledNow(self) ? orPal(self, self->_color, CXUI_PAL_Text) : pal(self, CXUI_PAL_TextDisabled);
    if (self->_ellipsis && strFindChar(self->_text, 0, '\n') < 0) {
        string shown = 0;
        ellipsize(&shown, f, self->_text, (int32)(box.w * cxuiFontScale(f)));
        cxuiDrawTextBox(d, f, box, shown, self->_align, c);
        strDestroy(&shown);
    } else {
        cxuiDrawTextBox(d, f, box, self->_text, self->_align, c);
    }
}

extern CxUiResult CxUiDrawn_handleEvent(_In_ CxUiDrawn* self, _Inout_ CxUiEvent* ev);   // parent
#undef parent_handleEvent
#define parent_handleEvent(ev) CxUiDrawn_handleEvent((CxUiDrawn*)(self), ev)
CxUiResult CxUiDLabel_handleEvent(_In_ CxUiDLabel* self, _Inout_ CxUiEvent* ev)
{
    if (ev->kind == CXUI_EV_Paint) {
        labelPaint(self, ((CxUiPaintEvent*)ev)->draw);
        return CXUI_Handled;
    }
    return CxUiDrawn_handleEvent(CxUiDrawn(self), ev);
}

void CxUiDLabel_destroy(_In_ CxUiDLabel* self)
{
    // Autogen begins -----
    strDestroy(&self->_text);
    // Autogen ends -------
}

// ---- CxUiDGroup ----

_objfactory_guaranteed CxUiDGroup* CxUiDGroup_create()
{
    CxUiDGroup* self;
    self = objInstCreate(CxUiDGroup);

    objInstInit(self);
    return self;
}

static CxUiDButton** members(CxUiDGroup* g)
{
    return (CxUiDButton**)g->_members;
}

static void groupAdd(CxUiDGroup* g, CxUiDButton* b)
{
    if (g->_count == g->_cap) {
        g->_cap = g->_cap ? g->_cap * 2 : 4;
        xaResize(&g->_members, g->_cap * sizeof(CxUiDButton*));
    }
    members(g)[g->_count++] = b;
}

static void groupRemove(CxUiDGroup* g, CxUiDButton* b)
{
    for (int32 i = 0; i < g->_count; i++) {
        if (members(g)[i] == b) {
            memmove(&members(g)[i], &members(g)[i + 1], (g->_count - i - 1) * sizeof(CxUiDButton*));
            g->_count--;
            return;
        }
    }
}

CxUiDButton* CxUiDGroup_value(_In_ CxUiDGroup* self)
{
    for (int32 i = 0; i < self->_count; i++) {
        if (members(self)[i]->_value)
            return members(self)[i];
    }
    return NULL;
}

void CxUiDGroup_destroy(_In_ CxUiDGroup* self)
{
    xaFree(self->_members);
}

// ---- CxUiDButton ----

_objfactory_guaranteed CxUiDButton* CxUiDButton_createWith(_In_opt_ const CxUiDButtonDesc* desc)
{
    CxUiDButton* self;
    self           = objInstCreate(CxUiDButton);
    self->_padding = (CxUiSize){ 8, 4 };

    objInstInit(self);
    if (desc) {
        strDup(&self->_title, desc->title);
        if (desc->image)
            self->_image = objAcquire(desc->image);
        self->_toggle = desc->toggle;
        self->_value  = desc->toggle && desc->value;
        if (desc->padding.w || desc->padding.h)
            self->_padding = (CxUiSize){ max(desc->padding.w, 0), max(desc->padding.h, 0) };
        CxUiDrawn(self)->_canFocus = desc->canFocus;
        CxUiWidget(self)->_lp      = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
        if (desc->group)
            CxUiDButton_setGroup(self, desc->group);
    }
    return self;
}

strref CxUiDButton_title(_In_ CxUiDButton* self)
{
    return live(self) ? self->_title : NULL;
}

void CxUiDButton_setTitle(_In_ CxUiDButton* self, _In_ strref title)
{
    if (!live(self) || strEq(title, self->_title))
        return;
    strDup(&self->_title, title);
    _cxuiLayoutDirty(CxUiWidget(self));
    repaint(self);
}

bool CxUiDButton_value(_In_ CxUiDButton* self)
{
    return live(self) && self->_value;
}

static void buttonSet(CxUiDButton* self, bool value)
{
    if (value && self->_group) {
        CxUiDButton* other = CxUiDGroup_value(self->_group);
        if (other && other != self) {
            other->_value = false;
            repaint(other);
        }
    }
    self->_value = value;
    repaint(self);
}

void CxUiDButton_setValue(_In_ CxUiDButton* self, bool value)
{
    if (live(self) && self->_toggle && value != self->_value)
        buttonSet(self, value);
}

void CxUiDButton_setGroup(_In_ CxUiDButton* self, _In_opt_ CxUiDGroup* group)
{
    if (!live(self) || group == self->_group)
        return;
    if (self->_group)
        groupRemove(self->_group, self);
    objRelease(&self->_group);
    if (!group)
        return;
    self->_group = objAcquire(group);
    groupAdd(group, self);
    if (self->_value)
        buttonSet(self, true);
}

void CxUiDButton_setColor(_In_ CxUiDButton* self, uint32 which, CxUiColor c)
{
    if (!live(self) || which >= CXUI_DBC_Count)
        return;
    self->_colors.c[which] = c;
    repaint(self);
}

extern uint32 CxUiCanvas_eventMask(_In_ CxUiCanvas* self);   // parent
#define parent_eventMask() CxUiCanvas_eventMask((CxUiCanvas*)(self))
uint32 CxUiDButton_eventMask(_In_ CxUiDButton* self)
{
    return parent_eventMask() | (1 << CXUI_EV_Activate) | (1 << CXUI_EV_Change);
}

// Image sizes are taken as DIPs: icons are made for scale 1.
static CxUiSize imageSize(CxUiImage* img)
{
    // Not a ternary: MSVC 19.16 for x86 drops `p ? (T){ p->x } : ...`'s test and reads through NULL
    CxUiSize sz = { 0 };
    if (img) {
        sz.w = img->_rect.w;
        sz.h = img->_rect.h;
    }
    return sz;
}

extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#undef parent_measure
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiDButton_measure(_In_ CxUiDButton* self)
{
    string shown = 0;
    _cxuiMnemonicStrip(&shown, self->_title);
    CxUiSize t = strEmpty(shown) ? (CxUiSize){ 0, textSize(CxUiWidget(self), shown).h } : textSize(CxUiWidget(self), shown);
    strDestroy(&shown);
    CxUiSize im = imageSize(self->_image);
    int32 w     = t.w + im.w + (im.w && t.w ? 4 : 0);
    return (CxUiSize){ w + self->_padding.w * 2, max(t.h, im.h) + self->_padding.h * 2 };
}

static CxUiColor own(CxUiDButton* self, uint32 which)
{
    return self->_colors.c[which];
}

static void buttonPaint(CxUiDButton* self, CxUiDraw* d)
{
    CxUiDrawn* dn = CxUiDrawn(self);
    CxUiSize sz   = cxuiDrawSize(d);
    bool on       = enabledNow(self);
    bool pressed  = on && dn->_down && dn->_hot;   // held, and over it as far as it knows
    bool checked  = self->_toggle && self->_value;
    CxUiColor bg = own(self, CXUI_DBC_Bg).bits ? own(self, CXUI_DBC_Bg) : _cxuiBackgroundOf(CxUiWidget(self));
    CxUiColor border = orPal(self, own(self, CXUI_DBC_Border), CXUI_PAL_Border);
    CxUiColor text   = orPal(self, own(self, CXUI_DBC_Text), CXUI_PAL_Text);
    if (!on) {
        text = pal(self, CXUI_PAL_TextDisabled);
        if (checked)
            bg = orPal(self, own(self, CXUI_DBC_Checked), CXUI_PAL_Selection);
    } else if (pressed) {
        bg     = orPal(self, own(self, CXUI_DBC_Pressed), CXUI_PAL_Pressed);
        border = orPal(self, own(self, CXUI_DBC_BorderPressed), CXUI_PAL_Accent);
        text   = orPal(self, own(self, CXUI_DBC_TextPressed), CXUI_PAL_Text);
    } else if (checked) {
        bg     = orPal(self, own(self, CXUI_DBC_Checked), CXUI_PAL_Selection);
        border = orPal(self, own(self, CXUI_DBC_BorderPressed), CXUI_PAL_Accent);
        text   = orPal(self, own(self, CXUI_DBC_TextPressed), CXUI_PAL_SelectionText);
    } else if (dn->_hot) {
        bg     = orPal(self, own(self, CXUI_DBC_Hover), CXUI_PAL_Hover);
        border = orPal(self, own(self, CXUI_DBC_BorderHover), CXUI_PAL_Accent);
        text   = own(self, CXUI_DBC_TextHover).bits ? own(self, CXUI_DBC_TextHover) : text;
    }

    cxuiDrawFill(d, 0, 0, sz.w, sz.h, bg);
    cxuiDrawFrame(d, 0, 0, sz.w, sz.h, 1, border);
    if (dn->_canFocus && dn->_focused)
        cxuiDrawFrame(d, 2, 2, sz.w - 4, sz.h - 4, 1, pal(self, CXUI_PAL_Focus));

    string shown = 0;
    _cxuiMnemonicStrip(&shown, self->_title);
    CxUiFont* f  = cxuiWidgetFont(self);
    CxUiSize im  = imageSize(self->_image);
    CxUiSize t   = strEmpty(shown) ? (CxUiSize){ 0 } : textSize(CxUiWidget(self), shown);
    int32 gap    = im.w && t.w ? 4 : 0;
    int32 x      = (sz.w - (im.w + gap + t.w)) / 2;
    if (self->_image)
        cxuiDrawImage(d, self->_image, NULL, (CxUiRect){ x, (sz.h - im.h) / 2, im.w, im.h });
    if (t.w)
        cxuiDrawTextBox(d, f, (CxUiRect){ x + im.w + gap, 0, t.w, sz.h }, shown, CXUI_TEXT_Left | CXUI_TEXT_Middle,
                        text);
    strDestroy(&shown);
}

// A click: a toggle changes (one in a group only goes down), then the button is activated.
// Only from the button's own event handling, so the dispatch that brought the event holds it.
static void buttonClick(CxUiDButton* self)
{
    if (self->_toggle && !(self->_group && self->_value)) {
        buttonSet(self, !self->_value);
        raiseEvent(self, CXUI_EV_Change);
    }
    if (live(self))
        raiseEvent(self, CXUI_EV_Activate);
}

extern CxUiResult CxUiDrawn_handleEvent(_In_ CxUiDrawn* self, _Inout_ CxUiEvent* ev);   // parent
#undef parent_handleEvent
#define parent_handleEvent(ev) CxUiDrawn_handleEvent((CxUiDrawn*)(self), ev)
CxUiResult CxUiDButton_handleEvent(_In_ CxUiDButton* self, _Inout_ CxUiEvent* ev)
{
    CxUiDrawn* dn = CxUiDrawn(self);
    switch (ev->kind) {
    case CXUI_EV_Paint:
        buttonPaint(self, ((CxUiPaintEvent*)ev)->draw);
        return CXUI_Handled;
    case CXUI_EV_Mouse: {
        CxUiMouseEvent* m = (CxUiMouseEvent*)ev;
        bool clicked      = m->action == CXUI_MOUSE_Up && m->button == CXUI_BTN_Left && dn->_down && inside(self, m);
        CxUiDrawn_handleEvent(dn, ev);
        if (clicked)
            buttonClick(self);
        return CXUI_Handled;
    }
    case CXUI_EV_Key: {
        CxUiKeyEvent* k = (CxUiKeyEvent*)ev;
        if (dn->_canFocus && k->down && !k->repeat && (k->key == CXUI_KEY_Space || k->key == CXUI_KEY_Enter)) {
            buttonClick(self);
            return CXUI_Handled;
        }
        return CXUI_Default;
    }
    default:
        return CxUiDrawn_handleEvent(dn, ev);
    }
}

void CxUiDButton_destroy(_In_ CxUiDButton* self)
{
    if (self->_group)
        groupRemove(self->_group, self);
    // Autogen begins -----
    strDestroy(&self->_title);
    objRelease(&self->_image);
    objRelease(&self->_group);
    // Autogen ends -------
}

// ---- CxUiDTabs ----

_objfactory_guaranteed CxUiDTabs* CxUiDTabs_createWith(_In_opt_ const CxUiDTabsDesc* desc)
{
    CxUiDTabs* self;
    self           = objInstCreate(CxUiDTabs);
    self->_padding = (CxUiSize){ 10, 3 };
    self->_gap     = 3;
    self->_hotTab  = -1;

    objInstInit(self);
    if (desc) {
        if (desc->zbox)
            self->_zbox = objAcquire(desc->zbox);
        if (desc->padding.w || desc->padding.h)
            self->_padding = (CxUiSize){ max(desc->padding.w, 0), max(desc->padding.h, 0) };
        if (desc->gap)
            self->_gap = max(desc->gap, 0);
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }
    return self;
}

_objinit_guaranteed bool CxUiDTabs_init(_In_ CxUiDTabs* self)
{
    // Autogen begins -----
    saInit(&self->_titles, string, 1);
    return true;
    // Autogen ends -------
}

void CxUiDTabs_add(_In_ CxUiDTabs* self, _In_ strref title)
{
    if (!live(self))
        return;
    saPush(&self->_titles, strref, title);
    _cxuiLayoutDirty(CxUiWidget(self));
    repaint(self);
}

void CxUiDTabs_setTitle(_In_ CxUiDTabs* self, int32 index, _In_ strref title)
{
    if (!live(self) || index < 0 || index >= saSize(self->_titles))
        return;
    strDup(&self->_titles.a[index], title);
    _cxuiLayoutDirty(CxUiWidget(self));
    repaint(self);
}

int32 CxUiDTabs_count(_In_ CxUiDTabs* self)
{
    return live(self) ? saSize(self->_titles) : 0;
}

int32 CxUiDTabs_value(_In_ CxUiDTabs* self)
{
    return live(self) && saSize(self->_titles) > 0 ? self->_value : -1;
}

void CxUiDTabs_setValue(_In_ CxUiDTabs* self, int32 index)
{
    if (!live(self) || index < 0 || index >= saSize(self->_titles))
        return;
    self->_value = index;
    if (self->_zbox)
        cxuiZBoxSetActive(self->_zbox, index);
    repaint(self);
}

extern uint32 CxUiCanvas_eventMask(_In_ CxUiCanvas* self);   // parent
#undef parent_eventMask
#define parent_eventMask() CxUiCanvas_eventMask((CxUiCanvas*)(self))
uint32 CxUiDTabs_eventMask(_In_ CxUiDTabs* self)
{
    return CxUiCanvas_eventMask(CxUiCanvas(self)) | (1 << CXUI_EV_Change);
}

// Where tab i starts and how wide it is, in DIPs
static int32 tabWidth(CxUiDTabs* self, int32 i)
{
    string shown = 0;
    _cxuiMnemonicStrip(&shown, self->_titles.a[i]);
    int32 w = textSize(CxUiWidget(self), shown).w + self->_padding.w * 2;
    strDestroy(&shown);
    return w;
}

static int32 tabAt(CxUiDTabs* self, int32 x)
{
    for (int32 i = 0, at = 0; i < saSize(self->_titles); i++) {
        int32 w = tabWidth(self, i);
        if (x >= at && x < at + w)
            return i;
        at += w + self->_gap;
    }
    return -1;
}

extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#undef parent_measure
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiDTabs_measure(_In_ CxUiDTabs* self)
{
    int32 n = saSize(self->_titles), w = 0;
    for (int32 i = 0; i < n; i++)
        w += tabWidth(self, i);
    w += n > 0 ? self->_gap * (n - 1) : 0;
    return (CxUiSize){ w, textSize(CxUiWidget(self), _S"").h + self->_padding.h * 2 };
}

static void tabsPaint(CxUiDTabs* self, CxUiDraw* d)
{
    CxUiSize sz = cxuiDrawSize(d);
    bool on     = enabledNow(self);
    cxuiDrawFill(d, 0, 0, sz.w, sz.h, _cxuiBackgroundOf(CxUiWidget(self)));
    CxUiFont* f  = cxuiWidgetFont(self);
    string shown = 0;
    for (int32 i = 0, x = 0; i < saSize(self->_titles); i++) {
        int32 w       = tabWidth(self, i);
        bool picked   = i == self->_value;
        bool hot      = on && i == self->_hotTab;
        CxUiColor bg  = picked ? pal(self, CXUI_PAL_Selection) : hot ? pal(self, CXUI_PAL_Hover) : _cxuiBackgroundOf(CxUiWidget(self));
        CxUiColor txt = !on ? pal(self, CXUI_PAL_TextDisabled) : picked ? pal(self, CXUI_PAL_SelectionText) : pal(self, CXUI_PAL_Text);
        cxuiDrawFill(d, x, 0, w, sz.h, bg);
        cxuiDrawFrame(d, x, 0, w, sz.h, 1, pal(self, picked || hot ? CXUI_PAL_Accent : CXUI_PAL_Border));
        if (picked)
            cxuiDrawFill(d, x, sz.h - 2, w, 2, pal(self, CXUI_PAL_Accent));
        _cxuiMnemonicStrip(&shown, self->_titles.a[i]);
        cxuiDrawTextBox(d, f, (CxUiRect){ x, 0, w, sz.h }, shown, CXUI_TEXT_Center | CXUI_TEXT_Middle, txt);
        x += w + self->_gap;
    }
    strDestroy(&shown);
}

extern CxUiResult CxUiDrawn_handleEvent(_In_ CxUiDrawn* self, _Inout_ CxUiEvent* ev);   // parent
#undef parent_handleEvent
#define parent_handleEvent(ev) CxUiDrawn_handleEvent((CxUiDrawn*)(self), ev)
CxUiResult CxUiDTabs_handleEvent(_In_ CxUiDTabs* self, _Inout_ CxUiEvent* ev)
{
    if (ev->kind == CXUI_EV_Paint) {
        tabsPaint(self, ((CxUiPaintEvent*)ev)->draw);
        return CXUI_Handled;
    }
    if (ev->kind != CXUI_EV_Mouse)
        return CxUiDrawn_handleEvent(CxUiDrawn(self), ev);

    CxUiMouseEvent* m = (CxUiMouseEvent*)ev;
    int32 hit         = m->action == CXUI_MOUSE_Leave || !inside(self, m) ? -1 : tabAt(self, m->x);
    bool click        = m->action == CXUI_MOUSE_Up && m->button == CXUI_BTN_Left && CxUiDrawn(self)->_down && hit >= 0;
    CxUiDrawn_handleEvent(CxUiDrawn(self), ev);
    if (hit != self->_hotTab) {
        self->_hotTab = hit;
        repaint(self);
    }
    if (click && hit != self->_value) {
        // The page is shown first, but it is created on screen only in the layout pass after the
        // handler, so the handler can still build it.
        CxUiDTabs_setValue(self, hit);
        raiseEvent(self, CXUI_EV_Change);
    }
    return CXUI_Handled;
}

void CxUiDTabs_destroy(_In_ CxUiDTabs* self)
{
    // Autogen begins -----
    saDestroy(&self->_titles);
    objRelease(&self->_zbox);
    // Autogen ends -------
}

// ---- CxUiDScrollBar ----

#define SB_MIN_THUMB    16   // DIPs
#define SB_REPEAT_MS    50
#define SB_REPEAT_SKIP  5    // ticks before holding the track repeats

_objfactory_guaranteed CxUiDScrollBar* CxUiDScrollBar_createWith(_In_opt_ const CxUiDScrollBarDesc* desc)
{
    CxUiDScrollBar* self;
    self             = objInstCreate(CxUiDScrollBar);
    self->_step      = 1;
    self->_thickness = 12;

    objInstInit(self);
    if (desc) {
        self->_orient   = desc->orient == CXUI_ORIENT_V ? CXUI_ORIENT_V : CXUI_ORIENT_H;
        self->_range    = max(desc->range, 0);
        self->_page     = max(desc->page, 0);
        self->_step     = desc->step > 0 ? desc->step : 1;
        self->_autoHide = desc->autoHide;
        if (desc->thickness > 0)
            self->_thickness = desc->thickness;
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
        self->_pos = desc->pos;
    }
    if (!CxUiWidget(self)->_lp.expand)
        CxUiWidget(self)->_lp.expand = self->_orient == CXUI_ORIENT_V ? CXUI_EXPAND_V : CXUI_EXPAND_H;
    CxUiDScrollBar_setRange(self, self->_range, self->_page);
    return self;
}

static int64 maxPos(CxUiDScrollBar* self)
{
    return max(self->_range - self->_page, 0);
}

static void autoHide(CxUiDScrollBar* self)
{
    if (self->_autoHide)
        cxuiWidgetSetVisible(self, self->_page < self->_range);
}

void CxUiDScrollBar_setRange(_In_ CxUiDScrollBar* self, int64 range, int64 page)
{
    if (!live(self))
        return;
    self->_range = max(range, 0);
    self->_page  = clamp(page, 0, self->_range);
    self->_pos   = clamp(self->_pos, 0, maxPos(self));
    autoHide(self);
    repaint(self);
}

int64 CxUiDScrollBar_pos(_In_ CxUiDScrollBar* self)
{
    return live(self) ? self->_pos : 0;
}

void CxUiDScrollBar_setPos(_In_ CxUiDScrollBar* self, int64 pos)
{
    pos = clamp(pos, 0, maxPos(self));
    if (!live(self) || pos == self->_pos)
        return;
    self->_pos = pos;
    repaint(self);
}

void CxUiDScrollBar_setStep(_In_ CxUiDScrollBar* self, int64 step)
{
    if (live(self))
        self->_step = max(step, 1);
}

extern uint32 CxUiCanvas_eventMask(_In_ CxUiCanvas* self);   // parent
#undef parent_eventMask
#define parent_eventMask() CxUiCanvas_eventMask((CxUiCanvas*)(self))
uint32 CxUiDScrollBar_eventMask(_In_ CxUiDScrollBar* self)
{
    return CxUiCanvas_eventMask(CxUiCanvas(self)) | (1 << CXUI_EV_Scroll);
}

extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#undef parent_measure
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiDScrollBar_measure(_In_ CxUiDScrollBar* self)
{
    int32 len = SB_MIN_THUMB * 2;
    return self->_orient == CXUI_ORIENT_V ? (CxUiSize){ self->_thickness, len } : (CxUiSize){ len, self->_thickness };
}

// The bar's length, and the thumb's start and length, in DIPs
typedef struct SbGeom {
    float32 len, thumbAt, thumbLen;
} SbGeom;

static SbGeom sbGeom(CxUiDScrollBar* self)
{
    CxUiSize sz = { 0 };
    cxuiCanvasSize(self, &sz);
    SbGeom g = { (float32)(self->_orient == CXUI_ORIENT_V ? sz.h : sz.w) };
    if (self->_range <= 0 || self->_page >= self->_range) {
        g.thumbLen = g.len;
        return g;
    }
    g.thumbLen = min(max(g.len * (float32)self->_page / (float32)self->_range, (float32)SB_MIN_THUMB), g.len);
    g.thumbAt  = (g.len - g.thumbLen) * (float32)self->_pos / (float32)maxPos(self);
    return g;
}

static float32 sbAlong(CxUiDScrollBar* self, CxUiMouseEvent* m)
{
    return (float32)(self->_orient == CXUI_ORIENT_V ? m->y : m->x);
}

// Moves the bar for the user, and says so; a grab or a release is reported even without a move.
static void sbScroll(CxUiDScrollBar* self, int64 pos, uint32 reason)
{
    pos = clamp(pos, 0, maxPos(self));
    if (pos == self->_pos && reason != CXUI_SCROLL_ThumbGrabbed && reason != CXUI_SCROLL_ThumbReleased)
        return;
    self->_pos = pos;
    repaint(self);
    CxUiScrollEvent ev = { .base.kind = CXUI_EV_Scroll, .pos = pos, .reason = reason };
    CxUiWidget* target = objAcquire(CxUiWidget(self));
    _cxuiDispatch(target, &ev.base);
    objRelease(&target);
}

// One page toward the pointer, unless the thumb has reached it
static void sbPage(CxUiDScrollBar* self)
{
    SbGeom g = sbGeom(self);
    if ((self->_paging < 0 && self->_pageAt >= g.thumbAt) || (self->_paging > 0 && self->_pageAt < g.thumbAt + g.thumbLen))
        return;
    sbScroll(self, self->_pos + self->_paging * max(self->_page, 1), CXUI_SCROLL_Page);
}

static CxUiResult sbRepeat(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    ObjInst_WeakRef* wref = NULL;
    if (!stvlNext(cvars, weakref, &wref))
        return CXUI_Default;
    CxUiDScrollBar* self = objAcquireFromWeakDyn(CxUiDScrollBar, wref);
    if (!self)
        return CXUI_Default;
    if (live(self) && self->_paging && ++self->_repeatTicks > SB_REPEAT_SKIP)
        sbPage(self);
    objRelease(&self);
    return CXUI_Default;
}

static void sbStopPaging(CxUiDScrollBar* self)
{
    self->_paging = 0;
    if (self->_repeat)
        cxuiTimerStop(self->_repeat);
}

static void sbStartPaging(CxUiDScrollBar* self, int32 dir, float32 at)
{
    self->_paging      = dir;
    self->_pageAt      = at;
    self->_repeatTicks = 0;
    sbPage(self);
    if (!self->_repeat) {
        // Holds the bar weakly: the bar holds the timer.
        Weak(CxUiDScrollBar)* wref = objGetWeak(CxUiDScrollBar, self);
        self->_repeat = cxuiTimerCreate(timeMS(SB_REPEAT_MS), 0, sbRepeat, stvar(weakref, objWeakRefBase(wref)));
        objDestroyWeak(&wref);
    }
    cxuiTimerStart(self->_repeat);
}

static void sbPaint(CxUiDScrollBar* self, CxUiDraw* d)
{
    CxUiSize sz = cxuiDrawSize(d);
    cxuiDrawFill(d, 0, 0, sz.w, sz.h, pal(self, CXUI_PAL_ScrollTrack));
    if (self->_range <= 0 || self->_page >= self->_range)
        return;
    SbGeom g   = sbGeom(self);
    bool hot   = self->_dragging || CxUiDrawn(self)->_hot;
    CxUiColor c = pal(self, hot && enabledNow(self) ? CXUI_PAL_ScrollThumbHot : CXUI_PAL_ScrollThumb);
    int32 at = (int32)floorf(g.thumbAt + 0.5f), len = (int32)floorf(g.thumbLen + 0.5f);
    if (self->_orient == CXUI_ORIENT_V)
        cxuiDrawFill(d, 2, at, sz.w - 4, len, c);
    else
        cxuiDrawFill(d, at, 2, len, sz.h - 4, c);
}

static CxUiResult sbMouse(CxUiDScrollBar* self, CxUiMouseEvent* m)
{
    float32 at = sbAlong(self, m);
    SbGeom g   = sbGeom(self);
    switch (m->action) {
    case CXUI_MOUSE_Down:
    case CXUI_MOUSE_DoubleClick:
        if (m->button != CXUI_BTN_Left || self->_range <= self->_page)
            break;
        if (at >= g.thumbAt && at < g.thumbAt + g.thumbLen) {
            self->_dragging = true;
            self->_dragFrom = at;
            self->_dragPos  = self->_pos;
            sbScroll(self, self->_pos, CXUI_SCROLL_ThumbGrabbed);
        } else {
            sbStartPaging(self, at < g.thumbAt ? -1 : 1, at);
        }
        break;
    case CXUI_MOUSE_Move:
        if (self->_dragging && g.len > g.thumbLen) {
            float32 units = (at - self->_dragFrom) * (float32)maxPos(self) / (g.len - g.thumbLen);
            sbScroll(self, self->_dragPos + (int64)floorf(units + 0.5f), CXUI_SCROLL_Thumb);
        } else if (self->_paging) {
            self->_pageAt = at;
        }
        break;
    case CXUI_MOUSE_Up:
        if (m->button != CXUI_BTN_Left)
            break;
        if (self->_dragging) {
            self->_dragging = false;
            sbScroll(self, self->_pos, CXUI_SCROLL_ThumbReleased);
        }
        sbStopPaging(self);
        break;
    default:
        if ((m->flags & CXUI_MOUSE_CaptureLost) && self->_dragging) {
            self->_dragging = false;
            sbScroll(self, self->_pos, CXUI_SCROLL_ThumbReleased);
        }
        if (m->flags & CXUI_MOUSE_CaptureLost)
            sbStopPaging(self);
        break;
    }
    return CXUI_Handled;
}

extern CxUiResult CxUiDrawn_handleEvent(_In_ CxUiDrawn* self, _Inout_ CxUiEvent* ev);   // parent
#undef parent_handleEvent
#define parent_handleEvent(ev) CxUiDrawn_handleEvent((CxUiDrawn*)(self), ev)
CxUiResult CxUiDScrollBar_handleEvent(_In_ CxUiDScrollBar* self, _Inout_ CxUiEvent* ev)
{
    switch (ev->kind) {
    case CXUI_EV_Paint:
        sbPaint(self, ((CxUiPaintEvent*)ev)->draw);
        return CXUI_Handled;
    case CXUI_EV_Mouse:
        CxUiDrawn_handleEvent(CxUiDrawn(self), ev);
        return sbMouse(self, (CxUiMouseEvent*)ev);
    case CXUI_EV_Wheel: {
        CxUiWheelEvent* w = (CxUiWheelEvent*)ev;
        float64 notches   = self->_orient == CXUI_ORIENT_H && w->dx != 0 ? w->dx : w->dy;
        // Passed on where the bar can't move that way
        if (!(notches > 0 && self->_pos < maxPos(self)) && !(notches < 0 && self->_pos > 0))
            return CXUI_Default;
        sbScroll(self, self->_pos + (int64)llround(notches * self->_step), CXUI_SCROLL_Wheel);
        return CXUI_Handled;
    }
    case CXUI_EV_Key: {
        CxUiKeyEvent* k = (CxUiKeyEvent*)ev;
        if (!k->down || !CxUiDrawn(self)->_canFocus)
            return CXUI_Default;
        bool v = self->_orient == CXUI_ORIENT_V;
        switch (k->key) {
        case CXUI_KEY_Up:
        case CXUI_KEY_Left:
            if ((k->key == CXUI_KEY_Up) != v)
                return CXUI_Default;
            sbScroll(self, self->_pos - self->_step, CXUI_SCROLL_Step);
            return CXUI_Handled;
        case CXUI_KEY_Down:
        case CXUI_KEY_Right:
            if ((k->key == CXUI_KEY_Down) != v)
                return CXUI_Default;
            sbScroll(self, self->_pos + self->_step, CXUI_SCROLL_Step);
            return CXUI_Handled;
        case CXUI_KEY_PageUp:
            sbScroll(self, self->_pos - max(self->_page, 1), CXUI_SCROLL_Page);
            return CXUI_Handled;
        case CXUI_KEY_PageDown:
            sbScroll(self, self->_pos + max(self->_page, 1), CXUI_SCROLL_Page);
            return CXUI_Handled;
        default:
            return CXUI_Default;
        }
    }
    default:
        return CxUiDrawn_handleEvent(CxUiDrawn(self), ev);
    }
}

void CxUiDScrollBar_destroy(_In_ CxUiDScrollBar* self)
{
    // Autogen begins -----
    objRelease(&self->_repeat);
    // Autogen ends -------
}

// ---- CxUiLink ----

// Room around the text for the focus rectangle, in DIPs
#define LINK_PAD 2

_objfactory_guaranteed CxUiLink* CxUiLink_createWith(_In_opt_ const CxUiLinkDesc* desc)
{
    CxUiLink* self;
    self = objInstCreate(CxUiLink);

    objInstInit(self);
    CxUiDrawn(self)->_canFocus = true;
    if (desc) {
        strDup(&self->_text, desc->text);
        strDup(&self->_url, desc->url);
        self->_underline           = desc->underline <= CXUI_LINK_NoUnderline ? desc->underline : CXUI_LINK_Underline;
        self->_color               = desc->color;
        self->_hoverColor          = desc->hoverColor;
        CxUiDrawn(self)->_canFocus = !desc->noFocus;
        CxUiWidget(self)->_lp      = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }
    return self;
}

strref CxUiLink_text(_In_ CxUiLink* self)
{
    return live(self) ? (strref)self->_text : NULL;
}

void CxUiLink_setText(_In_ CxUiLink* self, _In_ strref text)
{
    if (!live(self) || strEq(text, self->_text))
        return;
    strDup(&self->_text, text);
    _cxuiLayoutDirty(CxUiWidget(self));
    repaint(self);
}

strref CxUiLink_url(_In_ CxUiLink* self)
{
    return live(self) ? (strref)self->_url : NULL;
}

void CxUiLink_setUrl(_In_ CxUiLink* self, _In_ strref url)
{
    if (live(self))
        strDup(&self->_url, url);
}

extern uint32 CxUiCanvas_eventMask(_In_ CxUiCanvas* self);   // parent
#undef parent_eventMask
#define parent_eventMask() CxUiCanvas_eventMask((CxUiCanvas*)(self))
uint32 CxUiLink_eventMask(_In_ CxUiLink* self)
{
    return parent_eventMask() | (1 << CXUI_EV_Activate);
}

extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#undef parent_measure
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiLink_measure(_In_ CxUiLink* self)
{
    CxUiSize t = textSize(CxUiWidget(self), self->_text);
    return (CxUiSize){ t.w + LINK_PAD * 2, t.h + LINK_PAD * 2 };
}

extern void CxUiCanvas_realizePeer(_In_ CxUiCanvas* self, _In_ CxUiPeer* host);   // parent
#define parent_realizePeer(host) CxUiCanvas_realizePeer((CxUiCanvas*)(self), host)
void CxUiLink_realizePeer(_In_ CxUiLink* self, _In_ CxUiPeer* host)
{
    parent_realizePeer(host);
    cxuiCanvasSetCursor(self, CXUI_CURSOR_Hand);
}

static void linkPaint(CxUiLink* self, CxUiDraw* d)
{
    CxUiDrawn* dn = CxUiDrawn(self);
    CxUiSize sz   = cxuiDrawSize(d);
    cxuiDrawFill(d, 0, 0, sz.w, sz.h, _cxuiBackgroundOf(CxUiWidget(self)));
    bool hot    = dn->_hot && enabledNow(self);
    CxUiColor c = !enabledNow(self) ? pal(self, CXUI_PAL_TextDisabled)
                  : hot             ? orPal(self, self->_hoverColor, CXUI_PAL_LinkHover)
                                    : orPal(self, self->_color, CXUI_PAL_Link);
    CxUiFont* f = cxuiWidgetFont(self);
    CxUiSize t  = textSize(CxUiWidget(self), self->_text);
    int32 y     = (sz.h - t.h) / 2;
    cxuiDrawTextBox(d, f, (CxUiRect){ LINK_PAD, y, t.w, t.h }, self->_text, CXUI_TEXT_Left | CXUI_TEXT_Top, c);
    bool focused = dn->_canFocus && dn->_focused;
    if (self->_underline == CXUI_LINK_Underline || (self->_underline == CXUI_LINK_HoverOnly && (hot || focused))) {
        CxUiFontMetrics fm;
        cxuiFontMetrics(f, &fm);
        float32 s   = cxuiFontScale(f);
        int32 under = y + (int32)ceilf((fm.ascent + max(fm.descent / 2, 1)) / s);
        cxuiDrawFill(d, LINK_PAD, min(under, sz.h - 1), t.w, 1, c);
    }
    if (focused)
        cxuiDrawFrame(d, 0, 0, sz.w, sz.h, 1, pal(self, CXUI_PAL_Focus));
}

// A click is a press and a release over the link; Space or Enter opens it while it has focus.
extern CxUiResult CxUiDrawn_handleEvent(_In_ CxUiDrawn* self, _Inout_ CxUiEvent* ev);   // parent
#undef parent_handleEvent
#define parent_handleEvent(ev) CxUiDrawn_handleEvent((CxUiDrawn*)(self), ev)
CxUiResult CxUiLink_handleEvent(_In_ CxUiLink* self, _Inout_ CxUiEvent* ev)
{
    CxUiDrawn* dn = CxUiDrawn(self);
    switch (ev->kind) {
    case CXUI_EV_Paint:
        linkPaint(self, ((CxUiPaintEvent*)ev)->draw);
        return CXUI_Handled;
    case CXUI_EV_Mouse: {
        CxUiMouseEvent* m = (CxUiMouseEvent*)ev;
        bool clicked      = m->action == CXUI_MOUSE_Up && m->button == CXUI_BTN_Left && dn->_down && inside(self, m);
        CxUiDrawn_handleEvent(dn, ev);
        if (clicked)
            raiseEvent(self, CXUI_EV_Activate);
        return CXUI_Handled;
    }
    case CXUI_EV_Key: {
        CxUiKeyEvent* k = (CxUiKeyEvent*)ev;
        if (dn->_canFocus && k->down && !k->repeat && k->mods == 0 &&
            (k->key == CXUI_KEY_Space || k->key == CXUI_KEY_Enter || k->key == CXUI_KEY_PadEnter)) {
            raiseEvent(self, CXUI_EV_Activate);
            return CXUI_Handled;
        }
        return CXUI_Default;
    }
    default:
        return CxUiDrawn_handleEvent(dn, ev);
    }
}

void CxUiLink_destroy(_In_ CxUiLink* self)
{
    // Autogen begins -----
    strDestroy(&self->_text);
    strDestroy(&self->_url);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "core/cxuidrawn.auto.inc"
// clang-format on
// Autogen ends -------
