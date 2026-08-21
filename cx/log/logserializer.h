#pragma once
/// @file log/logserializer.h
/// @brief Log record serializers, independent of where the result is written

/// @defgroup log_serializer Log Serializers
/// @ingroup log
/// @{
///
/// A destination is two separable things: a **serializer**, which turns a LogRecord into bytes,
/// and a **transport**, which puts those bytes somewhere. Splitting them is what lets one
/// rendering feed several places and one place accept several renderings:
///
/// @code
///    record ──> serializer ──> transport
///               text            rotating file
///               NDJSON          console (TTY-aware)
/// @endcode
///
/// A rotating NDJSON log is therefore not a new kind of destination; it is the file transport
/// with a different serializer:
/// @code
///   LogFileConfig cfg = { .rotateMode = LOG_RotateSize, .rotateSize = 10 * 1024 * 1024 };
///   LogDest *dest = logfileRegister(LOG_Info, NULL, vfs, _SL("app.ndjson"), &cfg,
///                                   logNdjsonSerializer(NULL));
/// @endcode
///
/// **Ownership:** a transport takes ownership of the serializer it is created with and destroys
/// it when the destination is closed. Passing NULL gets a default text serializer, which is what
/// every transport did before serializers existed.
///
/// Serializers do not write line terminators. Whether records are separated by "\n", "\r\n", or
/// nothing at all is a property of the transport.

#include <cx/log/log.h>

CX_C_BEGIN

/// Timestamp format options for log output
enum LOG_DATE_FORMATS {
    LOG_DateISO,             ///< ISO 8601: "2026-01-02T15:04:05Z", or with a zone offset
    LOG_DateISOCompact,      ///< Compact ISO: "2026-01-02 15:04:05"
    LOG_DateNCSA,            ///< NCSA Common Log format: "02/Jan/2026:15:04:05 +00 00"
    LOG_DateSyslog,          ///< Syslog format: "Jan  2 15:04:05"
    LOG_DateISOCompactMsec,  ///< Compact ISO with milliseconds: "2026-01-02 15:04:05.123"
    LOG_DateTimeOnly,        ///< Time of day only, no calendar date: "15:04:05"
    LOG_DateTimeOnlyMsec     ///< Time of day only, with milliseconds: "15:04:05.123"
};

/// Formatting flags for text log output
enum LOG_FLAGS {
    LOG_LocalTime      = 0x0001,   ///< Use local time instead of UTC
    LOG_OmitLevel      = 0x0002,   ///< Do not include severity level
    LOG_ShortLevel     = 0x0004,   ///< Use single-character level abbreviations
    LOG_BracketLevel   = 0x0008,   ///< Enclose log level in brackets [INFO]
    LOG_JustifyLevel   = 0x0010,   ///< Make level a fixed-width column
    LOG_IncludeChannel = 0x0020,   ///< Include channel path in output
    LOG_BracketChannel = 0x0040,   ///< Enclose channel in brackets [net/http]
    LOG_AddColon       = 0x0080,   ///< Add colon after the prefix
    LOG_ChannelFirst   = 0x0100,   ///< Channel between date and level instead of at end
    LOG_IncludeContext = 0x0200,   ///< Append the log context's fields as [key:value ...]
    LOG_OmitDate       = 0x0400,   ///< Do not include a timestamp at all
};

/// Turns a record into bytes
///
/// @param out Receives the serialized record; any existing value is destroyed first
/// @param rec Record to serialize
/// @param userdata Serializer-private context
typedef void (*LogSerializeFunc)(_Inout_ string* out, _In_ const LogRecord* rec,
                                 _In_opt_ void* userdata);

/// Releases a serializer's private context
typedef void (*LogSerializerClose)(_In_opt_ void* userdata);

/// A record serializer, owned by whichever transport it was handed to
typedef struct LogSerializer {
    LogSerializeFunc serialize;   ///< Called once per record
    LogSerializerClose close;     ///< Optional; called when the owning transport closes
    void* userdata;               ///< Serializer-private context
} LogSerializer;

/// Assemble a serializer from callbacks
///
/// Only needed to write a serializer of your own; the built-in ones have their own factories.
///
/// @param serialize Called once per record
/// @param close Optional cleanup for userdata
/// @param userdata Serializer-private context
/// @return Serializer, ready to be handed to a transport
_Ret_valid_ LogSerializer* logSerializerCreate(_In_ LogSerializeFunc serialize,
                                               _In_opt_ LogSerializerClose close,
                                               _In_opt_ void* userdata);

/// Destroy a serializer
///
/// Transports call this on the serializer they own; a caller only needs it for a serializer that
/// was never handed to one.
///
/// @param ser Serializer to destroy; set to NULL
void logSerializerDestroy(_Inout_ LogSerializer** ser);

/// Serialize one record
///
/// @param out Receives the serialized record; any existing value is destroyed first
/// @param ser Serializer to use; NULL produces the record's plain rendered text
/// @param rec Record to serialize
void logSerialize(_Inout_ string* out, _In_opt_ LogSerializer* ser, _In_ const LogRecord* rec);

/// Configuration for the text serializer
///
/// Produces the one-line human-readable form: timestamp, level, channel, message.
typedef struct LogTextConfig {
    int dateFormat;   ///< Date format from LOG_DATE_FORMATS; ignored under LOG_OmitDate
    int spacing;      ///< Spaces between the prefix and the message (0 defaults to 2)
    uint32 flags;     ///< Bitwise OR of LOG_FLAGS values

    /// With LOG_IncludeContext, the context fields to render, comma-separated; empty renders
    /// all of them. A text log usually wants one or two correlation ids rather than everything
    /// a request accumulated, which is the whole reason this is a subset and not a flag.
    /// Copied at creation, so the caller need not keep it.
    strref ctxfields;
} LogTextConfig;

/// Create a text serializer
///
/// @param config Formatting configuration, or NULL for the zero-initialized default
/// @return Serializer, ready to be handed to a transport
/// @code
///   LogTextConfig tcfg = { .dateFormat = LOG_DateISO, .flags = LOG_BracketLevel };
///   LogDest *dest = logconsoleRegister(LOG_Info, NULL, NULL, NULL, &ccfg,
///                                      logTextSerializer(&tcfg));
/// @endcode
_Ret_valid_ LogSerializer* logTextSerializer(_In_opt_ LogTextConfig* config);

/// Configuration for the NDJSON serializer
typedef struct LogNdjsonConfig {
    uint32 flags;   ///< Bitwise OR of LOG_FLAGS values; only LOG_LocalTime is consulted
} LogNdjsonConfig;

/// Create an NDJSON serializer
///
/// Emits one JSON object per record: `time`, `level`, `seq`, `chan` (when the record has one)
/// and `msg`, followed by one field per **keyed** argument. Unkeyed arguments are not emitted
/// separately -- they belong to the message template and are already in `msg`.
///
/// @param config Configuration, or NULL for the default (UTC timestamps)
/// @return Serializer, ready to be handed to a transport
/// @code
///   logFmt(Info, _SL("request from ${string}"), stvar(string, host),
///          stvark(status, int32, 200));
///   // {"time":"2026-08-08T12:00:00Z","level":"Info","seq":7,"msg":"request from web01",
///   //  "status":200}
/// @endcode
_Ret_valid_ LogSerializer* logNdjsonSerializer(_In_opt_ LogNdjsonConfig* config);

// ============================================================================
// Text formatting helpers
// ============================================================================
//
// The pieces the text serializer assembles a line from. Exported so a custom serializer can
// match its prefixes exactly instead of reimplementing them.

/// Renders any variant as plain text
///
/// What a serializer needs to emit a field value without knowing the field's type: numbers,
/// strings and anything with a conversion to string come out as themselves, and objects go
/// through the formatter. A value with no text form produces an empty string.
///
/// @param out Receives the rendered value; any existing value is destroyed first
/// @param v Variant to render
void logVarText(_Inout_ string* out, _In_ const stvar* v);

/// Formats a timestamp per dateFormat/flags.
///
/// Produces an empty string when LOG_OmitDate is set. Unlike the level and channel prefixes,
/// the date does not carry a leading space -- it is the first thing on the line -- so a
/// serializer that omits it has to drop the space belonging to whatever now comes first.
///
/// @param out Receives the formatted date; any existing value is destroyed first
/// @param dateFormat One of the LOG_DATE_FORMATS values
/// @param flags Bitwise OR of LOG_FLAGS values (LOG_LocalTime and LOG_OmitDate are consulted)
/// @param timestamp Wall clock timestamp to format
void logFormatDate(_Inout_ string* out, int dateFormat, uint32 flags, int64 timestamp);

/// Formats a level prefix per flags (LOG_OmitLevel, LOG_ShortLevel, LOG_BracketLevel,
/// LOG_JustifyLevel). Produces an empty string when LOG_OmitLevel is set.
/// @param out Receives the formatted level prefix; any existing value is destroyed first
/// @param level Log severity level
/// @param flags Bitwise OR of LOG_FLAGS values
void logFormatLevel(_Inout_ string* out, int level, uint32 flags);

/// Formats a channel prefix per flags (LOG_IncludeChannel, LOG_BracketChannel). Produces
/// an empty string when the channel is omitted, NULL, or unnamed.
/// @param out Receives the formatted channel prefix; any existing value is destroyed first
/// @param chan Channel, or NULL for default
/// @param flags Bitwise OR of LOG_FLAGS values
void logFormatChannel(_Inout_ string* out, _In_opt_ LogChannel* chan, uint32 flags);

/// @}  // end of log_serializer group

CX_C_END
