#pragma once

// The QUIC wire codec and packet protection: everything in RFC 9000 sections 16-19 and
// RFC 9001 section 5 that is a pure function of bytes.
//
// Nothing in here holds connection state or touches a socket. The encoders write into a
// caller-supplied datagram buffer and the decoders read from one, so the whole layer can be
// exercised against the RFC's own test vectors without a network.

#include <cxquic/quic_shared.h>

#include <cxtls/tls_shared.h>
#include <cxtls/tls13_private.h>

#include <psa/crypto.h>

CX_C_BEGIN

extern LogChannel* QuicLogChannel;

// One-time process init: creates QuicLogChannel and runs cxtls' own init, which calls
// psa_crypto_init(). Everything in cxquic that touches crypto calls this first and refuses to
// work if it failed.
bool _quicInit(void);

// ---------------------------------------------------------------------------------------------
// Byte readers and writers (RFC 9000 section 16)
// ---------------------------------------------------------------------------------------------

// A bounds-checked cursor over a received datagram.
//
// Every read that would run past the end sets `bad` and returns zero, and once `bad` is set it
// stays set, so a decoder can run a whole sequence of reads and test the flag once at the end
// instead of after each field.
typedef struct QuicRd {
    const uint8* p;
    const uint8* end;
    bool bad;
} QuicRd;

void _quicRdInit(_Out_ QuicRd* rd, _In_reads_(len) const uint8* data, size_t len);
_Pure size_t _quicRdLeft(_In_ const QuicRd* rd);
uint8 _quicRd8(_Inout_ QuicRd* rd);
uint16 _quicRd16(_Inout_ QuicRd* rd);
uint32 _quicRd32(_Inout_ QuicRd* rd);

// Reads a big-endian integer of an explicit width, 1 to 8 bytes. Used for the truncated packet
// number, whose width is carried in the packet's first byte.
uint64 _quicRdInt(_Inout_ QuicRd* rd, uint8 nbytes);

// Returns a pointer to the next `n` bytes and advances past them, or NULL if there are not that
// many left. The bytes are not copied, so the result points into the caller's datagram.
_Ret_maybenull_ const uint8* _quicRdBytes(_Inout_ QuicRd* rd, size_t n);

// Reads a variable-length integer. Returns 0 with `bad` set if the encoding runs off the end.
uint64 _quicRdVarint(_Inout_ QuicRd* rd);

// Reads a variable-length length prefix followed by that many bytes, the shape QUIC uses for
// tokens, reason phrases, and frame payloads. `len` receives the byte count.
_Ret_maybenull_ const uint8* _quicRdVarintBytes(_Inout_ QuicRd* rd, _Out_ size_t* len);

// A bounds-checked cursor writing into a fixed datagram buffer.
//
// Like QuicRd, a write that does not fit sets `bad` and is dropped rather than truncating, so a
// packet is either built whole or rejected.
typedef struct QuicWr {
    uint8* base;
    uint8* p;
    uint8* end;
    bool bad;
} QuicWr;

void _quicWrInit(_Out_ QuicWr* wr, _Out_writes_(bufsz) uint8* buf, size_t bufsz);
_Pure size_t _quicWrLen(_In_ const QuicWr* wr);
_Pure size_t _quicWrLeft(_In_ const QuicWr* wr);
void _quicWr8(_Inout_ QuicWr* wr, uint8 val);
void _quicWr16(_Inout_ QuicWr* wr, uint16 val);
void _quicWr32(_Inout_ QuicWr* wr, uint32 val);
void _quicWrInt(_Inout_ QuicWr* wr, uint64 val, uint8 nbytes);
void _quicWrBytes(_Inout_ QuicWr* wr, _In_reads_opt_(len) const uint8* data, size_t len);

// Number of bytes _quicWrVarint would use for `val`, or 0 if the value is too large to encode.
_Pure uint8 _quicVarintSize(uint64 val);

// Writes a variable-length integer in the shortest encoding that fits.
void _quicWrVarint(_Inout_ QuicWr* wr, uint64 val);

// Writes a variable-length integer padded out to exactly `size` bytes, which must be 1, 2, 4 or 8
// and large enough to hold the value. QUIC allows the longer encodings, and a sender uses one so
// it can reserve room for a length field before it knows what will go in the packet.
void _quicWrVarintSized(_Inout_ QuicWr* wr, uint64 val, uint8 size);

// Writes a variable-length length prefix followed by the bytes themselves.
void _quicWrVarintBytes(_Inout_ QuicWr* wr, _In_reads_opt_(len) const uint8* data, size_t len);

// ---------------------------------------------------------------------------------------------
// Packet numbers (RFC 9000 sections 17.1 and A.2/A.3)
// ---------------------------------------------------------------------------------------------

// Stands in for "no packet has been sent or received in this number space yet", where the
// algorithms in RFC 9000 Appendix A use an absent largest-acknowledged value.
#define QUIC_PN_NONE UINT64_MAX

// Smallest number of bytes that can carry `pn` such that the peer will unambiguously recover it,
// given the largest packet number it has acknowledged. Returns 1 to 4.
_Pure uint8 _quicPnSize(uint64 pn, uint64 largestAcked);

// Recovers a full packet number from the truncated one on the wire.
//
// `largestPn` is the largest packet number already received in this number space, or
// #QUIC_PN_NONE if none has been.
_Pure uint64 _quicPnDecode(uint64 largestPn, uint64 truncated, uint8 pnLen);

// ---------------------------------------------------------------------------------------------
// Packet headers (RFC 9000 section 17)
// ---------------------------------------------------------------------------------------------

// The four long-header types are the values QUIC version 1 assigns to the type bits; the last two
// are not type-bit values at all, but the two other packet shapes a datagram can hold.
typedef enum QuicPktType {
    QUIC_PKT_INITIAL    = 0,
    QUIC_PKT_0RTT       = 1,
    QUIC_PKT_HANDSHAKE  = 2,
    QUIC_PKT_RETRY      = 3,
    QUIC_PKT_SHORT      = 4,
    QUIC_PKT_VERSIONNEG = 5,
    QUIC_PKT_UNKNOWN    = 6,
} QuicPktType;

// A decoded packet header.
//
// Header protection means decoding happens in two steps: _quicHdrDecode reads everything up to
// the packet number, which is all that can be read while the packet is still protected, and
// _quicHdrDecodePN finishes the job once the protection has been removed. Encoding is one step,
// because a sender knows all of it up front.
typedef struct QuicPktHdr {
    uint8 type;             // one of QuicPktType
    uint8 pnLen;            // packet number width, 1 to 4
    uint8 lenSize;          // encode only: pin the Length varint to this width, 0 for shortest
    bool keyPhase;          // short header only
    bool spin;              // short header only
    uint32 version;         // 0 for a short header
    QuicCid dcid;
    QuicCid scid;           // long header forms only
    const uint8* token;     // Initial token, or the Retry token
    size_t tokenLen;
    const uint8* tag;       // Retry integrity tag, QUIC_TAG_LEN bytes
    uint64 pn;              // full packet number
    uint64 pnEnc;           // truncated packet number as it appears on the wire
    size_t len;             // Length field: the packet number plus the protected payload
    size_t pnOff;           // offset of the packet number within the packet
    size_t pktLen;          // total length of this packet, which may be one of several coalesced
    const uint8* payload;   // protected payload after the packet number, or the Version
    size_t payloadLen;      //   Negotiation version list
} QuicPktHdr;

// Reads a packet header up to but not including the packet number, which is still protected.
//
// `dcidLen` is the connection ID length this endpoint issues, which a short header does not carry
// and the receiver must already know.
//
// A long header carrying a version other than QUIC_VERSION_1 decodes as QUIC_PKT_UNKNOWN with
// only the version and the two connection IDs filled in, since the rest of the layout is version
// specific. That is enough to answer with a Version Negotiation packet.
//
// On success `pnOff` and `pktLen` are set, so a caller walking coalesced packets can find both
// the header protection sample and the start of the next packet.
_Success_(return) bool _quicHdrDecode(_Out_ QuicPktHdr* h, _In_reads_(len) const uint8* pkt,
                                      size_t len, uint8 dcidLen);

// Finishes decoding a header whose protection has been removed, filling in the packet number and
// the payload. `largestPn` is the largest packet number already received in this number space, or
// #QUIC_PN_NONE.
_Success_(return) bool _quicHdrDecodePN(_Inout_ QuicPktHdr* h, _In_ const uint8* pkt,
                                        uint64 largestPn);

// Number of bytes _quicHdrEncode will write for this header, or 0 if it cannot be encoded.
//
// A sender uses this to work out how much room is left for frames, before it knows what the
// Length field will hold. Pin `lenSize` first in that case, or the answer changes once `len` is
// filled in and the header comes out a different size than was budgeted for.
_Pure size_t _quicHdrSize(_In_ const QuicPktHdr* h);

// Writes an unprotected packet header, including the truncated packet number.
//
// The caller sets `len` to the packet number width plus the protected payload length, and `pnEnc`
// and `pnLen` to the truncated packet number. For a Retry packet this writes the token and the
// integrity tag as well, so the packet is complete.
_Success_(return) bool _quicHdrEncode(_Inout_ QuicWr* wr, _In_ const QuicPktHdr* h);

// ---------------------------------------------------------------------------------------------
// Frames (RFC 9000 section 19)
// ---------------------------------------------------------------------------------------------

#define QUIC_FRAME_PADDING              0x00
#define QUIC_FRAME_PING                 0x01
#define QUIC_FRAME_ACK                  0x02
#define QUIC_FRAME_ACK_ECN              0x03
#define QUIC_FRAME_RESET_STREAM         0x04
#define QUIC_FRAME_STOP_SENDING         0x05
#define QUIC_FRAME_CRYPTO               0x06
#define QUIC_FRAME_NEW_TOKEN            0x07
#define QUIC_FRAME_STREAM               0x08
#define QUIC_FRAME_MAX_DATA             0x10
#define QUIC_FRAME_MAX_STREAM_DATA      0x11
#define QUIC_FRAME_MAX_STREAMS_BIDI     0x12
#define QUIC_FRAME_MAX_STREAMS_UNI      0x13
#define QUIC_FRAME_DATA_BLOCKED         0x14
#define QUIC_FRAME_STREAM_DATA_BLOCKED  0x15
#define QUIC_FRAME_STREAMS_BLOCKED_BIDI 0x16
#define QUIC_FRAME_STREAMS_BLOCKED_UNI  0x17
#define QUIC_FRAME_NEW_CONNECTION_ID    0x18
#define QUIC_FRAME_RETIRE_CONNECTION_ID 0x19
#define QUIC_FRAME_PATH_CHALLENGE       0x1a
#define QUIC_FRAME_PATH_RESPONSE        0x1b
#define QUIC_FRAME_CONNECTION_CLOSE     0x1c
#define QUIC_FRAME_CONNECTION_CLOSE_APP 0x1d
#define QUIC_FRAME_HANDSHAKE_DONE       0x1e

// RFC 9221. The two forms differ only in whether a length field is present; without one the
// frame runs to the end of the packet.
#define QUIC_FRAME_DATAGRAM             0x30
#define QUIC_FRAME_DATAGRAM_LEN         0x31

// The low three bits of a STREAM frame type say which optional fields are present.
#define QUIC_STREAM_FIN 0x01
#define QUIC_STREAM_LEN 0x02
#define QUIC_STREAM_OFF 0x04

// A decoded frame.
//
// Byte ranges are pointers into the packet the frame was decoded from rather than copies, so a
// frame is only valid for as long as that buffer is.
typedef struct QuicFrame {
    uint64 type;
    union {
        // PADDING: how many consecutive padding bytes the run covered
        uint64 padding;

        struct {
            uint64 largest;
            uint64 delay;
            uint64 firstRange;
            uint64 rangeCount;
            const uint8* rangeData;     // the gap and length pairs, walked by QuicAckIter
            size_t rangeLen;
            uint64 ect0, ect1, ecnce;   // only meaningful for QUIC_FRAME_ACK_ECN
        } ack;

        struct { uint64 id, error, finalSize; } resetStream;
        struct { uint64 id, error; } stopSending;
        struct { uint64 offset, len; const uint8* data; } crypto;
        struct { uint64 len; const uint8* token; } newToken;
        struct { uint64 id, offset, len; const uint8* data; } stream;
        struct { uint64 max; } maxData;
        struct { uint64 id, max; } maxStreamData;
        struct { uint64 max; } maxStreams;
        struct { uint64 limit; } dataBlocked;
        struct { uint64 id, limit; } streamDataBlocked;
        struct { uint64 limit; } streamsBlocked;
        struct {
            uint64 seq, retirePrior;
            QuicCid cid;
            uint8 token[QUIC_RESET_TOKEN_LEN];
        } newConnId;
        struct { uint64 seq; } retireConnId;
        struct { uint8 data[QUIC_PATH_DATA_LEN]; } path;    // PATH_CHALLENGE and PATH_RESPONSE
        struct {
            uint64 error, frameType, reasonLen;
            const uint8* reason;
        } connClose;
        struct { uint64 len; const uint8* data; } datagram;
    };
} QuicFrame;

// Decodes the next frame from a packet payload.
//
// A run of PADDING bytes decodes as one frame whose `padding` count covers the whole run. Returns
// false at the end of the payload as well as on a malformed frame; test `rd->bad` to tell them
// apart.
_Success_(return) bool _quicFrameDecode(_Out_ QuicFrame* f, _Inout_ QuicRd* rd);

// Number of bytes _quicFrameEncode will write, or 0 if the frame cannot be encoded.
_Pure size_t _quicFrameSize(_In_ const QuicFrame* f);

// Writes a frame. The type must already carry its low-order bits: which optional fields a STREAM
// frame has, and whether an ACK frame carries ECN counts.
_Success_(return) bool _quicFrameEncode(_Inout_ QuicWr* wr, _In_ const QuicFrame* f);

// One acknowledged range, inclusive at both ends.
typedef struct QuicAckRange {
    uint64 largest;
    uint64 smallest;
} QuicAckRange;

// Walks the ranges of a decoded ACK frame, largest first.
//
// The ranges are a chain of deltas on the wire, so they can only be read in order, and there is
// no count of them that is trustworthy before they have been walked.
typedef struct QuicAckIter {
    QuicRd rd;
    uint64 remaining;
    uint64 largest;
    uint64 firstRange;
    uint64 next;
    bool first;
    bool bad;
} QuicAckIter;

_Success_(return) bool _quicAckIterInit(_Out_ QuicAckIter* it, _In_ const QuicFrame* f);

// Produces the next acknowledged range. Returns false at the end of the list as well as on a
// malformed one; test `it->bad` to tell them apart.
_Success_(return) bool _quicAckIterNext(_Inout_ QuicAckIter* it, _Out_ QuicAckRange* out);

// Fills in the ACK fields of a frame from an array of ranges sorted largest first, so it can be
// handed to _quicFrameSize and _quicFrameEncode. The ranges must not touch or overlap.
//
// `rangeBuf` receives the encoded gap and length pairs and must stay alive as long as the frame
// does; `bufsz` bytes of it are enough for `nranges` ranges when it is at least 20 times
// `nranges`.
_Success_(return) bool _quicAckBuild(_Out_ QuicFrame* f, uint64 delay,
                                     _In_reads_(nranges) const QuicAckRange* ranges,
                                     size_t nranges, _In_opt_ const uint64 ecn[3],
                                     _Out_writes_(bufsz) uint8* rangeBuf, size_t bufsz);

// ---------------------------------------------------------------------------------------------
// Packet protection (RFC 9001 section 5)
// ---------------------------------------------------------------------------------------------

// One direction's keys at one encryption level.
//
// The traffic secret is kept alongside the keys because a key update derives the next generation
// from it rather than from the key.
typedef struct QuicKeys {
    psa_key_id_t aead;              // AEAD key
    psa_key_id_t hp;                // header protection key, unused when hpChaCha is set
    psa_algorithm_t aeadAlg;
    psa_algorithm_t hash;           // the suite's hash, needed to derive the next generation
    psa_key_type_t keyType;
    uint8 iv[TLS13_IV_LEN];
    uint8 hpKey[TLS13_MAX_KEY];     // header protection key material, kept across a key update
    uint8 secret[TLS13_MAX_HASH];
    uint8 secretLen;
    uint8 keyLen;
    bool hpChaCha;                  // header protection is ChaCha20 rather than AES-ECB
    bool valid;
} QuicKeys;

// Derives the Initial keys for both directions from the client's first Destination Connection ID,
// which is the only input either endpoint has before the handshake starts.
_Success_(return) bool _quicKeysInitial(_Out_ QuicKeys* client, _Out_ QuicKeys* server,
                                        _In_reads_(dcidLen) const uint8* dcid, size_t dcidLen);

// Derives one direction's keys at one encryption level from a TLS 1.3 traffic secret.
_Success_(return) bool _quicKeysDerive(_Out_ QuicKeys* k, _In_ const Tls13Suite* suite,
                                       _In_reads_(suite->hashLen) const uint8* secret);

// Derives the next generation of keys for a key update, leaving the current ones alone so packets
// still in flight under them can be opened. The header protection key carries over unchanged, as
// RFC 9001 requires: only the AEAD key and IV are replaced.
_Success_(return) bool _quicKeysNext(_Out_ QuicKeys* next, _In_ const QuicKeys* cur);

void _quicKeysDestroy(_Inout_ QuicKeys* k);

// Produces the five-byte header protection mask for a sample taken from the protected payload.
_Success_(return) bool _quicHpMask(_In_ const QuicKeys* k,
                                   _In_reads_(16) const uint8* sample,
                                   _Out_writes_(5) uint8* mask);

_Success_(return) bool _quicAeadSeal(_In_ const QuicKeys* k, uint64 pn,
                                     _In_reads_(aadLen) const uint8* aad, size_t aadLen,
                                     _In_reads_(ptLen) const uint8* pt, size_t ptLen,
                                     _Out_writes_(outsz) uint8* out, size_t outsz,
                                     _Out_ size_t* outLen);

_Success_(return) bool _quicAeadOpen(_In_ const QuicKeys* k, uint64 pn,
                                     _In_reads_(aadLen) const uint8* aad, size_t aadLen,
                                     _In_reads_(ctLen) const uint8* ct, size_t ctLen,
                                     _Out_writes_(outsz) uint8* out, size_t outsz,
                                     _Out_ size_t* outLen);

// Encrypts a packet's payload onto the end of its header and then protects the header.
//
// `pkt` holds an unprotected header of `pnOff` plus `pnLen` bytes, as written by _quicHdrEncode,
// and must have room for the payload and a QUIC_TAG_LEN tag after it. The payload must be in a
// separate buffer. `outLen` receives the length of the finished packet.
_Success_(return) bool _quicPktSeal(_In_ const QuicKeys* k, _Inout_updates_(bufsz) uint8* pkt,
                                    size_t bufsz, size_t pnOff, uint8 pnLen, uint64 pn,
                                    _In_reads_opt_(payloadLen) const uint8* payload,
                                    size_t payloadLen, _Out_ size_t* outLen);

// Removes header protection from a packet in place, recovers its packet number, and decrypts its
// payload into `out`.
//
// `h` must already have been filled in by _quicHdrDecode against the same buffer. On success the
// rest of `h` is filled in as well, and `pkt` holds the unprotected header.
//
// The header is left unprotected even when the payload fails to decrypt, so a caller trying a
// second set of AEAD keys after a key update passes the packet back in as it is. Header
// protection keys never change, so removing it twice would be wrong.
_Success_(return) bool _quicPktOpen(_In_ const QuicKeys* k, _Inout_ QuicPktHdr* h,
                                    _Inout_ uint8* pkt, uint64 largestPn,
                                    _Out_writes_(outsz) uint8* out, size_t outsz,
                                    _Out_ size_t* outLen);

// Computes the integrity tag that closes a Retry packet.
//
// `odcid` is the Destination Connection ID from the Initial packet being answered, and `retry` is
// the whole Retry packet up to but not including the tag.
_Success_(return) bool _quicRetryTag(_Out_writes_(QUIC_TAG_LEN) uint8* tag,
                                     _In_reads_(odcidLen) const uint8* odcid, size_t odcidLen,
                                     _In_reads_(retryLen) const uint8* retry, size_t retryLen);

CX_C_END
