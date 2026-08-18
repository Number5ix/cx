/// @file serwriter.h
/// @brief The write half of the serialization backend interface

#pragma once

#include <cx/container/hashtable.h>
#include <cx/serialize/sertype.h>

CX_C_BEGIN

/// @defgroup serialize_writer Writers
/// @ingroup serialize
/// @{

typedef struct SerWriter SerWriter;

/// A serialization format, defined as a set of operations for writing each kind of value.
///
/// Implement this once to add a new backend (JSON, binary, etc.). Every operation returns
/// false on failure and leaves a description in `w->err`.
typedef struct SerWriterOps {
    bool (*writeNull)(_Inout_ SerWriter* w);
    bool (*writeBool)(_Inout_ SerWriter* w, bool v);
    bool (*writeInt)(_Inout_ SerWriter* w, int64 v, stype declared);
    bool (*writeUint)(_Inout_ SerWriter* w, uint64 v, stype declared);
    bool (*writeReal)(_Inout_ SerWriter* w, float64 v, stype declared);
    bool (*writeStr)(_Inout_ SerWriter* w, _In_opt_ strref v);
    bool (*writeBytes)(_Inout_ SerWriter* w, _In_reads_bytes_(n) const void* p, size_t n);

    /// @param count number of elements, or < 0 if not known in advance
    bool (*arrBegin)(_Inout_ SerWriter* w, int32 count);
    bool (*arrEnd)(_Inout_ SerWriter* w);
    /// @param count number of entries, or < 0 if not known in advance
    bool (*mapBegin)(_Inout_ SerWriter* w, int32 count);
    bool (*mapKey)(_Inout_ SerWriter* w, _In_opt_ strref key);
    /// Only reached when the backend advertises SER_Cap_NonStringKeys; otherwise the traverser
    /// projects the map to an array of key/value pairs instead.
    bool (*mapKeyTyped)(_Inout_ SerWriter* w, _In_ const STypeInfoExt* kt, stgeneric key);
    bool (*mapEnd)(_Inout_ SerWriter* w);

    bool (*typeTag)(_Inout_ SerWriter* w, _In_ const STypeInfoExt* st);

    /// Marks the value that follows as the definition of `id`.
    ///
    /// Only reached when the backend advertises SER_Cap_Refs and the document enabled
    /// SER_Refs. Ids are assigned by the traverser, sequentially from zero in the order the
    /// definitions are emitted; a backend encodes the number it is given and does not invent
    /// its own.
    bool (*refDef)(_Inout_ SerWriter* w, uint32 id);
    /// Stands in for the whole value, which was already written under `id`.
    bool (*refUse)(_Inout_ SerWriter* w, uint32 id);

    bool (*finish)(_Inout_ SerWriter* w);   ///< last chance to fail; optional
    void (*destroy)(_Inout_ _Post_invalid_ SerWriter* w);   ///< frees private state and the struct
} SerWriterOps;

/// A handle for an in-progress write, returned by a backend's create function (e.g.
/// `serJsonWriterCreate`). Opaque to callers -- pass it to `serWrite()` and the other
/// functions in this header without needing to know which backend produced it.
typedef struct SerWriter {
    const SerWriterOps* ops;
    flags_t caps;        ///< SerCapabilitiesEnum -- what this backend can represent
    flags_t flags;       ///< SerFlagsEnum -- per-document options
    bool finished;        // serWriterFinish has run
    SerError err;
    SerTraverseState traverse;   // path stack and error list; managed internally

    sa_ptr inprogress;    // objects currently being written; cycle guard, unused with SER_Refs

    hashtable refids;     // object -> assigned reference id, once SER_Refs is in use
    uint32 nextrefid;     // next reference id to hand out
    // backend private data follows
} SerWriter;

/// Allocates a writer of the given total size and fills in the generic header.
///
/// Shared by every backend's create function, which supplies `sizeof` its own private struct
/// and then only has to write its own fields.
///
/// @param size  Total size of the backend's private struct, whose first member must be a SerWriter
/// @param ops   The backend's vtable
/// @param caps  Capabilities this backend advertises
/// @param flags Per-document options
/// @return A zero-filled writer; never NULL
_Ret_notnull_ SerWriter* _serWriterAlloc(size_t size, _In_ const SerWriterOps* ops, flags_t caps,
                                         flags_t flags);

/// Completes the document.
///
/// Separate from destroy because this is the backend's last chance to fail: `destroy` returns
/// void and cannot report an error, and closing out a document can. A writer destroyed without
/// being finished has produced an incomplete document.
///
/// @param w Writer to finish
/// @return true if the document is complete
bool serWriterFinish(_Inout_ SerWriter* w);

/// Releases the writer and sets the handle to NULL.
///
/// The free is left to the backend's `destroy` rather than done here, so a backend stays free
/// to allocate from somewhere other than xalloc.
///
/// @param w Pointer to the writer handle; set to NULL on return
void serWriterDestroy(_Inout_ SerWriter** w);

/// flags_t serWriterCaps(SerWriter *w)
///
/// The capabilities this writer's backend advertises.
#define serWriterCaps(w) ((w)->caps)

/// bool serWriterCan(SerWriter *w, capname)
///
/// Tests one capability of a writer.
///
/// @param w Writer to query
/// @param capname Capability name without the SER_Cap_ prefix, e.g. `Bytes`
///
/// Example:
/// @code
///   if (serWriterCan(w, Bytes))
///       serWriteBytes(w, data, len);
/// @endcode
#define serWriterCan(w, capname) (((w)->caps & SER_Cap_##capname) != 0)

// Thin wrappers around the vtable, used by the traverser and by custom serialize hooks.
_meta_inline bool serWriteNull(_Inout_ SerWriter* w) { return w->ops->writeNull(w); }
_meta_inline bool serWriteBool(_Inout_ SerWriter* w, bool v) { return w->ops->writeBool(w, v); }
_meta_inline bool serWriteInt(_Inout_ SerWriter* w, int64 v, stype declared)
{
    return w->ops->writeInt(w, v, declared);
}
_meta_inline bool serWriteUint(_Inout_ SerWriter* w, uint64 v, stype declared)
{
    return w->ops->writeUint(w, v, declared);
}
_meta_inline bool serWriteReal(_Inout_ SerWriter* w, float64 v, stype declared)
{
    return w->ops->writeReal(w, v, declared);
}
_meta_inline bool serWriteStr(_Inout_ SerWriter* w, _In_opt_ strref v)
{
    return w->ops->writeStr(w, v);
}
_meta_inline bool serWriteBytes(_Inout_ SerWriter* w, _In_reads_bytes_(n) const void* p, size_t n)
{
    return w->ops->writeBytes(w, p, n);
}
_meta_inline bool serArrBegin(_Inout_ SerWriter* w, int32 count)
{
    return w->ops->arrBegin(w, count);
}
_meta_inline bool serArrEnd(_Inout_ SerWriter* w) { return w->ops->arrEnd(w); }
_meta_inline bool serMapBegin(_Inout_ SerWriter* w, int32 count)
{
    return w->ops->mapBegin(w, count);
}
_meta_inline bool serMapKey(_Inout_ SerWriter* w, _In_opt_ strref key)
{
    return w->ops->mapKey(w, key);
}
_meta_inline bool serMapEnd(_Inout_ SerWriter* w) { return w->ops->mapEnd(w); }

// Prefer the serWrite() macro. This takes the schema as a descriptor rather than a type name,
// for the cases the macro cannot spell: a generated member descriptor (`memberdesc->schema`), a
// hand-built container descriptor, or a slot whose schema and value type come from different
// names -- a `structp` in a `structset` slot, for instance. The schema is never NULL; a dynamic
// slot uses a leaf descriptor such as `stExt(stvar)`. The value's runtime type is `schema->type`.
bool _serWrite(_Inout_ SerWriter* w, _In_ const STypeInfoExt* schema, stgeneric val);

/// bool serWrite(SerWriter *w, type, value)
///
/// Writes a value to the given backend (JSON, binary, etc.).
///
/// The type name supplies both what the value is at runtime and what it is declared to be on
/// the wire, so nested struct members and array/hashtable element types come along with it.
///
/// @param w Backend to write to
/// @param type Type name of the value
/// @param val The value
/// @return true on success; on failure the error is left in `w->err`
///
/// Example:
/// @code
///   SerWriter *w = serSsdWriterCreate(0);
///   serWrite(w, MyStruct, val);
///   serWriterFinish(w);
/// @endcode
#define serWrite(w, type, val) _serWrite(w, stExt(type), stArg(type, val))

/// Records an error against a writer and aborts the traversal.
///
/// Materializes the current path from the traverser's frame stack, which is why this is the only
/// supported way to fail out of a custom-type write hook.
///
/// @param w    Writer to fail
/// @param code SerErrorCodeEnum
/// @param msg  Human-readable description; copied
/// @return always false, so a failing op can `return serWriterFail(...)`
bool serWriterFail(_Inout_ SerWriter* w, int32 code, _In_opt_ strref msg);

/// @}

CX_C_END
