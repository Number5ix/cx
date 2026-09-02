/// @file sbfile.h
/// @brief Stream buffer file I/O adapters
///
/// @defgroup serialize_file File I/O
/// @ingroup serialize
/// @{
///
/// Adapters for using files as stream buffer producers and consumers.
///
/// These functions provide convenient ways to read from or write to files using the stream
/// buffer abstraction, supporting both push and pull modes. They take any File, so it does not
/// matter whether the file came from fsOpen() or vfsOpen().
///
/// **Producer (Input) Functions:**
/// - sbufFileIn() - Read entire file contents into stream buffer
/// - sbufFilePRegisterPull() - Register file as pull-mode producer
///
/// **Consumer (Output) Functions:**
/// - sbufFileOut() - Write all stream buffer data to file
/// - sbufFileCRegisterPush() - Register file as push-mode consumer
///
/// Every one of them takes any File -- an FSFile from fsOpen() or a VFSFile from vfsOpen() --
/// with no cast at the call site.
///
/// Where `close` is true, the file is closed and its reference released, the same as fsClose()
/// or vfsClose() would.

#pragma once

#include <cx/fs/file.h>
#include <cx/serialize/streambuf.h>

CX_C_BEGIN

/// @defgroup serialize_file_producer File Producers
/// @ingroup serialize_file
/// @{
///
/// Functions for using files as stream buffer data sources.

/// bool sbufFileIn(StreamBuffer *sb, File *file, bool close)
///
/// Reads a file and pushes its entire contents into a stream buffer.
///
/// Automatically chunks the data based on the stream buffer's target size for
/// efficient operation. The stream buffer is automatically finished after all
/// data is read.
///
/// If flow control is active (see sbufSetWatermark()), this waits for the consumer to make room
/// rather than dropping data, so the consumer has to be draining the buffer from another thread.
///
/// @param sb The stream buffer
/// @param file File to read from (optionally closed based on close parameter)
/// @param close If true, the file is closed after reading
/// @return true on success, false on error
///
/// Example:
/// @code
///   VFSFile *file = vfsOpen(vfs, _SL("data.txt"), FS_Read);
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufStrCRegisterPush(sb, &output);
///   sbufFileIn(sb, file, true);  // file is closed automatically
/// @endcode
bool _sbufFileIn(_Inout_ StreamBuffer* sb, _Pre_valid_ _When_(close, _Post_invalid_) File* file,
                 bool close);
#define sbufFileIn(sb, file, close) _sbufFileIn(sb, File(file), close)

/// bool sbufFilePRegisterPull(StreamBuffer *sb, File *file, bool close)
///
/// Registers a file as a producer with the stream buffer in pull mode.
///
/// In pull mode, the consumer pulls data as needed, and the file is read in
/// chunks on demand. Use this instead of sbufFileIn() when you need finer
/// control over when data is read.
///
/// @param sb The stream buffer
/// @param file File to read from (optionally closed when the producer finishes)
/// @param close If true, the file is closed when the producer finishes
/// @return true on success, false if registration failed
_Check_return_ bool _sbufFilePRegisterPull(_Inout_ StreamBuffer* sb, _Inout_ File* file,
                                           bool close);
#define sbufFilePRegisterPull(sb, file, close) _sbufFilePRegisterPull(sb, File(file), close)

/// @}  // end of serialize_file_producer

/// @defgroup serialize_file_consumer File Consumers
/// @ingroup serialize_file
/// @{
///
/// Functions for using files as stream buffer data sinks.

/// bool sbufFileOut(StreamBuffer *sb, File *file, bool close)
///
/// Consumes all available data from the buffer and writes it to a file.
///
/// Reads data from the stream buffer until the producer finishes (EOF) and
/// writes it to the file in chunks.
///
/// @param sb The stream buffer
/// @param file File to write to (optionally closed based on close parameter)
/// @param close If true, the file is closed after writing
/// @return true on success, false on error
///
/// Example:
/// @code
///   VFSFile *file = vfsOpen(vfs, _SL("output.txt"), FS_Write | FS_Create);
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufStrPRegisterPull(sb, inputData);
///   sbufFileOut(sb, file, true);  // file is closed automatically
/// @endcode
bool _sbufFileOut(_Inout_ StreamBuffer* sb, _Pre_valid_ _When_(close, _Post_invalid_) File* file,
                  bool close);
#define sbufFileOut(sb, file, close) _sbufFileOut(sb, File(file), close)

/// bool sbufFileCRegisterPush(StreamBuffer *sb, File *file, bool close)
///
/// Registers a file as a consumer with the stream buffer in push mode.
///
/// In push mode, data is automatically written to the file as it becomes
/// available from the producer. Use this instead of sbufFileOut() when you need
/// the producer and consumer to operate asynchronously.
///
/// @param sb The stream buffer
/// @param file File to write to (optionally closed when the consumer finishes)
/// @param close If true, the file is closed when the consumer finishes
/// @return true on success, false if registration failed
_Check_return_ bool _sbufFileCRegisterPush(_Inout_ StreamBuffer* sb, _Inout_ File* file,
                                           bool close);
#define sbufFileCRegisterPush(sb, file, close) _sbufFileCRegisterPush(sb, File(file), close)

/// @}  // end of serialize_file_consumer

/// bool sbufFSFileIn(StreamBuffer *sb, FSFile *file, bool close)
///
/// Another name for sbufFileIn(), for a file opened with fsOpen()
#define sbufFSFileIn(sb, file, close)            sbufFileIn(sb, file, close)

/// bool sbufFSFilePRegisterPull(StreamBuffer *sb, FSFile *file, bool close)
///
/// Another name for sbufFilePRegisterPull(), for a file opened with fsOpen()
#define sbufFSFilePRegisterPull(sb, file, close) sbufFilePRegisterPull(sb, file, close)

/// bool sbufFSFileOut(StreamBuffer *sb, FSFile *file, bool close)
///
/// Another name for sbufFileOut(), for a file opened with fsOpen()
#define sbufFSFileOut(sb, file, close)           sbufFileOut(sb, file, close)

/// bool sbufFSFileCRegisterPush(StreamBuffer *sb, FSFile *file, bool close)
///
/// Another name for sbufFileCRegisterPush(), for a file opened with fsOpen()
#define sbufFSFileCRegisterPush(sb, file, close) sbufFileCRegisterPush(sb, file, close)

/// @}  // end of serialize_file

CX_C_END
