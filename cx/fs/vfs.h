#pragma once

// ---------- Virtual Filesystem (VFS) ----------
//
// The VFS provides a unified, layered filesystem abstraction that can combine
// multiple data sources into a single namespace. This enables powerful features:
//
//   - Mount real OS filesystems at any path
//   - Stack multiple read-only layers with a writable overlay (Copy-On-Write)
//   - Create virtual filesystems backed by archives, memory, network, etc.
//   - Provide sandboxed filesystem access to untrusted code
//
// VFS Architecture:
//   A VFS is built from one or more "providers" mounted at specific paths.
//   When a file is accessed, the VFS searches mounted providers in order:
//     1. Most recently mounted provider is checked first
//     2. If not found (or read-only), check the next layer
//     3. For writes, COW semantics can copy from lower layers
//
// Common Use Cases:
//   - Game asset system: Mount game files with mod overlay
//   - Application data: OS filesystem with bundled default files as fallback
//   - Testing: Mock filesystem without touching real disk
//
// See vfsobj.h for vfsCreate().
// See vfsprovider.h for implementing custom providers.

#include <cx/fs/file.h>
#include <cx/fs/fs.h>
#include <cx/fs/vfsprovider.h>
#include <cx/fs/vfsobj.h>

// Virtual filesystem handle
//
// Opaque structure representing a virtual filesystem instance.
// Create with vfsCreate() and destroy with vfsDestroy().
typedef struct VFS VFS;

// VFS file handle
//
// Opaque structure representing an open file in a VFS.
// Similar to FSFile but works through the VFS layer.
typedef struct VFSFile VFSFile;

CX_C_BEGIN

// Destroys a VFS and releases all resources
//
// Unmounts all providers, releases all references, and frees the VFS.
// All open VFSFile handles must be closed before destroying the VFS.
// Sets *pvfs to NULL after destruction. Safe to call with NULL.
//
// To release an acquired VFS object handle without destroying it, use
// objRelease(&vfs) instead.
//
// Parameters:
//   pvfs - Pointer to VFS handle to destroy
_At_(*pvfs, _Pre_maybenull_ _Post_null_) void vfsDestroy(VFS** pvfs);

// Unmounts all providers at a specific path
//
// Removes all mounted providers from the specified mount point. Any files
// or directories provided by those providers become inaccessible.
//
// Parameters:
//   vfs - VFS instance
//   path - Mount path to unmount (must be absolute)
//
// Returns:
//   true if successfully unmounted, false on error
bool vfsUnmount(_Inout_ VFS *vfs, _In_opt_ strref path);

// Internal function - use vfsMountProvider() macro instead
bool _vfsMountProvider(_Inout_ VFS *vfs, _Inout_ ObjInst *provider, _In_opt_ strref path, flags_t flags);

// bool vfsMountProvider(VFS *vfs, provider, strref path, [flags_t flags]);
//
// Mounts a custom VFS provider at the specified path
//
// Attaches a VFS provider (implementing the VFSProvider interface) to the
// VFS at the given mount point. The provider handles all file operations
// for paths under this mount point.
//
// Providers are searched in reverse mount order (most recent first).
// Multiple providers can be mounted at the same path to create layers.
//
// Parameters:
//   vfs - VFS instance
//   provider - Provider instance (must implement VFSProvider interface)
//   path - Absolute path where provider should be mounted
//   flags - Optional combination of VFS mount flags
//
// Returns:
//   true if successfully mounted, false on error
//
// See: VFS_NewFiles, VFS_AlwaysCOW, VFS_Opaque flags
#define vfsMountProvider(vfs, provider, path, ...) _vfsMountProvider(vfs, objInstBase(provider), path, opt_flags(__VA_ARGS__))

// Internal function - use vfsMountFS() macro instead
bool _vfsMountFS(_Inout_ VFS *vfs, _In_opt_ strref path, _In_opt_ strref fsroot, flags_t flags);

// bool vfsMountFS(VFS *vfs, strref path, strref fsroot, [flags_t flags]);
//
// Mounts the OS filesystem into the VFS
//
// Provides access to the real filesystem through the VFS. The fsroot
// directory on the OS filesystem is mounted at the specified path in
// the VFS namespace.
//
// Parameters:
//   vfs - VFS instance
//   path - VFS path where filesystem should be mounted (e.g., "/")
//   fsroot - OS filesystem path to mount (e.g., "c:/data")
//   flags - Optional combination of VFS mount flags
//
// Returns:
//   true if successfully mounted, false on error
//
// Example:
//   VFS *vfs = vfsCreate(0);
//   vfsMountFS(vfs, _S"/", _S"c:/gamedata", VFS_ReadOnly);
//   // Now VFS path "/textures/foo.png" maps to "c:/gamedata/textures/foo.png"
#define vfsMountFS(vfs, path, fsroot, ...) _vfsMountFS(vfs, path, fsroot, opt_flags(__VA_ARGS__))

// Internal function - use vfsMountVFS() macro instead
bool _vfsMountVFS(_Inout_ VFS *vfs, _In_opt_ strref path, _Inout_ VFS *vfs2, _In_opt_ strref vfs2root, flags_t flags);

// bool vfsMountVFS(VFS *vfs, strref path, VFS *vfs2, strref vfs2root, [flags_t flags]);
//
// Mounts one VFS inside another (loopback mode)
//
// Allows a VFS to be mounted as a provider within another VFS. This enables
// creating complex filesystem hierarchies and reusing VFS configurations.
//
// Parameters:
//   vfs - VFS to mount into
//   path - Mount point in vfs
//   vfs2 - VFS to mount (source)
//   vfs2root - Root path within vfs2 to expose
//   flags - Optional combination of VFS mount flags
//
// Returns:
//   true if successfully mounted, false on error
//
// Example:
//   VFS *main = vfsCreate(0);
//   VFS *sub = vfsCreate(0);
//   vfsMountFS(sub, _S"/", _S"c:/assets");
//   vfsMountVFS(main, _S"/game", sub, _S"/");
//   // main VFS path "/game/x" now maps to sub VFS path "/x"
#define vfsMountVFS(vfs, path, vfs2, vfs2root, ...) _vfsMountVFS(vfs, path, vfs2, vfs2root, opt_flags(__VA_ARGS__))

// Gets the current directory within the VFS
//
// Each VFS maintains its own current directory for resolving relative paths.
// This is independent of the OS's current directory.
//
// Parameters:
//   vfs - VFS instance
//   out - Receives the current directory path
void vfsCurDir(_Inout_ VFS *vfs, _Inout_ string *out);

// Sets the current directory within the VFS
//
// Changes the VFS's current directory. Relative paths in subsequent
// operations will be resolved against this directory.
//
// Parameters:
//   vfs - VFS instance
//   cur - New current directory (can be relative or absolute)
//
// Returns:
//   true if successful, false if directory doesn't exist
bool vfsSetCurDir(_Inout_ VFS *vfs, _In_opt_ strref cur);

// Converts a relative path to absolute within the VFS
//
// Resolves a path against the VFS's current directory to produce an
// absolute path. If the path is already absolute, it is normalized.
//
// Parameters:
//   vfs - VFS instance
//   out - Receives the absolute path
//   path - Path to convert (relative or absolute)
void vfsAbsolutePath(_Inout_ VFS *vfs, _Inout_ string *out, _In_opt_ strref path);

#define _vfsStatAnno _Success_(return != FS_Nonexistent)

// Gets information about a filesystem object in the VFS
//
// Queries the VFS for information about a path. Searches mounted providers
// to find the object and retrieve its metadata.
//
// Parameters:
//   vfs - VFS instance
//   path - Path to query (can be relative or absolute)
//   stat - Optional pointer to receive detailed metadata
//
// Returns:
//   FS_Nonexistent if not found, FS_Directory or FS_File otherwise
_vfsStatAnno FSPathStat vfsStat(_Inout_ VFS* vfs, _In_opt_ strref path, _Out_opt_ FSStat* stat);

// Checks if a path exists in the VFS
//
// Returns:
//   true if path exists (as any type), false otherwise
_meta_inline bool vfsExist(_Inout_ VFS *vfs, _In_opt_ strref path)
{
    return vfsStat(vfs, path, NULL) != FS_Nonexistent;
}

// Checks if a path exists and is a directory
//
// Returns:
//   true if path exists and is a directory, false otherwise
_meta_inline bool vfsIsDir(_Inout_ VFS *vfs, _In_opt_ strref path)
{
    return vfsStat(vfs, path, NULL) == FS_Directory;
}

// Checks if a path exists and is a file
//
// Returns:
//   true if path exists and is a regular file, false otherwise
_meta_inline bool vfsIsFile(_Inout_ VFS *vfs, _In_opt_ strref path)
{
    return vfsStat(vfs, path, NULL) == FS_File;
}

// Sets modification and access times on a VFS object
//
// Updates timestamps through the VFS layer. The path must be in a
// writable provider.
//
// Parameters:
//   vfs - VFS instance
//   path - Path to modify
//   modified - New modification time (CX time format)
//   accessed - New access time (CX time format)
//
// Returns:
//   true if successful, false on error
bool vfsSetTimes(_Inout_ VFS *vfs, _In_opt_ strref path, int64 modified, int64 accessed);

// Creates a directory in the VFS
//
// Creates a single directory. Parent must exist. Uses the writable provider
// configured with VFS_NewFiles, or the first writable provider found.
//
// Parameters:
//   vfs - VFS instance
//   path - Directory path to create
//
// Returns:
//   true if created or already exists, false on error
bool vfsCreateDir(_Inout_ VFS *vfs, _In_opt_ strref path);

// Creates a directory and all parent directories
//
// Recursively creates all missing directories in the path.
// Similar to 'mkdir -p'.
//
// Parameters:
//   vfs - VFS instance
//   path - Full directory path to create
//
// Returns:
//   true if all directories created or exist, false on error
bool vfsCreateAll(_Inout_ VFS *vfs, _In_opt_ strref path);

// Removes an empty directory from the VFS
//
// Deletes a directory that contains no files or subdirectories.
//
// Parameters:
//   vfs - VFS instance
//   path - Directory to remove
//
// Returns:
//   true if successful, false on error (not empty, doesn't exist, etc.)
bool vfsRemoveDir(_Inout_ VFS *vfs, _In_opt_ strref path);

// Deletes a file from the VFS
//
// Removes a file through the VFS layer. The file must be in a writable
// provider.
//
// Parameters:
//   vfs - VFS instance
//   path - File to delete
//
// Returns:
//   true if successful, false on error
bool vfsDelete(_Inout_ VFS *vfs, _In_opt_ strref path);

// Copies a file within the VFS
//
// Copies data from one VFS path to another. Source and destination can
// be in different providers. This is implemented as read + write.
//
// Parameters:
//   vfs - VFS instance
//   from - Source file path
//   to - Destination file path (will be created or overwritten)
//
// Returns:
//   true if successful, false on error
bool vfsCopy(_Inout_ VFS *vfs, _In_opt_ strref from, _In_opt_ strref to);

// Renames or moves a file within the VFS
//
// Moves a file from one path to another. If source and destination are
// in the same provider, this may be a fast rename. Otherwise, it falls
// back to copy + delete.
//
// Parameters:
//   vfs - VFS instance
//   from - Current file path
//   to - New file path
//
// Returns:
//   true if successful, false on error
bool vfsRename(_Inout_ VFS *vfs, _In_opt_ strref from, _In_opt_ strref to);

// Retrieves the real OS filesystem path (VFSFS provider only)
//
// If the specified VFS path is backed by a VFSFS provider (OS filesystem),
// this returns the actual underlying OS path. This is useful for passing
// paths to external tools or system APIs that don't understand VFS.
//
// This only works for paths backed by VFSFS. Returns false for other
// provider types (memory, archive, etc.).
//
// Parameters:
//   out - Receives the OS filesystem path
//   vfs - VFS instance
//   path - VFS path to resolve
//
// Returns:
//   true if path is backed by VFSFS and OS path retrieved
//   false if path uses a different provider type
bool vfsGetFSPath(_Inout_ string *out, _Inout_ VFS *vfs, _In_opt_ strref path);

typedef struct FSSearchIter FSSearchIter;

// Begins directory iteration through the VFS
//
// Searches the specified directory in the VFS, merging results from all
// mounted providers. Files in higher-priority (more recent) mounts shadow
// files with the same name in lower layers.
//
// Use vfsSearchNext() in a loop to retrieve entries. Always call
// vfsSearchFinish() when done, even if breaking early.
//
// Parameters:
//   iter - Iterator structure to initialize
//   vfs - VFS instance
//   path - Directory to search
//   pattern - Optional wildcard pattern (NULL for all entries)
//   typefilter - FS_File, FS_Directory, or 0 for both
//   stat - If true, populate stat field for each entry (slower)
//
// Returns:
//   true if directory opened and first entry found, false otherwise
bool vfsSearchInit(_Out_ FSSearchIter *iter, _Inout_ VFS *vfs, _In_opt_ strref path,
                   _In_opt_ strref pattern, int typefilter, bool stat);

// Advances to the next directory entry in VFS search
//
// Parameters:
//   iter - Active search iterator
//
// Returns:
//   true if another entry found, false if no more entries
bool vfsSearchNext(_Inout_ FSSearchIter *iter);

// Completes VFS directory search and releases resources
//
// Must be called after vfsSearchInit() to clean up. Safe to call
// multiple times.
//
// Parameters:
//   iter - Search iterator to finish
void vfsSearchFinish(_Inout_ FSSearchIter *iter);

// Checks if a VFS search iterator is valid
//
// Returns:
//   true if iterator has a valid current entry, false otherwise
_meta_inline bool vfsSearchValid(_In_ FSSearchIter *iter) { return iter->_search; }

// Opens a file through the VFS
//
// Opens a file for I/O using the VFS layer. File operations are routed
// through the appropriate mounted provider. For writes, the VFS may
// implement Copy-On-Write if configured with VFS_AlwaysCOW.
//
// Parameters:
//   vfs - VFS instance
//   path - File path to open (can be relative or absolute)
//   flags - Combination of FS_Read, FS_Write, FS_Create, etc.
//
// Returns:
//   VFSFile handle on success, NULL on failure
//
// See: FSOpenFlags in file.h
_Ret_opt_valid_ VFSFile *vfsOpen(_Inout_ VFS *vfs, _In_opt_ strref path, flags_t flags);

// Closes a VFS file handle
//
// Flushes buffers, closes the file, and releases resources.
// The file pointer becomes invalid after this call.
//
// Parameters:
//   file - VFS file handle to close
//
// Returns:
//   true if successful, false on error
bool vfsClose(_Pre_opt_valid_ _Post_invalid_ VFSFile *file);

// Reads data from a VFS file
//
// Reads up to sz bytes from the current position. See fsRead() for
// detailed semantics.
//
// Parameters:
//   file - Open VFS file handle
//   buf - Buffer to receive data
//   sz - Maximum bytes to read
//   bytesread - Receives actual bytes read
//
// Returns:
//   true on success (even if 0 bytes at EOF), false on I/O error
bool vfsRead(_Inout_ VFSFile *file, _Out_writes_bytes_to_(sz, *bytesread) void *buf, size_t sz, _Out_ _Deref_out_range_(0, sz) size_t *bytesread);

// Writes data to a VFS file
//
// Writes sz bytes to the file. May trigger Copy-On-Write if the file
// is from a read-only layer and COW is configured.
//
// Parameters:
//   file - Open VFS file handle
//   buf - Data to write
//   sz - Number of bytes to write
//   byteswritten - Optional pointer to receive bytes written
//
// Returns:
//   true on success, false on I/O error
bool vfsWrite(_Inout_ VFSFile *file, _In_reads_bytes_(sz) void *buf, size_t sz, _Out_opt_ _Deref_out_range_(0, sz) size_t *byteswritten);

// Writes a string to a VFS file
//
// Convenience function to write string contents to a file. The string
// is written as-is (no line endings added).
//
// Parameters:
//   file - Open VFS file handle
//   str - String to write
//   byteswritten - Optional pointer to receive bytes written
//
// Returns:
//   true on success, false on I/O error
bool vfsWriteString(_Inout_ VFSFile *file, _In_opt_ strref str, _Out_opt_ size_t *byteswritten);

// Gets current position in a VFS file
//
// Returns:
//   Current byte offset from start of file, -1 on error
int64 vfsTell(_Inout_ VFSFile *file);

// Changes position in a VFS file
//
// Seeks to a new position for subsequent I/O. See fsSeek() for details.
//
// Parameters:
//   file - Open VFS file handle
//   off - Offset in bytes
//   seektype - FS_Set, FS_Cur, or FS_End
//
// Returns:
//   New file position, -1 on error
int64 vfsSeek(_Inout_ VFSFile *file, int64 off, FSSeekType seektype);

// Flushes buffered writes to storage
//
// Forces pending writes through the VFS layer to the underlying provider.
//
// Parameters:
//   file - Open VFS file handle
//
// Returns:
//   true if successful, false on error
bool vfsFlush(_Inout_ VFSFile *file);

// VFS configuration and mount flags
//
// These flags control VFS behavior and provider mounting options.
enum VFSFlags {
    // Flags valid for both VFS creation and provider mounting:

    VFS_ReadOnly = 0x00000001,        // Disallow write operations on this VFS/layer
                                      // Read-only providers can serve as base layers
                                      // with writable overlays on top

    VFS_CaseSensitive = 0x00000002,   // Case-sensitive path matching.
                                      // Efficiency of this flag (as well as of the default
                                      // case-insensitive mode) may vary depending on if the
                                      // underlying operating system provider is natively case
                                      // sensitive or not.

    VFS_NoCache = 0x00000004,   // Disable VFS directory/file cache
                                // Improves memory usage but slower repeated access

    // Flags valid only for provider mounting:

    VFS_NewFiles = 0x00000008,    // Direct new file creation to this layer
                                  // When multiple writable layers exist, this
                                  // determines where new files are created

    VFS_AlwaysCOW = 0x00000010,   // Force Copy-On-Write for all writes
                                  // Writing to files from lower layers creates
                                  // a copy in this layer, even if lower layer
                                  // is writable. Useful for mod overlays.

    VFS_Opaque = 0x00000020,      // Hide all lower layers completely
                                  // Files in lower layers are invisible even
                                  // if not shadowed by this layer
};

CX_C_END
