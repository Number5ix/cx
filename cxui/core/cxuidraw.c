#include "cxuidraw_private.h"

#include <cxui/test/cxuitest.h>

#include <math.h>

// ---- units ----

// Edges round half up, so adjacent DIP rectangles share an edge with no gap or overlap.
static int32 scaleEdge(float64 v, float32 scale)
{
    return (int32)floor(v * scale + 0.5);
}

static CxUiRect rectToPx(CxUiDraw* d, int32 x, int32 y, int32 w, int32 h)
{
    if (!d->dip)
        return (CxUiRect){ x, y, w, h };
    int32 x0 = scaleEdge(x, d->scale), y0 = scaleEdge(y, d->scale);
    return (CxUiRect){ x0, y0, scaleEdge((float64)x + w, d->scale) - x0, scaleEdge((float64)y + h, d->scale) - y0 };
}

static int32 widthToPx(CxUiDraw* d, int32 width)
{
    if (!d->dip || width <= 0)
        return width;
    return max(scaleEdge(width, d->scale), 1);
}

// The smallest rectangle in DIPs that covers r
static CxUiRect rectFromPx(CxUiDraw* d, CxUiRect r)
{
    if (!d->dip)
        return r;
    int32 x0 = (int32)floor(r.x / d->scale), y0 = (int32)floor(r.y / d->scale);
    int32 x1 = (int32)ceil((r.x + r.w) / d->scale), y1 = (int32)ceil((r.y + r.h) / d->scale);
    return (CxUiRect){ x0, y0, x1 - x0, y1 - y0 };
}

static CxUiRect curClip(CxUiDraw* d)
{
    return d->clip[d->nclip - 1];
}

static void fillPx(CxUiDraw* d, CxUiRect r, CxUiColor c)
{
    r = _cxuiRectIntersect(r, curClip(d));
    if (!_cxuiRectEmpty(r))
        d->ops->fill(d->target, r, c);
}

// ---- primitives ----

_Use_decl_annotations_
void cxuiDrawFill(CxUiDraw* d, int32 x, int32 y, int32 w, int32 h, CxUiColor c)
{
    if (c.a == 0)
        return;
    fillPx(d, rectToPx(d, x, y, w, h), c);
}

_Use_decl_annotations_
void cxuiDrawFrame(CxUiDraw* d, int32 x, int32 y, int32 w, int32 h, int32 width, CxUiColor c)
{
    CxUiRect r = rectToPx(d, x, y, w, h);
    width      = widthToPx(d, width);
    if (c.a == 0 || width <= 0 || _cxuiRectEmpty(r))
        return;
    if (width * 2 >= r.w || width * 2 >= r.h) {
        fillPx(d, r, c);
        return;
    }
    fillPx(d, (CxUiRect){ r.x, r.y, r.w, width }, c);
    fillPx(d, (CxUiRect){ r.x, r.y + r.h - width, r.w, width }, c);
    fillPx(d, (CxUiRect){ r.x, r.y + width, width, r.h - 2 * width }, c);
    fillPx(d, (CxUiRect){ r.x + r.w - width, r.y + width, width, r.h - 2 * width }, c);
}

_Use_decl_annotations_
void cxuiDrawLine(CxUiDraw* d, int32 x1, int32 y1, int32 x2, int32 y2, int32 width, CxUiColor c)
{
    width = widthToPx(d, width);
    if (c.a == 0 || width <= 0)
        return;
    if (d->dip) {
        x1 = scaleEdge(x1, d->scale);
        y1 = scaleEdge(y1, d->scale);
        x2 = scaleEdge(x2, d->scale);
        y2 = scaleEdge(y2, d->scale);
    }

    // The extra pixel of an even width goes below or to the right.
    int32 off = (width - 1) / 2;
    if (y1 == y2) {
        fillPx(d, (CxUiRect){ min(x1, x2), y1 - off, abs(x2 - x1) + 1, width }, c);
        return;
    }
    if (x1 == x2) {
        fillPx(d, (CxUiRect){ x1 - off, min(y1, y2), width, abs(y2 - y1) + 1 }, c);
        return;
    }

    CxUiRect bounds = { min(x1, x2) - width, min(y1, y2) - width, abs(x2 - x1) + 2 * width + 1,
                        abs(y2 - y1) + 2 * width + 1 };
    if (!_cxuiRectEmpty(_cxuiRectIntersect(bounds, curClip(d))))
        d->ops->line(d->target, (CxUiPoint){ x1, y1 }, (CxUiPoint){ x2, y2 }, width, c);
}

// ---- text ----

static int32 lengthFromPx(CxUiDraw* d, int32 px)
{
    return d->dip ? (int32)floor(px / d->scale + 0.5) : px;
}

static void underline(CxUiDraw* d, CxUiFont* f, int32 x, int32 y, int32 w, CxUiColor c)
{
    int32 thick = max(f->_metrics.height / 16, 1);
    fillPx(d, (CxUiRect){ x, y + f->_metrics.ascent + 1, w, thick }, c);
}

// True if a line of f at y can't touch the clip.
static bool lineHidden(CxUiDraw* d, CxUiFont* f, int32 y)
{
    CxUiRect clip = curClip(d);
    return y >= clip.y + clip.h || y + f->_metrics.height <= clip.y;
}

static int32 textPx(CxUiDraw* d, CxUiFont* f, int32 x, int32 y, strref text, CxUiColor c)
{
    if (strEmpty(text))
        return 0;
    if (c.a == 0 || lineHidden(d, f, y))
        return cxuiFontAdvance(f, text);
    int32 adv = d->ops->text(d->target, _cxuiFontNative(f, d->ops), (CxUiPoint){ x, y }, text, c);
    if (f->_flags & CXUI_FONT_Underline)
        underline(d, f, x, y, adv, c);
    return adv;
}

_Use_decl_annotations_
int32 cxuiDrawText(CxUiDraw* d, CxUiFont* f, int32 x, int32 y, strref text, CxUiColor c)
{
    if (d->dip) {
        x = scaleEdge(x, d->scale);
        y = scaleEdge(y, d->scale);
    }
    return lengthFromPx(d, textPx(d, f, x, y, text, c));
}

_Use_decl_annotations_
void cxuiDrawTextBox(CxUiDraw* d, CxUiFont* f, CxUiRect box, strref text, flags_t align, CxUiColor c)
{
    if (strEmpty(text))
        return;
    CxUiRect r = rectToPx(d, box.x, box.y, box.w, box.h);
    cxuiDrawClipPush(d, box);

    int32 lines = 1, len = strLen(text);
    for (int32 i = strFindChar(text, 0, '\n'); i >= 0; i = strFindChar(text, i + 1, '\n'))
        lines++;
    int32 lh = f->_metrics.height;
    int32 y  = r.y;
    if (align & CXUI_TEXT_Middle)
        y = r.y + (r.h - lines * lh) / 2;
    else if (align & CXUI_TEXT_Bottom)
        y = r.y + r.h - lines * lh;

    // One line is drawn as it is; only several need copying apart.
    string buf  = 0;
    strref line = text;
    for (int32 start = 0;; y += lh) {
        int32 nl = strFindChar(text, start, '\n');
        if (lines > 1) {
            strSubStr(&buf, text, start, nl < 0 ? len : nl);
            line = buf;
        }
        int32 x = r.x;
        if (align & (CXUI_TEXT_Center | CXUI_TEXT_Right)) {
            int32 w = cxuiFontAdvance(f, line);
            x       = (align & CXUI_TEXT_Right) ? r.x + r.w - w : r.x + (r.w - w) / 2;
        }
        textPx(d, f, x, y, line, c);
        if (nl < 0)
            break;
        start = nl + 1;
    }
    strDestroy(&buf);
    cxuiDrawClipPop(d);
}

_Use_decl_annotations_
int32 cxuiDrawTextCells(CxUiDraw* d, CxUiFont* f, int32 x, int32 y, strref text, int32 cellw, CxUiColor c)
{
    int32 n = _cxuiCodepoints(text);
    // Each cell edge is converted on its own, as every other DIP position is, so cells line up
    // with anything else drawn at x + i * cellw.
    int32 px0 = d->dip ? scaleEdge(x, d->scale) : x;
    int32 px1 = d->dip ? scaleEdge((float64)x + (float64)n * cellw, d->scale) : x + n * cellw;
    if (d->dip)
        y = scaleEdge(y, d->scale);
    CxUiRect clip = curClip(d);
    if (n == 0 || c.a == 0 || cellw <= 0 || lineHidden(d, f, y) || px0 >= clip.x + clip.w || px1 <= clip.x)
        return n;

    int32 buf[256];
    int32* widths = n <= 256 ? buf : xaAlloc(sizeof(int32) * n);
    for (int32 i = 0, prev = px0; i < n; i++) {
        int32 next = d->dip ? scaleEdge((float64)x + (float64)(i + 1) * cellw, d->scale) : px0 + (i + 1) * cellw;
        widths[i]  = next - prev;
        prev       = next;
    }
    d->ops->textCells(d->target, _cxuiFontNative(f, d->ops), (CxUiPoint){ px0, y }, text, widths, c);
    if (widths != buf)
        xaFree(widths);
    if (f->_flags & CXUI_FONT_Underline)
        underline(d, f, px0, y, px1 - px0, c);
    return n;
}

// ---- images ----

_Use_decl_annotations_
void cxuiDrawImage(CxUiDraw* d, CxUiImage* img, const CxUiRect* src, CxUiRect dst)
{
    CxUiImage* root = img->_root ? img->_root : img;
    // src is relative to img; the engine wants it within the root.
    CxUiRect s = src ? *src : (CxUiRect){ 0, 0, img->_rect.w, img->_rect.h };
    s          = _cxuiRectIntersect((CxUiRect){ img->_rect.x + s.x, img->_rect.y + s.y, s.w, s.h }, img->_rect);
    dst        = rectToPx(d, dst.x, dst.y, dst.w, dst.h);
    if (_cxuiRectEmpty(s) || _cxuiRectEmpty(dst) || _cxuiRectEmpty(_cxuiRectIntersect(dst, curClip(d))))
        return;
    void* native = _cxuiImageNative(root, d->ops);
    if (native)
        d->ops->image(d->target, native, s, dst);
}

// ---- clipping ----

_Use_decl_annotations_
void cxuiDrawClipPush(CxUiDraw* d, CxUiRect r)
{
    if (d->nclip == d->clipCap) {
        int32 cap = d->clipCap * 2;
        if (d->clip == d->clipInline) {
            d->clip = xaAlloc(cap * sizeof(CxUiRect));
            memcpy(d->clip, d->clipInline, sizeof(d->clipInline));
        } else {
            xaResize(&d->clip, cap * sizeof(CxUiRect));
        }
        d->clipCap = cap;
    }
    r                   = _cxuiRectIntersect(rectToPx(d, r.x, r.y, r.w, r.h), curClip(d));
    d->clip[d->nclip++] = r;
    d->ops->clipSet(d->target, r);
}

_Use_decl_annotations_
void cxuiDrawClipPop(CxUiDraw* d)
{
    if (d->nclip <= 1)
        return;
    d->nclip--;
    d->ops->clipSet(d->target, curClip(d));
}

// ---- queries ----

_Use_decl_annotations_
CxUiRect cxuiDrawDirty(CxUiDraw* d)
{
    return rectFromPx(d, d->dirty);
}

_Use_decl_annotations_
CxUiSize cxuiDrawSize(CxUiDraw* d)
{
    if (!d->dip)
        return d->size;
    return (CxUiSize){ (int32)floor(d->size.w / d->scale + 0.5), (int32)floor(d->size.h / d->scale + 0.5) };
}

_Use_decl_annotations_
float32 cxuiDrawScale(CxUiDraw* d)
{
    return d->scale;
}

// ---- paint pass ----

typedef void (*DrawPassFn)(CxUiDraw* d, void* ctx);

// How long the last pass took in each part, for cxuiTestPaintTimes(); UI thread only
static CxUiTestPaintTimes lastPass;

// Returns the area drawn, which the target may have grown.
static CxUiRect drawPass(const CxUiDrawOps* ops, void* target, CxUiSize size, float32 scale, bool dip, CxUiRect box,
                         DrawPassFn fn, void* ctx)
{
    if (ops->expandDirty)
        box = ops->expandDirty(target, box);
    CxUiDraw d = {
        .ops     = ops,
        .target  = target,
        .dirty   = box,
        .size    = size,
        .scale   = scale,
        .dip     = dip,
        .nclip   = 1,
        .clipCap = sizeof(d.clipInline) / sizeof(d.clipInline[0]),
    };
    d.clip    = d.clipInline;
    d.clip[0] = box;

    int64 t0 = clockTimer();
    ops->begin(target, box);
    int64 t1 = clockTimer();
    fn(&d, ctx);
    int64 t2 = clockTimer();
    ops->end(target);
    lastPass = (CxUiTestPaintTimes){ .begin = t1 - t0, .draw = t2 - t1, .end = clockTimer() - t2 };
    if (d.clip != d.clipInline)
        xaFree(d.clip);
    return box;
}

_Use_decl_annotations_
void cxuiTestPaintTimes(CxUiTestPaintTimes* out)
{
    *out = lastPass;
}

static void paintHandler(CxUiDraw* d, void* ctx)
{
    CxUiCanvas* cv    = ctx;
    CxUiPaintEvent ev = { .base.kind = CXUI_EV_Paint, .draw = d, .dirty = rectFromPx(d, d->dirty) };
    _cxuiDispatch(CxUiWidget(cv), &ev.base);
}

_Use_decl_annotations_
bool _cxuiCanvasPaint(CxUiCanvasPeer* p, CxUiRect* painted)
{
    CxUiCanvas* cv = p->base.w ? objDynCast(CxUiCanvas, p->base.w) : NULL;
    if (!cv || !_cxuiLive(CxUiWidget(cv)) || p->painting)
        return false;

    // A render view has no target: its paint event asks the program for a frame of all of it,
    // whether marked or shown again by the platform.
    if (!p->ops) {
        cv->_dirty.n      = 0;
        float32 s         = cv->_units == CXUI_Units_Dip ? cxuiWidgetScale(cv) : 1.0f;
        CxUiPaintEvent ev = { .base.kind = CXUI_EV_Paint,
                              .dirty     = { 0, 0, (int32)ceilf(cv->_size.w / s), (int32)ceilf(cv->_size.h / s) } };
        p->painting       = true;
        objAcquire(cv);
        _cxuiDispatch(CxUiWidget(cv), &ev.base);
        objRelease(&cv);
        p->painting = false;
        if (p->boundsPending) {
            p->boundsPending = false;
            _cxui.backend->canvasSetBounds(&p->base, p->pendingBounds);
        }
        if (painted)
            *painted = (CxUiRect){ 0, 0, 0, 0 };
        return true;
    }
    if (cv->_dirty.n == 0)
        return false;

    CxUiRect box = cv->_dirty.r[0];
    for (int32 i = 1; i < cv->_dirty.n; i++)
        box = _cxuiRectUnion(box, cv->_dirty.r[i]);
    // Cleared first, so the handler can mark more for the next paint.
    cv->_dirty.n = 0;
    box          = _cxuiRectIntersect(box, (CxUiRect){ 0, 0, cv->_size.w, cv->_size.h });
    if (_cxuiRectEmpty(box))
        return false;

    p->painting = true;
    objAcquire(cv);
    box = drawPass(p->ops, p->target, cv->_size, cxuiWidgetScale(cv), cv->_units == CXUI_Units_Dip, box, paintHandler,
                   cv);
    objRelease(&cv);
    p->painting = false;

    if (p->boundsPending) {
        p->boundsPending = false;
        _cxui.backend->canvasSetBounds(&p->base, p->pendingBounds);
    }
    if (painted)
        *painted = box;
    return true;
}

// ---- offscreen test targets ----

struct CxUiTestOffscreen {
    const CxUiDrawOps* ops;
    void* target;
    CxUiSize size;
};

_Use_decl_annotations_
CxUiTestOffscreen* cxuiTestOffscreenCreate(uint32 drawBackend, int32 w, int32 h)
{
    const CxUiDrawOps* ops = _cxuiPlatformDrawOps(drawBackend);
    void* target           = ops && ops->testRead ? ops->targetCreate(NULL) : NULL;
    if (!target)
        return NULL;
    CxUiTestOffscreen* o = xaAllocStruct(CxUiTestOffscreen, XA_Zero);
    o->ops               = ops;
    o->target            = target;
    o->size              = (CxUiSize){ w, h };
    ops->targetResize(target, o->size);
    return o;
}

_Use_decl_annotations_
strref cxuiTestOffscreenEngine(CxUiTestOffscreen* o)
{
    return o->ops->name;
}

_Use_decl_annotations_
void cxuiTestOffscreenDraw(CxUiTestOffscreen* o, CxUiRect dirty, CxUiTestDrawFn fn, void* ctx)
{
    dirty = _cxuiRectIntersect(dirty, (CxUiRect){ 0, 0, o->size.w, o->size.h });
    if (!_cxuiRectEmpty(dirty))
        drawPass(o->ops, o->target, o->size, 1.0f, false, dirty, fn, ctx);
}

_Use_decl_annotations_
CxUiFont* cxuiTestOffscreenFont(CxUiTestOffscreen* o, const CxUiFontDesc* desc)
{
    return _cxuiFontMake(o->ops, desc ? desc->faces : NULL, desc ? desc->size : 0, desc ? desc->flags : 0, 1.0f);
}

_Use_decl_annotations_
void cxuiTestOffscreenRead(CxUiTestOffscreen* o, uint8* rgba)
{
    o->ops->testRead(o->target, rgba);
}

_Use_decl_annotations_
void cxuiTestOffscreenDestroy(CxUiTestOffscreen** o)
{
    if (!*o)
        return;
    (*o)->ops->targetDestroy((*o)->target);
    xaFree(*o);
    *o = NULL;
}
