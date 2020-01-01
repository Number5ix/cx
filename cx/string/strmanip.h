#pragma once

#include <cx/string/strbase.h>
#include <cx/utils/macros.h>

_EXTERN_C_BEGIN

// String manipulation
// Argument naming convention:
//   o   = output string, existing content ignored and destroyed
//   io  = input+output string, used inplace if possible
//   s*  = input string, not modified
//   sc* = input string, consumed and handle set to NULLs

// Appends string s to string io (in-place)
bool strAppend(string *io, string s);
// Prepends string s to string io (in-place)
bool strPrepend(string s, string *io);


// Sets string o to string s1 concatenated to string s2
bool strConcat(string *o, string s1, string s2);
// Sets string o to string sc1 concatenated to string sc2
// sc1 and sc2 are destroyed in the process (but efficiently reused if possible)
bool strConcatC(string *o, string *sc1, string *sc2);
// Sets string o to n strings concatenated together
#define strNConcat(o, ...) _strNConcat(o, count_macro_args(__VA_ARGS__), (string[]){__VA_ARGS__})
bool _strNConcat(string *o, int n, string *stra);
// Sets string o to n strings concatenated together
// All input strings are destroyed (but efficiently reused if possible)
#define strNConcatC(o, ...) _strNConcatC(o, count_macro_args(__VA_ARGS__), (string*[]){__VA_ARGS__})
bool _strNConcatC(string *o, int n, string **stra);

// Sets string o to a substring of string s between b and e
bool strSubStr(string *o, string s, int32 b, int32 e);
// Sets string o to a substring of string sc between b and e
// sc is destroyed in the process (but efficiently reused if possible)
bool strSubStrC(string *o, string *sc, int32 b, int32 e);
// Efficiently replaces string io in-place with a substring of itself between b and e.
bool strSubStrI(string *io, int32 b, int32 e);

// Converts string to uppercase (ASCII ONLY!)
void strUpper(string *io);
// Converts string to lowercase (ASCII ONLY!)
void strLower(string *io);

// Splits s into pieces separated by sep.
//    out: handle to sarray of strings to place results into
//  empty: if true, empty segments will be preserved
// return: number of splits
int32 strSplit(string **out, string s, string sep, bool empty);

// Joins an array of strings into a single string separated by sep.
bool strJoin(string *out, string *arr, string sep);

// get a single byte
char strGetChar(string str, int32 i);
// set a single byte
void strSetChar(string *str, int32 i, char ch);

_EXTERN_C_END
