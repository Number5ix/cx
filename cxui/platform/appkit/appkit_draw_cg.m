#include "appkit_ui.h"

#include <CoreText/CoreText.h>

// A canvas's back buffer is a bitmap context, premultiplied B, G, R, A in memory, with the
// transform flipped so y grows downwards from the top left, as everywhere else in cxui. Text and
// images draw bottom up, so each run of text and each image flips the transform back locally.

#define BUFFER_SLACK 64

typedef struct CgTarget {
    CGContextRef ctx;
    void* bits;
    CxUiSize size;    // in use
    CxUiSize alloc;   // of the bitmap, with slack
    bool clipped;     // a clip's graphics state is saved on top of the pass's
} CgTarget;

static int32 withSlack(int32 v)
{
    return (v + BUFFER_SLACK - 1) / BUFFER_SLACK * BUFFER_SLACK;
}

static CGColorSpaceRef srgb(void)
{
    static CGColorSpaceRef cs;
    if (!cs)
        cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    return cs;
}

static void* cgTargetCreate(CxUiCanvasPeer* p)
{
    unused_noeval(p);
    return xaAllocStruct(CgTarget, XA_Zero);
}

// Grows with slack, and is made smaller once it is more than twice what is needed both ways. The
// contents are lost when it is made again, which a new size repaints anyway.
static void cgTargetResize(void* tp, CxUiSize px)
{
    CgTarget* t = tp;
    t->size     = px;
    bool grow   = px.w > t->alloc.w || px.h > t->alloc.h;
    bool shrink = px.w * 2 < t->alloc.w && px.h * 2 < t->alloc.h;
    if (!grow && !shrink && t->ctx)
        return;
    if (t->ctx)
        CGContextRelease(t->ctx);
    xaFree(t->bits);
    t->alloc = (CxUiSize){ withSlack(max(px.w, 1)), withSlack(max(px.h, 1)) };
    t->bits  = xaAlloc((size_t)t->alloc.w * t->alloc.h * 4, XA_Zero);
    t->ctx   = CGBitmapContextCreate(t->bits, t->alloc.w, t->alloc.h, 8, (size_t)t->alloc.w * 4, srgb(),
                                     kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    // Top left origin, in pixels
    CGContextTranslateCTM(t->ctx, 0, t->alloc.h);
    CGContextScaleCTM(t->ctx, 1, -1);
    CGContextSetTextMatrix(t->ctx, CGAffineTransformIdentity);
}

static void cgTargetDestroy(void* tp)
{
    CgTarget* t = tp;
    if (t->ctx)
        CGContextRelease(t->ctx);
    xaFree(t->bits);
    xaFree(t);
}

static CGRect cgRect(CxUiRect r)
{
    return CGRectMake(r.x, r.y, r.w, r.h);
}

static void setFill(CGContextRef ctx, CxUiColor c)
{
    CGContextSetRGBFillColor(ctx, c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0);
}

// Core Graphics only ever narrows a clip, so the pass's state is saved and each clip is a fresh
// state on top of it.
static void cgBegin(void* tp, CxUiRect dirty)
{
    CgTarget* t = tp;
    if (!t->ctx)
        return;
    CGContextSaveGState(t->ctx);
    CGContextClipToRect(t->ctx, cgRect(dirty));
    t->clipped = false;
}

static void cgEnd(void* tp)
{
    CgTarget* t = tp;
    if (!t->ctx)
        return;
    if (t->clipped)
        CGContextRestoreGState(t->ctx);
    CGContextRestoreGState(t->ctx);
    CGContextFlush(t->ctx);
}

static void cgClipSet(void* tp, CxUiRect r)
{
    CgTarget* t = tp;
    if (!t->ctx)
        return;
    if (t->clipped)
        CGContextRestoreGState(t->ctx);
    CGContextSaveGState(t->ctx);
    CGContextClipToRect(t->ctx, cgRect(r));
    t->clipped = true;
}

static void cgFill(void* tp, CxUiRect r, CxUiColor c)
{
    CgTarget* t = tp;
    if (!t->ctx)
        return;
    CGContextSetShouldAntialias(t->ctx, false);
    setFill(t->ctx, c);
    CGContextFillRect(t->ctx, cgRect(r));
}

static void cgLine(void* tp, CxUiPoint a, CxUiPoint b, int32 width, CxUiColor c)
{
    CgTarget* t = tp;
    if (!t->ctx)
        return;
    CGContextSetShouldAntialias(t->ctx, true);
    CGContextSetRGBStrokeColor(t->ctx, c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0);
    CGContextSetLineWidth(t->ctx, width);
    CGContextSetLineCap(t->ctx, kCGLineCapSquare);
    CGContextBeginPath(t->ctx);
    CGContextMoveToPoint(t->ctx, a.x + 0.5, a.y + 0.5);
    CGContextAddLineToPoint(t->ctx, b.x + 0.5, b.y + 0.5);
    CGContextStrokePath(t->ctx);
}

static void cgTestRead(void* tp, uint8* rgba)
{
    CgTarget* t = tp;
    if (!t->ctx)
        return;
    CGContextFlush(t->ctx);
    // Rows run top down in memory, the bitmap's own order.
    for (int32 y = 0; y < t->size.h; y++) {
        const uint8* src = (const uint8*)t->bits + (size_t)y * t->alloc.w * 4;
        uint8* dst       = rgba + (size_t)y * t->size.w * 4;
        for (int32 x = 0; x < t->size.w; x++) {
            dst[x * 4]     = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4];
            dst[x * 4 + 3] = 255;
        }
    }
}

_Use_decl_annotations_
CGImageRef _cxuiAppKitTargetImage(void* tp)
{
    CgTarget* t = tp;
    return t->ctx ? CGBitmapContextCreateImage(t->ctx) : NULL;
}

_Use_decl_annotations_
CxUiSize _cxuiAppKitTargetAlloc(void* tp)
{
    return ((CgTarget*)tp)->alloc;
}

// ---- fonts ----

// Glyph and rounded advance of a codepoint in the font itself, filled in on first use, in pages
// of 256 below 0x10000. Glyph 0 means the font lacks it, and the run goes through a CTLine,
// which finds a font that has it.
typedef struct CgGlyph {
    CGGlyph glyph;
    int16 advance;
    bool known;
} CgGlyph;

typedef struct CgFont {
    CTFontRef font;
    CxUiFontMetrics m;
    CgGlyph* pages[256];
} CgFont;

static CgGlyph lookupGlyph(CgFont* f, uint32 cp)
{
    CgGlyph none = { 0 };
    if (cp >= 0x10000)
        return none;
    CgGlyph** page = &f->pages[cp >> 8];
    if (!*page)
        *page = xaAlloc(sizeof(CgGlyph) * 256, XA_Zero);
    CgGlyph* g = &(*page)[cp & 0xFF];
    if (g->known)
        return *g;
    UniChar ch    = (UniChar)cp;
    CGGlyph glyph = 0;
    if (CTFontGetGlyphsForCharacters(f->font, &ch, &glyph, 1) && glyph) {
        CGSize adv;
        CTFontGetAdvancesForGlyphs(f->font, kCTFontOrientationHorizontal, &glyph, &adv, 1);
        g->glyph   = glyph;
        g->advance = (int16)floor(adv.width + 0.5);
    }
    g->known = true;
    return *g;
}

// Codepoints that need shaping in context: combining marks, the scripts from Hebrew to
// Mongolian, direction and joiner controls, variation selectors, and anything above U+FFFF.
static bool needsShaping(uint32 cp)
{
    return (cp >= 0x0300 && cp < 0x0370) || (cp >= 0x0590 && cp < 0x1E00) || (cp >= 0x200B && cp <= 0x200F) ||
           (cp >= 0x2028 && cp <= 0x202E) || (cp >= 0xFE00 && cp < 0xFE10) || cp >= 0x10000;
}

// A run's glyphs, if every codepoint is in the font and needs no context
typedef struct Simple {
    CGGlyph* glyphs;
    int32* advances;
    int32 n;
    CGGlyph gbuf[128];
    int32 abuf[128];
} Simple;

static bool simpleInit(CgFont* f, strref s, Simple* r)
{
    int32 count = _cxuiCodepoints(s);
    r->n        = 0;
    r->glyphs   = count <= 128 ? r->gbuf : xaAlloc(sizeof(CGGlyph) * count);
    r->advances = count <= 128 ? r->abuf : xaAlloc(sizeof(int32) * count);
    striter it;
    int32 cp;
    striBorrow(&it, s);
    while (r->n < count && striU8Char(&it, &cp)) {
        CgGlyph g = needsShaping((uint32)cp) ? (CgGlyph){ 0 } : lookupGlyph(f, (uint32)cp);
        if (!g.glyph)
            break;
        r->glyphs[r->n]     = g.glyph;
        r->advances[r->n++] = g.advance;
    }
    striFinish(&it);
    return r->n == count;
}

static void simpleFree(Simple* r)
{
    if (r->glyphs != r->gbuf) {
        xaFree(r->glyphs);
        xaFree(r->advances);
    }
}

static NSString* nsString(strref s)
{
    return [[NSString alloc] initWithBytes:strC(s) length:strLen(s) encoding:NSUTF8StringEncoding];
}

// A line laid out by Core Text, for runs the font can't draw alone
static CTLineRef makeLine(CgFont* f, NSString* s)
{
    NSDictionary* attrs = @{
        (__bridge id)kCTFontAttributeName : (__bridge id)f->font,
        (__bridge id)kCTForegroundColorFromContextAttributeName : @YES,
    };
    NSAttributedString* as = [[NSAttributedString alloc] initWithString:s ?: @"" attributes:attrs];
    return CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)as);
}

static int32 lineAdvance(CTLineRef line)
{
    return (int32)floor(CTLineGetTypographicBounds(line, NULL, NULL, NULL) + 0.5);
}

static CTFontRef fontNamed(NSString* family, CGFloat px)
{
    CTFontRef font = CTFontCreateWithName((__bridge CFStringRef)family, px, NULL);
    // Core Text substitutes a font for a name it doesn't have; that counts as not found.
    NSString* got  = CFBridgingRelease(CTFontCopyFamilyName(font));
    if ([got caseInsensitiveCompare:family] == NSOrderedSame)
        return font;
    CFRelease(font);
    return NULL;
}

static void* cgFontCreate(strref faces, float32 px, float32 scale, uint32 flags)
{
    if (px <= 0)
        px = (float32)(NSFont.systemFontSize * scale);
    CTFontRef font = NULL;
    string face    = 0;
    int32 len      = strLen(faces);
    for (int32 start = 0; start <= len && !font;) {
        int32 end = strFindChar(faces, start, ';');
        if (end < 0)
            end = len;
        strSubStr(&face, faces, start, end);
        strTrim(&face, face, 0);
        start = end + 1;
        if (strEmpty(face))
            continue;
        if (strEqi(face, _S"ui")) {
            font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, px, NULL);
        } else if (strEqi(face, _S"sans")) {
            font = fontNamed(@"Helvetica Neue", px);
        } else if (strEqi(face, _S"serif")) {
            font = fontNamed(@"Times New Roman", px);
            if (!font)
                font = fontNamed(@"Times", px);
        } else if (strEqi(face, _S"monospace")) {
            font = fontNamed(@"Menlo", px);
        } else {
            font = fontNamed(nsString(face), px);
        }
    }
    strDestroy(&face);
    if (!font)
        font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, px, NULL);

    CTFontSymbolicTraits traits = ((flags & CXUI_FONT_Bold) ? kCTFontTraitBold : 0) |
                                  ((flags & CXUI_FONT_Italic) ? kCTFontTraitItalic : 0);
    if (traits) {
        CTFontRef styled = CTFontCreateCopyWithSymbolicTraits(font, px, NULL, traits, traits);
        if (styled) {
            CFRelease(font);
            font = styled;
        }
    }

    CgFont* f    = xaAllocStruct(CgFont, XA_Zero);
    f->font      = font;
    f->m.ascent  = (int32)ceil(CTFontGetAscent(font));
    f->m.descent = (int32)ceil(CTFontGetDescent(font));
    f->m.height  = f->m.ascent + f->m.descent;
    f->m.avgCharW = max(lookupGlyph(f, 'x').advance, 1);
    f->m.cellW    = max(lookupGlyph(f, '0').advance, 1);
    f->m.cellH    = f->m.height;
    return f;
}

_Use_decl_annotations_
CTFontRef _cxuiAppKitFontAtPoints(CxUiFont* f)
{
    CgFont* cg    = _cxuiFontNative(f, &_cxuiAppKitCgOps);
    float32 scale = cxuiFontScale(f);
    return CTFontCreateCopyWithAttributes(cg->font, CTFontGetSize(cg->font) / (scale > 0 ? scale : 1), NULL, NULL);
}

static void cgFontDestroy(void* fp)
{
    CgFont* f = fp;
    for (int32 i = 0; i < 256; i++)
        xaFree(f->pages[i]);
    CFRelease(f->font);
    xaFree(f);
}

static void cgFontMetrics(void* fp, CxUiFontMetrics* out)
{
    *out = ((CgFont*)fp)->m;
}

static int32 cgFontAdvance(void* fp, strref s)
{
    CgFont* f = fp;
    Simple r;
    if (simpleInit(f, s, &r)) {
        int32 adv = 0;
        for (int32 i = 0; i < r.n; i++)
            adv += r.advances[i];
        simpleFree(&r);
        return adv;
    }
    simpleFree(&r);
    @autoreleasepool {
        CTLineRef line = makeLine(f, nsString(s));
        int32 adv      = lineAdvance(line);
        CFRelease(line);
        return adv;
    }
}

// Upright from the baseline at x: positions and glyphs share one space, unflipped.
static void baselineSpace(CgTarget* t, CgFont* f, CGFloat x, CGFloat y)
{
    CGContextSaveGState(t->ctx);
    CGContextTranslateCTM(t->ctx, x, y + f->m.ascent);
    CGContextScaleCTM(t->ctx, 1, -1);
}

static void drawGlyphs(CgTarget* t, CgFont* f, const CGGlyph* glyphs, const int32* advances, int32 n, CxUiPoint at)
{
    CGPoint buf[128];
    CGPoint* pos = n <= 128 ? buf : xaAlloc(sizeof(CGPoint) * n);
    CGFloat x    = 0;
    for (int32 i = 0; i < n; i++) {
        pos[i] = CGPointMake(x, 0);
        x += advances[i];
    }
    baselineSpace(t, f, at.x, at.y);
    CTFontDrawGlyphs(f->font, glyphs, pos, n, t->ctx);
    CGContextRestoreGState(t->ctx);
    if (pos != buf)
        xaFree(pos);
}

static void drawLine(CgTarget* t, CgFont* f, CTLineRef line, CGFloat x, CGFloat y)
{
    baselineSpace(t, f, x, y);
    CGContextSetTextPosition(t->ctx, 0, 0);
    CTLineDraw(line, t->ctx);
    CGContextRestoreGState(t->ctx);
}

static int32 cgText(void* tp, void* fp, CxUiPoint at, strref s, CxUiColor c)
{
    CgTarget* t = tp;
    CgFont* f   = fp;
    if (!t->ctx)
        return cgFontAdvance(fp, s);
    CGContextSetShouldAntialias(t->ctx, true);
    setFill(t->ctx, c);
    Simple r;
    if (simpleInit(f, s, &r)) {
        drawGlyphs(t, f, r.glyphs, r.advances, r.n, at);
        int32 adv = 0;
        for (int32 i = 0; i < r.n; i++)
            adv += r.advances[i];
        simpleFree(&r);
        return adv;
    }
    simpleFree(&r);
    @autoreleasepool {
        CTLineRef line = makeLine(f, nsString(s));
        drawLine(t, f, line, at.x, at.y);
        int32 adv = lineAdvance(line);
        CFRelease(line);
        return adv;
    }
}

// What the font has goes in runs of glyphs at the cells' positions; anything else is laid out
// alone at its cell, so a font that has it is found.
static void cgTextCells(void* tp, void* fp, CxUiPoint at, strref s, const int32* cellw, CxUiColor c)
{
    CgTarget* t = tp;
    CgFont* f   = fp;
    if (!t->ctx)
        return;
    CGContextSetShouldAntialias(t->ctx, true);
    setFill(t->ctx, c);
    int32 count     = _cxuiCodepoints(s);
    CGGlyph gbuf[128];
    int32 abuf[128];
    CGGlyph* glyphs = count <= 128 ? gbuf : xaAlloc(sizeof(CGGlyph) * count);
    int32* widths   = count <= 128 ? abuf : xaAlloc(sizeof(int32) * count);
    int32 n = 0, i = 0, x = at.x, runX = at.x, cp;
    striter it;
    striBorrow(&it, s);
    for (; i < count && striU8Char(&it, &cp); x += cellw[i], i++) {
        CgGlyph g = needsShaping((uint32)cp) ? (CgGlyph){ 0 } : lookupGlyph(f, (uint32)cp);
        if (g.glyph) {
            if (n == 0)
                runX = x;
            glyphs[n]   = g.glyph;
            widths[n++] = cellw[i];
            continue;
        }
        if (n > 0)
            drawGlyphs(t, f, glyphs, widths, n, (CxUiPoint){ runX, at.y });
        n = 0;
        @autoreleasepool {
            uint32 u      = (uint32)cp;
            NSString* one = [[NSString alloc] initWithBytes:&u length:4 encoding:NSUTF32LittleEndianStringEncoding];
            CTLineRef line = makeLine(f, one);
            drawLine(t, f, line, x, at.y);
            CFRelease(line);
        }
    }
    striFinish(&it);
    if (n > 0)
        drawGlyphs(t, f, glyphs, widths, n, (CxUiPoint){ runX, at.y });
    if (glyphs != gbuf) {
        xaFree(glyphs);
        xaFree(widths);
    }
}

// ---- images ----

static void releasePixels(void* info, const void* data, size_t size)
{
    unused_noeval(info);
    unused_noeval(size);
    xaFree((void*)data);
}

// A copy of the pixels, which the image owns: canvases may still hold it after the CxUiImage goes.
static void* cgImageUpload(const void* px, int32 w, int32 h)
{
    size_t bytes = (size_t)w * h * 4;
    void* copy   = xaAlloc(bytes);
    memcpy(copy, px, bytes);
    CGDataProviderRef dp = CGDataProviderCreateWithData(NULL, copy, bytes, releasePixels);
    CGImageRef img       = CGImageCreate(w, h, 8, 32, (size_t)w * 4, srgb(),
                                         kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little, dp, NULL, false,
                                         kCGRenderingIntentDefault);
    CGDataProviderRelease(dp);
    return (void*)img;
}

static void cgImageFree(void* img)
{
    CGImageRelease((CGImageRef)img);
}

static void cgImage(void* tp, void* ip, CxUiRect src, CxUiRect dst)
{
    CgTarget* t = tp;
    if (!t->ctx)
        return;
    CGImageRef part = CGImageCreateWithImageInRect((CGImageRef)ip, cgRect(src));
    if (!part)
        return;
    // Images draw bottom up; flip around the destination so it comes out the right way.
    CGContextSaveGState(t->ctx);
    CGContextSetShouldAntialias(t->ctx, false);
    CGContextSetInterpolationQuality(t->ctx, kCGInterpolationDefault);
    CGContextTranslateCTM(t->ctx, dst.x, dst.y + dst.h);
    CGContextScaleCTM(t->ctx, 1, -1);
    CGContextDrawImage(t->ctx, CGRectMake(0, 0, dst.w, dst.h), part);
    CGContextRestoreGState(t->ctx);
    CGImageRelease(part);
}

const CxUiDrawOps _cxuiAppKitCgOps = {
    .name          = _S"cg",
    .targetCreate  = cgTargetCreate,
    .targetResize  = cgTargetResize,
    .targetDestroy = cgTargetDestroy,
    .begin         = cgBegin,
    .end           = cgEnd,
    .fill          = cgFill,
    .line          = cgLine,
    .clipSet       = cgClipSet,
    .fontCreate    = cgFontCreate,
    .fontDestroy   = cgFontDestroy,
    .fontMetrics   = cgFontMetrics,
    .fontAdvance   = cgFontAdvance,
    .text          = cgText,
    .textCells     = cgTextCells,
    .imageUpload   = cgImageUpload,
    .imageFree     = cgImageFree,
    .image         = cgImage,
    .testRead      = cgTestRead,
};

const CxUiDrawOps* _cxuiPlatformDrawOps(uint32 which)
{
    unused_noeval(which);
    return &_cxuiAppKitCgOps;
}
