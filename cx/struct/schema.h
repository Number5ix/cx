#pragma once

#include <cx/stype/stype.h>

typedef struct StructTypeDetail StructTypeDetail;

/// @defgroup struct_builtin_schema Built-in Schema Descriptors
/// @ingroup struct
/// @{

/// One shared schema descriptor per built-in type.
///
/// Use the `TYPE_schema` macro rather than the underlying `_strtd_*` symbol, e.g.:
/// @code
///   serWrite(w, int32_schema, stType(int32), stArg(int32, 42));
/// @endcode
///
/// `sarray_schema` and `hashtable_schema` describe a plain, untyped container. A struct member
/// declared with specific element types (e.g. `sarray[int32]`) gets its own schema instead.
extern const StructTypeDetail _strtd_none;
extern const StructTypeDetail _strtd_bool;        ///< @copydoc _strtd_none
extern const StructTypeDetail _strtd_int8;        ///< @copydoc _strtd_none
extern const StructTypeDetail _strtd_int16;       ///< @copydoc _strtd_none
extern const StructTypeDetail _strtd_int32;       ///< @copydoc _strtd_none
extern const StructTypeDetail _strtd_int64;       ///< @copydoc _strtd_none
extern const StructTypeDetail _strtd_uint8;       ///< @copydoc _strtd_none
extern const StructTypeDetail _strtd_uint16;      ///< @copydoc _strtd_none
extern const StructTypeDetail _strtd_uint32;      ///< @copydoc _strtd_none
extern const StructTypeDetail _strtd_uint64;      ///< @copydoc _strtd_none
extern const StructTypeDetail _strtd_float32;     ///< @copydoc _strtd_none
extern const StructTypeDetail _strtd_float64;     ///< @copydoc _strtd_none
extern const StructTypeDetail _strtd_string;      ///< @copydoc _strtd_none
extern const StructTypeDetail _strtd_suid;        ///< @copydoc _strtd_none
extern const StructTypeDetail _strtd_object;      ///< @copydoc _strtd_none
extern const StructTypeDetail _strtd_buffer;      ///< @copydoc _strtd_none
extern const StructTypeDetail _strtd_stvar;       ///< @copydoc _strtd_none
extern const StructTypeDetail _strtd_sarray;      ///< @copydoc _strtd_none
extern const StructTypeDetail _strtd_hashtable;   ///< @copydoc _strtd_none

#define none_schema      (&_strtd_none)
#define bool_schema      (&_strtd_bool)
#define int8_schema      (&_strtd_int8)
#define int16_schema     (&_strtd_int16)
#define int32_schema     (&_strtd_int32)
#define int64_schema     (&_strtd_int64)
#define uint8_schema     (&_strtd_uint8)
#define uint16_schema    (&_strtd_uint16)
#define uint32_schema    (&_strtd_uint32)
#define uint64_schema    (&_strtd_uint64)
#define float32_schema   (&_strtd_float32)
#define float64_schema   (&_strtd_float64)
#define string_schema    (&_strtd_string)
#define suid_schema      (&_strtd_suid)
#define object_schema    (&_strtd_object)
#define buffer_schema    (&_strtd_buffer)
#define stvar_schema     (&_strtd_stvar)
#define sarray_schema    (&_strtd_sarray)
#define hashtable_schema (&_strtd_hashtable)

/// @}
