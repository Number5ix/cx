#include <cx/string.h>
#include <cxui.h>
#include <cxui/test/cxuitest.h>

#define TEST_FILE cxuidrawtest
#define TEST_FUNCS cxuidrawtest_funcs
#include "common.h"

// Pixel tests for each drawing engine the platform has, through offscreen targets: the same
// front end and engine code a canvas uses, with no window. Geometry is checked exactly; blending
// within 1 per channel.

#define PW 192
#define PH 160

// A model of what the image should hold: RGB per pixel.
typedef struct Image {
    uint8 px[PW * PH * 4];
} Image;

static void imgFill(Image* img, int32 x, int32 y, int32 w, int32 h, CxUiColor c)
{
    for (int32 j = max(y, 0); j < min(y + h, PH); j++) {
        for (int32 i = max(x, 0); i < min(x + w, PW); i++) {
            uint8* p = &img->px[(j * PW + i) * 4];
            p[0]     = c.r;
            p[1]     = c.g;
            p[2]     = c.b;
        }
    }
}

// The first pixel whose RGB differs by more than tol, or -1
static int32 imgDiff(const Image* want, const uint8* got, int32 tol)
{
    for (int32 i = 0; i < PW * PH; i++) {
        for (int32 ch = 0; ch < 3; ch++) {
            if (abs((int32)want->px[i * 4 + ch] - (int32)got[i * 4 + ch]) > tol)
                return i;
        }
    }
    return -1;
}

static int failAt(strref engine, strref what, const Image* want, const uint8* got, int32 i)
{
    TEST_FAIL(1, _SL("${string}: ${string}: pixel ${int},${int} is ${int} ${int} ${int}, expected ${int} ${int} ${int}"),
              stvar(strref, engine), stvar(strref, what), stvar(int32, i % PW), stvar(int32, i / PW),
              stvar(int32, got[i * 4]), stvar(int32, got[i * 4 + 1]), stvar(int32, got[i * 4 + 2]),
              stvar(int32, want->px[i * 4]), stvar(int32, want->px[i * 4 + 1]), stvar(int32, want->px[i * 4 + 2]));
}

typedef int (*EngineCheck)(CxUiTestOffscreen* o, strref engine);

// Runs check on each engine the platform has; skips if there are none.
static int forEachEngine(EngineCheck check)
{
    static const uint32 engines[] = { CXUI_Draw_GDI, CXUI_Draw_D2D, CXUI_Draw_Auto };
    string seen                   = 0;
    int32 ran                     = 0;
    for (int32 i = 0; i < (int32)(sizeof(engines) / sizeof(engines[0])); i++) {
        CxUiTestOffscreen* o = cxuiTestOffscreenCreate(engines[i], PW, PH);
        if (!o)
            continue;
        strref name = cxuiTestOffscreenEngine(o);
        if (strFind(seen, 0, name) < 0) {
            strAppend(&seen, name);
            strAppend(&seen, _S" ");
            ran++;
            int ret = check(o, name);
            if (ret) {
                cxuiTestOffscreenDestroy(&o);
                strDestroy(&seen);
                return ret;
            }
        }
        cxuiTestOffscreenDestroy(&o);
    }
    if (ran == 0) {
        strDestroy(&seen);
        TEST_SKIP(_SL("no drawing engines on this platform yet"), stvNone);
    }
    TEST_INFO(_SL("checked: ${string}"), stvar(string, seen));
    strDestroy(&seen);
    return 0;
}

static const CxUiColor white = CXUI_HEX(0xFFFFFF);
static const CxUiColor red   = CXUI_HEX(0xFF0000);
static const CxUiColor green = CXUI_HEX(0x00FF00);
static const CxUiColor blue  = CXUI_HEX(0x0000FF);
static const CxUiColor black = CXUI_HEX(0x000000);

// ---- exact geometry ----

static void drawShapes(CxUiDraw* d, void* ctx)
{
    unused_noeval(ctx);
    cxuiDrawFill(d, 0, 0, PW, PH, white);
    cxuiDrawFill(d, 10, 10, 20, 5, red);
    cxuiDrawFrame(d, 40, 40, 10, 10, 1, blue);
    cxuiDrawLine(d, 0, 60, 63, 60, 1, green);
    cxuiDrawLine(d, 5, 0, 5, 30, 2, black);
}

static int checkShapes(CxUiTestOffscreen* o, strref engine)
{
    Image want;
    imgFill(&want, 0, 0, PW, PH, white);
    imgFill(&want, 10, 10, 20, 5, red);
    imgFill(&want, 40, 40, 10, 1, blue);
    imgFill(&want, 40, 49, 10, 1, blue);
    imgFill(&want, 40, 41, 1, 8, blue);
    imgFill(&want, 49, 41, 1, 8, blue);
    imgFill(&want, 0, 60, 64, 1, green);
    imgFill(&want, 5, 0, 2, 31, black);

    uint8 got[PW * PH * 4];
    cxuiTestOffscreenDraw(o, (CxUiRect){ 0, 0, PW, PH }, drawShapes, NULL);
    cxuiTestOffscreenRead(o, got);
    int32 i = imgDiff(&want, got, 0);
    return i >= 0 ? failAt(engine, _S"shapes", &want, got, i) : 0;
}

// Fills, frames and straight lines land on exact pixels.
static int test_draw_pshapes(void)
{
    return forEachEngine(checkShapes);
}

// ---- clipping ----

static void drawClip(CxUiDraw* d, void* ctx)
{
    unused_noeval(ctx);
    cxuiDrawFill(d, 0, 0, PW, PH, white);
    cxuiDrawClipPush(d, (CxUiRect){ 8, 8, 16, 16 });
    cxuiDrawFill(d, 0, 0, PW, PH, red);
    cxuiDrawClipPush(d, (CxUiRect){ 20, 20, 30, 30 });
    cxuiDrawFill(d, 0, 0, PW, PH, blue);
    cxuiDrawLine(d, 0, 0, 63, 50, 1, green);   // angled lines obey the clip too
    cxuiDrawClipPop(d);
    cxuiDrawClipPop(d);
    cxuiDrawFill(d, 0, 0, 4, 4, green);
}

static int checkClip(CxUiTestOffscreen* o, strref engine)
{
    Image want;
    imgFill(&want, 0, 0, PW, PH, white);
    imgFill(&want, 8, 8, 16, 16, red);
    imgFill(&want, 20, 20, 4, 4, blue);
    imgFill(&want, 0, 0, 4, 4, green);

    uint8 got[PW * PH * 4];
    cxuiTestOffscreenDraw(o, (CxUiRect){ 0, 0, PW, PH }, drawClip, NULL);
    cxuiTestOffscreenRead(o, got);
    // The angled line may only touch the 4x4 blue square, which is the clip it was drawn in.
    for (int32 y = 20; y < 24; y++) {
        for (int32 x = 20; x < 24; x++)
            memcpy(&want.px[(y * PW + x) * 4], &got[(y * PW + x) * 4], 3);
    }
    int32 i = imgDiff(&want, got, 0);
    return i >= 0 ? failAt(engine, _S"clipping", &want, got, i) : 0;
}

// Clip pushes intersect, pops restore, and nothing lands outside.
static int test_draw_pclip(void)
{
    return forEachEngine(checkClip);
}

// ---- alpha ----

static const CxUiColor alphaBase = CXUI_HEX(0xC86432);
static const uint8 alphas[]      = { 1, 64, 128, 200, 254 };
static const CxUiColor alphaSrc[] = { CXUI_HEX(0x0000FF), CXUI_HEX(0xFFFFFF), CXUI_HEX(0x10E080) };

static void drawAlpha(CxUiDraw* d, void* ctx)
{
    unused_noeval(ctx);
    cxuiDrawFill(d, 0, 0, PW, PH, alphaBase);
    for (int32 i = 0; i < 5; i++) {
        for (int32 j = 0; j < 3; j++) {
            CxUiColor c = alphaSrc[j];
            c.a         = alphas[i];
            cxuiDrawFill(d, i * 12, j * 20, 10, 16, c);
        }
    }
}

static uint8 blend(uint8 src, uint8 dst, uint8 a)
{
    return (uint8)((src * a + dst * (255 - a) + 127) / 255);
}

static int checkAlpha(CxUiTestOffscreen* o, strref engine)
{
    Image want;
    imgFill(&want, 0, 0, PW, PH, alphaBase);
    for (int32 i = 0; i < 5; i++) {
        for (int32 j = 0; j < 3; j++) {
            CxUiColor s = alphaSrc[j];
            imgFill(&want, i * 12, j * 20, 10, 16,
                    cxuiRGB(blend(s.r, alphaBase.r, alphas[i]), blend(s.g, alphaBase.g, alphas[i]),
                            blend(s.b, alphaBase.b, alphas[i])));
        }
    }

    uint8 got[PW * PH * 4];
    cxuiTestOffscreenDraw(o, (CxUiRect){ 0, 0, PW, PH }, drawAlpha, NULL);
    cxuiTestOffscreenRead(o, got);
    int32 i = imgDiff(&want, got, 1);
    return i >= 0 ? failAt(engine, _S"alpha", &want, got, i) : 0;
}

// Translucent fills blend as straight alpha would, within 1 per channel.
static int test_draw_palpha(void)
{
    return forEachEngine(checkAlpha);
}

// ---- partial redraw ----

static void drawTiles(CxUiDraw* d, void* ctx)
{
    unused_noeval(ctx);
    for (int32 y = 0; y < PH; y += 8) {
        for (int32 x = 0; x < PW; x += 8)
            cxuiDrawFill(d, x, y, 8, 8, cxuiRGB((uint8)(x * 4), (uint8)(y * 4), 128));
    }
}

// Records the area it was given, which an engine may have grown from the one asked for.
static void drawBlack(CxUiDraw* d, void* ctx)
{
    *(CxUiRect*)ctx = cxuiDrawDirty(d);
    cxuiDrawFill(d, 0, 0, PW, PH, black);
    cxuiDrawLine(d, 0, 0, 63, 63, 3, white);
    cxuiDrawFill(d, 12, 33, 2, 2, red);   // inside the area asked for, so it lands wherever the pass is
}

static int checkPartial(CxUiTestOffscreen* o, strref engine)
{
    Image want;
    for (int32 y = 0; y < PH; y += 8) {
        for (int32 x = 0; x < PW; x += 8)
            imgFill(&want, x, y, 8, 8, cxuiRGB((uint8)(x * 4), (uint8)(y * 4), 128));
    }

    uint8 got[PW * PH * 4];
    CxUiRect dirty = { 0 };
    cxuiTestOffscreenDraw(o, (CxUiRect){ 0, 0, PW, PH }, drawTiles, NULL);
    cxuiTestOffscreenDraw(o, (CxUiRect){ 10, 30, 8, 8 }, drawBlack, &dirty);
    cxuiTestOffscreenRead(o, got);
    if (dirty.x > 10 || dirty.y > 30 || dirty.x + dirty.w < 18 || dirty.y + dirty.h < 38)
        TEST_FAIL(1, _SL("${string}: the pass was given ${int},${int} ${int}x${int}, less than was asked for"),
                  stvar(strref, engine), stvar(int32, dirty.x), stvar(int32, dirty.y), stvar(int32, dirty.w),
                  stvar(int32, dirty.h));
    if (dirty.w * dirty.h > 8 * 8)
        TEST_INFO(_SL("${string}: a pass for 10,30 8x8 was given ${int},${int} ${int}x${int}"), stvar(strref, engine),
                  stvar(int32, dirty.x), stvar(int32, dirty.y), stvar(int32, dirty.w), stvar(int32, dirty.h));
    // Black inside what the pass was given, except where the diagonal crosses it
    imgFill(&want, dirty.x, dirty.y, dirty.w, dirty.h, black);
    imgFill(&want, 12, 33, 2, 2, red);
    for (int32 y = dirty.y; y < dirty.y + dirty.h; y++) {
        for (int32 x = dirty.x; x < dirty.x + dirty.w; x++) {
            if (abs(x - y) <= 3)
                memcpy(&want.px[(y * PW + x) * 4], &got[(y * PW + x) * 4], 3);
        }
    }
    int32 i = imgDiff(&want, got, 0);
    return i >= 0 ? failAt(engine, _S"partial redraw", &want, got, i) : 0;
}

// A pass changes only the pixels inside its dirty rectangle.
static int test_draw_ppartial(void)
{
    return forEachEngine(checkPartial);
}

// ---- angled lines ----

static void drawDiagonal(CxUiDraw* d, void* ctx)
{
    unused_noeval(ctx);
    cxuiDrawFill(d, 0, 0, PW, PH, white);
    cxuiDrawLine(d, 4, 6, 60, 40, 1, black);
}

static bool inked(const uint8* got, int32 x, int32 y)
{
    const uint8* p = &got[(y * PW + x) * 4];
    return p[0] < 224 || p[1] < 224 || p[2] < 224;
}

static int checkDiagonal(CxUiTestOffscreen* o, strref engine)
{
    uint8 got[PW * PH * 4];
    cxuiTestOffscreenDraw(o, (CxUiRect){ 0, 0, PW, PH }, drawDiagonal, NULL);
    cxuiTestOffscreenRead(o, got);

    // Ink near both ends and the middle, and nowhere far from the line's box
    if (!inked(got, 4, 6) || !inked(got, 60, 40) || !(inked(got, 32, 23) || inked(got, 32, 22) || inked(got, 32, 24)))
        TEST_FAIL(1, _SL("${string}: the line is missing an end or its middle; green at 4,6 ${int}, 60,40 ${int}, "
                         "32,22-24 ${int} ${int} ${int}"),
                  stvar(strref, engine), stvar(int32, got[(6 * PW + 4) * 4 + 1]), stvar(int32, got[(40 * PW + 60) * 4 + 1]),
                  stvar(int32, got[(22 * PW + 32) * 4 + 1]), stvar(int32, got[(23 * PW + 32) * 4 + 1]),
                  stvar(int32, got[(24 * PW + 32) * 4 + 1]));
    for (int32 y = 0; y < PH; y++) {
        for (int32 x = 0; x < PW; x++) {
            if (inked(got, x, y) && (x < 3 || x > 61 || y < 5 || y > 41))
                TEST_FAIL(1, _SL("${string}: ink at ${int},${int}, outside the line's box"), stvar(strref, engine),
                          stvar(int32, x), stvar(int32, y));
        }
    }
    return 0;
}

// Lines at an angle reach both end pixels and stay near the line.
static int test_draw_pline(void)
{
    return forEachEngine(checkDiagonal);
}

// ---- text ----

typedef struct TextCtx {
    CxUiFont* f;
    int32 drawn;
} TextCtx;

static strref const sample = _S"Hxg, yjlM";

static void drawSample(CxUiDraw* d, void* ctx)
{
    TextCtx* t = ctx;
    cxuiDrawFill(d, 0, 0, PW, PH, white);
    t->drawn = cxuiDrawText(d, t->f, 10, 20, sample, black);
}

static void drawRedCells(CxUiDraw* d, void* ctx)
{
    TextCtx* t = ctx;
    cxuiDrawFill(d, 0, 0, PW, PH, white);
    cxuiDrawText(d, t->f, 10, 20, sample, red);
    t->drawn = cxuiDrawTextCells(d, t->f, 10, 80, _S"MiWi", 24, black);
}

// The bounding box of ink (anything not white) within r, or an empty rect
static CxUiRect inkBox(const uint8* px, CxUiRect r)
{
    int32 x0 = PW, y0 = PH, x1 = -1, y1 = -1;
    for (int32 y = max(r.y, 0); y < min(r.y + r.h, PH); y++) {
        for (int32 x = max(r.x, 0); x < min(r.x + r.w, PW); x++) {
            if (inked(px, x, y)) {
                x0 = min(x0, x);
                y0 = min(y0, y);
                x1 = max(x1, x);
                y1 = max(y1, y);
            }
        }
    }
    return x1 < 0 ? (CxUiRect){ 0 } : (CxUiRect){ x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
}

static int checkText(CxUiTestOffscreen* o, strref engine)
{
    TextCtx t = { .f = cxuiTestOffscreenFont(o, &(CxUiFontDesc){ .faces = _S"sans", .size = -16 }) };
    CxUiFontMetrics m;
    cxuiFontMetrics(t.f, &m);
    int32 adv = cxuiFontAdvance(t.f, sample);
    uint8 got[PW * PH * 4];
    cxuiTestOffscreenDraw(o, (CxUiRect){ 0, 0, PW, PH }, drawSample, &t);
    cxuiTestOffscreenRead(o, got);
    CxUiRect ink = inkBox(got, (CxUiRect){ 0, 0, PW, PH });
    TEST_INFO(_SL("${string}: ascent ${int}, height ${int}, advance ${int}, ink ${int},${int} ${int}x${int}"),
              stvar(strref, engine), stvar(int32, m.ascent), stvar(int32, m.height), stvar(int32, adv),
              stvar(int32, ink.x), stvar(int32, ink.y), stvar(int32, ink.w), stvar(int32, ink.h));

    int ret = 0;
    if (m.height < 14 || m.height > 26 || m.ascent <= m.height / 2 || m.ascent + m.descent > m.height + 1)
        TEST_FAILV(ret, 1, _SL("${string}: unlikely metrics for 16 px: ascent ${int}, descent ${int}, height ${int}"),
                         stvar(strref, engine), stvar(int32, m.ascent), stvar(int32, m.descent), stvar(int32, m.height));
    else if (t.drawn != adv || adv <= 0)
        TEST_FAILV(ret, 1, _SL("${string}: drawn advance ${int}, measured ${int}"), stvar(strref, engine),
                         stvar(int32, t.drawn), stvar(int32, adv));
    else if (ink.w == 0 || ink.x < 10 - 1 || ink.y < 20 - 1 || ink.x + ink.w > 10 + adv + 1 ||
             ink.y + ink.h > 20 + m.height + 1)
        TEST_FAILV(ret, 1, _SL("${string}: ink ${int},${int} ${int}x${int} outside the measured 10,20 ${int}x${int}"),
                         stvar(strref, engine), stvar(int32, ink.x), stvar(int32, ink.y), stvar(int32, ink.w),
                         stvar(int32, ink.h), stvar(int32, adv), stvar(int32, m.height));
    else if (ink.w < adv / 2 || ink.h < m.height / 2)
        TEST_FAILV(ret, 1, _SL("${string}: ink ${int}x${int} is too small for the measured ${int}x${int}"),
                         stvar(strref, engine), stvar(int32, ink.w), stvar(int32, ink.h), stvar(int32, adv),
                         stvar(int32, m.height));
    objRelease(&t.f);
    return ret;
}

// Text lands inside the box measuring gives, and the drawn advance is the measured one.
static int test_draw_ptext(void)
{
    return forEachEngine(checkText);
}

static int checkCells(CxUiTestOffscreen* o, strref engine)
{
    TextCtx t = { .f = cxuiTestOffscreenFont(o, &(CxUiFontDesc){ .faces = _S"sans", .size = -16 }) };
    uint8 got[PW * PH * 4];
    cxuiTestOffscreenDraw(o, (CxUiRect){ 0, 0, PW, PH }, drawRedCells, &t);
    cxuiTestOffscreenRead(o, got);
    objRelease(&t.f);

    // The red run is red: some pixel is strongly red and not dark.
    bool reddish = false;
    for (int32 y = 20; y < 50 && !reddish; y++) {
        for (int32 x = 10; x < PW && !reddish; x++) {
            const uint8* p = &got[(y * PW + x) * 4];
            reddish        = p[0] > 180 && p[1] < 110 && p[2] < 110;
        }
    }
    if (!reddish)
        TEST_FAIL(1, _SL("${string}: no red pixels in red text"), stvar(strref, engine));

    if (t.drawn != 4)
        TEST_FAIL(1, _SL("${string}: ${int} cells drawn, expected 4"), stvar(strref, engine), stvar(int32, t.drawn));
    CxUiRect all = inkBox(got, (CxUiRect){ 0, 70, PW, 50 });
    if (all.x < 10 - 1 || all.x + all.w > 10 + 4 * 24 + 1)
        TEST_FAIL(1, _SL("${string}: cell ink spans x ${int} to ${int}, outside the 4 cells"), stvar(strref, engine),
                  stvar(int32, all.x), stvar(int32, all.x + all.w));
    for (int32 n = 0; n < 4; n++) {
        // Glyph n starts in cell n: its cell has ink near its left edge.
        CxUiRect cell = inkBox(got, (CxUiRect){ 10 + n * 24, 70, 24, 50 });
        if (cell.w == 0 || cell.x > 10 + n * 24 + 12)
            TEST_FAIL(1, _SL("${string}: cell ${int} has ink at ${int}, width ${int}"), stvar(strref, engine),
                      stvar(int32, n), stvar(int32, cell.x), stvar(int32, cell.w));
    }
    return 0;
}

// A cell run puts glyph n in cell n, and text takes its color.
static int test_draw_pcells(void)
{
    return forEachEngine(checkCells);
}

// ---- images ----

typedef struct ImageCtx {
    CxUiImage* atlas;
    CxUiImage* part;
    CxUiImage* quads;
} ImageCtx;

// 16x16: red rises left to right, green top to bottom; the right half is half transparent.
static void atlasPixel(int32 x, int32 y, uint8* p)
{
    p[0] = (uint8)(x * 16);
    p[1] = (uint8)(y * 16);
    p[2] = 200;
    p[3] = x < 8 ? 255 : 128;
}

static const CxUiColor quadColors[4] = { CXUI_HEX(0xC02020), CXUI_HEX(0x20C020), CXUI_HEX(0x2020C0),
                                         CXUI_HEX(0xE0E040) };

static void drawImageScript(CxUiDraw* d, void* ctx)
{
    ImageCtx* t = ctx;
    cxuiDrawFill(d, 0, 0, PW, PH, white);
    cxuiDrawImage(d, t->atlas, NULL, (CxUiRect){ 10, 10, 16, 16 });
    cxuiDrawImage(d, t->part, NULL, (CxUiRect){ 40, 10, 8, 8 });
    cxuiDrawImage(d, t->atlas, &(CxUiRect){ 12, 0, 4, 4 }, (CxUiRect){ 60, 10, 4, 4 });
    cxuiDrawImage(d, t->quads, NULL, (CxUiRect){ 80, 40, 32, 32 });
}

static void expectAtlas(Image* want, int32 dx, int32 dy, CxUiRect src)
{
    for (int32 y = 0; y < src.h; y++) {
        for (int32 x = 0; x < src.w; x++) {
            uint8 p[4];
            atlasPixel(src.x + x, src.y + y, p);
            imgFill(want, dx + x, dy + y, 1, 1, cxuiRGB(blend(p[0], 255, p[3]), blend(p[1], 255, p[3]), blend(p[2], 255, p[3])));
        }
    }
}

static int checkImages(CxUiTestOffscreen* o, strref engine)
{
    uint8 apx[16 * 16 * 4], qpx[8 * 8 * 3];
    for (int32 y = 0; y < 16; y++) {
        for (int32 x = 0; x < 16; x++)
            atlasPixel(x, y, &apx[(y * 16 + x) * 4]);
    }
    for (int32 y = 0; y < 8; y++) {
        for (int32 x = 0; x < 8; x++) {
            CxUiColor c = quadColors[(y / 4) * 2 + x / 4];
            uint8* p    = &qpx[(y * 8 + x) * 3];
            p[0] = c.r, p[1] = c.g, p[2] = c.b;
        }
    }
    ImageCtx t = { .atlas = cxuiImageCreateRGBA(16, 16, 16 * 4, apx), .quads = cxuiImageCreateRGB(8, 8, 8 * 3, qpx) };
    t.part     = cxuiImageCreateSub(t.atlas, (CxUiRect){ 4, 4, 8, 8 });

    uint8 got[PW * PH * 4];
    cxuiTestOffscreenDraw(o, (CxUiRect){ 0, 0, PW, PH }, drawImageScript, &t);
    cxuiTestOffscreenRead(o, got);
    objRelease(&t.atlas);
    objRelease(&t.part);
    objRelease(&t.quads);

    Image want;
    imgFill(&want, 0, 0, PW, PH, white);
    expectAtlas(&want, 10, 10, (CxUiRect){ 0, 0, 16, 16 });
    expectAtlas(&want, 40, 10, (CxUiRect){ 4, 4, 8, 8 });
    expectAtlas(&want, 60, 10, (CxUiRect){ 12, 0, 4, 4 });
    // Scaled 2x: only the inside of each quarter, away from where smoothing blends them
    for (int32 q = 0; q < 4; q++)
        imgFill(&want, 80 + (q % 2) * 16, 40 + (q / 2) * 16, 16, 16, quadColors[q]);
    for (int32 y = 40; y < 72; y++) {
        for (int32 x = 80; x < 112; x++) {
            int32 fx = (x - 80) % 16, fy = (y - 40) % 16;
            if (fx < 2 || fx > 13 || fy < 2 || fy > 13)
                memcpy(&want.px[(y * PW + x) * 4], &got[(y * PW + x) * 4], 3);
        }
    }
    int32 i = imgDiff(&want, got, 1);
    return i >= 0 ? failAt(engine, _S"images", &want, got, i) : 0;
}

static int checkMono(CxUiTestOffscreen* o, strref engine)
{
    CxUiFont* f = cxuiTestOffscreenFont(o, &(CxUiFontDesc){ .faces = _S"No Such Face;monospace", .size = -16 });
    int32 narrow = cxuiFontAdvance(f, _S"iiiiiiii"), wide = cxuiFontAdvance(f, _S"WWWWWWWW");
    objRelease(&f);
    if (narrow <= 0 || narrow != wide)
        TEST_FAIL(1, _SL("${string}: a monospace font gave ${int} for i and ${int} for W"), stvar(strref, engine),
                  stvar(int32, narrow), stvar(int32, wide));
    return 0;
}

// The monospace family is fixed-width, even where none of the faces cxui knows it by is installed.
static int test_draw_pmono(void)
{
    return forEachEngine(checkMono);
}

// Images, parts of them and source rectangles land exactly at scale 1, blended within 1; scaled
// images keep their colors.
static int test_draw_pimage(void)
{
    return forEachEngine(checkImages);
}

static int test_draw_grp_pixels(void)
{
    TEST_CHAIN(test_draw_pshapes, test_draw_pclip, test_draw_palpha, test_draw_ppartial, test_draw_pline, test_draw_ptext,
               test_draw_pcells, test_draw_pmono, test_draw_pimage);
}

testfunc cxuidrawtest_funcs[] = {
    { "pshapes", test_draw_pshapes },
    { "pclip", test_draw_pclip },
    { "palpha", test_draw_palpha },
    { "ppartial", test_draw_ppartial },
    { "pline", test_draw_pline },
    { "ptext", test_draw_ptext },
    { "pcells", test_draw_pcells },
    { "pmono", test_draw_pmono },
    { "pimage", test_draw_pimage },
    { "grp_pixels", test_draw_grp_pixels },
    { 0, 0 },
};
