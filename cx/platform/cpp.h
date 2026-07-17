#pragma once

// We don't really like C++, but sometimes it is a necessary evil when
// header files are pulled into unusual places.

#ifdef __cplusplus
#define CX_C extern "C"
#define CX_C_BEGIN \
    extern "C"     \
    {
#define CX_C_END }
#else
#define CX_C
#define CX_C_BEGIN
#define CX_C_END
#endif

#ifdef __cplusplus
// _Static_assert is a C11 keyword; in C++ it maps to the static_assert keyword.
// On MSVC this is already handled in platform/base.h, so only define it elsewhere,
// and guard against it already being provided.
#if !defined(_MSC_VER) && !defined(_Static_assert)
#define _Static_assert static_assert
#endif
// The 'register' storage-class keyword was removed in C++17. Expand it away in C++
// so shared macros (e.g. foreach) that emit it stay valid in both languages.
#define _cx_keyword_register
#else
#define _cx_keyword_register register
#endif
