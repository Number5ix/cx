/// @file serjson.h
/// @brief Serialization backend that reads and writes JSON

#pragma once

#include <cx/serialize/serreader.h>
#include <cx/serialize/serwriter.h>
#include <cx/serialize/streambuf.h>

CX_C_BEGIN

/// @defgroup serialize_json_backend JSON Backend
/// @ingroup serialize
/// @{
///
/// JSON cannot represent the data model losslessly, so the projection is defined rather than
/// improvised. Each rule below is the behaviour, not a suggestion:
///
/// - An integer outside ±2^53 is written as a **decimal string**, because that is the range
///   past which a JSON consumer with only doubles starts silently rounding. The reader accepts
///   both the string and the number form.
/// - NaN and ±Inf have no JSON spelling and are written as `null`; under `SER_Strict` they are
///   an error instead.
/// - A byte run is a **base64 string**.
/// - A map with non-string keys becomes an **array of two-element `[key, value]` arrays**. The
///   backend does not advertise `SER_Cap_NonStringKeys`, so the traverser projects it.
/// - A `float32` is written in the shortest form that reads back as the same `float32`.
/// - A struct member equal to its default is **omitted**, and an absent member reads back as
///   its default. `SER_EmitDefaults` writes them anyway.
/// - An unknown key is ignored on read; under `SER_Strict` it is an error.
///
/// The transport is a `StreamBuffer` the caller owns and registers a consumer or producer on,
/// the same as the rest of cx's JSON support. The backend finishes the stream but does not
/// release it.
///
/// @code
///   StreamBuffer *sb = sbufCreate(4096);
///   string out = 0;
///   sbufStrCRegisterPush(sb, &out);
///
///   SerWriter *w = serJsonWriterCreate(sb, SER_JSON_Pretty);
///   serWrite(w, MyStruct, val);
///   serWriterFinish(w);
///   serWriterDestroy(&w);
///   sbufRelease(&sb);
/// @endcode

/// Creates a writer that emits JSON to a stream buffer.
///
/// @param sb    Stream buffer with a consumer registered in push mode. The writer registers as
///              the producer, taking a reference of its own; finishing or destroying it ends
///              the producer side and drops that reference, which spends the stream. The
///              caller's own reference is untouched and still has to be released.
/// @param flags SerFlagsEnum options. `SER_JSON_Pretty` indents by 4 and breaks lines,
///              `SER_JSON_Compact` strips every optional space; the default is one line with
///              spaces after punctuation. Pretty wins if both are given.
/// @return A new writer; destroy with serWriterDestroy()
_Ret_notnull_ SerWriter* serJsonWriterCreate(_Inout_ StreamBuffer* sb, flags_t flags);

/// Creates a reader that parses JSON from a stream buffer.
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
///   sbufStrPRegisterPull(sb, json);
///
///   SerReader *r = serJsonReaderCreate(sb, 0);
///   MyStruct out;
///   structInit(MyStruct, &out);
///   serRead(r, MyStruct, &out);
///   serReaderDestroy(&r);
///   sbufRelease(&sb);
/// @endcode
_Ret_notnull_ SerReader* serJsonReaderCreate(_Inout_ StreamBuffer* sb, flags_t flags);

/// @}

CX_C_END
