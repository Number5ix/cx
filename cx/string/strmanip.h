#pragma once

#include <cx/string/strbase.h>
#include <cx/container/sarray.h>

CX_C_BEGIN

enum STRING_SPECIAL {
    strEnd = 0x7fffffff
};

// String manipulation
// Argument naming convention:
//   o   = output string, existing content ignored and destroyed
//   io  = input+output string, used inplace if possible
//   s*  = input string, not modified
//   sc* = input string, consumed and handle set to NULLs

// Appends string s to string io (in-place)
bool strAppend(_Inout_ string *io, _In_opt_ strref s);
// Prepends string s to string io (in-place)
bool strPrepend(_In_opt_ strref s, _Inout_ string *io);


// Sets string o to string s1 concatenated to string s2
bool strConcat(_Inout_ string *o, _In_opt_ strref s1, _In_opt_ strref s2);
// Sets string o to string sc1 concatenated to string sc2
// sc1 and sc2 are destroyed in the process (but efficiently reused if possible)
bool strConcatC(_Inout_ string *o, _Inout_ string *sc1, _Inout_ string *sc2);
// Sets string o to n strings concatenated together
#define strNConcat(o, ...) _strNConcat(o, count_macro_args(__VA_ARGS__), (strref[]){__VA_ARGS__})
bool _strNConcat(_Inout_ string *o, int n, _In_ strref *stra);
// Sets string o to n strings concatenated together
// All input strings are destroyed (but efficiently reused if possible)
#define strNConcatC(o, ...) _strNConcatC(o, count_macro_args(__VA_ARGS__), (string*[]){__VA_ARGS__})
bool _strNConcatC(_Inout_ string *o, int n, _Inout_ string **stra);

// Sets string o to a substring of string s between b and e
bool strSubStr(_Inout_ string *o, _In_opt_ strref s, int32 b, int32 e);
// Sets string o to a substring of string sc between b and e
// sc is destroyed in the process (but efficiently reused if possible)
bool strSubStrC(_Inout_ string *o, _Inout_ string *sc, int32 b, int32 e);
// Efficiently replaces string io in-place with a substring of itself between b and e.
bool strSubStrI(_Inout_ string *io, int32 b, int32 e);

// Converts string to uppercase (ASCII ONLY!)
void strUpper(_Inout_ string *io);
// Converts string to lowercase (ASCII ONLY!)
void strLower(_Inout_ string *io);

// Splits s into pieces separated by sep.
//    out: handle to sarray of strings to place results into
//  empty: if true, empty segments will be preserved
// return: number of splits
int32 strSplit(_Inout_ sa_string *out, _In_opt_ strref s, _In_opt_ strref sep, bool empty);

// Joins an array of strings into a single string separated by sep.
bool strJoin(_Inout_ string *out, _In_ sa_string arr, _In_opt_ strref sep);

// get a single byte
uint8 strGetChar(_In_opt_ strref str, int32 i);
// set a single byte
void strSetChar(_Inout_ string *str, int32 i, uint8 ch);

CX_C_END
