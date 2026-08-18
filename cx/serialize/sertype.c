// Wire names for cx's own structural types.
//
// Nominal levels (structs, classes, custom opaques) are named by the schema or by the value
// itself; structural levels are named here, by a table that backs both write and read, so the
// two sides can't disagree.
//
// Kept separate from sertraverse.c: generated code references the canonical `_stie_*`
// descriptors (cx/stype/stype.c), so this file must link everywhere cxautogen output does,
// including cxautogen's own bootstrap build, which doesn't include the traverser.

#include "cx/serialize/sertype.h"

#include "cx/obj/objclass.h"

// ---------------------------------------------------------------------------------------
// Wire names
// ---------------------------------------------------------------------------------------

typedef struct SerBuiltinName {
    uint32 id;
    strref name;
    const STypeInfo* type;
} SerBuiltinName;

STR_CONSTR(sertype_none, "none");
STR_CONSTR(sertype_bool, "bool");
STR_CONSTR(sertype_int8, "int8");
STR_CONSTR(sertype_int16, "int16");
STR_CONSTR(sertype_int32, "int32");
STR_CONSTR(sertype_int64, "int64");
STR_CONSTR(sertype_uint8, "uint8");
STR_CONSTR(sertype_uint16, "uint16");
STR_CONSTR(sertype_uint32, "uint32");
STR_CONSTR(sertype_uint64, "uint64");
STR_CONSTR(sertype_float32, "float32");
STR_CONSTR(sertype_float64, "float64");
STR_CONSTR(sertype_string, "string");
STR_CONSTR(sertype_suid, "suid");
STR_CONSTR(sertype_object, "object");
STR_CONSTR(sertype_buffer, "buffer");
STR_CONSTR(sertype_stvar, "stvar");
STR_CONSTR(sertype_sarray, "sarray");
STR_CONSTR(sertype_hashtable, "hashtable");
STR_CONSTR(sertype_structp, "structp");
STR_CONSTR(sertype_struct, "struct");
STR_CONSTR(sertype_opaque, "opaque");

// Aliases sharing an id with a concrete width (intptr, uintptr, size, strref) are omitted --
// the concrete spelling is what goes on the wire. sarray/hashtable/struct/structp/opaque need
// parameters or an identity beyond a bare name, so they resolve via the type-expression parser
// instead and have no descriptor here.
static const SerBuiltinName _serBuiltinNames[] = {
    { STypeId_none,      _SR(sertype_none),      &_sti_none    },
    { STypeId_bool,      _SR(sertype_bool),      &_sti_bool    },
    { STypeId_int8,      _SR(sertype_int8),      &_sti_int8    },
    { STypeId_int16,     _SR(sertype_int16),     &_sti_int16   },
    { STypeId_int32,     _SR(sertype_int32),     &_sti_int32   },
    { STypeId_int64,     _SR(sertype_int64),     &_sti_int64   },
    { STypeId_uint8,     _SR(sertype_uint8),     &_sti_uint8   },
    { STypeId_uint16,    _SR(sertype_uint16),    &_sti_uint16  },
    { STypeId_uint32,    _SR(sertype_uint32),    &_sti_uint32  },
    { STypeId_uint64,    _SR(sertype_uint64),    &_sti_uint64  },
    { STypeId_float32,   _SR(sertype_float32),   &_sti_float32 },
    { STypeId_float64,   _SR(sertype_float64),   &_sti_float64 },
    { STypeId_string,    _SR(sertype_string),    &_sti_string  },
    { STypeId_suid,      _SR(sertype_suid),      &_sti_suid    },
    { STypeId_object,    _SR(sertype_object),    &_sti_object  },
    { STypeId_buffer,    _SR(sertype_buffer),    &_sti_buffer  },
    { STypeId_stvar,     _SR(sertype_stvar),     &_sti_stvar   },
    { STypeId_sarray,    _SR(sertype_sarray),    NULL          },
    { STypeId_hashtable, _SR(sertype_hashtable), NULL          },
    { STypeId_structp,   _SR(sertype_structp),   NULL          },
    { STypeId_struct,    _SR(sertype_struct),    NULL          },
    { STypeId_opaque,    _SR(sertype_opaque),    NULL          },
};

#define SER_BUILTIN_COUNT (sizeof(_serBuiltinNames) / sizeof(_serBuiltinNames[0]))

_Use_decl_annotations_
strref _serBuiltinName(uint32 stypeid)
{
    for (size_t i = 0; i < SER_BUILTIN_COUNT; i++) {
        if (_serBuiltinNames[i].id == stypeid)
            return _serBuiltinNames[i].name;
    }
    return NULL;
}

_Use_decl_annotations_
bool _serResolveBuiltin(SerResolved* out, strref name)
{
    memset(out, 0, sizeof(SerResolved));
    if (!name)
        return false;

    for (size_t i = 0; i < SER_BUILTIN_COUNT; i++) {
        if (!_serBuiltinNames[i].type)
            continue;
        if (strEq(name, _serBuiltinNames[i].name)) {
            out->type = _serBuiltinNames[i].type;
            return true;
        }
    }
    return false;
}

_Use_decl_annotations_
bool serStructSetResolver(SerResolved* out, strref name, void* user)
{
    memset(out, 0, sizeof(SerResolved));

    const StructInfo* si = user ? structSetFind((const StructSet*)user, name) : NULL;
    if (!si)
        return false;

    out->type       = si->type;
    out->structinfo = si;
    return true;
}

_Use_decl_annotations_
bool serClassSetResolver(SerResolved* out, strref name, void* user)
{
    memset(out, 0, sizeof(SerResolved));

    ObjClassInfo* cls = user ? classSetFind((const ClassSet*)user, name) : NULL;
    if (!cls)
        return false;

    out->type    = &_sti_object;
    out->clsinfo = cls;
    return true;
}

_Use_decl_annotations_
bool serObjClassResolver(SerResolved* out, strref name, void* user)
{
    memset(out, 0, sizeof(SerResolved));

    ObjClassInfo** classes = (ObjClassInfo**)user;
    if (!classes || !name)
        return false;

    for (; *classes; classes++) {
        // A class with no name never opted in to serialization, so it cannot be what a wire
        // name refers to even if the caller listed it.
        if ((*classes)->name && strEq((*classes)->name, name)) {
            out->type    = &_sti_object;
            out->clsinfo = *classes;
            return true;
        }
    }
    return false;
}
