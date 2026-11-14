// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "core/cxuifont.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cxuidraw_private.h"

_Use_decl_annotations_
int32 _cxuiCodepoints(strref s)
{
    striter it;
    int32 n = 0, cp;
    striBorrow(&it, s);
    while (striU8Char(&it, &cp))
        n++;
    striFinish(&it);
    return n;
}

_Use_decl_annotations_
int32 _cxuiByteOffset(strref s, int32 n)
{
    int32 len = strLen(s), at = 0;
    for (; at < len && n > 0; n--) {
        at++;
        while (at < len && (strGetChar(s, at) & 0xC0) == 0x80)
            at++;
    }
    return at;
}

const CxUiDrawOps* _cxuiFontOps(uint32 which)
{
    if (_cxui.backend == &_cxuiNullBackend)
        return &_cxuiNullDrawOps;
    const CxUiDrawOps* ops = _cxuiPlatformDrawOps(which);
    return ops ? ops : &_cxuiNullDrawOps;
}

// The em size in pixels, or 0 for the system's size
static float32 emPixels(float32 size, float32 scale)
{
    if (size < 0)
        return -size;
    return size * scale * 96.0f / 72.0f;
}

static void* nativeFor(CxUiFont* f, const CxUiDrawOps* ops)
{
    return ops->fontCreate(f->_faces, emPixels(f->_size, f->_scale), f->_scale, f->_flags);
}

CxUiFont* _cxuiFontMake(const CxUiDrawOps* ops, strref faces, float32 size, uint32 flags, float32 scale)
{
    CxUiFont* f = objInstCreate(CxUiFont);
    strDup(&f->_faces, strEmpty(faces) ? _S"ui" : faces);
    f->_size  = size;
    f->_flags = flags;
    f->_scale = scale > 0 ? scale : 1.0f;
    objInstInit(f);

    f->_ops    = (void*)ops;
    f->_native = nativeFor(f, ops);
    ops->fontMetrics(f->_native, &f->_metrics);
    return f;
}

_Use_decl_annotations_
CxUiFont* cxuiFontCreate(const CxUiFontDesc* desc, float32 scale)
{
    return _cxuiFontMake(_cxuiFontOps(CXUI_Draw_Auto), desc ? desc->faces : NULL, desc ? desc->size : 0,
                    desc ? desc->flags : 0, scale);
}

_Use_decl_annotations_
CxUiFont* _cxuiFontCreateFor(CxUiWidget* w, const CxUiFontDesc* desc)
{
    CxUiCanvas* cv = w ? objDynCast(CxUiCanvas, w) : NULL;
    return _cxuiFontMake(_cxuiFontOps(cv ? cv->_drawBackend : CXUI_Draw_Auto), desc ? desc->faces : NULL,
                    desc ? desc->size : 0, desc ? desc->flags : 0, cxuiWidgetScale(w));
}

_Use_decl_annotations_
CxUiFont* cxuiFontRescale(CxUiFont* f, float32 scale)
{
    return _cxuiFontMake(f->_ops, f->_faces, f->_size, f->_flags, scale);
}

_Use_decl_annotations_
void* _cxuiFontNative(CxUiFont* f, const CxUiDrawOps* ops)
{
    if (ops == f->_ops)
        return f->_native;
    if (ops != f->_altOps) {
        if (f->_alt)
            ((const CxUiDrawOps*)f->_altOps)->fontDestroy(f->_alt);
        f->_altOps = (void*)ops;
        f->_alt    = nativeFor(f, ops);
    }
    return f->_alt;
}

_Use_decl_annotations_
void cxuiFontMetrics(CxUiFont* f, CxUiFontMetrics* out)
{
    *out = f->_metrics;
}

_Use_decl_annotations_
float32 cxuiFontScale(CxUiFont* f)
{
    return f->_scale;
}

_Use_decl_annotations_
int32 cxuiFontAdvance(CxUiFont* f, strref text)
{
    if (strEmpty(text))
        return 0;
    return ((const CxUiDrawOps*)f->_ops)->fontAdvance(f->_native, text);
}

_Use_decl_annotations_
CxUiSize cxuiFontMeasure(CxUiFont* f, strref text)
{
    CxUiSize sz = { 0, f->_metrics.height };
    int32 len   = strLen(text);
    string line = 0;
    for (int32 start = 0;;) {
        int32 nl = strFindChar(text, start, '\n');
        strSubStr(&line, text, start, nl < 0 ? len : nl);
        sz.w = max(sz.w, cxuiFontAdvance(f, line));
        if (nl < 0)
            break;
        sz.h += f->_metrics.height;
        start = nl + 1;
    }
    strDestroy(&line);
    return sz;
}

void CxUiFont_destroy(_In_ CxUiFont* self)
{
    // Native fonts may be freed on any thread.
    if (self->_native)
        ((const CxUiDrawOps*)self->_ops)->fontDestroy(self->_native);
    if (self->_alt)
        ((const CxUiDrawOps*)self->_altOps)->fontDestroy(self->_alt);
    // Autogen begins -----
    strDestroy(&self->_faces);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "core/cxuifont.auto.inc"
// clang-format on
// Autogen ends -------
