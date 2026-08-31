#pragma once
/// @file log/logwire.h
/// @brief Wire codec for log records: records in, framed bytes out, and back again

/// @defgroup log_wire Log Wire Codec
/// @ingroup log
/// @{
///
/// Turns a LogRecord into bytes that another cx instance can turn back into a LogRecord.
///
/// **cx never touches a wire.** There is no listening, connecting, reading, writing or
/// authenticating anywhere in the log system. This codec produces complete frames and hands them
/// to whoever asked for them; the application decides what a connection is, and may multiplex log
/// traffic into a stream it already has, wrap it in its own authentication, or carry it over
/// something that is not a socket at all. See @ref log_forward for the destination that drives
/// this codec, and logInject() for the receiving end.
///
/// @section log_wire_frames Frames and segments
///
/// A frame is a kind, a length, and a payload. Frames arrive in order and a decoder hands each
/// one to a callback as it completes, so a transport may deliver bytes in any sized pieces.
///
/// Frames are grouped into **segments**. A segment is one self-contained document: everything a
/// frame refers to -- channel paths, message templates, field names -- is declared once within
/// the segment that uses it, so a busy channel costs its path exactly once no matter how many
/// records name it. A new segment starts whenever the encoder is told to end the current one,
/// which is what makes a stored run of frames safe to trim from the front: whole segments can be
/// dropped and the rest still decodes.
///
/// @code
///   LogWireEncoder *enc = logWireEncoderCreate(_SL("web-01"), 0);
///
///   Buffer frames = 0;
///   logWireEncode(enc, &frames, rec);   // frames now holds one or more complete frames
///   ...send them...
///
///   bufDestroy(&frames);
///   logWireEncoderDestroy(&enc);
/// @endcode
///
/// @section log_wire_out Output buffers
///
/// Every encode call writes its frames into a Buffer, replacing whatever was in it. Hand the same
/// Buffer back on the next call and it is reused, growing only when a run of frames needs more
/// room than it already has. Destroy it with bufDestroy() when you are done with it.

#include <cx/buffer/buffer.h>
#include <cx/container/sarray.h>
#include <cx/log/log.h>
#include <cx/log/logctx.h>

CX_C_BEGIN

/// Kinds of frame that can appear on the wire
///
/// A decoder hands a frame kind it does not implement to the callback with nothing filled in,
/// rather than failing, so an older receiver keeps working against a newer sender.
enum LOG_WIRE_FRAME {
    LOG_WireSegment = 1,   ///< Opens a segment; everything declared in the old one is forgotten
    LOG_WireChanDecl,      ///< Declares a channel path and its policy, for this segment
    LOG_WireSiteDecl,      ///< Declares a call site's message template, for this segment
    LOG_WireEntry,         ///< One log record
    LOG_WireGap,           ///< Records that were dropped rather than sent
    LOG_WireSubscribe,     ///< A receiver asking for a set of channels and levels
    LOG_WireCatalog,       ///< What a sender is capable of logging
};

/// Encoder options
enum LOG_WIRE_FLAGS {
    /// Leave context fields out of every record
    ///
    /// For a receiver that does its own correlation and does not want the sender's. The record's
    /// own arguments are unaffected.
    LOG_WireOmitCtx = 0x00000001,
};

/// One decoded log record, as it came off the wire
///
/// Everything here is borrowed for the duration of the frame callback. Hand it to logInject() to
/// deliver it locally, or copy what you need.
typedef struct LogWireRecord {
    int level;             ///< Severity the sender logged it at
    strref chanpath;       ///< Channel path the sender logged it to
    flags_t chanflags;     ///< The channel's policy flags at the sender (LOG_Restricted etc.)
    int64 timestamp;       ///< Sender's wall clock timestamp; never re-stamped on the way in
    uint64 seq;            ///< Sender's sequence number
    uint32 batchid;        ///< Sender's batch id
    uint32 sample;         ///< Sampling rate this record survived at the sender
    int trigger;           ///< Severity that released it from a retention ring, or -1
    uint8 hops;            ///< Instances it has already been forwarded through
    strref origin;         ///< Instance it was originally logged on
    strref msgtmpl;        ///< Message template, or the literal message when istmpl is false
    bool istmpl;           ///< msgtmpl is a format template rather than a literal message
    const stvar* args;     ///< Arguments, unkeyed ones first in their original order
    int nargs;             ///< Number of arguments
    const stvar* ctx;      ///< Context fields that were in scope at the sender, all keyed
    int nctx;              ///< Number of context fields
} LogWireRecord;

/// What a receiver has asked a sender for
///
/// A sender ships nothing until one of these arrives; there is no locally configured "forward
/// everything to host X". See @ref log_forward.
typedef struct LogSubSpec {
    /// Channel patterns wanted, in logRegisterDest() syntax. An empty list asks for everything
    /// the sender is willing to offer.
    sa_string patterns;

    /// Most verbose level wanted. A sender clamps this to what it was configured to allow.
    int maxlevel;

    /// Wall-clock time in microseconds at which the subscription lapses, or 0 for never
    ///
    /// A collector that goes away without saying so otherwise leaves a sender talking to nothing
    /// forever. Set this and renew it, and a sender that stops hearing from you goes quiet.
    int64 expiry;
} LogSubSpec;

/// One channel in a sender's catalog
typedef struct LogWireChanInfo {
    strref path;         ///< Channel path
    flags_t flags;       ///< Policy flags at the sender (LOG_Restricted, LOG_Broadcast, ...)
    int maxlevel;        ///< Most verbose level anything at the sender currently wants on it
} LogWireChanInfo;

/// What a sender is capable of logging
///
/// The channel inventory, which is what an operator browses before deciding what to subscribe to.
/// Call sites are not in it: C offers no portable way to enumerate the statics in a binary, so
/// what a channel actually says only becomes visible once something logs to it.
typedef struct LogWireCatalog {
    const LogWireChanInfo* chans;   ///< Channels the sender has interned
    int nchans;                     ///< Number of channels
} LogWireCatalog;

/// Records a sender dropped instead of sending
///
/// A gap says how much is missing and where, so a receiver can say so rather than silently
/// showing a shorter log than the sender produced.
typedef struct LogWireGap {
    uint64 count;      ///< How many records were dropped
    uint64 firstseq;   ///< Sequence number of the first one
    uint64 lastseq;    ///< Sequence number of the last one
} LogWireGap;

/// One decoded frame
///
/// Only the member matching `kind` is filled in; the rest are NULL.
typedef struct LogWireFrame {
    int kind;                     ///< LOG_WIRE_FRAME
    const LogWireRecord* rec;     ///< LOG_WireEntry
    const LogWireGap* gap;        ///< LOG_WireGap
    const LogSubSpec* sub;        ///< LOG_WireSubscribe
    const LogWireCatalog* cat;    ///< LOG_WireCatalog
} LogWireFrame;

/// Called once per complete frame
///
/// @param frame The frame; valid only for the duration of the call
/// @param ctx User context passed to logWireDecode()
/// @return false to stop decoding, which fails the whole logWireDecode() call
typedef bool (*LogWireFrameCB)(_In_ const LogWireFrame* frame, _In_opt_ void* ctx);

/// Opaque encoder; one per connection
typedef struct LogWireEncoder LogWireEncoder;

/// Opaque decoder; one per connection
typedef struct LogWireDecoder LogWireDecoder;

/// Create an encoder
///
/// @param origin This instance's identity, stamped on every record that does not already carry
///               one. Empty leaves records unstamped.
/// @param flags LOG_WIRE_FLAGS options, or 0
/// @return A new encoder; destroy with logWireEncoderDestroy()
/// @code
///   LogWireEncoder *enc = logWireEncoderCreate(_SL("web-01"), 0);
/// @endcode
_Ret_valid_ LogWireEncoder* logWireEncoderCreate(_In_opt_ strref origin, flags_t flags);

/// Destroy an encoder
///
/// @param enc Pointer to the encoder handle; set to NULL
void logWireEncoderDestroy(_Inout_ LogWireEncoder** enc);

/// Encode one record
///
/// Writes one or more complete frames into `out`: whatever declarations this record needs and has
/// not sent yet, then the record itself. Nothing partial is ever written.
///
/// @param enc Encoder to use
/// @param out Receives the frames, replacing anything already in the buffer. A NULL buffer is
///            created; an existing one is reused and grown as needed.
/// @param rec Record to encode
/// @return false if the record could not be encoded; `out` is left empty in that case
/// @code
///   Buffer frames = 0;
///   logWireEncode(enc, &frames, rec);
/// @endcode
bool logWireEncode(_Inout_ LogWireEncoder* enc, _Inout_ Buffer* out, _In_ const LogRecord* rec);

/// Encode a gap
///
/// @param enc Encoder to use
/// @param out Receives the frame, replacing anything already in the buffer
/// @param n How many records were dropped
/// @param firstseq Sequence number of the first one
/// @param lastseq Sequence number of the last one
/// @return false if the frame could not be encoded
bool logWireEncodeGap(_Inout_ LogWireEncoder* enc, _Inout_ Buffer* out, uint64 n, uint64 firstseq,
                      uint64 lastseq);

/// Encode a subscription request
///
/// For the receiving side of a connection: this is what a collector sends to a sender to say what
/// it wants. Sending it needs an encoder of its own, since the control plane is the same framing
/// running the other way.
///
/// @param enc Encoder to use
/// @param out Receives the frame, replacing anything already in the buffer
/// @param spec What is being asked for; NULL cancels an existing subscription
/// @return false if the frame could not be encoded
/// @code
///   LogSubSpec spec = { .maxlevel = LOG_Info };
///   saInit(&spec.patterns, string, 1);
///   saPush(&spec.patterns, string, _S"app/**");
///   logWireEncodeSub(enc, &out, &spec);
///   saDestroy(&spec.patterns);
/// @endcode
bool logWireEncodeSub(_Inout_ LogWireEncoder* enc, _Inout_ Buffer* out,
                      _In_opt_ const LogSubSpec* spec);

/// Encode a channel catalog
///
/// @param enc Encoder to use
/// @param out Receives the frame, replacing anything already in the buffer
/// @param chans Channels to describe
/// @param nchans Number of channels
/// @return false if the frame could not be encoded
bool logWireEncodeCatalog(_Inout_ LogWireEncoder* enc, _Inout_ Buffer* out,
                          _In_reads_(nchans) const LogWireChanInfo* chans, int nchans);

/// End the current segment
///
/// The next record encoded opens a new one and re-sends the declarations it needs. Call this
/// wherever a receiver may only ever see part of what was produced -- before storing frames that
/// might later be trimmed, and on reconnect.
///
/// @param enc Encoder to end the segment on
void logWireEndSegment(_Inout_ LogWireEncoder* enc);

/// Create a decoder
///
/// @return A new decoder; destroy with logWireDecoderDestroy()
_Ret_valid_ LogWireDecoder* logWireDecoderCreate(void);

/// Destroy a decoder
///
/// @param dec Pointer to the decoder handle; set to NULL
void logWireDecoderDestroy(_Inout_ LogWireDecoder** dec);

/// Feed received bytes to a decoder
///
/// Every frame that completes is handed to `cb`. A frame that is only partly here is kept until
/// the rest of it arrives, so bytes may be fed in any sized pieces.
///
/// @param dec Decoder to feed
/// @param buf Bytes received
/// @param len Number of bytes
/// @param cb Called once per complete frame
/// @param ctx Passed to the callback
/// @return false if the stream is malformed. The decoder cannot be used again after that and
///         must be destroyed; close the connection it came from.
/// @code
///   if (!logWireDecode(dec, buf, len, myFrameCB, self))
///       closeConnection(self);
/// @endcode
bool logWireDecode(_Inout_ LogWireDecoder* dec, _In_reads_bytes_(len) const uint8* buf, size_t len,
                   _In_ LogWireFrameCB cb, _In_opt_ void* ctx);

/// Deliver a record that came from another instance to this one's destinations
///
/// The record goes through the same routing, filtering and per-channel level checks as anything
/// logged here, so a record that does not arrive was dropped for a reason a local record would
/// have been dropped for too. Its timestamp, sequence number and batch id are the sender's and
/// are not replaced.
///
/// @param chanpath Channel to deliver it to, interned locally if it is new
/// @param rec Record decoded from the wire
/// @return false if the record was not delivered
/// @code
///   logInject(frame->rec->chanpath, frame->rec);
/// @endcode
bool logInject(_In_ strref chanpath, _In_ const LogWireRecord* rec);

/// @}  // end of log_wire group

CX_C_END
