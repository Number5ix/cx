#pragma once

/// @file draw.h
/// @brief Colors and drawing on a canvas

#include <cxui/cxui_shared.h>
#include <cxui/font.h>
#include <cxui/image.h>

CX_C_BEGIN

/// @defgroup cxui_draw Drawing
/// @ingroup cxui
/// @{
/// A canvas's paint handler draws through a CxUiDraw, which is only valid during that handler.
///
/// - **Coordinates are physical pixels**, from the canvas's top left, unless the canvas uses
///   CXUI_Units_Dip (see cxuiCanvasSetUnits()).
/// - **Rectangles are half-open:** a fill at (x, y) of size (w, h) covers x to x + w - 1 and y to
///   y + h - 1.
/// - **Only the dirty area is drawn.** Drawing starts clipped to it, and cxuiDrawClipPush()
///   narrows that further.
/// - **Horizontal and vertical lines and frames are exact**: whole pixels, never blended.
///   Lines at other angles are anti-aliased.
///
/// @code
///   static CxUiResult paint(stvlist* cvars, CxUiPaintEvent* ev)
///   {
///       CxUiSize sz = cxuiDrawSize(ev->draw);
///       cxuiDrawFill(ev->draw, 0, 0, sz.w, sz.h, cxuiHex(0x202020));
///       cxuiDrawFrame(ev->draw, 10, 10, 100, 40, 1, cxuiHex(0x808080));
///       return CXUI_Handled;
///   }
/// @endcode

/// A color from red, green, blue and alpha, each 0 to 255
_meta_inline CxUiColor cxuiRGBA(uint8 r, uint8 g, uint8 b, uint8 a)
{
    CxUiColor c = { { r, g, b, a } };
    return c;
}

/// An opaque color from red, green and blue, each 0 to 255
_meta_inline CxUiColor cxuiRGB(uint8 r, uint8 g, uint8 b)
{
    return cxuiRGBA(r, g, b, 255);
}

/// An opaque color from a hex value written 0xRRGGBB
_meta_inline CxUiColor cxuiHex(uint32 rgb)
{
    return cxuiRGBA((uint8)(rgb >> 16), (uint8)(rgb >> 8), (uint8)rgb, 255);
}

/// A color from a hex value written 0xRRGGBBAA
_meta_inline CxUiColor cxuiHexA(uint32 rgba)
{
    return cxuiRGBA((uint8)(rgba >> 24), (uint8)(rgba >> 16), (uint8)(rgba >> 8), (uint8)rgba);
}

/// CxUiColor CXUI_HEX(uint32 rgb)
///
/// An opaque color from a constant 0xRRGGBB, for initializing static tables
///
/// Example:
/// @code
///   static const CxUiColor levelColors[] = { CXUI_HEX(0x808080), CXUI_HEXA(0xFF404080) };
/// @endcode
#define CXUI_HEX(rgb) { { ((rgb) >> 16) & 0xFF, ((rgb) >> 8) & 0xFF, (rgb) & 0xFF, 0xFF } }

/// CxUiColor CXUI_HEXA(uint32 rgba)
///
/// A color from a constant 0xRRGGBBAA, for initializing static tables
#define CXUI_HEXA(rgba) { { ((rgba) >> 24) & 0xFF, ((rgba) >> 16) & 0xFF, ((rgba) >> 8) & 0xFF, (rgba) & 0xFF } }

/// Fills a rectangle
///
/// @param d The paint handler's draw context
/// @param x Left edge
/// @param y Top edge
/// @param w Width
/// @param h Height
/// @param c Color; one with alpha blends with what is already there
void cxuiDrawFill(_In_ CxUiDraw* d, int32 x, int32 y, int32 w, int32 h, CxUiColor c);

/// Outlines a rectangle, drawing inside it
///
/// @param d The paint handler's draw context
/// @param x Left edge
/// @param y Top edge
/// @param w Width
/// @param h Height
/// @param width Thickness of the outline
/// @param c Color
void cxuiDrawFrame(_In_ CxUiDraw* d, int32 x, int32 y, int32 w, int32 h, int32 width, CxUiColor c);

/// Draws a line, including both end points
///
/// A line more than one pixel thick is centered on the points, with any extra pixel below or to
/// the right.
///
/// @param d The paint handler's draw context
/// @param x1 Start
/// @param y1 Start
/// @param x2 End
/// @param y2 End
/// @param width Thickness
/// @param c Color
void cxuiDrawLine(_In_ CxUiDraw* d, int32 x1, int32 y1, int32 x2, int32 y2, int32 width, CxUiColor c);

/// How cxuiDrawTextBox() places text in its box: one horizontal and one vertical value
enum CxUiTextAlign {
    CXUI_TEXT_Left   = 0x00,
    CXUI_TEXT_Center = 0x01,
    CXUI_TEXT_Right  = 0x02,
    CXUI_TEXT_Top    = 0x00,
    CXUI_TEXT_Middle = 0x10,
    CXUI_TEXT_Bottom = 0x20,
};

/// Draws a line of text in one font and color
///
/// @param d The paint handler's draw context
/// @param f The font
/// @param x Left edge
/// @param y Top of the line; the baseline is the font's ascent below it
/// @param text The text; it shouldn't contain line breaks
/// @param c Color. On Windows with GDI, text is always opaque.
/// @return The text's width, as cxuiFontAdvance() measures it, in the canvas's units
int32 cxuiDrawText(_In_ CxUiDraw* d, _In_ CxUiFont* f, int32 x, int32 y, _In_opt_ strref text, CxUiColor c);

/// Draws text that may have several lines, aligned in a box and clipped to it
///
/// Lines are split at '\\n' and never wrapped.
///
/// @param d The paint handler's draw context
/// @param f The font
/// @param box Where to place the text
/// @param text The text
/// @param align CxUiTextAlign: a horizontal value or'd with a vertical one
/// @param c Color
void cxuiDrawTextBox(_In_ CxUiDraw* d, _In_ CxUiFont* f, CxUiRect box, _In_opt_ strref text, flags_t align,
                     CxUiColor c);

/// Draws text on a grid, one character per cell
///
/// Character n is drawn at x + n * cellw, whatever its own width, so text in any font keeps to a
/// grid of fixed cells, as in a terminal.
///
/// @param d The paint handler's draw context
/// @param f The font
/// @param x Left edge of the first cell
/// @param y Top of the line
/// @param text The text; it shouldn't contain line breaks
/// @param cellw Width of a cell
/// @param c Color
/// @return The number of cells drawn
int32 cxuiDrawTextCells(_In_ CxUiDraw* d, _In_ CxUiFont* f, int32 x, int32 y, _In_opt_ strref text, int32 cellw,
                        CxUiColor c);

/// Draws an image, or part of it, scaled to a rectangle
///
/// @param d The paint handler's draw context
/// @param img The image
/// @param src The part to draw, within img, or NULL for all of it
/// @param dst Where to draw it; scaled to fit with smoothing, except with GDI on Windows, which
/// picks the nearest pixels
void cxuiDrawImage(_In_ CxUiDraw* d, _In_ CxUiImage* img, _In_opt_ const CxUiRect* src, CxUiRect dst);

/// Narrows drawing to a rectangle, within the current clip
///
/// Each push must be matched by cxuiDrawClipPop(); any left over are popped when the paint handler
/// returns.
///
/// @param d The paint handler's draw context
/// @param r The rectangle
void cxuiDrawClipPush(_In_ CxUiDraw* d, CxUiRect r);

/// Undoes the most recent cxuiDrawClipPush()
/// @param d The paint handler's draw context
void cxuiDrawClipPop(_In_ CxUiDraw* d);

/// Gets the area being painted, as in the paint event
/// @param d The paint handler's draw context
/// @return The dirty rectangle
CxUiRect cxuiDrawDirty(_In_ CxUiDraw* d);

/// Gets the size of the canvas being painted
/// @param d The paint handler's draw context
/// @return The size
CxUiSize cxuiDrawSize(_In_ CxUiDraw* d);

/// Gets the canvas's scale
/// @param d The paint handler's draw context
/// @return Physical pixels per device-independent pixel
float32 cxuiDrawScale(_In_ CxUiDraw* d);

/// @}  // end of cxui_draw group

CX_C_END
