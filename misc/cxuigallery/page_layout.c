// Layout: a tool bar whose buttons share a width, over a split of a side panel and a frame of two
// pages. The tiles are canvases that show their own size.

#include "gallery.h"

#include <cxui/test/cxuitest.h>

// A canvas showing its label and its size in device-independent pixels
typedef struct Tile {
    CxUiCanvas* cv;   // released, and the tile freed, when the canvas is destroyed
    CxUiFont* font;
    string label;
    uint32 role;   // CxUiPaletteRole of its background
} Tile;

// Holds a reference to each widget it uses after building, released when the page goes
typedef struct LayoutPage {
    CxUiWidget* side;
    CxUiWidget* main;
    CxUiBox* tiles;
    CxUiZBox* pages;
    CxUiSplit* split;
    CxUiTimer* probe;   // reports how layout went, a moment after each action
    int32 added;
} LayoutPage;

static LayoutPage* page;

static CxUiResult tilePaint(stvlist* cvars, CxUiPaintEvent* ev)
{
    Tile* t     = stvlAtPtr(cvars, 0);
    CxUiDraw* d = ev->draw;
    CxUiSize sz = cxuiDrawSize(d);
    if (!t->font || cxuiFontScale(t->font) != cxuiCanvasScale(t->cv)) {
        objRelease(&t->font);
        t->font = cxuiFontCreateFor(t->cv, NULL);
    }
    // Everything from the palette, so the page follows the window's theme.
    CxUiPalette* pal = cxuiWidgetPalette(t->cv);
    bool on          = cxuiWidgetEffectiveEnabled(t->cv);
    cxuiDrawFill(d, 0, 0, sz.w, sz.h, cxuiPaletteColor(pal, t->role));
    cxuiDrawFrame(d, 0, 0, sz.w, sz.h, 1, cxuiPaletteColor(pal, CXUI_PAL_Border));
    string text = 0;
    strFormat(&text, _SL("${string}\n${int}x${int}"), stvar(strref, t->label), stvar(int32, sz.w), stvar(int32, sz.h));
    cxuiDrawTextBox(d, t->font, (CxUiRect){ 0, 0, sz.w, sz.h }, text, CXUI_TEXT_Center | CXUI_TEXT_Middle,
                    cxuiPaletteColor(pal, on ? CXUI_PAL_Text : CXUI_PAL_TextDisabled));
    strDestroy(&text);
    return CXUI_Handled;
}

static bool tileGone(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    Tile* t = stvlAtPtr(cvars, 0);
    objRelease(&t->font);
    objRelease(&t->cv);
    strDestroy(&t->label);
    xaFree(t);
    return true;
}

// A tile of a natural size in device-independent pixels (0 for none), appended to box; returns
// the canvas, borrowed.
static CxUiCanvas* tileAdd(void* box, strref label, int32 w, int32 h, uint32 role)
{
    Tile* t = xaAllocStruct(Tile, XA_Zero);
    t->role = role;
    strDup(&t->label, label);
    t->cv = cxuiCanvasCreate(&(CxUiCanvasDesc){ .units = CXUI_Units_Dip });
    cxuiOnPaint(t->cv, tilePaint, stvar(ptr, t));
    cchainAttach(&CxUiWidget(t->cv)->destroyNotify, tileGone, stvar(ptr, t));
    if (w || h) {
        cxuiWidgetSetExpand(t->cv, CXUI_EXPAND_None);
        cxuiWidgetSetNaturalSize(t->cv, cxuiLen(w), cxuiLen(h));
    }
    cxuiContainerAppend((CxUiWidget*)box, t->cv);
    return t->cv;
}

static CxUiResult layoutProbe(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    CxUiRect b    = { 0 };
    CxUiSize dips = { 0 };
    cxuiTestBounds(page->main, &b);
    cxuiWindowSize(galleryWindow(), &dips);
    say(_SL("layout: ${int} layout passes so far; the window is ${int}x${int}, the main area ${int}x${int}\n"),
        stvar(int32, cxuiTestLayoutPasses(galleryWindow())), stvar(int32, dips.w), stvar(int32, dips.h),
        stvar(int32, b.w), stvar(int32, b.h));
    return CXUI_Default;
}

static void pageSay(strref what)
{
    galleryStatus(_S"layout", what);
    if (!page->probe)
        page->probe = cxuiTimerCreate(timeMS(300), CXUI_Timer_OneShot, layoutProbe, stvNone);
    cxuiTimerStart(page->probe);
}

static void repaintTree(CxUiWidget* w)
{
    CxUiCanvas* cv = objDynCast(CxUiCanvas, w);
    if (cv)
        cxuiCanvasInvalidateAll(cv);
    for (int32 i = 0, n = cxuiContainerChildCount(w); i < n; i++)
        repaintTree(cxuiContainerChild(w, i));
}

static void actToggleSide(void)
{
    cxuiWidgetSetVisible(page->side, !cxuiWidgetVisible(page->side));
    pageSay(cxuiWidgetVisible(page->side) ? _S"side panel shown" : _S"side panel hidden");
}

static void actToggleEnabled(void)
{
    cxuiWidgetSetEnabled(page->main, !cxuiWidgetEnabled(page->main));
    // Canvases draw their own disabled look.
    repaintTree(page->main);
    pageSay(cxuiWidgetEnabled(page->main) ? _S"main area enabled" : _S"main area disabled");
}

static void actAdd(void)
{
    string label = 0;
    strFormat(&label, _SL("tile ${int}"), stvar(int32, ++page->added));
    tileAdd(page->tiles, label, 60 + (page->added % 3) * 20, 40, CXUI_PAL_Selection);
    pageSay(label);
    strDestroy(&label);
}

static void actPage(void)
{
    int32 next = (cxuiZBoxActive(page->pages) + 1) % 2;
    cxuiZBoxSetActive(page->pages, next);
    pageSay(next ? _S"page: grid" : _S"page: main");
}

static void actFit(void)
{
    // Exactly, smaller or larger; the split keeps its sash where it is as a share of the split.
    CxUiSize n  = { 0 };
    string what = 0;
    cxuiWindowFit(galleryWindow(), CXUI_FIT_Shrink);
    cxuiWindowSize(galleryWindow(), &n);
    strFormat(&what, _SL("window fitted to ${int}x${int}"), stvar(int32, n.w), stvar(int32, n.h));
    pageSay(what);
    strDestroy(&what);
}

static const strref toolNames[]       = { _S"Side panel", _S"Disable", _S"Add tile", _S"Page", _S"Fit" };
static void (*const toolActs[])(void) = { actToggleSide, actToggleEnabled, actAdd, actPage, actFit };

static CxUiResult toolPressed(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    toolActs[stvlAt(cvars, 0, int32)]();
    return CXUI_Default;
}

static CxUiResult splitMoved(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    string what = 0;
    strFormat(&what, _SL("split at ${int}%"), stvar(int32, (int32)(cxuiSplitValue(page->split) * 100 + 0.5f)));
    pageSay(what);
    strDestroy(&what);
    return CXUI_Default;
}

static bool pageGone(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    if (page->probe)
        cxuiTimerStop(page->probe);
    objRelease(&page->probe);
    objRelease(&page->side);
    objRelease(&page->main);
    objRelease(&page->tiles);
    objRelease(&page->pages);
    objRelease(&page->split);
    xaDestroy(&page);
    return true;
}

static void build(CxUiBox* box)
{
    page = xaAllocStruct(LayoutPage, XA_Zero);
    cchainAttach(&CxUiWidget(box)->destroyNotify, pageGone, stvNone);

    galleryAdd(box, cxuiLabelCreate(&(CxUiLabelDesc){
                        .text = _S"The buttons share the widest one's width. Drag the sash, hide the side panel, "
                                "disable the main area, add tiles until the window grows, or switch to a grid whose "
                                "middle cell takes the spare room. Fit sizes the window to this page.",
                        .wrap = true }));

    // The buttons share the widest one's width through a normalize group.
    CxUiBox* bar = galleryRow(box, 4);
    for (int32 i = 0; i < 5; i++) {
        CxUiDButton* b = cxuiDButtonCreate(&(CxUiDButtonDesc){ .title = toolNames[i], .padding = { 8, 4 } });
        cxuiOnActivate(b, toolPressed, stvar(int32, i));
        CxUiLayoutParams lp;
        cxuiWidgetLayout(b, &lp);
        lp.normalizeGroup = 1;
        cxuiWidgetSetLayout(b, &lp);
        galleryAdd(bar, b);
    }
    objRelease(&bar);

    page->split = cxuiSplitCreate(&(CxUiSplitDesc){ .value = 0.25f, .minPane = { 80, 260 }, .grip = true });
    cxuiWidgetSetExpand(page->split, CXUI_EXPAND_Both);
    cxuiOnChange(page->split, splitMoved, stvNone);
    cxuiContainerAppend(box, page->split);
    CxUiCanvas* side = tileAdd(page->split, _S"side", 120, 0, CXUI_PAL_Panel);
    page->side       = objAcquire(CxUiWidget(side));
    CxUiFrame* frame =cxuiFrameCreate(&(CxUiPanelDesc){ .margin = { 6, 6, 6, 6 } });
    cxuiContainerAppend(page->split, frame);
    page->pages = cxuiZBoxCreate(NULL);
    cxuiContainerAppend(frame, page->pages);
    objRelease(&frame);

    page->main = CxUiWidget(cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 4 }));
    cxuiContainerAppend(page->pages, page->main);
    CxUiCanvas* big = tileAdd(page->main, _S"main (expands)", 0, 0, CXUI_PAL_Content);
    cxuiWidgetSetMinSize(big, cxuiLen(240), cxuiLen(120));
    page->tiles = cxuiBoxCreate(&(CxUiBoxDesc){ .gap = 4 });
    cxuiContainerAppend(page->main, page->tiles);
    actAdd();

    // The second page, created when first shown: a grid whose middle column and row expand
    CxUiGridBox* grid = cxuiGridBoxCreate(&(CxUiGridBoxDesc){ .ncols = 3, .colGap = 4, .rowGap = 4 });
    cxuiContainerAppend(page->pages, grid);
    for (int i = 0; i < 9; i++) {
        string label     = 0;
        strFormat(&label, _SL("cell ${int}"), stvar(int32, i));
        CxUiCanvas* cell = tileAdd(grid, label, 60 + (i % 3) * 10, 30, i == 4 ? CXUI_PAL_Selection : CXUI_PAL_Panel);
        if (i == 4)
            cxuiWidgetSetExpand(cell, CXUI_EXPAND_Both);
        strDestroy(&label);
    }
    objRelease(&grid);
}

// The tiles draw from the palette, which the theme changes.
static void themed(void)
{
    repaintTree(page->side);
    repaintTree(CxUiWidget(page->pages));
}

const GalleryPage galleryLayout = {
    .name   = _S"layout",
    .title  = _S"Layout",
    .build  = build,
    .themed = themed,
};
