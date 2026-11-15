// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "core/cxuiimage.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cxuidraw_private.h"

static uint8 premul(uint8 c, uint8 a)
{
    return (uint8)((c * a + 127) / 255);
}

static CxUiImage* imageCreate(int32 w, int32 h, int32 stride, const uint8* px, int32 bpp)
{
    if (w <= 0 || h <= 0 || !px)
        return NULL;
    CxUiImage* img = objInstCreate(CxUiImage);
    img->_rect     = (CxUiRect){ 0, 0, w, h };
    objInstInit(img);

    uint8* out   = xaAlloc((size_t)w * h * 4);
    img->_pixels = out;
    for (int32 y = 0; y < h; y++) {
        const uint8* src = px + (size_t)y * stride;
        uint8* dst       = out + (size_t)y * w * 4;
        for (int32 x = 0; x < w; x++, src += bpp, dst += 4) {
            uint8 a = bpp == 4 ? src[3] : 255;
            dst[0]  = premul(src[2], a);
            dst[1]  = premul(src[1], a);
            dst[2]  = premul(src[0], a);
            dst[3]  = a;
        }
    }
    return img;
}

_Use_decl_annotations_
CxUiImage* cxuiImageCreateRGBA(int32 w, int32 h, int32 stride, const uint8* px)
{
    return imageCreate(w, h, stride, px, 4);
}

_Use_decl_annotations_
CxUiImage* cxuiImageCreateRGB(int32 w, int32 h, int32 stride, const uint8* px)
{
    return imageCreate(w, h, stride, px, 3);
}

_Use_decl_annotations_
CxUiImage* cxuiImageCreateSub(CxUiImage* img, CxUiRect r)
{
    r = _cxuiRectIntersect((CxUiRect){ img->_rect.x + r.x, img->_rect.y + r.y, r.w, r.h }, img->_rect);
    if (_cxuiRectEmpty(r))
        return NULL;
    CxUiImage* sub = objInstCreate(CxUiImage);
    sub->_root     = objAcquire(img->_root ? img->_root : img);
    sub->_rect     = r;
    objInstInit(sub);
    return sub;
}

_Use_decl_annotations_
CxUiSize cxuiImageSize(CxUiImage* img)
{
    return (CxUiSize){ img->_rect.w, img->_rect.h };
}

_Use_decl_annotations_
void* _cxuiImageNative(CxUiImage* root, const CxUiDrawOps* ops)
{
    if (ops != root->_cacheOps) {
        if (root->_cache)
            ((const CxUiDrawOps*)root->_cacheOps)->imageFree(root->_cache);
        root->_cacheOps = (void*)ops;
        root->_cache    = ops->imageUpload(root->_pixels, root->_rect.w, root->_rect.h);
    }
    return root->_cache;
}

_Use_decl_annotations_
const uint8* _cxuiImageRows(CxUiImage* img, int32* stride)
{
    CxUiImage* root = img->_root ? img->_root : img;
    *stride         = root->_rect.w * 4;
    return (const uint8*)root->_pixels + (size_t)img->_rect.y * *stride + (size_t)img->_rect.x * 4;
}

_objfactory_guaranteed CxUiCursor* CxUiCursor_create(_In_ CxUiImage* img, int32 hotX, int32 hotY)
{
    CxUiCursor* self;
    self = objInstCreate(CxUiCursor);
    self->_image = objAcquire(img);
    CxUiSize sz  = cxuiImageSize(img);
    self->_hot   = (CxUiPoint){ clamp(hotX, 0, sz.w - 1), clamp(hotY, 0, sz.h - 1) };
    objInstInit(self);
    return self;
}

void CxUiImage_destroy(_In_ CxUiImage* self)
{
    // An engine's copy may be freed on any thread.
    if (self->_cache)
        ((const CxUiDrawOps*)self->_cacheOps)->imageFree(self->_cache);
    xaFree(self->_pixels);
    // Autogen begins -----
    objRelease(&self->_root);
    // Autogen ends -------
}

void CxUiCursor_destroy(_In_ CxUiCursor* self)
{
    // Autogen begins -----
    objRelease(&self->_image);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "core/cxuiimage.auto.inc"
// clang-format on
// Autogen ends -------
