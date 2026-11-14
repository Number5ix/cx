// Times full repaints of a grid filling a 1920x1080 window, on the platform's own backend, over a
// list store of a million rows and a tree store of a million nodes, all open.
//
// Workloads:
//   empty        no rows: clearing the window and the headers, what any full repaint costs
//   list top     the first rows, four columns, stripes
//   list middle  the same half a million rows down, with 200 rows selected
//   list narrow  the name column too narrow for most names, so each is shortened with an ellipsis
//   tree middle  a thousand folders of a thousand files each, half way down, with guide lines
//
// The budget, under 4 ms, is for the paint pass: the drawing calls and the engine finishing. The
// rest of a paint, mostly copying the buffer to the window, is the same for any canvas the size.
//
// Each also reports the allocations cx made per paint, where mimalloc keeps statistics (Debug and
// Dev builds); the platform's own drawing allocates from its own heap and isn't counted.
//
// GSK builds render nodes when painting; the GPU draws them later, so its times are for building
// them.

#include <cx/console.h>
#include <cx/format.h>
#include <cxui/entry.h>
#include <cx/time.h>
#include <cxui.h>
#include <cxui/test/cxuitest.h>
#include <math.h>

#if defined(GRIDBENCH_MIMALLOC)
#include <mimalloc-stats.h>
#endif

#define W      1920
#define H      1080
#define ROWS   1000000
#define WARMUP 3
#define FRAMES 20

static const strref kinds[] = { _S"Text", _S"Image", _S"Model", _S"Sound", _S"Script" };

// Blocks cx has allocated on this thread so far, or -1 where nothing counts them
static int64 allocations(void)
{
#if defined(GRIDBENCH_MIMALLOC)
    mi_stats_t_decl(st);
    if (!mi_theap_stats_get(mi_theap_get_default(), &st))
        return -1;
    return st.malloc_normal_count.total + st.malloc_huge_count.total;
#else
    return -1;
#endif
}

typedef struct Bench {
    CxUiWindow* win;
    CxUiGrid* grid;
    CxUiListStore* list;
    CxUiTreeStore* tree;
    uint32 engine;
} Bench;

static CxUiListStore* makeList(void)
{
    CxUiListStore* s = cxuiListStoreCreate(4);
    string num = 0, name = 0, size = 0;
    for (int32 i = 0; i < ROWS; i++) {
        strFormat(&num, _S"${int}", stvar(int32, i));
        strFormat(&name, _S"file_${int}_with_a_longer_name.${string}", stvar(int32, i), stvar(strref, kinds[i % 5]));
        strFormat(&size, _S"${int} KB", stvar(int64, ((int64)i * 7919) % 100000));
        cxuiListStoreAppend(s, num, name, kinds[i % 5], size);
    }
    strDestroy(&num);
    strDestroy(&name);
    strDestroy(&size);
    return s;
}

static CxUiTreeStore* makeTree(void)
{
    CxUiTreeStore* t = cxuiTreeStoreCreate(2);
    string name = 0, size = 0;
    for (int32 f = 0; f < 1000; f++) {
        strFormat(&name, _S"folder ${int}", stvar(int32, f));
        CxUiNodeId folder = cxuiTreeStoreAppend(t, 0, name);
        for (int32 i = 0; i < 999; i++) {
            strFormat(&name, _S"item_${int}.dat", stvar(int32, i));
            strFormat(&size, _S"${int} KB", stvar(int32, (i * 31) % 997));
            cxuiTreeStoreAppend(t, folder, name, size);
        }
    }
    strDestroy(&name);
    strDestroy(&size);
    return t;
}

// A window of its own with the grid filling it, on the engine asked for
static void showGrid(Bench* b, const CxUiGridDesc* desc, ObjInst* source)
{
    b->win  = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui grid bench", .size = { W, H }, .sizing = CXUI_SIZE_Fixed });
    b->grid = cxuiGridCreate(desc);
    CxUiCanvas(b->grid->_body)->_drawBackend = b->engine;
    cxuiGridSetSource(b->grid, source);
    cxuiContainerAppend(b->win, b->grid);
    cxuiWindowShow(b->win, CXUI_SHOW_Normal);
}

static void timePaints(Bench* b, strref what, float64 budget)
{
    CxUiCanvas* body = CxUiCanvas(b->grid->_body);
    float64 avg = 0, best = 1e9;
    int64 allocs = 0;
    // Microseconds in each part: the engine starting, the grid's drawing calls, the engine
    // finishing, and the rest of paint-now, mostly copying the buffer to the window
    float64 parts[4] = { 0 };
    for (int32 i = 0; i < WARMUP + FRAMES; i++) {
        int64 before = allocations();
        int64 start  = clockTimer();
        cxuiCanvasInvalidateAll(body);
        cxuiCanvasPaintNow(body);
        float64 ms  = (clockTimer() - start) / 1000.0;
        int64 after = allocations();
        CxUiTestPaintTimes pt;
        cxuiTestPaintTimes(&pt);
        if (i >= WARMUP) {
            parts[0] += pt.begin / 1000.0 / FRAMES;
            parts[1] += pt.draw / 1000.0 / FRAMES;
            parts[2] += pt.end / 1000.0 / FRAMES;
            parts[3] += (ms - (pt.begin + pt.draw + pt.end) / 1000.0) / FRAMES;
            avg += ms / FRAMES;
            best   = min(best, ms);
            allocs = before < 0 ? -1 : allocs + after - before;
        }
    }
    string allocText = 0;
    if (allocs >= 0)
        strFormat(&allocText, _S", ${float} cx allocations a paint", stvar(float64, (float64)allocs / FRAMES));
    CxUiRect r   = CxUiWidget(body)->_bounds;
    float64 pass = parts[0] + parts[1] + parts[2];
    conFmt(conOut(), _SL("${string}: ${string} (${int}x${int}): ${float} ms average, ${float} ms best, paint pass ${float} ms${string}${string}\n"),
           stvar(strref, cxuiTestCanvasEngine(body)), stvar(strref, what), stvar(int32, r.w), stvar(int32, r.h),
           stvar(float64, floor(avg * 100 + 0.5) / 100), stvar(float64, floor(best * 100 + 0.5) / 100),
           stvar(float64, floor(pass * 100 + 0.5) / 100), stvar(strref, allocText),
           stvar(strref, budget > 0 ? (pass <= budget ? _S"  (within budget)" : _S"  (OVER BUDGET)") : _S""));
    conFmt(conOut(), _SL("    drawing ${float} ms, the engine finishing ${float} ms, starting ${float} ms, the rest ${float} ms\n"),
           stvar(float64, floor(parts[1] * 100 + 0.5) / 100), stvar(float64, floor(parts[2] * 100 + 0.5) / 100),
           stvar(float64, floor(parts[0] * 100 + 0.5) / 100), stvar(float64, floor(parts[3] * 100 + 0.5) / 100));
    conFlush(conOut());
    strDestroy(&allocText);
}

enum {
    STEP_Stores,
    STEP_ShowList,
    STEP_Shown,
    STEP_Empty,
    STEP_ListTop,
    STEP_ListMiddle,
    STEP_ListNarrow,
    STEP_ShowTree,
    STEP_TreeMiddle,
    STEP_Close,
    STEP_Free,
};

// One step on the UI thread; between steps it lays out and paints as it would
static bool step(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    Bench* b = stvlAtPtr(cvars, 0);
    switch (stvlAt(cvars, 1, int32)) {
    case STEP_Stores:
        b->list = makeList();
        b->tree = makeTree();
        break;
    case STEP_ShowList: {
        CxUiGridColumn cols[] = { { .title = _S"#", .width = cxuiChars(9), .align = CXUI_ALIGN_End },
                                  { .title = _S"Name", .flags = CXUI_COL_Stretch | CXUI_COL_CheckBox },
                                  { .title = _S"Kind", .width = cxuiLen(90), .align = CXUI_ALIGN_Center },
                                  { .title = _S"Size", .width = cxuiLen(110), .align = CXUI_ALIGN_End } };
        showGrid(b, &(CxUiGridDesc){ .columns = cols, .ncolumns = 4, .flags = CXUI_GRID_Stripes, .select = CXUI_SELECT_Multi },
                 NULL);
        break;
    }
    case STEP_Shown: {
        // The size is asked for in DIPs; the grid is to be W by H physical pixels.
        CxUiRect r = CxUiWidget(b->win)->_bounds;
        if (!CxUiWidget(b->grid->_body)->peer || r.w <= 0)
            return false;
        if (abs(r.w - W) <= 2 && abs(r.h - H) <= 2)
            return true;
        float32 sc = cxuiWidgetScale(b->win);
        cxuiWindowSetSize(b->win, (int32)ceil(W / sc), (int32)ceil(H / sc));
        return false;
    }
    case STEP_Empty:
        timePaints(b, _S"empty", 0);
        cxuiGridSetSource(b->grid, b->list);
        break;
    case STEP_ListTop:
        timePaints(b, _S"list top", 4.0);
        cxuiGridSetTop(b->grid, ROWS / 2);
        cxuiGridSetSelected(b->grid, ROWS / 2 + 10, 200, true);
        break;
    case STEP_ListMiddle:
        timePaints(b, _S"list middle", 4.0);
        // The kind column takes the room the name had.
        cxuiGridSetColumns(b->grid, { .title = _S"#", .width = cxuiChars(9), .align = CXUI_ALIGN_End },
                           { .title = _S"Name", .width = cxuiLen(120), .flags = CXUI_COL_CheckBox },
                           { .title = _S"Kind", .flags = CXUI_COL_Stretch, .align = CXUI_ALIGN_Center },
                           { .title = _S"Size", .width = cxuiLen(110), .align = CXUI_ALIGN_End });
        break;
    case STEP_ListNarrow:
        timePaints(b, _S"list narrow", 4.0);
        break;
    case STEP_ShowTree: {
        CxUiGridColumn cols[] = { { .title = _S"Name", .kind = CXUI_COL_Tree, .flags = CXUI_COL_Stretch | CXUI_COL_CheckBox },
                                  { .title = _S"Size", .width = cxuiLen(110), .align = CXUI_ALIGN_End } };
        showGrid(b, &(CxUiGridDesc){ .columns = cols, .ncolumns = 2, .flags = CXUI_GRID_TreeLines, .select = CXUI_SELECT_Multi },
                 (ObjInst*)b->tree);
        cxuiGridExpandAll(b->grid, 0);
        cxuiGridSetTop(b->grid, cxuiGridRowCount(b->grid) / 2);
        break;
    }
    case STEP_TreeMiddle:
        timePaints(b, _S"tree middle", 4.0);
        break;
    case STEP_Close:
        cxuiDestroy(b->win);
        objRelease(&b->grid);
        objRelease(&b->win);
        break;
    case STEP_Free:
        objRelease(&b->list);
        objRelease(&b->tree);
        break;
    }
    return true;
}

static bool run(Bench* b, int32 which)
{
    bool r = cxuiCallSync(closureCreate(step, stvar(ptr, b), stvar(int32, which)));
    // Time for the layout and painting a change asks for
    osSleep(timeMS(300));
    return r;
}

// Shows the grid and waits for its canvas to exist
static bool shown(Bench* b, int32 which)
{
    run(b, which);
    for (int32 i = 0; i < 100; i++) {
        if (run(b, STEP_Shown))
            return true;
    }
    return false;
}

int entryPoint(void)
{
    if (!cxuiStart(&(CxUiStartDesc){ .name = _S"Grid bench UI" })) {
        conPuts(conOut(), _SL("cxui didn't start; is there a display?\n"));
        return 1;
    }
    conFmt(conOut(), _SL("cxui grid bench: a ${int}x${int} window, average of ${int} paints\n"), stvar(int32, W),
           stvar(int32, H), stvar(int32, FRAMES));
    Bench b = { 0 };
    run(&b, STEP_Stores);
#if defined(_PLATFORM_WIN)
    static const uint32 engines[] = { CXUI_Draw_GDI, CXUI_Draw_D2D };
#else
    static const uint32 engines[] = { CXUI_Draw_Auto };
#endif
    for (size_t i = 0; i < sizeof(engines) / sizeof(engines[0]); i++) {
        b.engine = engines[i];
        if (!shown(&b, STEP_ShowList)) {
            conPuts(conOut(), _SL("the grid wasn't shown\n"));
            run(&b, STEP_Close);
            continue;
        }
        run(&b, STEP_Empty);
        run(&b, STEP_ListTop);
        run(&b, STEP_ListMiddle);
        run(&b, STEP_ListNarrow);
        run(&b, STEP_Close);
        if (shown(&b, STEP_ShowTree))
            run(&b, STEP_TreeMiddle);
        run(&b, STEP_Close);
    }
    run(&b, STEP_Free);
    cxuiShutdown();
    return 0;
}

CXUI_DEFINE_ENTRY_POINT;
