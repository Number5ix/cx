// The desktop: the monitors, the clipboard, and saving and restoring a window's placement.

#include "gallery.h"

// Holds a reference to each widget it uses after building, released when the page goes
typedef struct DesktopPage {
    CxUiLabel* monitors;
    CxUiTextField* text;
    CxUiPlacement saved;
} DesktopPage;

static DesktopPage* page;

static void pageSay(strref what)
{
    galleryStatus(_S"desktop", what);
}

static void sayPlacement(strref verb, const CxUiPlacement* pl)
{
    static const strref states[] = { _S"normal", _S"minimized", _S"maximized", _S"fullscreen" };
    string msg                   = 0;
    strFormat(&msg, _SL("${string} ${int},${int} ${int}x${int}, ${string}${string}"), stvar(strref, verb),
              stvar(int32, pl->normal.x), stvar(int32, pl->normal.y), stvar(int32, pl->normal.w),
              stvar(int32, pl->normal.h), stvar(strref, pl->state < 4 ? states[pl->state] : _S"?"),
              stvar(strref, (pl->valid & CXUI_PLACE_Position) ? _S"" : _S" (no position on this platform)"));
    pageSay(msg);
    strDestroy(&msg);
}

static void listMonitors(void)
{
    sa_CxUiMonitor mons = { 0 };
    string text = 0, line = 0;
    cxuiMonitors(&mons);
    for (int32 i = 0; i < saSize(mons); i++) {
        CxUiMonitor* m = &mons.a[i];
        strFormat(&line, _SLL("${string}Monitor ${int}: ${int},${int} ${int}x${int}, work area ${int},${int} ${int}x${int}, scale ${float}${string}"
                              "\n    \"${string}\" at ${float} Hz, id ${string}, safe area ${int},${int},${int},${int} in"),
                  stvar(strref, i ? _S"\n" : _S""), stvar(int32, i + 1), stvar(int32, m->bounds.x),
                  stvar(int32, m->bounds.y), stvar(int32, m->bounds.w), stvar(int32, m->bounds.h),
                  stvar(int32, m->workArea.x), stvar(int32, m->workArea.y), stvar(int32, m->workArea.w),
                  stvar(int32, m->workArea.h), stvar(float64, m->scale),
                  stvar(strref, (m->flags & CXUI_MON_Primary) ? _S", primary" : _S""), stvar(strref, m->name),
                  stvar(float64, m->refreshHz), stvar(strref, strEmpty(m->id) ? _S"unknown" : (strref)m->id),
                  stvar(int32, m->safeArea.left), stvar(int32, m->safeArea.top), stvar(int32, m->safeArea.right),
                  stvar(int32, m->safeArea.bottom));
        strAppend(&text, line);
    }
    cxuiLabelSetText(page->monitors, saSize(mons) ? (strref)text : _S"No monitors");
    saDestroy(&mons);
    strDestroy(&line);
    strDestroy(&text);
}

// The list follows the monitors as they change, while the page is there.
static bool monitorsChanged(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    if (!page)
        return true;
    listMonitors();
    pageSay(_S"the monitors changed");
    return true;
}

// A small window, placed where Save put the main one before it is first shown
static void openPlaced(void)
{
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui gallery: placed", .size = { 360, 200 },
                                                        .closeAction = CXUI_OnClose_Destroy });
    galleryFollowTheme(w);
    CxUiBox* col = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .margin = { 12, 12, 12, 12 } });
    galleryAdd(col, cxuiLabelCreate(&(CxUiLabelDesc){
                        .text = _S"This window took the saved placement before it was shown, so it should appear "
                                "there without moving.",
                        .wrap = true }));
    galleryAdd(w, col);
    cxuiWindowSetPlacement(w, &page->saved);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    sayPlacement(_S"opened a window at", &page->saved);
    objRelease(&w);   // cxui keeps shown windows alive
}

static CxUiResult action(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    string text = 0;
    CxUiPlacement pl;
    switch (stvlAt(cvars, 0, int32)) {
    case 0:
        listMonitors();
        pageSay(_S"monitors listed again");
        break;
    case 1:
        pageSay(cxuiClipboardSetText(cxuiTextFieldText(page->text)) ? _S"copied" : _S"couldn't copy");
        break;
    case 2:
        if (cxuiClipboardGetText(&text)) {
            cxuiTextFieldSetText(page->text, text);
            pageSay(_S"pasted");
        } else {
            pageSay(_S"no text on the clipboard");
        }
        break;
    case 3:
        if (cxuiWindowPlacement(galleryWindow(), &page->saved))
            sayPlacement(_S"saved", &page->saved);
        break;
    case 4:
        if (page->saved.valid) {
            cxuiWindowSetPlacement(galleryWindow(), &page->saved);
            sayPlacement(_S"restored", &page->saved);
        } else {
            pageSay(_S"nothing saved yet");
        }
        break;
    case 5:
        if (page->saved.valid)
            openPlaced();
        else
            pageSay(_S"nothing saved yet");
        break;
    case 6:
        pl = (CxUiPlacement){ .normal = { -30000, -30000, 940, 620 }, .valid = CXUI_PLACE_Position | CXUI_PLACE_Size };
        cxuiWindowSetPlacement(galleryWindow(), &pl);
        cxuiWindowPlacement(galleryWindow(), &pl);
        sayPlacement(_S"sent off screen, came back at", &pl);
        break;
    }
    strDestroy(&text);
    return CXUI_Default;
}

static void button(void* row, strref title, int32 what)
{
    CxUiButton* b = cxuiButtonCreate(&(CxUiButtonDesc){ .title = title });
    cxuiOnActivate(b, action, stvar(int32, what));
    galleryAdd(row, b);
}

static bool pageGone(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    cchainDetach(&cxuiMonitorsChangedNotify, monitorsChanged, 0);
    objRelease(&page->monitors);
    objRelease(&page->text);
    xaDestroy(&page);
    return true;
}

static void build(CxUiBox* box)
{
    page = xaAllocStruct(DesktopPage, XA_Zero);
    cchainAttach(&CxUiWidget(box)->destroyNotify, pageGone, stvNone);

    galleryHeading(box, _S"Monitors");
    page->monitors = cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S" " });
    cxuiContainerAppend(box, page->monitors);
    listMonitors();
    cchainAttach(&cxuiMonitorsChangedNotify, monitorsChanged, stvNone);
    CxUiBox* row = galleryRow(box, 6);
    button(row, _S"&List again", 0);
    objRelease(&row);

    galleryHeading(box, _S"Clipboard");
    row        = galleryRow(box, 6);
    page->text = cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .text = _S"Copy me", .columns = 30 });
    cxuiWidgetSetExpand(page->text, CXUI_EXPAND_H);
    cxuiContainerAppend(row, page->text);
    button(row, _S"&Copy", 1);
    button(row, _S"&Paste", 2);
    objRelease(&row);

    galleryHeading(box, _S"Placement");
    galleryAdd(box, cxuiLabelCreate(&(CxUiLabelDesc){
                        .text = _S"Save, then move, resize or maximize this window and Restore it, or open a new "
                                "window at the saved placement. Off screen asks for a spot no monitor shows; the "
                                "window should come back onto one.",
                        .wrap = true }));
    row = galleryRow(box, 6);
    button(row, _S"&Save", 3);
    button(row, _S"&Restore", 4);
    button(row, _S"&Open placed window", 5);
    button(row, _S"O&ff screen", 6);
    objRelease(&row);
}

const GalleryPage galleryDesktop = {
    .name  = _S"desktop",
    .title = _S"Desktop",
    .build = build,
};
