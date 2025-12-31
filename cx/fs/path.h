/// @file path.h
/// @brief Platform-independent path manipulation

/// @defgroup fs_path Path Manipulation
/// @ingroup fs
/// @{
///
/// All pathnames are UTF-8 encoded.
///
/// CX uses a unified path format: `namespace:/path/to/file`
///
/// The namespace prefix is optional and its meaning depends on context:
///   - Windows filesystem: Drive letters (c:, d:) or 'unc' for UNC paths (unc:/server/share)
///   - Unix filesystem: No namespace (or ignored if present)
///   - VFS: Identifies a virtual filesystem namespace
///
/// Path separators are forward slashes (/). Backslashes are converted to
/// forward slashes during normalization.

#pragma once

#include <cx/container/sarray.h>
#include <cx/cx.h>

CX_C_BEGIN

/// Normalizes a path by eliminating redundant elements
///
/// Performs the following transformations:
///   - Converts backslashes (\) to forward slashes (/)
///   - Removes redundant slashes (// becomes /)
///   - Resolves . (current directory) references
///   - Resolves .. (parent directory) references
///   - Handles namespace:path format correctly
///
/// The path is modified in place. If already normalized, no changes are made.
///
/// @param path Path to normalize (modified in place)
///
/// Example:
/// @code
///   string p = 0;
///   strDup(&p, _S"c:\\dir\\..\\file.txt");
///   pathNormalize(&p);  // Result: "c:/file.txt"
/// @endcode
void pathNormalize(_Inout_ string* path);

/// Decomposes a path into namespace and component parts
///
/// Breaks a path into its constituent parts for analysis or manipulation.
/// The path is normalized during decomposition (backslashes converted, etc.).
/// Empty components (from redundant slashes) and current/parent directory
/// references are resolved and removed.
///
/// For absolute paths, the first component may be an empty string if the path
/// represents a namespace root (i.e. c:/).
///
/// @param ns Receives the namespace portion (empty if no namespace)
/// @param components Array to receive path components (will be cleared first)
/// @param path Path to decompose
/// @return true if the path is absolute, false if relative
///
/// Example:
/// @code
///   string ns = 0;
///   sa_string parts;
///   saInit(&parts, string, 0);
///   bool abs = pathDecompose(&ns, &parts, _S"c:/dir/file.txt");
///   // abs = true, ns = "c", parts = ["dir", "file.txt"]
/// @endcode
bool pathDecompose(_Inout_ string* ns, _In_ sa_string* components, _In_opt_ strref path);

/// Reconstructs a path from namespace and components
///
/// Builds a complete path string from separated parts. This is the inverse
/// operation of pathDecompose(). Components are joined with forward slashes.
///
/// @param out Receives the composed path (prior contents destroyed)
/// @param ns Namespace prefix (can be empty/NULL for no namespace)
/// @param components Array of path components to join
/// @return true on success, false on failure
bool pathCompose(_Inout_ string* out, _In_opt_ strref ns, _In_ sa_string components);

/// Checks if a path is absolute or relative
///
/// A path is considered absolute if:
///   - It has a namespace prefix (namespace:...)
///   - It begins with a forward slash (/)
///
/// All other paths are relative.
///
/// @param path Path to check
/// @return true if the path is absolute, false if relative
bool pathIsAbsolute(_In_opt_ strref path);

/// Extracts the parent directory from a path
///
/// Returns the directory containing the given path. For a file path, this
/// is the directory containing the file. For a directory path, this is the
/// parent directory.
///
/// Special cases:
///   - Root paths (/, c:/) return themselves with trailing slash
///   - Paths with no parent return false
///
/// @param out Receives the parent directory path (prior contents destroyed)
/// @param path Path to get parent of
/// @return true if a parent exists, false if path is at root or has no parent
///
/// Example:
/// @code
///   string parent = 0;
///   pathParent(&parent, _S"c:/dir/file.txt");  // Result: "c:/dir"
///   pathParent(&parent, _S"c:/dir");           // Result: "c:/"
/// @endcode
bool pathParent(_Inout_ string* out, _In_opt_ strref path);

/// Extracts the filename from a path
///
/// Returns the last component of a path, which is typically the filename.
/// This includes the file extension if present. For directory paths ending
/// in a separator, returns an empty string.
///
/// @param out Receives the filename (prior contents destroyed)
/// @param path Path to extract filename from
/// @return true if the path contained a directory component, false otherwise
///
/// Example:
/// @code
///   string name = 0;
///   pathFilename(&name, _S"c:/dir/file.txt");  // Result: "file.txt"
///   pathFilename(&name, _S"file.txt");         // Result: "file.txt" (returns false)
/// @endcode
bool pathFilename(_Inout_ string* out, _In_opt_ strref path);

/// Splits a path into namespace and path portions
///
/// Separates the namespace prefix (if any) from the rest of the path.
/// The colon (:) separator is removed. If no namespace exists, nspart
/// will be empty and pathpart will contain the entire path.
///
/// @param nspart Receives the namespace portion (prior contents destroyed)
/// @param pathpart Receives the path portion (prior contents destroyed)
/// @param path Path to split
/// @return true on success, false if either output pointer is NULL
///
/// Example:
/// @code
///   string ns = 0, path = 0;
///   pathSplitNS(&ns, &path, _S"c:/dir/file.txt");
///   // ns = "c", path = "/dir/file.txt"
/// @endcode
bool pathSplitNS(_Inout_ string* nspart, _Inout_ string* pathpart, _In_opt_ strref path);

bool _pathJoin(_Inout_ string* out, int n, _In_ strref* elements);

/// bool pathJoin(string *out, ...);
///
/// Joins multiple path components with separators
///
/// Concatenates path components using forward slashes as separators.
/// This is a convenience function for building paths. Empty components
/// are skipped. If an absolute path appears after the first argument,
/// the operation fails.
///
/// @param out Receives the joined path (prior contents destroyed)
/// @param ... Variable number of path components (strref) to join
/// @return true on success, false if an absolute path appears mid-sequence
///
/// Example:
/// @code
///   string path = 0;
///   pathJoin(&path, _S"c:/dir", _S"subdir", _S"file.txt");
///   // Result: "c:/dir/subdir/file.txt"
/// @endcode
#define pathJoin(out, ...) _pathJoin(out, count_macro_args(__VA_ARGS__), (strref[]) { __VA_ARGS__ })

/// Adds a file extension to a path
///
/// Appends a period and the specified extension to the path. Does not
/// check if an extension already exists - use pathSetExt() to replace
/// an existing extension.
///
/// @param out Receives path with extension added (prior contents destroyed)
/// @param path Base path
/// @param ext Extension to add (without leading period)
///
/// Example:
/// @code
///   string p = 0;
///   pathAddExt(&p, _S"file", _S"txt");  // Result: "file.txt"
/// @endcode
void pathAddExt(_Inout_ string* out, _In_opt_ strref path, _In_opt_ strref ext);

/// Removes the file extension from a path
///
/// Strips the extension (including the period) from a filename. Only the
/// last extension is removed.
///
/// @param out Receives path without extension (prior contents destroyed)
/// @param path Path to remove extension from
/// @return true if an extension was found and removed, false otherwise
///
/// Example:
/// @code
///   string p = 0;
///   pathRemoveExt(&p, _S"file.txt");     // Result: "file"
///   pathRemoveExt(&p, _S"file.tar.gz");  // Result: "file.tar"
/// @endcode
bool pathRemoveExt(_Inout_ string* out, _In_opt_ strref path);

/// Extracts the file extension from a path
///
/// Returns just the extension portion (without the period) of the last
/// component in the path. If the last component has no extension or is
/// a directory, returns false.
///
/// @param out Receives the extension without period (prior contents destroyed)
/// @param path Path to extract extension from
/// @return true if an extension was found, false otherwise
///
/// Example:
/// @code
///   string ext = 0;
///   pathGetExt(&ext, _S"file.txt");  // Result: "txt"
/// @endcode
bool pathGetExt(_Inout_ string* out, _In_opt_ strref path);

/// Replaces or sets the file extension of a path
///
/// Removes any existing extension and adds the specified one. This is
/// equivalent to calling pathRemoveExt() followed by pathAddExt().
///
/// @param out Receives path with new extension (prior contents destroyed)
/// @param path Path to modify
/// @param ext New extension (without leading period)
///
/// Example:
/// @code
///   string p = 0;
///   pathSetExt(&p, _S"file.txt", _S"bak");  // Result: "file.bak"
/// @endcode
void pathSetExt(_Inout_ string* out, _In_opt_ strref path, _In_opt_ strref ext);

/// Path pattern matching flags
///
/// Control how wildcard patterns are matched against paths.
enum PathMatchFlags {
    PATH_LeadingDir = 1,   ///< Ignore /* after successful match
    PATH_IgnorePath = 2,   ///< Treat slashes as regular characters (not path separators)
    PATH_Smart = 4,   ///< Enable smart matching mode: Patterns starting with / match full paths
                      ///< (like PATH_LeadingDir). Patterns without leading / match only the
                      ///< filename component
    PATH_CaseInsensitive = 8,   ///< Perform case-insensitive matching
};

/// Tests if a path matches a wildcard pattern
///
/// Supports standard wildcards:
///   - `*` - Matches zero or more characters (excluding / unless PATH_IgnorePath)
///   - `?` - Matches exactly one character (excluding / unless PATH_IgnorePath)
///
/// By default, matching is path-aware (/ is a special separator). Use
/// PATH_IgnorePath to treat paths as simple strings.
///
/// PATH_Smart mode provides intuitive behavior:
///   - Pattern "/dir/*.txt" matches files in /dir/ at any depth
///   - Pattern "*.txt" matches only files named *.txt (ignoring directory)
///
/// @param path Path to test
/// @param pattern Wildcard pattern
/// @param flags Combination of PathMatchFlags
/// @return true if the path matches the pattern, false otherwise
///
/// Examples:
/// @code
///   pathMatch(_S"dir/file.txt", _S"*.txt", 0);                    // false
///   pathMatch(_S"dir/file.txt", _S"*.txt", PATH_Smart);           // true (filename only)
///   pathMatch(_S"dir/file.txt", _S"dir/*.txt", 0);                // true
///   pathMatch(_S"Dir/File.TXT", _S"dir/*.txt", PATH_CaseInsensitive); // true
/// @endcode
bool pathMatch(_In_opt_ strref path, _In_opt_ strref pattern, uint32 flags);

/// @}

CX_C_END
