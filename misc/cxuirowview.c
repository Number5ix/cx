// A log viewer's row view on a cxui canvas, timed against plain GDI drawing.
//
// The row painting is a full repaint of the visible rows, each with its time and level in cells
// and its message clipped to the column after them, selected rows and the find row filled behind,
// and the scroll bar's thumb. It is written once, against a small painter, and drawn two ways:
//
//   plain    GDI drawing without cxui: a compatible bitmap, Consolas 10 pt, ExtTextOutW with an
//            advance per character. Windows only. This is the baseline.
//   cxui     the same calls through a cxui canvas: cxuiDrawFill, cxuiDrawTextCells and clips.
//
//   cxuirowview bench [find]   full repaints offscreen, a new row streaming in before each one,
//                              for each engine there is; "find" highlights a word in every row
//   cxuirowview [gdi|d2d] [rate N]
//                              a window that streams N rows a second (default 200) and prints its
//                              paint times every 2 seconds

#include <cx/console.h>
#include <cxui/entry.h>
#include <cx/thread.h>
#include <cx/time.h>
#include <cxui.h>
#include <cxui/test/cxuitest.h>
#include <stdio.h>
#include <string.h>

// ---- the rows ----

#define TIME_CELLS  12
#define LEVEL_CELLS 7
#define MSG_CELL    (TIME_CELLS + 1 + LEVEL_CELLS + 1)
#define ROW_CAP     8192

static const char* const levelName[9] = { "Fatal", "Error", "Warn", "Notice", "Info", "Verbose", "Diag", "Debug",
                                          "Trace" };

// A dark theme
static const uint32 thBg = 0x1e1e1e, thDim = 0x7f8388, thSel = 0x264f78, thFind = 0x6b4a12, thFindRow = 0x3a3d41,
                    thThumb = 0x4b4b4f;
static const uint32 thLevelTag[9]  = { 0xff5f56, 0xf14c4c, 0xe5c07b, 0xffffff, 0x8fbf8f,
                                       0x8a8f96, 0x7c8ea3, 0x56a8b5, 0x4a8791 };
static const uint32 thLevelText[9] = { 0xff8a84, 0xf47a7a, 0xe5c07b, 0xffffff, 0xd4d4d4,
                                       0x9da2a8, 0x8c9bab, 0x6fb3bf, 0x5d98a2 };

typedef struct Row {
    int64 ts;   // milliseconds
    uint8 level;
    uint16 len;
    char text[200];   // NUL-terminated, with room to cut a line short in place
} Row;

typedef struct Store {
    Row* rows;   // a ring of ROW_CAP
    uint64 count;
    uint32 seed;
} Store;

static uint32 rnd(Store* s)
{
    s->seed = s->seed * 1664525 + 1013904223;
    return s->seed >> 8;
}

static void storeAppend(Store* s)
{
    static const char* const words[] = { "the", "server", "connection", "sent", "frame", "reply", "queued", "error",
                                         "timeout", "player", "zone", "entity", "update", "flushed", "bytes", "to",
                                         "from", "packet", "retry", "map", "loaded", "in", "ms", "cache", "hit" };
    Row* r   = &s->rows[s->count % ROW_CAP];
    r->ts    = 45296789 + (int64)s->count * 37;
    r->level = (uint8)(rnd(s) % 100 < 70 ? 4 : rnd(s) % 9);
    int32 n  = 3 + rnd(s) % 25;
    int32 at = 0;
    for (int32 i = 0; i < n && at < 180; i++) {
        const char* w = words[rnd(s) % (sizeof(words) / sizeof(words[0]))];
        size_t wl     = strlen(w);
        if (i)
            r->text[at++] = ' ';
        memcpy(r->text + at, w, wl);
        at += (int32)wl;
        if (rnd(s) % 5 == 0)
            at += snprintf(r->text + at, 8, " %u", rnd(s) % 100000);
    }
    r->text[at] = 0;
    r->len      = (uint16)at;
    s->count++;
}

static void timeText(char* out, int64 ms)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u.%03u", (unsigned)(ms / 3600000 % 24), (unsigned)(ms / 60000 % 60),
             (unsigned)(ms / 1000 % 60), (unsigned)(ms % 1000));
    memcpy(out, buf, TIME_CELLS);
    out[TIME_CELLS] = 0;
}

// ---- painting ----

typedef struct Painter {
    void (*fill)(void* ctx, int x, int y, int w, int h, uint32 rgb);
    // A run of cells; s is NUL-terminated at len
    void (*cells)(void* ctx, int x, int y, const char* s, int len, uint32 rgb);
    void (*clip)(void* ctx, int x, int y, int w, int h);
    void (*unclip)(void* ctx);
    void* ctx;
    int cellW, cellH;
} Painter;

static void paintRows(Painter* p, Store* s, int w, int h, const char* needle)
{
    int padX = p->cellW / 2, barW = max(p->cellW * 2, 14);
    int msgX = padX + MSG_CELL * p->cellW, textW = w - msgX - barW - padX;
    int cols = max(textW / p->cellW, 16), lines = max(h / p->cellH, 1);
    uint64 first = s->count > (uint64)lines ? s->count - lines : 0;
    size_t nlen  = needle ? strlen(needle) : 0;
    char timebuf[TIME_CELLS + 1];

    p->fill(p->ctx, 0, 0, w, h, thBg);
    for (uint64 id = first, y = 0; id < s->count; id++, y += p->cellH) {
        Row* r = &s->rows[id % ROW_CAP];
        if (id % 23 == 0)
            p->fill(p->ctx, 0, (int)y, w - barW, p->cellH, thSel);
        else if (id % 31 == 0)
            p->fill(p->ctx, 0, (int)y, w - barW, p->cellH, thFindRow);

        timeText(timebuf, r->ts);
        p->cells(p->ctx, padX, (int)y, timebuf, TIME_CELLS, thDim);
        p->cells(p->ctx, padX + (TIME_CELLS + 1) * p->cellW, (int)y, levelName[r->level], (int)strlen(levelName[r->level]),
                 thLevelTag[r->level]);

        p->clip(p->ctx, msgX, (int)y, textW, p->cellH);
        for (const char* m = nlen ? strstr(r->text, needle) : NULL; m; m = strstr(m + 1, needle))
            p->fill(p->ctx, msgX + (int)(m - r->text) * p->cellW, (int)y, (int)nlen * p->cellW, p->cellH, thFind);
        int end   = min((int)r->len, cols + 1);
        char kept = r->text[end];
        r->text[end] = 0;
        p->cells(p->ctx, msgX, (int)y, r->text, end, thLevelText[r->level]);
        r->text[end] = kept;
        p->unclip(p->ctx);
    }
    // the thumb, following the end
    int thumbH = max(h * lines / (int)max(s->count, 1u), 3 * p->cellH / 2);
    p->fill(p->ctx, w - barW + 3, h - thumbH + 2, barW - 6, thumbH - 4, thThumb);
}

// ---- cxui ----

typedef struct CxPaint {
    CxUiDraw* d;
    CxUiFont* font;
    int cellW;
} CxPaint;

static void cxFill(void* ctx, int x, int y, int w, int h, uint32 rgb)
{
    cxuiDrawFill(((CxPaint*)ctx)->d, x, y, w, h, cxuiHex(rgb));
}

static void cxCells(void* ctx, int x, int y, const char* s, int len, uint32 rgb)
{
    CxPaint* c = ctx;
    if (len > 0)
        cxuiDrawTextCells(c->d, c->font, x, y, (strref)s, c->cellW, cxuiHex(rgb));
}

static void cxClip(void* ctx, int x, int y, int w, int h)
{
    cxuiDrawClipPush(((CxPaint*)ctx)->d, (CxUiRect){ x, y, w, h });
}

static void cxUnclip(void* ctx)
{
    cxuiDrawClipPop(((CxPaint*)ctx)->d);
}

static Painter cxPainter(CxPaint* c, CxUiFont* font)
{
    CxUiFontMetrics m;
    cxuiFontMetrics(font, &m);
    c->font  = font;
    c->cellW = m.cellW;
    return (Painter){ cxFill, cxCells, cxClip, cxUnclip, c, m.cellW, m.cellH };
}

// ---- the benchmark ----

#define BENCH_W      1280
#define BENCH_H      800
#define BENCH_FRAMES 200

typedef struct BenchFrame {
    Store* s;
    CxUiFont* font;
    const char* needle;
} BenchFrame;

static void benchDraw(CxUiDraw* d, void* ctx)
{
    BenchFrame* f = ctx;
    CxPaint c     = { .d = d };
    Painter p     = cxPainter(&c, f->font);
    paintRows(&p, f->s, BENCH_W, BENCH_H, f->needle);
}

static void report(strref what, float64 avg, float64 worst, int32 rows)
{
    // Engines measure the same font a pixel or two differently, so they fit different numbers of
    // rows; per row is the fair comparison.
    conFmt(conOut(), _SL("${string}: ${float} ms average, ${float} ms worst, ${int} rows, ${float} us a row\n"),
           stvar(strref, what), stvar(float64, (int64)(avg * 1000 + 0.5) / 1000.0),
           stvar(float64, (int64)(worst * 1000 + 0.5) / 1000.0), stvar(int32, rows),
           stvar(float64, (int64)(avg * 1000 / max(rows, 1) * 10 + 0.5) / 10.0));
    conFlush(conOut());
}

static Store* storeNew(void)
{
    Store* s = xaAllocStruct(Store, XA_Zero);
    s->rows  = xaAlloc(sizeof(Row) * ROW_CAP, XA_Zero);
    s->seed  = 12345;
    for (int32 i = 0; i < 2000; i++)
        storeAppend(s);
    return s;
}

static void storeFree(Store** s)
{
    xaFree((*s)->rows);
    xaDestroy(s);
}

static void benchCxui(uint32 engine, string* seen, const char* needle)
{
    CxUiTestOffscreen* o = cxuiTestOffscreenCreate(engine, BENCH_W, BENCH_H);
    if (!o)
        return;
    strref name = cxuiTestOffscreenEngine(o);
    if (strFind(*seen, 0, name) >= 0) {
        cxuiTestOffscreenDestroy(&o);
        return;
    }
    strAppend(seen, name);
    strAppend(seen, _S" ");

    BenchFrame f = { .s = storeNew(), .needle = needle };
    f.font       = cxuiTestOffscreenFont(o, &(CxUiFontDesc){ .faces = _S"Consolas;monospace", .size = 10 });
    CxUiFontMetrics m;
    cxuiFontMetrics(f.font, &m);
    float64 total = 0, worst = 0;
    for (int32 i = 0; i < BENCH_FRAMES + 5; i++) {
        storeAppend(f.s);
        int64 start = clockTimer();
        cxuiTestOffscreenDraw(o, (CxUiRect){ 0, 0, BENCH_W, BENCH_H }, benchDraw, &f);
        float64 ms = (clockTimer() - start) / 1000.0;
        if (i >= 5) {
            total += ms;
            worst = max(worst, ms);
        }
    }
    string what = 0;
    strConcat(&what, _S"cxui ", name);
    report(what, total / BENCH_FRAMES, worst, BENCH_H / m.cellH);
    strDestroy(&what);
    objRelease(&f.font);
    storeFree(&f.s);
    cxuiTestOffscreenDestroy(&o);
}

#if defined(_PLATFORM_WIN)
#include <cx/platform/win.h>

// Plain GDI drawing, the baseline
#define DD_TEXT_MAX 4096

typedef struct DdSurface {
    HDC dc;
    HBITMAP bitmap, oldBitmap;
    HFONT font, oldFont;
    int w, h, cellW, cellH;
    bool clipped;
    int clipX1, clipY1, clipX2, clipY2;
    int advance[DD_TEXT_MAX];
} DdSurface;

static COLORREF ddRef(uint32 c)
{
    return RGB((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
}

static void ddFill(void* ctx, int x, int y, int w, int h, uint32 color)
{
    DdSurface* sf = ctx;
    RECT rect     = { x, y, x + w, y + h };
    if (sf->clipped) {
        rect.left   = max(rect.left, sf->clipX1);
        rect.top    = max(rect.top, sf->clipY1);
        rect.right  = min(rect.right, sf->clipX2);
        rect.bottom = min(rect.bottom, sf->clipY2);
    }
    if (rect.left >= rect.right || rect.top >= rect.bottom)
        return;
    SetBkColor(sf->dc, ddRef(color));
    ExtTextOutW(sf->dc, 0, 0, ETO_OPAQUE, &rect, NULL, 0, NULL);
}

static void ddText(void* ctx, int x, int y, const char* text, int len, uint32 color)
{
    DdSurface* sf = ctx;
    WCHAR wide[DD_TEXT_MAX];
    RECT clip = { sf->clipX1, sf->clipY1, sf->clipX2, sf->clipY2 };
    if (len <= 0)
        return;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, len, wide, DD_TEXT_MAX);
    if (wlen <= 0)
        return;
    SetTextColor(sf->dc, ddRef(color));
    ExtTextOutW(sf->dc, x, y, sf->clipped ? ETO_CLIPPED : 0, sf->clipped ? &clip : NULL, wide, (UINT)wlen,
                sf->advance);
}

static void ddClip(void* ctx, int x, int y, int w, int h)
{
    DdSurface* sf = ctx;
    sf->clipped   = true;
    sf->clipX1    = x;
    sf->clipY1    = y;
    sf->clipX2    = x + w;
    sf->clipY2    = y + h;
}

static void ddUnclip(void* ctx)
{
    ((DdSurface*)ctx)->clipped = false;
}

// dib draws into a DIB section, as cxui does, rather than a compatible bitmap; quality is the
// font's
static void benchGdi(const char* needle, bool dib, BYTE quality, strref name)
{
    DdSurface* sf = xaAllocStruct(DdSurface, XA_Zero);
    HDC screen    = GetDC(NULL);
    sf->font      = CreateFontW(-MulDiv(10, GetDeviceCaps(screen, LOGPIXELSY), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                                FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, quality,
                                FIXED_PITCH | FF_MODERN, L"Consolas");
    sf->w         = BENCH_W;
    sf->h         = BENCH_H;
    sf->dc        = CreateCompatibleDC(screen);
    if (dib) {
        BITMAPINFO bmi              = { 0 };
        bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
        bmi.bmiHeader.biWidth       = BENCH_W;
        bmi.bmiHeader.biHeight      = -BENCH_H;
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits                  = NULL;
        sf->bitmap                  = CreateDIBSection(sf->dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    } else {
        sf->bitmap = CreateCompatibleBitmap(screen, BENCH_W, BENCH_H);
    }
    sf->oldBitmap = SelectObject(sf->dc, sf->bitmap);
    sf->oldFont   = SelectObject(sf->dc, sf->font);
    SetBkMode(sf->dc, TRANSPARENT);
    TEXTMETRICW tm;
    GetTextMetricsW(sf->dc, &tm);
    sf->cellW = max(tm.tmAveCharWidth, 1);
    sf->cellH = max(tm.tmHeight + tm.tmExternalLeading, 1);
    for (int32 i = 0; i < DD_TEXT_MAX; i++)
        sf->advance[i] = sf->cellW;
    ReleaseDC(NULL, screen);

    Store* s      = storeNew();
    Painter p     = { ddFill, ddText, ddClip, ddUnclip, sf, sf->cellW, sf->cellH };
    float64 total = 0, worst = 0;
    for (int32 i = 0; i < BENCH_FRAMES + 5; i++) {
        storeAppend(s);
        int64 start = clockTimer();
        paintRows(&p, s, BENCH_W, BENCH_H, needle);
        GdiFlush();
        float64 ms = (clockTimer() - start) / 1000.0;
        if (i >= 5) {
            total += ms;
            worst = max(worst, ms);
        }
    }
    report(name, total / BENCH_FRAMES, worst, BENCH_H / sf->cellH);

    storeFree(&s);
    SelectObject(sf->dc, sf->oldFont);
    SelectObject(sf->dc, sf->oldBitmap);
    DeleteObject(sf->bitmap);
    DeleteObject(sf->font);
    DeleteDC(sf->dc);
    xaFree(sf);
}
#endif

static int bench(bool find)
{
    const char* needle = find ? "the" : NULL;
    conFmt(conOut(), _SL("row view: ${int}x${int} px, ${int} full repaints, a row streaming in before each${string}\n"),
           stvar(int32, BENCH_W), stvar(int32, BENCH_H), stvar(int32, BENCH_FRAMES),
           stvar(strref, find ? _S", \"the\" highlighted" : _S""));
#if defined(_PLATFORM_WIN)
    benchGdi(needle, false, DEFAULT_QUALITY, _S"plain gdi (baseline)");
    benchGdi(needle, true, DEFAULT_QUALITY, _S"plain gdi on a DIB section");
    benchGdi(needle, false, CLEARTYPE_QUALITY, _S"plain gdi, ClearType quality");
#endif
    string seen = 0;
    benchCxui(CXUI_Draw_GDI, &seen, needle);
    benchCxui(CXUI_Draw_D2D, &seen, needle);
    benchCxui(CXUI_Draw_Auto, &seen, needle);
    strDestroy(&seen);
    return 0;
}

// ---- the live view ----

typedef struct Live {
    Event done;
    Store* s;
    CxUiCanvas* cv;
    CxUiFont* font;
    CxUiTimer* timer;
    int32 rate;   // rows a second
    int64 lastTick;
    float64 carry;
    // paint times since the last report
    int32 paints;
    float64 total, worst;
    int64 since;
} Live;

static CxUiResult livePaint(stvlist* cvars, CxUiPaintEvent* ev)
{
    Live* l     = stvlAtPtr(cvars, 0);
    int64 start = clockTimer();
    CxUiSize sz = cxuiDrawSize(ev->draw);
    CxPaint c   = { .d = ev->draw };
    Painter p   = cxPainter(&c, l->font);
    paintRows(&p, l->s, sz.w, sz.h, NULL);
    float64 ms = (clockTimer() - start) / 1000.0;
    l->paints++;
    l->total += ms;
    l->worst = max(l->worst, ms);
    return CXUI_Handled;
}

static CxUiResult liveTick(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    Live* l   = stvlAtPtr(cvars, 0);
    int64 now = clockTimer();
    l->carry += (now - l->lastTick) / 1e6 * l->rate;
    l->lastTick = now;
    bool added  = false;
    for (; l->carry >= 1; l->carry -= 1, added = true)
        storeAppend(l->s);
    // Everything again when anything changes, as a log viewer does
    if (added)
        cxuiCanvasInvalidateAll(l->cv);
    if (now - l->since >= timeS(2)) {
        conFmt(conOut(), _SL("${int} paints a second, ${float} ms average, ${float} ms worst\n"),
               stvar(int32, (int32)(l->paints / ((now - l->since) / 1e6) + 0.5)),
               stvar(float64, l->paints ? (int64)(l->total / l->paints * 1000 + 0.5) / 1000.0 : 0),
               stvar(float64, (int64)(l->worst * 1000 + 0.5) / 1000.0));
        conFlush(conOut());
        l->paints = 0;
        l->total = l->worst = 0;
        l->since            = now;
    }
    return CXUI_Default;
}

static CxUiResult liveDpi(stvlist* cvars, CxUiDpiChangedEvent* ev)
{
    unused_noeval(ev);
    Live* l          = stvlAtPtr(cvars, 0);
    CxUiFont* scaled = cxuiFontRescale(l->font, ev->scale);
    objRelease(&l->font);
    l->font = scaled;
    return CXUI_Default;
}

static bool liveClosed(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    Live* l = stvlAtPtr(cvars, 0);
    cxuiTimerStop(l->timer);
    eventSignal(&l->done);
    return true;
}

static bool liveBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    Live* l       = stvlAtPtr(cvars, 0);
    uint32 engine = stvlAt(cvars, 1, uint32);
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .title       = _S"cxui row view",
                                                        .size        = { 1000, 700 },
                                                        .theme       = CXUI_Theme_Dark,
                                                        .closeAction = CXUI_OnClose_Destroy });
    l->cv         = cxuiCanvasCreate(&(CxUiCanvasDesc){ .draw = engine });
    l->font       = cxuiFontCreateFor(l->cv, &(CxUiFontDesc){ .faces = _S"Consolas;monospace", .size = 10 });
    cxuiOnPaint(l->cv, livePaint, stvar(ptr, l));
    cxuiOnDpiChanged(w, liveDpi, stvar(ptr, l));
    cchainAttach(&CxUiWidget(w)->destroyNotify, liveClosed, stvar(ptr, l));
    cxuiContainerAppend(w, l->cv);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    l->lastTick = l->since = clockTimer();
    l->timer               = cxuiTimerCreate(timeMS(10), 0, liveTick, stvar(ptr, l));
    cxuiTimerStart(l->timer);
    conFmt(conOut(), _SL("engine ${string}, ${int} rows a second; close the window to stop\n"),
           stvar(strref, cxuiTestCanvasEngine(l->cv)), stvar(int32, l->rate));
    conFlush(conOut());
    objRelease(&w);
    return true;
}

static bool liveRelease(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    Live* l = stvlAtPtr(cvars, 0);
    objRelease(&l->timer);
    objRelease(&l->font);
    objRelease(&l->cv);
    return true;
}

static int live(uint32 engine, int32 rate)
{
    if (!cxuiStart(&(CxUiStartDesc){ .name = _S"Row view UI" })) {
        conPuts(conErr(), _SL("cxui couldn't start; is there a display?\n"));
        return 1;
    }
    Live l = { .s = storeNew(), .rate = rate };
    eventInit(&l.done);
    cxuiCallSync(closureCreate(liveBuild, stvar(ptr, &l), stvar(uint32, engine)));
    eventWait(&l.done);
    cxuiCallSync(closureCreate(liveRelease, stvar(ptr, &l)));
    cxuiShutdown();
    storeFree(&l.s);
    eventDestroy(&l.done);
    return 0;
}

int entryPoint(void)
{
    bool benchMode = false, find = false;
    uint32 engine  = CXUI_Draw_Auto;
    int32 rate     = 200;
    for (int32 i = 0; i < saSize(cmdArgs); i++) {
        strref a = cmdArgs.a[i];
        if (strEqi(a, _S"bench"))
            benchMode = true;
        else if (strEqi(a, _S"find"))
            find = true;
        else if (strEqi(a, _S"gdi"))
            engine = CXUI_Draw_GDI;
        else if (strEqi(a, _S"d2d"))
            engine = CXUI_Draw_D2D;
        else if (strEqi(a, _S"rate") && i + 1 < saSize(cmdArgs))
            strToInt32(&rate, cmdArgs.a[++i], 10, true);
    }
    return benchMode ? bench(find) : live(engine, max(rate, 1));
}

// On macOS, cxui needs the main thread for AppKit.
CXUI_DEFINE_ENTRY_POINT;
