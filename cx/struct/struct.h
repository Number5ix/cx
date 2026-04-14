#pragma once

#include <cx/container/hashtable.h>
#include <cx/container/sarray.h>
#include <cx/container/stype_hashtable.h>
#include <cx/container/stype_sarray.h>
#include <cx/stype/stype.h>
#include "stype_struct.h"

enum StructMemberFlagsEnum {
    STRUCT_NoDestroy   = 1 << 0,   ///< Member should not be automatically destroyed
    STRUCT_NoCopy      = 1 << 1,   ///< Member should be skipped during copy operations
    STRUCT_NoSerialize = 1 << 2,   ///< Member should not be serialized (e.g. by JSON, etc.)

    /// Member should be ignored for all operations
    /// Ignored members may also be omitted from the list entirely, e.g. for types outside the stype
    /// system.
    STRUCT_Ignore = STRUCT_NoDestroy | STRUCT_NoSerialize | STRUCT_NoCopy,
};

typedef struct StructSet StructSet;

typedef struct StructMemberDesc {
    strref name;     // Name of the member
    size_t offset;   // Offset within the struct
    stype type;      // Type of the member
    uint32 flags;    // Member flags (StructMemberFlagsEnum)
    uint32 cflags;   // Creation flags (e.g. for arrays, hashtables, etc.)
} StructMemberDesc;

typedef struct StructInfo {
    strref name;                       // Name of the struct
    size_t structsize;                 // Size in bytes of the struct
    int nmembers;                      // Number of struct members
    const StructMemberDesc* members;   // Array of struct member descriptors
    const void* defaults;              // Default struct to copy (if any)

    // Optional custom initializer
    //
    // Structs are always zero-filled when allocated. If set, this function is called when a
    // struct  is initialized, and can be used to set up non-zero default values, etc.
    void (*init)(void* _struct);

    // Optional custom destructor
    //
    // Called when the struct is destroyed. This is called before the automatic
    // clean-up of struct members, so strings, etc. will still be valid.
    void (*destroy)(void* _struct);
} StructInfo;

// Set of structs for serialization of dynamic structs
typedef struct StructSet {
    int nentries;
    StructInfo* entries[];   // sorted by name for binary search
} StructSet;

typedef struct StructBase {
    union {
        const StructInfo* structinfo;   ///< Pointer to struct metadata
        void* _is_struct;               ///< Type marker for compile-time validation
    };

    // Struct-specific data members follow
} StructBase;

#define structInfoName(sname)     sname##_structinfo
#define structDefaultsName(sname) sname##_structdefaults

#define STRUCTBASE(s)    (unused_noeval(&((s)->_is_struct)), (StructBase*)(s))
#define STRUCTHANDLE(sp) (unused_noeval(&((*s)->_is_struct)), (StructBase**)(s))

void _structInitMany(_Out_ StructBase* base, _In_ StructInfo* info, int number);

/// void structInitMany(structname, struct* s, int n);
///
/// Initializes n consecutive struct instances of the given type.
///
/// Zero-fills each struct and then calls the type's custom init function (if any)
/// to set up non-zero default values. The struct memory must already be allocated —
/// this function only initializes its contents.
///
/// @param structname Name of the struct type (without the struct keyword)
/// @param s Pointer to the first struct instance to initialize
/// @param n Number of consecutive struct instances to initialize
///
/// Example:
/// @code
///   MyStruct arr[4];
///   structInitMany(MyStruct, arr, 4);
/// @endcode
#define structInitMany(structname, s, n) \
    _structInitMany(STRUCTBASE(s), &structInfoName(structname), n)

/// void structInit(structname, struct* s);
///
/// Initializes a single struct instance of the given type.
///
/// Zero-fills the struct and then calls the type's custom init function (if any)
/// to set up non-zero default values. The struct memory must already be allocated —
/// this function only initializes its contents.
///
/// @param structname Name of the struct type (without the struct keyword)
/// @param s Pointer to the struct instance to initialize
///
/// Example:
/// @code
///   MyStruct s;
///   structInit(MyStruct, &s);
/// @endcode
#define structInit(structname, s) structInitMany(STRUCTBASE(s), &structInfoName(structname), 1)

_Ret_notnull_ StructBase* _structAlloc(_In_ StructInfo* info);

/// struct* structCreate(structname);
///
/// Allocates and initializes a single struct instance of the given type on the heap.
///
/// Allocates memory for the struct, zero-fills it, and calls the type's custom init
/// function (if any) to set up non-zero default values. The returned pointer must
/// eventually be freed with structDestroy().
///
/// @param structname Name of the struct type (without the struct keyword)
/// @return Pointer to the newly allocated and initialized struct instance
///
/// Example:
/// @code
///   MyStruct *s = structCreate(MyStruct);
///   // ... use s ...
///   structDestroy(&s);
/// @endcode
#define structCreate(structname) ((structname*)_structAlloc(&structInfoName(structname)))

void _structDestroyMembersMany(_Pre_notnull_ _Post_invalid_ StructBase* base, int number);

/// void structDestroyMembersMany(struct* s, int n);
///
/// Destroys the members of n consecutive struct instances without freeing the structs.
///
/// Calls the custom destructor (if any) and then releases all managed members
/// (strings, containers, objects, etc.) of each struct. The struct memory itself
/// is not freed — use this for stack-allocated or embedded structs.
///
/// @param s Pointer to the first struct instance whose members should be destroyed
/// @param n Number of consecutive struct instances to process
///
/// Example:
/// @code
///   MyStruct arr[4];
///   structInitMany(MyStruct, &arr[0], 4);
///   // ... use arr ...
///   structDestroyMembersMany(&arr[0], 4);
/// @endcode
#define structDestroyMembersMany(s, n) _structDestroyMembersMany(STRUCTBASE(s), n)

/// void structDestroyMembers(struct* s);
///
/// Destroys the members of a single struct instance without freeing the struct.
///
/// Calls the custom destructor (if any) and then releases all managed members
/// (strings, containers, objects, etc.). The struct memory itself is not freed —
/// use this for stack-allocated or embedded structs. For heap-allocated structs,
/// use structDestroy() instead.
///
/// @param s Pointer to the struct instance whose members should be destroyed
///
/// Example:
/// @code
///   MyStruct s;
///   structInit(MyStruct, &s);
///   // ... use s ...
///   structDestroyMembers(&s);
/// @endcode
#define structDestroyMembers(s) _structDestroyMembersMany(STRUCTBASE(s), 1)

_At_(*pbase, _Pre_maybenull_ _Post_null_) void _structDestroy(StructBase** pbase);

/// void structDestroy(struct** ps);
///
/// Destroys and frees a heap-allocated struct instance.
///
/// Calls the custom destructor (if any), releases all managed members
/// (strings, containers, objects, etc.), frees the heap memory, and sets
/// the pointer to NULL. The struct must have been allocated with structCreate().
///
/// @param ps Pointer to the struct pointer to destroy; set to NULL on return
///
/// Example:
/// @code
///   MyStruct *s = structCreate(MyStruct);
///   // ... use s ...
///   structDestroy(&s);   // s is NULL after this
/// @endcode
#define structDestroy(ps) _structDestroy(STRUCTHANDLE(ps))
