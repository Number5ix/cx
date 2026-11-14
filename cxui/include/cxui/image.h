#pragma once

/// @file image.h
/// @brief Images to draw on canvases

#include <cxui/cxui_shared.h>

/// @defgroup cxui_image Images
/// @ingroup cxui
/// @{
/// A CxUiImage holds its own copy of the pixels it was made from, so the caller's buffer can be
/// freed at once. Draw one with cxuiDrawImage().
///
/// - **Pixels come in as bytes R, G, B, A** (or R, G, B), with straight alpha, top row first, and
///   any distance between rows.
/// - **Parts of an image** (cxuiImageCreateSub()) share its pixels, so cutting an atlas into many
///   pieces copies nothing.
/// - Images are objects: release them with objRelease(), from any thread. Each drawing engine
///   makes its own copy the first time the image is drawn.
///
/// @code
///   CxUiImage* atlas = cxuiImageCreateRGBA(256, 256, 256 * 4, pixels);
///   CxUiImage* icon  = cxuiImageCreateSub(atlas, (CxUiRect){ 32, 0, 16, 16 });
///   cxuiDrawImage(d, icon, NULL, (CxUiRect){ x, y, 16, 16 });
/// @endcode

#include <cxui/core/cxuiimage.h>

CX_C_BEGIN

/// Makes an image from pixels with alpha
///
/// @param w Width
/// @param h Height
/// @param stride Bytes from the start of one row to the next
/// @param px Bytes R, G, B, A per pixel, alpha not premultiplied
/// @return The image, or NULL if the size isn't positive; release it with objRelease()
_Ret_maybenull_ CxUiImage* cxuiImageCreateRGBA(int32 w, int32 h, int32 stride, _In_reads_bytes_(h * stride) const uint8* px);

/// Makes an opaque image from pixels without alpha
///
/// @param w Width
/// @param h Height
/// @param stride Bytes from the start of one row to the next
/// @param px Bytes R, G, B per pixel
/// @return The image, or NULL if the size isn't positive; release it with objRelease()
_Ret_maybenull_ CxUiImage* cxuiImageCreateRGB(int32 w, int32 h, int32 stride, _In_reads_bytes_(h * stride) const uint8* px);

/// Makes an image of part of another, sharing its pixels
///
/// @param img The image to take part of; it may itself be a part
/// @param r The part, within img; clipped to it
/// @return The new image, or NULL if r is empty after clipping; release it with objRelease()
_Ret_maybenull_ CxUiImage* cxuiImageCreateSub(_In_ CxUiImage* img, CxUiRect r);

/// Gets an image's size
/// @param img The image
/// @return Its size in pixels
CxUiSize cxuiImageSize(_In_ CxUiImage* img);

CX_C_END

/// @}  // end of cxui_image group
