// Tabs: the platform's own tabs over pages, one built the first time it is picked, a title that
// marks its page changed, one renamed, and pages added, taken away and picked from code.

#include "gallery.h"

#include <cx/time.h>

// Holds a reference to each widget it uses after building, released when the page goes
typedef struct TabsPage {
    CxUiTabs* tabs;
    CxUiBox* lazy;   // filled the first time it is picked
    CxUiTextField* name;
    CxUiButton* disable;
    int32 added;
} TabsPage;

static TabsPage* page;

#define LAZY_ROWS 120

static void pageSay(strref what)
{
    galleryStatus(_S"tabs", what);
}

// A page's first showing builds it, which with this many native controls is worth putting off.
static void buildLazy(void)
{
    int64 start      = clockTimer();
    CxUiScrollBox* sb = cxuiScrollBoxCreate(&(CxUiScrollBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 4, .view = { 0, 240 } });
    cxuiWidgetSetExpand(sb, CXUI_EXPAND_Both);
    string text = 0;
    for (int32 i = 0; i < LAZY_ROWS; i++) {
        CxUiBox* row = galleryRow(sb, 6);
        strFormat(&text, _SL("Field ${int}:"), stvar(int32, i + 1));
        CxUiLabel* l = cxuiLabelCreate(&(CxUiLabelDesc){ .text = text });
        cxuiWidgetSetNaturalSize(l, cxuiLen(70), cxuiLen(0));
        galleryAdd(row, l);
        CxUiTextField* f = cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .columns = 20 });
        cxuiWidgetSetExpand(f, CXUI_EXPAND_H);
        galleryAdd(row, f);
        objRelease(&row);
    }
    galleryAdd(page->lazy, sb);
    strFormat(&text, _SL("built the page's ${int} controls in ${int} ms, before it is shown"), stvar(int32, LAZY_ROWS * 2),
              stvar(int32, (int32)((clockTimer() - start) / 1000)));
    pageSay(text);
    strDestroy(&text);
}

static CxUiResult picked(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    int32 at    = cxuiZBoxActive(page->tabs);
    string text = 0;
    strFormat(&text, _SL("picked tab ${int}, \"${string}\""), stvar(int32, at + 1), stvar(strref, cxuiTabsTitle(page->tabs, at)));
    pageSay(text);
    strDestroy(&text);
    if (cxuiContainerChild(page->tabs, at) == CxUiWidget(page->lazy) && cxuiContainerChildCount(page->lazy) == 0)
        buildLazy();
    return CXUI_Default;
}

static CxUiResult markChanged(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    CxUiCheck* mark = stvlAtObj(cvars, 0, CxUiCheck);
    bool on         = cxuiCheckValue(mark);
    cxuiTabsSetTitle(page->tabs, 2, on ? _S"Ma&rked \xE2\x80\xA2" : _S"Ma&rked");
    pageSay(on ? _S"the tab is marked changed" : _S"the tab's mark is gone");
    return CXUI_Default;
}

static CxUiResult retitle(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    strref name = cxuiTextFieldText(page->name);
    cxuiTabsSetTitle(page->tabs, 3, strEmpty(name) ? _S"Re&name" : name);
    pageSay(_S"renamed the tab");
    return CXUI_Default;
}

// A page with a line of text, for the ones added and the first
static CxUiBox* textPage(strref text)
{
    CxUiBox* pg = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6, .margin = { 10, 10, 10, 10 } });
    galleryAdd(pg, cxuiLabelCreate(&(CxUiLabelDesc){ .text = text, .wrap = true }));
    return pg;
}

static CxUiResult tool(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    int32 what  = stvlAt(cvars, 0, int32);
    int32 n     = cxuiContainerChildCount(page->tabs);
    string text = 0;
    switch (what) {
    case 0: {
        page->added++;
        strFormat(&text, _SL("Page ${int} was added after the window was shown."), stvar(int32, page->added));
        CxUiBox* pg = textPage(text);
        strFormat(&text, _SL("Added ${int}"), stvar(int32, page->added));
        cxuiTabsAdd(page->tabs, pg, text);
        objRelease(&pg);
        pageSay(_S"added a page at the end");
        break;
    }
    case 1:
        // The first four stay, so the rest of the page goes on working.
        if (n > 4) {
            cxuiDestroy(cxuiContainerChild(page->tabs, n - 1));
            pageSay(_S"took the last page away");
        } else {
            pageSay(_S"only the pages added can be taken away");
        }
        break;
    case 2:
        cxuiZBoxSetActive(page->tabs, (cxuiZBoxActive(page->tabs) + 1) % n);
        strFormat(&text, _SL("picked tab ${int} from code, which raises no change"),
                  stvar(int32, cxuiZBoxActive(page->tabs) + 1));
        pageSay(text);
        break;
    case 3: {
        bool on = !cxuiWidgetEnabled(page->tabs);
        cxuiWidgetSetEnabled(page->tabs, on);
        cxuiButtonSetTitle(page->disable, on ? _S"Disable the tabs" : _S"Enable the tabs");
        pageSay(on ? _S"the tabs are enabled" : _S"the tabs are disabled");
        break;
    }
    }
    strDestroy(&text);
    return CXUI_Default;
}

static CxUiButton* toolButton(CxUiBox* row, strref title, int32 what)
{
    CxUiButton* b = cxuiButtonCreate(&(CxUiButtonDesc){ .title = title });
    cxuiOnActivate(b, tool, stvar(int32, what));
    cxuiContainerAppend(row, b);
    return b;
}

static bool pageGone(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    objRelease(&page->tabs);
    objRelease(&page->lazy);
    objRelease(&page->name);
    objRelease(&page->disable);
    xaDestroy(&page);
    return true;
}

static void build(CxUiBox* pageBox)
{
    page = xaAllocStruct(TabsPage, XA_Zero);
    cchainAttach(&CxUiWidget(pageBox)->destroyNotify, pageGone, stvNone);

    galleryHeading(pageBox, _S"Tabs");
    page->tabs = cxuiTabsCreate(NULL);
    cxuiWidgetSetExpand(page->tabs, CXUI_EXPAND_Both);
    cxuiOnChange(page->tabs, picked, stvNone);

    CxUiBox* general = textPage(_S"Each page has a tab. Ctrl+Tab and Ctrl+Shift+Tab, or Ctrl+Page Down and Up, go "
                                "round them from anywhere on a page. On Linux, Alt with a tab's underlined letter picks "
                                "it too; Windows' tabs have no underlined letters.");
    CxUiTextField* f = cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .cue = _S"A field on the first page", .columns = 30 });
    galleryAdd(general, f);
    cxuiTabsAdd(page->tabs, general, _S"&General");
    objRelease(&general);

    page->lazy = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .margin = { 10, 10, 10, 10 } });
    cxuiTabsAdd(page->tabs, page->lazy, _S"La&zy");

    CxUiBox* marked = textPage(_S"A title can change at any time, such as to show its page has changes.");
    CxUiCheck* mark = cxuiCheckCreate(&(CxUiCheckDesc){ .title = _S"&Mark this page's tab" });
    cxuiOnChange(mark, markChanged, stvar(object, mark));
    galleryAdd(marked, mark);
    cxuiTabsAdd(page->tabs, marked, _S"Ma&rked");
    objRelease(&marked);

    CxUiBox* named = textPage(_S"Type a title for this page's tab.");
    CxUiBox* row   = galleryRow(named, 6);
    page->name     = cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .text = _S"Re&name", .columns = 20 });
    cxuiContainerAppend(row, page->name);
    CxUiButton* apply = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"Set the &title" });
    cxuiOnActivate(apply, retitle, stvNone);
    galleryAdd(row, apply);
    objRelease(&row);
    cxuiTabsAdd(page->tabs, named, _S"Re&name");
    objRelease(&named);
    cxuiContainerAppend(pageBox, page->tabs);

    CxUiBox* tools = galleryRow(pageBox, 6);
    CxUiButton* b  = toolButton(tools, _S"Add a page", 0);
    objRelease(&b);
    b = toolButton(tools, _S"Take the last away", 1);
    objRelease(&b);
    b = toolButton(tools, _S"Next tab from code", 2);
    objRelease(&b);
    page->disable = toolButton(tools, _S"Disable the tabs", 3);
    objRelease(&tools);

    // Tabs on several lines: narrow the window and they take more lines
    galleryHeading(pageBox, _S"On several lines");
    CxUiTabs* wrapped = cxuiTabsCreate(&(CxUiTabsDesc){ .flags = CXUI_TABS_Multiline });
    static const strref groups[] = { _S"Overview", _S"Settings", _S"Colors", _S"Layout", _S"Advanced Options",
                                     _S"Notes", _S"History", _S"Shortcuts", _S"Fonts", _S"Network", _S"Storage",
                                     _S"Display Settings", _S"Sound", _S"Plugins", _S"Logs", _S"About" };
    for (int32 i = 0; i < (int32)(sizeof(groups) / sizeof(groups[0])); i++) {
        CxUiBox* p = textPage(_S"Windows and macOS put tabs that don't fit on more lines; GTK scrolls them.");
        cxuiTabsAdd(wrapped, p, groups[i]);
        objRelease(&p);
    }
    galleryAdd(pageBox, wrapped);
}

const GalleryPage galleryTabs = {
    .name  = _S"tabs",
    .title = _S"Tabs",
    .build = build,
};
