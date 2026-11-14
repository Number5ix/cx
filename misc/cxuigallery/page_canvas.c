// A canvas the program draws on: lines, frames, clipping, text and images, a moving square, and
// the mouse and keys it gets. The buttons open the same drawing in windows of its own, in pixels or
// device-independent pixels, or with a drawing engine named on Windows; and a render view, which
// the page presents frames to as a renderer would, with GDI on Windows and Metal on macOS.

#include "gallery.h"

#include <math.h>
#if defined(_PLATFORM_WIN)
#include <cx/platform/win.h>
#endif

typedef struct CanvasDemo {
    CxUiCanvas* cv;
    CxUiTimer* timer;
    int32 step;     // the moving square's position
    int32 paints;
    CxUiFont* ui;
    CxUiFont* styled[3];   // bold, italic, underlined
    CxUiFont* mono;
    CxUiImage* disc;
    CxUiImage* quarter;
    bool dip;   // drawn in device-independent pixels
    // A rubber band from where the left button went down to the pointer, while it is held
    bool dragging;
    CxUiPoint dragFrom, dragTo;
    uint32 cursor;
} CanvasDemo;

// The one on the page, for the scaled hook; NULL once it is gone
static CanvasDemo* embedded;

// A font measurement in the canvas's units
static int32 units(CanvasDemo* d, int32 px)
{
    return d->dip ? cxuiDip(d->cv, px) : px;
}

// A 32x32 disc with a soft edge, red to blue across it
static CxUiImage* makeDisc(void)
{
    uint8 px[32 * 32 * 4];
    for (int32 y = 0; y < 32; y++) {
        for (int32 x = 0; x < 32; x++) {
            float64 dist = sqrt((x - 15.5) * (x - 15.5) + (y - 15.5) * (y - 15.5));
            float64 a    = dist < 13 ? 1.0 : dist > 16 ? 0.0 : (16 - dist) / 3;
            uint8* p     = &px[(y * 32 + x) * 4];
            p[0]         = (uint8)(255 - x * 8);
            p[1]         = (uint8)(y * 4);
            p[2]         = (uint8)(x * 8);
            p[3]         = (uint8)(a * 255);
        }
    }
    return cxuiImageCreateRGBA(32, 32, 32 * 4, px);
}

static void demoFonts(CanvasDemo* d)
{
    static const uint32 styles[3] = { CXUI_FONT_Bold, CXUI_FONT_Italic, CXUI_FONT_Underline };
    objRelease(&d->ui);
    objRelease(&d->mono);
    d->ui   = cxuiFontCreateFor(d->cv, NULL);
    d->mono = cxuiFontCreateFor(d->cv, &(CxUiFontDesc){ .faces = _S"Consolas;monospace", .size = 10 });
    for (int32 i = 0; i < 3; i++) {
        objRelease(&d->styled[i]);
        d->styled[i] = cxuiFontCreateFor(d->cv, &(CxUiFontDesc){ .size = 10, .flags = styles[i] });
    }
}

static CxUiRect squareAt(CanvasDemo* d, CxUiSize sz)
{
    int32 span = max(sz.w - 40, 1);
    int32 x    = d->step % (span * 2);
    return (CxUiRect){ 20 + (x < span ? x : span * 2 - x), sz.h - 40, 20, 20 };
}

static CxUiResult demoPaint(stvlist* cvars, CxUiPaintEvent* ev)
{
    CanvasDemo* d = stvlAtPtr(cvars, 0);
    CxUiDraw* dc  = ev->draw;
    CxUiSize sz   = cxuiDrawSize(dc);
    d->paints++;

    cxuiDrawFill(dc, 0, 0, sz.w, sz.h, cxuiHex(0x1E1E1E));
    // A 1-px grid every 10 px: any blur or gap shows at once.
    for (int32 x = 0; x < sz.w; x += 10)
        cxuiDrawLine(dc, x, 0, x, sz.h - 1, 1, cxuiHex(x % 50 ? 0x2A2A2A : 0x3A3A3A));
    for (int32 y = 0; y < sz.h; y += 10)
        cxuiDrawLine(dc, 0, y, sz.w - 1, y, 1, cxuiHex(y % 50 ? 0x2A2A2A : 0x3A3A3A));

    for (int32 i = 0; i < 4; i++)
        cxuiDrawFrame(dc, 20 + i * 50, 20, 40, 40, i + 1, cxuiHex(0xE0E0E0));
    cxuiDrawFill(dc, 240, 20, 60, 40, cxuiHexA(0xFF000080));
    cxuiDrawFill(dc, 270, 40, 60, 40, cxuiHexA(0x00FF0080));
    cxuiDrawFill(dc, 255, 60, 60, 40, cxuiHexA(0x4080FF80));

    cxuiDrawLine(dc, 20, 80, 220, 140, 1, cxuiHex(0xFFC040));
    cxuiDrawLine(dc, 20, 90, 220, 180, 3, cxuiHex(0x40C0FF));

    // Clipped: only the inner square of the cross shows.
    cxuiDrawClipPush(dc, (CxUiRect){ 360, 30, 40, 40 });
    cxuiDrawFill(dc, 340, 45, 80, 10, cxuiHex(0xC060FF));
    cxuiDrawFill(dc, 375, 10, 10, 80, cxuiHex(0xC060FF));
    cxuiDrawClipPop(dc);
    cxuiDrawFrame(dc, 360, 30, 40, 40, 1, cxuiHex(0x606060));

    // The run's measured box is outlined; the text should sit inside it.
    strref sample = _S"Measured: \xC3\xBCn\xC3\xAF" "c\xC3\xB6" "d\xC3\xA9 \xE2\x9C\x93 gyp";
    CxUiFontMetrics m;
    cxuiFontMetrics(d->ui, &m);
    int32 lh  = units(d, m.height);
    int32 adv = cxuiDrawText(dc, d->ui, 20, 190, sample, cxuiHex(0xF0F0F0));
    cxuiDrawFrame(dc, 19, 189, adv + 2, lh + 2, 1, cxuiHex(0x4060A0));
    int32 x = 20;
    static const strref names[3] = { _S"Bold", _S"Italic", _S"Underlined" };
    for (int32 i = 0; i < 3; i++)
        x += cxuiDrawText(dc, d->styled[i], x, 190 + lh + 6, names[i], cxuiHex(0xE0C080)) + 12;

    // One character per cell, whatever its width
    CxUiFontMetrics mm;
    cxuiFontMetrics(d->mono, &mm);
    int32 gy = 190 + 2 * (lh + 6), cw = units(d, mm.cellW);
    for (int32 i = 0; i <= 12; i++)
        cxuiDrawLine(dc, 20 + i * cw, gy, 20 + i * cw, gy + units(d, mm.cellH) - 1, 1, cxuiHex(0x404040));
    cxuiDrawTextCells(dc, d->mono, 20, gy, _S"cells: WiW\xE2\x94\x82\xE2\x96\x88", cw, cxuiHex(0x80FF80));

    // An image at its own size, a quarter of it, and the whole of it twice as big
    cxuiDrawImage(dc, d->disc, NULL, (CxUiRect){ 330, 190, 32, 32 });
    cxuiDrawImage(dc, d->quarter, NULL, (CxUiRect){ 370, 190, 16, 16 });
    cxuiDrawImage(dc, d->disc, NULL, (CxUiRect){ 394, 190, 64, 64 });

    cxuiDrawFrame(dc, 340, 110, 120, 60, 1, cxuiHex(0x606060));
    cxuiDrawTextBox(dc, d->ui, (CxUiRect){ 340, 110, 120, 60 }, _S"centered\nin a box", CXUI_TEXT_Center | CXUI_TEXT_Middle,
                    cxuiHex(0xC0C0FF));

    if (d->dragging) {
        CxUiPoint a = d->dragFrom, b = d->dragTo;
        cxuiDrawFrame(dc, min(a.x, b.x), min(a.y, b.y), abs(b.x - a.x) + 1, abs(b.y - a.y) + 1, 1, cxuiHex(0xFFFF00));
    }

    CxUiRect sq = squareAt(d, sz);
    cxuiDrawFill(dc, sq.x, sq.y, sq.w, sq.h, cxuiHex(0x40E080));
    return CXUI_Handled;
}

static CxUiResult demoTick(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    CanvasDemo* d = stvlAtPtr(cvars, 0);
    CxUiSize sz;
    if (!cxuiCanvasSize(d->cv, &sz))
        return CXUI_Default;
    // Only the old and new squares are repainted.
    CxUiRect old = squareAt(d, sz);
    d->step += 2;
    CxUiRect now = squareAt(d, sz);
    cxuiCanvasInvalidate(d->cv, old.x, old.y, old.w, old.h);
    cxuiCanvasInvalidate(d->cv, now.x, now.y, now.w, now.h);
    return CXUI_Default;
}

static CxUiResult demoResize(stvlist* cvars, CxUiResizeEvent* ev)
{
    CanvasDemo* d = stvlAtPtr(cvars, 0);
    say(_SL("canvas: ${int}x${int} ${string}\n"), stvar(int32, ev->size.w), stvar(int32, ev->size.h),
        stvar(strref, d->dip ? _S"DIPs" : _S"px"));
    return CXUI_Default;
}

static CxUiResult demoDpi(stvlist* cvars, CxUiDpiChangedEvent* ev)
{
    demoFonts(stvlAtPtr(cvars, 0));
    say(_SL("canvas: scale ${float} -> ${float}\n"), stvar(float64, ev->oldScale), stvar(float64, ev->scale));
    return CXUI_Default;
}

static bool demoGone(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CanvasDemo* d = stvlAtPtr(cvars, 0);
    say(_SL("canvas: gone after ${int} paints\n"), stvar(int32, d->paints));
    if (d == embedded)
        embedded = NULL;
    cxuiTimerStop(d->timer);
    objRelease(&d->timer);
    objRelease(&d->ui);
    objRelease(&d->mono);
    for (int32 i = 0; i < 3; i++)
        objRelease(&d->styled[i]);
    objRelease(&d->disc);
    objRelease(&d->quarter);
    objRelease(&d->cv);
    xaFree(d);
    return true;
}

static strref buttonName(uint32 b)
{
    switch (b) {
    case CXUI_BTN_Left:   return _S"left";
    case CXUI_BTN_Middle: return _S"middle";
    case CXUI_BTN_Right:  return _S"right";
    case CXUI_BTN_X1:     return _S"back";
    case CXUI_BTN_X2:     return _S"forward";
    }
    return _S"";
}

static void invalidateBand(CanvasDemo* d)
{
    CxUiPoint a = d->dragFrom, b = d->dragTo;
    cxuiCanvasInvalidate(d->cv, min(a.x, b.x), min(a.y, b.y), abs(b.x - a.x) + 1, abs(b.y - a.y) + 1);
}

// Prints everything but moves, and moves only while they are outside the canvas, which shows the
// canvas keeps the mouse while a button is held.
static CxUiResult demoMouse(stvlist* cvars, CxUiMouseEvent* ev)
{
    CanvasDemo* d = stvlAtPtr(cvars, 0);
    static const strref actions[] = { _S"down", _S"up", _S"double click", _S"move", _S"enter", _S"leave" };
    CxUiSize sz   = { 0 };
    cxuiCanvasSize(d->cv, &sz);
    bool outside = ev->x < 0 || ev->y < 0 || ev->x >= sz.w || ev->y >= sz.h;
    if (ev->action != CXUI_MOUSE_Move || outside)
        say(_SL("canvas: ${string} ${string} at ${int},${int}${string}${string}\n"), stvar(strref, actions[ev->action]),
            stvar(strref, buttonName(ev->button)), stvar(int32, ev->x), stvar(int32, ev->y),
            stvar(strref, outside ? _S" (outside)" : _S""),
            stvar(strref, (ev->flags & CXUI_MOUSE_CaptureLost) ? _S" (capture lost)" : _S""));

    if (ev->action == CXUI_MOUSE_Down && ev->button == CXUI_BTN_Left) {
        d->dragging = true;
        d->dragFrom = d->dragTo = (CxUiPoint){ ev->x, ev->y };
        invalidateBand(d);
        // Clicking takes the keys, so C works without tabbing to the canvas.
        cxuiFocus(d->cv);
    } else if (d->dragging && (ev->action == CXUI_MOUSE_Move || ev->action == CXUI_MOUSE_Up)) {
        invalidateBand(d);
        d->dragTo = (CxUiPoint){ ev->x, ev->y };
        invalidateBand(d);
        if (ev->action == CXUI_MOUSE_Up && ev->button == CXUI_BTN_Left)
            d->dragging = false;
    }
    return CXUI_Handled;
}

static CxUiResult demoWheel(stvlist* cvars, CxUiWheelEvent* ev)
{
    unused_noeval(cvars);
    say(_SL("canvas: wheel ${float},${float} at ${int},${int}\n"), stvar(float64, ev->dx), stvar(float64, ev->dy),
        stvar(int32, ev->x), stvar(int32, ev->y));
    return CXUI_Handled;
}

// C cycles through the cursors.
static CxUiResult demoKey(stvlist* cvars, CxUiKeyEvent* ev)
{
    CanvasDemo* d = stvlAtPtr(cvars, 0);
    string name   = 0;
    cxuiKeyFormat(&name, ev->key, ev->mods);
    say(_SL("canvas: key ${string} ${string}${string}\n"), stvar(string, name), stvar(strref, ev->down ? _S"down" : _S"up"),
        stvar(strref, ev->repeat ? _S" (repeat)" : _S""));
    strDestroy(&name);
    if (ev->down && ev->key == CXUI_KEY_C && !ev->mods) {
        static const strref cursors[CXUI_CURSOR_Count] = { _S"arrow", _S"I-beam", _S"hand", _S"wait", _S"cross",
                                                           _S"size W-E", _S"size N-S", _S"size NW-SE", _S"size NE-SW",
                                                           _S"move", _S"not allowed", _S"none" };
        d->cursor = (d->cursor + 1) % CXUI_CURSOR_Count;
        cxuiCanvasSetCursor(d->cv, d->cursor);
        galleryStatus(_S"canvas", cursors[d->cursor]);
        return CXUI_Handled;
    }
    return CXUI_Default;
}

static CxUiResult demoFocus(stvlist* cvars, CxUiFocusEvent* ev)
{
    unused_noeval(cvars);
    say(_SL("canvas: focus ${string}\n"), stvar(strref, ev->gained ? _S"gained" : _S"lost"));
    return CXUI_Default;
}

// A canvas with the drawing; the demo holds the canvas, and is freed when the canvas is destroyed.
static CanvasDemo* demoCreate(bool dip, uint32 engine)
{
    CanvasDemo* d = xaAllocStruct(CanvasDemo, XA_Zero);
    d->dip        = dip;
    d->disc       = makeDisc();
    d->quarter    = cxuiImageCreateSub(d->disc, (CxUiRect){ 0, 0, 16, 16 });
    d->cv = cxuiCanvasCreate(&(CxUiCanvasDesc){ .units = dip ? CXUI_Units_Dip : CXUI_Units_Pixels, .draw = engine });
    cxuiOnPaint(d->cv, demoPaint, stvar(ptr, d));
    cxuiOnResize(d->cv, demoResize, stvar(ptr, d));
    cxuiOnMouse(d->cv, demoMouse, stvar(ptr, d));
    cxuiOnWheel(d->cv, demoWheel, stvNone);
    cxuiOnKey(d->cv, demoKey, stvar(ptr, d));
    cxuiOnFocus(d->cv, demoFocus, stvNone);
    cchainAttach(&CxUiWidget(d->cv)->destroyNotify, demoGone, stvar(ptr, d));
    demoFonts(d);
    d->timer = cxuiTimerCreate(timeMS(30), 0, demoTick, stvar(ptr, d));
    cxuiTimerStart(d->timer);
    return d;
}

static CxUiResult openWindow(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    bool dip                      = stvlAt(cvars, 0, bool);
    uint32 engine                 = stvlAt(cvars, 1, uint32);
    static const strref engines[] = { _S"", _S", Direct2D", _S", GDI" };
    string title                  = 0;
    strConcat(&title, dip ? _S"cxui gallery: canvas in DIPs" : _S"cxui gallery: canvas in pixels", engines[engine]);
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .title = title, .size = { 480, 320 },
                                                        .closeAction = CXUI_OnClose_Destroy });
    strDestroy(&title);
    galleryFollowTheme(w);
    CanvasDemo* d = demoCreate(dip, engine);
    cxuiOnDpiChanged(w, demoDpi, stvar(ptr, d));
    cxuiContainerAppend(w, d->cv);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    say(_SL("canvas: a window at scale ${float}\n"), stvar(float64, cxuiCanvasScale(d->cv)));
    objRelease(&w);   // cxui keeps shown windows alive
    return CXUI_Default;
}

// ---- render view ----

typedef struct RenderDemo {
    CxUiRenderView* view;   // held until it is destroyed
    CxUiLabel* status;
    CxUiNativeSurface surface;
    bool hasSurface;
    int32 frames;
    CxUiPoint click;
    int32 clicks, wheels;
} RenderDemo;

static void renderStatus(RenderDemo* d)
{
    static const strref kinds[] = { _S"none", _S"Win32", _S"X11", _S"Wayland", _S"Metal" };
    string text                 = 0;
    if (!d->hasSurface)
        strCopy(&text, _S"No surface");
    else
        strFormat(&text, _SL("${string} surface ${ptr}, ${int}x${int} pixels; ${int} frames asked for; ${int} clicks, the last at ${int},${int}; ${int} wheel turns"),
                  stvar(strref, kinds[min(d->surface.kind, 4u)]), stvar(ptr, d->surface.native), stvar(int32, d->surface.w),
                  stvar(int32, d->surface.h), stvar(int32, d->frames), stvar(int32, d->clicks), stvar(int32, d->click.x),
                  stvar(int32, d->click.y), stvar(int32, d->wheels));
    cxuiLabelSetText(d->status, text);
    strDestroy(&text);
}

// A frame as a renderer would present it: straight to the window, never through cxui
static void renderFrame(RenderDemo* d)
{
#if defined(_PLATFORM_WIN)
    if (!d->hasSurface)
        return;
    HWND hwnd = d->surface.native;
    HDC dc    = GetDC(hwnd);
    for (int32 band = 0; band < 16; band++) {
        int32 shade = (band * 10 + d->frames * 3) % 150;
        RECT r      = { 0, d->surface.h * band / 16, d->surface.w, d->surface.h * (band + 1) / 16 };
        SetDCBrushColor(dc, RGB(32, 64 + shade / 2, 96 + shade));
        FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
    }
    WCHAR text[96];
    wsprintfW(text, L"Presented by the program: %dx%d, frame %d", d->surface.w, d->surface.h, d->frames);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    TextOutW(dc, 12, 12, text, lstrlenW(text));
    ReleaseDC(hwnd, dc);
#elif defined(_PLATFORM_MACOS)
    if (d->hasSurface)
        galleryMetalFrame(d->surface.native, d->frames);
#else
    unused_noeval(d);
#endif
}

static CxUiResult renderSurface(stvlist* cvars, CxUiSurfaceEvent* ev)
{
    RenderDemo* d = stvlAtPtr(cvars, 0);
    d->hasSurface = ev->available && cxuiRenderViewSurface(d->view, &d->surface);
    say(_SL("render view: the surface ${string}\n"), stvar(strref, ev->available ? _S"came" : _S"is going"));
    if (ev->available)
        cxuiCanvasInvalidateAll(d->view);
    else
        d->hasSurface = false;
    renderStatus(d);
    return CXUI_Default;
}

static CxUiResult renderResize(stvlist* cvars, CxUiResizeEvent* ev)
{
    RenderDemo* d = stvlAtPtr(cvars, 0);
    if (d->hasSurface)
        cxuiRenderViewSurface(d->view, &d->surface);
    unused_noeval(ev);
    renderStatus(d);
    return CXUI_Default;
}

static CxUiResult renderPaint(stvlist* cvars, CxUiPaintEvent* ev)
{
    unused_noeval(ev);
    RenderDemo* d = stvlAtPtr(cvars, 0);
    d->frames++;
    renderFrame(d);
    renderStatus(d);
    return CXUI_Handled;
}

static CxUiResult renderMouse(stvlist* cvars, CxUiMouseEvent* ev)
{
    RenderDemo* d = stvlAtPtr(cvars, 0);
    if (ev->action == CXUI_MOUSE_Down) {
        d->clicks++;
        d->click = (CxUiPoint){ ev->x, ev->y };
        renderStatus(d);
    }
    return CXUI_Default;
}

static CxUiResult renderWheel(stvlist* cvars, CxUiWheelEvent* ev)
{
    unused_noeval(ev);
    RenderDemo* d = stvlAtPtr(cvars, 0);
    d->wheels++;
    renderStatus(d);
    return CXUI_Handled;
}

static bool renderGone(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    RenderDemo* d = stvlAtPtr(cvars, 0);
    objRelease(&d->status);
    xaFree(d);
    return true;
}

static CxUiResult openRenderView(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui gallery: render view", .size = { 520, 380 },
                                                        .closeAction = CXUI_OnClose_Destroy });
    galleryFollowTheme(w);
    RenderDemo* d = xaAllocStruct(RenderDemo, XA_Zero);
    CxUiBox* col  = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6, .margin = { 8, 8, 8, 8 } });
    d->status     = cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"No surface yet", .wrap = true });
    d->view       = cxuiRenderViewCreate(NULL);
    cxuiWidgetSetNaturalSize(d->view, cxuiLen(320), cxuiLen(200));
    cxuiOnSurface(d->view, renderSurface, stvar(ptr, d));
    cxuiOnResize(d->view, renderResize, stvar(ptr, d));
    cxuiOnPaint(d->view, renderPaint, stvar(ptr, d));
    cxuiOnMouse(d->view, renderMouse, stvar(ptr, d));
    cxuiOnWheel(d->view, renderWheel, stvar(ptr, d));
    cchainAttach(&CxUiWidget(d->view)->destroyNotify, renderGone, stvar(ptr, d));
    galleryAdd(col, d->view);
    cxuiContainerAppend(col, d->status);
    galleryAdd(w, col);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    objRelease(&w);
    return CXUI_Default;
}

static void windowButton(void* row, strref title, bool dip, uint32 engine)
{
    CxUiButton* b = cxuiButtonCreate(&(CxUiButtonDesc){ .title = title });
    cxuiOnActivate(b, openWindow, stvar(bool, dip), stvar(uint32, engine));
    galleryAdd(row, b);
}

static void build(CxUiBox* box)
{
    galleryAdd(box, cxuiLabelCreate(&(CxUiLabelDesc){
                        .text = _S"Drag in the canvas for a rubber band, and click it then press C to change the "
                                "cursor; the console shows the mouse, wheel and keys. Move a window to a monitor "
                                "with another scale and the drawing should stay sharp.",
                        .wrap = true }));
    CxUiBox* row = galleryRow(box, 6);
    windowButton(row, _S"In &pixels", false, CXUI_Draw_Auto);
    windowButton(row, _S"In &DIPs", true, CXUI_Draw_Auto);
#if defined(_PLATFORM_WIN)
    windowButton(row, _S"&GDI", false, CXUI_Draw_GDI);
    windowButton(row, _S"Direct&2D", false, CXUI_Draw_D2D);
#endif
    CxUiButton* rv = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"&Render view" });
    cxuiOnActivate(rv, openRenderView, stvNone);
    galleryAdd(row, rv);
    objRelease(&row);

    embedded = demoCreate(true, CXUI_Draw_Auto);
    cxuiWidgetSetNaturalSize(embedded->cv, cxuiLen(480), cxuiLen(300));
    cxuiWidgetSetExpand(embedded->cv, CXUI_EXPAND_Both);
    cxuiContainerAppend(box, embedded->cv);
}

static void scaled(void)
{
    if (embedded)
        demoFonts(embedded);
}

const GalleryPage galleryCanvas = {
    .name   = _S"canvas",
    .title  = _S"Canvas",
    .build  = build,
    .scaled = scaled,
};
