#pragma once
/// @file log/logconsole.h
/// @brief Console-based log destination with level coloring

/// @defgroup log_console Console Logging
/// @ingroup log
/// @{
///
/// Console-based logging destination that writes formatted, level-colored log lines to
/// conOut()/conErr() (see @ref console).
///
/// The console is a **transport** and takes a serializer like any other (see
/// @ref log_serializer), so a line looks the same whether it ends up in a file or on a terminal.
/// What the console destination adds is routing between stdout/stderr by severity and a
/// per-level ConStyle, both automatically downgraded (or dropped entirely) by the destination
/// stream's own capabilities -- a line sent to a redirected-to-a-file stdout gets no escape
/// codes even though the identical line to a real terminal would be colored.
///
/// **Basic Usage:**
/// @code
///   LogConsoleConfig cfg = {
///       .stderrLevel = LOG_Count,   // everything to stderr, stdout stays clean
///   };
///   LogTextConfig tcfg = {
///       .dateFormat = LOG_DateISOCompact,
///       .flags      = LOG_ShortLevel | LOG_IncludeChannel,
///   };
///
///   // ...or, on a console where the timestamp is just noise, LOG_OmitDate leaves it out
///   // entirely and the line starts with whatever comes next:
///   //   LogTextConfig tcfg = { .flags = LOG_OmitDate | LOG_ShortLevel | LOG_IncludeChannel };
///
///   // real conOut()/conErr()
///   LogConsoleData *lcd = logconsoleCreate(NULL, NULL, &cfg, logTextSerializer(&tcfg));
///   LogDest *dest = logconsoleRegister(LOG_Info, NULL, lcd);
///
///   // Later, unregister to release
///   logUnregisterDest(dest);
/// @endcode

#include <cx/console/console.h>
#include <cx/console/constyle.h>
#include <cx/log/log.h>
#include <cx/log/logserializer.h>

CX_C_BEGIN

/// Color decision for a console destination
enum LOGCON_COLOR_MODE {
    LOGCON_ColorAuto = 0,   ///< Style only when the destination stream reports color support
    LOGCON_ColorOn,         ///< Always apply per-level style, letting ConStyle's own downgrade
                            ///< ladder decide what the stream can actually render
    LOGCON_ColorOff,        ///< Never apply style; plain text only
};

/// Configuration for console-based logging
///
/// Controls stdout/stderr routing and per-level coloring. Output formatting belongs to the
/// serializer the console is created with, not here.
typedef struct LogConsoleConfig {
    /// Messages at this level or more severe (level <= stderrLevel) go to conErr(); the
    /// rest go to conOut(). Pass LOG_Count to send everything to stderr, keeping stdout
    /// free for program output -- the usual choice for a CLI tool.
    int stderrLevel;

    int colorMode;   ///< A LOGCON_COLOR_MODE value

    /// Per-level style override. An all-zero entry (the zero-initialized default) uses
    /// this destination's built-in style for that level instead.
    ConStyle levelStyle[LOG_Count];
} LogConsoleConfig;

/// Opaque handle for console logging state
typedef struct LogConsoleData LogConsoleData;

// ============================================================================
// High Level Interface
// ============================================================================

/// Create a console logging destination
///
/// @param out Stream for messages less severe than stderrLevel; NULL uses conOut(). Not
///            acquired or owned -- must outlive the returned handle.
/// @param err Stream for messages at or more severe than stderrLevel; NULL uses conErr().
///            Not acquired or owned -- must outlive the returned handle.
/// @param config Logging configuration (copied, caller retains ownership)
/// @param ser Serializer to render records with; **ownership transfers**. NULL gets a default
///            text serializer.
/// @return Console logging handle
/// @code
///   LogConsoleConfig cfg = { .stderrLevel = LOG_Count };
///   LogConsoleData *lcd = logconsoleCreate(NULL, NULL, &cfg, NULL);   // real conOut()/conErr()
///
///   // or, for a test that asserts exact output:
///   ConStream *out = conCreateMem(&(ConCaps){ 0 });
///   LogConsoleData *lcdTest = logconsoleCreate(out, out, &cfg, NULL);
/// @endcode
_Ret_valid_ LogConsoleData* logconsoleCreate(_In_opt_ ConStream* out, _In_opt_ ConStream* err,
                                             _In_ LogConsoleConfig* config,
                                             _In_opt_ LogSerializer* ser);

/// Register a console destination with the logging system
///
/// @param maxlevel Maximum log level to write to the console
/// @param chanfilter Channel path pattern, or NULL for every unrestricted channel
/// @param console Console logging handle from logconsoleCreate()
/// @return Destination handle for later unregistration, or NULL on failure
LogDest* logconsoleRegister(int maxlevel, _In_opt_ strref chanfilter,
                            _In_ LogConsoleData* console);

// ============================================================================
// Low Level Interface
// ============================================================================
//
// These callbacks can be used directly with logRegisterDest() for custom
// destination handling. Most users should use the high-level interface above.

/// Log message callback for console destinations
///
/// Renders a log record and writes it to conOut() or conErr(), styled per level
/// according to the destination's configuration and the destination stream's capabilities.
///
/// @param rec Log record to write
/// @param userdata LogConsoleData pointer from logconsoleCreate()
void logconsoleMsgFunc(_In_ const LogRecord* rec, _In_opt_ void* userdata);

/// Batch completion callback for console destinations
///
/// Flushes both conOut() and conErr() so batch messages reach the terminal together.
///
/// @param batchid Completed batch identifier
/// @param userdata LogConsoleData pointer from logconsoleCreate()
void logconsoleBatchFunc(uint32 batchid, _In_opt_ void* userdata);

/// Cleanup callback for console destinations
///
/// Releases the console logging handle. Never closes conOut()/conErr() themselves --
/// they are process singletons.
///
/// @param userdata LogConsoleData pointer from logconsoleCreate()
void logconsoleCloseFunc(_In_opt_ void* userdata);

/// @}  // end of log_console group

CX_C_END
