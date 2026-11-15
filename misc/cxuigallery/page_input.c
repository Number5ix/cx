// A canvas that takes all of its input: typed text through the system's input methods, keys the
// window would otherwise use, and the pointer locked for mouse look or an orbit-style drag; and
// the main window made fullscreen. The console shows each key and the text; the main window's
// activation there says when a lock was let go and taken again.

#include "gallery.h"

#include <cxui/canvas.h>

#include <math.h>

// Holds a reference to each widget it uses after building, released when the page goes
typedef struct InputPage {
    CxUiCanvas* cv;
    CxUiFont* font;
    CxUiCheck* text;
    CxUiCheck* capture;
    CxUiCheck* sysKey;
    CxUiDropdown* lock;
    CxUiCheck* dragLock;
    CxUiCheck* imageCursor;
    CxUiCheck* topmost;
    CxUiDropdown* monitor;
    string typed;
    // Where the pointer is, or where the lock began; the motion the lock reported
    CxUiPoint at;
    float64 movedX, movedY;
    float32 lastDx, lastDy;
    // A drag that locks the pointer once it has gone past the click slop
    bool dragging;
    CxUiPoint dragFrom;
} InputPage;

static InputPage* page;

#define DRAG_SLOP 4

static void pageSay(strref what)
{
    galleryStatus(_S"input", what);
}

static void caretUpdate(void)
{
    if (!page->font)
        page->font = cxuiFontCreateFor(page->cv, NULL);
    CxUiFontMetrics fm;
    cxuiFontMetrics(page->font, &fm);
    cxuiCanvasSetTextCaret(page->cv, 10 + cxuiFontAdvance(page->font, page->typed), 10, 1, fm.height);
}

static CxUiResult inputPaint(stvlist* cvars, CxUiPaintEvent* ev)
{
    unused_noeval(cvars);
    CxUiDraw* d = ev->draw;
    CxUiSize sz;
    cxuiCanvasSize(page->cv, &sz);
    if (!page->font)
        page->font = cxuiFontCreateFor(page->cv, NULL);
    CxUiFontMetrics fm;
    cxuiFontMetrics(page->font, &fm);
    CxUiColor bg = cxuiHex(0x202428), fg = cxuiHex(0xE8E8E8), dim = cxuiHex(0x80868C), mark = cxuiHex(0x4FC3F7);
    cxuiDrawFill(d, 0, 0, sz.w, sz.h, bg);

    int32 x = 10 + cxuiDrawText(d, page->font, 10, 10, page->typed, fg);
    if (cxuiCanvasTextInput(page->cv))
        cxuiDrawFill(d, x, 10, 1, fm.height, fg);

    string line = 0;
    strFormat(&line, _SL("lock ${uint}, last move ${float},${float}, all motion ${float},${float}"),
              stvar(uint32, cxuiCanvasPointerLockMode(page->cv)), stvar(float64, page->lastDx),
              stvar(float64, page->lastDy), stvar(float64, page->movedX), stvar(float64, page->movedY));
    cxuiDrawText(d, page->font, 10, 10 + fm.height * 2, line, dim);
    strDestroy(&line);

    cxuiDrawLine(d, page->at.x - 12, page->at.y, page->at.x + 12, page->at.y, 1, mark);
    cxuiDrawLine(d, page->at.x, page->at.y - 12, page->at.x, page->at.y + 12, 1, mark);
    return CXUI_Handled;
}

static void lockSet(uint32 mode)
{
    bool ok = cxuiCanvasSetPointerLock(page->cv, mode);
    string msg = 0;
    strFormat(&msg, _SL("pointer lock ${uint} asked for: ${string}, in effect ${uint}"), stvar(uint32, mode),
              stvar(strref, ok ? _S"taken" : _S"refused"), stvar(uint32, cxuiCanvasPointerLockMode(page->cv)));
    pageSay(msg);
    strDestroy(&msg);
}

static CxUiResult inputMouse(stvlist* cvars, CxUiMouseEvent* ev)
{
    unused_noeval(cvars);
    page->at = (CxUiPoint){ ev->x, ev->y };
    if (ev->action == CXUI_MOUSE_Move) {
        page->lastDx = ev->dx;
        page->lastDy = ev->dy;
        page->movedX += ev->dx;
        page->movedY += ev->dy;
    }
    if (cxuiCheckValue(page->dragLock)) {
        if (ev->action == CXUI_MOUSE_Down && !page->dragging) {
            page->dragging = true;
            page->dragFrom = page->at;
        } else if (ev->action == CXUI_MOUSE_Move && page->dragging && !cxuiCanvasPointerLockMode(page->cv) &&
                   (abs(ev->x - page->dragFrom.x) > DRAG_SLOP || abs(ev->y - page->dragFrom.y) > DRAG_SLOP)) {
            lockSet(CXUI_POINTERLOCK_Warp);
        } else if (ev->action == CXUI_MOUSE_Up && !ev->buttons) {
            page->dragging = false;
            if (cxuiCanvasPointerLockMode(page->cv))
                lockSet(CXUI_POINTERLOCK_Off);
            if (ev->flags & CXUI_MOUSE_CaptureLost)
                pageSay(_S"the drag ended with a lost capture");
        }
    }
    cxuiCanvasInvalidateAll(page->cv);
    return CXUI_Handled;
}

static CxUiResult inputKey(stvlist* cvars, CxUiKeyEvent* ev)
{
    unused_noeval(cvars);
    say(_SL("input: key ${uint} scancode ${uint} mods ${uint} ${string}${string}\n"), stvar(uint32, ev->key),
        stvar(uint32, ev->scancode), stvar(uint32, ev->mods), stvar(strref, ev->down ? _S"down" : _S"up"),
        stvar(strref, ev->repeat ? _S" (repeat)" : _S""));
    if (ev->down && ev->key == CXUI_KEY_Backspace && cxuiCanvasTextInput(page->cv) && strLen(page->typed) > 0) {
        // Back to the start of the last character
        uint32 len = strLen(page->typed);
        while (len > 0 && ((uint8)strC(page->typed)[len - 1] & 0xC0) == 0x80)
            len--;
        strSubStr(&page->typed, page->typed, 0, len > 0 ? len - 1 : 0);
        caretUpdate();
        cxuiCanvasInvalidateAll(page->cv);
        return CXUI_Handled;
    }
    return CXUI_Default;
}

static CxUiResult inputText(stvlist* cvars, CxUiTextEvent* ev)
{
    unused_noeval(cvars);
    say(_SL("input: text \"${string}\"\n"), stvar(strref, ev->text));
    strAppend(&page->typed, ev->text);
    caretUpdate();
    cxuiCanvasInvalidateAll(page->cv);
    return CXUI_Handled;
}

static CxUiResult inputFocus(stvlist* cvars, CxUiFocusEvent* ev)
{
    unused_noeval(cvars);
    say(_SL("input: canvas focus ${string}\n"), stvar(strref, ev->gained ? _S"gained" : _S"lost"));
    return CXUI_Default;
}

// A ring 32 pixels across, larger on a display with a larger scale, clicking at its middle
static CxUiCursor* makeCursor(void)
{
    int32 size = (int32)(32 * cxuiCanvasScale(page->cv));
    uint8* px  = xaAlloc((size_t)size * size * 4);
    float32 r  = size / 2.0f;
    for (int32 y = 0; y < size; y++) {
        for (int32 x = 0; x < size; x++) {
            float32 d = sqrtf((x + 0.5f - r) * (x + 0.5f - r) + (y + 0.5f - r) * (y + 0.5f - r));
            uint8* p  = &px[(y * size + x) * 4];
            bool ring = d < r - 1 && d > r - 1 - r / 4;
            p[0] = 255, p[1] = 80, p[2] = 40, p[3] = ring ? 255 : 0;
        }
    }
    CxUiImage* img  = cxuiImageCreateRGBA(size, size, size * 4, px);
    CxUiCursor* cur = cxuiCursorCreate(img, size / 2, size / 2);
    objRelease(&img);
    xaFree(px);
    return cur;
}

static CxUiResult warpCenter(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    CxUiSize sz;
    cxuiCanvasSize(page->cv, &sz);
    pageSay(cxuiCanvasWarpPointer(page->cv, sz.w / 2, sz.h / 2) ? _S"the pointer moved to the canvas's middle"
                                                                 : _S"the pointer can't be moved here");
    return CXUI_Default;
}

static void captureApply(void)
{
    flags_t flags = cxuiCheckValue(page->capture) ? CXUI_KEYCAPTURE_On : 0;
    if (flags && cxuiCheckValue(page->sysKey))
        flags |= CXUI_KEYCAPTURE_SystemKey;
    if (!cxuiCanvasSetKeyCapture(page->cv, flags))
        pageSay(_S"key capture: the system key isn't supported here");
}

static CxUiResult optionChanged(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    switch (stvlAt(cvars, 0, int32)) {
    case 0:
        if (!cxuiCanvasSetTextInput(page->cv, cxuiCheckValue(page->text) ? CXUI_TEXTINPUT_On : 0))
            pageSay(_S"text input isn't supported here");
        caretUpdate();
        cxuiCanvasInvalidateAll(page->cv);
        break;
    case 1:
        captureApply();
        break;
    case 2:
        lockSet((uint32)cxuiDropdownValue(page->lock));
        break;
    case 4:
        if (!cxuiWindowSetTopmost(galleryWindow(), cxuiCheckValue(page->topmost)))
            pageSay(_S"windows can't be kept on top here");
        break;
    case 3:
        if (cxuiCheckValue(page->imageCursor)) {
            CxUiCursor* cur = makeCursor();
            cxuiCanvasSetCursorImage(page->cv, cur);
            objRelease(&cur);
        } else {
            cxuiCanvasSetCursor(page->cv, CXUI_CURSOR_Arrow);
        }
        break;
    }
    cxuiFocus(page->cv);
    return CXUI_Default;
}

static CxUiResult fullscreen(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    uint32 mode = (uint32)stvlAt(cvars, 0, int32);
    sa_CxUiMonitor mons = { 0 };
    cxuiMonitors(&mons);
    int32 which = cxuiDropdownValue(page->monitor);
    const CxUiMonitor* mon = which > 0 && which <= saSize(mons) ? &mons.a[which - 1] : NULL;
    bool ok     = cxuiWindowSetFullscreen(galleryWindow(), mode, mon);
    saDestroy(&mons);
    string msg = 0;
    strFormat(&msg, _SL("fullscreen ${uint}: ${string}"), stvar(uint32, mode), stvar(strref, ok ? _S"asked" : _S"refused"));
    pageSay(msg);
    strDestroy(&msg);
    return CXUI_Default;
}

// "This monitor", then each monitor by name
static void listMonitors(void)
{
    sa_CxUiMonitor mons = { 0 };
    sa_string items     = { 0 };
    cxuiMonitors(&mons);
    saInit(&items, string, saSize(mons) + 1);
    saPush(&items, string, _S"This monitor");
    for (int32 i = 0; i < saSize(mons); i++) {
        string item = 0;
        strFormat(&item, _SL("${int}: ${string}"), stvar(int32, i + 1),
                  stvar(strref, strEmpty(mons.a[i].name) ? _S"Monitor" : mons.a[i].name));
        saPush(&items, string, item);
        strDestroy(&item);
    }
    int32 was = cxuiDropdownValue(page->monitor);
    cxuiDropdownSetItems(page->monitor, items);
    cxuiDropdownSetValue(page->monitor, was > 0 && was < saSize(items) ? was : 0);
    saDestroy(&items);
    saDestroy(&mons);
}

static bool monitorsChanged(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    if (page)
        listMonitors();
    return true;
}

static CxUiCheck* option(CxUiBox* row, strref title, int32 which)
{
    CxUiCheck* c = cxuiCheckCreate(&(CxUiCheckDesc){ .title = title });
    cxuiOnChange(c, optionChanged, stvar(int32, which));
    cxuiContainerAppend(row, c);
    return c;
}

static void fsButton(void* row, strref title, uint32 mode)
{
    CxUiButton* b = cxuiButtonCreate(&(CxUiButtonDesc){ .title = title });
    cxuiOnActivate(b, fullscreen, stvar(int32, mode));
    galleryAdd(row, b);
}

static bool pageGone(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    cchainDetach(&cxuiMonitorsChangedNotify, monitorsChanged, 0);
    objRelease(&page->cv);
    objRelease(&page->font);
    objRelease(&page->text);
    objRelease(&page->capture);
    objRelease(&page->sysKey);
    objRelease(&page->lock);
    objRelease(&page->dragLock);
    objRelease(&page->imageCursor);
    objRelease(&page->topmost);
    objRelease(&page->monitor);
    strDestroy(&page->typed);
    xaDestroy(&page);
    return true;
}

static void build(CxUiBox* box)
{
    page = xaAllocStruct(InputPage, XA_Zero);
    cchainAttach(&CxUiWidget(box)->destroyNotify, pageGone, stvNone);
    galleryAdd(box, cxuiLabelCreate(&(CxUiLabelDesc){
                        .text = _S"Click the canvas, then type: with text input on, dead keys and input methods "
                                "make text. Key capture keeps Tab, Esc, F10 and Command shortcuts in the canvas. "
                                "Lock the pointer from the list, or check the drag box and drag with any "
                                "button; Cmd+Tab or Alt+Tab away and back to see the lock let go and taken "
                                "again.",
                        .wrap = true }));

    static const strref locks[] = { _S"Pointer free", _S"Lock: warp", _S"Lock: raw", NULL };
    CxUiBox* row  = galleryRow(box, 12);
    page->text    = option(row, _S"&Text input", 0);
    page->capture = option(row, _S"&Key capture", 1);
    page->sysKey  = option(row, _S"S&ystem key", 1);
    page->lock    = cxuiDropdownCreate(&(CxUiDropdownDesc){ .items = locks, .value = 0 });
    cxuiOnChange(page->lock, optionChanged, stvar(int32, 2));
    cxuiContainerAppend(row, page->lock);
    page->dragLock = cxuiCheckCreate(&(CxUiCheckDesc){ .title = _S"Lock while &dragging" });
    cxuiContainerAppend(row, page->dragLock);
    page->imageCursor = option(row, _S"&Image cursor", 3);
    CxUiButton* warp  = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"&Warp to middle" });
    cxuiOnActivate(warp, warpCenter, stvNone);
    galleryAdd(row, warp);
    objRelease(&row);

    row           = galleryRow(box, 6);
    page->monitor = cxuiDropdownCreate(NULL);
    listMonitors();
    cchainAttach(&cxuiMonitorsChangedNotify, monitorsChanged, stvNone);
    cxuiContainerAppend(row, page->monitor);
    fsButton(row, _S"&Fullscreen", CXUI_FULLSCREEN_System);
    fsButton(row, _S"Fullscreen, &no frame", CXUI_FULLSCREEN_Monitor);
    fsButton(row, _S"&Leave fullscreen", CXUI_FULLSCREEN_None);
    page->topmost = option(row, _S"Keep on t&op", 4);
    objRelease(&row);

    page->cv = cxuiCanvasCreate(NULL);
    cxuiWidgetSetNaturalSize(page->cv, cxuiLen(480), cxuiLen(260));
    cxuiWidgetSetExpand(page->cv, CXUI_EXPAND_Both);
    cxuiOnPaint(page->cv, inputPaint, stvNone);
    cxuiOnMouse(page->cv, inputMouse, stvNone);
    cxuiOnKey(page->cv, inputKey, stvNone);
    cxuiOnText(page->cv, inputText, stvNone);
    cxuiOnFocus(page->cv, inputFocus, stvNone);
    cxuiContainerAppend(box, page->cv);
}

const GalleryPage galleryInput = {
    .name  = _S"input",
    .title = _S"Input",
    .build = build,
};
