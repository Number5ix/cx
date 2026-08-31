#pragma once
/// @file log/logforward.h
/// @brief Forwarding log records to another instance

/// @defgroup log_forward Forwarding
/// @ingroup log
/// @{
///
/// A destination that encodes the records it receives and hands the bytes to your transport.
///
/// **cx owns none of the transport.** It does not listen, connect, read, write or authenticate.
/// You supply a `send` callback; cx supplies the codec, a bounded spool for when `send` says no,
/// loop prevention, and the destination plumbing. Your connection stays entirely yours, so log
/// traffic can share a stream you already have, sit inside your own authentication, or travel
/// over something that is not a socket.
///
/// @code
///   static bool mySend(void *ctx, const uint8 *buf, size_t len)
///   {
///       return myTransportWrite(ctx, buf, len);   // false means "not now"
///   }
///   static const LogForwardHandlers kHandlers = { .send = mySend };
///
///   LogForwarder *fwd = logforwardRegister(LOG_Info, _SL("app/**"), &kHandlers, conn, NULL);
///   ...
///   logForwardResume(fwd);   // when the transport can take more
/// @endcode
///
/// @section log_forward_backpressure Backpressure
///
/// `send` returns false for "not now". Everything from that record on is spooled until
/// logForwardResume(), which drains the spool through `send` again. The spool is bounded: past
/// its limit the **oldest** records are dropped and a gap record takes their place, so what the
/// receiver ends up with is the newest traffic plus an honest statement of what is missing.
///
/// The spool is memory only, and it is a second copy. Local destinations are the system of
/// record; forwarding is layered on top of them.
///
/// @section log_forward_loops Loops
///
/// A forwarder's `send` runs transport code, transport code logs, and those records would come
/// back to the forwarder. Left alone that loop sustains itself with no application activity and
/// does not recover, so cx closes it in two places:
///
/// - **cx's own transport is never forwarded.** A forwarder does not receive anything logged to
///   `cx/net` or beneath it, however its filter is written. This is not configurable. It costs
///   the central copy of cx's network diagnostics, which still reach every local destination.
/// - **Anything logged inside a log-owned callback stays local.** That covers `send` itself, and
///   withLogLocal() extends it to your own code.
///
/// Neither reaches an application transport of your own that logs about its sends from another
/// thread after the send returned. If you have one, do not subscribe a forwarder to its channel.
///
/// Records refused for a loop reason are counted; see logForwardStats().
///
/// @section log_forward_sub Subscription
///
/// **A forwarder ships nothing until a receiver asks.** There is no locally configured "forward
/// everything to host X": the level and channel filter given at registration say what this process
/// is *willing* to send, and a subscription arriving from the far end says what it actually wants
/// within that. Until one does, the forwarder is silent and its call sites cost what they cost
/// with nobody listening.
///
/// A subscription is applied either from the bytes a receiver sent -- logForwardRecv() -- or
/// directly, for an application with a control plane of its own -- logForwardApplySub(). The two
/// produce identical routing.
///
/// Applying one re-binds the destination, so channel filters, per-channel levels and the call-site
/// gate all recompute: a subsystem nobody has subscribed to costs nothing, and one that is
/// subscribed to at Debug starts producing records that were compiled in but dormant. That is
/// fleet-wide verbosity control per subsystem, at runtime, paid for only where somebody is
/// listening.

#include <cx/log/log.h>
#include <cx/log/logwire.h>

CX_C_BEGIN

/// Opaque handle to a registered forwarder
typedef struct LogForwarder LogForwarder;

/// What a forwarder needs from your transport
///
/// The table is borrowed, not copied, so it must outlive the forwarder. A `static const` one is
/// the usual shape.
typedef struct LogForwardHandlers {
    /// Hand a run of complete frames to the transport
    ///
    /// Never called with part of a frame. Return false for "not now": cx spools this run and
    /// everything after it until logForwardResume(). Returning true means the transport has taken
    /// all of it.
    ///
    /// Called on the `remote` drain thread, and from logForwardResume(),
    /// logForwardConnected() and logForwardDisconnected() on whichever thread called those.
    /// Anything logged inside it stays on this machine.
    bool (*send)(_In_opt_ void* ctx, _In_reads_bytes_(len) const uint8* buf, size_t len);

    /// Called once when the forwarder is unregistered; optional
    ///
    /// Must not log: it can run with the log system's configuration lock held.
    void (*close)(_In_opt_ void* ctx);
} LogForwardHandlers;

/// Bytes of spooled frames a forwarder holds by default before it starts dropping the oldest
#define LOG_FORWARD_SPOOL_DEFAULT (4 * 1024 * 1024)

/// Bytes per spool segment by default
///
/// The spool drops whole segments, and each one repeats the declarations it needs, so a small
/// value turns a long outage into mostly declarations while a large one makes each drop coarse.
#define LOG_FORWARD_SEGMENT_DEFAULT (256 * 1024)

/// Instances a record may pass through by default before a forwarder refuses to pass it on again
#define LOG_FORWARD_MAXHOPS_DEFAULT 4

/// Optional forwarder settings; zero in any field takes the default
typedef struct LogForwardConfig {
    strref origin;       ///< This instance's identity on the wire
    uint64 spoolbytes;   ///< Spooled bytes held before the oldest are dropped
    uint64 segbytes;     ///< Bytes per spool segment
    uint32 maxhops;      ///< Refuse a record that has already travelled this far
} LogForwardConfig;

/// What a forwarder has done so far
typedef struct LogForwardStats {
    uint64 sent;      ///< Records handed to the transport
    uint64 spooled;   ///< Records held because the transport was not taking any
    uint64 dropped;   ///< Records the spool evicted to stay within its bound
    uint64 looped;    ///< Records refused because forwarding them would close a loop
    uint64 failed;    ///< Records that could not be encoded
    uint64 pending;   ///< Bytes currently spooled
    bool subscribed;  ///< A receiver has asked for something and has not been unsubscribed
} LogForwardStats;

/// Register a forwarder
///
/// The forwarder is an ordinary log destination that happens to encode what it receives. It lands
/// in the `remote` drain group, so a transport that stalls cannot hold up the local file writes
/// you would need in order to find out why.
///
/// A forwarder starts **connected** but **unsubscribed**: it sends nothing until
/// logForwardRecv() or logForwardApplySub() says what a receiver wants. Call
/// logForwardDisconnected() as well if there is no transport yet.
///
/// @param maxlevel Most verbose level this forwarder may ever send; a subscription asking for
///                 more than this is clamped to it
/// @param chanfilter Channels this forwarder may ever send, as a path pattern; NULL means every
///                   unrestricted channel. A subscription narrows this and can never widen it.
/// @param handlers Transport callbacks; borrowed, must outlive the forwarder
/// @param ctx Passed back to the callbacks
/// @param config Optional settings; NULL takes every default
/// @return Forwarder handle, or NULL on failure
/// @code
///   LogForwarder *fwd = logforwardRegister(LOG_Info, _SL("app/**"), &kHandlers, conn, NULL);
/// @endcode
_Ret_opt_valid_ LogForwarder* logforwardRegister(int maxlevel, _In_opt_ strref chanfilter,
                                                 _In_ const LogForwardHandlers* handlers,
                                                 _In_opt_ void* ctx,
                                                 _In_opt_ const LogForwardConfig* config);

/// A forwarder's underlying destination
///
/// For the destination-level calls a forwarder has no wrapper of its own -- extra filter rules
/// with logDestAddFilter(), a different drain group with logDestSetGroup(). Do not unregister it
/// directly; use logforwardUnregister().
///
/// @param fwd Forwarder to inspect
/// @return Its destination handle
/// @code
///   logDestAddFilter(logForwardDest(fwd), _SL("app/debug/**"), true);
/// @endcode
_Ret_valid_ LogDest* logForwardDest(_In_ LogForwarder* fwd);

/// Unregister a forwarder
///
/// Anything still spooled is discarded. The `close` handler runs once the log system has finished
/// with the destination.
///
/// @param fwd Forwarder to unregister; invalid afterwards
void logforwardUnregister(_Pre_valid_ _Post_invalid_ LogForwarder* fwd);

/// Tell a forwarder the transport can take more
///
/// Drains the spool through `send` until it is empty or `send` refuses again. Callable from any
/// thread.
///
/// @param fwd Forwarder to resume
void logForwardResume(_In_ LogForwarder* fwd);

/// Tell a forwarder its connection is gone
///
/// Records are spooled from this point instead of being sent, and the segment in progress is
/// closed so that what is spooled stays decodable on its own.
///
/// @param fwd Forwarder that lost its connection
void logForwardDisconnected(_In_ LogForwarder* fwd);

/// Tell a forwarder it has a connection again
///
/// Replays whatever is spooled through `send`, then goes live. If `send` refuses during the
/// replay the rest stays spooled until logForwardResume().
///
/// @param fwd Forwarder that has reconnected
void logForwardConnected(_In_ LogForwarder* fwd);

/// Take spooled frames instead of being handed them
///
/// For a transport that would rather pull. Hands back the oldest run of complete frames and
/// removes it from the spool. Wrap whatever you then do with the bytes in withLogLocal(), so that
/// a transport which logs about its own sends cannot feed itself.
///
/// @param fwd Forwarder to take from
/// @param out Receives the frames, replacing anything already in the buffer
/// @return false if nothing is spooled
/// @code
///   Buffer frames = 0;
///   while (logForwardTake(fwd, &frames)) {
///       withLogLocal() { myTransportWrite(conn, frames->data, frames->len); }
///   }
///   bufDestroy(&frames);
/// @endcode
bool logForwardTake(_In_ LogForwarder* fwd, _Inout_ Buffer* out);

/// Feed a forwarder bytes its receiver sent
///
/// Control frames are applied; anything else is ignored, so this is safe to call with whatever
/// arrives on the connection. A malformed stream fails the call, after which the forwarder accepts
/// no more of it -- close the connection.
///
/// Call this from one thread at a time: it holds a decoder for the connection it is reading, and a
/// half-delivered frame belongs to whoever is feeding it. Everything else on a forwarder may be
/// called from any thread.
///
/// @param fwd Forwarder to feed
/// @param buf Bytes received
/// @param len Number of bytes
/// @return false if the receiver's stream is malformed
/// @code
///   if (!logForwardRecv(fwd, buf, n))
///       netsocketClose(sock);
/// @endcode
bool logForwardRecv(_In_ LogForwarder* fwd, _In_reads_bytes_(len) const uint8* buf, size_t len);

/// Apply a subscription directly
///
/// For an application whose control plane is its own. Identical in effect to the same
/// subscription arriving through logForwardRecv().
///
/// @param fwd Forwarder to configure
/// @param spec What to send; NULL unsubscribes, returning the forwarder to silence
/// @return false if the forwarder's destination is no longer registered
/// @code
///   LogSubSpec spec = { .maxlevel = LOG_Info };
///   saInit(&spec.patterns, string, 1);
///   saPush(&spec.patterns, string, _S"app/db/**");
///   logForwardApplySub(fwd, &spec);
///   saDestroy(&spec.patterns);
/// @endcode
bool logForwardApplySub(_In_ LogForwarder* fwd, _In_opt_ const LogSubSpec* spec);

/// Encode this process's channel inventory
///
/// What an operator browses to find out what this binary is capable of logging, before deciding
/// what to subscribe to. Send the bytes back over the same connection.
///
/// Channels appear as they are interned, which for most is the first time something logs to them.
/// Call sites are not included.
///
/// @param fwd Forwarder to describe
/// @param out Receives the frames, replacing anything already in the buffer
/// @return false if the catalog could not be encoded
/// @code
///   Buffer frames = 0;
///   logForwardCatalog(fwd, &frames);
///   myTransportWrite(conn, frames->data, frames->len);
///   bufDestroy(&frames);
/// @endcode
bool logForwardCatalog(_In_ LogForwarder* fwd, _Inout_ Buffer* out);

/// Read a forwarder's counters
///
/// @param fwd Forwarder to inspect
/// @param out Receives the counters
void logForwardStats(_In_ LogForwarder* fwd, _Out_ LogForwardStats* out);

/// @}  // end of log_forward group

CX_C_END
