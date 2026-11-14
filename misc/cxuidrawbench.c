// Times cxui's drawing engines on a full-HD offscreen target, through the same front end and
// engine code a canvas uses, without a window.
//
// Workloads:
//   text    500 runs of 80 characters in four colors, and 500 fills (the budget: under 3 ms)
//   cells   500 cell runs of 80 characters
//   images  500 parts of an atlas, 32x32 each
//   stream  a log view: 60 rows of 4 runs, repainting only the bottom row each frame
//
// Then pieces of those, to tell fixed costs from per-run and per-glyph ones, and on Windows the
// same 500 runs drawn with ExtTextOutW directly, without cxui, at three font qualities.
//
// GSK builds render nodes when drawing; the GPU draws them later. Its times are for building the
// nodes, and "render" adds drawing them with the cairo renderer on the CPU, which is far slower
// than GTK's GL renderer and only a rough upper bound.

#include <cx/console.h>
#include <cx/sys/entry.h>
#include <cx/time.h>
#include <cxui.h>
#include <cxui/test/cxuitest.h>
#include <math.h>

#define W      1920
#define H      1080
#define WARMUP 3
#define FRAMES 20

typedef struct Bench {
    string shortLine;   // 10 characters
    bool opaqueImages;
    CxUiImage* opaque;
    CxUiImage* opaqueParts[64];
    CxUiFont* font;
    CxUiImage* atlas;
    CxUiImage* parts[64];
    string line;   // 80 characters
    string runs[4];
    int32 rowH;
    int32 cellW;
} Bench;

static const CxUiColor colors[4] = { CXUI_HEX(0xE0E0E0), CXUI_HEX(0x80C0FF), CXUI_HEX(0xFFC060), CXUI_HEX(0xFF6060) };

static void drawText(CxUiDraw* d, void* ctx)
{
    Bench* b = ctx;
    cxuiDrawFill(d, 0, 0, W, H, cxuiHex(0x1E1E1E));
    // 60 rows down, overlapping across, so every run is inside the target and drawn
    for (int32 i = 0; i < 500; i++)
        cxuiDrawText(d, b->font, (i / 60) * 220, (i % 60) * 18, b->line, colors[i & 3]);
    for (int32 i = 0; i < 500; i++)
        cxuiDrawFill(d, (i * 37) % (W - 40), (i * 53) % (H - 20), 40, 12, colors[(i >> 2) & 3]);
}

static void drawOneFill(CxUiDraw* d, void* ctx)
{
    unused_noeval(ctx);
    cxuiDrawFill(d, 0, 0, W, H, cxuiHex(0x1E1E1E));
}

static void drawFills(CxUiDraw* d, void* ctx)
{
    unused_noeval(ctx);
    for (int32 i = 0; i < 500; i++)
        cxuiDrawFill(d, (i * 37) % (W - 40), (i * 53) % (H - 20), 40, 12, colors[(i >> 2) & 3]);
}

static void drawRuns(CxUiDraw* d, Bench* b, strref line)
{
    for (int32 i = 0; i < 500; i++)
        cxuiDrawText(d, b->font, (i / 60) * 220, (i % 60) * 18, line, colors[i & 3]);
}

static void drawLongRuns(CxUiDraw* d, void* ctx)
{
    drawRuns(d, ctx, ((Bench*)ctx)->line);
}

static void drawShortRuns(CxUiDraw* d, void* ctx)
{
    drawRuns(d, ctx, ((Bench*)ctx)->shortLine);
}

static void drawOpaqueImages(CxUiDraw* d, void* ctx)
{
    Bench* b = ctx;
    for (int32 i = 0; i < 500; i++)
        cxuiDrawImage(d, b->opaqueParts[i & 63], NULL, (CxUiRect){ (i * 61) % (W - 32), (i * 43) % (H - 32), 32, 32 });
}

static void drawCells(CxUiDraw* d, void* ctx)
{
    Bench* b = ctx;
    cxuiDrawFill(d, 0, 0, W, H, cxuiHex(0x1E1E1E));
    for (int32 i = 0; i < 500; i++)
        cxuiDrawTextCells(d, b->font, (i / 60) * 220, (i % 60) * 18, b->line, b->cellW, colors[i & 3]);
}

static void drawImages(CxUiDraw* d, void* ctx)
{
    Bench* b = ctx;
    cxuiDrawFill(d, 0, 0, W, H, cxuiHex(0x1E1E1E));
    for (int32 i = 0; i < 500; i++)
        cxuiDrawImage(d, b->parts[i & 63], NULL, (CxUiRect){ (i * 61) % (W - 32), (i * 43) % (H - 32), 32, 32 });
}

// A log viewer's row view: a background per row and four runs (time, level, source, message)
static void drawRows(CxUiDraw* d, void* ctx)
{
    Bench* b      = ctx;
    CxUiRect dirt = cxuiDrawDirty(d);
    for (int32 row = 0; row < 60; row++) {
        int32 y = row * b->rowH;
        if (y + b->rowH <= dirt.y || y >= dirt.y + dirt.h)
            continue;
        cxuiDrawFill(d, 0, y, W, b->rowH, cxuiHex(row & 1 ? 0x202020 : 0x262626));
        int32 x = 4;
        for (int32 r = 0; r < 4; r++)
            x += cxuiDrawText(d, b->font, x, y, b->runs[r], colors[r]) + 8;
    }
}

typedef struct Timing {
    float64 avg, min;
} Timing;

static Timing timeFrames(CxUiTestOffscreen* o, CxUiTestDrawFn fn, Bench* b, CxUiRect dirty, uint8* readback)
{
    Timing t = { 0, 1e9 };
    for (int32 i = 0; i < WARMUP + FRAMES; i++) {
        int64 start = clockTimer();
        cxuiTestOffscreenDraw(o, dirty, fn, b);
        if (readback)
            cxuiTestOffscreenRead(o, readback);
        float64 ms = (clockTimer() - start) / 1000.0;
        if (i >= WARMUP) {
            t.avg += ms / FRAMES;
            t.min = min(t.min, ms);
        }
    }
    return t;
}

static void report(strref engine, strref what, Timing t, float64 budget)
{
    conFmt(conOut(), _SL("${string}: ${string}: ${float} ms average, ${float} ms best${string}\n"),
           stvar(strref, engine), stvar(strref, what), stvar(float64, floor(t.avg * 100 + 0.5) / 100),
           stvar(float64, floor(t.min * 100 + 0.5) / 100),
           stvar(strref, budget > 0 ? (t.avg <= budget ? _S"  (within budget)" : _S"  (OVER BUDGET)") : _S""));
    conFlush(conOut());
}

static void benchEngine(uint32 which, string* seen)
{
    CxUiTestOffscreen* o = cxuiTestOffscreenCreate(which, W, H);
    if (!o)
        return;
    strref engine = cxuiTestOffscreenEngine(o);
    if (strFind(*seen, 0, engine) >= 0) {
        cxuiTestOffscreenDestroy(&o);
        return;
    }
    strAppend(seen, engine);
    strAppend(seen, _S" ");

    Bench b = { 0 };
    b.font  = cxuiTestOffscreenFont(o, &(CxUiFontDesc){ .faces = _S"Consolas;monospace", .size = -13 });
    CxUiFontMetrics m;
    cxuiFontMetrics(b.font, &m);
    b.rowH  = m.height;
    b.cellW = m.cellW;
    strDup(&b.line, _S"12:34:56.789 [Info ] cxui.bench: the quick brown fox jumps over the lazy dog 0123");
    strDup(&b.shortLine, _S"12:34:56.7");
    strDup(&b.runs[0], _S"12:34:56.789");
    strDup(&b.runs[1], _S"[Info ]");
    strDup(&b.runs[2], _S"cxui.bench");
    strDup(&b.runs[3], _S"a message of moderate length, as most log lines are, with a number 12345");

    uint8* px = xaAlloc(256 * 256 * 4);
    for (int32 i = 0; i < 256 * 256; i++) {
        px[i * 4]     = (uint8)(i & 255);
        px[i * 4 + 1] = (uint8)(i >> 8);
        px[i * 4 + 2] = 128;
        px[i * 4 + 3] = (i & 15) < 8 ? 255 : 160;
    }
    b.atlas = cxuiImageCreateRGBA(256, 256, 256 * 4, px);
    for (int32 i = 0; i < 256 * 256; i++)
        px[i * 4 + 3] = 255;
    b.opaque = cxuiImageCreateRGBA(256, 256, 256 * 4, px);
    xaFree(px);
    for (int32 i = 0; i < 64; i++) {
        b.parts[i]       = cxuiImageCreateSub(b.atlas, (CxUiRect){ (i % 8) * 32, (i / 8) * 32, 32, 32 });
        b.opaqueParts[i] = cxuiImageCreateSub(b.opaque, (CxUiRect){ (i % 8) * 32, (i / 8) * 32, 32, 32 });
    }

    CxUiRect all = { 0, 0, W, H };
    report(engine, _S"text + fills", timeFrames(o, drawText, &b, all, NULL), 3.0);
    report(engine, _S"cells", timeFrames(o, drawCells, &b, all, NULL), 0);
    report(engine, _S"images", timeFrames(o, drawImages, &b, all, NULL), 0);
    cxuiTestOffscreenDraw(o, all, drawRows, &b);
    report(engine, _S"stream, 1 row", timeFrames(o, drawRows, &b, (CxUiRect){ 0, 59 * b.rowH, W, b.rowH }, NULL), 0);
    report(engine, _S"  one full fill", timeFrames(o, drawOneFill, &b, all, NULL), 0);
    report(engine, _S"  500 fills", timeFrames(o, drawFills, &b, all, NULL), 0);
    report(engine, _S"  500 x 10 chars", timeFrames(o, drawShortRuns, &b, all, NULL), 0);
    report(engine, _S"  500 x 80 chars", timeFrames(o, drawLongRuns, &b, all, NULL), 0);
    report(engine, _S"  500 opaque images", timeFrames(o, drawOpaqueImages, &b, all, NULL), 0);
    if (strEq(engine, _S"gsk")) {
        uint8* rb = xaAlloc((size_t)W * H * 4);
        report(engine, _S"text + render", timeFrames(o, drawText, &b, all, rb), 0);
        xaFree(rb);
    }

    for (int32 i = 0; i < 64; i++) {
        objRelease(&b.parts[i]);
        objRelease(&b.opaqueParts[i]);
    }
    objRelease(&b.atlas);
    objRelease(&b.opaque);
    strDestroy(&b.shortLine);
    objRelease(&b.font);
    strDestroy(&b.line);
    for (int32 i = 0; i < 4; i++)
        strDestroy(&b.runs[i]);
    cxuiTestOffscreenDestroy(&o);
}

#if defined(_PLATFORM_WIN)
#include <cx/platform/win.h>

typedef struct RawTarget {
    HDC dc;
    HBITMAP bmp;
    HGDIOBJ old;
} RawTarget;

static void rawInit(RawTarget* r)
{
    BITMAPINFO bmi              = { 0 };
    bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth       = W;
    bmi.bmiHeader.biHeight      = -H;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits                  = NULL;
    r->dc                       = CreateCompatibleDC(NULL);
    r->bmp                      = CreateDIBSection(r->dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    r->old                      = SelectObject(r->dc, r->bmp);
}

static void rawDestroy(RawTarget* r)
{
    SelectObject(r->dc, r->old);
    DeleteObject(r->bmp);
    DeleteDC(r->dc);
}

// 500 small fills three ways, with and without a clip region
static void rawFills(int how, bool clip, strref name)
{
    RawTarget r;
    rawInit(&r);
    if (clip)
        IntersectClipRect(r.dc, 0, 0, W, H);
    SelectObject(r.dc, GetStockObject(DC_BRUSH));
    static const COLORREF cols[4] = { RGB(0xE0, 0xE0, 0xE0), RGB(0x80, 0xC0, 0xFF), RGB(0xFF, 0xC0, 0x60), RGB(0xFF, 0x60, 0x60) };
    Timing t = { 0, 1e9 };
    for (int32 f = 0; f < WARMUP + FRAMES; f++) {
        int64 start = clockTimer();
        for (int32 i = 0; i < 500; i++) {
            RECT rc = { (i * 37) % (W - 40), (i * 53) % (H - 20), 0, 0 };
            rc.right  = rc.left + 40;
            rc.bottom = rc.top + 12;
            COLORREF c = cols[(i >> 2) & 3];
            if (how == 0) {
                SetBkColor(r.dc, c);
                ExtTextOutW(r.dc, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL);
            } else if (how == 1) {
                SetDCBrushColor(r.dc, c);
                PatBlt(r.dc, rc.left, rc.top, 40, 12, PATCOPY);
            } else {
                SetDCBrushColor(r.dc, c);
                FillRect(r.dc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
            }
        }
        GdiFlush();
        float64 ms = (clockTimer() - start) / 1000.0;
        if (f >= WARMUP) {
            t.avg += ms / FRAMES;
            t.min = min(t.min, ms);
        }
    }
    report(_S"raw gdi", name, t, 0);
    rawDestroy(&r);
}

// The same 500 runs of 80 characters, drawn with ExtTextOutW directly on a DIB section: what GDI
// costs without cxui.
enum { RAW_Plain, RAW_Dx, RAW_ClipRegion, RAW_ClipRect, RAW_Short };

static void rawGdi(int quality, int how, strref name)
{
    BITMAPINFO bmi              = { 0 };
    bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth       = W;
    bmi.bmiHeader.biHeight      = -H;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits                  = NULL;
    HDC dc                      = CreateCompatibleDC(NULL);
    HBITMAP bmp                 = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ oldBmp              = SelectObject(dc, bmp);
    LOGFONTW lf                 = { 0 };
    lf.lfHeight                 = -13;
    lf.lfWeight                 = FW_NORMAL;
    lf.lfCharSet                = DEFAULT_CHARSET;
    lf.lfQuality                = (BYTE)quality;
    memcpy(lf.lfFaceName, L"Consolas", sizeof(L"Consolas"));
    HFONT font     = CreateFontIndirectW(&lf);
    HGDIOBJ oldFnt = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    RECT clip = { 0, 0, W, H };
    if (how == RAW_ClipRegion)
        IntersectClipRect(dc, 0, 0, W, H);
    static const WCHAR line[] = L"12:34:56.789 [Info ] cxui.bench: the quick brown fox jumps over the lazy dog 0123";
    INT dx[sizeof(line) / sizeof(WCHAR)];
    for (int32 i = 0; i < (int32)(sizeof(dx) / sizeof(dx[0])); i++)
        dx[i] = 7;
    static const COLORREF cols[4] = { RGB(0xE0, 0xE0, 0xE0), RGB(0x80, 0xC0, 0xFF), RGB(0xFF, 0xC0, 0x60), RGB(0xFF, 0x60, 0x60) };

    Timing t = { 0, 1e9 };
    for (int32 f = 0; f < WARMUP + FRAMES; f++) {
        int64 start = clockTimer();
        RECT all    = { 0, 0, W, H };
        SetBkColor(dc, RGB(0x1E, 0x1E, 0x1E));
        ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &all, NULL, 0, NULL);
        for (int32 i = 0; i < 500; i++) {
            SetTextColor(dc, cols[i & 3]);
            UINT len = how == RAW_Short ? 10 : (UINT)(sizeof(line) / sizeof(WCHAR) - 1);
            ExtTextOutW(dc, (i / 60) * 220, (i % 60) * 18, how == RAW_ClipRect ? ETO_CLIPPED : 0,
                        how == RAW_ClipRect ? &clip : NULL, line, len, how == RAW_Dx ? dx : NULL);
        }
        GdiFlush();
        float64 ms = (clockTimer() - start) / 1000.0;
        if (f >= WARMUP) {
            t.avg += ms / FRAMES;
            t.min = min(t.min, ms);
        }
    }
    report(_S"raw gdi", name, t, 0);
    SelectObject(dc, oldFnt);
    DeleteObject(font);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// GDI+ through its flat API, loaded at run time
typedef struct GpStartupInput {
    UINT32 version;
    void* debugCallback;
    BOOL suppressBackgroundThread;
    BOOL suppressExternalCodecs;
} GpStartupInput;
typedef struct GpPointF {
    float x, y;
} GpPointF;
typedef struct GpRectF {
    float x, y, w, h;
} GpRectF;

typedef int(WINAPI* GdiplusStartupFn)(ULONG_PTR* token, const GpStartupInput* in, void* out);
typedef int(WINAPI* GdipCreateFromHDCFn)(HDC dc, void** g);
typedef int(WINAPI* GdipDeleteGraphicsFn)(void* g);
typedef int(WINAPI* GdipCreateFontFromLogfontWFn)(HDC dc, const LOGFONTW* lf, void** font);
typedef int(WINAPI* GdipDeleteFontFn)(void* font);
typedef int(WINAPI* GdipCreateSolidFillFn)(DWORD argb, void** brush);
typedef int(WINAPI* GdipSetSolidFillColorFn)(void* brush, DWORD argb);
typedef int(WINAPI* GdipDeleteBrushFn)(void* brush);
typedef int(WINAPI* GdipSetTextRenderingHintFn)(void* g, int hint);
typedef int(WINAPI* GdipDrawStringFn)(void* g, const WCHAR* s, INT len, const void* font, const GpRectF* rect,
                                      const void* fmt, const void* brush);
typedef int(WINAPI* GdipDrawDriverStringFn)(void* g, const UINT16* s, INT len, const void* font, const void* brush,
                                            const GpPointF* pos, INT flags, const void* matrix);
typedef int(WINAPI* GdipFillRectangleIFn)(void* g, void* brush, INT x, INT y, INT w, INT h);

#define GP(name) name##Fn p##name = (name##Fn)GetProcAddress(gp, #name)

static void rawGdiPlus(void)
{
    HMODULE gp = LoadLibraryW(L"gdiplus.dll");
    if (!gp) {
        conPuts(conOut(), _SL("raw gdi+: not available\n"));
        return;
    }
    GP(GdiplusStartup);
    GP(GdipCreateFromHDC);
    GP(GdipDeleteGraphics);
    GP(GdipCreateFontFromLogfontW);
    GP(GdipDeleteFont);
    GP(GdipCreateSolidFill);
    GP(GdipSetSolidFillColor);
    GP(GdipDeleteBrush);
    GP(GdipSetTextRenderingHint);
    GP(GdipDrawString);
    GP(GdipDrawDriverString);
    GP(GdipFillRectangleI);
    ULONG_PTR token = 0;
    GpStartupInput in = { 1, NULL, FALSE, FALSE };
    if (!pGdiplusStartup || pGdiplusStartup(&token, &in, NULL) != 0)
        return;

    RawTarget r;
    rawInit(&r);
    LOGFONTW lf  = { 0 };
    lf.lfHeight  = -13;
    lf.lfWeight  = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    memcpy(lf.lfFaceName, L"Consolas", sizeof(L"Consolas"));
    static const WCHAR line[] = L"12:34:56.789 [Info ] cxui.bench: the quick brown fox jumps over the lazy dog 0123";
    const INT len             = (INT)(sizeof(line) / sizeof(WCHAR) - 1);
    static const DWORD cols[4] = { 0xFFE0E0E0, 0xFF80C0FF, 0xFFFFC060, 0xFFFF6060 };
    GpPointF pos[sizeof(line) / sizeof(WCHAR)];
    void* font = NULL;
    void* brush = NULL;
    pGdipCreateFontFromLogfontW(r.dc, &lf, &font);
    pGdipCreateSolidFill(cols[0], &brush);

    // 0: DrawString, 1: DrawDriverString, 2: fills; hint 4 is antialiased, 5 ClearType
    static const struct {
        int what, hint;
        const char* name;
    } runs[] = { { 0, 4, "80 chars, DrawString, grayscale" },     { 0, 5, "80 chars, DrawString, ClearType" },
                 { 1, 4, "80 chars, DrawDriverString, grayscale" }, { 1, 5, "80 chars, DrawDriverString, ClearType" },
                 { 2, 4, "500 fills" } };
    for (int32 k = 0; k < (int32)(sizeof(runs) / sizeof(runs[0])); k++) {
        void* g = NULL;
        pGdipCreateFromHDC(r.dc, &g);
        pGdipSetTextRenderingHint(g, runs[k].hint);
        Timing t = { 0, 1e9 };
        for (int32 f = 0; f < WARMUP + FRAMES; f++) {
            int64 start = clockTimer();
            for (int32 i = 0; i < 500; i++) {
                float x = (float)((i / 60) * 220), y = (float)((i % 60) * 18);
                pGdipSetSolidFillColor(brush, cols[i & 3]);
                if (runs[k].what == 0) {
                    GpRectF rc = { x, y, 2000, 40 };
                    pGdipDrawString(g, line, len, font, &rc, NULL, brush);
                } else if (runs[k].what == 1) {
                    for (INT c = 0; c < len; c++)
                        pos[c] = (GpPointF){ x + c * 7.0f, y + 13 };
                    pGdipDrawDriverString(g, (const UINT16*)line, len, font, brush, pos, 1, NULL);
                } else {
                    pGdipFillRectangleI(g, brush, (i * 37) % (W - 40), (i * 53) % (H - 20), 40, 12);
                }
            }
            GdiFlush();
            float64 ms = (clockTimer() - start) / 1000.0;
            if (f >= WARMUP) {
                t.avg += ms / FRAMES;
                t.min = min(t.min, ms);
            }
        }
        pGdipDeleteGraphics(g);
        report(_S"raw gdi+", (strref)runs[k].name, t, 0);
    }
    pGdipDeleteBrush(brush);
    pGdipDeleteFont(font);
    rawDestroy(&r);
}
#endif

int entryPoint(void)
{
    string seen = 0;
    conFmt(conOut(), _SL("cxui draw bench: ${int}x${int}, average of ${int} frames\n"), stvar(int32, W), stvar(int32, H),
           stvar(int32, FRAMES));
    benchEngine(CXUI_Draw_GDI, &seen);
    benchEngine(CXUI_Draw_D2D, &seen);
    benchEngine(CXUI_Draw_Auto, &seen);
    if (strEmpty(seen))
        conPuts(conOut(), _SL("no drawing engines on this platform yet\n"));
#if defined(_PLATFORM_WIN)
    rawGdi(CLEARTYPE_QUALITY, RAW_Plain, _S"80 chars, ClearType");
    rawGdi(CLEARTYPE_QUALITY, RAW_Dx, _S"80 chars, ClearType, lpDx");
    rawGdi(CLEARTYPE_QUALITY, RAW_ClipRegion, _S"80 chars, ClearType, clip region");
    rawGdi(CLEARTYPE_QUALITY, RAW_ClipRect, _S"80 chars, ClearType, ETO_CLIPPED");
    rawGdi(CLEARTYPE_QUALITY, RAW_Short, _S"10 chars, ClearType");
    rawGdi(ANTIALIASED_QUALITY, RAW_Plain, _S"80 chars, grayscale");
    rawGdi(NONANTIALIASED_QUALITY, RAW_Plain, _S"80 chars, aliased");
    rawFills(0, false, _S"500 fills, ExtTextOut");
    rawFills(1, false, _S"500 fills, PatBlt");
    rawFills(2, false, _S"500 fills, FillRect");
    rawFills(0, true, _S"500 fills, ExtTextOut, clipped");
    rawGdiPlus();
#endif
    strDestroy(&seen);
    return 0;
}

DEFINE_ENTRY_POINT;
