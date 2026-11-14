// Native controls that hold a value: a list and a dropdown, a list where any number can be
// selected, a text area, sliders and progress bars; and a titled frame of links, with tips on what
// is around them.

#include "gallery.h"

#include <cx/time.h>

// Holds a reference to each widget it uses after building, released when the page goes
typedef struct ValuesPage {
    CxUiList* list;
    CxUiList* multi;
    CxUiDropdown* drop;
    CxUiTextArea* notes;
    CxUiSlider* slider;
    CxUiProgress* bar;
    int32 added;
    int32 logged;
} ValuesPage;

static ValuesPage* page;

static void pageSay(strref what)
{
    galleryStatus(_S"values", what);
}

// ---- lists ----

static void sayItem(strref what, strref item, int32 index)
{
    string line = 0;
    strFormat(&line, _SL("${string} \"${string}\" (${int})"), stvar(strref, what), stvar(strref, item), stvar(int32, index));
    pageSay(line);
    strDestroy(&line);
}

static CxUiResult listChanged(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    int32 v = cxuiListValue(page->list);
    sayItem(_S"list: picked", cxuiListItem(page->list, v), v);
    return CXUI_Default;
}

static CxUiResult listOpened(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    sayItem(_S"list: opened", cxuiListItem(page->list, (int32)ev->item), (int32)ev->item);
    return CXUI_Default;
}

static CxUiResult dropChanged(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    int32 v = cxuiDropdownValue(page->drop);
    sayItem(_S"dropdown: picked", cxuiDropdownItem(page->drop, v), v);
    return CXUI_Default;
}

static CxUiResult listAction(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    string line = 0;
    switch (stvlAt(cvars, 0, int32)) {
    case 0:
        // Before the selection, which stays on its item
        strFormat(&line, _SL("Added ${int}"), stvar(int32, ++page->added));
        cxuiListInsert(page->list, max(cxuiListValue(page->list), 0), line);
        cxuiDropdownAppend(page->drop, line);
        pageSay(_S"list: added an item before the selection, and one to the dropdown");
        break;
    case 1:
        if (cxuiListValue(page->list) >= 0)
            cxuiListRemove(page->list, cxuiListValue(page->list));
        pageSay(_S"list: removed the selected item");
        break;
    case 2: {
        sa_string many;
        saInit(&many, string, 500);
        for (int32 i = 1; i <= 500; i++) {
            strFormat(&line, _SL("Item ${int} of 500"), stvar(int32, i));
            saPush(&many, string, line);
        }
        cxuiListSetItems(page->list, many);
        saDestroy(&many);
        cxuiListSetValue(page->list, 249);
        cxuiListShow(page->list, 249);
        pageSay(_S"list: 500 items, the 250th selected and shown");
        break;
    }
    }
    strDestroy(&line);
    return CXUI_Default;
}

static void buildLists(CxUiBox* col)
{
    static const strref planets[] = { _S"Mercury", _S"Venus", _S"Earth", _S"Mars", _S"Jupiter", _S"Saturn",
                                      _S"Uranus",  _S"Neptune", NULL };
    static const strref sizes[]   = { _S"Small", _S"Medium", _S"Large", _S"Extra large", NULL };
    galleryHeading(col, _S"List and dropdown");
    CxUiBox* row = galleryRow(col, 6);
    CxUiLabel* l = cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"Si&ze:" });
    galleryAdd(row, l);
    page->drop = cxuiDropdownCreate(&(CxUiDropdownDesc){ .items = sizes, .value = 1 });
    cxuiOnChange(page->drop, dropChanged, stvNone);
    cxuiContainerAppend(row, page->drop);
    objRelease(&row);
    galleryAdd(col, cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"&Planets (double-click or Enter opens one):" }));
    row        = cxuiBoxCreate(&(CxUiBoxDesc){ .gap = 6 });
    page->list = cxuiListCreate(&(CxUiListDesc){ .items = planets, .value = 2, .visibleItems = 6 });
    cxuiOnChange(page->list, listChanged, stvNone);
    cxuiOnActivate(page->list, listOpened, stvNone);
    cxuiWidgetSetExpand(page->list, CXUI_EXPAND_H);
    cxuiContainerAppend(row, page->list);
    CxUiBox* tools = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 4, .normalize = CXUI_EXPAND_H });
    static const strref titles[] = { _S"Add", _S"Remove", _S"500 items" };
    for (int32 i = 0; i < 3; i++) {
        CxUiButton* b = cxuiButtonCreate(&(CxUiButtonDesc){ .title = titles[i] });
        cxuiOnActivate(b, listAction, stvar(int32, i));
        galleryAdd(tools, b);
    }
    galleryAdd(row, tools);
    galleryAdd(col, row);
}

// ---- a list where any number can be selected ----

static CxUiResult multiChanged(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    string line = 0;
    int32 first = cxuiListValue(page->multi);
    strFormat(&line, _SL("several: ${int} selected, the first \"${string}\""), stvar(int32, cxuiListSelectedCount(page->multi)),
              stvar(strref, cxuiListItem(page->multi, first)));
    pageSay(line);
    strDestroy(&line);
    return CXUI_Default;
}

// Delete takes the selected items out, from the last so the positions hold.
static CxUiResult multiKey(stvlist* cvars, CxUiKeyEvent* ev)
{
    unused_noeval(cvars);
    if (ev->key != CXUI_KEY_Delete || ev->mods != 0 || !ev->down)
        return CXUI_Default;
    int32 n = 0;
    cxuiListBeginUpdate(page->multi);
    for (int32 i = cxuiListCount(page->multi) - 1; i >= 0; i--) {
        if (cxuiListIsSelected(page->multi, i)) {
            cxuiListRemove(page->multi, i);
            n++;
        }
    }
    cxuiListEndUpdate(page->multi);
    string line = 0;
    strFormat(&line, _SL("several: Delete took ${int} out"), stvar(int32, n));
    pageSay(line);
    strDestroy(&line);
    return CXUI_Handled;
}

static void multiFill(bool held)
{
    int64 start = clockTimer();
    string line = 0;
    if (held)
        cxuiListBeginUpdate(page->multi);
    cxuiListClear(page->multi);
    for (int32 i = 1; i <= 5000; i++) {
        strFormat(&line, _SL("Row ${int}"), stvar(int32, i));
        cxuiListAppend(page->multi, line);
    }
    cxuiListSetSelected(page->multi, 2, 3, true);
    if (held)
        cxuiListEndUpdate(page->multi);
    cxuiRefresh(page->multi);
    strFormat(&line, _SL("several: 5,000 items appended ${string} in ${int} ms"),
              stvar(strref, held ? _S"while held back" : _S"one at a time"),
              stvar(int32, (int32)((clockTimer() - start) / 1000)));
    pageSay(line);
    strDestroy(&line);
}

static CxUiResult multiAction(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    switch (stvlAt(cvars, 0, int32)) {
    case 0: multiFill(true); break;
    case 1: multiFill(false); break;
    case 2:
        cxuiListSetTopItem(page->multi, 99);
        pageSay(_S"several: the 100th item is the first shown");
        break;
    }
    return CXUI_Default;
}

static void buildMulti(CxUiBox* col)
{
    static const strref fruit[] = { _S"Apple", _S"Banana", _S"Cherry", _S"Date", _S"Elderberry", _S"Fig", _S"Grape", NULL };
    galleryHeading(col, _S"Several selected");
    galleryAdd(col, cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"Ctrl and Shift select more; Delete takes them out:" }));
    CxUiBox* row = cxuiBoxCreate(&(CxUiBoxDesc){ .gap = 6 });
    page->multi  = cxuiListCreate(&(CxUiListDesc){ .items = fruit, .value = -1, .visibleItems = 6, .columns = 16,
                                                    .select = CXUI_SELECT_Multi });
    cxuiOnChange(page->multi, multiChanged, stvNone);
    cxuiOnKey(page->multi, multiKey, stvNone);
    cxuiWidgetSetExpand(page->multi, CXUI_EXPAND_H);
    cxuiContainerAppend(row, page->multi);
    CxUiBox* tools = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 4, .normalize = CXUI_EXPAND_H });
    static const strref titles[] = { _S"5,000, held back", _S"5,000, one at a time", _S"100th at the top" };
    for (int32 i = 0; i < 3; i++) {
        CxUiButton* b = cxuiButtonCreate(&(CxUiButtonDesc){ .title = titles[i] });
        cxuiOnActivate(b, multiAction, stvar(int32, i));
        galleryAdd(tools, b);
    }
    galleryAdd(row, tools);
    galleryAdd(col, row);
}

// ---- text area ----

static CxUiResult notesAction(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    if (stvlAt(cvars, 0, int32) == 0) {
        string line = 0;
        strFormat(&line, _SL("\nLine ${int}, added at the end"), stvar(int32, ++page->logged));
        cxuiTextAreaAppend(page->notes, line);
        cxuiTextAreaScrollToEnd(page->notes);
        strDestroy(&line);
        pageSay(_S"text area: a line added, and scrolled to the end");
    } else {
        bool ro = !cxuiTextFieldReadOnly(CxUiTextField(page->notes));
        cxuiTextFieldSetReadOnly(CxUiTextField(page->notes), ro);
        pageSay(ro ? _S"text area: read-only; Enter presses OK" : _S"text area: editable");
    }
    return CXUI_Default;
}

static CxUiResult notesCommitted(stvlist* cvars, CxUiCommitEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    string line = 0;
    strFormat(&line, _SL("text area: left after an edit, ${int} characters"),
              stvar(int32, strLen(cxuiTextFieldText(CxUiTextField(page->notes)))));
    pageSay(line);
    strDestroy(&line);
    return CXUI_Default;
}

static void buildNotes(CxUiBox* col)
{
    galleryHeading(col, _S"Text area");
    page->notes = cxuiTextAreaCreate(&(CxUiTextAreaDesc){
        .text  = _S"A text area of several lines. Enter starts a new line here rather than pressing OK, and Tab "
                 "moves on.\nIts lines wrap at words.",
        .wrap  = true,
        .lines = 5 });
    cxuiOnCommit(page->notes, notesCommitted, stvNone);
    cxuiContainerAppend(col, page->notes);
    CxUiBox* row = galleryRow(col, 6);
    static const strref titles[] = { _S"Add a &line", _S"&Read-only" };
    for (int32 i = 0; i < 2; i++) {
        CxUiButton* b = cxuiButtonCreate(&(CxUiButtonDesc){ .title = titles[i] });
        cxuiOnActivate(b, notesAction, stvar(int32, i));
        galleryAdd(row, b);
    }
    objRelease(&row);
}

// ---- sliders and progress bars ----

static CxUiResult sliderMoved(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    cxuiProgressSetValue(page->bar, cxuiSliderValue(page->slider));
    return CXUI_Default;
}

static CxUiResult sliderLetGo(stvlist* cvars, CxUiCommitEvent* ev)
{
    unused_noeval(ev);
    CxUiSlider* s = stvlAtObj(cvars, 0, CxUiSlider);
    string line   = 0;
    strFormat(&line, _SL("${string}: let go at ${float(0.2)}"), stvar(strref, CxUiWidget(s)->name),
              stvar(float64, cxuiSliderValue(s)));
    pageSay(line);
    strDestroy(&line);
    return CXUI_Default;
}

static CxUiResult busyChanged(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    CxUiProgress* busy = stvlAtObj(cvars, 0, CxUiProgress);
    bool on            = cxuiCheckValue((CxUiCheck*)ev->base.current);
    if (on)
        cxuiProgressSetIndeterminate(busy, true);
    else
        cxuiProgressSetValue(busy, 0);
    return CXUI_Default;
}

static void buildSliders(CxUiBox* col)
{
    galleryHeading(col, _S"Sliders and progress bars");
    CxUiBox* row = galleryRow(col, 12);
    CxUiBox* bars = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6 });
    cxuiWidgetSetExpand(bars, CXUI_EXPAND_H);
    page->slider = cxuiSliderCreate(&(CxUiSliderDesc){ .max = 100, .step = 5, .value = 40, .ticks = 11, .name = _S"slider" });
    cxuiWidgetSetExpand(page->slider, CXUI_EXPAND_H);
    cxuiOnChange(page->slider, sliderMoved, stvNone);
    cxuiOnCommit(page->slider, sliderLetGo, stvar(object, page->slider));
    cxuiContainerAppend(bars, page->slider);
    page->bar = cxuiProgressCreate(&(CxUiProgressDesc){ .value = 40 });
    cxuiWidgetSetExpand(page->bar, CXUI_EXPAND_H);
    cxuiContainerAppend(bars, page->bar);
    CxUiProgress* busy = cxuiProgressCreate(NULL);
    cxuiWidgetSetExpand(busy, CXUI_EXPAND_H);
    CxUiCheck* on = cxuiCheckCreate(&(CxUiCheckDesc){ .title = _S"B&usy, with no value" });
    cxuiOnChange(on, busyChanged, stvar(object, busy));
    galleryAdd(bars, on);
    galleryAdd(bars, busy);
    galleryAdd(row, bars);
    CxUiSlider* up = cxuiSliderCreate(&(CxUiSliderDesc){ .min = 0, .max = 1, .step = 0.1, .value = 0.5,
                                                          .orient = CXUI_ORIENT_V, .length = 90, .name = _S"vertical" });
    cxuiOnCommit(up, sliderLetGo, stvar(object, up));
    galleryAdd(row, up);
    objRelease(&row);
}

// ---- links and tips ----

static CxUiResult linkOpened(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    CxUiLink* l = (CxUiLink*)ev->base.current;
    string line = 0;
    strFormat(&line, _SL("link: would open ${string}"), stvar(strref, cxuiLinkUrl(l)));
    pageSay(line);
    strDestroy(&line);
    return CXUI_Default;
}

static void link(void* box, strref text, strref url, uint32 underline, strref tip)
{
    CxUiLink* l = cxuiLinkCreate(&(CxUiLinkDesc){ .text = text, .url = url, .underline = underline });
    cxuiOnActivate(l, linkOpened, stvNone);
    cxuiWidgetSetTip(l, tip);
    galleryAdd(box, l);
}

static void buildLinks(CxUiBox* col)
{
    CxUiFrame* f = cxuiFrameCreate(&(CxUiPanelDesc){ .orient = CXUI_ORIENT_V, .title = _S"Links and tips",
                                                     .margin = { 8, 4, 8, 8 }, .gap = 4 });
    link(f, _S"Release notes", _S"https://example.com/notes", CXUI_LINK_Underline, _S"Opens the release notes");
    link(f, _S"Underlined only under the mouse", _S"https://example.com/hover", CXUI_LINK_HoverOnly,
         _S"Tab stops here too;\nSpace or Enter opens it");
    CxUiLink* off = cxuiLinkCreate(&(CxUiLinkDesc){ .text = _S"A disabled link", .url = _S"https://example.com" });
    cxuiWidgetSetEnabled(off, false);
    galleryAdd(f, off);
    galleryAdd(f, cxuiLabelCreate(&(CxUiLabelDesc){
                      .text = _S"Rest the mouse on the list, the sliders or the text area for their tips.", .wrap = true }));
    cxuiWidgetSetTip(f, _S"A frame with a title");
    galleryAdd(col, f);
}

static bool pageGone(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    objRelease(&page->list);
    objRelease(&page->multi);
    objRelease(&page->drop);
    objRelease(&page->notes);
    objRelease(&page->slider);
    objRelease(&page->bar);
    xaDestroy(&page);
    return true;
}

static void build(CxUiBox* pageBox)
{
    page = xaAllocStruct(ValuesPage, XA_Zero);
    cchainAttach(&CxUiWidget(pageBox)->destroyNotify, pageGone, stvNone);

    // The list and the sliders on the left, the text area on the right
    CxUiBox* cols  = galleryRow(pageBox, 24);
    CxUiBox* left  = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6 });
    CxUiBox* right = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6 });
    cxuiWidgetSetAlign(left, CXUI_ALIGN_Default, CXUI_ALIGN_Start);
    cxuiWidgetSetAlign(right, CXUI_ALIGN_Default, CXUI_ALIGN_Start);
    cxuiContainerAppend(cols, left);
    cxuiContainerAppend(cols, right);
    buildLists(left);
    buildSliders(left);
    buildNotes(right);
    buildMulti(right);
    buildLinks(right);
    cxuiWidgetSetTip(page->list, _S"Double-click a planet, or press Enter, to open it");
    cxuiWidgetSetTip(page->slider, _S"Moves in steps of 5;\nthe bar below follows it");
    cxuiWidgetSetTip(page->notes, _S"Several lines, wrapped at words");
    objRelease(&left);
    objRelease(&right);
    objRelease(&cols);
}

const GalleryPage galleryValues = {
    .name  = _S"values",
    .title = _S"Values",
    .build = build,
};
