/// @file sbfsfile.h
/// @brief Stream buffer low-level filesystem file I/O adapters
///
/// @defgroup serialize_fsfile Filesystem File I/O
/// @ingroup serialize
/// @{
///
/// Adapters for using low-level FSFile handles as stream buffer producers and consumers.
///
/// These functions provide convenient ways to read from or write to filesystem files
/// using the stream buffer abstraction, supporting both push and pull modes.
///
/// **Producer (Input) Functions:**
/// - sbufFSFileIn() - Read entire file contents into stream buffer
/// - sbufFSFilePRegisterPull() - Register filesystem file as pull-mode producer
///
/// **Consumer (Output) Functions:**
/// - sbufFSFileOut() - Write all stream buffer data to filesystem file
/// - sbufFSFileCRegisterPush() - Register filesystem file as push-mode consumer
///
/// **Note:** For VFS (Virtual File System) operations, use the functions in
/// sbfile.h instead. FSFile provides direct low-level filesystem access.

#pragma once

#include <cx/fs/file.h>
#include <cx/serialize/streambuf.h>

/// @defgroup serialize_fsfile_producer Filesystem File Producers
/// @ingroup serialize_fsfile
/// @{
///
/// Functions for using low-level filesystem files as stream buffer data sources.

/// bool sbufFSFileIn(StreamBuffer *sb, FSFile *file, bool close)
///
/// Reads a filesystem file and pushes its entire contents into a stream buffer.
///
/// Automatically chunks the data based on the stream buffer's target size for
/// efficient operation. The stream buffer is automatically finished after all
/// data is read.
///
/// **IMPORTANT:** The stream buffer is invalidated after this call.
///
/// @param sb The stream buffer (invalidated after call)
/// @param file Filesystem file to read from (optionally closed based on close parameter)
/// @param close If true, the file is closed after reading
/// @return true on success, false on error
///
/// Example:
/// @code
///   FSFile *file = fsOpen(_S"data.bin", FS_Read, 0);
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufStrCRegisterPush(sb, &output);
///   sbufFSFileIn(sb, file, true);  // file is closed automatically
/// @endcode
bool sbufFSFileIn(_Pre_valid_ _Post_invalid_ StreamBuffer* sb, _Pre_valid_ _When_(close, _Post_invalid_) FSFile* file,
                  bool close);

/// bool sbufFSFilePRegisterPull(StreamBuffer *sb, FSFile *file, bool close)
///
/// Registers a filesystem file as a producer with the stream buffer in pull mode.
///
/// In pull mode, the consumer pulls data as needed, and the file is read in
/// chunks on demand. Use this instead of sbufFSFileIn() when you need finer
/// control over when data is read.
///
/// @param sb The stream buffer
/// @param file Filesystem file to read from (optionally closed when producer finishes)
/// @param close If true, the file is closed when the producer finishes
/// @return true on success, false if registration failed
_Check_return_ bool
sbufFSFilePRegisterPull(_Inout_ StreamBuffer* sb, _Inout_ FSFile* file, bool close);

/// @}  // end of serialize_fsfile_producer

/// @defgroup serialize_fsfile_consumer Filesystem File Consumers
/// @ingroup serialize_fsfile
/// @{
///
/// Functions for using low-level filesystem files as stream buffer data sinks.

/// bool sbufFSFileOut(StreamBuffer *sb, FSFile *file, bool close)
///
/// Consumes all available data from the buffer and writes it to a filesystem file.
///
/// Reads data from the stream buffer until the producer finishes (EOF) and
/// writes it to the file in chunks.
///
/// **IMPORTANT:** The stream buffer is invalidated after this call.
///
/// @param sb The stream buffer (invalidated after call)
/// @param file Filesystem file to write to (optionally closed based on close parameter)
/// @param close If true, the file is closed after writing
/// @return true on success, false on error
///
/// Example:
/// @code
///   FSFile *file = fsOpen(_S"output.bin", FS_Write | FS_Create, 0);
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufStrPRegisterPull(sb, inputData);
///   sbufFSFileOut(sb, file, true);  // file is closed automatically
/// @endcode
bool sbufFSFileOut(_Pre_valid_ _Post_invalid_ StreamBuffer* sb, _Pre_valid_ _When_(close, _Post_invalid_) FSFile* file,
                   bool close);

/// bool sbufFSFileCRegisterPush(StreamBuffer *sb, FSFile *file, bool close)
///
/// Registers a filesystem file as a consumer with the stream buffer in push mode.
///
/// In push mode, data is automatically written to the file as it becomes
/// available from the producer. Use this instead of sbufFSFileOut() when you need
/// the producer and consumer to operate asynchronously.
///
/// @param sb The stream buffer
/// @param file Filesystem file to write to (optionally closed when consumer finishes)
/// @param close If true, the file is closed when the consumer finishes
/// @return true on success, false if registration failed
_Check_return_ bool
sbufFSFileCRegisterPush(_Inout_ StreamBuffer* sb, _Inout_ FSFile* file, bool close);

/// @}  // end of serialize_fsfile_consumer
/// @}  // end of serialize_fsfile
