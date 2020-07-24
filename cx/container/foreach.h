#pragma once

#include <cx/utils/macros.h>

// collection of macros to iterate through various containers and containerlike objects

// foreach takes a type specifier (mostly stype containers, though there are a few extra),
// a name for an iterator variable, and a set of initializaiton parameters that vary depending
// on the type.
#define foreach(type, itervar, ...) foreach_##type(type, itervar, __VA_ARGS__)

#define foreach_generic(type, itervar, ...) goto tokconcat(_start, __LINE__); \
    for(;;) { \
        ForEachIterType_##type itervar; \
        ForEachFinish_##type(itervar); \
        break; \
        tokconcat(_start, __LINE__): \
        for(ForEachInit_##type(itervar, __VA_ARGS__); ForEachValid_##type(itervar); ForEachNext_##type(itervar))

#define endforeach }

#define foreach_sarray(...) _foreach_array_msvc_workaround((__VA_ARGS__))
#define _foreach_array_msvc_workaround(args) _foreach_sarray args
#define _foreach_sarray(type, itervar, elemtype, elemvar, arrptr) if (1) { \
        int32 itervar, _##itervar##_max = saSize(arrptr); \
        elemtype elemvar; \
        for (itervar = 0; elemvar = (*arrptr)[itervar], itervar < _##itervar##_max; ++itervar)

#define foreach_hashtable foreach_generic
#define ForEachIterType_hashtable htiter
#define ForEachInit_hashtable(itervar, htptr) htiInit(&itervar, htptr)
#define ForEachValid_hashtable(itervar) htiValid(&itervar)
#define ForEachNext_hashtable(itervar) htiNext(&itervar)
#define ForEachFinish_hashtable(itervar) htiFinish(&itervar)

#define foreach_string foreach_generic
#define ForEachIterType_string striter
#define ForEachInit_string(itervar, str) striInit(&itervar, str)
#define ForEachValid_string(itervar) striValid(&itervar)
#define ForEachNext_string(itervar) striNext(&itervar)
#define ForEachFinish_string(itervar) striFinish(&itervar)

#define foreach_vfssearch foreach_generic
#define ForEachIterType_vfssearch FSSearchIter
#define ForEachInit_vfssearch(itervar, ...) vfsSearchInit(&itervar, __VA_ARGS__)
#define ForEachValid_vfssearch(itervar) vfsSearchValid(&itervar)
#define ForEachNext_vfssearch(itervar) vfsSearchNext(&itervar)
#define ForEachFinish_vfssearch(itervar) vfsSearchFinish(&itervar)
