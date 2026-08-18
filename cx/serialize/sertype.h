/// @file sertype.h
/// @brief The abstract data model, type resolution, and serialization errors
///
/// The schema descriptor itself (`STypeInfoExt`) lives in `cx/stype/stype.h` -- it is stype's
/// type, not serialization's; this header only consumes it.

#pragma once

#include <cx/container/sarray.h>
#include <cx/string.h>
#include <cx/struct/struct.h>
#include <cx/stype/stype.h>

CX_C_BEGIN

/// @defgroup serialize_model Data Model and Schema
/// @ingroup serialize
/// @{

typedef struct ObjClassInfo ObjClassInfo;

/// A node in the abstract data model.
///
/// Every cx value decomposes into a sequence of these, and is reassembled from the same
/// sequence. The vocabulary is deliberately close to CBOR's: rich enough to be lossless in
/// a binary format, small enough that the JSON projection is a short table.
///
/// A backend is free to ignore nodes it has no use for -- a schema-locked binary stream
/// ignores `SER_TypeTag` -- and to reject ones it cannot represent, which it declares by
/// not advertising the matching capability.
typedef enum SerNodeKindEnum {
    SER_Invalid = 0,   ///< not a node; returned by peek when the reader is in an error state
    SER_EOF,           ///< reader is positioned past the end of the document

    SER_Null,          ///< no value
    SER_Bool,          ///< bool
    SER_Int,           ///< int64 plus the declared stype
    SER_Uint,          ///< uint64 plus the declared stype
    SER_Real,          ///< float64 plus the declared stype
    SER_Str,           ///< UTF-8 string
    SER_Bytes,         ///< opaque byte run

    SER_ArrayBegin,    ///< start of an ordered sequence
    SER_ArrayEnd,
    SER_MapBegin,      ///< start of a keyed collection
    SER_MapKey,        ///< a key; a string, or a typed value where supported
    SER_MapEnd,

    SER_TypeTag,   ///< a type annotation preceding a value
    SER_RefDef,    ///< defines an id for the value that follows
    SER_RefUse,    ///< stands in for a value already written under an id
} SerNodeKind;

/// The result of resolving a wire type name.
///
/// A resolver returns more than an `stype` because for a nominal type the `stype` alone is
/// useless for *constructing* an instance -- every class resolves to `_sti_object` -- so the
/// reader also needs the `StructInfo` or `ObjClassInfo` that describes how to allocate one.
typedef struct SerResolved {
    stype type;                     ///< runtime descriptor
    const StructInfo* structinfo;   ///< struct / structp, else NULL
    ObjClassInfo* clsinfo;          ///< object class, else NULL
} SerResolved;

/// Resolves a wire type name to something the reader can construct.
///
/// @param out  Receives the resolved type on success
/// @param name Wire type name, as it appeared in a type tag
/// @param user Opaque value supplied when the resolver was registered
/// @return true if this resolver recognized the name
typedef bool (*SerTypeResolver)(_Out_ SerResolved* out, _In_opt_ strref name,
                                _Inout_opt_ void* user);

typedef struct SerResolverEnt {
    SerTypeResolver fn;
    void* user;
} SerResolverEnt;
saDeclare(SerResolverEnt);

// The wire-name table for cx's own structural types, defined in sertype.c. It names types on
// the way out and resolves them on the way back in, so the two directions cannot disagree.
// Reached through serReaderResolve(), which consults it after the reader's own resolvers.
_Ret_maybenull_ strref _serBuiltinName(uint32 stypeid);
bool _serResolveBuiltin(_Out_ SerResolved* out, _In_opt_ strref name);

/// Resolves a struct name through a `StructSet`.
///
/// Register with `serReaderAddResolver(r, serStructSetResolver, &MySet_structset)` to let a
/// reader construct any struct in the set from its wire name. cxautogen emits one `StructSet`
/// per `structset` declaration, sorted for binary search.
///
/// A member declared `structp[MySet]` does **not** need this: the set travels with the slot,
/// and the traverser resolves through it directly. Register it for the cases a declaration
/// cannot cover -- a dynamic value at the top level of a document, or a bare `structp` slot.
///
/// @param out  Receives the resolved struct type
/// @param name Wire type name
/// @param user The `StructSet` to search
/// @return true if the set contains a struct with that name
bool serStructSetResolver(_Out_ SerResolved* out, _In_opt_ strref name, _Inout_opt_ void* user);

/// Resolves a class name through a `ClassSet`.
///
/// The class counterpart of `serStructSetResolver`, over the set cxautogen emits for a
/// `classset` declaration. A slot declared `object[SomeSet]` consults its own set without any
/// registration; this is for the other case, where a document's *top-level* value, or a slot
/// declared as bare `object`, has to resolve against the same vocabulary.
///
/// @param out  Receives the resolved class
/// @param name Wire type name
/// @param user The `ClassSet` to search
/// @return true if the set contains a class with that wire name
bool serClassSetResolver(_Out_ SerResolved* out, _In_opt_ strref name, _Inout_opt_ void* user);

/// Resolves a class name through a NULL-terminated array of `ObjClassInfo*`.
///
/// The same thing as `serClassSetResolver` for a list a consumer assembles itself rather than
/// declaring in a `.cxh`: a class list is a handful of `&X_clsinfo` it already has the symbols
/// for. Classes without a wire name never match: not having one is exactly what "this class
/// does not serialize" means.
///
/// @param out  Receives the resolved class
/// @param name Wire type name
/// @param user NULL-terminated array of ObjClassInfo pointers to search
/// @return true if one of the classes carries that wire name
///
/// Example:
/// @code
///   static ObjClassInfo* myclasses[] = { &Document_clsinfo, &Folder_clsinfo, NULL };
///   serReaderAddResolver(r, serObjClassResolver, myclasses);
/// @endcode
bool serObjClassResolver(_Out_ SerResolved* out, _In_opt_ strref name, _Inout_opt_ void* user);

/// @}

/// @defgroup serialize_flags Capabilities, Flags and Errors
/// @ingroup serialize
/// @{

/// What a backend can represent.
///
/// A backend advertises these in `SerWriter::caps` / `SerReader::caps` so the traverser, and
/// any custom type with its own serialize/deserialize hook, can adapt -- for example, writing a
/// byte buffer as raw bytes if `SER_Cap_Bytes` is set, or as a base64 string otherwise.
enum SerCapabilitiesEnum {
    SER_Cap_NonStringKeys = (1 << 0),   ///< maps may be keyed by arbitrary values
    SER_Cap_Bytes         = (1 << 1),   ///< native binary blob node
    SER_Cap_TypeTags      = (1 << 2),   ///< type annotations are preserved
    SER_Cap_Refs          = (1 << 3),   ///< SER_RefDef / SER_RefUse are honored
    SER_Cap_ExactInt      = (1 << 4),   ///< the full int64/uint64 range round-trips exactly
    SER_Cap_Skip          = (1 << 5),   ///< the reader can skip over an unknown value
    SER_Cap_Sizes         = (1 << 6),   ///< container counts are known before their contents
};

/// Per-document options, passed to a backend's create function.
enum SerFlagsEnum {
    // JSON
    SER_JSON_Pretty  = 0x00000001,   ///< 4-space indent
    SER_JSON_Compact = 0x00000002,   ///< single line

    // binary
    SER_Bin_Compact       = 0x00000100,   ///< omit type tags
    SER_Bin_NoStringDedup = 0x00000200,   ///< inline string values

    // general
    SER_EmitDefaults = 0x01000000,   ///< write members equal to their default
    SER_Refs         = 0x02000000,   ///< enable reference dedup where supported
    SER_Strict       = 0x10000000,   ///< unknown fields and lossy cases are errors

    /// Accumulate errors instead of stopping at the first one.
    ///
    /// Not yet fully implemented -- traversal currently still stops at the first error.
    SER_Collect = 0x20000000,
};

/// Error codes reported through SerError::code
enum SerErrorCodeEnum {
    SER_Err_None = 0,
    SER_Err_Backend,       ///< the backend refused or failed to emit/consume a node
    SER_Err_Type,          ///< a type cannot be serialized, or is not what the schema said
    SER_Err_Schema,        ///< the schema is missing or malformed where one is required
    SER_Err_Data,          ///< the data does not match what the schema expects
    SER_Err_Unsupported,   ///< the backend lacks a capability this value needs
    SER_Err_Overflow,      ///< a value does not fit the slot it is being read into
};

typedef struct SerError {
    int32 code;    ///< SerErrorCodeEnum
    string msg;    ///< human-readable description
    string path;   ///< location in the document, e.g. "/config/servers[3]/port"
} SerError;
saDeclare(SerError);

void serErrorDestroy(_Inout_ SerError* err);

/// One frame of the traverser's location within the document.
///
/// The traverser pushes a frame per level as it descends and materializes them into
/// `SerError::path` only when an error is actually raised, so the common path costs a push
/// and a pop and no allocation.
typedef struct SerPathFrame {
    strref name;   ///< member or map key; NULL for an array index frame
    int32 idx;     ///< array index, when name is NULL
} SerPathFrame;
saDeclare(SerPathFrame);

/// Traverser state shared by both directions.
///
/// Embedded in `SerWriter` and `SerReader` alike; the traverser's error and path helpers take a
/// pointer to this rather than to one of the two.
typedef struct SerTraverseState {
    sa_SerPathFrame path;    ///< current location, innermost last
    sa_SerError collected;   ///< SER_Collect only
} SerTraverseState;

void _serTraverseStateDestroy(_Inout_ SerTraverseState* ws);

/// Renders the current path stack as a document path string.
///
/// @param out Receives the path, e.g. "/config/servers[3]/port"; "/" at the document root
/// @param ws  Traverser state to read the path stack from
void serPathString(_Inout_ string* out, _In_ const SerTraverseState* ws);

/// @}

CX_C_END
