#pragma once

#include <cx/container/sarray.h>
#include <cx/cx.h>
#include <cx/stype/stvar.h>

/// @defgroup string_format Formatting
/// @ingroup string
/// @{
///
/// Type-safe string formatter with variable substitution and rich formatting options.
///
/// Though the function prototype itself is extremely simple, the usage is not. On a
/// basic level, the formatter works similarly to printf, but is type safe and has many
/// convenience features.
///
/// @section string_format_syntax Format syntax
///
/// The format string is copied as-is unless the character sequence `${` is encountered.
/// This sequence triggers a variable replacement, and everything until the next `}` is
/// considered to be part of the variable. If a literal `${` is needed, it can be escaped
/// with a backtick -- i.e. `` `${ ``
///
/// Variable substitution follows the following format, where `N` stands for a number and
/// square brackets mark optional parts (the brackets around `idx` are literal):
///
/// `${type}`
/// `${typeN}`  // e.g. ${type1}
/// `${type:key}`
/// `${type#[idx](width,fmtopts);default}`
/// `${type:key[idx](width,fmtopts);default}`
///
/// or, broken down:
/// `[prefix] type [N] [:key] [ [idx] ] [(width,fmtopts)] [;default]`
///
/// The subscript precedes the formatting options because it binds tighter: it selects
/// *which* value to render, and the options then apply to whatever came out. So
/// `${int[2](6)}` takes element 2 and pads it to a width of 6. The reverse order is
/// rejected rather than silently accepted.
///
/// `type` is a type name (see below), and `N` indicates the n-th instance of that
/// particular type in the format arguments. Both are OPTIONAL, but at least one of
/// `type` or `:key` must be present. If `N` is omitted, an internal count is maintained,
/// and each time that type name is used, the count is increased.
///
/// For example, `${string}` indicates the next string in the argument sequence, and
/// `${int3}` uses the third integer.
////
/// @section string_format_prefixes Optional prefixes
///
/// An optional prefix may precede the type name. Supported prefixes are:
/// - `-` For numeric types, leave an extra space for the sign if positive
/// - `+` For numeric types, always print the sign
/// - `0` For numeric types, add leading zeros to fill out the width
///
/// @section string_format_opts Format options
///
/// `fmtopts` is a comma-delimited list of formatting options, most of which are specific
/// to particular types -- see below for details. A formatting option that is just a
/// number by itself is interpreted to be the field width.
///
/// Most types right-justify within the field width if it is present -- this can be
/// overridden with the formatting options of `left`, or `center`.
///
/// @section string_format_types Supported types
///
/// - **string** - Supported fmtopts:
///   - `upper` - Transforms string to all uppercase
///   - `lower` - Transforms string to all lowercase
///   - `name` - Transforms string to all lowercase, capitalizes first character (ASCII only)
///   - `empty` - Replaces empty strings with the default if set
///   - `null` - Replaces null strings with the default if set
/// - **int** - Supported fmtopts:
///   - `min:#` - Minimum number of digits to output. Different from width in that this will not
///   truncate, and always 0-pads
///   - `hex` - Outputs a Base 16 number (can be modified by upper)
///   - `octal` - Outputs a Base 8 number
///   - `binary` - Outputs a Base 2 number
///   - `prefix` - Adds an 0x prefix for hex, and a 0 prefix for octal
///   - `utfchar` - Outputs a UTF-8 encoded Unicode character. Argument is treated as unsigned, and
///   width is ignored.
/// - **uint** - Unsigned integers are considered a different type and do not support the +/-
/// prefixes. They otherwise support all of the int fmtopts.
/// - **float** - Supported fmtopts:
///   - `sig:#` - Maximum number of significant digits
///   - `fixed` - Fixed mode. Will never use scientific notation, and may produce a very long
///   string.
///   - `dec:#` - Maximum number of digits following the decimal point. Defaults to 6 for fixed
///   mode, 0 for auto.
///   - `zero` - Zero-pads after the decimal point if used with dec:#
///   - If fixed is not specified, automatic mode is the default. If used without sig, this will try
///   to output the shortest possible string that can be converted back to the original floating
///   point number. Not 100% guaranteed to be the shortest form, but is the shortest 99.6% of the
///   time and is extremely fast. Adding sig or dec will round the number instead of being precise.
///   - If a field width is specified, automatically sets sig in order to fit.
/// - **ptr** - By default, the pointer value is output in the form 1234abcd. A 0 prefix causes the
/// pointer to be zero-padded up to the pointer size.
///   - `upper` - Uses uppercase hex digits
///   - `prefix` - Adds a 0x prefix (not affected by upper)
/// - **suid** - Supported fmtopts:
///   - `upper` - Uses uppercase SUID (not recommended)
/// - **object** - Requires that the object in question implement the Formattable interface. fmtopts
/// are passed to the object and may vary.
///
/// @section string_format_keys Keyed arguments
///
/// An argument passed with `stvark()` carries a name, and `:key` selects it by that name
/// instead of by type and position. Keyed lookup searches the whole argument list and is
/// order-independent, so keyed and positional placeholders can be freely interleaved
/// without disturbing each other.
///
/// The two addressing modes are disjoint: a **keyed argument is never matched by an
/// unkeyed placeholder**, and a keyed placeholder never consults unkeyed arguments. This
/// means adding a keyed argument to an existing call cannot renumber the placeholders
/// already there, which is the whole point of naming one.
///
/// - `${string:host}` -- the argument keyed `host`, which must be a string
/// - `${:host}` -- the argument keyed `host`, whatever type it is
/// - `${string:host(10,left)}` -- keys compose with format options as usual
/// - `${int:sizes[2]}` -- element 2 of the array keyed `sizes`
/// - `${string:hdrs[agent]}` -- key `agent` of the hashtable keyed `hdrs`
///
/// Keys compose with container subscripting (below), which is the only way to
/// disambiguate two containers of the same element type in one call. A typeless keyed
/// subscript takes its type from the container's element or value type, since that is
/// what actually gets rendered.
///
/// @code
///   strFormat(&s, _SL("${string:host} took ${int:ms}ms"),
///             stvark(host, string, hostname), stvark(ms, int32, elapsed));
/// @endcode
///
/// The key sits in the type-name position, before any subscript or formatting options, so
/// that the typed and typeless forms parse identically. Omitting the type is a
/// convenience: the argument's own runtime type selects the formatter.
///
/// @section string_format_container Container indexing
///
/// The 'idx' field subscripts a container argument and acts as a modifier on the type.
/// Both forms use square brackets; which one applies is decided from the bracket contents,
/// never from which arguments happen to be present:
/// - `type[#]` - Uses the first matching argument that is an sarray of type. The number specifies
/// the 0-based array index. `[]` without an index may be used to increment an internal counter.
/// - `type[key]` - Uses the first matching argument that is a hashtable with string keys and
/// values of the specified type. The string 'key' is looked up in the hashtable in order to find
/// the matching value.
///
/// Bracket contents that parse as a non-negative integer are always an array index; anything
/// else is always a hashtable key. To force a hashtable key that looks like a number, escape it
/// with a leading backtick -- `` ${string[`0]} `` looks up the key "0".
///
/// @note Hashtable lookup previously used `type:key`. That spelling now means a keyed argument
/// (see above), which is by far the more common case, so hashtable lookups moved into the
/// bracket syntax alongside array indexing.
///
/// Without a key the container is selected the usual way -- the first matching argument of the
/// right shape -- so two same-typed containers in one call cannot be told apart. Adding a key
/// does that: `${int:sizes[0]}` and `${int:limits[0]}` index two different arrays.
///
/// Remember that the subscript precedes `(width,fmtopts)`: `${int[2](6)}`, not `${int(6)[2]}`.
///
/// @section string_format_default Default values
///
/// If there is an error such as not being able to find an argument of the appropriate
/// type, the normal behavior is for strFormat to return false and produce no output.
/// However, if the variable contains a `;default` section, instead of failing the entire
/// format operation, the literal text of 'default' (anything after the semicolon) will
/// be inserted in place of the variable instead. A `}` character may be escaped with `` `} ``
/// in this section.

CX_C bool _strFormat(_Inout_ string* out, _In_ strref fmt, int n, _In_ stvar* args);

/// bool strFormat(string *out, strref fmt, ...)
///
/// Type-safe string formatter with variable substitution and rich formatting options
///
/// Copies the format string as-is, replacing `${...}` sequences with formatted variables.
/// See detailed documentation sections above for complete syntax and formatting options.
///
/// @param out Pointer to string to receive the formatted result
/// @param fmt Format string with ${type} variable sequences
/// @param ... Variable arguments, each wrapped with stvar(type, value)
/// @return true on success, false if a required variable is missing or type mismatch occurs
///
/// Example:
/// @code
///   string s = 0;
///   strFormat(&s, _SL("Hello ${string}, you have ${int} messages"),
///             stvar(string, name), stvar(int32, count));
/// @endcode
#define strFormat(out, fmt, ...) \
    _strFormat(out, fmt, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ })

// strFormat previously used the sizeof((stvariant[]){ __VA_ARGS__ })/sizeof(stvar)
// contruction to count the arguments, but this caused MSVC to allocate double the stack
// space and push duplicate argument lists for no reason! Falling back to the old school
// macro trick instead.

/// @defgroup string_format_formattable Formattable interface
/// @ingroup string_format
/// @{
///
/// Types and structures for implementing the Formattable interface in custom objects.
///
/// Not needed for normal API usage; only for objects that implement Formattable.

/// Format variable flags for Formattable interface implementations
enum FMTVarFlags {
    FMTVar_NoGenCase  = 0x0001,     ///< no generic case conversions
    FMTVar_NoGenWidth = 0x0002,     ///< no generic width handling

    FMTVar_SignPrefix   = 0x0010,   ///< had the - prefix to leave space for sign
    FMTVar_SignAlways   = 0x0020,   ///< had the + prefix to always print sign
    FMTVar_LeadingZeros = 0x0040,   ///< had the 0 prefix to add leading zeros

    FMTVar_Left   = 0x0100,         ///< left format option
    FMTVar_Center = 0x0200,         ///< center format option
    FMTVar_Right  = 0x0400,         ///< right format option
    FMTVar_Upper  = 0x0800,         ///< uppercase option
    FMTVar_Lower  = 0x1000,         ///< lowercase option

    // type-specific flags may use 0x00010000 - 0xffff0000
};

/// Format variable descriptor for Formattable interface implementations
typedef struct FMTVar {
    string var;           ///< (unparsed) variable descriptor
    int32 vtype;          ///< variable type
    int32 idx;            ///< explicit index (1-based)
    int32 width;          ///< field width
    sa_string fmtopts;    ///< variable format options (other than width)
    string def;           ///< default text
    string tmp;           ///< temporary storage for variable-level operations

    int32 arrayidx;       ///< array index (or -1)
    string hashkey;       ///< hashtable lookup key, from ${type[key]}
    string key;           ///< keyed-argument name, from ${type:key}

    uint32 flags;         ///< format flags
    uintptr fmtdata[4];   ///< type-specific format data

    stype type;           ///< original type
    void* data;           ///< data backing up this variable
} FMTVar;

/// @}  // end of string_format_formattable group

/// @}  // end of string_format group
