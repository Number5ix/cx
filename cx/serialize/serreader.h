/// @file serreader.h
/// @brief The read half of the serialization backend interface

#pragma once

#include <cx/buffer/buffer.h>
#include <cx/container/hashtable.h>
#include <cx/serialize/sertype.h>

CX_C_BEGIN

/// @defgroup serialize_reader Readers
/// @ingroup serialize
/// @{

typedef struct SerReader SerReader;

/// A serialization format, defined as a set of operations for reading each kind of value.
///
/// The traverser asks for whatever should come next according to the schema, and the backend
/// checks and consumes it. `peek` is only needed when the next value's type isn't already
/// known (a dynamic slot, an optional member); `skip` is for fields the schema doesn't
/// recognize.
typedef struct SerReaderOps {
    SerNodeKind (*peek)(_Inout_ SerReader* r);

    bool (*readNull)(_Inout_ SerReader* r);
    bool (*readBool)(_Inout_ SerReader* r, _Out_ bool* out);

    /// `declared` matters because some formats (e.g. compact binary) don't store a value's
    /// width on the wire -- only the schema knows it. The value always comes back in its
    /// widest form; narrowing to the member's actual type happens elsewhere.
    bool (*readInt)(_Inout_ SerReader* r, _Out_ int64* out, stype declared);
    bool (*readUint)(_Inout_ SerReader* r, _Out_ uint64* out, stype declared);   ///< @copydoc readInt
    bool (*readReal)(_Inout_ SerReader* r, _Out_ float64* out, stype declared);  ///< @copydoc readInt
    bool (*readStr)(_Inout_ SerReader* r, _Inout_ string* out);
    bool (*readBytes)(_Inout_ SerReader* r, _Inout_ Buffer* out);

    /// @param count Receives the element count, or < 0 if the format does not carry one
    bool (*arrBegin)(_Inout_ SerReader* r, _Out_ int32* count);
    bool (*arrNext)(_Inout_ SerReader* r);   ///< false once the array is exhausted
    bool (*arrEnd)(_Inout_ SerReader* r);
    /// @param count Receives the entry count, or < 0 if the format does not carry one
    bool (*mapBegin)(_Inout_ SerReader* r, _Out_ int32* count);
    bool (*mapNext)(_Inout_ SerReader* r, _Inout_ string* key);   ///< false at end of map
    bool (*mapEnd)(_Inout_ SerReader* r);

    bool (*readTypeTag)(_Inout_ SerReader* r, _Inout_ string* name);
    bool (*readRef)(_Inout_ SerReader* r, _Out_ uint32* id);
    bool (*skip)(_Inout_ SerReader* r);

    void (*destroy)(_Inout_ _Post_invalid_ SerReader* r);   ///< frees private state and the struct
} SerReaderOps;

typedef struct SerReader {
    const SerReaderOps* ops;
    flags_t caps;     ///< SerCapabilitiesEnum -- what this backend can represent
    flags_t flags;    ///< SerFlagsEnum -- per-document options
    SerError err;
    SerTraverseState traverse;
    sa_SerResolverEnt resolvers;

    /// Objects the document has defined a reference id for, keyed by that id. `uint32` to `ptr`.
    /// Populated only once the document uses SER_Refs; internal to the traverser.
    hashtable refs;
    // backend private data follows
} SerReader;

/// Allocates a reader of the given total size and fills in the generic header.
///
/// @param size  Total size of the backend's private struct, whose first member must be a SerReader
/// @param ops   The backend's vtable
/// @param caps  Capabilities this backend advertises
/// @param flags Per-document options
/// @return A zero-filled reader; never NULL
_Ret_notnull_ SerReader* _serReaderAlloc(size_t size, _In_ const SerReaderOps* ops, flags_t caps,
                                         flags_t flags);

/// Releases the reader and sets the handle to NULL.
///
/// Readers have no finish step; nothing is buffered on the way in.
///
/// @param r Pointer to the reader handle; set to NULL on return
void serReaderDestroy(_Inout_ SerReader** r);

/// Adds a type resolver to this reader.
///
/// There is no global mutable type registry: resolution is explicit and per-reader, so the
/// meaning of a stream never depends on what happened to be linked into the process.
/// Resolvers are consulted in registration order, first match wins, and cx's own built-in
/// resolver for its structural type names runs last.
///
/// @param r    Reader to register with
/// @param fn   Resolver function
/// @param user Opaque value passed back to the resolver
///
/// Example:
/// @code
///   serReaderAddResolver(r, myStructSetResolver, (void*)&MyStructs_structset);
/// @endcode
void serReaderAddResolver(_Inout_ SerReader* r, SerTypeResolver fn, _Inout_opt_ void* user);

/// Resolves a wire type name through this reader's resolvers, then cx's built-in table.
///
/// @param out  Receives the resolved type
/// @param r    Reader whose resolver list to consult
/// @param name Wire type name
/// @return true if the name was recognized
bool serReaderResolve(_Out_ SerResolved* out, _Inout_ SerReader* r, _In_opt_ strref name);

/// flags_t serReaderCaps(SerReader *r)
///
/// The capabilities this reader's backend advertises.
#define serReaderCaps(r) ((r)->caps)

/// bool serReaderCan(SerReader *r, capname)
///
/// Tests one capability of a reader.
///
/// @param r Reader to query
/// @param capname Capability name without the SER_Cap_ prefix, e.g. `Skip`
#define serReaderCan(r, capname) (((r)->caps & SER_Cap_##capname) != 0)

// Node consumers, mirroring the emitters in serwriter.h.
_meta_inline SerNodeKind serPeek(_Inout_ SerReader* r) { return r->ops->peek(r); }
_meta_inline bool serReadNull(_Inout_ SerReader* r) { return r->ops->readNull(r); }
_meta_inline bool serReadBool(_Inout_ SerReader* r, _Out_ bool* out)
{
    return r->ops->readBool(r, out);
}
_meta_inline bool serReadInt(_Inout_ SerReader* r, _Out_ int64* out, stype declared)
{
    return r->ops->readInt(r, out, declared);
}
_meta_inline bool serReadUint(_Inout_ SerReader* r, _Out_ uint64* out, stype declared)
{
    return r->ops->readUint(r, out, declared);
}
_meta_inline bool serReadReal(_Inout_ SerReader* r, _Out_ float64* out, stype declared)
{
    return r->ops->readReal(r, out, declared);
}
_meta_inline bool serReadStr(_Inout_ SerReader* r, _Inout_ string* out)
{
    return r->ops->readStr(r, out);
}
_meta_inline bool serReadBytes(_Inout_ SerReader* r, _Inout_ Buffer* out)
{
    return r->ops->readBytes(r, out);
}
_meta_inline bool serArrBeginR(_Inout_ SerReader* r, _Out_ int32* count)
{
    return r->ops->arrBegin(r, count);
}
_meta_inline bool serArrNext(_Inout_ SerReader* r) { return r->ops->arrNext(r); }
_meta_inline bool serArrEndR(_Inout_ SerReader* r) { return r->ops->arrEnd(r); }
_meta_inline bool serMapBeginR(_Inout_ SerReader* r, _Out_ int32* count)
{
    return r->ops->mapBegin(r, count);
}
_meta_inline bool serMapNext(_Inout_ SerReader* r, _Inout_ string* key)
{
    return r->ops->mapNext(r, key);
}
_meta_inline bool serMapEndR(_Inout_ SerReader* r) { return r->ops->mapEnd(r); }
_meta_inline bool serSkip(_Inout_ SerReader* r) { return r->ops->skip(r); }

// Prefer the serRead() macro. This takes the schema as a descriptor rather than a type name,
// for the cases the macro cannot spell: a generated member descriptor (`memberdesc->schema`), a
// hand-built container descriptor, or a slot whose schema and value type come from different
// names -- a `structp` in a `structset` slot, for instance. The schema is never NULL; a dynamic
// slot uses a leaf descriptor such as `stExt(stvar)`. The type to produce is `schema->type`.
bool _serRead(_Inout_ SerReader* r, _In_ const STypeInfoExt* schema, _Inout_ stgeneric* val);

/// bool serRead(SerReader *r, type, pvalue)
///
/// Reads a value back from the given backend. The counterpart to serWrite().
///
/// Safe to call whether the destination already holds a value or is freshly zeroed -- any
/// existing value is destroyed before the new one is read in.
///
/// @param r Backend to read from
/// @param type Type name of the destination
/// @param pval Pointer to the destination
/// @return true on success; on failure the error is left in `r->err`
///
/// Example:
/// @code
///   int32 n = 0;
///   serRead(r, int32, &n);
/// @endcode
#define serRead(r, type, pval) _serRead(r, stExt(type), stArgPtr(type, pval))

/// Records an error against a reader and aborts the traversal.
///
/// @param r    Reader to fail
/// @param code SerErrorCodeEnum
/// @param msg  Human-readable description; copied
/// @return always false, so a failing op can `return serReaderFail(...)`
bool serReaderFail(_Inout_ SerReader* r, int32 code, _In_opt_ strref msg);

/// @}

CX_C_END
