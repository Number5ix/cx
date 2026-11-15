#pragma once

// The draw front end (cxuidraw.c) and the per-backend draw ops beneath it. The front end converts
// units, keeps the clip stack, clips fills, and turns frames and straight lines into fills, so
// every backend draws those identically.

#include "cxui_private.h"

#include "core/cxuifont.h"
#include "core/cxuiimage.h"

CX_C_BEGIN

typedef struct CxUiCanvasPeer CxUiCanvasPeer;

// One table per drawing engine. A target is one canvas's back buffer; the backend creates,
// resizes and destroys it along with the canvas's peer.
typedef struct CxUiDrawOps {
    strref name;
    void* (*targetCreate)(CxUiCanvasPeer* p);
    // Grows with slack, and never in the middle of a paint pass
    void (*targetResize)(void* t, CxUiSize px);
    void (*targetDestroy)(void* t);

    // Grows a dirty rect to what the target can repaint, within its size; NULL to keep it
    CxUiRect (*expandDirty)(void* t, CxUiRect dirty);
    // A paint pass into the back buffer, clipped to dirty
    void (*begin)(void* t, CxUiRect dirty);
    void (*end)(void* t);

    // Already clipped, never empty, alpha above 0
    void (*fill)(void* t, CxUiRect r, CxUiColor c);
    // Anti-aliased, through the centers of the end pixels, with square ends so both end pixels
    // are covered; never horizontal or vertical
    void (*line)(void* t, CxUiPoint a, CxUiPoint b, int32 width, CxUiColor c);
    // Replaces the clip; always within the dirty rect
    void (*clipSet)(void* t, CxUiRect r);

    // Fonts need no target, so they measure outside paint. px is the em size in pixels, or 0 for
    // the system's interface font size at scale. The front end draws underlines.
    void* (*fontCreate)(strref faces, float32 px, float32 scale, uint32 flags);
    void (*fontDestroy)(void* f);
    void (*fontMetrics)(void* f, CxUiFontMetrics* out);
    // One line; the same rounding as text
    int32 (*fontAdvance)(void* f, strref s);
    // at is the top left of the line; returns the advance. Not empty.
    int32 (*text)(void* t, void* f, CxUiPoint at, strref s, CxUiColor c);
    // Codepoint n's cell is cellw[n] wide, the first at at.x; one entry per codepoint. Not empty.
    void (*textCells)(void* t, void* f, CxUiPoint at, strref s, const int32* cellw, CxUiColor c);

    // Images need no target either. px is premultiplied B, G, R, A, rows of w pixels; the copy
    // may be freed on any thread.
    void* (*imageUpload)(const void* px, int32 w, int32 h);
    void (*imageFree)(void* img);
    // src within the image, scaled to dst with linear filtering where the engine can
    void (*image)(void* t, void* img, CxUiRect src, CxUiRect dst);

    // Tests: the target's pixels, top-down RGBA rows of its size; alpha is undefined. May be NULL.
    void (*testRead)(void* t, uint8* rgba);
} CxUiDrawOps;

// The draw ops this platform has for a CxUiDrawBackends value, whether or not cxui is running;
// NULL where it has none. Each platform backend defines it.
const CxUiDrawOps* _cxuiPlatformDrawOps(uint32 which);

// The null backend's ops, which also stand in where a platform has none.
extern const CxUiDrawOps _cxuiNullDrawOps;

// The ops fonts are made for: the null ones while a null session runs, so measuring is the same
// on every machine; otherwise the platform's for which.
const CxUiDrawOps* _cxuiFontOps(uint32 which);

// A font measured by ops
CxUiFont* _cxuiFontMake(_In_ const CxUiDrawOps* ops, _In_opt_ strref faces, float32 size, uint32 flags,
                        float32 scale);

// f's native font for ops, made on first use. UI thread.
void* _cxuiFontNative(_In_ CxUiFont* f, _In_ const CxUiDrawOps* ops);

// The engine's copy of a root image, made on first use. UI thread.
void* _cxuiImageNative(_In_ CxUiImage* root, _In_ const CxUiDrawOps* ops);

// An image's first row of premultiplied B, G, R, A pixels, and the bytes from one row to the next
_Ret_valid_ const uint8* _cxuiImageRows(_In_ CxUiImage* img, _Out_ int32* stride);

// The number of codepoints in s
int32 _cxuiCodepoints(_In_opt_ strref s);
// The byte offset of code point n in s, or its length if s is shorter
int32 _cxuiByteOffset(_In_opt_ strref s, int32 n);

// The start of every backend's canvas peer
struct CxUiCanvasPeer {
    CxUiPeer base;
    const CxUiDrawOps* ops;
    void* target;
    // Bounds that arrived during a paint pass, applied after it
    CxUiRect pendingBounds;
    bool boundsPending;
    bool painting;
};

struct CxUiDraw {
    const CxUiDrawOps* ops;
    void* target;
    CxUiRect dirty;   // physical pixels, like everything below
    CxUiSize size;
    float32 scale;
    bool dip;
    // clip[0] is the dirty rect; the last entry is the current clip
    CxUiRect* clip;
    int32 nclip;
    int32 clipCap;
    CxUiRect clipInline[16];
};

_meta_inline bool _cxuiRectEmpty(CxUiRect r)
{
    return r.w <= 0 || r.h <= 0;
}

// No compound literals here: the Direct2D engine includes this from C++.
_meta_inline CxUiRect _cxuiRectIntersect(CxUiRect a, CxUiRect b)
{
    int32 x0 = max(a.x, b.x), y0 = max(a.y, b.y);
    int32 x1 = min(a.x + a.w, b.x + b.w), y1 = min(a.y + a.h, b.y + b.h);
    CxUiRect r = { x0, y0, max(x1 - x0, 0), max(y1 - y0, 0) };
    return r;
}

_meta_inline CxUiRect _cxuiRectUnion(CxUiRect a, CxUiRect b)
{
    int32 x0 = min(a.x, b.x), y0 = min(a.y, b.y);
    int32 x1 = max(a.x + a.w, b.x + b.w), y1 = max(a.y + a.h, b.y + b.h);
    CxUiRect r = { x0, y0, x1 - x0, y1 - y0 };
    return r;
}

_meta_inline bool _cxuiRectContains(CxUiRect outer, CxUiRect inner)
{
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w &&
           inner.y + inner.h <= outer.y + outer.h;
}

// Paints whatever is dirty into the peer's back buffer, through the paint handler. UI thread,
// inside _cxuiNativeEnter/Leave, so the peer outlives the call even if the handler destroys the
// canvas. Returns false, with nothing drawn, if nothing was dirty; otherwise painted receives the
// area drawn, in physical pixels.
bool _cxuiCanvasPaint(_In_ CxUiCanvasPeer* p, _Out_opt_ CxUiRect* painted);

// ---- canvases (cxuicanvas.c) ----

// Creates the canvas's peer inside parent, if the backend supports canvases.
void _cxuiCanvasRealize(_In_ CxUiCanvas* cv, _In_ CxUiPeer* parent);

// Places a realized canvas within its parent, in physical pixels. A new size repaints all of it
// and sends a resize event.
void _cxuiCanvasSetBounds(_In_ CxUiCanvas* cv, CxUiRect px);

// Marks an area in physical pixels to be painted.
void _cxuiCanvasInvalidatePx(_In_ CxUiCanvas* cv, CxUiRect px);

CX_C_END
