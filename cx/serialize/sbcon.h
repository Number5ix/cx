/// @file sbcon.h
/// @brief Stream buffer console adapter
///
/// @defgroup serialize_con Console Adapter
/// @ingroup serialize
/// @{
///
/// Adapter for using a console stream (see @ref console) as a stream buffer consumer.
///
/// Unlike sbfile.h's VFS adapters, there is no producer half and no close parameter --
/// conOut()/conErr()/conIn() are process singletons and a conCreateMem() stream is owned
/// by whoever created it, so a stream buffer consumer never closes or destroys the
/// ConStream it writes to.
///
/// **Consumer (Output) Functions:**
/// - sbufConOut() - Write all stream buffer data to a console stream
/// - sbufConCRegisterPush() - Register a console stream as a push-mode consumer

#pragma once

#include <cx/console/console.h>
#include <cx/serialize/streambuf.h>

CX_C_BEGIN

/// bool sbufConOut(StreamBuffer *sb, ConStream *con)
///
/// Consumes all available data from the buffer and writes it to a console stream.
///
/// Reads data from the stream buffer until the producer finishes (EOF) and writes it to
/// con in chunks. Never closes or destroys con.
///
/// **IMPORTANT:** The stream buffer is invalidated after this call.
///
/// @param sb The stream buffer (invalidated after call)
/// @param con Console stream to write to
/// @return true on success, false on error
///
/// Example:
/// @code
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufStrPRegisterPull(sb, inputData);
///   sbufConOut(sb, conOut());
/// @endcode
bool sbufConOut(_Pre_valid_ _Post_invalid_ StreamBuffer* sb, _In_ ConStream* con);

/// bool sbufConCRegisterPush(StreamBuffer *sb, ConStream *con)
///
/// Registers a console stream as a consumer with the stream buffer in push mode.
///
/// In push mode, data is automatically written to con as it becomes available from the
/// producer. Use this instead of sbufConOut() when you need the producer and consumer to
/// operate asynchronously. Never closes or destroys con.
///
/// @param sb The stream buffer
/// @param con Console stream to write to
/// @return true on success, false if registration failed
_Check_return_ bool sbufConCRegisterPush(_Inout_ StreamBuffer* sb, _In_ ConStream* con);

/// @}  // end of serialize_con

CX_C_END
