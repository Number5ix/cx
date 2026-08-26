/// @file sbbuffer.h
/// @brief Stream buffer Buffer I/O adapters
///
/// @defgroup serialize_buffer Buffer I/O
/// @ingroup serialize
/// @{
///
/// Adapters for using a Buffer as a stream buffer producer.
///
/// A Buffer is the natural source for arbitrary binary data that is already in memory.
/// Unlike the string adapters, a Buffer has exactly one owner, so these functions can take
/// that ownership over and destroy the Buffer for you once the stream is done with it.
///
/// - sbufBufIn() - Push the entire Buffer into the stream buffer at once
/// - sbufBufPRegisterPull() - Register the Buffer as a pull-mode producer

#pragma once

#include <cx/buffer/buffer.h>
#include <cx/serialize/streambuf.h>

CX_C_BEGIN

/// Pushes the entire contents of a Buffer into a stream buffer.
///
/// The data is written in chunks of the stream buffer's target size. The producer is finished
/// after the last chunk, which invalidates the stream buffer.
///
/// This does not return until the whole Buffer has been handed over. Register the Buffer as a
/// pull-mode producer with sbufBufPRegisterPull() instead when the consumer should set the pace.
///
/// If flow control is active (see sbufSetWatermark()), this waits for the consumer to make room
/// rather than dropping data, so the consumer has to be draining the buffer from another thread.
///
/// @param sb The stream buffer (invalidated after call)
/// @param buf Buffer to push into the stream buffer
/// @param own If true, buf is destroyed before this function returns. The caller must not use
///            or destroy buf afterwards, even if this call returns false.
/// @return true on success, false on error
///
/// Example:
/// @code
///   string output = 0;
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufStrCRegisterPush(sb, &output);
///   sbufBufIn(sb, buf, true);
/// @endcode
bool sbufBufIn(_Pre_valid_ _Post_invalid_ StreamBuffer* sb, _In_ Buffer buf, bool own);

/// Registers a Buffer as a producer with the stream buffer in pull mode.
///
/// The contents are handed to the consumer a slice at a time as it reads, so a large Buffer
/// does not have to be copied into the stream buffer all at once. The producer finishes on
/// its own once the last byte has been read.
///
/// @param sb The stream buffer
/// @param buf Buffer to use as data source
/// @param own If true, the stream buffer takes ownership of buf and destroys it when the
///            stream is torn down. The caller must not use or destroy buf afterwards, even
///            if this call returns false.
/// @return true on success, false if a producer is already registered
///
/// Example:
/// @code
///   Buffer buf = bufCreate(len);
///   memcpy(buf->data, data, len);
///   buf->len = len;
///
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufBufPRegisterPull(sb, buf, true);
/// @endcode
_Check_return_ bool sbufBufPRegisterPull(_Inout_ StreamBuffer* sb, _In_ Buffer buf, bool own);

/// @}  // end of serialize_buffer

CX_C_END
