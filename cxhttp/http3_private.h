#pragma once

// HTTP/3 (RFC 9114) and QPACK (RFC 9204), the parts that are a pure function of bytes.
//
// Nothing in this half touches a socket or holds connection state: the encoders write into a
// caller-supplied buffer and the decoders read from one, so the whole layer can be run against the
// RFCs' own published encodings without a network. The session and connection machinery built on
// top of it is declared further down the same header.
//
// Internal rather than public API, and reached by the test suite the same way HttpParser is,
// through <cxhttp/http3_private.h>.

#include "http_private.h"

#include <cxquic.h>

CX_C_BEGIN

// ---------------------------------------------------------------------------------------------
// Protocol constants (RFC 9114 sections 7, 8 and 11; RFC 9204 sections 5 and 6)
// ---------------------------------------------------------------------------------------------

// Frame types. Every other type is either reserved for greasing or unassigned, and both are
// ignored rather than refused, which is what keeps a future extension from breaking this peer.
#define H3_FRAME_DATA         0x00
#define H3_FRAME_HEADERS      0x01
#define H3_FRAME_CANCEL_PUSH  0x03
#define H3_FRAME_SETTINGS     0x04
#define H3_FRAME_PUSH_PROMISE 0x05
#define H3_FRAME_GOAWAY       0x07
#define H3_FRAME_MAX_PUSH_ID  0x0d

// Unidirectional stream types, sent as a varint prefix on the first byte of the stream.
#define H3_STREAM_CONTROL       0x00
#define H3_STREAM_PUSH          0x01
#define H3_STREAM_QPACK_ENCODER 0x02
#define H3_STREAM_QPACK_DECODER 0x03

// Settings identifiers. cxhttp sends the first three and understands the fourth; anything else is
// ignored, as the RFC requires.
#define H3_SETTING_QPACK_MAX_TABLE_CAPACITY 0x01
#define H3_SETTING_MAX_FIELD_SECTION_SIZE   0x06
#define H3_SETTING_QPACK_BLOCKED_STREAMS    0x07
#define H3_SETTING_ENABLE_CONNECT_PROTOCOL  0x08

// Shorter spellings of the HttpH3Error codes, which are what the code below reads as. The enum
// itself is public so that a program logging why a request died can name the code it saw.
#define H3ERR_NO_ERROR                0x0100
#define H3ERR_GENERAL_PROTOCOL_ERROR  0x0101
#define H3ERR_INTERNAL_ERROR          0x0102
#define H3ERR_STREAM_CREATION_ERROR   0x0103
#define H3ERR_CLOSED_CRITICAL_STREAM  0x0104
#define H3ERR_FRAME_UNEXPECTED        0x0105
#define H3ERR_FRAME_ERROR             0x0106
#define H3ERR_EXCESSIVE_LOAD          0x0107
#define H3ERR_ID_ERROR                0x0108
#define H3ERR_SETTINGS_ERROR          0x0109
#define H3ERR_MISSING_SETTINGS        0x010a
#define H3ERR_REQUEST_REJECTED        0x010b
#define H3ERR_REQUEST_CANCELLED       0x010c
#define H3ERR_REQUEST_INCOMPLETE      0x010d
#define H3ERR_MESSAGE_ERROR           0x010e
#define H3ERR_CONNECT_ERROR           0x010f
#define H3ERR_VERSION_FALLBACK        0x0110

#define H3ERR_QPACK_DECOMPRESSION_FAILED 0x0200
#define H3ERR_QPACK_ENCODER_STREAM_ERROR 0x0201
#define H3ERR_QPACK_DECODER_STREAM_ERROR 0x0202

// The ALPN protocol identifier, spelled once in http_private.h because the client's dialer names
// it from a file that is in every build.
#define H3_ALPN HTTP_ALPN_H3

// ---------------------------------------------------------------------------------------------
// Byte readers and writers
//
// The same variable-length integer encoding QUIC uses, because HTTP/3 borrows it wholesale. It is
// re-implemented here rather than shared with cxquic because that codec lives behind cxquic's
// implementation header, and because HTTP/3 needs the incremental reader further down as well.
// ---------------------------------------------------------------------------------------------

// A bounds-checked cursor over received bytes.
//
// Every read that would run past the end sets `bad` and returns zero, and once `bad` is set it
// stays set, so a decoder can run a whole sequence of reads and test the flag once at the end.
typedef struct H3Rd {
    const uint8* p;
    const uint8* end;
    bool bad;
} H3Rd;

void _h3RdInit(_Out_ H3Rd* rd, _In_reads_bytes_(len) const uint8* data, size_t len);
_Pure size_t _h3RdLeft(_In_ const H3Rd* rd);
uint8 _h3Rd8(_Inout_ H3Rd* rd);

// Returns a pointer to the next `n` bytes and advances past them, or NULL if there are not that
// many left. The bytes are not copied, so the result points into the caller's buffer.
_Ret_maybenull_ const uint8* _h3RdBytes(_Inout_ H3Rd* rd, size_t n);

// Reads a variable-length integer. Returns 0 with `bad` set if the encoding runs off the end.
uint64 _h3RdVarint(_Inout_ H3Rd* rd);

// A bounds-checked cursor writing into a fixed buffer.
//
// Like H3Rd, a write that does not fit sets `bad` and is dropped rather than truncating, so what
// comes out is either whole or rejected.
typedef struct H3Wr {
    uint8* base;
    uint8* p;
    uint8* end;
    bool bad;
} H3Wr;

void _h3WrInit(_Out_ H3Wr* wr, _Out_writes_bytes_(bufsz) uint8* buf, size_t bufsz);
_Pure size_t _h3WrLen(_In_ const H3Wr* wr);
_Pure size_t _h3WrLeft(_In_ const H3Wr* wr);
void _h3Wr8(_Inout_ H3Wr* wr, uint8 val);
void _h3WrBytes(_Inout_ H3Wr* wr, _In_reads_bytes_opt_(len) const uint8* data, size_t len);

// Largest value the encoding can carry: a varint is at most 62 bits.
#define H3_VARINT_MAX (((uint64)1 << 62) - 1)

// Number of bytes _h3WrVarint would use for `val`, or 0 if the value is too large to encode.
_Pure uint8 _h3VarintSize(uint64 val);

// Writes a variable-length integer in the shortest encoding that fits.
void _h3WrVarint(_Inout_ H3Wr* wr, uint64 val);

// ---------------------------------------------------------------------------------------------
// Frames (RFC 9114 section 7.1)
// ---------------------------------------------------------------------------------------------

// Longest frame header: a type varint and a length varint, each at most eight bytes.
#define H3_FRAME_HDR_MAX 16

// Size of the header for a frame of this type carrying this many payload bytes.
_Pure uint8 _h3FrameHdrSize(uint64 type, uint64 len);

// Writes a frame header -- the type and the payload length -- into `buf`, which needs
// #H3_FRAME_HDR_MAX bytes to be sure of fitting. Returns the number of bytes written, or 0 if it
// did not fit.
size_t _h3WrFrameHdr(_Out_writes_bytes_to_(bufsz, return) uint8* buf, size_t bufsz, uint64 type,
                     uint64 len);

// What one step of the frame reader produced.
typedef enum {
    H3FR_NeedMore = 0,   // everything available is consumed; feed more bytes
    H3FR_Header,         // a frame header was consumed; `type` and `len` are readable
    H3FR_Payload,        // `avail` payload bytes are waiting at the head of the source ring
    H3FR_Complete,       // the current frame's payload ended
    H3FR_Error           // the reader was poisoned by _h3FrameReaderFail()
} H3FrameResult;

// An incremental reader for the frame layer of one QUIC stream.
//
// It never needs a whole frame resident: it reports each frame's header once and then hands the
// payload over in whatever runs the network delivered, so a DATA frame carrying a gigabyte and a
// HEADERS frame carrying two hundred bytes take the same path. A caller that does need the whole
// payload -- HEADERS does -- accumulates the runs itself and bounds what it will accept.
typedef struct H3FrameReader {
    uint64 type;      // current frame's type, valid from H3FR_Header onward
    uint64 len;       // current frame's total payload length, valid from H3FR_Header onward
    uint64 remain;    // payload bytes not yet reported to the caller

    // Payload bytes at the head of the source ring that belong to this frame, set alongside
    // H3FR_Payload. The caller must remove exactly this many from the same ring before calling
    // _h3FrameReaderStep() again, or the reader and the ring fall out of step.
    size_t avail;

    bool inFrame;     // a header has been reported and its payload has not ended
    bool bad;
} H3FrameReader;

void _h3FrameReaderInit(_Out_ H3FrameReader* fr);

// Consume what it can from `src` and report what that produced. Call in a loop until it answers
// NeedMore or Error, draining `avail` bytes after each H3FR_Payload.
H3FrameResult _h3FrameReaderStep(_Inout_ H3FrameReader* fr, _Inout_ BufRing* src);

// Poison the reader, so every later step answers H3FR_Error. What a caller calls when the frame
// layer was well formed but what it carried was not.
void _h3FrameReaderFail(_Inout_ H3FrameReader* fr);

// ---------------------------------------------------------------------------------------------
// QPACK Huffman coding (RFC 7541 Appendix B, which RFC 9204 adopts unchanged)
// ---------------------------------------------------------------------------------------------

// Encoded length of `data` in bytes, including the padding bits that round it out to a byte.
_Pure size_t _qpackHuffLen(_In_reads_bytes_(len) const uint8* data, size_t len);

// Encodes `data`. `out` needs _qpackHuffLen() bytes. Returns the number written, or 0 if it did
// not fit -- which is also what an empty input produces, and means the same thing to a caller.
size_t _qpackHuffEncode(_Out_writes_bytes_to_(outsz, return) uint8* out, size_t outsz,
                        _In_reads_bytes_(len) const uint8* data, size_t len);

// Upper bound on what `len` encoded bytes can decode to. The shortest code is five bits, so no
// input can expand by more than eight fifths.
_Pure size_t _qpackHuffMaxDecoded(size_t len);

// Decodes a Huffman string. `out` needs _qpackHuffMaxDecoded(len) bytes.
//
// Fails on anything a conforming encoder cannot have produced: the EOS symbol, a padding run of
// eight bits or more, or padding that is not the leading bits of the EOS code. Each of those is
// how a decoder is made to accept two different encodings of the same string, so none of them is
// tolerated.
_Success_(return) bool _qpackHuffDecode(_Out_writes_bytes_to_(outsz, *outlen) uint8* out,
                                        size_t outsz, _Out_ size_t* outlen,
                                        _In_reads_bytes_(len) const uint8* data, size_t len);

// ---------------------------------------------------------------------------------------------
// QPACK field sections (RFC 9204 sections 4.5 and 5)
//
// Static table only. cxhttp advertises a dynamic table capacity of zero, which forbids the peer
// from using one either, so every field is an index into the 99-entry static table, a static name
// with a literal value, or two literals. That removes head-of-line blocking on header state
// entirely: a field section decodes the moment it arrives, whatever else is in flight.
// ---------------------------------------------------------------------------------------------

#define QPACK_STATIC_COUNT 99

// The entry at `idx`, or false if there is no such entry.
_Success_(return) bool _qpackStaticGet(_Out_ strref* name, _Out_ strref* value, int32 idx);

// Index of the first static entry whose name matches, or -1. The name must already be lowercase,
// which is what HTTP/3 requires on the wire anyway.
_Pure int32 _qpackStaticFindName(_In_opt_ strref name);

// Index of the static entry whose name and value both match, or -1.
_Pure int32 _qpackStaticFind(_In_opt_ strref name, _In_opt_ strref value);

// Encoded size of a field list as RFC 9114 section 4.2.2 counts it, which is what
// SETTINGS_MAX_FIELD_SECTION_SIZE bounds: the length of every name and value plus 32 bytes of
// per-field overhead.
_Pure uint64 _qpackFieldSectionSize(_In_ const HttpHeaders* h);

// Encode a field section, appending it to `out`.
//
// Field names are lowercased on the way out, because HTTP/3 has no uppercase field names and a
// peer is required to reject one. Huffman coding is used for a name or value only when it comes
// out shorter, which it usually does for text and never does for an already-compact token.
_Success_(return) bool _qpackEncode(_Inout_ strhandle out, _In_ const HttpHeaders* h);

// Decode a field section.
//
// `out` is initialized by this call and is left in a state safe to destroy however it ends, so a
// failed decode still needs exactly one httpHeadersDestroy().
//
// `maxCount` bounds the number of fields and `maxSize` the total counted the way
// _qpackFieldSectionSize() counts it; either may be 0 for no limit.
//
// Returns 0 on success, or the HTTP/3 error code the failure calls for. A
// #H3ERR_QPACK_DECOMPRESSION_FAILED is a connection error -- the field section could not be
// decoded, so nothing after it on any stream can be trusted either. A #H3ERR_MESSAGE_ERROR
// decoded fine and is merely unacceptable, so it costs only the one stream.
uint64 _qpackDecode(_Out_ HttpHeaders* out, _In_reads_bytes_(len) const uint8* data, size_t len,
                    uint32 maxCount, uint64 maxSize);

// ---------------------------------------------------------------------------------------------
// Connection session (RFC 9114 sections 5, 6.2 and 7.2)
//
// The HTTP/3 analogue of HttpParser: direction-agnostic, driven by bytes rather than by a socket,
// and shared by both roles. Writing it once is what stops a cx client and a cx server from
// agreeing with each other about framing while both being wrong.
//
// Everything a connection needs that is not per-request lives here: the control stream in each
// direction, the peer's settings, the GOAWAY state, and what each unidirectional stream the peer
// opened turned out to be.
// ---------------------------------------------------------------------------------------------

// What one unidirectional stream carries. The type is a varint on the first byte of the stream,
// and until it arrives there is no way to know.
typedef enum {
    H3UNI_Unknown = 0,     // the type varint has not arrived yet
    H3UNI_Control,
    H3UNI_Push,
    H3UNI_QpackEncoder,
    H3UNI_QpackDecoder,
    H3UNI_Ignored          // a type this endpoint does not implement; read and discarded
} H3UniKind;

// The settings one endpoint advertises to the other.
typedef struct Http3Settings {
    uint64 maxFieldSectionSize;    // 0 means no limit was advertised
    uint64 qpackMaxTableCapacity;
    uint64 qpackBlockedStreams;
    bool enableConnectProtocol;
} Http3Settings;

// State for one unidirectional stream the peer opened. Lives on the flow, so only that stream's
// worker ever touches it.
typedef struct H3UniStream {
    uint64 id;
    H3UniKind kind;

    BufRing in;          // bytes read off the stream and not yet consumed
    H3FrameReader fr;    // control streams only

    // The control frame payload being accumulated. Allocated to the frame's declared length when
    // its header arrives, which is safe because that length is bounded by H3_MAX_CONTROL_FRAME.
    uint8* frame;
    size_t frameLen;
    bool frameKeep;      // the frame being read is one whose payload is worth keeping

    bool typeRead;
} H3UniStream;

typedef struct Http3Session {
    bool server;

    Http3Settings local;    // what this endpoint advertised
    Http3Settings peer;     // what the peer advertised
    bool peerSettings;      // the peer's SETTINGS frame has arrived

    bool peerControl;       // the peer has opened its control stream
    uint64 peerControlId;

    bool peerEncoderStream;   // the peer's QPACK encoder stream exists
    bool peerDecoderStream;

    bool goawaySent;        // this endpoint has sent a GOAWAY
    uint64 goawaySentId;

    bool goawayRecvd;       // the peer has sent one
    uint64 goawayId;        // the id it named; nothing at or above it was processed

    uint64 maxPushId;       // server: the largest push id the client has permitted, plus one
} Http3Session;

// Largest control-stream frame payload this endpoint will buffer. SETTINGS and GOAWAY are tens of
// bytes; anything approaching this is a peer trying to make us allocate on its behalf.
#define H3_MAX_CONTROL_FRAME 16384

void _h3SessionInit(_Out_ Http3Session* s, bool server, _In_opt_ const HttpLimits* limits);
void _h3SessionDestroy(_Inout_ Http3Session* s);

// The bytes that open this endpoint's control stream: the stream type prefix followed by the
// SETTINGS frame, which RFC 9114 requires to be the first frame on it.
_Success_(return) bool _h3SessionControlPreamble(_In_ const Http3Session* s, _Inout_ strhandle out);

// The stream type prefix for a unidirectional stream this endpoint is opening. The QPACK encoder
// and decoder streams carry nothing but this, because with no dynamic table there are no
// instructions to send, and RFC 9204 still requires both to exist.
_Success_(return) bool _h3SessionUniPrefix(_Inout_ strhandle out, uint64 type);

// A GOAWAY frame naming `id`: the first request stream this endpoint will not serve, or on a
// client the first push it will not accept. Refused if it would raise the id above one already
// sent, which RFC 9114 section 5.2 forbids.
_Success_(return) bool _h3SessionGoawayFrame(_Inout_ Http3Session* s, _Inout_ strhandle out,
                                             uint64 id);

void _h3UniInit(_Out_ H3UniStream* u, uint64 id);
void _h3UniDestroy(_Inout_ H3UniStream* u);

// Feed bytes that arrived on a unidirectional stream the peer opened. Returns 0, or the HTTP/3
// error code the connection must be closed with.
uint64 _h3SessionUniRecv(_Inout_ Http3Session* s, _Inout_ H3UniStream* u,
                         _In_reads_bytes_(len) const uint8* data, size_t len);

// Tell the session the peer ended a unidirectional stream. Ending a critical stream -- the control
// stream or either QPACK stream -- is a connection error whatever else is going on.
uint64 _h3SessionUniEnd(_Inout_ Http3Session* s, _Inout_ H3UniStream* u);

// True if a request on this stream id is one the peer's GOAWAY said it would not process, so
// retrying it on a fresh connection is safe whatever the method was.
_Pure bool _h3SessionGoneAway(_In_ const Http3Session* s, uint64 streamId);

// How an HTTP/3 or QUIC application error code reaching the application is reported.
_Pure HttpError _h3ErrorToHttp(uint64 code);

// ---------------------------------------------------------------------------------------------
// The connection's own streams
//
// Both roles open the same three unidirectional streams and read the peer's the same way, so both
// do it through here. That is the same argument Http3Session itself rests on: written once, a cx
// client and a cx server cannot disagree about it.
// ---------------------------------------------------------------------------------------------

// Close the connection with an HTTP/3 error code. Supplied by whichever class owns the connection,
// because the two have no common base.
typedef void (*H3ConnErrorCB)(_Inout_ ObjInst* conn, uint64 code);

// State for one unidirectional stream the peer opened, held as that flow's handler context.
typedef struct H3Uni {
    H3UniStream u;

    // The connection this belongs to, weak because the connection owns the socket that owns the
    // flow that holds this. Resolving it per callback is also what proves `session` is still there.
    Weak(ObjInst)* conn;

    Http3Session* session;    // borrowed from the connection resolved above
    H3ConnErrorCB onError;
} H3Uni;

// Open this endpoint's control stream and its two QPACK streams, and send what each has to carry:
// the SETTINGS frame, and a type prefix. The QPACK streams carry nothing else -- with no dynamic
// table there are no instructions to send, and RFC 9204 section 4.2 requires both to exist anyway.
//
// The three flows come back as references the caller owns.
_Success_(return) bool _h3OpenUniStreams(_In_ NetSocket* sock, _In_ const Http3Session* s,
                                         _Outptr_ NetFlow** control, _Outptr_ NetFlow** enc,
                                         _Outptr_ NetFlow** dec);

// Take over a unidirectional stream the peer opened: allocate its state, register the shared
// handlers on the flow, and hand it the session to feed.
void _h3UniAttach(_In_ NetFlow* flow, _In_ ObjInst* conn, _In_ Http3Session* s,
                  H3ConnErrorCB onError);

// ---------------------------------------------------------------------------------------------
// Request and response streams (RFC 9114 sections 4.1 and 7.1)
//
// One request is one bidirectional QUIC stream, which is one NetFlow, which is one ordering domain
// with a worker of its own. Everything below runs on that worker and nowhere else.
// ---------------------------------------------------------------------------------------------

// How much to pull off a stream per read.
#define HTTP3_READ_CHUNK 8192

// What one step over a request or response stream produced. The caller loops until NeedMore or
// Error, exactly as it does with HttpParser.
typedef enum {
    H3MSG_NeedMore = 0,   // everything available is consumed; feed more bytes
    H3MSG_Head,           // a field section arrived before any body; `headers` holds it decoded
    H3MSG_Body,           // `bodyReady` body bytes are waiting at the head of the source ring
    H3MSG_Trailers,       // a field section arrived after the body; `headers` holds it decoded
    H3MSG_Error           // `err` is the HTTP/3 code this stream or connection has to carry
} H3MsgResult;

// The frame layer of one request or response stream, and the field sections on it.
//
// Direction-agnostic in the same way HttpParser is: a server feeds it a request and a client feeds
// it a response, and the difference between them is entirely in what the caller does with a
// H3MSG_Head. What it knows on its own is the frame grammar -- which frames may appear on a
// request stream, in what order, and how large this endpoint will let one grow.
typedef struct H3MsgReader {
    H3FrameReader fr;

    bool server;             // reading requests rather than responses
    uint32 maxHeaderCount;   // fields in one section; 0 for no limit
    uint64 maxFieldSize;     // one section's uncompressed size; 0 for no limit
    uint64 maxBody;          // body bytes; 0 for no limit

    // The field section being accumulated. Allocated to the frame's declared length, which is
    // refused before the allocation when it is larger than this endpoint will hold.
    uint8* fields;
    size_t fieldsLen;
    bool inFields;

    HttpHeaders headers;   // the decoded section, valid at H3MSG_Head and H3MSG_Trailers

    // Body bytes at the head of the source ring, set alongside H3MSG_Body. The caller must remove
    // exactly this many before stepping again.
    size_t bodyReady;
    uint64 bodyTotal;      // body bytes reported so far, for maxBody

    bool bodyStarted;      // a DATA frame has been seen, so a later section is trailers
    bool headDone;         // a field section has been reported
    uint64 err;            // the code behind H3MSG_Error
} H3MsgReader;

void _h3MsgInit(_Out_ H3MsgReader* r, bool server, _In_opt_ const HttpLimits* limits);
void _h3MsgDestroy(_Inout_ H3MsgReader* r);

// Consume what it can from `src` and report what that produced. Call in a loop until it answers
// NeedMore or Error, draining `bodyReady` bytes after each H3MSG_Body.
H3MsgResult _h3MsgStep(_Inout_ H3MsgReader* r, _Inout_ BufRing* src);

// ---------------------------------------------------------------------------------------------
// Field sections and HTTP messages (RFC 9114 sections 4.1 and 4.2)
// ---------------------------------------------------------------------------------------------

// True for a field HTTP/3 forbids outright (RFC 9114 section 4.2). Each of these describes the
// HTTP/1.1 connection rather than the message, and over a protocol where the connection is not the
// framing they mean nothing. They are rejected inbound and dropped outbound, the same rule the
// HTTP/1.1 writer already follows for Content-Length: cxhttp writes what the message actually is,
// never what the application asked for.
_Pure bool _h3FieldForbidden(_In_opt_ strref name);

// Split a decoded field section into the pseudo-headers and the ordinary fields, checking what
// both have to satisfy: pseudo-headers first, no unknown ones, no duplicates, nothing forbidden.
//
// `out` is initialized by this call. The four pseudo-header outputs are left empty when absent.
// Returns 0 or #H3ERR_MESSAGE_ERROR.
uint64 _h3SplitFields(_Out_ HttpHeaders* out, _Inout_ sa_string* pseudoNames,
                      _Inout_ sa_string* pseudoValues, _In_ const HttpHeaders* fields);

// Fill in the received half of a server request from a decoded field section: method, target,
// path, query, headers, and the Host that :authority becomes. Returns 0 or the stream error the
// request must be rejected with.
uint64 _h3ReqFromFields(_Inout_ HttpServerRequest* req, _In_ const HttpHeaders* fields);

// Read a response's status out of a decoded field section, leaving the ordinary fields in `out`.
uint64 _h3RespFromFields(_Out_ uint16* status, _Out_ HttpHeaders* out,
                         _In_ const HttpHeaders* fields);

// Build the field section a request goes out as: the pseudo-headers first, then everything the
// application set that HTTP/3 permits, then the framing fields cxhttp decides itself.
_Success_(return) bool _h3ReqToFields(_Out_ HttpHeaders* out, _In_opt_ strref method,
                                      _In_ const HttpUrl* url, _In_ const HttpHeaders* headers,
                                      int64 bodyLen);

// Build the field section a response goes out as.
_Success_(return) bool _h3RespToFields(_Out_ HttpHeaders* out, uint16 status,
                                       _In_ const HttpHeaders* headers, int64 bodyLen);

// ---------------------------------------------------------------------------------------------
// Writing to a stream
// ---------------------------------------------------------------------------------------------

// Send a HEADERS frame carrying `fields`. All or nothing: if the stream has not the room for the
// whole frame, nothing is sent and this answers false.
_Success_(return) bool _h3SendFields(_In_ NetFlow* flow, _In_ const HttpHeaders* fields, bool fin);

// Send `len` bytes of body as a DATA frame.
//
// False means the stream had no room for the whole frame and nothing was sent. The refusal is
// what asks to be told when there is room again, so a caller that stops here is woken by
// NET_SendReady once the frame would fit.
_Success_(return) bool _h3SendData(_In_ NetFlow* flow, _In_reads_bytes_(len) const uint8* data,
                                   size_t len);

CX_C_END
