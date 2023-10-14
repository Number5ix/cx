#include "string_private.h"
#include "strtest.h"
#include "cx/platform/base.h"
#include "cx/utils/scratch.h"
#include "cx/debug/assert.h"

// lookup table for structure positions, lives in a single cache line
// Len, Refcount, String, 0
// 255 indicates invalid combination

alignMem(32) const uint8 _str_off[32] = {
    // STR_LEN0, no refcount
    255, 255, 2, 0,
    // STR_LEN8, no refcount
    2, 255, 3, 0,
    // STR_LEN16, no refcount
    2, 255, 4, 0,
    // STR_LEN32, no refcount
    4, 255, 8, 0,
    // STR_LEN0, refcount
    255, 2, 3, 0,
    // STR_LEN8, refcount
    3, 2, 4, 0,
    // STR_LEN16, refcount
    4, 2, 6, 0,
    // STR_LEN32, refcount
    4, 2, 8, 0,
};

string _strEmpty = _S;

_Post_equal_to_(STR_ALLOC_SIZE)
static inline uint32 _strAllocSz(uint8 hdr, uint32 strsz)
{
    uint32 sz = STR_OFF_STR(hdr) + strsz + 1;
    sz = (sz + (STR_ALLOC_SIZE - 1)) / STR_ALLOC_SIZE;
    return (uint32)xaOptSize((size_t)max(sz, 1) * STR_ALLOC_SIZE);
}

// fairly arbitrary numbers to start with, can tune if needed
static inline uint8 _strLenClass(uint32 dlen, uint32 dref)
{
    if (dref < 200 && dlen < 200)
        return STR_LEN8;
    if (dlen < 60000)
        return STR_LEN16;
    return STR_LEN32;
}

void _strSetLen(_Inout_ string s, uint32 len)
{
    switch (STR_HDR(s) & STR_LEN_MASK) {
    case STR_LEN8:
        STR_FIELD(s, STR_OFF_LEN(STR_HDR(s)), uint8) = (uint8)len;
        break;
    case STR_LEN16:
        STR_FIELD(s, STR_OFF_LEN(STR_HDR(s)), uint16) = (uint16)len;
        break;
    case STR_LEN32:
        STR_FIELD(s, STR_OFF_LEN(STR_HDR(s)), uint32) = len;
    }
    // STR_LEN0 -- Bad Things
}

void _strInitRef(_Inout_ string s)
{
    int l = STR_HDR(s) & STR_LEN_MASK;

    // don't waste time with atomic stores on init since nothing else can possibly be accessing it yet

    if (l <= STR_LEN8)
        STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), uint8) = 1;
    else // STR_LEN16 and STR_LEN32 both have 16-bit ref count
        STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), uint16) = 1;

    // and if you called this function on something without STR_ALLOC set, woe be upon you...
}

void _strSetRef(_Inout_ string s, uint16 ref)
{
    int l = STR_HDR(s) & STR_LEN_MASK;

    if (l <= STR_LEN8)
        atomicStore(uint8, &STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), atomic(uint8)), (uint8)ref, AcqRel);
    else // STR_LEN16 and STR_LEN32 both have 16-bit ref count
        atomicStore(uint16, &STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), atomic(uint16)), (uint16)ref, AcqRel);

    // and if you called this function on something without STR_ALLOC set, woe be upon you...
}

static void _strIncRef(_Inout_ string s)
{
    int l = STR_HDR(s) & STR_LEN_MASK;

    if (l <= STR_LEN8)
        atomicFetchAdd(uint8, &STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), atomic(uint8)), 1, Relaxed);
    else // STR_LEN16 and STR_LEN32 both have 16-bit ref count
        atomicFetchAdd(uint16, &STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), atomic(uint16)), 1, Relaxed);
}

static uint16 _strDecRef(_Inout_ string s)
{
    int l = STR_HDR(s) & STR_LEN_MASK;

    if (l <= STR_LEN8)
        return atomicFetchSub(uint8, &STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), atomic(uint8)), 1, Release);
    else // STR_LEN16 and STR_LEN32 both have 16-bit ref count
        return atomicFetchSub(uint16, &STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), atomic(uint16)), 1, Release);
}

_Ret_maybenull_ string _strCopy(_In_ strref s, uint32 minsz)
{
    uint32 len = _strFastLen(s);

    string ret =  0;
    strReset(&ret, max(len, minsz));
    if (!ret)
        return NULL;
    _strSetLen(ret, len);
    _strFastCopy(s, 0, STR_BUFFER(ret), len);
    STR_BUFFER(ret)[len] = 0;           // terminating NULL
    // copy encoding bits to new string
    *STR_HDRP(ret) &= ~STR_ENCODING_MASK;
    *STR_HDRP(ret) |= STR_HDR(s) & STR_ENCODING_MASK;
    return ret;
}

uint32 _strFastCopy(_In_ strref s, uint32 off, _Out_writes_bytes_(bytes) uint8 *buf, uint32 bytes)
{
    if (!(STR_HDR(s) & STR_ROPE)) {
        // easy, just copy the buffer
        memcpy(buf, STR_BUFFER(s) + off, bytes);
        return bytes;
    } else {
        // harder, punt to rope module
        return _strRopeFastCopy(s, off, buf, bytes);
    }
}

bool _strResize(_Inout_ string *ps, uint32 newsz, bool unique)
{
    devAssert(!(STR_HDR(*ps) & STR_ROPE));
    bool isstack = STR_HDR(*ps) & STR_STACK;

    // see if we are the only owner and can just realloc in place
    if (STR_HDR(*ps) & STR_ALLOC && !isstack) {
        uint32 bufsz = (uint32)xaSize(*ps) - (uint32)((uint8 *)STR_BUFFER(*ps) - (uint8 *)(*ps));
        if (bufsz >= newsz + 1) {       // check for room for string + terminator
            // room already, good to go
            if (unique)
                return _strMakeUnique(ps, newsz);
            return true;
        }

        uint8 lencl = _strLenClass(newsz, 1);
        if (_strFastRef(*ps) == 1 && lencl == (STR_HDR(*ps) & STR_LEN_MASK)) {
            // same length class, can just realloc and not have to change the header
            xaResize(ps, _strAllocSz(STR_HDR(*ps), newsz));
            return true;
        } else {
            // can't reallocate, just copy it and deref the original
            string s = *ps;
            *ps = _strCopy(*ps, newsz);
            strDestroy(&s);
            return true;
        }
    } else if (isstack) {
        // stack allocated string, check the buffer size
        uint32 bufsz = _strFastRefNoSync(*ps);
        if (bufsz >= newsz + 1)
            return true;

        // not enough room, replace it with a normal string from the heap
        *ps = _strCopy(*ps, newsz);
        return (*ps != NULL);
    } else {
        // not allocated by us, make a copy
        *ps = _strCopy(*ps, newsz);
        return (*ps != NULL);
    }
}

bool _strMakeUnique(_Inout_ string *ps, uint32 minszforcopy)
{
    string s = *ps;         // borrow ref

    // stack allocated strings are always unique
    if (STR_HDR(s) & STR_STACK)
        return true;

    if (STR_HDR(s) & STR_ALLOC) {
        uint16 ref = _strFastRef(s);
        // if there's only a single ref, it's already unique
        if (ref == 1)
            return true;

        // replace caller's handle with a fresh unique copy
        if (STR_HDR(s) & STR_ROPE)
            *ps = _strCloneRope(s);
        else
            *ps = _strCopy(s, minszforcopy);

        // and remove the ref that the caller held
        strDestroy(&s);
        return true;
    } else {
        // wasn't allocated by us, so copy it into one that is
        *ps = _strCopy(s, minszforcopy);
        return (*ps != NULL);
    }
}

bool _strFlatten(_Inout_ string *ps, uint32 minszforcopy)
{
    string s = *ps;     // borrow ref

    // stack allocated strings are always flat
    if (STR_HDR(s) & STR_STACK)
        return true;

    if (STR_HDR(s) & STR_ALLOC) {
        uint16 ref = _strFastRef(s);
        // if there's only a single ref, it's already unique
        // but if this is a rope, we do want to flatten it
        if (!(STR_HDR(s) & STR_ROPE) && ref == 1)
            return true;

        // replace caller's handle with a fresh unique copy
        *ps = _strCopy(s, minszforcopy);

        // and remove the ref that the caller held
        strDestroy(&s);
        return true;
    } else {
        // wasn't allocated by us, so copy it into one that is
        *ps = _strCopy(s, minszforcopy);
        return (*ps != NULL);
    }
}

void strReset(_Inout_ string *o, uint32 sizehint)
{
    if (!o)
        return;

    strDestroy(o);
    uint8 lencl = _strLenClass(sizehint, 0);

    // UTF-8 and ASCII are set on empty string so concats pick up the other string
    uint8 newhdr = STR_CX | STR_ALLOC | STR_UTF8 | STR_ASCII | lencl;

    string ret = xaAlloc(_strAllocSz(newhdr, sizehint));
    if (!ret)
        return;

    *(uint8*)ret = newhdr;
    ((uint8*)ret)[1] = 0xc1;        // magic string header
    STR_BUFFER(ret)[0] = 0;

    _strSetLen(ret, 0);
    _strInitRef(ret);

    *o = ret;
}

static void strDupIntoStack(string *o, strref s)
{
    uint32 bufsz = _strFastRefNoSync(*o);
    uint32 srclen = _strFastLen(s);

    // see if there's enough room in the buffer
    if (srclen > bufsz - 1) {
        // if not, replace it with a heap-allocated copy
        *o = _strCopy(s, 0);
        return;
    }

    _strFastCopy(s, 0, STR_BUFFER(*o), srclen);
    STR_BUFFER(*o)[srclen] = 0;
    _strSetLen(*o, srclen);
}

void strDup(_Inout_ string *o, _In_opt_ strref s)
{
    if (!o || !STR_CHECK_VALID(s)) return;
    if (*o == s)
        return;

    // special case for duplicating into a stack-allocated string
    // do a copy into the buffer instead
    if (STR_CHECK_VALID(*o) && (STR_HDR(*o) & STR_STACK)) {
        strDupIntoStack(o, s);
        return;
    }

    strDestroy(o);

    int l = STR_HDR(s) & STR_LEN_MASK;

    if (!(STR_HDR(s) & STR_CX) || (STR_HDR(s) & STR_STACK)) {
        // if this is a plain C string, copy it, because we can't
        // make any assumptions about the lifetime of the underlying
        // buffer.
        // also copy if the source is a stack allocated string, since
        // it can't be referenced.
        *o = _strCopy(s, 0);
        return;
    } else if (!(STR_HDR(s) & STR_ALLOC)) {
        // assume strings we don't control are immutable
        // makeunique will copy this string if we need to modify it
        *o = (string)s;
        return;
    }

    // There is a bit of buffer room above maxref, so we can bypass thread
    // synchronization for checking the count here
    uint16 maxref = (l == STR_LEN0 || l == STR_LEN8) ? 240 : 65000;
    uint16 ref = _strFastRefNoSync(s);

    // source is a refcounted string, see if we can just bump the count
    if (ref < maxref) {
        _strIncRef((string)s);
        *o = (string)s;
        return;
    }

    // have to actually copy the string
    *o = _strCopy(s, 0);
}

void strCopy(_Inout_ string *o, _In_opt_ strref s)
{
    if (!o || !STR_CHECK_VALID(s)) return;
    if (*o == s)
        return;

    // special case for copying into a stack-allocated string
    // do a copy into the buffer instead
    if (STR_CHECK_VALID(*o) && (STR_HDR(*o) & STR_STACK)) {
        strDupIntoStack(o, s);
        return;
    }

    strDestroy(o);

    *o = _strCopy(s, 0);
}

void _strReset(_Inout_ string *s, uint32 minsz)
{
    if (!STR_CHECK_VALID(*s) || !(STR_HDR(*s) & STR_ALLOC) ||
        !!(STR_HDR(*s) & STR_ROPE) ||
        (!(STR_HDR(*s) & STR_STACK) && _strFastRef(*s) != 1)) {
        // can't do anything with these, just re-init them
        strReset(s, minsz);
        return;
    }

    if (!(STR_HDR(*s) & STR_STACK)) {
        uint32 bufsz = (uint32)xaSize(*s) - (uint32)((uint8 *)STR_BUFFER(*s) - (uint8 *)(*s));
        if (bufsz < minsz + 1) {
            // just destroy and create new rather than copy useless data
            strReset(s, minsz);
            return;
        }
    } else {
        // stack allocated string, just verify that the buffer is big enough
        uint32 bufsz = _strFastRefNoSync(*s);
        if (bufsz < minsz + 1) {
            strReset(s, minsz);
            return;
        }
    }

    // yay, we can actually be efficient!
    *STR_HDRP(*s) |= STR_ASCII | STR_UTF8;
    STR_BUFFER(*s)[0] = 0;
    _strSetLen(*s, 0);
}

void strClear(_Inout_ string *s)
{
    if (!s)
        return;

    _strReset(s, 0);
}

uint32 strLen(_In_opt_ strref s)
{
    if (!STR_CHECK_VALID(s)) return 0;

    return _strFastLen(s);
}

bool strEmpty(_In_opt_ strref s)
{
    if (!STR_CHECK_VALID(s)) return true;

    // avoid calling cstrLen by checking first byte of strings
    // that don't have the length embedded
    if (!(STR_HDR(s) & STR_CX))
        return !((const uint8*)s)[0];
    if ((STR_HDR(s) & STR_LEN_MASK) == STR_LEN0)
        return !STR_BUFFER(s)[0];

    return _strFastLen(s) == 0;
}

void strDestroy(_Inout_ string *ps)
{
    string s = STR_SAFE_DEREF(ps);
    if (!s) return;

    if (!(STR_HDR(s) & STR_ALLOC) || (STR_HDR(s) & STR_STACK)) {
        // don't deallocate strings we didn't alloc, but still clear the handle
        *ps = NULL;
        return;
    }

    uint16 ref = _strDecRef(s);
    if (ref > 1) {
        // just deref if there are multiple refs out there
        *ps = NULL;
        return;
    }

    atomicFence(Acquire);
    if (STR_HDR(s) & STR_ROPE) {
        _strDestroyRope(s);
    }

    // this is the last reference
    xaFree(s);
    *ps = NULL;
}

const char *strC(_In_opt_ strref s)
{
    if (!STR_CHECK_VALID(s)) return "";

    if (!(STR_HDR(s) & STR_ROPE)) {
        // simple string, can just return the buffer
        return (char*)STR_BUFFER(s);
    } else {
        uint32 len = _strFastLen(s);
        char *buf = scratchGet((size_t)len + 1);
        _strRopeFastCopy(s, 0, (uint8*)buf, len);
        buf[len] = 0;
        return buf;
    }
}

_Ret_maybenull_ uint8 *strBuffer(_Inout_ string *ps, uint32 minsz)
{
    if (!ps)
        return NULL;
    if (!STR_CHECK_VALID(*ps))
        strReset(ps, minsz);

    // make sure ps points to a flat string with only 1 reference
    if (!_strFlatten(ps, minsz))
        return NULL;

    uint32 cursz = _strFastLen(*ps);
    if (cursz < minsz) {
        _strResize(ps, minsz, false);
        memset(&STR_BUFFER(*ps)[cursz], 0, (size_t)minsz - cursz + 1);
        _strSetLen(*ps, minsz);
    }

    // we can't guarantee any encodings will still be valid after the buffer is modified
    *STR_HDRP(*ps) &= ~STR_ENCODING_MASK;

    return STR_BUFFER(*ps);
}

uint32 strCopyOut(_In_opt_ strref s, uint32 off, _Out_writes_bytes_(bufsz) uint8 *buf, uint32 bufsz)
{
    if (!STR_CHECK_VALID(s) || !buf || !bufsz)
        return 0;

    uint32 len = _strFastLen(s);
    off = min(off, len);
    len = min(len - off, bufsz - 1);
    buf[len] = 0;
    return _strFastCopy(s, off, buf, len);
}

uint32 strCopyRaw(_In_opt_ strref s, uint32 off, _Out_writes_bytes_(maxlen) uint8 *buf, uint32 maxlen)
{
    if (!STR_CHECK_VALID(s) || !buf || !maxlen)
        return 0;

    uint32 len = _strFastLen(s);
    off = min(off, len);
    len = min(len - off, maxlen);
    return _strFastCopy(s, off, buf, len);
}

bool strSetLen(_Inout_ string *ps, uint32 len)
{
    if (!ps)
        return false;
    if (!STR_CHECK_VALID(*ps))
        _strReset(ps, len);

    // make sure ps points to a flat string with only 1 reference
    if (!_strFlatten(ps, len))
        return false;

    uint32 cursz = _strFastLen(*ps);
    if (cursz < len) {
        _strResize(ps, len, false);
        memset(&STR_BUFFER(*ps)[cursz], 0, (size_t)len - cursz + 1);
    } else {
        STR_BUFFER(*ps)[len] = 0;
    }

    _strSetLen(*ps, len);
    return true;
}

int strTestRefCount(_In_opt_ strref s)
{
    if (!STR_CHECK_VALID(s) || !(STR_HDR(s) & STR_ALLOC) || (STR_HDR(s) & STR_STACK))
        return 0;

    return _strFastRef(s);
}

uint32 _strStackAllocSize(uint32 maxlen)
{
    if (maxlen == 0)
        return 0;
    if (maxlen < 254)
        return maxlen + 5;      // 2 bytes header, 1 byte ref, 1 bytes len, 1 byte null terminator
    if (maxlen < 65529)
        return maxlen + 7;      // 2 bytes header, 2 byte ref, 2 bytes len, 1 byte null terminator
    devFatalError("Tried to stack allocate too long of a string");
    return 0;
}

void _strInitStack(_Inout_ string *ps, uint32 maxlen)
{
    devAssert(*ps);
    if (!*ps || maxlen == 0 || maxlen >= 65529) {
        *ps = NULL;
        return;
    }
    string s = *ps;

    uint8 lencl = 0;
    if (maxlen < 254)
        lencl = STR_LEN8;
    else
        lencl = STR_LEN16;

    uint8 newhdr = STR_CX | STR_ALLOC | STR_STACK | STR_UTF8 | STR_ASCII | lencl;

    *(uint8*)s = newhdr;
    ((uint8*)s)[1] = 0xc1;      // magic string header

    // stack-allocated only ever have a single reference but set STR_ALLOC, so we
    // can stash the size of the buffer in the reference count field.
    if (lencl == STR_LEN8) {
        STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), uint8) = (uint8)maxlen + 1;
        STR_FIELD(s, STR_OFF_LEN(STR_HDR(s)), uint8) = (uint8)0;
    } else {
        STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), uint16) = (uint16)maxlen + 1;
        STR_FIELD(s, STR_OFF_LEN(STR_HDR(s)), uint16) = (uint16)0;
    }

    STR_BUFFER(s)[0] = 0;
}
