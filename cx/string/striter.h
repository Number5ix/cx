#pragma once

#include <cx/string/strbase.h>

CX_C_BEGIN

// String iterators
// The public-facing members represent a run of contiguous bytes within the string.
// A string may be made up of one or more runs, call striNext() to advance to the
// next run, or use the other functions to advance the iterator on a byte or character
// basis.
typedef struct striter {
    // public-facing members
    char *bytes;
    uint32 off;
    uint32 len;
    uint32 cursor;

    // private members
    string _str;
    bool _borrowed;
} striter;

typedef enum { STRI_BYTE, STRI_U8CHAR } STRI_SEEK_TYPE;
typedef enum { STRI_SET, STRI_CUR, STRI_END } STRI_SEEK_WHENCE;

void striInit(striter *i, strref s);
void striInitRev(striter *i, strref s);
bool striNext(striter *i);
bool striPrev(striter *i);
bool striSeek(striter *i, int32 off, STRI_SEEK_TYPE type, STRI_SEEK_WHENCE whence);
void striFinish(striter *i);

// Borrowed iterators do not hold a reference to the string. They are intended for
// carefully controlled circumstances where maximum performance is needed. They
// may be discarded when finished.
void striBorrow(striter *i, strref s);
void striBorrowRev(striter *i, strref s);


// It's preferred to for the caller to use striter->len to scan through bytes, but in
// some circumstances, like UTF-8 decoding, an outside state machine needs to drive the
// iteration and access groups of characters without worrying about where the
// iterator runs start and end. In that case, striChar can be used to retrieve one
// byte at a time from the string, and striAdvance can scan forwards through it.
// This is less efficient as it has to check the cursor position for each character,
// but vastly simplifies the code.

_meta_inline bool striChar(striter *i, char *out)
{
    while (i->cursor >= i->len) {
        striNext(i);
        if (i->len == 0)
            return false;
    }

    *out = i->bytes[i->cursor++];
    return true;
}

_meta_inline bool striPeekChar(striter *i, char *out)
{
    while (i->cursor >= i->len) {
        striNext(i);
        if (i->len == 0)
            return false;
    }

    *out = i->bytes[i->cursor];
    return true;
}

_meta_inline bool striAdvance(striter *i, uint32 by)
{
    i->cursor += by;
    while (i->cursor >= i->len) {
        striNext(i);
        if (i->len == 0)
            return (i->cursor == 0);        // return true for hitting end of string exactly
    }
    return true;
}

// UTF-8 versions of char and advance functions that operate on unicode code points
bool striU8Char(striter *i, int32 *out);
bool striPeekU8Char(striter *i, int32 *out);
bool striAdvanceU8(striter *i, uint32 by);

CX_C_END
