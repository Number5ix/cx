#pragma once

#include "fs_private.h"
#include "cx/container.h"
#include "cx/string.h"
#include "vfs.h"

typedef struct VFSDirEnt {
    string name;
    int type;
    FSStat stat;
} VFSDirEnt;
saDeclare(VFSDirEnt);

typedef struct VFSSearch {
    VFS* vfs;

    sa_VFSDirEnt ents;
    int32 idx;
} VFSSearch;

// object-like structures for VFS
// these use custom type ops instead of the object framework so that
// they can be tightly packed into arrays/hashtables

typedef struct VFSMount VFSMount;

// One provider that could serve a path, captured while the VFS locks were held.
typedef struct VFSCand {
    VFSMount* mount;     // holds a reference
    string mountpath;    // absolute VFS path of the mount point
    string relpath;      // path below the mount point, as the provider sees it
    sa_string relcomp;   // relpath, split into components
} VFSCand;
saDeclare(VFSCand);
stDeclare(VFSCand);
#define SType_VFSCand                         VFSCand*
#define STStorageType_VFSCand                 VFSCand
#define STypeArg_VFSCand(type, val)           stgeneric(opaque, &(val))
#define STypeArgPtr_VFSCand(type, val)        &stgeneric(opaque, (val))
#define STypeCheckedArg_VFSCand(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_VFSCand(type, val) stType(type), stArgPtr(type, val)

// A cache entry discovered while no lock was held, waiting to be inserted once they are back.
typedef struct VFSPendEnt {
    VFSMount* mount;   // holds a reference
    string dirpath;    // absolute VFS path of the directory this belongs to
    string name;       // entry name within that directory
    string origpath;   // path as the provider sees it
} VFSPendEnt;
saDeclare(VFSPendEnt);
stDeclare(VFSPendEnt);
#define SType_VFSPendEnt                         VFSPendEnt*
#define STStorageType_VFSPendEnt                 VFSPendEnt
#define STypeArg_VFSPendEnt(type, val)           stgeneric(opaque, &(val))
#define STypeArgPtr_VFSPendEnt(type, val)        &stgeneric(opaque, (val))
#define STypeCheckedArg_VFSPendEnt(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_VFSPendEnt(type, val) stType(type), stArgPtr(type, val)

typedef struct VFSCacheEnt {
    VFSMount* mount;   // which VFS mount this file belongs to
    string origpath;   // original path (relative to provider)
} VFSCacheEnt;

// One entry in a cached directory listing. Its name, in the provider's own case, is the key it
// is stored under.
typedef struct VFSListEnt {
    int32 type;   // FSPathStat
    FSStat stat;
} VFSListEnt;
stDeclare(VFSListEnt);
#define SType_VFSListEnt                         VFSListEnt*
#define STStorageType_VFSListEnt                 VFSListEnt
#define STypeArg_VFSListEnt(type, val)           stgeneric(opaque, &(val))
#define STypeArgPtr_VFSListEnt(type, val)        &stgeneric(opaque, (val))
#define STypeCheckedArg_VFSListEnt(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_VFSListEnt(type, val) stType(type), stArgPtr(type, val)

// One mount's whole view of one directory, cached on that directory's VFSDir.
typedef struct VFSListing {
    VFSMount* mount;   // holds a reference
    string relpath;    // the directory's real path in that provider
    hashtable ents;    // string name -> VFSListEnt, by the VFS's case rules
    bool exists;       // false if the provider has no such directory; ents is empty then
} VFSListing;
saDeclare(VFSListing);
stDeclare(VFSListing);
#define SType_VFSListing                         VFSListing*
#define STStorageType_VFSListing                 VFSListing
#define STypeArg_VFSListing(type, val)           stgeneric(opaque, &(val))
#define STypeArgPtr_VFSListing(type, val)        &stgeneric(opaque, (val))
#define STypeCheckedArg_VFSListing(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_VFSListing(type, val) stType(type), stArgPtr(type, val)

// Starts an empty listing of relpath in mount m. Takes a reference on m.
void _vfsListingInit(_Out_ VFSListing* l, _In_ VFS* vfs, _In_ VFSMount* m, _In_opt_ strref relpath);
VFSCacheEnt* _vfsCacheEntCreate(VFSMount* m, strref opath);
extern STypeOps VFSCacheEnt_ops;

typedef struct VFSDir VFSDir;
typedef struct VFSDir {
    string name;
    VFSDir* parent;       // weak ref
    VFS* vfs;             // weak ref, so the destructor can keep the VFS node count
    sa_VFSMount mounts;   // VFS providers mounted in this directory

    hashtable subdirs;    // hashtable of string/VFSDir*

    // CACHE
    hashtable files;          // hashtable of string/VFSCacheEnt*
    sa_VFSListing listings;   // at most one per mount; guarded like files
    atomic(uint64) touched;   // clockTimer() at last use
    bool cache;               // only exists to cache directory entries, can be discarded
} VFSDir;
_Ret_valid_ VFSDir* _vfsDirCreate(_Inout_ VFS* vfs, _In_opt_ VFSDir* parent);

// custom types for pointers with cleanup

stDeclare(VFSDir);
#define SType_VFSDir                         VFSDir*
#define STStorageType_VFSDir                 VFSDir*
#define STypeArg_VFSDir(type, val)           stgeneric(ptr, val)
#define STypeArgPtr_VFSDir(type, val)        (stgeneric*)stCheckPtr(ptr, (void**)(val))
#define STypeCheckedArg_VFSDir(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_VFSDir(type, val) stType(type), stArgPtr(type, val)

stDeclare(VFSCacheEnt);
#define SType_VFSCacheEnt                         VFSCacheEnt*
#define STStorageType_VFSCacheEnt                 VFSCacheEnt*
#define STypeArg_VFSCacheEnt(type, val)           stgeneric(ptr, val)
#define STypeArgPtr_VFSCacheEnt(type, val)        (stgeneric*)stCheckPtr(ptr, (void**)(val))
#define STypeCheckedArg_VFSCacheEnt(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_VFSCacheEnt(type, val) stType(type), stArgPtr(type, val)

// gets (and creates) path in VFS cache
// Must be called with vfslock held for read, or -- when exclusive is true -- with vfsdlock held
// for write instead, which excludes every reader of the directory tree on its own.
_Ret_valid_ _When_(!exclusive, _Requires_shared_lock_held_(vfs->vfslock)) VFSDir*
_vfsGetDir(_Inout_ VFS* vfs, _In_opt_ strref path, bool isfile, bool cache, bool exclusive);
// gets a file from VFS cache if it exists
// Same locking requirement as _vfsGetDir above.
_Ret_valid_ _When_(!exclusive, _Requires_shared_lock_held_(vfs->vfslock)) VFSCacheEnt*
_vfsGetFile(_Inout_ VFS* vfs, _In_opt_ strref path, bool exclusive);
// finds a suitable provider for a particular file
enum VFS_FIND_PROVIDER_ENUM {
    VFS_FindWriteFile = 0x0100,
    VFS_FindCreate    = 0x0200,
    VFS_FindDelete    = 0x0400,
    VFS_FindCache     = 0x1000,
};
_Ret_opt_valid_ VFSMount*
_vfsFindMount(_Inout_ VFS* vfs, _Inout_ string* rpath, _In_opt_ strref path,
              _Out_opt_ VFSMount** cowmount, _Inout_opt_ string* cowrpath, flags_t flags);
// Finds a mount registered directly on abspath's own VFSDir node, as opposed to a mount that
// would serve abspath as a file within its parent (which is what _vfsFindMount answers).
_Ret_opt_valid_ VFSMount* _vfsFindSelfMount(_Inout_ VFS* vfs, _In_opt_ strref abspath);
void _vfsInvalidateCache(_Inout_ VFS* vfs, _In_opt_ strref path);
void _vfsInvalidateRecursive(_Inout_ VFS* vfs, _In_ VFSDir* dir, bool havelock);
// Forgets what the cache knows inside the directory at abspath -- and, with recursive, below
// it -- without creating any directory node that is not already there. Takes the locks itself.
void _vfsInvalidatePath(_Inout_ VFS* vfs, _In_opt_ strref abspath, bool recursive);
// reads vfs->curdir, which vfsSetCurDir can replace and destroy out from under it
_Requires_shared_lock_held_(vfs->vfslock) void _vfsAbsPath(_Inout_ VFS* vfs, _Inout_ string* out,
                                                           _In_opt_ strref path);

// Builds the ordered list of providers that could serve path. Stops at the first opaque layer,
// since nothing below one is reachable. Takes a reference on every mount it records.
_Requires_shared_lock_held_(vfs->vfslock) void _vfsSnapshot(_Inout_ VFS* vfs,
                                                            _Inout_ sa_VFSCand* out,
                                                            _In_opt_ strref abspath, bool isfile);

// Inserts cache entries that were discovered with no lock held.
_Requires_exclusive_lock_held_(vfs->vfslock) void _vfsFlushPending(_Inout_ VFS* vfs,
                                                                   _In_ sa_VFSPendEnt* pending);

// Resolves components against a case-sensitive provider by walking its real directory entries,
// which is how a case-insensitive VFS finds a file whose name it only knows the wrong case of.
// Writes the provider's real path for it to out and returns its type. Files it passes on the way
// are appended to pending, to be cached once the caller has the locks back.
//
// Calls into the provider, so no VFS lock may be held.
int _vfsFindCIHelper(_Inout_ string* out, _In_opt_ strref mountpath, _In_ sa_string components,
                     _Inout_ VFSMount* mount, _Inout_ VFSProvider* provif,
                     _Inout_ sa_VFSPendEnt* pending);

// Drops cache-only directories that nothing has touched inside the configured TTL, if the tree
// has grown past the configured limit. Takes no lock; call it before acquiring any.
void _vfsMaybeEvict(_Inout_ VFS* vfs);

bool _vfsIsPlatformCaseSensitive();

// Starts keeping a VFS_CacheListings mount's listings current and marks it listable, or marks
// a VFS_Immutable one listable outright. Calls into the provider; hold no VFS lock.
void _vfsMountArmCache(_Inout_ VFS* vfs, _Inout_ VFSMount* m);

// Private VFS flags, kept clear of the public VFSFlags range.
enum VFS_PRIVATE_FLAGS_ENUM {
    // Set by vfsMountPlatformFS where drives can appear after the VFS is set up. A lookup of a
    // single-letter namespace the VFS does not have yet asks the platform to mount it.
    VFS_AutoMountDrives = 0x40000000,
    // Mount flag for _vfsMountProvider: mount only if the path's namespace does not exist yet,
    // checked under the same lock that adds it. Returns true without mounting otherwise.
    VFS_MountNewNS      = 0x80000000,
};

// Mounts the drive for a single-letter namespace if the platform has one. Takes no VFS lock
// on entry; the mount takes its own.
bool _vfsPlatformMountNamespace(_Inout_ VFS* vfs, _In_ strref ns);

// If vfs has VFS_AutoMountDrives and path is absolute with a single-letter namespace the VFS
// does not have yet, gives the platform a chance to mount it. Call with no VFS lock held.
void _vfsEnsureNamespace(_Inout_ VFS* vfs, _In_opt_ strref path);
