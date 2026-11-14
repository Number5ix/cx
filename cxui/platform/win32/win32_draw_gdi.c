#include "win32_ui.h"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")

// ---- back buffer ----

#define BUFFER_SLACK 64

static int32 withSlack(int32 v)
{
    return (v + BUFFER_SLACK - 1) / BUFFER_SLACK * BUFFER_SLACK;
}

static HBITMAP createDib(HDC dc, int32 w, int32 h, void** bits)
{
    BITMAPINFO bmi              = { 0 };
    bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;   // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, bits, NULL, 0);
}

_Use_decl_annotations_
bool _cxuiWin32BufferInit(Win32BackBuffer* bb)
{
    memset(bb, 0, sizeof(*bb));
    bb->dc = CreateCompatibleDC(NULL);
    return bb->dc != NULL;
}

_Use_decl_annotations_
void _cxuiWin32BufferResize(Win32BackBuffer* bb, CxUiSize px)
{
    bb->size = px;
    // Grows with slack, so a live resize doesn't reallocate for every pixel; shrinks once the
    // buffer is more than twice what is needed both ways.
    bool grow   = px.w > bb->alloc.w || px.h > bb->alloc.h;
    bool shrink = px.w * 2 < bb->alloc.w && px.h * 2 < bb->alloc.h;
    if (!grow && !shrink && bb->bmp)
        return;

    CxUiSize alloc = { withSlack(max(px.w, 1)), withSlack(max(px.h, 1)) };
    void* bits     = NULL;
    HBITMAP bmp    = createDib(bb->dc, alloc.w, alloc.h, &bits);
    if (!bmp) {
        logFmt(Error, _SL("cxui couldn't allocate a ${int}x${int} canvas buffer"), stvar(int32, alloc.w),
               stvar(int32, alloc.h));
        return;
    }
    HGDIOBJ old = SelectObject(bb->dc, bmp);
    if (bb->bmp)
        DeleteObject(bb->bmp);
    else
        bb->oldBmp = old;
    bb->bmp   = bmp;
    bb->bits  = bits;
    bb->alloc = alloc;
}

_Use_decl_annotations_
void _cxuiWin32BufferDestroy(Win32BackBuffer* bb)
{
    if (bb->bmp) {
        SelectObject(bb->dc, bb->oldBmp);
        DeleteObject(bb->bmp);
    }
    if (bb->dc)
        DeleteDC(bb->dc);
    memset(bb, 0, sizeof(*bb));
}

_Use_decl_annotations_
void _cxuiWin32BufferRead(Win32BackBuffer* bb, uint8* rgba)
{
    GdiFlush();
    for (int32 y = 0; y < bb->size.h; y++) {
        const uint8* src = (const uint8*)bb->bits + (size_t)y * bb->alloc.w * 4;
        uint8* dst       = rgba + (size_t)y * bb->size.w * 4;
        for (int32 x = 0; x < bb->size.w; x++) {
            dst[x * 4]     = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4];
            dst[x * 4 + 3] = 255;
        }
    }
}

// ---- GDI ----

// ---- fonts ----

#ifndef CLEARTYPE_QUALITY
#define CLEARTYPE_QUALITY 5
#endif

// Each font keeps a memory DC with it selected, for measuring without a screen DC.
typedef struct GdiFont {
    HFONT font;
    HDC dc;
    HGDIOBJ old;
    CxUiFontMetrics m;
    // Advance widths of UTF-16 units, 256 to a page, each page filled by one call when first used
    int16* widths[256];
} GdiFont;

static void faceCopy(WCHAR* dst, const WCHAR* src)
{
    size_t n = min(wcslen(src), (size_t)LF_FACESIZE - 1);
    memcpy(dst, src, n * sizeof(WCHAR));
    dst[n] = 0;
}

static int CALLBACK faceFound(const LOGFONTW* lf, const TEXTMETRICW* tm, DWORD type, LPARAM found)
{
    unused_noeval(lf);
    unused_noeval(tm);
    unused_noeval(type);
    *(bool*)found = true;
    return 0;
}

static bool faceInstalled(HDC dc, const WCHAR* face)
{
    LOGFONTW lf  = { 0 };
    lf.lfCharSet = DEFAULT_CHARSET;
    faceCopy(lf.lfFaceName, face);
    bool found = false;
    EnumFontFamiliesExW(dc, &lf, faceFound, (LPARAM)&found, 0);
    return found;
}

void _cxuiWin32SystemUiFont(LOGFONTW* out)
{
    NONCLIENTMETRICSW ncm = { 0 };
    // The size without iPaddedBorderWidth, which XP doesn't know
    ncm.cbSize = offsetof(NONCLIENTMETRICSW, lfMessageFont) + sizeof(LOGFONTW);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0)) {
        *out = ncm.lfMessageFont;
    } else {
        memset(out, 0, sizeof(*out));
        out->lfHeight = -12;
        faceCopy(out->lfFaceName, L"Tahoma");
    }
}

// The first installed face from the list, as a generic family where named one. True for a
// monospace list with none of its faces installed (as under Wine): out is then empty, for any
// fixed-pitch font the system has.
static bool pickFace(HDC dc, strref faces, const LOGFONTW* ui, WCHAR* out)
{
    faceCopy(out, ui->lfFaceName);
    bool wantFixed = false;
    string face = 0;
    int32 len   = strLen(faces);
    for (int32 start = 0; start <= len;) {
        int32 end = strFindChar(faces, start, ';');
        if (end < 0)
            end = len;
        strSubStr(&face, faces, start, end);
        strTrim(&face, face, 0);
        start = end + 1;

        static const struct {
            strref generic;
            const WCHAR* faces[2];
        } generics[] = {
            { _S"sans", { L"Segoe UI", L"Arial" } },
            { _S"serif", { L"Cambria", L"Times New Roman" } },
            { _S"monospace", { L"Consolas", L"Courier New" } },
        };
        if (strEqi(face, _S"ui")) {
            strDestroy(&face);
            return false;
        }
        bool generic = false;
        for (int32 g = 0; g < 3; g++) {
            if (!strEqi(face, generics[g].generic))
                continue;
            generic = true;
            for (int32 i = 0; i < 2; i++) {
                if (faceInstalled(dc, generics[g].faces[i])) {
                    faceCopy(out, generics[g].faces[i]);
                    strDestroy(&face);
                    return false;
                }
            }
            wantFixed |= g == 2;
        }
        if (generic || strEmpty(face))
            continue;
        WCHAR w[LF_FACESIZE] = { 0 };
        strToUTF16(face, (uint16*)w, LF_FACESIZE);
        if (w[0] && faceInstalled(dc, w)) {
            faceCopy(out, w);
            strDestroy(&face);
            return false;
        }
    }
    strDestroy(&face);
    if (wantFixed)
        out[0] = 0;
    return wantFixed;
}

// UTF-16 for a run, in a caller's buffer when it fits
typedef struct Wide {
    uint16* s;
    INT* dx;
    int32 n;
    uint16 buf[256];
    INT dxbuf[256];
} Wide;

static bool wideInit(Wide* w, strref s)
{
    w->s        = w->buf;
    w->dx       = w->dxbuf;
    size_t need = strToUTF16(s, w->buf, 256);
    if (need > 256) {
        w->s  = xaAlloc(need * sizeof(uint16));
        w->dx = xaAlloc(need * sizeof(INT));
        need  = strToUTF16(s, w->s, need);
    }
    w->n = need > 0 ? (int32)need - 1 : 0;
    return w->n > 0;
}

static void wideDestroy(Wide* w)
{
    if (w->s != w->buf) {
        xaFree(w->s);
        xaFree(w->dx);
    }
}

// Units whose advance depends on their neighbors: combining marks, scripts that are shaped,
// direction and joiner controls, variation selectors, and surrogates.
static bool needsContext(uint16 u)
{
    return (u >= 0x0300 && u < 0x0370) || (u >= 0x0590 && u < 0x1E00) || (u >= 0x200B && u <= 0x200F) ||
           (u >= 0x2028 && u <= 0x202E) || (u >= 0xD800 && u < 0xE000) || (u >= 0xFE00 && u < 0xFE10);
}

static int32 unitWidth(GdiFont* f, uint16 u)
{
    int16** page = &f->widths[u >> 8];
    if (!*page) {
        INT w[256];
        *page = xaAlloc(sizeof(int16) * 256);
        if (!GetCharWidth32W(f->dc, u & 0xFF00, (u & 0xFF00) | 0xFF, w))
            memset(w, 0, sizeof(w));
        for (int32 i = 0; i < 256; i++)
            (*page)[i] = (int16)w[i];
    }
    return (*page)[u & 0xFF];
}

// Each unit's advance in w->dx, so drawing and measuring agree exactly; returns the total. Simple
// text adds up cached widths, which is what ExtTextOutW would use; anything else asks GDI.
static int32 wideAdvances(GdiFont* f, Wide* w)
{
    int32 total = 0, i = 0;
    for (; i < w->n && !needsContext(w->s[i]); i++) {
        w->dx[i] = unitWidth(f, w->s[i]);
        total += w->dx[i];
    }
    if (i == w->n)
        return total;

    SIZE sz;
    if (!GetTextExtentExPointW(f->dc, (LPCWSTR)w->s, w->n, 0, NULL, w->dx, &sz))
        return 0;
    for (int32 i = w->n - 1; i > 0; i--)
        w->dx[i] -= w->dx[i - 1];
    return sz.cx;
}

static void* gdiFontCreate(strref faces, float32 px, float32 scale, uint32 flags)
{
    GdiFont* f = xaAllocStruct(GdiFont, XA_Zero);
    f->dc      = CreateCompatibleDC(NULL);

    LOGFONTW ui;
    _cxuiWin32SystemUiFont(&ui);
    LOGFONTW lf = { 0 };
    if (px <= 0) {
        // The system's size is at the system DPI.
        int sysDpi = GetDeviceCaps(f->dc, LOGPIXELSY);
        px         = (float32)abs(ui.lfHeight) * scale * 96.0f / (sysDpi ? sysDpi : 96);
    }
    lf.lfHeight  = -(LONG)(px + 0.5f);
    lf.lfWeight  = (flags & CXUI_FONT_Bold) ? FW_BOLD : FW_NORMAL;
    lf.lfItalic  = (flags & CXUI_FONT_Italic) ? TRUE : FALSE;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    if (pickFace(f->dc, faces, &ui, lf.lfFaceName))
        lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;

    f->font = CreateFontIndirectW(&lf);
    f->old  = SelectObject(f->dc, f->font);
    TEXTMETRICW tm;
    GetTextMetricsW(f->dc, &tm);
    SIZE zero = { 0 };
    GetTextExtentPoint32W(f->dc, L"0", 1, &zero);
    f->m = (CxUiFontMetrics){ .ascent   = tm.tmAscent,
                              .descent  = tm.tmDescent,
                              .height   = tm.tmHeight,
                              .avgCharW = tm.tmAveCharWidth,
                              .cellW    = zero.cx,
                              .cellH    = tm.tmHeight };
    return f;
}

_Use_decl_annotations_
HFONT _cxuiWin32GdiFont(CxUiFont* f, TEXTMETRICW* tm)
{
    GdiFont* g = _cxuiFontNative(f, &_cxuiWin32GdiOps);
    if (tm)
        GetTextMetricsW(g->dc, tm);
    return g->font;
}

static void gdiFontDestroy(void* fp)
{
    GdiFont* f = fp;
    for (int32 i = 0; i < 256; i++)
        xaFree(f->widths[i]);
    SelectObject(f->dc, f->old);
    DeleteDC(f->dc);
    DeleteObject(f->font);
    xaFree(f);
}

static void gdiFontMetrics(void* fp, CxUiFontMetrics* out)
{
    *out = ((GdiFont*)fp)->m;
}

static int32 gdiFontAdvance(void* fp, strref s)
{
    Wide w;
    int32 adv = wideInit(&w, s) ? wideAdvances(fp, &w) : 0;
    wideDestroy(&w);
    return adv;
}

// ---- targets ----

typedef struct GdiTarget {
    Win32BackBuffer bb;   // first: a GDI canvas presents from it
    HFONT curFont;        // selected into bb.dc, or NULL
    // The clip. A region on the DC is made only when a line or an image needs it: setting one for
    // every clip change costs more than the text it clips (a row view changes it every row).
    // Fills arrive clipped, and text is clipped with ETO_CLIPPED.
    RECT clip;
    bool clipRegion;   // the DC's region matches clip
    bool anyRegion;    // the DC has a region, which may be an earlier clip's
    // A 1x1 premultiplied pixel, stretched by AlphaBlend for translucent fills
    HDC pixelDC;
    HBITMAP pixelBmp;
    HGDIOBJ pixelOld;
    uint32* pixel;
} GdiTarget;

static COLORREF colorRef(CxUiColor c)
{
    return RGB(c.r, c.g, c.b);
}

static void* gdiTargetCreate(CxUiCanvasPeer* p)
{
    unused_noeval(p);
    GdiTarget* t = xaAllocStruct(GdiTarget, XA_Zero);
    if (!_cxuiWin32BufferInit(&t->bb)) {
        xaFree(t);
        return NULL;
    }
    t->pixelDC  = CreateCompatibleDC(NULL);
    t->pixelBmp = createDib(t->pixelDC, 1, 1, (void**)&t->pixel);
    t->pixelOld = SelectObject(t->pixelDC, t->pixelBmp);
    return t;
}

static void gdiTargetResize(void* tp, CxUiSize px)
{
    _cxuiWin32BufferResize(&((GdiTarget*)tp)->bb, px);
}

static void gdiTargetDestroy(void* tp)
{
    GdiTarget* t = tp;
    if (t->curFont)
        SelectObject(t->bb.dc, GetStockObject(SYSTEM_FONT));
    SelectObject(t->pixelDC, t->pixelOld);
    DeleteObject(t->pixelBmp);
    DeleteDC(t->pixelDC);
    _cxuiWin32BufferDestroy(&t->bb);
    xaFree(t);
}

static void gdiClipSet(void* tp, CxUiRect r)
{
    GdiTarget* t  = tp;
    t->clip       = (RECT){ r.x, r.y, r.x + r.w, r.y + r.h };
    t->clipRegion = false;
    // Fills rely on the DC having no region but the right one.
    if (t->anyRegion) {
        SelectClipRgn(t->bb.dc, NULL);
        t->anyRegion = false;
    }
}

static void clipRegion(GdiTarget* t)
{
    if (t->clipRegion)
        return;
    SelectClipRgn(t->bb.dc, NULL);
    IntersectClipRect(t->bb.dc, t->clip.left, t->clip.top, t->clip.right, t->clip.bottom);
    t->clipRegion = t->anyRegion = true;
}

static void gdiBegin(void* tp, CxUiRect dirty)
{
    GdiTarget* t = tp;
    SetBkMode(t->bb.dc, TRANSPARENT);
    SetTextAlign(t->bb.dc, TA_TOP | TA_LEFT | TA_NOUPDATECP);
    gdiClipSet(tp, dirty);
}

static void selectFont(GdiTarget* t, GdiFont* f, CxUiColor c)
{
    if (t->curFont != f->font) {
        SelectObject(t->bb.dc, f->font);
        t->curFont = f->font;
    }
    SetTextColor(t->bb.dc, colorRef(c));
}

static int32 gdiText(void* tp, void* fp, CxUiPoint at, strref s, CxUiColor c)
{
    Wide w;
    int32 adv = 0;
    if (wideInit(&w, s)) {
        adv = wideAdvances(fp, &w);
        selectFont(tp, fp, c);
        ExtTextOutW(((GdiTarget*)tp)->bb.dc, at.x, at.y, ETO_CLIPPED, &((GdiTarget*)tp)->clip, (LPCWSTR)w.s, w.n,
                    w.dx);
    }
    wideDestroy(&w);
    return adv;
}

static void gdiTextCells(void* tp, void* fp, CxUiPoint at, strref s, const int32* cellw, CxUiColor c)
{
    Wide w;
    if (wideInit(&w, s)) {
        // One cell per codepoint; a surrogate pair's advance goes on its second unit.
        for (int32 i = 0, cp = 0; i < w.n; i++) {
            bool pair = i + 1 < w.n && w.s[i] >= 0xD800 && w.s[i] <= 0xDBFF && w.s[i + 1] >= 0xDC00 && w.s[i + 1] <= 0xDFFF;
            if (pair)
                w.dx[i++] = 0;
            w.dx[i] = cellw[cp++];
        }
        selectFont(tp, fp, c);
        ExtTextOutW(((GdiTarget*)tp)->bb.dc, at.x, at.y, ETO_CLIPPED, &((GdiTarget*)tp)->clip, (LPCWSTR)w.s, w.n,
                    w.dx);
    }
    wideDestroy(&w);
}

static void gdiEnd(void* tp)
{
    SelectClipRgn(((GdiTarget*)tp)->bb.dc, NULL);
    ((GdiTarget*)tp)->clipRegion = ((GdiTarget*)tp)->anyRegion = false;
    GdiFlush();
}

static void gdiFill(void* tp, CxUiRect r, CxUiColor c)
{
    GdiTarget* t = tp;
    if (c.a == 255) {
        // The cheapest fill GDI has
        RECT rc = { r.x, r.y, r.x + r.w, r.y + r.h };
        SetBkColor(t->bb.dc, colorRef(c));
        ExtTextOutW(t->bb.dc, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL);
        return;
    }

    GdiFlush();
    // BGRA, premultiplied
    *t->pixel = ((uint32)c.a << 24) | ((uint32)((c.r * c.a + 127) / 255) << 16) |
                ((uint32)((c.g * c.a + 127) / 255) << 8) | (uint32)((c.b * c.a + 127) / 255);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(t->bb.dc, r.x, r.y, r.w, r.h, t->pixelDC, 0, 0, 1, 1, bf);
}

// Not anti-aliased: GDI has no anti-aliasing for lines.
static void gdiLine(void* tp, CxUiPoint a, CxUiPoint b, int32 width, CxUiColor c)
{
    clipRegion(tp);
    HDC dc = ((GdiTarget*)tp)->bb.dc;
    HPEN pen;
    if (width == 1) {
        pen = (HPEN)GetStockObject(DC_PEN);
        SetDCPenColor(dc, colorRef(c));
    } else {
        LOGBRUSH lb = { BS_SOLID, colorRef(c), 0 };
        pen         = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT, width, &lb, 0, NULL);
    }
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, a.x, a.y, NULL);
    LineTo(dc, b.x, b.y);
    // LineTo leaves out the last pixel.
    if (width == 1)
        SetPixelV(dc, b.x, b.y, colorRef(c));
    SelectObject(dc, old);
    if (width != 1)
        DeleteObject(pen);
}

// ---- images ----

// Each image keeps its bitmap selected into a DC of its own, which any target can copy from.
typedef struct GdiImage {
    HDC dc;
    HBITMAP bmp;
    HGDIOBJ old;
    bool opaque;   // every pixel's alpha is 255, so it can be copied rather than blended
} GdiImage;

static void* gdiImageUpload(const void* px, int32 w, int32 h)
{
    GdiImage* img = xaAllocStruct(GdiImage, XA_Zero);
    void* bits    = NULL;
    img->dc       = CreateCompatibleDC(NULL);
    img->bmp      = createDib(img->dc, w, h, &bits);
    if (!img->dc || !img->bmp) {
        if (img->dc)
            DeleteDC(img->dc);
        xaFree(img);
        return NULL;
    }
    memcpy(bits, px, (size_t)w * h * 4);
    img->old    = SelectObject(img->dc, img->bmp);
    img->opaque = true;
    for (size_t i = 0; i < (size_t)w * h && img->opaque; i++)
        img->opaque = ((const uint8*)px)[i * 4 + 3] == 255;
    return img;
}

static void gdiImageFree(void* ip)
{
    GdiImage* img = ip;
    SelectObject(img->dc, img->old);
    DeleteObject(img->bmp);
    DeleteDC(img->dc);
    xaFree(img);
}

// Scaling picks the nearest pixels.
static void gdiImage(void* tp, void* ip, CxUiRect src, CxUiRect dst)
{
    GdiTarget* t  = tp;
    GdiImage* img = ip;
    clipRegion(t);
    if (img->opaque && src.w == dst.w && src.h == dst.h) {
        BitBlt(t->bb.dc, dst.x, dst.y, dst.w, dst.h, img->dc, src.x, src.y, SRCCOPY);
    } else if (img->opaque) {
        SetStretchBltMode(t->bb.dc, COLORONCOLOR);
        StretchBlt(t->bb.dc, dst.x, dst.y, dst.w, dst.h, img->dc, src.x, src.y, src.w, src.h, SRCCOPY);
    } else {
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        AlphaBlend(t->bb.dc, dst.x, dst.y, dst.w, dst.h, img->dc, src.x, src.y, src.w, src.h, bf);
    }
}

static void gdiTestRead(void* tp, uint8* rgba)
{
    _cxuiWin32BufferRead(&((GdiTarget*)tp)->bb, rgba);
}

const CxUiDrawOps _cxuiWin32GdiOps = {
    .name          = _S"gdi",
    .targetCreate  = gdiTargetCreate,
    .targetResize  = gdiTargetResize,
    .targetDestroy = gdiTargetDestroy,
    .begin         = gdiBegin,
    .end           = gdiEnd,
    .fill          = gdiFill,
    .line          = gdiLine,
    .clipSet       = gdiClipSet,
    .fontCreate    = gdiFontCreate,
    .fontDestroy   = gdiFontDestroy,
    .fontMetrics   = gdiFontMetrics,
    .fontAdvance   = gdiFontAdvance,
    .text          = gdiText,
    .textCells     = gdiTextCells,
    .imageUpload   = gdiImageUpload,
    .imageFree     = gdiImageFree,
    .image         = gdiImage,
    .testRead      = gdiTestRead,
};
