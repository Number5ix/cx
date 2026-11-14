#pragma once

/// @file theme.h
/// @brief Light and dark themes, and the colors drawn widgets use

#include <cxui/cxui_shared.h>
#include <cxui/core/cxuipalette.h>

CX_C_BEGIN

/// @defgroup cxui_theme Themes and palettes
/// @ingroup cxui
/// @{
/// Each window is light or dark: its theme mode (cxuiWindowSetThemeMode()) is Light, Dark, or
/// System, which follows the system's setting and changes with it. The native parts of the window
/// follow its mode where the platform allows.
///
/// Widgets that cxui draws itself, and canvases that want to match them, take their colors from a
/// **palette**: one color for each CxUiPaletteRole.
///
/// - **Palettes are inherited.** A widget uses its own palette (cxuiWidgetSetPalette()), else its
///   nearest container's, else its window's built-in light or dark palette, as the window's theme
///   resolves. cxuiWidgetPalette() gives the one a widget uses.
/// - **A window with no palette of its own switches built-in palettes** when its theme changes.
///   One with its own keeps it; to follow the theme with custom colors, set another palette in its
///   cxuiOnThemeChanged() handler.
/// - **Backgrounds:** a window, and the panels and frames on it, are CXUI_PAL_Panel, the color of
///   the platform's dialogs (on Windows the light palette takes it from the system); grids, lists
///   and canvases are CXUI_PAL_Content. A panel or scroll box shows another with its `bgRole` or a
///   color of its own (cxuiPanelSetBgRole(), cxuiPanelSetBg()), and everything inside it that
///   shows the background around it follows; a palette of the window's own changes them all.
/// - **A palette in use can't change.** Make a new one with cxuiPaletteCreate() or
///   cxuiPaletteClone(), set its colors, and give it to the widget; whatever uses it is painted
///   again.
///
/// @code
///   CxUiPalette* pal = cxuiPaletteClone(dark);
///   cxuiPaletteSetColor(pal, CXUI_PAL_Accent, cxuiHex(0xE08030));
///   cxuiWidgetSetPalette(win, pal);   // the window holds its own reference
///   objRelease(&pal);
/// @endcode

/// Gets the built-in light palette
/// @return The palette, with a reference the caller owns
_Ret_valid_ CxUiPalette* cxuiPaletteLight(void);

/// Gets the built-in dark palette
/// @return The palette, with a reference the caller owns
_Ret_valid_ CxUiPalette* cxuiPaletteDark(void);

/// Gets the system's light or dark setting
///
/// UI thread only.
///
/// @return CXUI_Theme_Light or CXUI_Theme_Dark
CxUiThemeMode cxuiSystemTheme(void);

/// @}  // end of cxui_theme group

CX_C_END
