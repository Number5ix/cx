// The gallery's main window: a menu bar, a list of pages beside the page shown, the theme every
// gallery window takes, and a status line; and the console, which can show a page by name, open
// every page in turn, and quit.

#include "gallery.h"

#include <cxui/entry.h>
#include <cxui/test/cxuitest.h>

CXUI_DEFINE_ENTRY_POINT;

static const GalleryPage* const pages[] = {
    &galleryNative, &galleryValues, &galleryDrawn,   &galleryText,   &galleryLayout,  &galleryTabs,
    &galleryGrid,   &galleryCanvas, &galleryDesktop, &galleryModals, &galleryWindows,
#if defined(CXUIGALLERY_LUA)
    &galleryLua,
#endif
};
#define NPAGES ((int32)(sizeof(pages) / sizeof(pages[0])))

// Holds a reference to each widget it uses after building, released when the window goes. Its
// UI-thread parts are touched only on the UI thread.
typedef struct Gallery {
    Event done;
    CxUiWindow* w;
    CxUiZBox* zbox;
    CxUiBox* boxes[NPAGES];
    CxUiDButton* entries[NPAGES];
    bool built[NPAGES];
    int32 shown;
    CxUiDButton* themes[3];   // by CxUiThemeMode
    CxUiMenuItem* themeItems[3];
    CxUiMenu* recent;
    sa_string recentNames;
    int32 recents;
    CxUiPanel* statusBar;
    CxUiLabel* status;
    uint32 theme;
    sa_ptr followers;   // Weak(CxUiWindow)*
} Gallery;

static Gallery g;

// ---- for the pages ----

CxUiWindow* galleryWindow(void)
{
    return g.w;
}

void galleryStatus(strref page, strref text)
{
    string line = 0;
    strFormat(&line, _SL("${string}: ${string}"), stvar(strref, page), stvar(strref, text));
    if (g.status)
        cxuiLabelSetText(g.status, line);
    say(_SL("${string}\n"), stvar(string, line));
    strDestroy(&line);
}

void galleryFollowTheme(CxUiWindow* w)
{
    cxuiWindowSetThemeMode(w, g.theme);
    saPush(&g.followers, ptr, objGetWeak(CxUiWindow, w));
}

void galleryAdd(void* box, void* w)
{
    CxUiWidget* child = w;
    cxuiContainerAppend((CxUiWidget*)box, child);
    objRelease(&child);
}

CxUiBox* galleryRow(void* box, int32 gap)
{
    CxUiBox* row = cxuiBoxCreate(&(CxUiBoxDesc){ .gap = gap, .align = { 0, CXUI_ALIGN_Center } });
    cxuiContainerAppend((CxUiWidget*)box, CxUiWidget(row));
    return row;
}

void galleryHeading(void* box, strref text)
{
    CxUiLabel* l = cxuiLabelCreate(&(CxUiLabelDesc){ .text = text });
    cxuiWidgetSetFont(l, &(CxUiFontDesc){ .flags = CXUI_FONT_Bold });
    galleryAdd(box, l);
}

// ---- window events ----

static strref titleOf(CxUiEvent* ev)
{
    return cxuiWindowTitle(ev->window);
}

static CxUiResult onResize(stvlist* cvars, CxUiResizeEvent* ev)
{
    unused_noeval(cvars);
    say(_SL("${string}: resized to ${int}x${int} px\n"), stvar(strref, titleOf(&ev->base)), stvar(int32, ev->size.w),
        stvar(int32, ev->size.h));
    return CXUI_Default;
}

static CxUiResult onMove(stvlist* cvars, CxUiMoveEvent* ev)
{
    unused_noeval(cvars);
    say(_SL("${string}: moved to ${int},${int}\n"), stvar(strref, titleOf(&ev->base)), stvar(int32, ev->pos.x),
        stvar(int32, ev->pos.y));
    return CXUI_Default;
}

static CxUiResult onShowState(stvlist* cvars, CxUiShowStateEvent* ev)
{
    unused_noeval(cvars);
    static const strref states[] = { _S"normal", _S"minimized", _S"maximized" };
    say(_SL("${string}: ${string}\n"), stvar(strref, titleOf(&ev->base)),
        stvar(strref, ev->state < 3 ? states[ev->state] : _S"?"));
    return CXUI_Default;
}

static CxUiResult onFocus(stvlist* cvars, CxUiFocusEvent* ev)
{
    unused_noeval(cvars);
    say(_SL("${string}: ${string}\n"), stvar(strref, titleOf(&ev->base)),
        stvar(strref, ev->gained ? _S"activated" : _S"deactivated"));
    return CXUI_Default;
}

static CxUiResult onClose(stvlist* cvars, CxUiCloseEvent* ev)
{
    unused_noeval(cvars);
    say(_SL("${string}: closed\n"), stvar(strref, titleOf(&ev->base)));
    return CXUI_Default;
}

void galleryLogWindow(CxUiWindow* w)
{
    cxuiOnResize(w, onResize, stvNone);
    cxuiOnMove(w, onMove, stvNone);
    cxuiOnShowState(w, onShowState, stvNone);
    cxuiOnFocus(w, onFocus, stvNone);
    cxuiOnClose(w, onClose, stvNone);
}

// ---- the main window ----

// Built the first time it is shown; the window's defaults are the page's to set.
static void showPage(int32 i)
{
    if (i < 0 || i >= NPAGES || !g.w)
        return;
    if (!g.built[i]) {
        g.built[i] = true;
        pages[i]->build(g.boxes[i]);
    }
    cxuiZBoxSetActive(g.zbox, i);
    cxuiDButtonSetValue(g.entries[i], true);
    cxuiWindowSetDefaultEnter(g.w, CxUiWidgetNone);
    cxuiWindowSetDefaultEsc(g.w, CxUiWidgetNone);
    g.shown = i;
    if (pages[i]->shown)
        pages[i]->shown();
    galleryStatus(pages[i]->name, _S"shown");
}

static CxUiResult entryChosen(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    showPage(stvlAt(cvars, 0, int32));
    return CXUI_Default;
}

static void setTheme(uint32 mode)
{
    static const strref names[] = { _S"system", _S"light", _S"dark" };
    g.theme = mode;
    cxuiWindowSetThemeMode(g.w, mode);
    for (int32 i = saSize(g.followers) - 1; i >= 0; i--) {
        Weak(CxUiWindow)* weak = g.followers.a[i];
        CxUiWindow* w          = objAcquireFromWeak(CxUiWindow, weak);
        if (w) {
            cxuiWindowSetThemeMode(w, mode);
            objRelease(&w);
        } else {
            objDestroyWeak(&weak);
            saRemove(&g.followers, i);
        }
    }
    if (g.themeItems[mode])
        cxuiMenuItemSetChecked(g.themeItems[mode], true);
    galleryStatus(_S"gallery", names[mode]);
}

static CxUiResult themeChosen(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    setTheme(stvlAt(cvars, 0, int32));
    return CXUI_Default;
}

static CxUiResult windowThemed(stvlist* cvars, CxUiThemeChangedEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    for (int32 i = 0; i < NPAGES; i++) {
        if (g.built[i] && pages[i]->themed)
            pages[i]->themed();
    }
    return CXUI_Default;
}

static CxUiResult windowScaled(stvlist* cvars, CxUiDpiChangedEvent* ev)
{
    unused_noeval(cvars);
    say(_SL("gallery: scale ${float} -> ${float}\n"), stvar(float64, ev->oldScale), stvar(float64, ev->scale));
    for (int32 i = 0; i < NPAGES; i++) {
        if (g.built[i] && pages[i]->scaled)
            pages[i]->scaled();
    }
    return CXUI_Default;
}

static bool windowGone(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    for (int32 i = 0; i < NPAGES; i++) {
        objRelease(&g.boxes[i]);
        objRelease(&g.entries[i]);
    }
    for (int32 i = 0; i < 3; i++)
        objRelease(&g.themes[i]);
    objRelease(&g.recent);
    saDestroy(&g.recentNames);
    objRelease(&g.statusBar);
    for (int32 i = 0; i < saSize(g.followers); i++) {
        Weak(CxUiWindow)* weak = g.followers.a[i];
        objDestroyWeak(&weak);
    }
    saDestroy(&g.followers);
    objRelease(&g.status);
    objRelease(&g.zbox);
    objRelease(&g.w);
    eventSignal(&g.done);
    return true;
}

static CxUiDButton* listEntry(void* box, strref title, CxUiDGroup* group, CxUiChangeFn fn, int32 value)
{
    CxUiDButton* b = cxuiDButtonCreate(&(CxUiDButtonDesc){ .title = title, .toggle = true, .group = group,
                                                           .padding = { 10, 4 } });
    cxuiWidgetSetAlign(b, CXUI_ALIGN_Fill, CXUI_ALIGN_Default);
    cxuiOnChange(b, fn, stvar(int32, value));
    cxuiContainerAppend((CxUiWidget*)box, CxUiWidget(b));
    return b;
}

static void sidebarHeading(void* box, strref text)
{
    CxUiDLabel* l = cxuiDLabelCreate(&(CxUiDLabelDesc){ .text = text, .padding = { 4, 2 } });
    cxuiWidgetSetFont(l, &(CxUiFontDesc){ .flags = CXUI_FONT_Bold });
    galleryAdd(box, l);
}

// ---- menus ----

static CxUiResult menuRecent(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    galleryStatus(_S"menu", cxuiMenuItemTitle((CxUiMenuItem*)ev->base.current));
    return CXUI_Default;
}

// Rebuilds Open Recent from the names, newest first, ahead of its separator and Clear
static void fillRecent(void)
{
    while (cxuiContainerChildCount(g.recent) > 2)
        cxuiDestroy(cxuiContainerChild(g.recent, 0));
    for (int32 i = 0; i < saSize(g.recentNames); i++) {
        string title = 0;
        strFormat(&title, _SL("&${int} ${string}"), stvar(int32, i + 1), stvar(string, g.recentNames.a[i]));
        CxUiMenuItem* item = cxuiMenuItemCreate(&(CxUiMenuItemDesc){ .title = title });
        cxuiOnActivate(item, menuRecent, stvNone);
        cxuiContainerInsert(g.recent, item, i);
        objRelease(&item);
        strDestroy(&title);
    }
}

// Adds a made-up file at the top of Open Recent, keeping five
static CxUiResult menuAddRecent(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    string name = 0;
    strFormat(&name, _SL("file${int}.txt"), stvar(int32, ++g.recents));
    saInsert(&g.recentNames, 0, string, name);
    strDestroy(&name);
    while (saSize(g.recentNames) > 5)
        saRemove(&g.recentNames, saSize(g.recentNames) - 1);
    fillRecent();
    galleryStatus(_S"menu", _S"a recent file added");
    return CXUI_Default;
}

static CxUiResult menuClearRecent(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    saClear(&g.recentNames);
    fillRecent();
    galleryStatus(_S"menu", _S"recent files cleared");
    return CXUI_Default;
}

static CxUiResult menuExit(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    cxuiWindowClose(g.w);
    return CXUI_Default;
}

static CxUiResult menuTheme(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    int32 mode = stvlAt(cvars, 0, int32);
    cxuiDButtonSetValue(g.themes[mode], true);
    setTheme(mode);
    return CXUI_Default;
}

static CxUiResult menuStatusBar(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    cxuiWidgetSetVisible(g.statusBar, cxuiMenuItemChecked((CxUiMenuItem*)ev->base.current));
    return CXUI_Default;
}

static CxUiResult menuAbout(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    cxuiMessageBox(&(CxUiMessageBoxDesc){ .parent = g.w, .title = _S"About", .kind = CXUI_MB_Info,
                                          .text = _S"cxui gallery: every kind of widget cxui has." });
    return CXUI_Default;
}

static CxUiMenu* buildMenus(void)
{
    CxUiMenu* file = cxuiMenuCreate();
    g.recent       = cxuiMenuCreate();
    cxuiMenuAppendSeparator(g.recent);
    cxuiOnActivate(cxuiMenuAppendItem(g.recent, .title = _S"&Clear the List"), menuClearRecent, stvNone);
    // Ctrl+R, or Command+R on macOS
    string hint = 0;
    cxuiKeyLabel(&hint, CXUI_KEY_R, CXUI_MOD_Primary);
    cxuiOnActivate(cxuiMenuAppendItem(file, .title = _S"&Add a Recent File", .hint = hint), menuAddRecent, stvNone);
    strDestroy(&hint);
    cxuiMenuAppendItem(file, .title = _S"Open &Recent", .submenu = g.recent);
    cxuiMenuAppendSeparator(file);
    cxuiOnActivate(cxuiMenuAppendItem(file, .title = _S"E&xit"), menuExit, stvNone);

    CxUiMenu* theme = cxuiMenuCreate();
    static const strref names[] = { _S"&System", _S"&Light", _S"&Dark" };
    for (int32 i = 0; i < 3; i++) {
        g.themeItems[i] = cxuiMenuAppendItem(theme, .title = names[i], .kind = CXUI_MENU_Radio, .checked = i == (int32)g.theme);
        cxuiOnActivate(g.themeItems[i], menuTheme, stvar(int32, i));
    }
    CxUiMenu* view = cxuiMenuCreate();
    cxuiMenuAppendItem(view, .title = _S"&Theme", .submenu = theme);
    cxuiOnActivate(cxuiMenuAppendItem(view, .title = _S"&Status Bar", .kind = CXUI_MENU_Check, .checked = true),
                   menuStatusBar, stvNone);

    CxUiMenu* help = cxuiMenuCreate();
    cxuiOnActivate(cxuiMenuAppendItem(help, .title = _S"&About"), menuAbout, stvNone);

    CxUiMenu* bar = cxuiMenuCreate();
    cxuiMenuAppendItem(bar, .title = _S"&File", .submenu = file);
    cxuiMenuAppendItem(bar, .title = _S"&View", .submenu = view);
    cxuiMenuAppendItem(bar, .title = _S"&Help", .submenu = help);
    objRelease(&file);
    objRelease(&theme);
    objRelease(&view);
    objRelease(&help);
    return bar;
}

// Add a Recent File's shortcut, which the menu only shows
static CxUiResult windowKey(stvlist* cvars, CxUiKeyEvent* ev)
{
    if (ev->down && cxuiKeyMatches(CXUI_KEY_R, CXUI_MOD_Primary, ev->key, ev->mods)) {
        menuAddRecent(cvars, NULL);
        return CXUI_Handled;
    }
    return CXUI_Default;
}

static bool galleryBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    saInit(&g.followers, ptr, 8);
    saInit(&g.recentNames, string, 8);
    g.shown = -1;
    // Shrink: dragged narrower than a page, the page gives up space and wraps its text.
    g.w = cxuiWindowCreate(&(CxUiWindowDesc){ .title       = _S"cxui gallery",
                                              .size        = { 940, 620 },
                                              .sizing      = CXUI_SIZE_Start,
                                              .flags       = CXUI_WINDOW_Shrink,
                                              .closeAction = CXUI_OnClose_Destroy });
    galleryLogWindow(g.w);
    CxUiMenu* menus = buildMenus();
    cxuiWindowSetMenu(g.w, menus);
    objRelease(&menus);
    cxuiOnKey(g.w, windowKey, stvNone);
    cxuiOnThemeChanged(g.w, windowThemed, stvNone);
    cxuiOnDpiChanged(g.w, windowScaled, stvNone);
    cchainAttach(&CxUiWidget(g.w)->destroyNotify, windowGone, stvNone);

    CxUiBox* col = cxuiBoxCreateV();
    CxUiBox* row = cxuiBoxCreateH();
    cxuiWidgetSetExpand(row, CXUI_EXPAND_Both);

    // The list: the theme every gallery window takes, then the pages
    CxUiPanel* side = cxuiPanelCreate(&(CxUiPanelDesc){ .orient = CXUI_ORIENT_V, .gap = 2, .margin = { 6, 6, 6, 6 },
                                                        .bgRole = CXUI_PAL_Panel });
    cxuiWidgetSetAlign(side, CXUI_ALIGN_Default, CXUI_ALIGN_Fill);
    sidebarHeading(side, _S"Theme");
    static const strref themeNames[] = { _S"System", _S"Light", _S"Dark" };
    CxUiDGroup* themeGroup = cxuiDGroupCreate();
    for (int32 i = 0; i < 3; i++)
        g.themes[i] = listEntry(side, themeNames[i], themeGroup, themeChosen, i);
    cxuiDButtonSetValue(g.themes[CXUI_Theme_System], true);
    galleryAdd(side, cxuiSpaceCreate(cxuiLen(0), cxuiLen(10)));
    sidebarHeading(side, _S"Pages");
    CxUiDGroup* pageGroup = cxuiDGroupCreate();
    for (int32 i = 0; i < NPAGES; i++)
        g.entries[i] = listEntry(side, pages[i]->title, pageGroup, entryChosen, i);
    objRelease(&themeGroup);
    objRelease(&pageGroup);
    galleryAdd(row, side);

    g.zbox = cxuiZBoxCreate(&(CxUiZBoxDesc){ .flags = CXUI_ZBOX_SizeToActive });
    cxuiWidgetSetExpand(g.zbox, CXUI_EXPAND_Both);
    // A page scrolls when the window is too small for it.
    for (int32 i = 0; i < NPAGES; i++) {
        g.boxes[i] = CxUiBox(cxuiScrollBoxCreate(
            &(CxUiScrollBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 8, .margin = { 14, 12, 14, 12 } }));
        cxuiContainerAppend(g.zbox, g.boxes[i]);
    }
    cxuiContainerAppend(row, g.zbox);
    galleryAdd(col, row);

    g.statusBar = cxuiPanelCreate(&(CxUiPanelDesc){ .margin = { 8, 4, 8, 4 }, .bgRole = CXUI_PAL_Panel });
    g.status    = cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S" ", .ellipsis = CXUI_ELLIPSIS_End });
    cxuiWidgetSetNaturalSize(g.status, cxuiLen(200), cxuiLen(0));
    cxuiWidgetSetExpand(g.status, CXUI_EXPAND_H);
    cxuiContainerAppend(g.statusBar, g.status);
    cxuiContainerAppend(col, g.statusBar);
    galleryAdd(g.w, col);

    showPage(0);
    cxuiWindowShow(g.w, CXUI_SHOW_Normal);
    return true;
}

// ---- console ----

static bool cmdPage(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    showPage(stvlAt(cvars, 0, int32));
    return true;
}

// Clicks a theme's entry in the list, as the user would
static void pressTheme(int32 mode)
{
    if (!g.w)
        return;
    CxUiCanvas* cv = CxUiCanvas(g.themes[mode]);
    cxuiTestPointer(cv, CXUI_TESTPTR_Move, 0, 0, 0, 4, 4);
    cxuiTestPointer(cv, CXUI_TESTPTR_Press, CXUI_BTN_Left, CXUI_BTN_Left, 0, 4, 4);
    cxuiTestPointer(cv, CXUI_TESTPTR_Release, CXUI_BTN_Left, 0, 0, 4, 4);
    cxuiTestPointer(cv, CXUI_TESTPTR_Left, 0, 0, 0, -1, -1);
}

static bool cmdTheme(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    pressTheme(stvlAt(cvars, 0, int32));
    return true;
}

// Every page in turn, each built and laid out on screen, then each theme, then the end
static bool cmdSmoke(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    for (int32 i = 0; i < NPAGES && g.w; i++) {
        showPage(i);
        cxuiRefresh(g.w);
        cxuiFlush();
    }
    static const int32 order[] = { CXUI_Theme_Dark, CXUI_Theme_Light, CXUI_Theme_System };
    for (int32 i = 0; i < 3 && g.w; i++) {
        pressTheme(order[i]);
        cxuiRefresh(g.w);
        cxuiFlush();
    }
    say(_SL("smoke: ${int} pages opened, 3 themes chosen\n"), stvar(int32, NPAGES));
    eventSignal(&g.done);
    return true;
}

static void help(void)
{
    conPutsz(conOut(), "commands:\n"
                       "  page <name>  shows a page\n"
                       "  pages        lists the pages\n"
                       "  theme <name> chooses system, light or dark\n"
                       "  smoke        shows every page and each theme in turn, then quits\n"
                       "  quit         closes everything\n");
    conFlush(conOut());
}

static void listPages(void)
{
    for (int32 i = 0; i < NPAGES; i++)
        say(_SL("  ${string}: ${string}\n"), stvar(strref, pages[i]->name), stvar(strref, pages[i]->title));
}

static int consoleThread(Thread* self)
{
    unused_noeval(self);
    string line = 0, name = 0;
    help();
    while (conReadLine(conIn(), &line)) {
        strTrim(&line, line, 0);
        if (strBeginsWithi(line, _SL("page "))) {
            strSubStr(&name, line, 5, strEnd);
            strTrim(&name, name, 0);
            int32 found = -1;
            for (int32 i = 0; i < NPAGES && found < 0; i++) {
                if (strEqi(name, pages[i]->name))
                    found = i;
            }
            if (found >= 0)
                cxuiPost(closureCreate(cmdPage, stvar(int32, found)));
            else
                listPages();
        } else if (strBeginsWithi(line, _SL("theme "))) {
            static const strref themes[] = { _S"system", _S"light", _S"dark" };
            strSubStr(&name, line, 6, strEnd);
            strTrim(&name, name, 0);
            int32 found = -1;
            for (int32 i = 0; i < 3 && found < 0; i++) {
                if (strEqi(name, themes[i]))
                    found = i;
            }
            if (found >= 0)
                cxuiPost(closureCreate(cmdTheme, stvar(int32, found)));
            else
                help();
        } else if (strEqi(line, _SL("pages"))) {
            listPages();
        } else if (strEqi(line, _SL("smoke"))) {
            cxuiPost(closureCreate(cmdSmoke, stvNone));
        } else if (strEqi(line, _SL("quit"))) {
            eventSignal(&g.done);
            break;
        } else if (!strEmpty(line)) {
            help();
        }
    }
    strDestroy(&name);
    strDestroy(&line);
    return 0;
}

static bool quitRequested(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    say(_SL("quit requested\n"), stvNone);
    eventSignal(&g.done);
    return true;
}

static bool uiStopping(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    say(_SL("the UI is stopping\n"), stvNone);
    return true;
}

int entryPoint(void)
{
    if (!cxuiStart(&(CxUiStartDesc){ .name = _S"Gallery UI" })) {
        conPuts(conErr(), _SL("cxui couldn't start; is there a display?\n"));
        return 1;
    }

    eventInit(&g.done);
    cchainAttach(&cxuiQuitRequestNotify, quitRequested, stvNone);
    // What a library sharing the program's UI would use to take its windows down
    cchainAttach(&cxuiShutdownNotify, uiStopping, stvNone);
    cxuiCallSync(closureCreate(galleryBuild, stvNone));
    // Not waited for: it may be blocked reading the console when the window closes.
    Thread* console = thrCreate(consoleThread, _S"Console", stvNone);
    eventWait(&g.done);
    cchainDetach(&cxuiQuitRequestNotify, quitRequested, 0);
    cxuiShutdown();
    thrRelease(&console);
    eventDestroy(&g.done);
    return 0;
}
