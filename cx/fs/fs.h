#pragma once

#include <cx/cx.h>

// ---------- Platform-specific filesystem operations ----------
//
// This module provides direct access to the operating system's filesystem
// using platform-native paths. For platform-independent path manipulation,
// see path.h. For a virtual filesystem abstraction, see vfs.h.
//
// All paths are UTF-8 encoded.

CX_C_BEGIN

// Converts a platform-specific path into a normalized CX path
//
// Platform-specific path formats:
//   Windows: C:\path\to\file or \\server\share\file
//   Unix: /path/to/file
//
// CX path format: namespace:/path/to/file
//   Windows: Drive letters become namespaces (c:/path/to/file)
//            UNC paths use unc namespace (unc:/server/share/file)
//   Unix: No namespace, just /path/to/file
//
// The output path is normalized (forward slashes, redundant elements removed).
//
// Parameters:
//   out - String to receive the converted path (prior contents destroyed)
//   platformpath - Platform-specific path to convert
void pathFromPlatform(_Inout_ string* out, _In_opt_ strref platformpath);

// Converts a normalized CX path into a platform-specific path
//
// This is the inverse of pathFromPlatform. The output uses the native
// path format required by OS-level file operations.
//
// Parameters:
//   out - String to receive the platform path (prior contents destroyed)
//   path - CX normalized path to convert
void pathToPlatform(_Inout_ string* out, _In_opt_ strref path);

// Gets the current working directory
//
// Returns the OS's current working directory as a normalized CX path.
// This is a platform-specific concept and may vary per-thread on some systems.
//
// Parameters:
//   out - String to receive the current directory (prior contents destroyed)
void fsCurDir(_Inout_ string* out);

// Sets the current working directory
//
// Changes the OS's current working directory. This affects relative path
// resolution for all filesystem operations in the process.
//
// Parameters:
//   cur - Path to set as current directory (can be relative or absolute)
//
// Returns:
//   true if successful, false on error (directory doesn't exist, no permission, etc.)
bool fsSetCurDir(_In_opt_ strref cur);

// Converts a relative path to an absolute path using the current directory
//
// If the input path is already absolute, it is normalized and returned.
// If relative, it is resolved against the OS's current working directory.
//
// Parameters:
//   out - String to receive the absolute path (prior contents destroyed)
//   path - Relative or absolute path to convert
//
// Returns:
//   true if the path was made absolute (was relative), false if already absolute
bool pathMakeAbsolute(_Inout_ string* out, _In_opt_ strref path);

// Gets the full path to the currently running executable
//
// Returns the absolute path to the executable file that is currently running.
// Useful for locating resources relative to the executable.
//
// Parameters:
//   out - String to receive the executable path (prior contents destroyed)
void fsExe(_Inout_ string* out);

// Gets the directory containing the currently running executable
//
// Returns the parent directory of the current executable. Equivalent to
// calling pathParent on the result of fsExe().
//
// Parameters:
//   out - String to receive the executable directory (prior contents destroyed)
void fsExeDir(_Inout_ string* out);

// Path type enumeration
//
// Returned by fsStat to indicate what kind of filesystem object exists
// at a given path (if any).
typedef enum FSPathStatEnum {
    FS_Nonexistent,   // Path does not exist
    FS_Directory,     // Path is a directory
    FS_File           // Path is a regular file
} FSPathStat;

// Filesystem object metadata
//
// Contains timestamps and size information for a file or directory.
// All timestamps are in CX time format (microseconds since epoch).
typedef struct FSStat {
    uint64 size;      // Size in bytes (0 for directories)
    int64 created;    // Creation time (birth time)
    int64 modified;   // Last modification time
    int64 accessed;   // Last access time
} FSStat;

// Gets information about a filesystem object
//
// Queries the filesystem for information about the object at the given path.
// This can determine if a path exists and what type of object it is, and
// optionally retrieve detailed metadata.
//
// Parameters:
//   path - Path to query (can be relative or absolute)
//   stat - Optional pointer to receive detailed metadata (can be NULL)
//
// Returns:
//   FS_Nonexistent if the path doesn't exist
//   FS_Directory if the path is a directory
//   FS_File if the path is a regular file
//
// Example:
//   FSStat stat;
//   if (fsStat(_S"file.txt", &stat) == FS_File) {
//       // file exists, stat.size contains the size
//   }
_Success_(return != FS_Nonexistent) FSPathStat fsStat(_In_opt_ strref path, _Out_opt_ FSStat* stat);

// Checks if a path exists in the filesystem
//
// This is a convenience wrapper around fsStat that only checks existence
// without retrieving metadata or caring about the type.
//
// Returns:
//   true if the path exists (as any type), false otherwise
_meta_inline bool fsExist(_In_opt_ strref path)
{
    return fsStat(path, NULL) != FS_Nonexistent;
}

// Checks if a path exists and is a directory
//
// Returns:
//   true if the path exists and is a directory, false otherwise
_meta_inline bool fsIsDir(_In_opt_ strref path)
{
    return fsStat(path, NULL) == FS_Directory;
}

// Checks if a path exists and is a regular file
//
// Returns:
//   true if the path exists and is a regular file, false otherwise
_meta_inline bool fsIsFile(_In_opt_ strref path)
{
    return fsStat(path, NULL) == FS_File;
}

// Sets the modification and access times for a filesystem object
//
// Updates the timestamps on a file or directory. The creation time cannot
// be modified. Timestamps are in CX time format (microseconds since epoch).
//
// Parameters:
//   path - Path to the file or directory to modify
//   modified - New modification time
//   accessed - New access time
//
// Returns:
//   true if successful, false on error (file doesn't exist, no permission, etc.)
bool fsSetTimes(_In_opt_ strref path, int64 modified, int64 accessed);

// Creates a single directory
//
// Creates a new directory at the specified path. The parent directory must
// already exist. To create multiple levels of directories at once, use
// fsCreateAll() instead.
//
// Parameters:
//   path - Path where the new directory should be created
//
// Returns:
//   true if successful or directory already exists
//   false on error (parent doesn't exist, no permission, path is a file, etc.)
bool fsCreateDir(_In_opt_ strref path);

// Creates a directory and all necessary parent directories
//
// Recursively creates all directories in the path that don't already exist.
// Similar to 'mkdir -p' on Unix. Safe to call on existing directories.
//
// Parameters:
//   path - Full path to create
//
// Returns:
//   true if all directories were created or already existed
//   false on error (no permission, path component is a file, etc.)
bool fsCreateAll(_In_opt_ strref path);

// Removes an empty directory
//
// Deletes a directory from the filesystem. The directory must be empty
// (contain no files or subdirectories). To delete files, use fsDelete().
//
// Parameters:
//   path - Path to the directory to remove
//
// Returns:
//   true if successful, false on error (doesn't exist, not empty, no permission, etc.)
bool fsRemoveDir(_In_opt_ strref path);

// Deletes a file
//
// Removes a file from the filesystem. This cannot delete directories;
// use fsRemoveDir() for that.
//
// Parameters:
//   path - Path to the file to delete
//
// Returns:
//   true if successful, false on error (doesn't exist, is a directory, no permission, etc.)
bool fsDelete(_In_opt_ strref path);

// Renames or moves a file or directory
//
// Moves a file or directory from one path to another. Can be used to rename
// (if in the same directory) or move to a different directory. On most systems,
// this is atomic if both paths are on the same filesystem.
//
// If 'to' already exists, behavior is platform-dependent:
//   Windows: Fails if destination exists
//   Unix: Overwrites destination if it's a file
//
// Parameters:
//   from - Current path of the file or directory
//   to - New path for the file or directory
//
// Returns:
//   true if successful, false on error (source doesn't exist, no permission, etc.)
bool fsRename(_In_opt_ strref from, _In_opt_ strref to);

// Directory search iterator
//
// Opaque structure for iterating through directory contents.
// Use fsSearchInit/fsSearchNext/fsSearchFinish to enumerate files and
// subdirectories. Always call fsSearchFinish when done, even if the
// iteration ends early.
typedef struct FSSearch FSSearch;

// Directory search result
//
// Contains information about one entry found during directory iteration.
// The 'name' string is valid only until the next call to fsSearchNext
// or fsSearchFinish.
typedef struct FSSearchIter {
    string name;   // Filename (not full path, valid until next search operation)
    int type;      // FSPathStat type (FS_File or FS_Directory)
    FSStat stat;   // File metadata (only valid if stat=true in fsSearchInit)
    // private
    void* _search;   // Internal search state - do not access
} FSSearchIter;

// Begins iteration over directory contents
//
// Initializes a directory search iterator. After this call, use fsSearchNext
// in a loop to retrieve entries. Always call fsSearchFinish when done,
// even if breaking out of the loop early.
//
// Parameters:
//   iter - Iterator structure to initialize (will be cleared first)
//   path - Directory path to search
//   pattern - Optional wildcard pattern to filter results (NULL for all entries)
//             Patterns support * and ? wildcards (e.g., "*.txt")
//   stat - If true, populate the stat field for each entry (slower)
//
// Returns:
//   true if the directory was opened successfully and the first entry was found
//   false if the directory doesn't exist, can't be read, or is empty
//
// Example:
//   FSSearchIter iter;
//   if (fsSearchInit(&iter, _S"/some/dir", _S"*.txt", false)) {
//       do {
//           printf("Found: %s\n", strC(iter.name));
//       } while (fsSearchNext(&iter));
//   }
//   fsSearchFinish(&iter);
bool fsSearchInit(_Out_ FSSearchIter* iter, _In_opt_ strref path, _In_opt_ strref pattern,
                  bool stat);

// Advances to the next directory entry
//
// Call this in a loop after fsSearchInit to iterate through all matching
// entries. The iter->name field is updated with each entry.
//
// Parameters:
//   iter - Active search iterator (from fsSearchInit)
//
// Returns:
//   true if another entry was found (iter->name is valid)
//   false if no more entries (end of directory reached)
bool fsSearchNext(_Inout_ FSSearchIter* iter);

// Completes directory iteration and releases resources
//
// Must be called after fsSearchInit to clean up the iterator, even if
// iteration was terminated early. Safe to call multiple times or on
// an uninitialized iterator.
//
// Parameters:
//   iter - Search iterator to clean up
void fsSearchFinish(_Inout_ FSSearchIter* iter);

// Checks if a search iterator is valid and has a current entry.
// This can be used to make fsSearch loops more convenient.
//
// Returns:
//   true if iter->name contains a valid entry, false otherwise
//
// Example:
//   FSSearchIter iter;
//   fsSearchInit(&iter, _S"/some/dir", _S"*.txt", false);
//   while(fsSearchValid(&iter)) {
//       printf("Found: %s\n", strC(iter.name));
//       fsSearchNext(&iter);
//   }
//   fsSearchFinish(&iter);
_meta_inline bool fsSearchValid(_In_ FSSearchIter* iter)
{
    return iter->name;
}

CX_C_END
