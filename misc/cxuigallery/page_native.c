// Native controls: labels and buttons (mnemonics, the window's Enter and Esc defaults, ellipses,
// colors, a paragraph that wraps, and a popup with defaults of its own), check boxes and radio
// groups.

#include "gallery.h"

// Holds a reference to each widget it uses after building, released when the page goes
typedef struct NativePage {
    CxUiTextField* name;
    CxUiButton* ok;
    CxUiButton* apply;
    CxUiButton* cancel;
    CxUiCheck* all;   // mixed while the two below differ
    CxUiCheck* wrap;
    CxUiCheck* hidden;
    CxUiRadioGroup* size;
    CxUiRadioGroup* color;
    bool applyDefault;
} NativePage;

static NativePage* page;

static void pageSay(strref what)
{
    galleryStatus(_S"native", what);
}

static CxUiResult popupAnswer(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    cxuiWindowEndPopup(stvlAtObj(cvars, 0, CxUiWindow), stvlAt(cvars, 1, int32));
    return CXUI_Default;
}

// A small dialog: Enter saves, Esc cancels, Alt+D discards
static int32 askSave(void)
{
    CxUiWindow* p = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"Save changes?" });
    galleryFollowTheme(p);
    CxUiBox* col = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 10, .margin = { 14, 14, 14, 14 } });
    galleryAdd(col, cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"Save the changes to \"Untitled\" before closing?" }));
    CxUiBox* row = cxuiBoxCreate(&(CxUiBoxDesc){ .gap = 6 });
    galleryAdd(row, cxuiFillCreate());
    CxUiButton* save    = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"&Save" });
    CxUiButton* discard = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"&Discard" });
    CxUiButton* cancel  = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"Cancel" });
    cxuiOnActivate(save, popupAnswer, stvar(object, p), stvar(int32, 1));
    cxuiOnActivate(discard, popupAnswer, stvar(object, p), stvar(int32, 2));
    cxuiOnActivate(cancel, popupAnswer, stvar(object, p), stvar(int32, 3));
    cxuiContainerAppend(row, discard);
    cxuiContainerAppend(row, cancel);
    cxuiContainerAppend(row, save);
    cxuiWindowSetDefaultEnter(p, save);
    cxuiWindowSetDefaultEsc(p, cancel);
    objRelease(&save);
    objRelease(&discard);
    objRelease(&cancel);
    galleryAdd(col, row);
    galleryAdd(p, col);
    int32 r = cxuiWindowPopup(p, galleryWindow());
    cxuiDestroy(p);
    objRelease(&p);
    return r;
}

static void setDefaults(void)
{
    cxuiWindowSetDefaultEnter(galleryWindow(), page->applyDefault ? page->apply : page->ok);
    cxuiWindowSetDefaultEsc(galleryWindow(), page->cancel);
}

static CxUiResult action(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    int32 what                    = stvlAt(cvars, 1, int32);
    static const strref answers[] = { _S"closed", _S"Save", _S"Discard", _S"Cancel" };
    string line                   = 0;
    switch (what) {
    case 0: pageSay(_S"OK"); break;
    case 1: pageSay(_S"Cancel"); break;
    case 2: pageSay(_S"Apply"); break;
    case 3: {
        int32 r = askSave();
        strFormat(&line, _SL("the popup answered ${string}"), stvar(strref, answers[r >= 0 && r <= 3 ? r : 0]));
        pageSay(line);
        break;
    }
    case 4:
        page->applyDefault = !page->applyDefault;
        setDefaults();
        pageSay(page->applyDefault ? _S"Enter now presses Apply" : _S"Enter now presses OK");
        break;
    case 5: pageSay(_S"Padded"); break;
    case 6: pageSay(_S"Disabled, yet pressed"); break;
    }
    strDestroy(&line);
    return CXUI_Default;
}

static CxUiButton* button(void* box, strref title, int32 what)
{
    CxUiButton* b = cxuiButtonCreate(&(CxUiButtonDesc){ .title = title });
    cxuiOnActivate(b, action, stvar(ptr, page), stvar(int32, what));
    cxuiContainerAppend((CxUiWidget*)box, CxUiWidget(b));
    return b;
}

// A labeled field appended to col; returns the caller's reference to the field
static CxUiTextField* field(void* col, strref label, strref text)
{
    CxUiBox* row = galleryRow(col, 6);
    CxUiLabel* l = cxuiLabelCreate(&(CxUiLabelDesc){ .text = label });
    cxuiWidgetSetNaturalSize(l, cxuiLen(70), cxuiLen(0));
    galleryAdd(row, l);
    CxUiTextField* f = cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .text = text, .columns = 24 });
    cxuiWidgetSetExpand(f, CXUI_EXPAND_H);
    cxuiContainerAppend(row, f);
    objRelease(&row);
    return f;
}

// ---- check boxes and radio buttons ----

static void allFollows(void)
{
    bool a = cxuiCheckValue(page->wrap), b = cxuiCheckValue(page->hidden);
    cxuiCheckSetState(page->all, a && b ? CXUI_CHECK_On : a || b ? CXUI_CHECK_Mixed : CXUI_CHECK_Off);
}

static CxUiResult checkChanged(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(cvars);
    CxUiCheck* c = objDynCast(CxUiCheck, ev->base.current);
    if (c == page->all) {
        // From mixed, a click checks it.
        bool on = cxuiCheckValue(c);
        cxuiCheckSetValue(page->wrap, on);
        cxuiCheckSetValue(page->hidden, on);
    } else if (c == page->wrap || c == page->hidden) {
        allFollows();
    }
    static const strref states[] = { _S"clear", _S"checked", _S"mixed" };
    string line = 0;
    strFormat(&line, _SL("\"${string}\" ${string}"), stvar(strref, cxuiCheckTitle(c)),
              stvar(strref, states[cxuiCheckState(c)]));
    pageSay(line);
    strDestroy(&line);
    return CXUI_Default;
}

static CxUiResult radioChanged(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(cvars);
    CxUiRadioGroup* g = objDynCast(CxUiRadioGroup, ev->base.current);
    CxUiRadio* r      = cxuiRadioGroupValue(g);
    string line       = 0;
    strFormat(&line, _SL("${string}: ${string} (index ${int})"), stvar(strref, CxUiWidget(g)->name),
              stvar(strref, r ? cxuiRadioTitle(r) : NULL), stvar(int32, cxuiRadioGroupIndex(g)));
    pageSay(line);
    strDestroy(&line);
    return CXUI_Default;
}

static CxUiResult sizesEnabled(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    cxuiWidgetSetEnabled(page->size, !cxuiWidgetEnabled(page->size));
    return CXUI_Default;
}

static CxUiCheck* check(void* box, strref title, uint32 state)
{
    CxUiCheck* c = cxuiCheckCreate(&(CxUiCheckDesc){ .title = title, .state = state });
    cxuiOnChange(c, checkChanged, stvNone);
    cxuiContainerAppend((CxUiWidget*)box, c);
    return c;
}

static void radio(void* box, strref title)
{
    galleryAdd(box, cxuiRadioCreate(&(CxUiRadioDesc){ .title = title }));
}

static void buildChoices(CxUiBox* col)
{
    galleryHeading(col, _S"Check boxes");
    page->all     = check(col, _S"&Everything below", CXUI_CHECK_Off);
    CxUiBox* subs = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6, .margin = { 20, 0, 0, 0 } });
    page->wrap    = check(subs, _S"Wrap &long lines", CXUI_CHECK_On);
    page->hidden  = check(subs, _S"Show hidden &files", CXUI_CHECK_Off);
    galleryAdd(col, subs);
    allFollows();
    CxUiCheck* three = cxuiCheckCreate(&(CxUiCheckDesc){ .title = _S"Three &states, clicked through mixed",
                                                         .threeState = true });
    cxuiOnChange(three, checkChanged, stvNone);
    galleryAdd(col, three);
    CxUiCheck* off = check(col, _S"Disabled, checked", CXUI_CHECK_On);
    cxuiWidgetSetEnabled(off, false);
    objRelease(&off);

    galleryHeading(col, _S"Radio groups");
    CxUiBox* row = galleryRow(col, 24);
    page->size   = cxuiRadioGroupCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 4, .name = _S"size" });
    radio(page->size, _S"S&mall");
    radio(page->size, _S"Med&ium");
    radio(page->size, _S"Lar&ge");
    cxuiRadioGroupSetIndex(page->size, 1);
    cxuiOnChange(page->size, radioChanged, stvNone);
    cxuiContainerAppend(row, page->size);
    // One of the colors sits in a row of its own inside the group, and still belongs to it.
    page->color  = cxuiRadioGroupCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 4, .name = _S"color" });
    radio(page->color, _S"&Red");
    CxUiBox* nest = galleryRow(page->color, 6);
    radio(nest, _S"Gree&n");
    galleryAdd(nest, cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"(in a row of its own)" }));
    objRelease(&nest);
    radio(page->color, _S"&Blue");
    cxuiOnChange(page->color, radioChanged, stvNone);
    cxuiContainerAppend(row, page->color);
    objRelease(&row);
    CxUiCheck* lock = cxuiCheckCreate(&(CxUiCheckDesc){ .title = _S"Disable the sizes" });
    cxuiOnChange(lock, sizesEnabled, stvNone);
    galleryAdd(col, lock);
    galleryAdd(col, cxuiLabelCreate(&(CxUiLabelDesc){
                        .text = _S"Tab stops at a group's choice; the arrow keys pick another.", .wrap = true }));
}

static bool pageGone(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    objRelease(&page->name);
    objRelease(&page->ok);
    objRelease(&page->apply);
    objRelease(&page->cancel);
    objRelease(&page->all);
    objRelease(&page->wrap);
    objRelease(&page->hidden);
    objRelease(&page->size);
    objRelease(&page->color);
    xaDestroy(&page);
    return true;
}

static void build(CxUiBox* pageBox)
{
    page = xaAllocStruct(NativePage, XA_Zero);
    cchainAttach(&CxUiWidget(pageBox)->destroyNotify, pageGone, stvNone);

    // Labels and buttons on the left, the other controls on the right
    CxUiBox* cols = galleryRow(pageBox, 24);
    CxUiBox* box  = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6 });
    CxUiBox* side = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6 });
    cxuiWidgetSetAlign(box, CXUI_ALIGN_Default, CXUI_ALIGN_Start);
    cxuiWidgetSetAlign(side, CXUI_ALIGN_Default, CXUI_ALIGN_Start);
    cxuiContainerAppend(cols, box);
    cxuiContainerAppend(cols, side);
    buildChoices(side);
    objRelease(&side);
    objRelease(&cols);

    galleryHeading(box, _S"Labels");
    page->name           = field(box, _S"&Name:", NULL);
    CxUiTextField* email = field(box, _S"&Email:", _S"someone@example.com");
    objRelease(&email);

    // Narrower than their text, so they shorten it
    CxUiLabel* end = cxuiLabelCreate(&(CxUiLabelDesc){
        .text = _S"A long label that ends with an ellipsis where it runs out of room", .ellipsis = CXUI_ELLIPSIS_End });
    cxuiWidgetSetNaturalSize(end, cxuiLen(240), cxuiLen(0));
    galleryAdd(box, end);
    CxUiLabel* path = cxuiLabelCreate(&(CxUiLabelDesc){
        .text = _S"C:\\Users\\Someone\\Documents\\Projects\\Example\\notes.txt", .ellipsis = CXUI_ELLIPSIS_Path });
    cxuiWidgetSetNaturalSize(path, cxuiLen(240), cxuiLen(0));
    galleryAdd(box, path);
    CxUiLabel* right = cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"Right-aligned, in red", .align = CXUI_TEXT_Right,
                                                         .color = cxuiRGB(200, 40, 40) });
    cxuiWidgetSetAlign(right, CXUI_ALIGN_Fill, CXUI_ALIGN_Default);
    galleryAdd(box, right);
    galleryAdd(box, cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"Two lines,\nand a literal && in a label" }));
    galleryAdd(box, cxuiLabelCreate(&(CxUiLabelDesc){
                        .text = _S"A paragraph that wraps: it is as wide as 40 average characters to begin with, and "
                                "breaks into fewer lines when the window is made wider, or more when it is made "
                                "narrower, with everything below it moving to make room.",
                        .wrap = true }));

    galleryHeading(box, _S"Buttons");
    CxUiBox* tools     = galleryRow(box, 6);
    CxUiButton* padded = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"Pa&dded", .padding = { 12, 4 } });
    cxuiOnActivate(padded, action, stvar(ptr, page), stvar(int32, 5));
    galleryAdd(tools, padded);
    CxUiButton* off = button(tools, _S"Disabled", 6);
    cxuiWidgetSetEnabled(off, false);
    objRelease(&off);
    CxUiButton* popup = button(tools, _S"&Popup...", 3);
    objRelease(&popup);
    CxUiButton* swap = button(tools, _S"S&wap default", 4);
    objRelease(&swap);
    objRelease(&tools);

    CxUiBox* row = galleryRow(box, 6);
    CxUiLabel* hint =
        cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"Enter presses OK, Esc presses Cancel", .ellipsis = CXUI_ELLIPSIS_End });
    cxuiWidgetSetNaturalSize(hint, cxuiLen(240), cxuiLen(0));
    cxuiWidgetSetExpand(hint, CXUI_EXPAND_H);
    galleryAdd(row, hint);
    page->apply  = button(row, _S"&Apply", 2);
    page->ok     = button(row, _S"OK", 0);
    page->cancel = button(row, _S"Cancel", 1);
    objRelease(&row);
    objRelease(&box);
}

static void shown(void)
{
    setDefaults();
    cxuiFocus(page->name);
}

const GalleryPage galleryNative = {
    .name  = _S"native",
    .title = _S"Native controls",
    .build = build,
    .shown = shown,
};
