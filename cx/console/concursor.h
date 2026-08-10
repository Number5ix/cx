/// @file concursor.h
/// @brief Cursor positioning and screen erase/scroll operations

#pragma once

#include "console.h"

CX_C_BEGIN

/// @defgroup console_cursor Cursor and Screen
/// @ingroup console
/// @{

/// Which portion of a line or screen to erase, relative to the current cursor position.
typedef enum ConEraseMode {
    CON_EraseToEnd,     ///< From the cursor to the end of the line/screen
    CON_EraseToStart,   ///< From the start of the line/screen to the cursor
    CON_EraseAll,       ///< The entire line/screen
} ConEraseMode;

/// Moves the cursor to an absolute position.
///
/// A no-op returning false if the stream has no cursor capability (see ConCaps::cursor).
///
/// @param con Destination stream
/// @param row 0-based row
/// @param col 0-based column
/// @return true on success
bool conSetCursor(_In_ ConStream* con, uint16 row, uint16 col);

/// Moves the cursor relative to its current position. Positive drow moves down, positive
/// dcol moves right; either may be negative.
///
/// On a stream without ConCaps::cursorquery (most VT terminals), this still works even
/// though conGetCursor() would fail -- moving relative to the current position doesn't
/// require knowing what that position is.
///
/// @param con Destination stream
/// @param drow Rows to move (negative moves up)
/// @param dcol Columns to move (negative moves left)
/// @return true on success
bool conMoveCursor(_In_ ConStream* con, int16 drow, int16 dcol);

/// Reports the cursor's current absolute position.
///
/// Only available when ConCaps::cursorquery is true -- querying position under VT would
/// require sending a DSR request and reading the reply from stdin, racing with any other
/// input consumer and potentially hanging against a terminal that never answers, so this
/// module never attempts it. Use conSaveCursor()/conRestoreCursor() instead, which is what
/// most callers actually need and works under VT too.
///
/// @param con Stream to query
/// @param row Receives the 0-based row
/// @param col Receives the 0-based column
/// @return true on success; false if the stream cannot report its position
bool conGetCursor(_In_ ConStream* con, _Out_ uint16* row, _Out_ uint16* col);

/// Shows or hides the cursor.
/// @param con Destination stream
/// @param show true to show, false to hide
/// @return true on success
bool conShowCursor(_In_ ConStream* con, bool show);

/// Saves the current cursor position for a later conRestoreCursor().
///
/// Only one position is remembered at a time; a second conSaveCursor() before a matching
/// conRestoreCursor() overwrites the first.
/// @param con Stream whose cursor position to save
/// @return true on success
bool conSaveCursor(_In_ ConStream* con);

/// Restores the cursor position most recently saved with conSaveCursor(). A no-op returning
/// false if nothing has been saved.
/// @param con Stream to restore
/// @return true on success
bool conRestoreCursor(_In_ ConStream* con);

/// Erases part or all of the current line. The cursor position does not change.
/// @param con Destination stream
/// @param mode Which portion of the line to erase
/// @return true on success
bool conEraseLine(_In_ ConStream* con, ConEraseMode mode);

/// Erases part or all of the screen. The cursor position does not change.
/// @param con Destination stream
/// @param mode Which portion of the screen to erase
/// @return true on success
bool conEraseScreen(_In_ ConStream* con, ConEraseMode mode);

/// Scrolls the screen content. Positive lines scrolls up, revealing blank lines at the
/// bottom; negative scrolls down, revealing blank lines at the top. The cursor position does
/// not change.
/// @param con Destination stream
/// @param lines Number of lines to scroll (negative scrolls down)
/// @return true on success
bool conScroll(_In_ ConStream* con, int16 lines);

/// Switches to (or back from) the terminal's alternate screen buffer, if available (see
/// ConCaps::altscreen). Only ever available under VT -- the legacy Windows console has no
/// equivalent, so this always fails there.
/// @param con Destination stream
/// @param enable true to switch to the alternate screen, false to switch back
/// @return true on success
bool conAltScreen(_In_ ConStream* con, bool enable);

/// @}  // end of console_cursor group

CX_C_END
