// Direct2D and DirectWrite. DirectWrite has no C interface, so this engine is C++ behind the same
// C ops table as the others. Both DLLs are loaded at run time; where either is missing (Windows
// XP) the engine reports itself unavailable and canvases use GDI.

#include "win32_ui.h"

#include <cx/utils/lazyinit.h>

#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <math.h>
#include <wincodec.h>

typedef HRESULT(WINAPI* D2D1CreateFactoryFn)(D2D1_FACTORY_TYPE type, REFIID iid, const D2D1_FACTORY_OPTIONS* opt,
                                             void** out);
typedef HRESULT(WINAPI* DWriteCreateFactoryFn)(DWRITE_FACTORY_TYPE type, REFIID iid, IUnknown** out);

static ID2D1Factory* d2dFactory;
static IDWriteFactory* dwFactory;
static IDWriteFontCollection* sysFonts;
static IWICImagingFactory* wicFactory;
static LazyInitState d2dInitState;
static atomic(uint32) nextTargetId;

template <typename T> static void release(T** p)
{
    if (*p) {
        (*p)->Release();
        *p = NULL;
    }
}

static void d2dInitOnce(void* unused)
{
    unused_noeval(unused);
    HMODULE d2d = LoadLibraryW(L"d2d1.dll");
    HMODULE dw  = LoadLibraryW(L"dwrite.dll");
    if (!d2d || !dw)
        return;
    D2D1CreateFactoryFn createD2d   = (D2D1CreateFactoryFn)GetProcAddress(d2d, "D2D1CreateFactory");
    DWriteCreateFactoryFn createDw = (DWriteCreateFactoryFn)GetProcAddress(dw, "DWriteCreateFactory");
    if (!createD2d || !createDw)
        return;

    // Multi-threaded, so resources can be released on any thread.
    D2D1_FACTORY_OPTIONS opt = { D2D1_DEBUG_LEVEL_NONE };
    if (FAILED(createD2d(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory), &opt, (void**)&d2dFactory)))
        return;
    // WIC's factory straight from its DLL, so it doesn't matter whether the thread has started COM
    // (a host's thread may not have). The original factory class, which every version has.
    static const CLSID wicClass = { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
    typedef HRESULT(WINAPI * DllGetClassObjectFn)(REFCLSID cls, REFIID iid, void** out);
    HMODULE wic                    = LoadLibraryW(L"windowscodecs.dll");
    DllGetClassObjectFn getClass   = wic ? (DllGetClassObjectFn)GetProcAddress(wic, "DllGetClassObject") : NULL;
    IClassFactory* cf              = NULL;
    if (getClass && SUCCEEDED(getClass(wicClass, __uuidof(IClassFactory), (void**)&cf))) {
        cf->CreateInstance(NULL, __uuidof(IWICImagingFactory), (void**)&wicFactory);
        cf->Release();
    }
    if (!wicFactory)
        CoCreateInstance(wicClass, NULL, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory), (void**)&wicFactory);

    if (!wicFactory || FAILED(createDw(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&dwFactory)) ||
        FAILED(dwFactory->GetSystemFontCollection(&sysFonts, FALSE))) {
        release(&wicFactory);
        release(&dwFactory);
        release(&d2dFactory);
    }
}

extern "C" bool _cxuiWin32D2dAvailable(void)
{
    lazyInit(&d2dInitState, d2dInitOnce, NULL);
    return sysFonts != NULL;
}

// ---- targets ----

// Each canvas draws into a WIC bitmap through a software render target, which keeps its pixels
// between passes and is shown from the bitmap's memory, with no copy into a DC first.
struct D2dTarget {
    IWICBitmap* bmp;
    ID2D1RenderTarget* rt;
    ID2D1SolidColorBrush* brush;
    CxUiSize size;    // in use
    CxUiSize alloc;   // of the bitmap, with slack
    uint32 id;        // names the target in images' bitmap caches
    bool innerClip;
    bool lost;        // EndDraw failed; the render target is made again before the next pass
    bool smooth;      // the antialias mode set: per primitive rather than aliased
};

#define BUFFER_SLACK 64

static int32 withSlack(int32 v)
{
    return (v + BUFFER_SLACK - 1) / BUFFER_SLACK * BUFFER_SLACK;
}

static void dropRenderTarget(D2dTarget* t)
{
    release(&t->brush);
    release(&t->rt);
}

static bool makeRenderTarget(D2dTarget* t)
{
    if (!t->bmp)
        return false;
    // Software: a hardware target pays for reading back from the GPU. 32-bit BGR without alpha,
    // so text can use ClearType.
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f,
        96.0f);
    if (FAILED(d2dFactory->CreateWicBitmapRenderTarget(t->bmp, &props, &t->rt)))
        return false;
    if (FAILED(t->rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &t->brush))) {
        release(&t->rt);
        return false;
    }
    t->id = atomicFetchAdd(uint32, &nextTargetId, 1, Relaxed) + 1;
    return true;
}

static void* d2dTargetCreate(CxUiCanvasPeer* p)
{
    unused_noeval(p);
    if (!_cxuiWin32D2dAvailable())
        return NULL;
    return xaAlloc(sizeof(D2dTarget), XA_Zero);
}

// Grows with slack, and makes the bitmap smaller once it is more than twice what is needed both
// ways. The contents are lost when it is made again, which a new size repaints anyway.
static void d2dTargetResize(void* tp, CxUiSize px)
{
    D2dTarget* t = (D2dTarget*)tp;
    t->size      = px;
    bool grow    = px.w > t->alloc.w || px.h > t->alloc.h;
    bool shrink  = px.w * 2 < t->alloc.w && px.h * 2 < t->alloc.h;
    if (!grow && !shrink && t->bmp)
        return;
    dropRenderTarget(t);
    release(&t->bmp);
    t->alloc.w = withSlack(max(px.w, 1));
    t->alloc.h = withSlack(max(px.h, 1));
    if (FAILED(wicFactory->CreateBitmap(t->alloc.w, t->alloc.h, GUID_WICPixelFormat32bppBGR, WICBitmapCacheOnLoad,
                                        &t->bmp)) ||
        !makeRenderTarget(t)) {
        logStr(Warn, _SL("Direct2D couldn't make a canvas buffer"));
        release(&t->bmp);
        t->alloc.w = t->alloc.h = 0;
    }
}

static void d2dTargetDestroy(void* tp)
{
    D2dTarget* t = (D2dTarget*)tp;
    dropRenderTarget(t);
    release(&t->bmp);
    xaFree(t);
}

// Calls fn with the bitmap's memory, rows top down.
template <typename Fn> static void withPixels(D2dTarget* t, Fn fn)
{
    if (!t->bmp)
        return;
    WICRect all            = { 0, 0, t->alloc.w, t->alloc.h };
    IWICBitmapLock* lock   = NULL;
    if (FAILED(t->bmp->Lock(&all, WICBitmapLockRead, &lock)))
        return;
    UINT bytes = 0, stride = 0;
    BYTE* px   = NULL;
    if (SUCCEEDED(lock->GetDataPointer(&bytes, &px)) && SUCCEEDED(lock->GetStride(&stride)))
        fn(px, stride);
    lock->Release();
}

extern "C" void _cxuiWin32D2dPresent(void* tp, HDC dc, const RECT* r)
{
    D2dTarget* t = (D2dTarget*)tp;
    withPixels(t, [&](BYTE* px, UINT stride) {
        // Only the rows being shown, as a bitmap of their own: the source's y is then 0 and covers
        // all of it, which StretchDIBits reads the same way for top-down and bottom-up bitmaps
        // (with part of one, it counts y from the bottom).
        int w = r->right - r->left, h = r->bottom - r->top;
        if (w <= 0 || h <= 0 || r->bottom > t->alloc.h)
            return;
        BITMAPINFO bmi              = {};
        bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
        bmi.bmiHeader.biWidth       = (LONG)(stride / 4);
        bmi.bmiHeader.biHeight      = -h;   // top down
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        StretchDIBits(dc, r->left, r->top, w, h, r->left, 0, w, h, px + (size_t)r->top * stride, &bmi, DIB_RGB_COLORS,
                      SRCCOPY);
    });
}

static D2D1_RECT_F rectF(CxUiRect r)
{
    return D2D1::RectF((float)r.x, (float)r.y, (float)(r.x + r.w), (float)(r.y + r.h));
}

// Changed only when it differs; each change is a state change for the render target.
static void setSmooth(D2dTarget* t, bool smooth)
{
    if (t->smooth != smooth) {
        t->smooth = smooth;
        t->rt->SetAntialiasMode(smooth ? D2D1_ANTIALIAS_MODE_PER_PRIMITIVE : D2D1_ANTIALIAS_MODE_ALIASED);
    }
}

static void setColor(D2dTarget* t, CxUiColor c)
{
    t->brush->SetColor(D2D1::ColorF(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f));
}

static void d2dBegin(void* tp, CxUiRect dirty)
{
    D2dTarget* t = (D2dTarget*)tp;
    if (t->lost) {
        dropRenderTarget(t);
        t->lost = !makeRenderTarget(t);
    }
    t->innerClip = false;
    if (!t->rt)
        return;
    t->rt->BeginDraw();
    t->smooth = false;
    t->rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    t->rt->PushAxisAlignedClip(rectF(dirty), D2D1_ANTIALIAS_MODE_ALIASED);
}

static void d2dEnd(void* tp)
{
    D2dTarget* t = (D2dTarget*)tp;
    if (!t->rt)
        return;
    if (t->innerClip)
        t->rt->PopAxisAlignedClip();
    t->rt->PopAxisAlignedClip();
    HRESULT hr = t->rt->EndDraw();
    if (FAILED(hr)) {
        static bool logged;
        if (!logged) {
            logged = true;
            // logFmt builds a compound literal, which C++ doesn't have.
            logStr(Warn, _SL("Direct2D couldn't draw a canvas; trying again on the next paint"));
        }
        t->lost = true;
    }
}

static void d2dFill(void* tp, CxUiRect r, CxUiColor c)
{
    D2dTarget* t = (D2dTarget*)tp;
    if (!t->rt)
        return;
    setColor(t, c);
    setSmooth(t, false);
    t->rt->FillRectangle(rectF(r), t->brush);
}

static void d2dLine(void* tp, CxUiPoint a, CxUiPoint b, int32 width, CxUiColor c)
{
    D2dTarget* t = (D2dTarget*)tp;
    if (!t->rt)
        return;
    setColor(t, c);
    setSmooth(t, true);
    // Each end is pushed out by half the width, as a square cap would, so both end pixels are
    // covered; Wine ignores stroke styles, so no cap style is used.
    float dx = (float)(b.x - a.x), dy = (float)(b.y - a.y);
    float k  = width * 0.5f / sqrtf(dx * dx + dy * dy);
    t->rt->DrawLine(D2D1::Point2F(a.x + 0.5f - dx * k, a.y + 0.5f - dy * k),
                    D2D1::Point2F(b.x + 0.5f + dx * k, b.y + 0.5f + dy * k), t->brush, (float)width);
}

static void d2dClipSet(void* tp, CxUiRect r)
{
    D2dTarget* t = (D2dTarget*)tp;
    if (!t->rt)
        return;
    if (t->innerClip)
        t->rt->PopAxisAlignedClip();
    t->rt->PushAxisAlignedClip(rectF(r), D2D1_ANTIALIAS_MODE_ALIASED);
    t->innerClip = true;
}

static void d2dTestRead(void* tp, uint8* rgba)
{
    D2dTarget* t = (D2dTarget*)tp;
    withPixels(t, [&](BYTE* px, UINT stride) {
        for (int32 y = 0; y < t->size.h; y++) {
            const uint8* src = px + (size_t)y * stride;
            uint8* dst       = rgba + (size_t)y * t->size.w * 4;
            for (int32 x = 0; x < t->size.w; x++) {
                dst[x * 4]     = src[x * 4 + 2];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4];
                dst[x * 4 + 3] = 255;
            }
        }
    });
}

// ---- fonts ----

// Glyph and rounded advance per codepoint, filled in on first use, in pages of 256 codepoints
// below 0x10000.
struct GlyphInfo {
    uint16 glyph;
    int16 advance;
    bool known;
};

struct DwFont {
    IDWriteFontFace* face;
    IDWriteTextFormat* format;   // for text that needs shaping or fallback
    float32 em;
    float32 unit;                // pixels per design unit
    CxUiFontMetrics m;
    GlyphInfo* pages[256];
};

static GlyphInfo lookupGlyph(DwFont* f, uint32 cp)
{
    GlyphInfo* gi = NULL;
    GlyphInfo tmp = { 0 };
    if (cp < 0x10000) {
        GlyphInfo*& page = f->pages[cp >> 8];
        if (!page)
            page = (GlyphInfo*)xaAlloc(sizeof(GlyphInfo) * 256, XA_Zero);
        gi = &page[cp & 0xFF];
        if (gi->known)
            return *gi;
    } else {
        gi = &tmp;
    }
    UINT16 glyph = 0;
    f->face->GetGlyphIndices(&cp, 1, &glyph);
    DWRITE_GLYPH_METRICS gm = { 0 };
    if (glyph)
        f->face->GetDesignGlyphMetrics(&glyph, 1, &gm, FALSE);
    gi->glyph   = glyph;
    gi->advance = (int16)floorf(gm.advanceWidth * f->unit + 0.5f);
    gi->known   = true;
    return *gi;
}

// Codepoints that need shaping or a fallback font, which glyph runs from one face can't do
static bool needsLayout(DwFont* f, uint32 cp)
{
    if ((cp >= 0x0300 && cp < 0x0370) || (cp >= 0x0590 && cp < 0x1E00) || (cp >= 0x200B && cp <= 0x200F) ||
        (cp >= 0x2028 && cp <= 0x202E) || (cp >= 0xFE00 && cp < 0xFE10) || cp >= 0x10000)
        return true;
    return lookupGlyph(f, cp).glyph == 0;
}

// A run's codepoints, in the caller's buffer when they fit
struct Codepoints {
    uint32* cp;
    int32 n;
    uint32 buf[256];

    Codepoints(strref s) : cp(buf), n(0)
    {
        int32 count = _cxuiCodepoints(s);
        if (count > 256)
            cp = (uint32*)xaAlloc(sizeof(uint32) * count);
        striter it;
        int32 c;
        striBorrow(&it, s);
        while (n < count && striU8Char(&it, &c))
            cp[n++] = (uint32)c;
        striFinish(&it);
    }
    ~Codepoints()
    {
        if (cp != buf)
            xaFree(cp);
    }
};

static bool runIsSimple(DwFont* f, const Codepoints& cps)
{
    for (int32 i = 0; i < cps.n; i++) {
        if (needsLayout(f, cps.cp[i]))
            return false;
    }
    return true;
}

static IDWriteTextLayout* makeLayoutW(DwFont* f, const WCHAR* w, UINT32 len)
{
    IDWriteTextLayout* layout = NULL;
    if (f->format)
        dwFactory->CreateTextLayout(w, len, f->format, 100000.0f, 100000.0f, &layout);
    return layout;
}

static IDWriteTextLayout* makeLayout(DwFont* f, strref s)
{
    uint16* w = strToUTF16A(s);
    if (!w)
        return NULL;
    IDWriteTextLayout* layout = makeLayoutW(f, (const WCHAR*)w, (UINT32)wcslen((const WCHAR*)w));
    xaFree(w);
    return layout;
}

static int32 layoutAdvance(IDWriteTextLayout* layout)
{
    DWRITE_TEXT_METRICS tm = { 0 };
    layout->GetMetrics(&tm);
    return (int32)floorf(tm.widthIncludingTrailingWhitespace + 0.5f);
}

// Whether a family's regular face gives 'i' and 'W' the same advance
static bool familyIsFixed(IDWriteFontFamily* family)
{
    IDWriteFont* font     = NULL;
    IDWriteFontFace* face = NULL;
    bool fixed            = false;
    if (SUCCEEDED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                               DWRITE_FONT_STYLE_NORMAL, &font)) &&
        SUCCEEDED(font->CreateFontFace(&face))) {
        const UINT32 cps[2] = { 'i', 'W' };
        UINT16 glyphs[2]    = { 0 };
        DWRITE_GLYPH_METRICS gm[2];
        fixed = SUCCEEDED(face->GetGlyphIndices(cps, 2, glyphs)) && glyphs[0] && glyphs[1] &&
                SUCCEEDED(face->GetDesignGlyphMetrics(glyphs, 2, gm, FALSE)) && gm[0].advanceWidth == gm[1].advanceWidth;
    }
    release(&face);
    release(&font);
    return fixed;
}

// The first installed family that is fixed-width, for a system without any cxui knows by name
static void anyFixedFamily(WCHAR* out)
{
    UINT32 n = sysFonts->GetFontFamilyCount();
    for (UINT32 i = 0; i < n && !out[0]; i++) {
        IDWriteFontFamily* family     = NULL;
        IDWriteLocalizedStrings* names = NULL;
        if (SUCCEEDED(sysFonts->GetFontFamily(i, &family)) && familyIsFixed(family) &&
            SUCCEEDED(family->GetFamilyNames(&names)) && FAILED(names->GetString(0, out, LF_FACESIZE)))
            out[0] = 0;
        release(&names);
        release(&family);
    }
}

static void familyFor(strref face, WCHAR* out, const LOGFONTW* ui)
{
    static const struct {
        strref generic;
        const WCHAR* names[2];
    } generics[] = {
        { _S"sans", { L"Segoe UI", L"Arial" } },
        { _S"serif", { L"Cambria", L"Times New Roman" } },
        { _S"monospace", { L"Consolas", L"Courier New" } },
    };
    out[0] = 0;
    if (strEqi(face, _S"ui")) {
        wcsncpy_s(out, LF_FACESIZE, ui->lfFaceName, _TRUNCATE);
        return;
    }
    for (int32 g = 0; g < 3; g++) {
        if (!strEqi(face, generics[g].generic))
            continue;
        for (int32 i = 0; i < 2; i++) {
            UINT32 index;
            BOOL exists = FALSE;
            if (SUCCEEDED(sysFonts->FindFamilyName(generics[g].names[i], &index, &exists)) && exists) {
                wcsncpy_s(out, LF_FACESIZE, generics[g].names[i], _TRUNCATE);
                return;
            }
        }
        if (g == 2)
            anyFixedFamily(out);
        return;
    }
    strToUTF16(face, (uint16*)out, LF_FACESIZE);
}

static void* d2dFontCreate(strref faces, float32 px, float32 scale, uint32 flags)
{
    if (!_cxuiWin32D2dAvailable())
        return NULL;
    LOGFONTW ui;
    _cxuiWin32SystemUiFont(&ui);
    if (px <= 0) {
        HDC dc     = GetDC(NULL);
        int sysDpi = GetDeviceCaps(dc, LOGPIXELSY);
        ReleaseDC(NULL, dc);
        px = (float32)abs(ui.lfHeight) * scale * 96.0f / (sysDpi ? sysDpi : 96);
    }

    // The first family in the list that is installed, else the interface font's
    IDWriteFontFamily* family = NULL;
    WCHAR name[LF_FACESIZE];
    string face = 0;
    int32 len   = strLen(faces);
    for (int32 start = 0; start <= len && !family;) {
        int32 end = strFindChar(faces, start, ';');
        if (end < 0)
            end = len;
        strSubStr(&face, faces, start, end);
        strTrim(&face, face, 0);
        start = end + 1;
        familyFor(face, name, &ui);
        UINT32 index;
        BOOL exists = FALSE;
        if (name[0] && SUCCEEDED(sysFonts->FindFamilyName(name, &index, &exists)) && exists)
            sysFonts->GetFontFamily(index, &family);
    }
    strDestroy(&face);
    if (!family) {
        UINT32 index;
        BOOL exists = FALSE;
        wcsncpy_s(name, LF_FACESIZE, ui.lfFaceName, _TRUNCATE);
        if (FAILED(sysFonts->FindFamilyName(name, &index, &exists)) || !exists) {
            wcsncpy_s(name, LF_FACESIZE, L"Arial", _TRUNCATE);
            if (FAILED(sysFonts->FindFamilyName(name, &index, &exists)) || !exists)
                index = 0;
        }
        sysFonts->GetFontFamily(index, &family);
    }

    DWRITE_FONT_WEIGHT weight = (flags & CXUI_FONT_Bold) ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STYLE style   = (flags & CXUI_FONT_Italic) ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
    IDWriteFont* font         = NULL;
    DwFont* f                 = (DwFont*)xaAlloc(sizeof(DwFont), XA_Zero);
    if (!family || FAILED(family->GetFirstMatchingFont(weight, DWRITE_FONT_STRETCH_NORMAL, style, &font)) ||
        FAILED(font->CreateFontFace(&f->face))) {
        release(&font);
        release(&family);
        xaFree(f);
        return NULL;
    }
    release(&font);
    release(&family);

    DWRITE_FONT_METRICS dm;
    f->face->GetMetrics(&dm);
    f->em        = px;
    f->unit      = px / dm.designUnitsPerEm;
    f->m.ascent  = (int32)ceilf(dm.ascent * f->unit);
    f->m.descent = (int32)ceilf(dm.descent * f->unit);
    f->m.height  = f->m.ascent + f->m.descent;
    f->m.avgCharW = max(lookupGlyph(f, 'x').advance, 1);
    f->m.cellW    = max(lookupGlyph(f, '0').advance, 1);
    f->m.cellH    = f->m.height;

    dwFactory->CreateTextFormat(name, NULL, weight, style, DWRITE_FONT_STRETCH_NORMAL, px, L"", &f->format);
    if (f->format) {
        // The same line box as glyph runs: baseline at the ascent
        f->format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, (float)f->m.height, (float)f->m.ascent);
        f->format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    return f;
}

static void d2dFontDestroy(void* fp)
{
    DwFont* f = (DwFont*)fp;
    if (!f)
        return;
    for (int32 i = 0; i < 256; i++)
        xaFree(f->pages[i]);
    release(&f->format);
    release(&f->face);
    xaFree(f);
}

static void d2dFontMetrics(void* fp, CxUiFontMetrics* out)
{
    DwFont* f = (DwFont*)fp;
    if (f) {
        *out = f->m;
    } else {
        CxUiFontMetrics none = { 0 };
        *out                 = none;
    }
}

static int32 d2dFontAdvance(void* fp, strref s)
{
    DwFont* f = (DwFont*)fp;
    if (!f)
        return 0;
    Codepoints cps(s);
    if (!runIsSimple(f, cps)) {
        IDWriteTextLayout* layout = makeLayout(f, s);
        int32 adv                 = layout ? layoutAdvance(layout) : 0;
        release(&layout);
        return adv;
    }
    int32 adv = 0;
    for (int32 i = 0; i < cps.n; i++)
        adv += lookupGlyph(f, cps.cp[i]).advance;
    return adv;
}

// A glyph run from one face, with the given advances, its baseline at the line's ascent
static void drawGlyphs(D2dTarget* t, DwFont* f, float32 x, float32 y, const UINT16* glyphs, const FLOAT* advances,
                       int32 n)
{
    DWRITE_GLYPH_RUN run = { 0 };
    run.fontFace         = f->face;
    run.fontEmSize       = f->em;
    run.glyphCount       = (UINT32)n;
    run.glyphIndices     = glyphs;
    run.glyphAdvances    = advances;
    t->rt->DrawGlyphRun(D2D1::Point2F(x, y + f->m.ascent), &run, t->brush, DWRITE_MEASURING_MODE_NATURAL);
}

static int32 d2dText(void* tp, void* fp, CxUiPoint at, strref s, CxUiColor c)
{
    D2dTarget* t = (D2dTarget*)tp;
    DwFont* f    = (DwFont*)fp;
    if (!f || !t->rt)
        return f ? d2dFontAdvance(f, s) : 0;
    setColor(t, c);
    Codepoints cps(s);
    if (!runIsSimple(f, cps)) {
        IDWriteTextLayout* layout = makeLayout(f, s);
        int32 adv                 = 0;
        if (layout) {
            adv = layoutAdvance(layout);
            t->rt->DrawTextLayout(D2D1::Point2F((float)at.x, (float)at.y), layout, t->brush);
        }
        release(&layout);
        return adv;
    }

    UINT16 gbuf[256];
    FLOAT abuf[256];
    UINT16* glyphs  = cps.n <= 256 ? gbuf : (UINT16*)xaAlloc(sizeof(UINT16) * cps.n);
    FLOAT* advances = cps.n <= 256 ? abuf : (FLOAT*)xaAlloc(sizeof(FLOAT) * cps.n);
    int32 adv       = 0;
    for (int32 i = 0; i < cps.n; i++) {
        GlyphInfo gi = lookupGlyph(f, cps.cp[i]);
        glyphs[i]    = gi.glyph;
        advances[i]  = gi.advance;
        adv += gi.advance;
    }
    drawGlyphs(t, f, (float)at.x, (float)at.y, glyphs, advances, cps.n);
    if (glyphs != gbuf) {
        xaFree(glyphs);
        xaFree(advances);
    }
    return adv;
}

// Codepoints this face can draw go in glyph runs with the cells' widths as advances; any other
// is drawn on its own, through a layout, at its cell.
static void d2dTextCells(void* tp, void* fp, CxUiPoint at, strref s, const int32* cellw, CxUiColor c)
{
    D2dTarget* t = (D2dTarget*)tp;
    DwFont* f    = (DwFont*)fp;
    if (!f || !t->rt)
        return;
    setColor(t, c);
    Codepoints cps(s);
    UINT16 gbuf[256];
    FLOAT abuf[256];
    UINT16* glyphs  = cps.n <= 256 ? gbuf : (UINT16*)xaAlloc(sizeof(UINT16) * cps.n);
    FLOAT* advances = cps.n <= 256 ? abuf : (FLOAT*)xaAlloc(sizeof(FLOAT) * cps.n);

    int32 x = at.x, runX = at.x, n = 0;
    for (int32 i = 0; i < cps.n; i++) {
        if (!needsLayout(f, cps.cp[i])) {
            if (n == 0)
                runX = x;
            glyphs[n]     = lookupGlyph(f, cps.cp[i]).glyph;
            advances[n++] = (FLOAT)cellw[i];
        } else {
            if (n > 0)
                drawGlyphs(t, f, (float)runX, (float)at.y, glyphs, advances, n);
            n = 0;
            uint32 cp = cps.cp[i];
            WCHAR w[2];
            UINT32 len = 1;
            if (cp >= 0x10000) {
                w[0] = (WCHAR)(0xD800 + ((cp - 0x10000) >> 10));
                w[1] = (WCHAR)(0xDC00 + ((cp - 0x10000) & 0x3FF));
                len  = 2;
            } else {
                w[0] = (WCHAR)cp;
            }
            IDWriteTextLayout* layout = makeLayoutW(f, w, len);
            if (layout)
                t->rt->DrawTextLayout(D2D1::Point2F((float)x, (float)at.y), layout, t->brush);
            release(&layout);
        }
        x += cellw[i];
    }
    if (n > 0)
        drawGlyphs(t, f, (float)runX, (float)at.y, glyphs, advances, n);
    if (glyphs != gbuf) {
        xaFree(glyphs);
        xaFree(advances);
    }
}

// ---- images ----

// A bitmap belongs to the render target that made it, so each image keeps a few.
#define IMAGE_SLOTS 4

struct D2dImage {
    void* px;   // premultiplied B, G, R, A
    int32 w, h;
    struct {
        uint32 target;
        ID2D1Bitmap* bmp;
    } slots[IMAGE_SLOTS];
    int32 next;   // the slot to reuse
};

static void* d2dImageUpload(const void* px, int32 w, int32 h)
{
    D2dImage* img = (D2dImage*)xaAlloc(sizeof(D2dImage), XA_Zero);
    img->px       = xaAlloc((size_t)w * h * 4);
    memcpy(img->px, px, (size_t)w * h * 4);
    img->w = w;
    img->h = h;
    return img;
}

static void d2dImageFree(void* ip)
{
    D2dImage* img = (D2dImage*)ip;
    for (int32 i = 0; i < IMAGE_SLOTS; i++)
        release(&img->slots[i].bmp);
    xaFree(img->px);
    xaFree(img);
}

static void d2dImage(void* tp, void* ip, CxUiRect src, CxUiRect dst)
{
    D2dTarget* t  = (D2dTarget*)tp;
    D2dImage* img = (D2dImage*)ip;
    if (!t->rt)
        return;
    ID2D1Bitmap* bmp = NULL;
    for (int32 i = 0; i < IMAGE_SLOTS && !bmp; i++) {
        if (img->slots[i].target == t->id)
            bmp = img->slots[i].bmp;
    }
    if (!bmp) {
        D2D1_BITMAP_PROPERTIES props =
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(t->rt->CreateBitmap(D2D1::SizeU(img->w, img->h), img->px, img->w * 4, &props, &bmp)))
            return;
        int32 s = img->next;
        img->next = (s + 1) % IMAGE_SLOTS;
        release(&img->slots[s].bmp);
        img->slots[s].target = t->id;
        img->slots[s].bmp    = bmp;
    }
    t->rt->DrawBitmap(bmp, rectF(dst), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, rectF(src));
}

extern "C" const CxUiDrawOps _cxuiWin32D2dOps = {
    _S"d2d",
    d2dTargetCreate,
    d2dTargetResize,
    d2dTargetDestroy,
    NULL,   // expandDirty
    d2dBegin,
    d2dEnd,
    d2dFill,
    d2dLine,
    d2dClipSet,
    d2dFontCreate,
    d2dFontDestroy,
    d2dFontMetrics,
    d2dFontAdvance,
    d2dText,
    d2dTextCells,
    d2dImageUpload,
    d2dImageFree,
    d2dImage,
    d2dTestRead,
};
