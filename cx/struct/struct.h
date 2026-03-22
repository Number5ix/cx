#pragma once

#include <cx/stype/stype.h>

enum StructMemberFlagsEnum {
    STRUCT_NoDestroy   = 1 << 0,   ///< Member should not be automatically destroyed
    STRUCT_NoCopy      = 1 << 1,   ///< Member should be skipped during copy operations
    STRUCT_NoSerialize = 1 << 2,   ///< Member should not be serialized (e.g. by JSON, etc.)

    /// Member should be ignored for all operations
    /// Ignored members may also be omitted from the list entirely, e.g. for types outside the stype
    /// system.
    STRUCT_Ignore = STRUCT_NoDestroy | STRUCT_NoSerialize | STRUCT_NoCopy,
};

typedef struct StructMemberDesc {
    strref name;     ///< Name of the member
    size_t offset;   ///< Offset within the struct
    stype type;      ///< Type of the member
    stgeneric def;   ///< Default value for the member (if any)
    uint32 flags;    ///< Additional type-dependent flags for the member
} StructMemberDesc;

typedef struct StructInfo {
    strref name;                 ///< Name of the struct
    size_t structsize;           ///< Size in bytes of the struct
    int nmembers;                ///< Number of struct members
    StructMemberDesc* members;   ///< Array of struct member descriptors

    /// Optional custom initializer
    ///
    /// Structs are always zero-filled when allocated. If set, this function is called when a
    /// struct  is initialized, and can be used to set up non-zero default values, etc.
    void (*init)(void* _struct);

    /// Optional custom destructor
    ///
    /// Called when the struct is destroyed. This is called before the automatic
    /// clean-up of struct members, so strings, etc. will still be valid.
    void (*destroy)(void* _struct);
} StructInfo;

typedef struct StructBase {
    union {
        StructInfo* structinfo;   ///< Pointer to struct metadata
        void* _is_struct;         ///< Type marker for compile-time validation
    };

    // Struct-specific data members follow
} StructBase;

#define structInfoName(sname) sname##_structinfo

#define STRUCTBASE(s)    (unused_noeval(&((s)->_is_struct)), (StructBase*)(s))
#define STRUCTHANDLE(sp) (unused_noeval(&((*s)->_is_struct)), (StructBase**)(s))

void _structInitMany(StructBase* base, StructInfo* info, int number);

/// void structInitMany(structname, struct* s, int n)
#define structInitMany(structname, s, n) \
    _structInitMany(STRUCTBASE(s), &structInfoName(structname), n)

/// void structInit(structname, struct* s)
#define structInit(structname, s) structInitMany(STRUCTBASE(s), &structInfoName(structname), 1)

StructBase* _structAlloc(StructInfo* info);

/// struct* structAlloc(structname)
#define structAlloc(structname) ((structname*)_structAlloc(&structInfoName(structname)))

void _structDestroyMembersMany(StructBase* base, int number);

/// void structDestroyMembersMany(struct* s, int n)
#define structDestroyMembersMany(s, n) _structDestroyMembersMany(STRUCTBASE(s), n)

/// void structDestroyMembers(struct* s)
#define structDestroyMembers(s) _structDestroyMembersMany(STRUCTBASE(s), 1)

void _structDestroy(StructBase** pbase);

/// void structDestroy(struct** ps)
#define structDestroy(ps) _structDestroy(STRUCTHANDLE(ps))
