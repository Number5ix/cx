#pragma once

#include <cx/cx.h>
#include <cx/utils/macros.h>

// ---------- Platform-indepedent path manipulation ----------
// All pathnames are UTF-8!

// CX pathnames take the form of:
//     namespace:/path/to/file
//
// The meaning of namespace depends on the context in which the path is used.
//
// Windows filesystem: Each drive letter is its own namespace, unc namespace is used for UNC paths
// Unix filesystem: None, namespace is ignored
// VFS: Identifies a VFS namespace (if that namespace is allowed by the view)

CX_C_BEGIN

// Eliminate redundant path elements, convert backslashes, etc
void pathNormalize(string *path);

// Same as pathNormalize, but outputs an sarray of strings
// returns true if it was an absolute path, false for relative
bool pathDecompose(string *ns, string **components, string path);

// Recomposes a path
bool pathCompose(string *out, string ns, string *components);

// Is this an absolute or relative path?
bool pathIsAbsolute(string path);

// Get parent directory
bool pathParent(string *out, string path);

// Get filename
bool pathFilename(string *out, string path);

// Split a full path into a namespace/path components
bool pathSplitNS(string *nspart, string *pathpart, string path);

// For convenience, just string concats with the path separator
bool _pathJoin(string *out, int n, string* elements);
#define pathJoin(out, ...) _pathJoin(out, count_macro_args(__VA_ARGS__), (string[]){ __VA_ARGS__ })

// Add filename extension
void pathAddExt(string *out, string path, string ext);
bool pathRemoveExt(string *out, string path);
bool pathGetExt(string *out, string path);
void pathSetExt(string *out, string path, string ext);

// Path wildcard matching
enum PathMatchFlags {
    PATH_LeadingDir       = 1,      // Ignore /* after successful match
    PATH_IgnorePath       = 2,      // match slashes as regular character
    PATH_CaseInsensitive  = 4,      // Ignore case differences
};
bool pathMatch(string path, string pattern, uint32 flags);

CX_C_END
