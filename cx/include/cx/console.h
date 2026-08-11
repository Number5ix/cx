#pragma once

#include <cx/console/concursor.h>
#include <cx/console/conin.h>
#include <cx/console/console.h>
#include <cx/console/constyle.h>

/// @defgroup console Console
/// @{
/// Portable console and terminal I/O: capability detection, buffered output, styled text,
/// cursor/screen control, and keyboard input, each backed by a per-platform implementation
/// selected at link time.
///
/// @defgroup console_overview Overview
/// @ingroup console
/// @{
///
/// @section console_streams Streams
///
/// A console stream (ConStream) is an opaque handle to one of the process's standard I/O
/// streams. Three singletons are always available -- conOut(), conErr(), conIn() -- lazily
/// created on first use and never destroyed by the caller. A fourth kind, created with
/// conCreateMem(), captures output into memory instead of a real terminal; it exists for
/// tests and behaves identically to a real stream from every other function's point of
/// view.
///
/// @section console_caps Capabilities
///
/// Every stream carries a ConCaps describing what it can actually do -- whether it is a
/// terminal at all, what color depth it accepts, whether cursor addressing works, and its
/// current size. Capabilities are detected once, from environment variables and a platform
/// probe -- never by reading a terminal capability database (the terminfo/termcap files
/// some other libraries consult) and never by linking a terminal library such as curses.
/// Query them with conGetCaps() before relying on a capability.
///
/// @section console_style Style
///
/// ConStyle carries a foreground color, background color, and attribute flags (bold,
/// underline, and so on), applied with conSetStyle()/conResetStyle() or bundled into a single
/// write with conPutsS() and friends. Colors are requested as CON_Idx() palette entries or
/// CON_RGB() truecolor values and are automatically downgraded to whatever the stream's
/// ConCaps.color actually supports -- truecolor to 256-color to the standard 16 to no color
/// at all -- so calling code never needs its own capability branches. Unsupported attributes
/// are dropped rather than emitting garbage. conShutdown() resets any style left active
/// before it restores terminal state.
///
/// @section console_cursor Cursor and Screen
///
/// conSetCursor()/conMoveCursor() position the cursor; conEraseLine()/conEraseScreen()/
/// conScroll() edit the visible buffer; conAltScreen() switches to the terminal's alternate
/// screen buffer where available. All are backed by VT sequences (the ANSI/VT100-style
/// escape codes most terminals accept for cursor and screen control) when ConCaps.vt is
/// true, and by the older Windows console API otherwise; all return false rather than emit
/// anything when the underlying capability (ConCaps.cursor / ConCaps.altscreen) is absent.
/// conGetCursor() is the exception: it only works when ConCaps.cursorquery is true (the
/// legacy Windows backend), because querying position under VT would mean sending a query
/// sequence and reading the reply back from stdin -- a race with any other input consumer,
/// and a possible hang against a terminal that never answers. conSaveCursor()/
/// conRestoreCursor() is what most callers reaching for conGetCursor() actually want, and it
/// works under VT too.
///
/// @section console_input Input
///
/// conSetMode() switches conIn() between cooked (line-buffered, terminal-echoed) and raw
/// (keystrokes available immediately) input; conReadKey() reads and decodes one key,
/// including arrows, function keys, and Home/End/PageUp/PageDown where the terminal sends a
/// recognizable sequence for them. conReadLine()/conReadPassword() build a small line editor
/// (Backspace only) out of conReadKey(), the latter never echoing what was typed. conSetMode(),
/// conSetEcho(), conInWait(), and conReadKey() all require a real interactive terminal, but
/// conReadLine()/conReadPassword() fall back to a plain fgets()-style byte read (no editing, no
/// echo) when conIn() is redirected to a pipe or file. Raw mode is
/// restored to cooked automatically by conShutdown() and, on a best-effort basis, by the
/// crash handler (cx/debug/crash.h) on an abnormal exit, so a crash never leaves the shell
/// stuck with echo off. Ctrl+C/Ctrl+Z/Ctrl+\\ still raise their usual signals even in raw
/// mode -- like ConCaps not tracking SIGWINCH (the Unix signal sent when a terminal window
/// is resized), this module deliberately leaves signal disposition alone.
///
/// @section console_fmt Formatting
///
/// conFmt()/conFmtS() format arguments with cx's type-safe formatter (@ref
/// string_format) and write the result, exactly as if by strFormat() into a temporary
/// string followed by conPuts()/conPutsS(). The StreamBuffer adapter in
/// cx/serialize/sbcon.h (sbufConOut()/sbufConCRegisterPush()) writes a stream buffer's
/// contents to a console stream the same way sbfile.h does for VFS files, minus a close
/// parameter -- console streams are never closed by a consumer.
///
/// @section console_threading Threading
///
/// Every public call is protected by the stream's own lock: a write can never be split by
/// a write from another thread. conLock() / conUnlock() (or the withConLock() block
/// wrapper) group a sequence of calls into one critical section; recursive locking from the
/// owning thread is supported via an internal depth counter, since cx's Mutex is
/// deliberately not reentrant.
///
/// @section console_norelog No logging, ever
///
/// This module must never call into cx/log, in either direction: a console log
/// destination writes through this module, so a console stream that itself logged could
/// deadlock or recurse forever. Errors are always reported through return values.
///
/// @}  // end of console_overview group
/// @}  // end of console group
