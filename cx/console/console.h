/// @file console.h
/// @brief Console streams, capability detection, and buffered output

#pragma once

#include <cx/cx.h>
#include <cx/meta/block.h>
#include <cx/string/strbase.h>
#include <cx/stype/stvar.h>
#include <cx/utils/macros/args.h>

CX_C_BEGIN

/// @defgroup console_core Streams and Capabilities
/// @ingroup console
/// @{

/// Opaque handle to a console stream. Obtain via conOut(), conErr(), conIn(), or
/// conCreateMem(). Never access members directly.
typedef struct ConStream ConStream;

/// Color depth a stream is able to render.
typedef enum ConColorDepth {
    CON_ColorNone = 0,   ///< No color support; styling is a no-op
    CON_Color16,         ///< Standard + bright ANSI 16-color palette
    CON_Color256,        ///< 256-color palette (6x6x6 cube + grayscale ramp + 16 base)
    CON_ColorTrue,       ///< 24-bit RGB
} ConColorDepth;

/// Capabilities of a console stream, detected once at stream initialization from
/// environment variables and a platform probe (never from a terminfo/termcap database).
typedef struct ConCaps {
    bool istty;            ///< The underlying handle is an interactive terminal
    bool vt;               ///< VT/ANSI escape sequences are accepted on output
    bool unicode;          ///< Non-ASCII UTF-8 output is safe to write directly
    bool cursor;           ///< Cursor addressing (conSetCursor and friends) is available
    bool altscreen;        ///< The alternate screen buffer is available
    bool cursorquery;      ///< conGetCursor() can report the real cursor position
    ConColorDepth color;   ///< Highest color depth the stream will render
    uint16 width;          ///< Terminal width in columns, 0 if unknown
    uint16 height;         ///< Terminal height in rows, 0 if unknown
} ConCaps;

/// Returns the process-wide standard output stream, creating it on first call.
///
/// The returned pointer is a permanent singleton; it is never destroyed and must not be
/// passed to conDestroy(). Safe to call from any thread at any time.
///
/// @return The standard output stream
_Ret_valid_ ConStream* conOut(void);

/// Returns the process-wide standard error stream, creating it on first call.
///
/// Always unbuffered -- every write reaches the underlying stream immediately, so
/// crash-adjacent diagnostics are never lost in a buffer.
///
/// @return The standard error stream
_Ret_valid_ ConStream* conErr(void);

/// Returns the process-wide standard input stream, creating it on first call.
/// @return The standard input stream
_Ret_valid_ ConStream* conIn(void);

/// Restores terminal state changed by this module and flushes all singleton streams that
/// have been created. Safe to call more than once. Does not free the singletons; they
/// remain usable afterward.
void conShutdown(void);

/// ConStream* conCreateMem(ConCaps *caps)
///
/// Creates a memory-backed console stream for testing.
///
/// Writes are captured into an internal string instead of reaching any real terminal, and
/// nothing this module does ever logs, so it is safe to use inside log-destination tests as
/// well. Behaves exactly like a real stream to every other function in this module.
///
/// @param caps Capabilities to report for this stream (copied)
/// @return A new memory-backed stream
///
/// Example:
/// @code
///   ConCaps caps = { .istty = true, .color = CON_Color256, .width = 80 };
///   ConStream *con = conCreateMem(&caps);
///   conPuts(con, _SL("hello"));
///
///   string out = 0;
///   conMemGet(con, &out);   // out == "hello"
///   strDestroy(&out);
///   conDestroy(&con);
/// @endcode
_Ret_valid_ ConStream* conCreateMem(_In_ ConCaps* caps);

/// Copies everything written to a memory-backed stream so far into *out, replacing any
/// value already there. Does not clear the stream's internal capture buffer.
/// @param con A stream created with conCreateMem()
/// @param out Receives a copy of the captured output
void conMemGet(_In_ ConStream* con, _Inout_ string* out);

/// Destroys a memory-backed stream created with conCreateMem(). Never call this on
/// conOut()/conErr()/conIn() -- they are process singletons and are never destroyed.
/// @param con Pointer to the stream handle; set to NULL on return
void conDestroy(_Pre_valid_ _Post_invalid_ ConStream** con);

/// Retrieves the current capabilities of a stream. Terminal size is re-queried; everything
/// else was detected once at stream initialization.
/// @param con Stream to query
/// @param out Receives a copy of the stream's capabilities
void conGetCaps(_In_ ConStream* con, _Out_ ConCaps* out);

/// Current terminal width in columns, re-queried on every call. 0 if unknown or not a tty.
uint16 conWidth(_In_ ConStream* con);

/// Current terminal height in rows, re-queried on every call. 0 if unknown or not a tty.
uint16 conHeight(_In_ ConStream* con);

/// Locks a stream for the calling thread.
///
/// Every public function in this module already locks internally, so explicit locking is
/// only needed to group several calls into one sequence atomic with respect to other
/// threads. Reentrant from the owning thread via an internal depth counter -- nested
/// conLock()/conUnlock() pairs on the same thread are supported and cheap -- but never
/// share a lock across threads without a matching unlock.
/// @param con Stream to lock
void conLock(_In_ ConStream* con);

/// Unlocks a stream previously locked with conLock(). Must be called once per matching
/// conLock() call, from the same thread.
/// @param con Stream to unlock
void conUnlock(_In_ ConStream* con);

/// void withConLock(ConStream *con) { ... }
///
/// Executes a block with the stream locked for its duration, unlocking automatically on
/// every exit path (including early return or break).
///
/// Example:
/// @code
///   withConLock(con) {
///       conPuts(con, _SL("fatal: "));
///       conPuts(con, msg);
///       conNL(con);
///   }
/// @endcode
/// @param con Stream to lock for the duration of the block
#define withConLock(con) blkWrap (conLock(con), conUnlock(con))

/// @}  // end of console_core group

/// @defgroup console_output Output
/// @ingroup console
/// @{

/// Writes raw bytes to a stream exactly as given -- no encoding, no line-ending
/// translation, no buffering policy beyond the stream's own.
/// @param con Destination stream
/// @param buf Bytes to write
/// @param sz Number of bytes
/// @return true if the bytes were accepted (buffered or written); false on a write error
bool conWrite(_In_ ConStream* con, _In_reads_bytes_(sz) const void* buf, size_t sz);

/// Writes a cx string to a stream. Rope-shaped strings are walked chunk by chunk with no
/// flattening allocation.
/// @param con Destination stream
/// @param s String to write (NULL or empty writes nothing and returns true)
/// @return true on success
bool conPuts(_In_ ConStream* con, _In_opt_ strref s);

/// Writes a NUL-terminated C string to a stream.
/// @param con Destination stream
/// @param sz String to write (NULL or empty writes nothing and returns true)
/// @return true on success
bool conPutsz(_In_ ConStream* con, _In_opt_z_ const char* sz);

/// Writes a single Unicode code point, UTF-8 encoded. An invalid code point is replaced
/// with U+FFFD.
/// @param con Destination stream
/// @param codepoint Unicode code point to write
/// @return true on success
bool conPutc(_In_ ConStream* con, int32 codepoint);

/// Writes a platform-appropriate newline ("\n" on unix/wasm, "\r\n" on Windows) and
/// flushes if the stream is line-buffered. A conCreateMem() stream always gets a bare
/// "\n" on every platform, since it exists to give tests byte-identical output.
/// @param con Destination stream
/// @return true on success
bool conNL(_In_ ConStream* con);

/// Forces any output buffered by this module to reach the underlying stream immediately.
/// @param con Stream to flush
/// @return true on success
bool conFlush(_In_ ConStream* con);

/// bool conFmt(ConStream *con, strref fmt, ...);
///
/// Formats arguments with cx's type-safe formatter (see @ref string_format) and writes
/// the result, exactly as if by strFormat() into a temporary string followed by
/// conPuts().
///
/// @param con Destination stream
/// @param fmt Format string
/// @return true if formatting and the write both succeeded
///
/// Example:
/// @code
///   conFmt(conOut(), _SL("Hello ${string}, you have ${int} messages"),
///          stvar(string, name), stvar(int32, count));
/// @endcode
bool _conFmt(_In_ ConStream* con, _In_ strref fmt, int n, _In_ stvar* args);
#define conFmt(con, fmt, ...) \
    _conFmt(con, fmt, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ })

/// @}  // end of console_output group

CX_C_END
