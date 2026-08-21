#pragma once

// Every console-related C source file MUST begin with:
//
//   #ifdef CX_LOCK_DEBUG
//   #undef CX_LOCK_DEBUG
//   #endif
//
// as its literal first lines, before any #include -- exactly like every cx/log/*.c file
// does. Under CX_LOCK_DEBUG, mutex.h redefines mutexAcquire()/mutexRelease() to log, and a
// console stream taking its own lock while that is active would log, which reaches
// logconsole, which deadlocks.

#include <cx/thread/atomic.h>
#include <cx/thread/mutex.h>
#include "concursor.h"
#include "conin.h"
#include "constyle.h"

CX_C_BEGIN

typedef enum ConKind {
    CON_Kind_Out,
    CON_Kind_Err,
    CON_Kind_In,
    CON_Kind_Mem,
} ConKind;

#define CONBUF_DEFAULT_SIZE 4096

struct ConStream {
    ConKind kind;

    Mutex lock;
    atomic(intptr) owner;   // thrCurrentOSThreadID() of the current holder; 0 == free
    uint32 depth;           // owner-only; valid only while the lock is held

    ConCaps caps;

    uint8* buf;   // NULL for CON_Kind_Mem
    uint32 bufsz;
    uint32 bufused;
    bool linebuffered;   // flush when a write contains '\n'
    bool autoflush;      // flush after every write (conErr())

    ConStyle curStyle;   // exactly what the caller last passed to conSetStyle/conResetStyle
    bool styleActive;    // curStyle differs from the all-default style; drives conShutdown()

    string memcapture;   // CON_Kind_Mem only

    void* plat;          // platform-owned; opaque to portable code
};

// Internal write helper (conout.c) shared with constyle.c. con must already be locked by the
// caller.
bool _conWriteLocked(_Inout_ ConStream* con, _In_reads_bytes_(sz) const uint8* buf, size_t sz);

// Internal UTF-8 encoder (conout.c) shared with constyle.c. Returns the number of bytes
// written to out (1-4). An invalid code point is replaced with U+FFFD.
uint32 _conUtf8Encode(_Out_writes_(4) uint8 out[4], int32 cp);

// Internal UTF-8 decoder (conout.c) shared with conin.c's unix backend. Decodes the single
// codepoint starting at buf[0], writing it to *cp (U+FFFD on an invalid or truncated
// sequence) and returning the number of bytes consumed -- always >= 1 and <= len, so the
// caller can always advance. len is how many bytes are actually available, which may be
// fewer than the sequence needs if a read was interrupted; this never reads past it.
uint32 _conUtf8Decode(_In_reads_bytes_(len) const uint8* buf, uint32 len, _Out_ int32* cp);

// Internal decimal-ASCII appenders (conout.c) shared with constyle.c and concursor.c -- both
// build VT escape sequences out of small unsigned numbers. _conAppendDec writes the bare
// digits; _conAppendCode prefixes them with ';', the separator every SGR/CSI parameter after
// the first one needs. Neither NUL-terminates; the caller tracks its own length.
uint32 _conAppendDec(char* buf, uint32 pos, uint32 v);
uint32 _conAppendCode(char* buf, uint32 pos, uint32 code);

// --- platform primitives, implemented once per platform selected at link time ---

// Allocates and initializes the platform-specific portion of a real (non-memory) stream:
// determines the underlying OS handle, detects con->caps (via _conDetectCapsAuto plus
// whatever platform probing it can add), and stores whatever the platform needs in
// con->plat. Called exactly once per singleton, from the lazy-init callbacks in console.c.
// Never called for CON_Kind_Mem.
void _conPlatInit(_Inout_ ConStream* con, ConKind kind);

// Writes sz raw bytes to the underlying OS handle, retrying on partial writes. Returns
// true on success. Never called for CON_Kind_Mem.
bool _conPlatWrite(_Inout_ ConStream* con, _In_reads_bytes_(sz) const void* buf, size_t sz);

// Re-queries the current terminal size and updates con->caps.width/height. A no-op if the
// stream is not a tty.
void _conPlatQuerySize(_Inout_ ConStream* con);

// Restores anything _conPlatInit changed and releases con->plat. Called once per real
// stream from conShutdown().
void _conPlatShutdown(_Inout_ ConStream* con);

// Sets text/background color and attributes on a stream whose VT/ANSI escape sequences are
// not usable (con->caps.vt == false) but which still has color capability -- today, only the
// Windows legacy console falling back from a failed ENABLE_VIRTUAL_TERMINAL_PROCESSING probe.
// style.fg/bg have already been downgraded by constyle.c to CON_ColorDefault or CON_Idx(0-15)
// before this is called, so implementations never need to handle CON_Color256/RGB. On
// unix/wasm con->caps.vt is always true whenever con->caps.color != CON_ColorNone (see
// concaps.c's ladder), so this is never actually reached there; their implementations exist
// only to satisfy the link and devAssert if that assumption is ever violated. Never called
// for CON_Kind_Mem.
bool _conPlatSetStyleLegacy(_Inout_ ConStream* con, ConStyle style);

// --- legacy cursor/screen backend, used only when caps.cursor is true but caps.vt is false ---
//
// Today that combination exists only on Windows falling back from a failed
// ENABLE_VIRTUAL_TERMINAL_PROCESSING probe (see win_console.c's _conPlatInit). On unix/wasm
// caps.cursor is always false whenever caps.vt is false (concaps.c sets caps.cursor = vt
// outright), so concursor.c never reaches these there; their implementations exist only to
// satisfy the link and devAssert if that assumption is ever violated. Never called for
// CON_Kind_Mem -- a memory stream reporting caps.cursor without caps.vt is a malformed test
// fixture, not a real backend.

// row/col are 0-based, already clamped to the stream's actual buffer/window bounds by the
// caller where that matters (conMoveCursor()).
bool _conPlatCursorSet(_Inout_ ConStream* con, uint16 row, uint16 col);
bool _conPlatCursorGet(_Inout_ ConStream* con, _Out_ uint16* row, _Out_ uint16* col);
bool _conPlatCursorShow(_Inout_ ConStream* con, bool show);

// Save/restore the cursor position across the legacy backend, where (unlike VT's DECSC/DECRC)
// there is no terminal-side save slot -- the platform file remembers the position itself.
bool _conPlatCursorSave(_Inout_ ConStream* con);
bool _conPlatCursorRestore(_Inout_ ConStream* con);

bool _conPlatEraseLine(_Inout_ ConStream* con, ConEraseMode mode);
bool _conPlatEraseScreen(_Inout_ ConStream* con, ConEraseMode mode);

// Positive lines scrolls content up (new blank lines appear at the bottom); negative scrolls
// down. Matches conScroll()'s sign convention.
bool _conPlatScroll(_Inout_ ConStream* con, int16 lines);

// --- capability detection, implemented in concaps.c (portable, no platform dependency) ---

// Pure, unit-testable capability detection from explicit inputs -- no I/O of its own. NULL
// means the corresponding environment variable was unset. Windows-only inputs (wt_session,
// conemuansi, term_program) are harmless to pass as NULL on other platforms.
//
// `termless` is what an unset TERM should be taken to mean on this platform, and it is the
// one input that is not an environment variable. On unix an unset TERM is a real signal that
// there is no terminal to speak ANSI at, so the platform file passes CON_ColorNone. Windows
// consoles never set TERM at all, so it set an appropriate default based on API capabilities.
//
// NO_COLOR is applied after `termless`, so it still wins on every platform.
void _conDetectCaps(_Out_ ConCaps* out, bool istty, ConColorDepth termless,
                    _In_opt_z_ const char* term, _In_opt_z_ const char* colorterm,
                    _In_opt_z_ const char* no_color, _In_opt_z_ const char* force_color,
                    _In_opt_z_ const char* clicolor_force, _In_opt_z_ const char* wt_session,
                    _In_opt_z_ const char* conemuansi, _In_opt_z_ const char* term_program,
                    _In_opt_z_ const char* lang);

// Convenience wrapper that reads the environment itself via getenv() and forwards to
// _conDetectCaps(). Platform files call this after determining `istty` and `termless`.
void _conDetectCapsAuto(_Out_ ConCaps* out, bool istty, ConColorDepth termless);

// --- escape-sequence decoding, implemented in conin.c (portable, no platform dependency) ---
//
// Used only by the unix backend -- Windows gets structured key events straight from
// ReadConsoleInputW and never has raw escape bytes to decode; wasm has no input at all.

typedef enum ConDecodeResult {
    CON_Decode_Incomplete,   // a valid prefix so far, but more bytes are needed to know which
                             // sequence this is; the caller should read more and retry
    CON_Decode_Matched,      // *out and *consumed are filled in
    CON_Decode_NoMatch,      // buf[0..1] is not a sequence this module recognizes
} ConDecodeResult;

// buf[0] must be ESC (0x1B); len is how many bytes are available to look at so far. Pure and
// unit-testable directly, like _conDetectCaps -- no I/O, no platform dependency.
ConDecodeResult _conDecodeEscape(_In_reads_bytes_(len) const uint8* buf, uint32 len,
                                 _Out_ ConKeyEvent* out, _Out_ uint32* consumed);

// --- input platform primitives, implemented once per platform selected at link time ---
//
// All four are meaningful only for CON_Kind_In and return false immediately otherwise
// (including CON_Kind_Mem -- there is no real input behind a memory stream). None of these
// take con->lock themselves; conin.c's public wrappers do that, and the crash-handler
// restore path calls _conPlatSetMode()/_conPlatSetEcho() directly, bypassing the lock
// entirely, specifically so it can run from a crash callback without risking the deadlock a
// mutex acquisition could cause there (see conin.c).

bool _conPlatSetMode(_Inout_ ConStream* con, ConInputMode mode);
bool _conPlatSetEcho(_Inout_ ConStream* con, bool echo);
bool _conPlatInWait(_Inout_ ConStream* con, int64 timeout);
bool _conPlatReadKey(_Inout_ ConStream* con, _Out_ ConKeyEvent* out, int64 timeout);

// Reads one raw byte from CON_Kind_In's underlying handle with no tty/key decoding of any
// kind -- used only by conin.c's redirected-input fallback (conGetCaps().istty == false), so
// that conReadLine()/conReadPassword() work when stdin is a pipe or file instead of failing
// outright. Unlike the four hooks above, this is meaningful (and expected to be reached)
// whether or not the stream is a real terminal; on wasm, where istty is always false, it is
// the ordinary path rather than an unreachable one. Blocks until a byte arrives or the
// underlying handle hits EOF/error, in which case it returns false.
bool _conPlatReadRawByte(_Inout_ ConStream* con, _Out_ uint8* out);

CX_C_END
