// Windows of their own: each theme, each way a window's size is chosen, and each way it can be
// shown. They log their events to the console.

#include "gallery.h"

static void pageSay(strref what)
{
    galleryStatus(_S"windows", what);
}

static void button(void* row, strref title, CxUiActivateFn fn, stvar arg);

// A little of everything, so the window's theme shows; returns the caller's reference to the column
// holding it.
static CxUiBox* windowContent(CxUiWindow* w, strref text)
{
    galleryLogWindow(w);
    CxUiBox* col = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 8, .margin = { 12, 12, 12, 12 } });
    galleryAdd(col, cxuiLabelCreate(&(CxUiLabelDesc){ .text = text, .wrap = true }));
    galleryAdd(col, cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .cue = _S"A field" }));
    CxUiBox* row = galleryRow(col, 6);
    galleryAdd(row, cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"Native" }));
    galleryAdd(row, cxuiDButtonCreate(&(CxUiDButtonDesc){ .title = _S"Drawn", .toggle = true, .padding = { 8, 4 } }));
    objRelease(&row);
    cxuiContainerAppend(w, col);
    return col;
}

static CxUiResult themeWindow(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    static const strref titles[] = { _S"cxui gallery: system theme", _S"cxui gallery: light", _S"cxui gallery: dark" };
    uint32 mode                  = stvlAt(cvars, 0, uint32);
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .title = titles[mode], .size = { 380, 0 }, .theme = mode,
                                                        .closeAction = CXUI_OnClose_Destroy });
    CxUiBox* col = windowContent(w, _S"This window keeps its own theme, whatever the gallery's is. On Linux the "
                                    "light or dark setting is shared by the whole program, so it may not match.");
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    objRelease(&col);
    objRelease(&w);   // cxui keeps shown windows alive
    pageSay(titles[mode]);
    return CXUI_Default;
}

static CxUiResult grow(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    CxUiBox* col = stvlAtObj(cvars, 0, CxUiBox);
    string line  = 0;
    strFormat(&line, _SL("Line ${int}: a natural-size window grows to fit"), stvar(int32, cxuiContainerChildCount(col)));
    galleryAdd(col, cxuiLabelCreate(&(CxUiLabelDesc){ .text = line }));
    strDestroy(&line);
    return CXUI_Default;
}

static CxUiResult sizingWindow(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    static const struct {
        strref title;
        strref text;
        uint32 sizing;
        flags_t flags;
    } kinds[] = {
        { _S"cxui gallery: natural size",
          _S"Fits its content, and fits again when it changes, until it is resized. Press Grow.", CXUI_SIZE_Natural, 0 },
        { _S"cxui gallery: fixed size", _S"360 by 220, and can't be resized.", CXUI_SIZE_Fixed, 0 },
        { _S"cxui gallery: start size",
          _S"Starts at 420 by 260 and can be resized, but not smaller than its content.", CXUI_SIZE_Start, 0 },
        { _S"cxui gallery: shrinks",
          _S"Starts at 420 by 260 and can be made smaller than its content, which then gives up space and wraps "
          "this text into more lines.", CXUI_SIZE_Start, CXUI_WINDOW_Shrink },
    };
    int32 k       = stvlAt(cvars, 0, int32);
    CxUiSize size = k == 0 ? (CxUiSize){ 0 } : k == 1 ? (CxUiSize){ 360, 220 } : (CxUiSize){ 420, 260 };
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .title = kinds[k].title, .size = size, .sizing = kinds[k].sizing,
                                                        .flags = kinds[k].flags, .closeAction = CXUI_OnClose_Destroy });
    galleryFollowTheme(w);
    CxUiBox* col = windowContent(w, kinds[k].text);
    if (k == 0) {
        CxUiButton* b = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"&Grow" });
        cxuiOnActivate(b, grow, stvar(object, col));
        galleryAdd(col, b);
    }
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    objRelease(&col);
    objRelease(&w);
    pageSay(kinds[k].title);
    return CXUI_Default;
}

static CxUiResult showWindow(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    static const strref titles[] = { _S"", _S"cxui gallery: not activated", _S"cxui gallery: minimized",
                                     _S"cxui gallery: maximized" };
    uint32 show                  = stvlAt(cvars, 0, uint32);
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .title = titles[show], .size = { 380, 0 },
                                                        .closeAction = CXUI_OnClose_Destroy });
    galleryFollowTheme(w);
    CxUiBox* col = windowContent(w, _S"Shown as its title says.");
    cxuiWindowShow(w, show);
    objRelease(&col);
    objRelease(&w);
    pageSay(titles[show]);
    return CXUI_Default;
}

static CxUiResult dragged(stvlist* cvars, CxUiMouseEvent* ev)
{
    unused_noeval(cvars);
    if (ev->action != CXUI_MOUSE_Down || ev->button != CXUI_BTN_Left)
        return CXUI_Default;
    if (!cxuiWindowBeginDrag(ev->base.window))
        pageSay(_S"the platform wouldn't move the window");
    return CXUI_Handled;
}

static CxUiResult minimize(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    cxuiWindowShow(ev->base.window, CXUI_SHOW_Minimized);
    return CXUI_Default;
}

static CxUiResult closeWindow(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    cxuiWindowClose(ev->base.window);
    return CXUI_Default;
}

// No frame: a drawn strip at the top moves it, and its own buttons minimize and close it.
static CxUiResult borderlessWindow(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui gallery: borderless", .size = { 380, 0 },
                                                        .flags = CXUI_WINDOW_Borderless,
                                                        .closeAction = CXUI_OnClose_Destroy });
    galleryFollowTheme(w);
    CxUiBox* col     = windowContent(w, _S"No frame or title bar, and a fixed size. Drag the strip above to move "
                                         "it; its taskbar button minimizes and restores it.");
    CxUiDLabel* grip = cxuiDLabelCreate(&(CxUiDLabelDesc){ .text = _S"cxui gallery: borderless (drag here)",
                                                          .padding = { 8, 6 } });
    cxuiOnMouse(grip, dragged, stvNone);
    cxuiContainerInsert(col, grip, 0);
    objRelease(&grip);
    CxUiBox* row = galleryRow(col, 6);
    button(row, _S"Mi&nimize", minimize, stvNone);
    button(row, _S"&Close", closeWindow, stvNone);
    objRelease(&row);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    objRelease(&col);
    objRelease(&w);
    pageSay(_S"borderless window");
    return CXUI_Default;
}

static CxUiResult toolColor(stvlist* cvars, CxUiActivateEvent* ev)
{
    static CxUiColor color = { .r = 48, .g = 112, .b = 192, .a = 255 };
    CxUiLabel* l           = stvlAtObj(cvars, 0, CxUiLabel);
    if (!cxuiColorDialog(&(CxUiColorDialogDesc){ .parent = ev->base.window, .title = _S"Tool window color" }, &color)) {
        pageSay(_S"color: cancelled");
        return CXUI_Default;
    }
    string line = 0;
    strFormat(&line, _SL("Color: ${int}, ${int}, ${int}"), stvar(int32, color.r), stvar(int32, color.g),
              stvar(int32, color.b));
    cxuiLabelSetText(l, line);
    pageSay(line);
    strDestroy(&line);
    return CXUI_Default;
}

static CxUiResult toolMove(stvlist* cvars, CxUiActivateEvent* ev)
{
    if (!cxuiWindowMoveNear(ev->base.window, CxUiWindowNone, stvlAt(cvars, 0, uint32)))
        pageSay(_S"the platform doesn't let programs place windows");
    return CXUI_Default;
}

// Owned by the gallery, and put beside it before it is shown
static CxUiResult toolWindow(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui gallery: tool window",
                                                        .closeAction = CXUI_OnClose_Destroy });
    galleryFollowTheme(w);
    cxuiWindowSetOwner(w, galleryWindow());
    CxUiBox* col = windowContent(w, _S"Belongs to the gallery: it stays in front of it, and on Windows is "
                                      "minimized with it and has no taskbar button of its own. It opens beside "
                                      "the gallery's right edge, or its left where the screen has no room.");
    CxUiLabel* color = cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"Color: 48, 112, 192" });
    cxuiContainerAppend(col, color);
    CxUiBox* row = galleryRow(col, 6);
    button(row, _S"&Color...", toolColor, stvar(object, color));
    button(row, _S"&Left", toolMove, stvar(uint32, CXUI_NEAR_Left));
    button(row, _S"&Right", toolMove, stvar(uint32, CXUI_NEAR_Right));
    button(row, _S"C&enter", toolMove, stvar(uint32, CXUI_NEAR_Center));
    objRelease(&row);
    objRelease(&color);
    if (!cxuiWindowMoveNear(w, CxUiWindowNone, CXUI_NEAR_Right))
        pageSay(_S"tool window; the platform places it");
    else
        pageSay(_S"tool window");
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    objRelease(&col);
    objRelease(&w);
    return CXUI_Default;
}

static void button(void* row, strref title, CxUiActivateFn fn, stvar arg)
{
    CxUiButton* b = cxuiButtonCreate(&(CxUiButtonDesc){ .title = title });
    cxuiOnActivate(b, fn, arg);
    galleryAdd(row, b);
}

static void build(CxUiBox* box)
{
    galleryAdd(box, cxuiLabelCreate(&(CxUiLabelDesc){
                        .text = _S"Each button opens a window that logs its moves, resizes, state changes, "
                                "activation and closing to the console. The ones without a theme of their own "
                                "follow the gallery's.",
                        .wrap = true }));

    galleryHeading(box, _S"Themes");
    CxUiBox* row = galleryRow(box, 6);
    button(row, _S"&System", themeWindow, stvar(uint32, CXUI_Theme_System));
    button(row, _S"&Light", themeWindow, stvar(uint32, CXUI_Theme_Light));
    button(row, _S"&Dark", themeWindow, stvar(uint32, CXUI_Theme_Dark));
    objRelease(&row);

    galleryHeading(box, _S"Sizing");
    row = galleryRow(box, 6);
    button(row, _S"&Natural", sizingWindow, stvar(int32, 0));
    button(row, _S"&Fixed", sizingWindow, stvar(int32, 1));
    button(row, _S"S&tart", sizingWindow, stvar(int32, 2));
    button(row, _S"Shrin&ks", sizingWindow, stvar(int32, 3));
    objRelease(&row);

    galleryHeading(box, _S"Showing");
    row = galleryRow(box, 6);
    button(row, _S"Not &activated", showWindow, stvar(uint32, CXUI_SHOW_NoActivate));
    button(row, _S"&Minimized", showWindow, stvar(uint32, CXUI_SHOW_Minimized));
    button(row, _S"Ma&ximized", showWindow, stvar(uint32, CXUI_SHOW_Maximized));
    button(row, _S"&Borderless", borderlessWindow, stvNone);
    objRelease(&row);

    galleryHeading(box, _S"Owned");
    row = galleryRow(box, 6);
    button(row, _S"T&ool window", toolWindow, stvNone);
    objRelease(&row);
}

const GalleryPage galleryWindows = {
    .name  = _S"windows",
    .title = _S"Windows",
    .build = build,
};
