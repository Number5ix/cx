#pragma once

// C++ interop layer for the stype system
//
// This header provides C++-valid replacements for the stype macros that rely on
// C99 compound literals, address-of-temporary, or the scalar-to-union cast used
// by the C paths. It is included at the very end of stype.h, AFTER the C versions
// of these macros have been defined, so every override #undefs the C macro first.
//
// It must NOT be wrapped in extern "C": it declares C++ templates and inline
// helpers, and it is included after stype.h closes its CX_C_END block.

// Pack a typed value into an stgeneric union via a pointer-to-member. Zeroing the
// full 64-bit width first mirrors the C compound-literal semantics (unwritten bytes
// are zero). The C-style (M)val cast reproduces the implicit conversions/narrowing
// the C union-member assignment would perform, without emitting warnings.
template <typename M, typename V>
_meta_inline stgeneric _cxStGen(M stgeneric::*member, V val)
{
    stgeneric g;
    g.st_generic = 0;
    g.*member    = (M)val;
    return g;
}

#undef stgeneric
#undef stgeneric_unchecked
#undef stgensarray
#define stgeneric(type, val)           _cxStGen(&stgeneric::st_##type, stCheck(type, val))
#define stgeneric_unchecked(type, val) _cxStGen(&stgeneric::st_##type, (val))
#define stgensarray(val)               stgeneric(ptr, (val)._is_sarray)

// A zero-initialized generic, replacing the C compound literal ((stgeneric){ 0 }).
_meta_inline stgeneric _cxStGenZero()
{
    stgeneric g;
    g.st_generic = 0;
    return g;
}

// Take the address of an rvalue temporary. The temporary lives until the end of the
// full expression, which is sufficient for the argument-passing use sites (mirrors
// how a C99 compound literal is used inline in a call).
template <typename T>
_meta_inline T* _cxRvalAddr(const T& v)
{
    return const_cast<T*>(&v);
}

// Same idea, specialized for stgeneric, used where the C code took &stgeneric(...).
_meta_inline stgeneric* _cxStGenPtrTemp(const stgeneric& g)
{
    return const_cast<stgeneric*>(&g);
}

// stRvalAddr: create an lvalue (pointer to a temporary) from an rvalue expression.
#undef stRvalAddr
#define stRvalAddr(type, rval) _cxRvalAddr<stStorageType(type)>(rval)

// none ignores the value; replaces the C ((stgeneric){ 0 }) compound literal.
#undef STypeArg_none
#define STypeArg_none(type, val) _cxStGenZero()

// Pointer-argument forms that took the address of a temporary in C.
#undef STypeArgPtr_opaque
#undef STypeArgPtr_suid
#undef STypeArgPtr_stvar
#undef STypeArgPtr_struct
#define STypeArgPtr_opaque(type, val) _cxStGenPtrTemp(stgeneric(type, val))
#define STypeArgPtr_suid(type, val)   _cxStGenPtrTemp(stgeneric_unchecked(type, stCheckPtr(type, val)))
#define STypeArgPtr_stvar(type, val)  _cxStGenPtrTemp(stgeneric_unchecked(type, stCheckPtr(type, val)))
#define STypeArgPtr_struct(type, val) _cxStGenPtrTemp(stgeneric(type, val))

// Temporary STypeInfo builders for opaque(RealType) / struct(RealType) type names.
// The call sites apply a textual '&' to the result, so these must yield an lvalue;
// _cxStiRef returns a const reference to keep the temporary addressable through the
// end of the full expression.
_meta_inline STypeInfo _cxStiOpaque(uint16 sz)
{
    STypeInfo si = {};
    si.id    = stTypeId(opaque);
    si.flags = (uint16)(stFlag(PassPtr) | stFlag(Temporary));
    si.size  = sz;
    si.ops   = _stops_opaque;
    return si;
}

_meta_inline STypeInfo _cxStiStruct(uint16 sz)
{
    STypeInfo si = {};
    si.id    = stTypeId(struct);
    si.flags = (uint16)(stFlag(PassPtr) | stFlag(Object) | stFlag(Temporary));
    si.size  = sz;
    si.ops   = _stops_struct;
    return si;
}

_meta_inline const STypeInfo& _cxStiRef(const STypeInfo& si)
{
    return si;
}

#undef _sti_opaque
#undef _sti_struct
#define _sti_opaque(realtype) _cxStiRef(_cxStiOpaque((uint16)sizeof(realtype)))
#define _sti_struct(realtype) _cxStiRef(_cxStiStruct((uint16)sizeof(realtype)))

// Storage access helpers. The C macros return a bare union member or take the
// address of a temporary; both need C++-valid forms.
_meta_inline void* _cxStGenPtr(stype st, const stgeneric& gen)
{
    return stHasFlag(st, PassPtr) ? gen.st_ptr : (void*)&gen;
}

#undef stGenPtr
#define stGenPtr(st, gen) _cxStGenPtr(st, gen)

#undef stStoredPtr
#define stStoredPtr(st, storage)                                                   \
    (stHasFlag(st, PassPtr) ? _cxStGenPtrTemp(stgeneric(ptr, ((void*)(storage)))) \
                            : (stgeneric*)((void*)(storage)))
