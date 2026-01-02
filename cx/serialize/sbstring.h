/// @file sbstring.h
/// @brief Stream buffer string I/O adapters
///
/// @defgroup serialize_string String I/O
/// @ingroup serialize
/// @{
///
/// Adapters for using strings as stream buffer producers and consumers.
///
/// These functions provide convenient ways to read from or write to strings using
/// the stream buffer abstraction, supporting both push and pull modes.
///
/// **Producer (Input) Functions:**
/// - sbufStrIn() - Push entire string contents into stream buffer
/// - sbufStrPRegisterPull() - Register string as pull-mode producer
///
/// **Consumer (Output) Functions:**
/// - sbufStrOut() - Read all data from stream buffer into string
/// - sbufStrCRegisterPush() - Register string as push-mode consumer
///
/// **Convenience:**
/// - sbufStrCreatePush() - Create and configure a stream buffer for string output

#pragma once

#include <cx/serialize/streambuf.h>
#include <cx/string/strbase.h>

/// @defgroup serialize_string_producer String Producers
/// @ingroup serialize_string
/// @{
///
/// Functions for using strings as stream buffer data sources.

/// bool sbufStrIn(StreamBuffer *sb, strref str)
///
/// Pushes the entire contents of a string into a stream buffer.
///
/// Automatically chunks the data based on the stream buffer's target size for
/// efficient operation. The stream buffer is automatically finished after all
/// data is written.
///
/// **IMPORTANT:** The stream buffer is invalidated after this call.
///
/// @param sb The stream buffer (invalidated after call)
/// @param str String to push into the buffer
/// @return true on success, false on error
///
/// Example:
/// @code
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufStrCRegisterPush(sb, &output);
///   sbufStrIn(sb, _S"Hello, World!");
/// @endcode
bool sbufStrIn(_Pre_valid_ _Post_invalid_ StreamBuffer* sb, _In_opt_ strref str);

/// bool sbufStrPRegisterPull(StreamBuffer *sb, strref str)
///
/// Registers a string as a producer with the stream buffer in pull mode.
///
/// In pull mode, the consumer pulls data as needed, and the string provides it
/// in chunks. Use this instead of sbufStrIn() when you need finer control over
/// when data is provided.
///
/// @param sb The stream buffer
/// @param str String to use as data source
/// @return true on success, false if registration failed
_Check_return_ bool sbufStrPRegisterPull(_Inout_ StreamBuffer* sb, _In_opt_ strref str);

/// @}  // end of serialize_string_producer

/// @defgroup serialize_string_consumer String Consumers
/// @ingroup serialize_string
/// @{
///
/// Functions for using strings as stream buffer data sinks.

/// bool sbufStrOut(StreamBuffer *sb, string *strout)
///
/// Consumes all available data from the buffer and outputs to a string.
///
/// Reads data from the stream buffer until the producer finishes (EOF) and
/// writes it to the output string, overwriting any existing contents.
///
/// **IMPORTANT:** The stream buffer is invalidated after this call.
///
/// @param sb The stream buffer (invalidated after call)
/// @param strout Output string (will be overwritten)
/// @return true on success, false on error
///
/// Example:
/// @code
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufStrPRegisterPull(sb, inputData);
///   string output = 0;
///   sbufStrOut(sb, &output);
///   // use output
///   strDestroy(&output);
/// @endcode
bool sbufStrOut(_Pre_valid_ _Post_invalid_ StreamBuffer* sb, _Inout_ string* strout);

/// bool sbufStrCRegisterPush(StreamBuffer *sb, string *strout)
///
/// Registers a string as a consumer with the stream buffer in push mode.
///
/// In push mode, data is automatically appended to the string as it becomes
/// available from the producer. Use this instead of sbufStrOut() when you need
/// the producer and consumer to operate asynchronously.
///
/// **Note:** This function appends to the string rather than overwriting it.
///
/// @param sb The stream buffer
/// @param strout String to append output data to
/// @return true on success, false if registration failed
_Check_return_ bool sbufStrCRegisterPush(_Inout_ StreamBuffer* sb, _Inout_ string* strout);

/// @}  // end of serialize_string_consumer

/// @defgroup serialize_string_convenience String Convenience Functions
/// @ingroup serialize_string
/// @{
///
/// Convenience functions for common string streaming patterns.

/// StreamBuffer *sbufStrCreatePush(string *strout, size_t targetsz)
///
/// Creates a stream buffer configured for string output in push mode.
///
/// This is a convenience function for the common pattern of creating a stream buffer
/// where the caller acts as a producer in push mode and output goes to a string.
///
/// @param strout String to append output data to
/// @param targetsz Target buffer size in bytes
/// @return New configured stream buffer, or NULL on failure
///
/// Example:
/// @code
///   string output = 0;
///   StreamBuffer *sb = sbufStrCreatePush(&output, 4096);
///   sbufPWrite(sb, data, size);
///   sbufPFinish(sb);
///   // output now contains the data
///   strDestroy(&output);
/// @endcode
_Check_return_ _Ret_opt_valid_ StreamBuffer*
sbufStrCreatePush(_Inout_ string* strout, size_t targetsz);

/// @}  // end of serialize_string_convenience
/// @}  // end of serialize_string
