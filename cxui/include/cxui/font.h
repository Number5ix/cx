#pragma once

/// @file font.h
/// @brief Fonts and measuring text

#include <cxui/cxui_shared.h>

CX_C_BEGIN

/// @defgroup cxui_font Fonts
/// @ingroup cxui
/// @{
/// A CxUiFont is made once and then used for measuring and drawing as often as needed; nothing
/// is looked up again per call.
///
/// - **A font has one scale.** Make fonts for the scale of the canvas they draw on, with
///   cxuiFontCreateFor(). When a window's scale changes (cxuiOnDpiChanged()), make them again,
///   or use cxuiFontRescale().
/// - **Measuring matches drawing.** cxuiFontAdvance() returns exactly what cxuiDrawText() does,
///   and works outside paint handlers, so layout can be done ahead of time.
/// - **Sizes and metrics are physical pixels**, whatever units a canvas uses.
/// - Fonts are objects: release them with objRelease(), from any thread. Make and measure them
///   on the UI thread.
///
/// @code
///   CxUiFont* mono = cxuiFontCreateFor(cv, &(CxUiFontDesc){ .faces = _S"Consolas;monospace", .size = 9 });
///   CxUiFontMetrics m;
///   cxuiFontMetrics(mono, &m);
///   int32 rowHeight = m.height;
/// @endcode

/// Flags for CxUiFontDesc
enum CxUiFontFlags {
    CXUI_FONT_Bold      = 0x0001,
    CXUI_FONT_Italic    = 0x0002,
    CXUI_FONT_Underline = 0x0004,
};

/// @}  // end of cxui_font group

CX_C_END

#include <cxui/core/cxuifont.h>

CX_C_BEGIN

/// @addtogroup cxui_font
/// @{

/// Makes a font for a scale
///
/// @param desc What to ask for, or NULL for the system's interface font
/// @param scale Physical pixels per device-independent pixel, as from cxuiWidgetScale()
/// @return The font; release it with objRelease()
///
/// Example:
/// @code
///   CxUiFont* f = cxuiFontCreate(&(CxUiFontDesc){ .faces = _S"serif", .size = 12 }, 1.5f);
/// @endcode
_Ret_valid_ CxUiFont* cxuiFontCreate(_In_opt_ const CxUiFontDesc* desc, float32 scale);

// Internal: use cxuiFontCreateFor()
_Ret_valid_ CxUiFont* _cxuiFontCreateFor(_In_opt_ CxUiWidget* w, _In_opt_ const CxUiFontDesc* desc);

/// CxUiFont* cxuiFontCreateFor(CxUiWidget* w, const CxUiFontDesc* desc)
///
/// Makes a font at a widget's scale, for the engine its canvas draws with
///
/// @param w The widget, usually the canvas the font will draw on
/// @param ... (desc) What to ask for, or NULL for the system's interface font
/// @return The font; release it with objRelease()
#define cxuiFontCreateFor(w, ...) _cxuiFontCreateFor(CxUiWidget(w), __VA_ARGS__)

/// void cxuiWidgetSetFont(CxUiWidget* w, const CxUiFontDesc* desc)
///
/// Sets the font a widget and everything inside it use
///
/// Character units (cxuiChars()) are measured in it, so the widgets inside are laid out again.
/// cxuiWidgetFont() gives the font, made at the widget's scale.
///
/// @param w Any widget
/// @param ... (desc) The font, or NULL to use the container's (at the top, the system's interface font)
///
/// Example:
/// @code
///   cxuiWidgetSetFont(log, &(CxUiFontDesc){ .faces = _S"Consolas;monospace", .size = 9 });
/// @endcode
#define cxuiWidgetSetFont(w, ...) CxUiWidget_setFontWith(CxUiWidget(w), __VA_ARGS__)

/// Makes the same font at another scale
///
/// @param f A font
/// @param scale The new scale
/// @return A new font; release it with objRelease()
_Ret_valid_ CxUiFont* cxuiFontRescale(_In_ CxUiFont* f, float32 scale);

/// Gets a font's measurements
/// @param f The font
/// @param out Receives the metrics, in physical pixels
void cxuiFontMetrics(_In_ CxUiFont* f, _Out_ CxUiFontMetrics* out);

/// Gets a font's scale
/// @param f The font
/// @return Physical pixels per device-independent pixel
float32 cxuiFontScale(_In_ CxUiFont* f);

/// Measures a single line of text
///
/// @param f The font
/// @param text The text; it shouldn't contain line breaks
/// @return Its width in physical pixels, the same as cxuiDrawText() returns
int32 cxuiFontAdvance(_In_ CxUiFont* f, _In_opt_ strref text);

/// Measures text that may have several lines, split at '\\n'
///
/// @param f The font
/// @param text The text
/// @return The widest line's width, and the line height times the number of lines, in physical
/// pixels; empty text is one line tall and 0 wide
CxUiSize cxuiFontMeasure(_In_ CxUiFont* f, _In_opt_ strref text);

/// @}  // end of cxui_font group

CX_C_END
