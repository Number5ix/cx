/// @file sbfile.h
/// @brief Stream buffer VFS file I/O adapters
///
/// @defgroup serialize_file VFS File I/O
/// @ingroup serialize
/// @{
///
/// Adapters for using VFS files as stream buffer producers and consumers.
///
/// These functions provide convenient ways to read from or write to VFS files
/// (Virtual File System) using the stream buffer abstraction, supporting both
/// push and pull modes.
///
/// **Producer (Input) Functions:**
/// - sbufFileIn() - Read entire file contents into stream buffer
/// - sbufFilePRegisterPull() - Register VFS file as pull-mode producer
///
/// **Consumer (Output) Functions:**
/// - sbufFileOut() - Write all stream buffer data to VFS file
/// - sbufFileCRegisterPush() - Register VFS file as push-mode consumer
///
/// **Note:** For low-level filesystem operations (FSFile), use the functions in
/// sbfsfile.h instead.

#pragma once

#include <cx/fs/vfs.h>
#include <cx/serialize/streambuf.h>

/// @defgroup serialize_file_producer VFS File Producers
/// @ingroup serialize_file
/// @{
///
/// Functions for using VFS files as stream buffer data sources.

/// bool sbufFileIn(StreamBuffer *sb, VFSFile *file, bool close)
///
/// Reads a VFS file and pushes its entire contents into a stream buffer.
///
/// Automatically chunks the data based on the stream buffer's target size for
/// efficient operation. The stream buffer is automatically finished after all
/// data is read.
///
/// **IMPORTANT:** The stream buffer is invalidated after this call.
///
/// @param sb The stream buffer (invalidated after call)
/// @param file VFS file to read from (optionally closed based on close parameter)
/// @param close If true, the file is closed after reading
/// @return true on success, false on error
///
/// Example:
/// @code
///   VFSFile *file = vfsOpen(vfs, _S"data.txt", FS_Read);
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufStrCRegisterPush(sb, &output);
///   sbufFileIn(sb, file, true);  // file is closed automatically
/// @endcode
bool sbufFileIn(_Pre_valid_ _Post_invalid_ StreamBuffer* sb, _Pre_valid_ _When_(close, _Post_invalid_) VFSFile* file,
                bool close);

/// bool sbufFilePRegisterPull(StreamBuffer *sb, VFSFile *file, bool close)
///
/// Registers a VFS file as a producer with the stream buffer in pull mode.
///
/// In pull mode, the consumer pulls data as needed, and the file is read in
/// chunks on demand. Use this instead of sbufFileIn() when you need finer
/// control over when data is read.
///
/// @param sb The stream buffer
/// @param file VFS file to read from (optionally closed when producer finishes)
/// @param close If true, the file is closed when the producer finishes
/// @return true on success, false if registration failed
_Check_return_ bool
sbufFilePRegisterPull(_Inout_ StreamBuffer* sb, _Inout_ VFSFile* file, bool close);

/// @}  // end of serialize_file_producer

/// @defgroup serialize_file_consumer VFS File Consumers
/// @ingroup serialize_file
/// @{
///
/// Functions for using VFS files as stream buffer data sinks.

/// bool sbufFileOut(StreamBuffer *sb, VFSFile *file, bool close)
///
/// Consumes all available data from the buffer and writes it to a VFS file.
///
/// Reads data from the stream buffer until the producer finishes (EOF) and
/// writes it to the file in chunks.
///
/// **IMPORTANT:** The stream buffer is invalidated after this call.
///
/// @param sb The stream buffer (invalidated after call)
/// @param file VFS file to write to (optionally closed based on close parameter)
/// @param close If true, the file is closed after writing
/// @return true on success, false on error
///
/// Example:
/// @code
///   VFSFile *file = vfsOpen(vfs, _S"output.txt", FS_Write | FS_Create);
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufStrPRegisterPull(sb, inputData);
///   sbufFileOut(sb, file, true);  // file is closed automatically
/// @endcode
bool sbufFileOut(_Pre_valid_ _Post_invalid_ StreamBuffer* sb, _Pre_valid_ _When_(close, _Post_invalid_) VFSFile* file,
                 bool close);

/// bool sbufFileCRegisterPush(StreamBuffer *sb, VFSFile *file, bool close)
///
/// Registers a VFS file as a consumer with the stream buffer in push mode.
///
/// In push mode, data is automatically written to the file as it becomes
/// available from the producer. Use this instead of sbufFileOut() when you need
/// the producer and consumer to operate asynchronously.
///
/// @param sb The stream buffer
/// @param file VFS file to write to (optionally closed when consumer finishes)
/// @param close If true, the file is closed when the consumer finishes
/// @return true on success, false if registration failed
_Check_return_ bool
sbufFileCRegisterPush(_Inout_ StreamBuffer* sb, _Inout_ VFSFile* file, bool close);

/// @}  // end of serialize_file_consumer
/// @}  // end of serialize_file
