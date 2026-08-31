/// @file sbbuffer.h
/// @brief Stream buffer Buffer I/O adapters
///
/// @defgroup serialize_buffer Buffer I/O
/// @ingroup serialize
/// @{
///
/// Adapters for using a Buffer as a stream buffer producer or consumer.
///
/// A Buffer is the natural home for arbitrary binary data. Unlike the string adapters, a Buffer
/// has exactly one owner, so the producer functions can take that ownership over and destroy the
/// Buffer for you once the stream is done with it.
///
/// **Producer (Input) Functions:**
/// - sbufBufIn() - Push the entire Buffer into the stream buffer at once
/// - sbufBufPRegisterPull() - Register the Buffer as a pull-mode producer
///
/// **Consumer (Output) Functions:**
/// - sbufBufOut() - Read all data from the stream buffer into a Buffer
/// - sbufBufCRegisterPush() - Register a Buffer as a push-mode consumer
///
/// **Convenience:**
/// - sbufBufCreatePush() - Create and configure a stream buffer for Buffer output
///
/// The consumer functions append to the Buffer they are given, growing it as needed, and create
/// one if the pointer they are handed is NULL.
///
/// The push-mode consumer registers in **direct mode**: a Buffer can always take everything it is
/// handed, so the stream buffer keeps no storage of its own and the producer's bytes are copied
/// once, straight into the output. This means the stream buffer holds nothing to read back --
/// sbufCRead() and friends have nothing to return -- and watermark flow control does not apply,
/// because there is never anything to drain.

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

/// Consumes all available data from the stream buffer and appends it to a Buffer.
///
/// Reads until the producer finishes (EOF).
///
/// **IMPORTANT:** The stream buffer is invalidated after this call.
///
/// @param sb The stream buffer (invalidated after call)
/// @param bufout Buffer to append the data to; a new one is created if it is NULL
/// @return true on success, false on error
///
/// Example:
/// @code
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufStrPRegisterPull(sb, inputData);
///   Buffer out = 0;
///   sbufBufOut(sb, &out);
///   bufDestroy(&out);
/// @endcode
bool sbufBufOut(_Pre_valid_ _Post_invalid_ StreamBuffer* sb, _Inout_ Buffer* bufout);

/// Registers a Buffer as a consumer with the stream buffer in direct push mode.
///
/// Data is appended to the Buffer as the producer writes it. Use this instead of sbufBufOut()
/// when the producer drives the stream.
///
/// The Buffer pointer is borrowed, so it must stay valid for as long as the stream buffer is
/// registered against it. Nothing is buffered along the way, so create the stream buffer with a
/// target size of 0 unless a producer of yours needs one to chunk its writes by.
///
/// @param sb The stream buffer
/// @param bufout Buffer to append output data to; a new one is created if it is NULL
/// @return true on success, false if registration failed
_Check_return_ bool sbufBufCRegisterPush(_Inout_ StreamBuffer* sb, _Inout_ Buffer* bufout);

/// Creates a stream buffer configured for Buffer output in direct push mode.
///
/// For the common pattern of the caller producing in push mode with the output going to a Buffer.
///
/// @param bufout Buffer to append output data to; a new one is created if it is NULL
/// @return New configured stream buffer, or NULL on failure
///
/// Example:
/// @code
///   Buffer out = 0;
///   StreamBuffer *sb = sbufBufCreatePush(&out);
///   sbufPWrite(sb, data, size);
///   sbufPFinish(sb);
///   bufDestroy(&out);
/// @endcode
_Check_return_ _Ret_opt_valid_ StreamBuffer* sbufBufCreatePush(_Inout_ Buffer* bufout);

/// @}  // end of serialize_buffer

CX_C_END
