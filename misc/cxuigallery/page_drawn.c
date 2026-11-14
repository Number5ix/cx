// The drawn set: buttons, toggles and a group, labels, scroll bars and tabs, all drawn by cxui in
// the palette's colors.

#include "gallery.h"

// Holds a reference to each widget it uses after building, released when the page goes
typedef struct DrawnPage {
    CxUiBox* area;          // everything Disable grays
    CxUiDLabel* sample;     // aligned by the group
    CxUiDLabel* scrolled;   // what the bars say
} DrawnPage;

static DrawnPage* page;

static void pageSay(strref what)
{
    galleryStatus(_S"drawn", what);
}

static strref titleOf(CxUiEvent* ev)
{
    CxUiDButton* b = objDynCast(CxUiDButton, ev->target);
    return b ? cxuiDButtonTitle(b) : _S"?";
}

static CxUiResult pressed(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    string what = 0;
    strFormat(&what, _SL("${string} pressed"), stvar(strref, titleOf(&ev->base)));
    pageSay(what);
    strDestroy(&what);
    return CXUI_Default;
}

static CxUiResult toggled(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(cvars);
    CxUiDButton* b = objDynCast(CxUiDButton, ev->base.target);
    string what    = 0;
    strFormat(&what, _SL("${string} ${string}"), stvar(strref, titleOf(&ev->base)),
              stvar(strref, cxuiDButtonValue(b) ? _S"on" : _S"off"));
    pageSay(what);
    strDestroy(&what);
    return CXUI_Default;
}

static CxUiResult aligned(stvlist* cvars, CxUiChangeEvent* ev)
{
    cxuiWidgetSetAlign(page->sample, stvlAt(cvars, 0, uint32), CXUI_ALIGN_Default);
    string what = 0;
    strFormat(&what, _SL("sample placed ${string}"), stvar(strref, titleOf(&ev->base)));
    pageSay(what);
    strDestroy(&what);
    return CXUI_Default;
}

static CxUiResult disabled(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(cvars);
    bool off = cxuiDButtonValue(objDynCast(CxUiDButton, ev->base.target));
    cxuiWidgetSetEnabled(page->area, !off);
    pageSay(off ? _S"everything below disabled, in the palette's disabled colors" : _S"enabled again");
    return CXUI_Default;
}

static CxUiResult scrolled(stvlist* cvars, CxUiScrollEvent* ev)
{
    static const strref reasons[] = { _S"thumb", _S"page", _S"step", _S"wheel", _S"thumb grabbed", _S"thumb released" };
    string what                   = 0;
    strFormat(&what, _SL("${string} bar at ${int} (${string})"), stvar(strref, stvlAt(cvars, 0, bool) ? _S"down" : _S"across"),
              stvar(int64, ev->pos), stvar(strref, ev->reason < 6 ? reasons[ev->reason] : _S"?"));
    cxuiDLabelSetText(page->scrolled, what);
    pageSay(what);
    strDestroy(&what);
    return CXUI_Default;
}

static CxUiResult tabChosen(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(cvars);
    string what = 0;
    strFormat(&what, _SL("tab ${int}"), stvar(int32, cxuiDTabsValue(objDynCast(CxUiDTabs, ev->base.target)) + 1));
    pageSay(what);
    strDestroy(&what);
    return CXUI_Default;
}

static CxUiDButton* drawnButton(void* box, strref title, bool toggle, CxUiDGroup* group)
{
    CxUiDButton* b = cxuiDButtonCreate(&(CxUiDButtonDesc){ .title = title, .toggle = toggle, .group = group,
                                                           .padding = { 8, 4 } });
    cxuiContainerAppend((CxUiWidget*)box, CxUiWidget(b));
    return b;
}

static bool pageGone(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    objRelease(&page->area);
    objRelease(&page->sample);
    objRelease(&page->scrolled);
    xaDestroy(&page);
    return true;
}

static void build(CxUiBox* box)
{
    page = xaAllocStruct(DrawnPage, XA_Zero);
    cchainAttach(&CxUiWidget(box)->destroyNotify, pageGone, stvNone);

    CxUiBox* top     = galleryRow(box, 6);
    CxUiDButton* off = drawnButton(top, _S"Disable everything below", true, NULL);
    cxuiOnChange(off, disabled, stvNone);
    objRelease(&off);
    objRelease(&top);

    page->area = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 8 });
    cxuiContainerAppend(box, page->area);
    CxUiBox* area = page->area;

    galleryHeading(area, _S"Buttons");
    CxUiBox* row = galleryRow(area, 4);
    static const strref titles[] = { _S"Drawn button", _S"With a &mnemonic", _S"Not enabled" };
    for (int i = 0; i < 3; i++) {
        CxUiDButton* b = drawnButton(row, titles[i], false, NULL);
        cxuiOnActivate(b, pressed, stvNone);
        if (i == 2)
            cxuiWidgetSetEnabled(b, false);
        objRelease(&b);
    }
    CxUiDButton* focusable = cxuiDButtonCreate(&(CxUiDButtonDesc){ .title = _S"Takes focus", .canFocus = true,
                                                                   .padding = { 8, 4 } });
    cxuiOnActivate(focusable, pressed, stvNone);
    galleryAdd(row, focusable);
    objRelease(&row);

    galleryHeading(area, _S"Toggles and a group");
    row = galleryRow(area, 4);
    static const strref toggles[] = { _S"Bold", _S"Italic" };
    for (int i = 0; i < 2; i++) {
        CxUiDButton* b = drawnButton(row, toggles[i], true, NULL);
        cxuiOnChange(b, toggled, stvNone);
        objRelease(&b);
    }
    galleryAdd(row, cxuiSpaceCreate(cxuiLen(16), cxuiLen(0)));
    CxUiDGroup* group           = cxuiDGroupCreate();
    static const strref names[] = { _S"Left", _S"Center", _S"Right" };
    static const uint32 aligns[] = { CXUI_ALIGN_Start, CXUI_ALIGN_Center, CXUI_ALIGN_End };
    for (int i = 0; i < 3; i++) {
        CxUiDButton* b = drawnButton(row, names[i], true, group);
        if (i == 0)
            cxuiDButtonSetValue(b, true);
        cxuiOnChange(b, aligned, stvar(uint32, aligns[i]));
        objRelease(&b);
    }
    objRelease(&group);
    objRelease(&row);
    page->sample = cxuiDLabelCreate(&(CxUiDLabelDesc){ .text = _S"The group places this label", .padding = { 6, 4 } });
    cxuiWidgetSetAlign(page->sample, CXUI_ALIGN_Start, CXUI_ALIGN_Default);
    cxuiContainerAppend(area, page->sample);

    galleryHeading(area, _S"Labels");
    CxUiDLabel* shortened = cxuiDLabelCreate(&(CxUiDLabelDesc){
        .text = _S"A drawn label too long for the room it has, shortened with an ellipsis", .ellipsis = true });
    cxuiWidgetSetNaturalSize(shortened, cxuiLen(260), cxuiLen(0));
    galleryAdd(area, shortened);
    CxUiDLabel* twoLines = cxuiDLabelCreate(&(CxUiDLabelDesc){ .text = _S"Two lines,\ncentered", .align = CXUI_TEXT_Center });
    galleryAdd(area, twoLines);

    galleryHeading(area, _S"Scroll bars");
    row = galleryRow(area, 8);
    CxUiDScrollBar* across = cxuiDScrollBarCreate(&(CxUiDScrollBarDesc){ .range = 1000, .page = 100, .step = 10 });
    cxuiWidgetSetNaturalSize(across, cxuiLen(240), cxuiLen(0));
    cxuiOnScroll(across, scrolled, stvar(bool, false));
    galleryAdd(row, across);
    CxUiDScrollBar* down = cxuiDScrollBarCreate(&(CxUiDScrollBarDesc){ .orient = CXUI_ORIENT_V, .range = 50, .page = 10 });
    cxuiWidgetSetNaturalSize(down, cxuiLen(0), cxuiLen(70));
    cxuiWidgetSetExpand(down, CXUI_EXPAND_None);
    cxuiOnScroll(down, scrolled, stvar(bool, true));
    galleryAdd(row, down);
    objRelease(&row);
    // Below the bars, so its changing width doesn't move them
    page->scrolled = cxuiDLabelCreate(&(CxUiDLabelDesc){ .text = _S"Drag a thumb, click a track, or turn the wheel" });
    cxuiContainerAppend(area, page->scrolled);

    galleryHeading(area, _S"Tabs");
    CxUiZBox* tabPages = cxuiZBoxCreate(NULL);
    static const strref tabTexts[] = { _S"The first tab's page", _S"The second tab's page", _S"The third tab's page" };
    for (int i = 0; i < 3; i++)
        galleryAdd(tabPages, cxuiDLabelCreate(&(CxUiDLabelDesc){ .text = tabTexts[i], .padding = { 8, 8 } }));
    CxUiDTabs* tabs = cxuiDTabsCreate(&(CxUiDTabsDesc){ .zbox = tabPages });
    cxuiDTabsAdd(tabs, _S"First");
    cxuiDTabsAdd(tabs, _S"Second");
    cxuiDTabsAdd(tabs, _S"Third");
    cxuiOnChange(tabs, tabChosen, stvNone);
    galleryAdd(area, tabs);
    galleryAdd(area, tabPages);
}

const GalleryPage galleryDrawn = {
    .name  = _S"drawn",
    .title = _S"Drawn widgets",
    .build = build,
};
