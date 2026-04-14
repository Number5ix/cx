#pragma once

/// @file strliteral.h
/// @brief String literal prefix macros and compile-time constant declarations

#ifdef _WIN32
// _S definition conflicts with Windows SDK headers; pull them in first
#include <mmintrin.h>
#include <wchar.h>
#endif

/// @defgroup string_literals String Literal Macros
/// @ingroup string
/// @{
///
/// Three families of macros for creating CX strings from C string literals.
/// All produce static read-only strings that require no initialization and no
/// cleanup — modification automatically triggers a copy-on-write.
///
/// **Simple prefix macros** (`_S`, `_SU`, `_SO`) — portable, all platforms:
///
/// Prepend a 2-byte CX header to a string literal. The string length is not
/// stored in the header and is computed at runtime via `strlen()` on first use.
/// These are the simplest form and are appropriate for most usage:
/// @code
///   string s1 = _S"Hello";           // ASCII literal
///   string s2 = _SU"Hello 世界";      // UTF-8 literal
///   strDup(&s1, _S"World");          // Can use in any string function
/// @endcode
///
/// **Inline expression macros** (`_SL`, `_SLU`, `_SLO` / `_SLL`, `_SLUL`, `_SLOL`):
///
/// Drop-in replacements for `_S` that embed the length as a compile-time constant,
/// eliminating the runtime `strlen()`. On GCC/Clang these expand to a static struct
/// (STR_LEN8 or STR_LEN16). On MSVC they silently fall back to `_S` behavior
/// (STR_LEN0); correctness is preserved, the length optimization is simply not applied.
/// Prefer these over `_S` on hot paths when targeting GCC/Clang.
///
/// The short forms (`_SL`, `_SLU`, `_SLO`) enforce content < 200 bytes at compile
/// time on GCC/Clang — use the long forms (`_SLL`, `_SLUL`, `_SLOL`) for larger strings:
/// @code
///   strDup(&s, _SL("hello"));                   // STR_LEN8 on GCC/Clang
///   strDup(&s, _SLL("a 200+ byte string..."));  // STR_LEN16 on GCC/Clang
/// @endcode
///
/// **Named constants** (`STR_CONST` / `STR_CONSTL` families) — portable, all platforms:
///
/// Declares a named `strref` backed by a static struct with a compile-time
/// embedded length. Works on all platforms. Use at file scope or as `static` locals
/// when the same string is referenced in multiple places:
/// @code
///   STR_CONST(kError, "Something went wrong");    // ASCII, STR_LEN8, < 255 bytes
///   STR_CONSTU(kGreeting, "Héllo");               // UTF-8, STR_LEN8
///   STR_CONSTL(kTemplate, "... long string ..."); // ASCII, STR_LEN16, < 65535 bytes
/// @endcode
///
/// Header byte quick reference:
/// | Family           | Encoding    | Length class    | hdr byte    |
/// |------------------|-------------|-----------------|-------------|
/// | `_S`  / `_SL`   | ASCII+UTF-8 | STR_LEN0 / LEN8 | 0xE0 / 0xE1 |
/// | `_SU` / `_SLU`  | UTF-8       | STR_LEN0 / LEN8 | 0xA0 / 0xA1 |
/// | `_SO` / `_SLO`  | other       | STR_LEN0 / LEN8 | 0x80 / 0x81 |
/// | `_SLL`           | ASCII+UTF-8 | STR_LEN16       | 0xE2        |
/// | `_SLUL`          | UTF-8       | STR_LEN16       | 0xA2        |
/// | `_SLOL`          | other       | STR_LEN16       | 0x82        |

// ---- Simple prefix macros (_S family) ----

/// @def _S
/// @brief Creates a static ASCII string literal (STR_LEN0, runtime strlen).
/// Prefer _SL() on hot paths when targeting GCC/Clang.
/// @hideinitializer
#define _S (string) "\xE0\xC1"

/// @def _SU
/// @brief Creates a static UTF-8 string literal (STR_LEN0, runtime strlen).
/// Prefer _SLU() on hot paths when targeting GCC/Clang.
/// @hideinitializer
#define _SU (string) "\xA0\xC1"

/// @def _SO
/// @brief Creates a static string literal with other/unknown encoding (STR_LEN0, runtime strlen).
/// Prefer _SLO() on hot paths when targeting GCC/Clang.
/// @hideinitializer
#define _SO (string) "\x80\xC1"

// ---- Named constant declaration helpers ----
// STR_LEN8 layout:  [hdr:u8][0xC1:u8][len:u8][content + NUL]  — data at offset 3
// STR_LEN16 layout: [hdr:u8][0xC1:u8][len:u16][content + NUL] — data at offset 4

#define _STR_CONST8(name, enc, s)                                                          \
    _Static_assert(sizeof(s) - 1 <= 255, "String too long for STR_CONST, use STR_CONSTL"); \
    static const struct {                                                                  \
        uint8 _h, _m, _l;                                                                  \
        char _d[sizeof(s)];                                                                \
    } _cx_sc_##name          = { (enc), 0xC1u, (uint8)(sizeof(s) - 1u), s };               \
    static const strref name = (strref) & _cx_sc_##name

#define _STR_CONST16(name, enc, s)                                            \
    _Static_assert(sizeof(s) - 1 <= 65535, "String too long for STR_CONSTL"); \
    static const struct {                                                     \
        uint8 _h, _m;                                                         \
        uint16 _l;                                                            \
        char _d[sizeof(s)];                                                   \
    } _cx_sc_##name          = { (enc), 0xC1u, (uint16)(sizeof(s) - 1u), s }; \
    static const strref name = (strref) & _cx_sc_##name

// named constant helpers for references
#define _STR_CONSTR8(name, enc, s)                                                           \
    _Static_assert(sizeof(s) - 1 <= 255, "String too long for STR_CONSTR, use STR_CONSTRL"); \
    static const struct {                                                                    \
        uint8 _h, _m, _l;                                                                    \
        char _d[sizeof(s)];                                                                  \
    } _cx_sr_##name = { (enc), 0xC1u, (uint8)(sizeof(s) - 1u), s };

#define _STR_CONSTR16(name, enc, s)                                            \
    _Static_assert(sizeof(s) - 1 <= 65535, "String too long for STR_CONSTRL"); \
    static const struct {                                                      \
        uint8 _h, _m;                                                          \
        uint16 _l;                                                             \
        char _d[sizeof(s)];                                                    \
    } _cx_sr_##name = { (enc), 0xC1u, (uint16)(sizeof(s) - 1u), s };

/// Declare a named ASCII string constant with a compile-time length (STR_LEN8, < 255 bytes).
#define STR_CONST(name, s)   _STR_CONST8(name, 0xE1u, s)
/// Declare a named UTF-8 string constant with a compile-time length (STR_LEN8, < 255 bytes).
#define STR_CONSTU(name, s)  _STR_CONST8(name, 0xA1u, s)
/// Declare a named other-encoding string constant with a compile-time length (STR_LEN8, < 255
/// bytes).
#define STR_CONSTO(name, s)  _STR_CONST8(name, 0x81u, s)
/// Declare a named ASCII string constant with a compile-time length (STR_LEN16, < 65535 bytes).
#define STR_CONSTL(name, s)  _STR_CONST16(name, 0xE2u, s)
/// Declare a named UTF-8 string constant with a compile-time length (STR_LEN16, < 65535 bytes).
#define STR_CONSTUL(name, s) _STR_CONST16(name, 0xA2u, s)
/// Declare a named other-encoding string constant with a compile-time length (STR_LEN16, < 65535
/// bytes).
#define STR_CONSTOL(name, s) _STR_CONST16(name, 0x82u, s)

// Internal version of STR_CONST for static references only (use &name in initializer)
#define STR_CONSTR(name, s)   _STR_CONSTR8(name, 0xE1u, s)
#define STR_CONSTRU(name, s)  _STR_CONSTR8(name, 0xA1u, s)
#define STR_CONSTRO(name, s)  _STR_CONSTR8(name, 0x81u, s)
#define STR_CONSTRL(name, s)  _STR_CONSTR16(name, 0xE2u, s)
#define STR_CONSTRUL(name, s) _STR_CONSTR16(name, 0xA2u, s)
#define STR_CONSTROL(name, s) _STR_CONSTR16(name, 0x82u, s)

// Helper for using STR_CONSTR declared constants in initializers
#define _SR(name) ((strref)&_cx_sr_##name)

// ---- Inline literal macros ----

#if defined(_COMPILER_MSVC)
// MSVC: no statement expressions; silently degrade to STR_LEN0, same as _S / _SU / _SO.
#define _SL(s)   ((strref)("\xE0\xC1" s))
#define _SLU(s)  ((strref)("\xA0\xC1" s))
#define _SLO(s)  ((strref)("\x80\xC1" s))
#define _SLL(s)  ((strref)("\xE0\xC1" s))
#define _SLUL(s) ((strref)("\xA0\xC1" s))
#define _SLOL(s) ((strref)("\x80\xC1" s))

#else
// GCC/Clang: statement expressions with file-scoped statics — no heap, no runtime strlen.

/// @def _SL(s)
/// @brief Inline ASCII string literal with compile-time embedded length (STR_LEN8).
/// Content must be < 200 bytes; use _SLL() for longer strings.
/// @hideinitializer
#define _SL(s)                                                                    \
    __extension__({                                                               \
        _Static_assert(sizeof(s) - 1 < 200, "String too long for _SL, use _SLL"); \
        static const struct {                                                     \
            uint8 _h, _m, _l;                                                     \
            char _d[sizeof(s)];                                                   \
        } _sl = { 0xE1u, 0xC1u, (uint8)(sizeof(s) - 1u), s };                     \
        (strref) & _sl;                                                           \
    })

/// @def _SLU(s)
/// @brief Inline UTF-8 string literal with compile-time embedded length (STR_LEN8).
/// Content must be < 200 bytes; use _SLUL() for longer strings.
/// @hideinitializer
#define _SLU(s)                                                                     \
    __extension__({                                                                 \
        _Static_assert(sizeof(s) - 1 < 200, "String too long for _SLU, use _SLUL"); \
        static const struct {                                                       \
            uint8 _h, _m, _l;                                                       \
            char _d[sizeof(s)];                                                     \
        } _sl = { 0xA1u, 0xC1u, (uint8)(sizeof(s) - 1u), s };                       \
        (strref) & _sl;                                                             \
    })

/// @def _SLO(s)
/// @brief Inline other-encoding string literal with compile-time embedded length (STR_LEN8).
/// Content must be < 200 bytes; use _SLOL() for longer strings.
/// @hideinitializer
#define _SLO(s)                                                                     \
    __extension__({                                                                 \
        _Static_assert(sizeof(s) - 1 < 200, "String too long for _SLO, use _SLOL"); \
        static const struct {                                                       \
            uint8 _h, _m, _l;                                                       \
            char _d[sizeof(s)];                                                     \
        } _sl = { 0x81u, 0xC1u, (uint8)(sizeof(s) - 1u), s };                       \
        (strref) & _sl;                                                             \
    })

/// @def _SLL(s)
/// @brief Inline ASCII string literal with compile-time embedded length (STR_LEN16).
/// For strings 200–65534 bytes; use _SL() for shorter strings.
/// @hideinitializer
#define _SLL(s)                                                \
    __extension__({                                            \
        static const struct {                                  \
            uint8 _h, _m;                                      \
            uint16 _l;                                         \
            char _d[sizeof(s)];                                \
        } _sl = { 0xE2u, 0xC1u, (uint16)(sizeof(s) - 1u), s }; \
        (strref) & _sl;                                        \
    })

/// @def _SLUL(s)
/// @brief Inline UTF-8 string literal with compile-time embedded length (STR_LEN16).
/// @hideinitializer
#define _SLUL(s)                                               \
    __extension__({                                            \
        static const struct {                                  \
            uint8 _h, _m;                                      \
            uint16 _l;                                         \
            char _d[sizeof(s)];                                \
        } _sl = { 0xA2u, 0xC1u, (uint16)(sizeof(s) - 1u), s }; \
        (strref) & _sl;                                        \
    })

/// @def _SLOL(s)
/// @brief Inline other-encoding string literal with compile-time embedded length (STR_LEN16).
/// @hideinitializer
#define _SLOL(s)                                               \
    __extension__({                                            \
        static const struct {                                  \
            uint8 _h, _m;                                      \
            uint16 _l;                                         \
            char _d[sizeof(s)];                                \
        } _sl = { 0x82u, 0xC1u, (uint16)(sizeof(s) - 1u), s }; \
        (strref) & _sl;                                        \
    })

#endif   // _COMPILER_MSVC

/// @}  // end of string_literals group
