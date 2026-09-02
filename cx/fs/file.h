/// @file file.h
/// @brief Low-level file I/O operations

/// @defgroup fs_file Native Files
/// @ingroup fs
/// @{
///
/// Provides direct access to the operating system's file I/O APIs for
/// synchronous read/write operations. This is a thin wrapper around
/// platform-native file handles with minimal buffering.
///
/// Key characteristics:
///   - Synchronous I/O only (blocking operations)
///   - Uses OS-provided buffering (no additional CX buffering layer)
///   - Binary mode (no line-ending translation)
///   - Not line-oriented (use string or format libraries for text parsing)
///
/// An open file is a File object no matter which layer opened it, so `FSFile` and `VFSFile` are
/// both just other names for `File`. A function that reads and writes can take a `File*` and be
/// handed either one; the type does not record where the handle came from.
///
/// The `fs*` calls below and the `vfs*` calls in vfs.h are the same operations as `fileRead()`,
/// `fileWrite()` and the rest, under names that say which layer the file was opened through.
/// Which spelling you use is a matter of readability.
///
/// For higher-level filesystem operations (directories, metadata), see fs.h.
/// For virtual filesystem abstraction, see vfs.h.

#pragma once

#include <cx/cx.h>
#include <cx/fs/fs.h>
#include <cx/fs/fileobj.h>

CX_C_BEGIN

/// A file opened on the OS filesystem with fsOpen()
typedef File FSFile;

/// Opens a file for I/O operations
///
/// Creates a file handle for reading, writing, or both. Close it with fsClose() when done, or
/// hold on to it and release it later with objRelease().
///
/// Common flag combinations:
///   - `FS_Read` - Open existing file for reading
///   - `FS_Write` - Open existing file for writing
///   - `FS_Read | FS_Write` - Open existing file for read/write
///   - `FS_Write | FS_Create` - Open or create file for writing
///   - `FS_Overwrite` - Create new or truncate existing file
///   - `FS_Read | FS_Write | FS_Create` - Open or create for read/write
///
/// The FS_Lock flag requests exclusive write access. On Windows, other processes
/// can still read the file but cannot write. On Unix, this is advisory locking.
///
/// @param path Path to the file to open (can be relative or absolute)
/// @param flags Combination of FSOpenFlags specifying open mode
/// @return File handle on success, NULL on failure. Common failures: file doesn't exist (without
/// FS_Create), no permission, path is a directory, disk full (with FS_Create)
///
/// Example:
/// @code
///   FSFile *f = fsOpen(_SL("data.bin"), FS_Read);
///   if (f) {
///       // ... read operations ...
///       fsClose(f);
///   }
/// @endcode
_Ret_opt_valid_ FSFile* fsOpen(_In_opt_ strref path, flags_t flags);

/// Closes a file and releases the handle
///
/// Flushes pending writes, closes the underlying OS handle, and drops the reference this caller
/// owns. Use this for the common case of one owner opening a file, using it, and being done
/// with it. If the handle is shared, close it with fileClose() and release each reference with
/// objRelease() instead.
///
/// @param file File handle to close (may be NULL)
/// @return true if successful, false if an error occurred during flush/close
///
/// @note The reference is dropped even if this returns false
_meta_inline bool fsClose(_Pre_opt_valid_ _Post_invalid_ FSFile* file)
{
    bool ret = fileClose(file);
    objRelease(&file);
    return ret;
}

/// Reads data from a file
///
/// Reads up to 'sz' bytes from the current file position into the buffer.
/// The file position is advanced by the number of bytes actually read.
/// Reading less than requested is not an error - it indicates end-of-file
/// or incomplete data available.
///
/// @param file Open file handle (must have FS_Read flag)
/// @param buf Buffer to receive the data (must be at least 'sz' bytes)
/// @param sz Maximum number of bytes to read
/// @param bytesread Receives the actual number of bytes read (can be 0 at EOF)
/// @return true if the read operation succeeded (even if 0 bytes read due to EOF), false if an I/O
/// error occurred (bytesread will be set to 0)
///
/// Example:
/// @code
///   uint8 buffer[1024];
///   size_t n;
///   if (fsRead(file, buffer, sizeof(buffer), &n)) {
///       // successfully read n bytes (0 means EOF)
///       if (n > 0) {
///           // process buffer...
///       }
///   }
/// @endcode
_meta_inline bool fsRead(_Inout_ FSFile* file, _Out_writes_bytes_to_(sz, *bytesread) void* buf,
                         size_t sz, _Out_ _Deref_out_range_(0, sz) size_t* bytesread)
{
    if (!file) {
        *bytesread = 0;
        return false;
    }
    return fileRead(file, buf, sz, bytesread);
}

/// Writes data to a file
///
/// Writes 'sz' bytes from the buffer to the file at the current position.
/// The file position is advanced by the number of bytes written.
///
/// On most systems, this function will write all requested bytes or fail.
/// However, on some systems (particularly networked filesystems), a short
/// write may occur. Check byteswritten to verify the full write completed.
///
/// @param file Open file handle (must have FS_Write flag)
/// @param buf Buffer containing data to write
/// @param sz Number of bytes to write
/// @param byteswritten Optional pointer to receive actual bytes written (can be NULL)
/// @return true if the write succeeded (check byteswritten for actual amount), false if an I/O
/// error occurred. Common failures: disk full, quota exceeded, permission denied
///
/// Example:
/// @code
///   uint8 data[] = { 1, 2, 3, 4 };
///   size_t written;
///   if (fsWrite(file, data, sizeof(data), &written) && written == sizeof(data)) {
///       // all data written successfully
///   }
/// @endcode
_meta_inline bool fsWrite(_Inout_ FSFile* file, _In_reads_bytes_(sz) const void* buf, size_t sz,
                          _Out_opt_ _Deref_out_range_(0, sz) size_t* byteswritten)
{
    if (!file) {
        if (byteswritten)
            *byteswritten = 0;
        return false;
    }
    return fileWrite(file, buf, sz, byteswritten);
}

/// Writes a string to a file
///
/// The string contents are written as-is, with no line ending added.
///
/// @param file Open file handle (must have FS_Write flag)
/// @param str String to write
/// @param byteswritten Optional pointer to receive actual bytes written (can be NULL)
/// @return true on success, false on I/O error
_meta_inline bool fsWriteString(_Inout_ FSFile* file, _In_opt_ strref str,
                                _Out_opt_ size_t* byteswritten)
{
    if (!file) {
        if (byteswritten)
            *byteswritten = 0;
        return false;
    }
    return fileWriteString(file, str, byteswritten);
}

/// Gets the current file position
///
/// Returns the current byte offset within the file. This is the position
/// where the next read or write operation will occur. The position starts
/// at 0 (beginning of file) when the file is opened.
///
/// @param file Open file handle
/// @return Current file position in bytes (0 = start of file), -1 on error
_meta_inline int64 fsTell(_Inout_ FSFile* file)
{
    if (!file)
        return -1;
    return fileTell(file);
}

/// Changes the current file position
///
/// Moves the file position to a new location for subsequent read/write
/// operations. The position can be set relative to the beginning, current
/// position, or end of the file.
///
/// @param file Open file handle
/// @param off Offset in bytes (can be negative for FS_Cur and FS_End)
/// @param seektype Reference point: FS_Set (start), FS_Cur (current), FS_End (end)
/// @return New file position in bytes from start of file, -1 on error (invalid position, unseekable
/// file)
///
/// Examples:
/// @code
///   fsSeek(file, 0, FS_Set);        // Seek to beginning
///   fsSeek(file, 100, FS_Cur);      // Skip forward 100 bytes
///   fsSeek(file, 0, FS_End);        // Seek to end
///   fsSeek(file, -10, FS_End);      // Seek to 10 bytes before end
///   fsSeek(file, 1024, FS_Set);     // Seek to absolute position 1024
/// @endcode
_meta_inline int64 fsSeek(_Inout_ FSFile* file, int64 off, FSSeekType seektype)
{
    if (!file)
        return -1;
    return fileSeek(file, off, seektype);
}

/// Flushes buffered writes to disk
///
/// Forces any buffered write data to be physically written to the storage
/// device. This ensures data durability but may be slow. The OS may buffer
/// writes for performance; this function ensures they reach the disk.
///
/// @param file Open file handle
/// @return true if successful, false if flush failed (I/O error, disk full, etc.)
///
/// @note fsClose() automatically flushes, so explicit flushing is only
/// needed for long-lived files or when durability is critical (e.g., after
/// writing a transaction log entry).
_meta_inline bool fsFlush(_Inout_ FSFile* file)
{
    if (!file)
        return false;
    return fileFlush(file);
}

/// @}

CX_C_END
