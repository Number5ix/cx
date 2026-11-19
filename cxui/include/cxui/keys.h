#pragma once

/// @file keys.h
/// @brief Keys and modifiers

#include <cx/cx.h>
#include <cx/string.h>

CX_C_BEGIN

/// @defgroup cxui_keys Keys
/// @ingroup cxui
/// @{
/// Keys are identified by what they mean in the current keyboard layout, not by their position:
/// CXUI_KEY_Z is the key that types a "z", wherever it is on the keyboard.
///
/// A key cxui has no code for, such as `ü` on a German keyboard, still raises key events, with
/// CXUI_KEY_None as the key. Its `scancode` tells it apart from other keys, so it can still be
/// bound. Scancodes are the platform's own numbers: one saved on Windows means nothing on Linux.
///
/// A key with modifiers has a **name** such as `"Ctrl+Shift+F3"` or `"Primary+F"`, for storing in
/// settings, and a **label** for showing to users. Names are the same on every platform and stable
/// across versions. The modifiers come first, in the order Primary, Ctrl, Alt, Shift, Meta, and the
/// key's name is its CXUI_KEY_ suffix: `"Up"`, `"PageDown"`, `"Pad5"`, `"BracketLeft"`.
///
/// A label is written the platform's way, with Primary as the key it stands for: `"Ctrl+F"` on
/// Windows and Linux and `"⌘F"` on macOS, which uses Apple's symbols for the modifiers and for
/// keys such as Enter (`"⇧↩"`). Labels can't be parsed back.
///
/// Parsing ignores case and spaces around each part, and also accepts some common other spellings:
/// `"Control"`, `"Cmd"`, `"Command"`, `"Win"`, `"Super"`, `"Option"`, `"Esc"`, `"Return"`, `"Del"`,
/// `"Ins"`, `"PgUp"`, `"PgDn"`.
///
/// @code
///   CxUiKey key;
///   uint32 mods;
///   if (cxuiKeyParse(&key, &mods, _SL("Primary+F")))
///       findShortcut = (Shortcut){ key, mods };
///   ...
///   if (cxuiKeyMatches(findShortcut.key, findShortcut.mods, ev->key, ev->mods))
///       return showFind(ev);
/// @endcode

/// A key on the keyboard
typedef enum CxUiKey {
    CXUI_KEY_None = 0,

    CXUI_KEY_A, CXUI_KEY_B, CXUI_KEY_C, CXUI_KEY_D, CXUI_KEY_E, CXUI_KEY_F, CXUI_KEY_G,
    CXUI_KEY_H, CXUI_KEY_I, CXUI_KEY_J, CXUI_KEY_K, CXUI_KEY_L, CXUI_KEY_M, CXUI_KEY_N,
    CXUI_KEY_O, CXUI_KEY_P, CXUI_KEY_Q, CXUI_KEY_R, CXUI_KEY_S, CXUI_KEY_T, CXUI_KEY_U,
    CXUI_KEY_V, CXUI_KEY_W, CXUI_KEY_X, CXUI_KEY_Y, CXUI_KEY_Z,

    CXUI_KEY_0, CXUI_KEY_1, CXUI_KEY_2, CXUI_KEY_3, CXUI_KEY_4,
    CXUI_KEY_5, CXUI_KEY_6, CXUI_KEY_7, CXUI_KEY_8, CXUI_KEY_9,

    CXUI_KEY_F1, CXUI_KEY_F2, CXUI_KEY_F3, CXUI_KEY_F4, CXUI_KEY_F5, CXUI_KEY_F6,
    CXUI_KEY_F7, CXUI_KEY_F8, CXUI_KEY_F9, CXUI_KEY_F10, CXUI_KEY_F11, CXUI_KEY_F12,
    CXUI_KEY_F13, CXUI_KEY_F14, CXUI_KEY_F15, CXUI_KEY_F16, CXUI_KEY_F17, CXUI_KEY_F18,
    CXUI_KEY_F19, CXUI_KEY_F20, CXUI_KEY_F21, CXUI_KEY_F22, CXUI_KEY_F23, CXUI_KEY_F24,

    CXUI_KEY_Up, CXUI_KEY_Down, CXUI_KEY_Left, CXUI_KEY_Right,
    CXUI_KEY_Home, CXUI_KEY_End, CXUI_KEY_PageUp, CXUI_KEY_PageDown,
    CXUI_KEY_Insert, CXUI_KEY_Delete, CXUI_KEY_Backspace,
    CXUI_KEY_Tab, CXUI_KEY_Enter, CXUI_KEY_Escape, CXUI_KEY_Space,

    CXUI_KEY_Pad0, CXUI_KEY_Pad1, CXUI_KEY_Pad2, CXUI_KEY_Pad3, CXUI_KEY_Pad4,
    CXUI_KEY_Pad5, CXUI_KEY_Pad6, CXUI_KEY_Pad7, CXUI_KEY_Pad8, CXUI_KEY_Pad9,
    CXUI_KEY_PadDecimal, CXUI_KEY_PadAdd, CXUI_KEY_PadSubtract, CXUI_KEY_PadMultiply,
    CXUI_KEY_PadDivide, CXUI_KEY_PadEnter,

    CXUI_KEY_Period, CXUI_KEY_Comma, CXUI_KEY_Slash, CXUI_KEY_Minus, CXUI_KEY_Equals,
    CXUI_KEY_Semicolon, CXUI_KEY_Quote, CXUI_KEY_BracketLeft, CXUI_KEY_BracketRight,
    CXUI_KEY_Backslash, CXUI_KEY_Grave,

    CXUI_KEY_Shift, CXUI_KEY_Ctrl, CXUI_KEY_Alt, CXUI_KEY_Meta,
    CXUI_KEY_CapsLock, CXUI_KEY_NumLock, CXUI_KEY_ScrollLock,
    CXUI_KEY_PrintScreen, CXUI_KEY_Pause, CXUI_KEY_ContextMenu,

    CXUI_KEY_Count
} CxUiKey;

/// Modifier keys held during a key or mouse event
enum CxUiModifiers {
    CXUI_MOD_Shift = 0x0001,
    CXUI_MOD_Ctrl  = 0x0002,
    CXUI_MOD_Alt   = 0x0004,
    /// The Windows key, or Command on macOS
    CXUI_MOD_Meta  = 0x0008,
    /// The platform's usual shortcut modifier: Ctrl on Windows and Linux, Command on macOS. Used
    /// when parsing and matching shortcuts; events report the real key instead.
    CXUI_MOD_Primary = 0x0010,
};

/// Lock keys that are on, as given by cxuiKeyLocks()
enum CxUiKeyLocks {
    CXUI_LOCK_Caps   = 0x0001,
    CXUI_LOCK_Num    = 0x0002,
    CXUI_LOCK_Scroll = 0x0004,
};

/// Writes the name of a key with modifiers
///
/// @param out String to receive the name
/// @param key The key
/// @param mods CxUiModifiers
/// @return false, leaving out empty, if key isn't a valid key
bool cxuiKeyFormat(_Inout_ string* out, CxUiKey key, uint32 mods);

/// Writes a key with modifiers as the platform shows shortcuts, for menus, buttons and hints
///
/// @param out String to receive the label
/// @param key The key
/// @param mods CxUiModifiers; CXUI_MOD_Primary is written as the platform's shortcut modifier
/// @return false, leaving out empty, if key isn't a valid key
///
/// Example:
/// @code
///   string hint = 0;
///   cxuiKeyLabel(&hint, CXUI_KEY_F, CXUI_MOD_Primary);   // "Ctrl+F", or "⌘F" on macOS
/// @endcode
bool cxuiKeyLabel(_Inout_ string* out, CxUiKey key, uint32 mods);

/// Reads the name of a key with modifiers
///
/// @param key Receives the key
/// @param mods Receives the CxUiModifiers, with CXUI_MOD_Primary kept as it is
/// @param name The name, such as `"Ctrl+Shift+F3"`
/// @return false, setting key to CXUI_KEY_None and mods to 0, if the name isn't valid
bool cxuiKeyParse(_Out_ CxUiKey* key, _Out_ uint32* mods, _In_ strref name);

/// Checks whether a key event matches a shortcut
///
/// The modifiers must match exactly, with CXUI_MOD_Primary in the shortcut standing for Ctrl on
/// Windows and Linux and for Meta (Command) on macOS.
///
/// @param key The shortcut's key
/// @param mods The shortcut's CxUiModifiers
/// @param evKey The key from a CxUiKeyEvent
/// @param evMods The modifiers from a CxUiKeyEvent
/// @return true if they match
bool cxuiKeyMatches(CxUiKey key, uint32 mods, CxUiKey evKey, uint32 evMods);

/// Gets which lock keys are on
///
/// macOS has no Num Lock or Scroll Lock: there CXUI_LOCK_Num is always set, as the keypad always
/// types digits, and CXUI_LOCK_Scroll never is. UI thread only.
///
/// @return CxUiKeyLocks; 0 if cxui isn't running
///
/// Example:
/// @code
///   if (ev->key == CXUI_KEY_Pad7 && (cxuiKeyLocks() & CXUI_LOCK_Num))
///       typeDigit(7);
/// @endcode
uint32 cxuiKeyLocks(void);

/// @}  // end of cxui_keys group

CX_C_END
