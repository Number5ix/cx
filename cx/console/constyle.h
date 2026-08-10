/// @file constyle.h
/// @brief Console text styling: colors, attributes, and styled writes

#pragma once

#include "console.h"

CX_C_BEGIN

/// @defgroup console_style Style
/// @ingroup console
/// @{

/// The stream's own default foreground/background color -- i.e. no color escape is emitted
/// for this half of a ConStyle.
#define CON_ColorDefault 0u

/// uint32 CON_Idx(uint8 n);
///
/// A palette color, 0-255. 0-15 are the standard + bright ANSI 16-color entries; 16-255 are
/// the xterm 256-color cube and grayscale ramp. Always representable exactly on a
/// CON_Color256 or CON_ColorTrue stream; downgraded to the nearest of the 16 base colors on a
/// CON_Color16 stream.
#define CON_Idx(n) (0x01000000u | (uint32)(uint8)(n))

/// uint32 CON_RGB(uint8 r, uint8 g, uint8 b);
///
/// A 24-bit RGB color. Downgraded to the nearest representable color on streams below
/// CON_ColorTrue.
#define CON_RGB(r, g, b) \
    (0x02000000u | ((uint32)(uint8)(r) << 16) | ((uint32)(uint8)(g) << 8) | (uint32)(uint8)(b))

/// @name Standard 16-color palette
/// @{
#define CON_Black         CON_Idx(0)
#define CON_Red           CON_Idx(1)
#define CON_Green         CON_Idx(2)
#define CON_Yellow        CON_Idx(3)
#define CON_Blue          CON_Idx(4)
#define CON_Magenta       CON_Idx(5)
#define CON_Cyan          CON_Idx(6)
#define CON_White         CON_Idx(7)
#define CON_BrightBlack   CON_Idx(8)
#define CON_BrightRed     CON_Idx(9)
#define CON_BrightGreen   CON_Idx(10)
#define CON_BrightYellow  CON_Idx(11)
#define CON_BrightBlue    CON_Idx(12)
#define CON_BrightMagenta CON_Idx(13)
#define CON_BrightCyan    CON_Idx(14)
#define CON_BrightWhite   CON_Idx(15)
/// @}

/// Text attribute flags for ConStyle::attr. Attributes unsupported by a stream (e.g. italic
/// on the Windows legacy console) are dropped silently rather than emitting garbage.
enum CON_ATTR_FLAGS {
    CON_Bold      = 0x01,
    CON_Dim       = 0x02,
    CON_Italic    = 0x04,
    CON_Underline = 0x08,
    CON_Blink     = 0x10,
    CON_Reverse   = 0x20,
    CON_Strike    = 0x40,
};

/// A text style: foreground color, background color, and attribute flags. Passed by value --
/// 12 bytes, no lifetime of its own.
typedef struct ConStyle {
    uint32 fg;      ///< CON_ColorDefault, CON_Idx(n), or CON_RGB(r,g,b)
    uint32 bg;      ///< CON_ColorDefault, CON_Idx(n), or CON_RGB(r,g,b)
    flags_t attr;   ///< Bitwise OR of CON_ATTR_FLAGS
} ConStyle;

/// ConStyle CONSTYLE(uint32 fg, flags_t attr);
///
/// Builds a ConStyle with a foreground color and attributes, default background.
#define CONSTYLE(fg, attr) ((ConStyle) { (fg), CON_ColorDefault, (attr) })

/// ConStyle CONSTYLE2(uint32 fg, uint32 bg, flags_t attr);
///
/// Builds a ConStyle with both a foreground and background color, plus attributes.
#define CONSTYLE2(fg, bg, attr) ((ConStyle) { (fg), (bg), (attr) })

/// Sets the stream's current style, downgrading colors and dropping unsupported attributes
/// according to the stream's capabilities. Stays in effect for subsequent unstyled writes
/// until the next conSetStyle()/conResetStyle() call.
/// @param con Stream to style
/// @param style Style to apply
void conSetStyle(_In_ ConStream* con, ConStyle style);

/// Restores a stream to its default (unstyled) appearance. Equivalent to
/// conSetStyle(con, CONSTYLE(CON_ColorDefault, 0)).
/// @param con Stream to reset
void conResetStyle(_In_ ConStream* con);

/// Retrieves the style most recently passed to conSetStyle() or conResetStyle() -- exactly as
/// requested, not the capability-downgraded form actually emitted.
/// @param con Stream to query
/// @param out Receives a copy of the current style
void conGetStyle(_In_ ConStream* con, _Out_ ConStyle* out);

/// Writes raw bytes with a style applied for the duration of the write, then restores
/// whatever style was active beforehand. Atomic with respect to other threads.
/// @param con Destination stream
/// @param style Style to apply for this write
/// @param buf Bytes to write
/// @param sz Number of bytes
/// @return true on success
bool conWriteS(_In_ ConStream* con, ConStyle style, _In_reads_bytes_(sz) const void* buf,
               size_t sz);

/// Writes a cx string with a style applied for the duration of the write, then restores
/// whatever style was active beforehand. Rope-shaped strings are walked chunk by chunk with
/// no flattening allocation.
/// @param con Destination stream
/// @param style Style to apply for this write
/// @param s String to write (NULL or empty writes nothing and returns true)
/// @return true on success
bool conPutsS(_In_ ConStream* con, ConStyle style, _In_opt_ strref s);

/// Writes a NUL-terminated C string with a style applied for the duration of the write, then
/// restores whatever style was active beforehand.
/// @param con Destination stream
/// @param style Style to apply for this write
/// @param sz String to write (NULL or empty writes nothing and returns true)
/// @return true on success
bool conPutszS(_In_ ConStream* con, ConStyle style, _In_opt_z_ const char* sz);

/// Writes a single Unicode code point with a style applied for the duration of the write,
/// then restores whatever style was active beforehand.
/// @param con Destination stream
/// @param style Style to apply for this write
/// @param codepoint Unicode code point to write
/// @return true on success
bool conPutcS(_In_ ConStream* con, ConStyle style, int32 codepoint);

/// bool conFmtS(ConStream *con, ConStyle style, strref fmt, ...);
///
/// Formats arguments with cx's type-safe formatter (see @ref string_format) and writes
/// the result with a style applied for the duration of the write, then restores whatever
/// style was active beforehand.
///
/// @param con Destination stream
/// @param style Style to apply for this write
/// @param fmt Format string
/// @return true if formatting and the write both succeeded
///
/// Example:
/// @code
///   conFmtS(conOut(), CONSTYLE(CON_Red, CON_Bold), _SL("error: ${string}"), stvar(string, msg));
/// @endcode
bool _conFmtS(_In_ ConStream* con, ConStyle style, _In_ strref fmt, int n, _In_ stvar* args);
#define conFmtS(con, style, fmt, ...) \
    _conFmtS(con, style, fmt, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ })

/// @}  // end of console_style group

CX_C_END
