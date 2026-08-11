#pragma once
/// @file log/log.h
/// @brief Core logging system API

/// @defgroup log_core Core Logging API
/// @ingroup log
/// @{
///
/// A log call names a level, a message, and whatever typed arguments go with it. It never
/// carries, acquires, or looks up a logger object: where a record goes is a property of its
/// channel and of the destinations that asked for that channel, configured in one place and
/// invisible at the call site.
///
/// @code
///   logStr(Info, _SL("Application started"));
///   logFmt(Warn, _SL("Invalid value: ${int}"), stvar(int32, value));
/// @endcode
///
/// Logging at a level nobody is listening to is cheap enough to leave in place, and a level
/// disabled at build time by DEBUG_LEVEL is free -- it and everything it would have computed are
/// compiled out entirely (see @ref log_macros).
///
/// **A record is not a line.** A log call formats nothing: it copies its arguments, hands the
/// entry off and returns. The template is expanded later, on the thread that does the writing,
/// and only if a text destination actually needs it. The same record reaches a structured
/// destination as named fields instead, so JSON and human-readable output are one call site and
/// one system rather than two parallel ones (see LogRecord and @ref log_serializer).
///
/// **Channels:**
/// Channels are hierarchical `/`-separated paths that logs are routed by. They are interned, so
/// the same path always gives back the same channel, and permanent, so the pointer can be cached
/// at a call site:
/// @code
///   LogChannel *netchan = logChan(_SL("net/http"));
///   logStrC(Info, netchan, _SL("Connection established"));
/// @endcode
/// A source file that always logs to one channel can bind it with LOG_CHANNEL instead; see the
/// log_macros group.
///
/// **The `cx` channel:** everything the framework logs about itself goes under `cx` -- `cx/net`,
/// `cx/log/stats`, and so on -- and that channel is declared LOG_Restricted before anything else
/// can name it. A destination reaches the subtree only by naming it literally, so a program
/// that registers a plain console sees its own logging and nothing else, and one that wants the
/// framework's view asks for it:
/// @code
///   logconsoleRegister(LOG_Info, NULL, lcd);         // the application's own channels only
///   logfileRegister(LOG_Diag, _SL("cx/**"), lfd);    // ...and cx's, in a file of their own
/// @endcode
///
/// **Destinations:**
/// A destination is where records are written -- a file, a console, a memory buffer. It is
/// registered with a maximum level and a channel pattern, and any number of them can be active
/// at once:
/// @code
///   LogFileData *lfd = logfileCreate(vfs, _SL("app.log"), &config, NULL);
///   LogDest *dest = logfileRegister(LOG_Info, _SL("net/**"), lfd);
///
///   // Later, unregister when done
///   logUnregisterDest(dest);
/// @endcode
/// Patterns are matched once, when the destination is bound, so filtering can be as expressive
/// as it likes without costing a call site anything.
///
/// **Threads:** writing happens on a drain thread rather than on the thread that logged. There
/// is one per **group**, and every destination is in the `default` group unless it is moved, so
/// a program that never mentions groups behaves as though there were a single background thread.
/// Moving the expensive destinations into a group of their own is what keeps a slow one -- a
/// file rotating, a disk flushing -- from holding up the console (see @ref log_group).
///
/// **Batching:**
/// Batch multiple log messages together to ensure they appear consecutively in output, even
/// while other threads are logging:
/// @code
///   logBatchBegin();
///   logStr(Info, _SL("Starting operation"));
///   logStr(Info, _SL("Step 1 complete"));
///   logStr(Info, _SL("Step 2 complete"));
///   logBatchEnd();
/// @endcode
///
/// Beyond this: @ref log_ctx attaches correlation fields to everything logged on a thread,
/// @ref log_ring retains records nothing asked for so a later failure can claim them, and
/// @ref log_volume decides what gives when there is more to log than there is room for.

#include <cx/cx.h>
#include <cx/string/strbase.h>
#include <cx/stype/stvar.h>
#include <cx/thread/atomic.h>

CX_C_BEGIN

/// Log severity levels
///
/// Levels are ordered from most to least severe. When registering a destination with a
/// maximum level, all messages at that level and below (more severe) will be delivered.
enum LOG_LEVEL_ENUM {
    LOG_Fatal,     ///< Fatal errors, application cannot continue
    LOG_Error,     ///< Non-fatal errors requiring attention
    LOG_Warn,      ///< Warning conditions that may indicate problems
    LOG_Notice,    ///< Normal but significant conditions
    LOG_Info,      ///< Informational messages
    LOG_Verbose,   ///< Detailed informational messages
    LOG_Diag,      ///< Release build diagnostics not normally needed
    LOG_Debug,     ///< Debug messages (compiled out of non-development builds)
    LOG_Trace,     ///< Detailed trace messages (only available in debug builds)

    LOG_Count      // Not a real level, used for array sizing
};

/// Array of log level names as strings (e.g., "Fatal", "Error", etc.)
extern strref LogLevelNames[];

/// Array of single-character log level abbreviations (e.g., "F", "E", etc.)
extern strref LogLevelAbbrev[];

/// Channel is visible to any destination rule that matches its path
///
/// This is the default, and it is inherited down the path from the root, so interning a channel
/// from a call site never makes output disappear. It is also the flag that re-opens a subtree
/// beneath a restricted parent.
#define LOG_Broadcast 0x00000001

/// Channel is restricted: a destination rule reaches it only by naming this node literally
///
/// This is the default for an explicit logDeclareChan(), because declaring a channel is the act
/// of carving out a stream. The restriction gates the subtree at this node, not at every channel
/// beneath it: with `net` restricted, a rule of `net/**` sees all of `net/http/request`, while a
/// bare `**` sees none of it. Restriction changes permission, never reach.
#define LOG_Restricted 0x00000002

/// Log channel for filtering and organizing log messages
///
/// Channels are the routing key of the log system. They are named by a `/`-separated
/// hierarchical path (`net/http/request`), interned in a process-wide registry so that the same
/// path always yields the same channel, and permanent for the lifetime of the process -- a
/// channel pointer cached at a call site never dangles, even across logShutdown()/logRestart().
///
/// Obtain one with logChan(); declare policy for one with logDeclareChan().
typedef struct LogChannel {
    string path;                 ///< Full channel path
    struct LogChannel* parent;   ///< Parent channel, NULL for the root channel
    uint32 idx;                  ///< Stable index into the routing table
    uint32 gatedepth;            ///< Depth of the deepest restricted ancestor-or-self
    flags_t flags;               ///< Visibility and policy flags

    /// Highest level anything wants on this channel, -1 for none
    ///
    /// This is the gate the call site checks, and it accounts for retention rings as well as
    /// destinations: a ring wants entries no destination asked for, and an entry that fails this
    /// check never exists at all (see @ref log_ring).
    atomic(int32) maxlevel;

    /// Highest level a *destination* wants on this channel, -1 for none
    ///
    /// The same number as maxlevel whenever no ring is open. The difference between the two is
    /// exactly the set of records that exist only because something is retaining them, which is
    /// what the retroactive debug ring keeps and what it would otherwise duplicate.
    atomic(int32) destlevel;

    /// Retroactive debug ring covering this channel, or NULL; see logChanSetDebugRing()
    ///
    /// Inherited down the path, so configuring one on `net` covers `net/http/request` too.
    atomic(ptr) ring;

    bool ownring;   ///< This channel's ring is its own, not inherited

    /// Drain groups this channel's destinations live in, one bit per group
    ///
    /// Maintained automatically as destinations are registered and moved between groups; there
    /// is normally no need to read or set this directly.
    atomic(uint32) groupmask;

    atomic(uint32) sample;      ///< Keep one record in N; 0 or 1 is every record
    atomic(uint32) samplecnt;   ///< Arrivals so far, for the sampling decision
} LogChannel;

/// Opaque handle to a registered log destination
typedef struct LogDest LogDest;

/// Per-call-site state, declared by the log macros in a block of their own
///
/// Every log macro opens a block containing one of these. Its **address** is a stable identity
/// for the call site: unique, never reused, valid for the lifetime of the process, and
/// disclosing nothing -- unlike `__FILE__`/`__LINE__`, which cx deliberately keeps out of the
/// binary by default. Source location is a separate, opt-in concern and nothing here depends on
/// it.
///
/// The counters exist for the rate-limiting macro variants (logStrOnce(), logStrEveryN(),
/// logStrEveryT()); a call site that never uses one only ever contributes its address.
///
/// @par Aside: log macros in header functions
/// Because the site is a static object, a log macro cannot appear in a `_meta_inline` function:
/// C forbids an inline definition with external linkage from containing one, so gcc and clang
/// reject it outright. A header function that wants to log has to be plain `static` instead --
/// which is no loss, since `_meta_inline` is for small fragments that are really just
/// stronger-typed macros. Code that genuinely needs to log without a site can call `_logStr()` or
/// `_logFmt()` with a NULL one; it forgoes rate limiting and any per-site behavior, and the
/// resulting record carries no call site identity.
typedef struct LogSite {
    atomic(uint32) count;   ///< times a gated call site has been reached
    atomic(uint32) last;    ///< coarse timestamp of the last emission, in milliseconds
} LogSite;

/// Rate-limiting policy applied to a call site
///
/// Always a compile-time constant at the call site, so the ungated case folds away entirely.
enum LOG_SITE_GATE {
    LOG_SiteAlways = 0,   ///< no rate limiting
    LOG_SiteOnce,         ///< emit only the first time this call site is reached
    LOG_SiteEveryN,       ///< emit every Nth time this call site is reached
    LOG_SiteEveryT,       ///< emit at most once per interval
};

/// Default log channel used when no channel is specified
extern LogChannel* LogDefault;

/// Opaque handle to a log context; see logctx.h
typedef struct LogCtx LogCtx;

/// Look up a log channel by path, creating it if it does not exist yet
///
/// The same path always returns the same channel, so a call site can look it up once, cache the
/// pointer, and never look it up again. Channels are permanent: the pointer stays valid for the
/// lifetime of the process, even across logShutdown() and logRestart().
///
/// Paths are `/`-separated and hierarchical; every ancestor along the path is interned too.
/// Path components may not contain `/`, `*`, `[` or `]`. A channel created this way inherits its
/// parent's visibility, so naming a path that nobody has configured behaves exactly like an
/// unnamed log line.
///
/// @param path Channel path, e.g. `net/http/request`
/// @return Channel handle, or NULL if the logging system is not initialized
/// @code
///   LogChannel *netchan = logChan(_SL("net/http"));
///   logStrC(Info, netchan, _SL("Connection established"));
/// @endcode
_Ret_opt_valid_ LogChannel* logChan(_In_ strref path);

/// Declare policy for a log channel
///
/// Like logChan(), but attaches policy to the channel rather than merely naming it. With no
/// flags the channel becomes LOG_Restricted, gating itself and its subtree: declaring a channel
/// is the act of carving out a stream that goes somewhere specific, so a destination has to ask
/// for it by name. Pass LOG_Broadcast to re-open a subtree beneath a restricted parent.
///
/// Declaring a channel that already exists changes its policy, including for channels beneath it
/// that already exist.
///
/// @param path Channel path
/// @param flags LOG_Broadcast, LOG_Restricted, or 0 for the LOG_Restricted default
/// @return Channel handle, or NULL if the logging system is not initialized
/// @code
///   logDeclareChan(_SL("audit"), 0);              // audit and its subtree are restricted
///   logDeclareChan(_SL("audit/public"), LOG_Broadcast);   // ...except this
/// @endcode
_Ret_opt_valid_ LogChannel* logDeclareChan(_In_ strref path, flags_t flags);

// Drain-owned cache of a record's flat rendering, shared between the destinations of one
// dispatch. Not part of the destination interface; use logRecordRender().
typedef struct LogRenderCache {
    string str;
    bool valid;
} LogRenderCache;

/// One log record as a destination sees it
///
/// A record is **not** a formatted line. It carries the message template and a copy of the
/// arguments that were logged with it, so that a text destination renders a sentence and a
/// structured destination emits named fields -- from the same record, with no parallel API and
/// no possibility of a structured-only record reaching a console.
///
/// Formatting therefore happens here, on the drain thread, rather than at the call site. Call
/// logRecordRender() to get the flat text; repeat calls within one dispatch are cheap because
/// the rendering is shared across destinations.
///
/// Structured destinations read `args` directly. Keyed arguments (stvark()) become named fields;
/// unkeyed ones are positional and belong to the template. Note that the two are disjoint --
/// a keyed argument is never matched by an unkeyed placeholder, so both have to be read through
/// their own accessors.
///
/// `ctx` carries the fields that were in scope on the logging thread (see @ref log_ctx). They
/// are fields exactly like keyed arguments are, from a different source, and logRecordRender()
/// makes them available to the template under their keys -- so `${string:reqid}` resolves against
/// the context when the call site did not supply a `reqid` argument of its own.
typedef struct LogRecord {
    int level;             ///< Log severity level (LOG_Fatal, LOG_Error, etc.)
    LogChannel* chan;      ///< Channel this record was logged to, never NULL
    int64 timestamp;       ///< Wall clock timestamp when the record was created
    uint64 seq;            ///< Increasing sequence number, ordering records across threads
    const LogSite* site;   ///< Call site identity, or NULL if logged dynamically
    strref msgtmpl;        ///< Message template, or the literal message when istmpl is false
    const stvar* args;     ///< Arguments the record was logged with, NULL if none
    int nargs;             ///< Number of arguments

    /// Severity of the event that released this record from a retention ring, or -1
    ///
    /// A record with a trigger was retained by a debug ring and let out by something more severe
    /// (see logChanSetDebugRing()). Destinations filter on this instead of the record's own
    /// level, so the context of a failure reaches whoever would have seen the failure. Its own
    /// level is unchanged and is still what a destination renders.
    int trigger;

    /// True if msgtmpl is a format template, false if it is a literal message
    ///
    /// logFmt() sets this and logStr() does not. It is what keeps a literal message containing
    /// `${...}` from being substituted into: an unformatted record has nothing to expand even
    /// when a context is in scope, and the two cannot be told apart by argument count.
    bool istmpl;

    const LogCtx* ctx;   ///< Context in scope when the record was logged, or NULL
    uint32 batchid;      ///< Opaque batch identifier for grouping related records

    /// Sampling rate this record survived, or 0/1 if the channel was not being sampled
    ///
    /// A structured destination scales counts back up by this (see logChanSetSampling()); text
    /// destinations ignore it, because a sampled log is a thing you configured and a line that
    /// says so every time is noise.
    uint32 sample;

    LogRenderCache* _cache;   // internal: shared rendering, use logRecordRender()
} LogRecord;

/// Render a record to flat text
///
/// Runs the template through the formatter with the record's arguments. Text destinations use
/// this instead of receiving a pre-formatted string, which is what moves the cost of formatting
/// off the thread that logged the message.
///
/// The result is cached for the duration of the dispatch, so several text destinations receiving
/// the same record only pay for one rendering between them.
///
/// @param out Receives the rendered text; any existing value is destroyed first
/// @param rec Record to render
/// @code
///   string line = 0;
///   logRecordRender(&line, rec);
///   ...
///   strDestroy(&line);
/// @endcode
void logRecordRender(_Inout_ string* out, _In_ const LogRecord* rec);

/// Callback function type for log destinations
///
/// This function is called for each log record that passes the destination's level
/// and channel filters. Records with the same batchid should be kept together when
/// possible (e.g., not split across log file rotations).
///
/// @param rec The log record; valid only for the duration of the call
/// @param userdata User-provided context pointer from logRegisterDest()
typedef void (*LogDestMsg)(_In_ const LogRecord* rec, _In_opt_ void* userdata);

/// Callback function type for batch completion notification
///
/// Called after all messages in a batch have been delivered to the destination.
/// Destinations can use this to flush buffers or perform cleanup after a batch.
///
/// @param batchid The batch that was completed
/// @param userdata User-provided context pointer from logRegisterDest()
typedef void (*LogDestBatchDone)(uint32 batchid, _In_opt_ void* userdata);

/// Callback function type for destination cleanup
///
/// Called when a destination is unregistered. The destination should release
/// any resources it holds.
///
/// @param userdata User-provided context pointer from logRegisterDest()
typedef void (*LogDestClose)(_In_opt_ void* userdata);

/// Register a new log destination
///
/// Registers callbacks that will receive log messages matching the specified level
/// and channel filter. Multiple destinations can be registered simultaneously.
///
/// The channel filter is a path pattern, matched once when the destination is bound rather than
/// per message, so its expressiveness costs nothing at a call site:
///
/// - `net` matches the channel `net` exactly, and nothing beneath it
/// - `net/*` matches the immediate children of `net`
/// - `net/**` matches `net` and its entire subtree
/// - `**` matches everything, and is what a NULL filter means
///
/// **Restricted channels.** Matching a restricted channel (LOG_Restricted) is not enough on its
/// own: the pattern also has to name the gate, meaning the part of it before its first wildcard
/// must spell out the restricted channel's own path. A wildcard cannot stand in for any of those
/// components, so `**` -- which names nothing at all -- never reaches a restricted subtree:
/// @code
///   logDeclareChan(_SL("audit"), 0);               // audit and its subtree are restricted
///
///   logRegisterDest(LOG_Info, NULL, ...);              // `**`: sees nothing under audit
///   logRegisterDest(LOG_Info, _SL("*/**"), ...);       // still nothing: no literal `audit`
///   logRegisterDest(LOG_Info, _SL("audit/**"), ...);   // names audit: the whole subtree
///   logRegisterDest(LOG_Info, _SL("audit/db"), ...);   // names audit: that one channel
/// @endcode
/// Gates nest. Declaring `audit/keys` restricted as well puts a second gate below the first, and
/// a pattern must clear the deepest one that applies -- so `audit/**` still covers `audit/db`,
/// but only a pattern spelling out `audit/keys` reaches that branch.
///
/// Add further include/exclude rules with logDestAddFilter().
///
/// @param maxlevel Maximum log level to receive (e.g., LOG_Info receives Fatal through Info)
/// @param chanfilter Channel path pattern, or NULL for every unrestricted channel
/// @param msgfunc Callback invoked for each log message
/// @param batchfunc Optional callback invoked when a batch completes
/// @param closefunc Optional callback invoked when destination is unregistered
/// @param userdata User context pointer passed to all callbacks
/// @return Destination handle for later unregistration, or NULL on failure
/// @code
///   LogDest *dest = logRegisterDest(LOG_Info, _SL("net/**"), myMsgFunc, NULL, NULL, &mydata);
/// @endcode
_Ret_opt_valid_ LogDest*
logRegisterDest(int maxlevel, _In_opt_ strref chanfilter, _In_ LogDestMsg msgfunc,
                _In_opt_ LogDestBatchDone batchfunc, _In_opt_ LogDestClose closefunc,
                _In_opt_ void* userdata);

/// Add an include or exclude rule to a registered destination
///
/// Rules compose with most-specific-wins precedence: the matching rule with the most path
/// components before its first wildcard decides, and an exclude wins a tie. A destination with
/// no rules at all receives every unrestricted channel.
///
/// @param dhandle Destination handle returned from logRegisterDest()
/// @param pattern Channel path pattern (see logRegisterDest())
/// @param exclude true for an exclude rule, false for an include rule
/// @return false if the destination is not registered
/// @code
///   LogDest *dest = logRegisterDest(LOG_Debug, _SL("net/**"), myMsgFunc, NULL, NULL, NULL);
///   logDestAddFilter(dest, _SL("net/http/**"), true);   // ...but not the HTTP subtree
/// @endcode
bool logDestAddFilter(_In_ LogDest* dhandle, _In_ strref pattern, bool exclude);

/// Unregister a log destination
///
/// Removes the destination from the logging system and calls its close callback if provided.
/// The destination handle becomes invalid after this call.
///
/// @param dhandle Destination handle returned from logRegisterDest()
/// @return true if destination was found and removed, false otherwise
bool logUnregisterDest(_Pre_valid_ _Post_invalid_ LogDest* dhandle);

/// Flush all pending log messages
///
/// Blocks until all queued log messages have been processed by all destinations.
/// Useful before critical operations or shutdown to ensure logs are written.
void logFlush(void);

/// Shutdown the logging system
///
/// Flushes all pending logs and unregisters all destinations. Channels are permanent and are
/// deliberately kept, so a cached channel pointer stays valid across a shutdown/restart cycle.
/// After shutdown, logging calls will be ignored until logRestart() is called.
void logShutdown(void);

/// Restart the logging system after shutdown
///
/// Reinitializes the logging system after a previous logShutdown() call. This allows
/// logging to resume after being explicitly stopped.
void logRestart(void);

/// Begin a log batch
///
/// Groups subsequent log messages into a batch that will be delivered together.
/// Batches can be nested; only when the outermost batch ends will messages be sent.
///
/// @code
///   logBatchBegin();
///   logStr(Info, _SL("Operation started"));
///   logStr(Info, _SL("Step 1 complete"));
///   logStr(Info, _SL("Step 2 complete"));
///   logBatchEnd();  // All three messages delivered together
/// @endcode
void logBatchBegin(void);

/// End a log batch
///
/// Completes a log batch started with logBatchBegin(). When the outermost batch ends,
/// all batched messages are queued for delivery to destinations.
void logBatchEnd(void);

/// Test whether anything is listening to a channel at a given level
///
/// Guards computation that is only needed in order to log. This is the same check the log macros
/// make internally, so it costs one relaxed atomic load and no allocation.
///
/// @param level Log level constant, e.g. LOG_Debug
/// @param chan Channel to test, or NULL for the default channel
/// @return true if at least one destination would receive a message at this level
/// @code
///   if (logWouldLog(LOG_Debug, netchan))
///       logFmtC(Debug, netchan, _SL("state: ${string}"), stvar(string, expensiveDump()));
/// @endcode
_meta_inline bool logWouldLog(int level, _In_opt_ LogChannel* chan)
{
    if (!chan)
        chan = LogDefault;
    return level <= atomicLoad(int32, &chan->maxlevel, Relaxed);
}

/// @} // end of log_core group

/// @defgroup log_macros Log Message Macros
/// @ingroup log
/// @{
///
/// These macros provide the primary interface for logging messages. They compile to
/// no-ops for levels that are disabled based on DEBUG_LEVEL, ensuring zero overhead
/// for disabled log levels.
///
/// **Debug Level Filtering:**
/// - `DEBUG_LEVEL >= 2`: All levels including Trace
/// - `DEBUG_LEVEL >= 1`: Debug and above (no Trace)
/// - `DEBUG_LEVEL == 0`: Diag and above (no Debug or Trace)
///
/// **Dev variants:**
/// The `Dev` prefix variants (e.g., `logStr(DevInfo, ...)`) are only compiled in
/// development builds and map to their corresponding regular levels in production.
///
/// **Binding a source file to a channel:**
/// `logStr` and `logFmt` log to whatever `LOG_CHANNEL` names, which is the default channel
/// unless the file says otherwise. A translation unit declares its channel once, near the top,
/// and every call site in the file picks it up with no logger object carried anywhere:
/// @code
///   #include <cx/log.h>
///
///   #undef LOG_CHANNEL
///   #define LOG_CHANNEL chan_net_http    // a LogChannel* from logChan()
/// @endcode
/// The `#undef` is needed because log.h supplies the default. Use logStrC()/logFmtC() where the
/// channel is chosen at runtime rather than per file.
///
/// **These macros are statements, not expressions.** Each opens a block holding the call site's
/// LogSite (see @ref log_ratelimit), so a log call cannot appear where an expression is required
/// -- as the operand of `?:`, for instance. That form fails to compile rather than misbehaving.

/// void logStr(level, str)
///
/// Log a string message using the file's channel (LOG_CHANNEL, default if unset)
/// @param level Log level without LOG_ prefix (e.g., Info, Warn, Error)
/// @param str String or string reference to log
/// @code
///   logStr(Info, _SL("Application started"));
///   logStr(Error, errorMessage);
/// @endcode
#define logStr(level, str) _logStr_##level(LOG_##level, LOG_CHANNEL, LOG_SiteAlways, 0, str)

/// void logStrC(level, chan, str)
///
/// Log a string message with a specific channel
/// @param level Log level without LOG_ prefix (e.g., Info, Warn, Error)
/// @param chan LogChannel pointer
/// @param str String or string reference to log
/// @code
///   LogChannel *netchan = logChan(_SL("net/http"));
///   logStrC(Info, netchan, _SL("Connection established"));
/// @endcode
#define logStrC(level, chan, str) _logStr_##level(LOG_##level, chan, LOG_SiteAlways, 0, str)

/// void logFmt(level, fmt, ...)
///
/// Log a formatted message using the file's channel (LOG_CHANNEL, default if unset)
/// @param level Log level without LOG_ prefix (e.g., Info, Warn, Error)
/// @param fmt Format string (see @ref string_format for format syntax)
/// @param ... Format arguments wrapped in stvar(); at least one is required
///
/// A template may also name context fields by key (see @ref log_ctx). Such a template needs no
/// arguments of its own, but the macro still needs one: pass stvNone, which the formatter never
/// matches and a structured destination never emits.
///
/// @code
///   logFmt(Info, _SL("Connection from ${string}:${int}"),
///          stvar(string, hostname), stvar(int32, port));
///   logFmt(Warn, _SL("Invalid value: ${int}"), stvar(int32, value));
///   logFmt(Info, _SL("handling ${string:reqid}"), stvNone);   // reqid comes from the context
/// @endcode
#define logFmt(level, fmt, ...) _logFmtArgs(level, LOG_CHANNEL, LOG_SiteAlways, 0, fmt, __VA_ARGS__)

/// void logFmtC(level, chan, fmt, ...)
///
/// Log a formatted message with a specific channel
/// @param level Log level without LOG_ prefix
/// @param chan LogChannel pointer
/// @param fmt Format string
/// @param ... Format arguments wrapped in stvar()
/// @code
///   LogChannel *dbchan = logChan(_SL("db"));
///   logFmtC(Warn, dbchan, _SL("Query took ${int}ms"), stvar(int32, elapsed));
/// @endcode
#define logFmtC(level, chan, fmt, ...) _logFmtArgs(level, chan, LOG_SiteAlways, 0, fmt, __VA_ARGS__)

/// @}  // end of log_macros group

/// @defgroup log_ratelimit Rate-Limited Log Macros
/// @ingroup log
/// @{
///
/// Variants that emit only some of the times their call site is reached. The state lives in the
/// call site's own LogSite, so no registry, allocation, or key derivation is involved and two
/// call sites never share a budget -- not even two identical messages in the same function.
///
/// **The level check happens first.** A gate is only consumed when something is actually
/// listening at that level on that channel, so a logStrOnce() in startup code does not spend its
/// single emission on a message logged before any destination was registered.
///
/// **The counters are not serialized.** Two threads reaching a logStrEveryN() site at once can
/// both pass; making a rate limit exact needs a lock per call site, which costs more than the
/// handful of messages it saves. Treat the counts as approximate.

/// void logStrOnce(level, str)
///
/// Log a string message the first time this call site is reached, and never again
/// @param level Log level without LOG_ prefix
/// @param str String or string reference to log
/// @code
///   logStrOnce(Warn, _SL("hardware clock is not monotonic; using fallback"));
/// @endcode
#define logStrOnce(level, str) _logStr_##level(LOG_##level, LOG_CHANNEL, LOG_SiteOnce, 0, str)

/// void logStrEveryN(level, n, str)
///
/// Log a string message every Nth time this call site is reached, starting with the first
/// @param level Log level without LOG_ prefix
/// @param n Emit on every Nth arrival; values below 1 are treated as 1
/// @param str String or string reference to log
/// @code
///   logStrEveryN(Warn, 1000, _SL("packet checksum mismatch"));
/// @endcode
#define logStrEveryN(level, n, str) \
    _logStr_##level(LOG_##level, LOG_CHANNEL, LOG_SiteEveryN, (n), str)

/// void logStrEveryT(level, interval, str)
///
/// Log a string message at most once per interval, starting with the first arrival
/// @param level Log level without LOG_ prefix
/// @param interval Minimum time between emissions, in cx time units (e.g. timeS(10))
/// @param str String or string reference to log
/// @code
///   logStrEveryT(Warn, timeS(10), _SL("send queue is still over the high-water mark"));
/// @endcode
#define logStrEveryT(level, interval, str) \
    _logStr_##level(LOG_##level, LOG_CHANNEL, LOG_SiteEveryT, (interval), str)

/// void logFmtOnce(level, fmt, ...)
///
/// Log a formatted message the first time this call site is reached, and never again
/// @param level Log level without LOG_ prefix
/// @param fmt Format string
/// @param ... Format arguments wrapped in stvar()
/// @code
///   logFmtOnce(Warn, _SL("falling back to ${string}"), stvar(string, name));
/// @endcode
#define logFmtOnce(level, fmt, ...) \
    _logFmtArgs(level, LOG_CHANNEL, LOG_SiteOnce, 0, fmt, __VA_ARGS__)

/// void logFmtEveryN(level, n, fmt, ...)
///
/// Log a formatted message every Nth time this call site is reached
/// @param level Log level without LOG_ prefix
/// @param n Emit on every Nth arrival; values below 1 are treated as 1
/// @param fmt Format string
/// @param ... Format arguments wrapped in stvar()
/// @code
///   logFmtEveryN(Warn, 1000, _SL("dropped ${int} frames"), stvar(int32, ndropped));
/// @endcode
#define logFmtEveryN(level, n, fmt, ...) \
    _logFmtArgs(level, LOG_CHANNEL, LOG_SiteEveryN, (n), fmt, __VA_ARGS__)

/// void logFmtEveryT(level, interval, fmt, ...)
///
/// Log a formatted message at most once per interval
/// @param level Log level without LOG_ prefix
/// @param interval Minimum time between emissions, in cx time units (e.g. timeS(10))
/// @param fmt Format string
/// @param ... Format arguments wrapped in stvar()
/// @code
///   logFmtEveryT(Warn, timeS(5), _SL("queue depth ${int}"), stvar(int32, depth));
/// @endcode
#define logFmtEveryT(level, interval, fmt, ...) \
    _logFmtArgs(level, LOG_CHANNEL, LOG_SiteEveryT, (interval), fmt, __VA_ARGS__)

/// @}  // end of log_ratelimit group

// The channel logStr()/logFmt() use. A source file overrides it with #undef followed by its own
// #define; see the log_macros documentation above.
#define LOG_CHANNEL LogDefault

// Internal implementation functions used by macros - do not call directly
void _logStr(int level, int64 timestamp, _In_ LogChannel* chan, _In_opt_ const LogSite* site,
             _In_ strref str);
void _logFmt(int level, int64 timestamp, _In_ LogChannel* chan, _In_opt_ const LogSite* site,
             _In_ strref fmtstr, int n, _In_ stvar* args);

// Advances a call site's counters and decides whether it emits this time. Only called for gates
// other than LOG_SiteAlways.
bool _logSiteGate(_Inout_ LogSite* site, int gate, int64 garg);

_meta_inline bool _logSitePasses(_Inout_ LogSite* site, int level, _In_ LogChannel* chan, int gate,
                                 int64 garg)
{
    // gate is a constant at every call site, so an ungated call folds this away entirely and
    // costs exactly what it did before sites existed
    if (gate == LOG_SiteAlways)
        return true;

    // The level check comes first deliberately: a gate consumed while nothing is listening would
    // spend logStrOnce()'s single emission on a message that goes nowhere.
    return logWouldLog(level, chan) && _logSiteGate(site, gate, garg);
}

// Opens the block that holds the call site's LogSite. The static is declared here, inside each
// level's own macro, rather than by the outer macro, so that a level compiled out by DEBUG_LEVEL
// costs no storage either. The channel is bound to a local because the gated forms would
// otherwise evaluate the expression twice.
#define _logStrSite(level, chan, gate, garg, str)                          \
    do {                                                                   \
        static LogSite _log_site;                                          \
        LogChannel* _log_site_chan = (chan);                               \
        if (_logSitePasses(&_log_site, level, _log_site_chan, gate, garg)) \
            _logStr(level, -1, _log_site_chan, &_log_site, str);           \
    } while (0)

#define _logFmtSite(level, chan, gate, garg, fmt, nargs, args)                \
    do {                                                                      \
        static LogSite _log_site;                                             \
        LogChannel* _log_site_chan = (chan);                                  \
        if (_logSitePasses(&_log_site, level, _log_site_chan, gate, garg))    \
            _logFmt(level, -1, _log_site_chan, &_log_site, fmt, nargs, args); \
    } while (0)

// Collapses a variadic argument list before handing off to the per-level table, so that the
// table itself stays non-variadic and every user-facing logFmt variant is one line.
//
// count_macro_args() reports 1 for an empty list and there is no portable way to do better, so
// every logFmt() needs at least one argument; a template that substitutes only context fields
// passes stvNone. Expanding __VA_ARGS__ a second time to count it exactly is not an option --
// format.h records what that costs on MSVC.
#define _logFmtArgs(level, chan, gate, garg, fmt, ...) \
    _logFmt_##level(LOG_##level,                       \
                    chan,                              \
                    gate,                              \
                    garg,                              \
                    fmt,                               \
                    count_macro_args(__VA_ARGS__),     \
                    ((stvar[]) { __VA_ARGS__ }))

// Implementation macros for conditional compilation based on DEBUG_LEVEL. The disabled forms are
// variadic so that they cannot fall out of step with the enabled ones' arity; a level compiled
// out never expands its arguments at all, which is what keeps a Trace call site and everything
// it computes out of a release binary.
#if DEBUG_LEVEL >= 2
#define _logStr_Trace _logStrSite
#define _logFmt_Trace _logFmtSite
#else
#define _logStr_Trace(...) ((void)0)
#define _logFmt_Trace(...) ((void)0)
#endif

#if DEBUG_LEVEL >= 1
#define _logStr_Debug                                 _logStrSite
#define _logFmt_Debug                                 _logFmtSite
#define _logStr_DevDiag(level, chan, gate, garg, str) _logStrSite(LOG_Diag, chan, gate, garg, str)
#define _logFmt_DevDiag(level, chan, gate, garg, fmt, nargs, args) \
    _logFmtSite(LOG_Diag, chan, gate, garg, fmt, nargs, args)
#define _logStr_DevVerbose(level, chan, gate, garg, str) \
    _logStrSite(LOG_Verbose, chan, gate, garg, str)
#define _logFmt_DevVerbose(level, chan, gate, garg, fmt, nargs, args) \
    _logFmtSite(LOG_Verbose, chan, gate, garg, fmt, nargs, args)
#define _logStr_DevInfo(level, chan, gate, garg, str) _logStrSite(LOG_Info, chan, gate, garg, str)
#define _logFmt_DevInfo(level, chan, gate, garg, fmt, nargs, args) \
    _logFmtSite(LOG_Info, chan, gate, garg, fmt, nargs, args)
#define _logStr_DevNotice(level, chan, gate, garg, str) \
    _logStrSite(LOG_Notice, chan, gate, garg, str)
#define _logFmt_DevNotice(level, chan, gate, garg, fmt, nargs, args) \
    _logFmtSite(LOG_Notice, chan, gate, garg, fmt, nargs, args)
#define _logStr_DevWarn(level, chan, gate, garg, str) _logStrSite(LOG_Warn, chan, gate, garg, str)
#define _logFmt_DevWarn(level, chan, gate, garg, fmt, nargs, args) \
    _logFmtSite(LOG_Warn, chan, gate, garg, fmt, nargs, args)
#define _logStr_DevError(level, chan, gate, garg, str) _logStrSite(LOG_Error, chan, gate, garg, str)
#define _logFmt_DevError(level, chan, gate, garg, fmt, nargs, args) \
    _logFmtSite(LOG_Error, chan, gate, garg, fmt, nargs, args)
#else
#define _logStr_Debug(...)      ((void)0)
#define _logFmt_Debug(...)      ((void)0)
#define _logStr_DevDiag(...)    ((void)0)
#define _logFmt_DevDiag(...)    ((void)0)
#define _logStr_DevVerbose(...) ((void)0)
#define _logFmt_DevVerbose(...) ((void)0)
#define _logStr_DevInfo(...)    ((void)0)
#define _logFmt_DevInfo(...)    ((void)0)
#define _logStr_DevNotice(...)  ((void)0)
#define _logFmt_DevNotice(...)  ((void)0)
#define _logStr_DevWarn(...)    ((void)0)
#define _logFmt_DevWarn(...)    ((void)0)
#define _logStr_DevError(...)   ((void)0)
#define _logFmt_DevError(...)   ((void)0)
#endif

#define _logStr_Diag    _logStrSite
#define _logFmt_Diag    _logFmtSite
#define _logStr_Verbose _logStrSite
#define _logFmt_Verbose _logFmtSite
#define _logStr_Info    _logStrSite
#define _logFmt_Info    _logFmtSite
#define _logStr_Notice  _logStrSite
#define _logFmt_Notice  _logFmtSite
#define _logStr_Warn    _logStrSite
#define _logFmt_Warn    _logFmtSite
#define _logStr_Error   _logStrSite
#define _logFmt_Error   _logFmtSite
#define _logStr_Fatal   _logStrSite
#define _logFmt_Fatal   _logFmtSite

CX_C_END
