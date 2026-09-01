/// @file serbinary.h
/// @brief Serialization backend that reads and writes cx's compact binary format

#pragma once

#include <cx/serialize/serreader.h>
#include <cx/serialize/serwriter.h>
#include <cx/serialize/streambuf.h>

CX_C_BEGIN

/// @defgroup serialize_binary_backend Binary Backend
/// @ingroup serialize
/// @{
///
/// A compact, lossless binary format. It advertises `SER_Cap_Bytes`, `SER_Cap_ExactInt`, and
/// `SER_Cap_Sizes`, so nothing is lost or approximated the way it can be with JSON.
///
/// Documents are written and read strictly front-to-back, which makes this format a good fit
/// for a socket or pipe. It is not meant for reading a large file at random; use a different
/// backend for that.
///
/// A few notes on the encoding, useful if you need to inspect or debug a document:
///
/// - Little-endian, with varint-encoded lengths, counts, and dictionary ids.
/// - Every value starts with a one-byte tag.
/// - Member names, type names, and (unless `SER_Bin_NoStringDedup` is set) string values are
///   deduplicated into a shared dictionary, so a repeated string only costs a few bytes after
///   the first time it appears.
///
/// **Compact mode** (`SER_Bin_Compact`) makes documents smaller by dropping the tag byte on
/// values whose type the schema already pins down (integers, reals, byte runs). The trade-off:
/// a compact document can only be read back with the exact schema it was written with, and an
/// unrecognized field is an error rather than something that can be skipped. Use the default,
/// self-describing mode if documents need to keep reading after the schema changes.
///
/// The reader detects the mode from the document itself, so you don't need to know how a
/// document was written in order to read it.
///
/// @code
///   StreamBuffer *sb = sbufCreate(4096);
///   string out = 0;
///   sbufStrCRegisterPush(sb, &out);
///
///   SerWriter *w = serBinaryWriterCreate(sb, 0);
///   serWrite(w, MyStruct, val);
///   serWriterFinish(w);
///   serWriterDestroy(&w);
///   sbufClose(sb);
///   sbufRelease(&sb);
/// @endcode

/// Magic at the start of every document, followed by a version byte and a flags byte.
#define SER_BIN_MAGIC "CXSB"

/// Format version this build writes. The reader rejects anything it does not know.
#define SER_BIN_VERSION 1

/// Header flag bits, recording how the body was encoded.
enum SerBinHeaderFlagsEnum {
    SER_BinHdr_SelfDescribing = (1 << 0),   ///< values carry tag bytes
    SER_BinHdr_StringDedup    = (1 << 1),   ///< string values intern into the dictionary
    SER_BinHdr_Refs           = (1 << 2),   ///< document may contain reference nodes
};

/// Creates a writer that emits binary to a stream buffer.
///
/// @param sb    Stream buffer with a consumer registered in push mode. The writer registers as
///              the producer, taking a reference of its own; finishing or destroying it ends
///              the producer side and drops that reference, which spends the stream. The
///              caller's own reference is untouched and still has to be released.
/// @param flags SerFlagsEnum options. `SER_Bin_Compact` omits the tags the schema makes
///              redundant, `SER_Bin_NoStringDedup` writes string values inline instead of
///              interning them.
/// @return A new writer; destroy with serWriterDestroy()
_Ret_notnull_ SerWriter* serBinaryWriterCreate(_Inout_ StreamBuffer* sb, flags_t flags);

/// Creates a reader that decodes binary from a stream buffer.
///
/// The document's header decides whether it is self-describing or compact and whether string
/// values were interned; none of that has to be supplied here.
///
/// @param sb    Stream buffer with a producer registered in pull mode. The reader registers as
///              the consumer, taking a reference of its own; destroying it ends the consumer
///              side and drops that reference, which spends the stream. The caller's own
///              reference is untouched and still has to be released.
/// @param flags SerFlagsEnum options
/// @return A new reader; destroy with serReaderDestroy()
///
/// Example:
/// @code
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufStrPRegisterPull(sb, doc);
///
///   SerReader *r = serBinaryReaderCreate(sb, 0);
///   MyStruct out;
///   structInit(MyStruct, &out);
///   serRead(r, MyStruct, &out);
///   serReaderDestroy(&r);
///   sbufClose(sb);
///   sbufRelease(&sb);
/// @endcode
_Ret_notnull_ SerReader* serBinaryReaderCreate(_Inout_ StreamBuffer* sb, flags_t flags);

/// @}

CX_C_END
