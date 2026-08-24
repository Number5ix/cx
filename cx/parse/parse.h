#pragma once

#include <cx/container/sarray.h>
#include <cx/cx.h>
#include <cx/string.h>
#include <cx/stype/stvar.h>
#include <cx/utils/lazyinit.h>

/// @file parse.h
/// @brief Pattern-based text parsing, the reading counterpart to strFormat

CX_C_BEGIN

/// @defgroup string_parse Parsing
/// @ingroup string
/// @{
///
/// Reads structured text by matching it against a pattern written in the same style as a
/// strFormat template. Where `strFormat` turns values into text, this turns text back
/// into values.
///
/// @code
///   string method = 0, target = 0;
///   uint8 minor   = 0;
///
///   strParse(line, _SL("${string:m} ${string:t} HTTP/1.${uint:v}"),
///            stvpk(m, string, &method),
///            stvpk(t, string, &target),
///            stvpk(v, uint8,  &minor));
/// @endcode
///
/// For anything a pattern cannot express - a grammar with counted repetition, or one that
/// needs a decision made in C partway through - use the @ref string_scan "strscan" cursor
/// instead.
///
/// @section string_parse_syntax Pattern syntax
///
/// A pattern is literal text with three things mixed in:
///
/// - `${...}` a **placeholder**, which matches a value and remembers where it was
/// - `(...)` a **group**, which can be made optional with `?` or offer alternatives with `|`
/// - everything else, which must appear exactly as written
///
/// A backtick escapes the character after it, which is how a pattern matches a literal
/// `$`, `(`, `)`, `|` or backtick: `` `$ ``, `` `( ``, `` `) ``, `` `| ``, ``` `` ```.
///
/// Whitespace in the literal text matches a run of one or more whitespace bytes, so a
/// single space in the pattern also matches a tab or several spaces. STRPAT_ExactWS
/// turns that off and makes whitespace match exactly as written.
///
/// @section string_parse_placeholder Placeholders
///
/// `${type[:key][(parseopts)][;default]}`
///
/// `type` says what kind of text to match, and is one of:
///
/// - **string** - text, delimited by whatever literal comes next in the pattern, matched
///   as short as possible
/// - **int** - an optional sign followed by digits
/// - **uint** - digits, with no sign allowed
/// - **float** - a number in decimal or scientific notation, or `inf` / `nan`
/// - **bool** - `true`/`false`, `yes`/`no` or `1`/`0`, in any case
/// - **object** - text, delimited like `string`, handed to an object that implements the
///   Parsable interface
///
/// The type controls **matching only**. What the value is finally converted into is
/// decided by the destination it is bound to, so `${uint:port}` can fill a `uint16`, an
/// `int64` or a `string` without the pattern changing.
///
/// @section string_parse_binding Binding
///
/// Destinations are passed with `stvp()` and `stvpk()`, and are matched to placeholders
/// two different ways:
///
/// - a **keyed** placeholder `${uint:port}` binds to `stvpk(port, uint16, &port)`, by name
/// - an **unkeyed** placeholder `${uint}` binds to `stvp(uint16, &port)`, by position
///
/// The two modes are disjoint, exactly as in strFormat: a keyed placeholder never takes an
/// unkeyed destination, and adding a keyed destination to a call cannot renumber the
/// positional ones.
///
/// A key named in the pattern with nothing bound to it is fine and is simply not written -
/// which lets one shared pattern serve callers who want different fields out of it. The
/// reverse is an error: a destination whose key does not appear in the pattern fails the
/// match rather than being quietly ignored, because that is a typo, not a choice.
///
/// A placeholder that did not take part in the match leaves its destination **untouched**,
/// so pre-setting it is how a call-site default is written:
///
/// @code
///   uint16 port = 443;                    // stays 443 if the text has no port
///   strPatternMatch(strPat(kUrl), url, stvpk(port, uint16, &port));
/// @endcode
///
/// @section string_parse_groups Optional text and alternatives
///
/// `(...)` groups elements together. A `?` after the closing paren makes the whole group
/// optional, and `|` inside it offers alternatives:
///
/// @code
///   "${string:host}(:${uint:port})?"                                 // port may be absent
///   "(${uint:day} ${string:mon}|${string:mon} ${uint:day}) ${uint:year}"   // either order
/// @endcode
///
/// **A placeholder inside a group must be keyed.** Positional order stops being meaningful
/// once a field might not appear at all, so an unkeyed placeholder inside a group is a
/// compile error.
///
/// A group can carry a key of its own, written after the closing paren and the optional
/// `?`. What the destination receives depends on its type:
///
/// - a `bool` destination gets whether the group matched at all
/// - an integer destination gets which alternative matched, counting from 1, or 0 if the
///   group did not match
///
/// @code
///   "${string:host}(:${uint:port})?:hasport"                          // bool hasport
///   "(${uint:day} ${string:mon}|${string:mon} ${uint:day}):form"      // uint8 form: 1 or 2
/// @endcode
///
/// This is how an absent optional field is told apart from one the caller pre-filled,
/// since a placeholder that did not participate leaves its destination alone.
///
/// A group that has no placeholder, no `|`, no `?` and no key cannot affect anything, so
/// it is rejected at compile time. That is deliberate: it turns a pattern containing a
/// literal `"(none)"` into an error telling the author to write `` "`(none`)" `` instead
/// of quietly matching `none` without the parentheses.
///
/// @section string_parse_default Defaults
///
/// `;default` covers the case where a field's own text is missing but the literals around
/// it are still there:
///
/// @code
///   "a=${uint:x;0},b=${uint:y}"       // matches "a=,b=5" with x = 0
/// @endcode
///
/// If the field does not match, nothing is consumed, the default text is converted into
/// the destination, and matching carries on from the same place - the surrounding literals
/// still have to match. For a `string` placeholder, "does not match" means the text came
/// out empty.
///
/// Groups cover the other case, where the field *and* its surrounding text are missing.
/// The two do not mix: **`;default` inside a group is a compile error.** A group already
/// says the contents are optional, and a placeholder that could quietly succeed on nothing
/// would make a branch always match, which would leave later alternatives unreachable for
/// reasons no reader could see in the pattern.
///
/// The default text is checked against the placeholder's type when the pattern is
/// compiled, so `${uint:port;https}` fails to compile rather than failing on some later
/// call.
///
/// @section string_parse_opts Parse options
///
/// `(parseopts)` is a comma-separated list. Any type accepts:
///
/// - `skip` - match the text but never bind it anywhere
///
/// **int** and **uint** accept:
///
/// - `hex`, `octal`, `binary`, `base:#` - the number's base, decimal by default
/// - `digits:#` - exactly this many digits, no more and no fewer
/// - `maxdigits:#` - at most this many digits
/// - `min:#`, `max:#` - the value must be in range, or the match fails
/// - `ws` - allow whitespace before the number
///
/// **float** accepts:
///
/// - `fixed` - refuse scientific notation
///
/// **string** accepts:
///
/// - `len:#` - exactly this many bytes
/// - `until:<text>` - everything up to the next occurrence of some text
/// - `chars:<set>`, `notchars:<set>` - a run of bytes that are, or are not, in a set
/// - `word` - a run of non-whitespace
/// - `rest` - everything left
/// - `quoted` - a double-quoted string, with `\` escapes removed
/// - `trim` - remove leading and trailing whitespace from the result
/// - `upper`, `lower` - change the case of the result
///
/// Numbers are matched strictly by default: no leading whitespace, and no `0x` prefix
/// unless `hex` was asked for. This is the deliberate opposite of strToInt32()'s default,
/// because a parser that quietly accepts a differently-spelled number is a parser two
/// implementations can disagree about.
///
/// @section string_parse_allornothing All or nothing
///
/// Matching happens in two passes. The first records only *where* each field was found;
/// destinations are written afterwards, once the whole pattern has matched. So a failed
/// match writes nothing at all, mirroring strFormat's "a failed format produces no
/// output", and backtracking never has to un-write a value.
///
/// @section string_parse_threads Reuse and threads
///
/// A compiled pattern is immutable, and matching writes only to the caller's own state, so
/// one pattern can be matched from any number of threads at once. Compile once and keep
/// it - `STR_PATTERN` does that for a file-scope pattern, and strParse() is the throwaway
/// form for a pattern used once.

/// Flags controlling how a pattern is compiled and matched
enum STRPAT_FLAGS {
    STRPAT_ExactWS = 0x01,   ///< Whitespace in the pattern matches exactly, not a run
    STRPAT_CaseI   = 0x02,   ///< Literal text matches without regard to case
};

/// A compiled pattern.
///
/// Immutable once compiled, and safe to match from several threads at once.
typedef struct StrPattern StrPattern;

// Internal - use the strPatternCreate() macro
_Ret_opt_valid_ StrPattern* _strPatternCreate(_In_opt_ strref pat, flags_t flags);

/// StrPattern *strPatternCreate(strref pat, [flags])
///
/// Compiles a pattern.
///
/// @param pat Pattern text
/// @param ... (flags) Optional: STRPAT_FLAGS
/// @return Compiled pattern, or NULL if the pattern is not valid (cxerr is set)
///
/// Example:
/// @code
///   StrPattern *p = strPatternCreate(_SL("${uint:x},${uint:y}"));
///   ...
///   strPatternDestroy(&p);
/// @endcode
#define strPatternCreate(pat, ...) _strPatternCreate(pat, opt_flags(__VA_ARGS__))

/// Releases a compiled pattern.
///
/// @param pat Pointer to the pattern handle, which is set to NULL
void strPatternDestroy(_Inout_ StrPattern* _Nullable* _Nonnull pat);

// Internal - use the strPatternMatch() macro
bool _strPatternMatch(_In_ StrPattern* pat, _In_opt_ strref s, int n, _In_ stvp* dests);

/// bool strPatternMatch(StrPattern *pat, strref s, ...)
///
/// Matches a compiled pattern against a string and fills in the destinations.
///
/// The whole string must match. Nothing is written unless it does.
///
/// @param pat Compiled pattern
/// @param s String to match
/// @param ... One or more destinations, each wrapped with stvp() or stvpk()
/// @return true if the string matched and every value was stored
///
/// Example:
/// @code
///   uint32 x = 0, y = 0;
///   strPatternMatch(p, _SL("10,20"), stvpk(x, uint32, &x), stvpk(y, uint32, &y));
/// @endcode
#define strPatternMatch(pat, s, ...) \
    _strPatternMatch(pat, s, count_macro_args(__VA_ARGS__), (stvp[]) { __VA_ARGS__ })

// Internal - use the strPatternMatchAt() macro
bool _strPatternMatchAt(_Inout_ int32* io_pos, _In_ StrPattern* pat, _In_opt_ strref s, int n,
                        _In_ stvp* dests);

/// bool strPatternMatchAt(int32 *io_pos, StrPattern *pat, strref s, ...)
///
/// Matches a pattern against part of a string, starting where `*io_pos` says.
///
/// Unlike strPatternMatch(), the rest of the string after the pattern is left alone, so
/// this is the form to use when walking through text a piece at a time. On success
/// `*io_pos` moves to just past what matched; on failure it moves to where the match went
/// wrong, which makes it an error position for free.
///
/// @param io_pos Byte offset to start at, updated on return
/// @param pat Compiled pattern
/// @param s String to match
/// @param ... One or more destinations, each wrapped with stvp() or stvpk()
/// @return true if the pattern matched and every value was stored
#define strPatternMatchAt(io_pos, pat, s, ...) \
    _strPatternMatchAt(io_pos, pat, s, count_macro_args(__VA_ARGS__), (stvp[]) { __VA_ARGS__ })

// Internal - use the strParse() macro
bool _strParse(_In_opt_ strref s, _In_opt_ strref pat, int n, _In_ stvp* dests);

/// bool strParse(strref s, strref pat, ...)
///
/// Compiles a pattern, matches it once, and throws it away.
///
/// The convenient form for a one-off parse. Do not put it in a loop - compile the pattern
/// with STR_PATTERN or strPatternCreate() and match that instead.
///
/// @param s String to match
/// @param pat Pattern text
/// @param ... One or more destinations, each wrapped with stvp() or stvpk()
/// @return true if the string matched and every value was stored
///
/// Example:
/// @code
///   uint32 maj = 0, min = 0;
///   strParse(ver, _SL("${uint:maj}.${uint:min}"),
///            stvpk(maj, uint32, &maj), stvpk(min, uint32, &min));
/// @endcode
#define strParse(s, pat, ...) \
    _strParse(s, pat, count_macro_args(__VA_ARGS__), (stvp[]) { __VA_ARGS__ })

// strFormat learned the hard way that sizeof((stvar[]){...})/sizeof(stvar) makes MSVC
// allocate double the stack space and push the argument list twice. Same reason for
// count_macro_args here.

/// File-scope pattern declaration, compiled on first use.
///
/// Declared by STR_PATTERN and read with strPat(); there is no reason to build one by
/// hand.
typedef struct StrPatternDecl {
    strref pat;                    ///< Pattern text
    flags_t flags;                 ///< STRPAT_FLAGS
    LazyInitState state;           ///< Guards the one-time compile
    StrPattern* compiled;          ///< The compiled pattern, once there is one
    struct StrPatternDecl* next;   ///< Links every compiled pattern off a global root
} StrPatternDecl;

// Internal - use the strPat() macro
_Ret_opt_valid_ StrPattern* _strPatGet(_Inout_ StrPatternDecl* decl);

#define _STR_PATTERN_DECL(name, cname, patstr, patflags) \
    STR_CONSTRL(cname, patstr);                          \
    static StrPatternDecl name = { _SR(cname), (patflags) }

/// STR_PATTERN(name, "pattern", [flags])
///
/// Declares a file-scope pattern that compiles itself the first time it is used.
///
/// This is the form for a pattern on a hot path: the compile happens once, on the first
/// match, and every later match reuses it. The compiled pattern is reachable from a global
/// root for the life of the process, so it is not a leak and there is nothing to tear down.
///
/// @param name Name for the declaration, passed to strPat() at the match site
/// @param patstr Pattern text as a string literal
/// @param ... (flags) Optional: STRPAT_FLAGS
///
/// Example:
/// @code
///   STR_PATTERN(kReqLine, "${string:m} ${string:t} HTTP/1.${uint:v}");
///
///   strPatternMatch(strPat(kReqLine), line,
///                   stvpk(m, string, &method),
///                   stvpk(t, string, &target),
///                   stvpk(v, uint8,  &minor));
/// @endcode
#define STR_PATTERN(name, patstr, ...) \
    _STR_PATTERN_DECL(name, _strpat_##name, patstr, opt_flags(__VA_ARGS__))

/// StrPattern *strPat(name)
///
/// Returns the compiled pattern for a STR_PATTERN declaration, compiling it if this is the
/// first use.
///
/// @param name Name given to STR_PATTERN
/// @return Compiled pattern, or NULL if the pattern text is not valid
#define strPat(name) _strPatGet(&name)

/// @defgroup string_parse_parsable Parsable interface
/// @ingroup string_parse
/// @{
///
/// Types for objects that know how to build themselves from text.
///
/// Not needed for normal API usage; only for objects that implement Parsable.

/// Parse variable flags for Parsable interface implementations
enum PARSEVarFlags {
    PARSEVar_CaseInsensitive = 0x0001,   ///< the pattern was compiled case-insensitive
    PARSEVar_Trim            = 0x0002,   ///< the trim option was given
    PARSEVar_Upper           = 0x0004,   ///< the upper option was given
    PARSEVar_Lower           = 0x0008,   ///< the lower option was given
};

/// Parse variable descriptor for Parsable interface implementations
typedef struct ParseVar {
    sa_string opts;   ///< Parse options the pattern gave that the object must interpret
    string def;       ///< Default text, if the placeholder had one
    flags_t flags;    ///< PARSEVarFlags
} ParseVar;

/// @}  // end of string_parse_parsable group

/// @}  // end of string_parse group

CX_C_END
