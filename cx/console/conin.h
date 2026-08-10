/// @file conin.h
/// @brief Keyboard input: raw mode, key decoding, line/password reading

#pragma once

#include <cx/time/time.h>
#include "console.h"

CX_C_BEGIN

/// @defgroup console_input Input
/// @ingroup console
/// @{

/// Logical key identity, independent of the byte/escape sequence a terminal happened to send.
typedef enum ConKey {
    CON_Key_None = 0,   ///< No key (conReadKey() failed/timed out); never appears in a
                        ///< successful ConKeyEvent
    CON_Key_Char,       ///< A printable/control character; the codepoint is in ConKeyEvent::ch

    CON_Key_Enter,
    CON_Key_Tab,
    CON_Key_Backspace,
    CON_Key_Escape,

    CON_Key_Up,
    CON_Key_Down,
    CON_Key_Left,
    CON_Key_Right,

    CON_Key_Home,
    CON_Key_End,
    CON_Key_PageUp,
    CON_Key_PageDown,
    CON_Key_Insert,
    CON_Key_Delete,

    CON_Key_F1,
    CON_Key_F2,
    CON_Key_F3,
    CON_Key_F4,
    CON_Key_F5,
    CON_Key_F6,
    CON_Key_F7,
    CON_Key_F8,
    CON_Key_F9,
    CON_Key_F10,
    CON_Key_F11,
    CON_Key_F12,

    CON_Key_Resize,   ///< Synthetic: the terminal size changed (Windows only -- see conReadKey())
} ConKey;

/// Modifier flags for ConKeyEvent::mods.
enum CON_MOD_FLAGS {
    CON_Mod_Shift = 0x01,
    CON_Mod_Alt   = 0x02,
    CON_Mod_Ctrl  = 0x04,
};

/// A single decoded key press.
typedef struct ConKeyEvent {
    ConKey key;     ///< Which key. CON_Key_Char means ch holds the codepoint.
    int32 ch;       ///< Unicode codepoint for CON_Key_Char; 0 for every other key
    flags_t mods;   ///< Bitwise OR of CON_MOD_FLAGS. Not all terminals report all modifiers on
                    ///< all keys -- Ctrl+<letter> is reliable everywhere; Alt and Shift on
                    ///< arrow/function keys depend on the terminal emulator.
} ConKeyEvent;

/// Input mode for a stream. See conSetMode().
typedef enum ConInputMode {
    CON_Cooked,   ///< Line-buffered, terminal-echoed, signal-generating -- the default
    CON_Raw,      ///< Keystrokes available immediately, one at a time
} ConInputMode;

/// Switches a stream between cooked and raw input mode.
///
/// CON_Raw disables canonical (line-buffered) input so conReadKey() sees each keystroke as
/// soon as it arrives, and disables software flow control (Ctrl+S/Ctrl+Q) so those pass
/// through as ordinary key events instead of freezing output. It does **not** touch signal
/// generation: Ctrl+C/Ctrl+Z/Ctrl+\ still raise SIGINT/SIGTSTP/SIGQUIT as usual, the same way
/// this module never touches SIGWINCH (see ConCaps). A caller that wants Ctrl+C delivered as
/// a keystroke instead of a signal must block or ignore the signal itself.
///
/// Only meaningful on conIn(); requires a real interactive terminal (ConCaps::istty).
/// Automatically restored to cooked on conShutdown(), and on process crash where the crash
/// handler (cx/debug/crash.h) is active, so an abnormal exit never leaves the shell in raw
/// mode with echo off.
///
/// @param con Stream to switch (only conIn() is meaningful)
/// @param mode CON_Cooked or CON_Raw
/// @return true on success
bool conSetMode(_In_ ConStream* con, ConInputMode mode);

/// Enables or disables the terminal's own echoing of typed characters.
///
/// Independent of conSetMode() -- conReadLine()/conReadPassword() manage this themselves.
/// Only meaningful on conIn(); requires a real interactive terminal.
/// @param con Stream to configure (only conIn() is meaningful)
/// @param echo true to echo typed characters, false to suppress
/// @return true on success
bool conSetEcho(_In_ ConStream* con, bool echo);

/// Waits for input to become available without consuming it.
/// @param con Stream to poll (only conIn() is meaningful)
/// @param timeout Maximum time to wait (microseconds; timeForever to block indefinitely)
/// @return true if input is available; false on timeout or if the stream has no input
bool conInWait(_In_ ConStream* con, int64 timeout);

/// Reads and decodes a single key press.
///
/// On Windows, a console resize while waiting is reported as CON_Key_Resize (query the new
/// size with conWidth()/conHeight()) rather than being silently swallowed; unix/wasm never
/// produce it -- poll conWidth()/conHeight() there instead (see ConCaps for why this module
/// does not install a SIGWINCH handler).
///
/// @param con Stream to read from (only conIn() is meaningful)
/// @param out Receives the decoded key
/// @param timeout Maximum time to wait (microseconds; timeForever to block indefinitely)
/// @return true if a key was read; false on timeout, error, or a stream with no input
bool conReadKey(_In_ ConStream* con, _Out_ ConKeyEvent* out, int64 timeout);

/// Reads a line of input with basic editing (Backspace), echoing each character typed.
///
/// When con is a real interactive terminal, switches it to CON_Raw for the duration of the
/// call and restores CON_Cooked before returning, success or not. When con is redirected
/// (ConCaps::istty is false -- a pipe or a file), there is no terminal to put in raw mode and
/// nothing to echo, so this instead reads plain bytes up to the next '\n' (tolerating a
/// preceding '\r'), same as a stdio fgets() -- no editing, no echo, and indistinguishable
/// from conReadPassword() in that case.
///
/// @param con Stream to read from (only conIn() is meaningful)
/// @param out Receives the line, replacing any previous value. Never includes the
///            terminating '\n' (or the interactive form's Enter).
/// @return true on success; false on EOF with nothing read, or if the read failed
bool conReadLine(_In_ ConStream* con, _Inout_ string* out);

/// Like conReadLine(), but never echoes typed characters -- for password entry.
///
/// Same redirected-input fallback as conReadLine(): when con is not a real terminal, there
/// is no terminal echo to suppress, so this just reads a plain line like conReadLine() would.
///
/// @param con Stream to read from (only conIn() is meaningful)
/// @param out Receives the line, replacing any previous value
/// @return true on success; false on EOF with nothing read, or if the read failed
bool conReadPassword(_In_ ConStream* con, _Inout_ string* out);

/// @}  // end of console_input group

CX_C_END
