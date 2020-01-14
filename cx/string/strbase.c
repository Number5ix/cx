#include "string_private.h"
#include "strtest.h"
#include "cx/platform/base.h"

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

static inline uint32 _strAllocSz(uint8 hdr, uint32 strsz)
{
    uint32 sz = STR_OFF_STR(hdr) + strsz + 1;
    sz = (sz + (STR_ALLOC_SIZE - 1)) / STR_ALLOC_SIZE;
    return max(sz, 1) * STR_ALLOC_SIZE;
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

void _strSetLen(string s, uint32 len)
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

void _strSetRef(string s, uint16 ref)
{
    int l = STR_HDR(s) & STR_LEN_MASK;

    if (l <= STR_LEN8)
        atomic_store_uint8(&STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), atomic_uint8), (uint8)ref, ATOMIC_ACQ_REL);
    else // STR_LEN16 and STR_LEN32 both have 16-bit ref count
        atomic_store_uint16(&STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), atomic_uint16), (uint16)ref, ATOMIC_ACQ_REL);

    // and if you called this function on something without STR_ALLOC set, woe be upon you...
}

void _strIncRef(string s)
{
    int l = STR_HDR(s) & STR_LEN_MASK;

    if (l <= STR_LEN8)
        atomic_fetch_add_uint8(&STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), atomic_uint8), 1, ATOMIC_RELAXED);
    else // STR_LEN16 and STR_LEN32 both have 16-bit ref count
        atomic_fetch_add_uint16(&STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), atomic_uint16), 1, ATOMIC_RELAXED);
}

uint16 _strDecRef(string s)
{
    int l = STR_HDR(s) & STR_LEN_MASK;

    if (l <= STR_LEN8)
        return atomic_fetch_sub_uint8(&STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), atomic_uint8), 1, ATOMIC_RELEASE);
    else // STR_LEN16 and STR_LEN32 both have 16-bit ref count
        return atomic_fetch_sub_uint16(&STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), atomic_uint16), 1, ATOMIC_RELEASE);
}

string _strCopy(string s, uint32 minsz)
{
    uint32 len = _strFastLen(s);

    string ret = 0;
    strInit(&ret, max(len, minsz));
    if (!ret)
        return NULL;
    _strSetLen(ret, len);
    _strFastCopy(s, 0, STR_BUFFER(ret), len);
    STR_BUFFER(ret)[len] = 0;           // termianting NULL
    // copy encoding bits to new string
    *STR_HDRP(ret) &= ~STR_ENCODING_MASK;
    *STR_HDRP(ret) |= STR_HDR(s) & STR_ENCODING_MASK;
    return ret;
}

uint32 _strFastCopy(string s, uint32 off, char *buf, uint32 bytes)
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

bool _strResize(string *ps, uint32 newsz, bool unique)
{
    // TODO: add assert that this isn't a rope?
    // see if we are the only owner and can just realloc in place
    if (STR_HDR(*ps) & STR_ALLOC) {
        uint32 bufsz = (uint32)xaSize(*ps) - (uint32)((char*)STR_BUFFER(*ps) - (char*)(*ps));
        if (bufsz >= newsz + 1) {       // check for room for string + terminator
            // room already, good to go
            if (unique)
                return _strMakeUnique(ps, newsz);
            return true;
        }

        uint8 lencl = _strLenClass(newsz, 1);
        if (_strFastRef(*ps) == 1 && lencl == (STR_HDR(*ps) & STR_LEN_MASK)) {
            // same length class, can just realloc and not have to change the header
            *ps = xaResize(*ps, _strAllocSz(STR_HDR(*ps), newsz));
            return true;
        } else {
            // can't reallocate, just copy it and deref the original
            string s = *ps;
            *ps = _strCopy(*ps, newsz);
            strDestroy(&s);
            return true;
        }
    } else {
        // not allocated by us, make a copy
        *ps = _strCopy(*ps, newsz);
        return (*ps != NULL);
    }
}

bool _strMakeUnique(string *ps, uint32 minszforcopy)
{
    string s = *ps;         // borrow ref

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

bool _strFlatten(string *ps, uint32 minszforcopy)
{
    string s = *ps;     // borrow ref

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

string strCreate()
{
    string ret = 0;

    strInit(&ret, 0);
    return ret;
}

void strInit(string *o, uint32 sizehint)
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
    _strSetRef(ret, 1);

    *o = ret;
}

void strDup(string *o, string s)
{
    if (o && *o == s)
        return;

    strDestroy(o);
    if (!o || !STR_CHECK_VALID(s)) return;

    int l = STR_HDR(s) & STR_LEN_MASK;

    if (!(STR_HDR(s) & STR_CX)) {
        // if this is a plain C string, copy it, because we can't
        // make any assumptions about the lifetime of the underlying
        // buffer
        *o = _strCopy(s, 0);
        return;
    } else if (!(STR_HDR(s) & STR_ALLOC)) {
        // assume strings we don't control are immutable
        // makeunique will copy this string if we need to modify it
        *o = s;
        return;
    }

    // There is a bit of buffer room above maxref, so we can bypass thread
    // synchronization for checking the count here
    uint16 maxref = (l == STR_LEN0 || l == STR_LEN8) ? 240 : 65000;
    uint16 ref = _strFastRefNoSync(s);

    // source is a refcounted string, see if we can just bump the count
    if (ref < maxref) {
        _strIncRef(s);
        *o = s;
        return;
    }

    // have to actually copy the string
    *o = _strCopy(s, 0);
}

void strCopy(string *o, string s)
{
    strDestroy(o);
    if (!o || !STR_CHECK_VALID(s)) return;

    *o = _strCopy(s, 0);
}

void _strReset(string *s, uint32 minsz)
{
    if (!STR_CHECK_VALID(*s) || !(STR_HDR(*s) & STR_ALLOC) ||
        !!(STR_HDR(*s) & STR_ROPE) || _strFastRef(*s) != 1) {
        // can't do anything with these, just re-init them
        strInit(s, minsz);
        return;
    }

    uint32 bufsz = (uint32)xaSize(*s) - (uint32)((char*)STR_BUFFER(*s) - (char*)(*s));
    if (bufsz < minsz + 1 && _strLenClass(minsz, 1) == (STR_HDR(*s) & STR_LEN_MASK)) {
        // attempt to expand the allocation in place (does not copy)
        bufsz = (uint32)xaExpand(*s, _strAllocSz(STR_HDR(*s), minsz), 0) - (uint32)((char*)STR_BUFFER(*s) - (char*)(*s));
    }

    if (bufsz < minsz + 1) {
        // just destroy and create new rather than copy useless data
        strInit(s, minsz);
        return;
    }

    // yay, we can actually be efficient!
    *STR_HDRP(*s) |= STR_ASCII | STR_UTF8;
    STR_BUFFER(*s)[0] = 0;
    _strSetLen(*s, 0);
}

void strClear(string *s)
{
    if (!s)
        return;

    _strReset(s, 0);
}

uint32 strLen(string s)
{
    if (!STR_CHECK_VALID(s)) return 0;

    return _strFastLen(s);
}

bool strEmpty(string s)
{
    if (!STR_CHECK_VALID(s)) return true;

    // avoid calling cstrLen by checking first byte of strings
    // that don't have the length embedded
    if (!(STR_HDR(s) & STR_CX))
        return !((const char*)s)[0];
    if ((STR_HDR(s) & STR_LEN_MASK) == STR_LEN0)
        return !STR_BUFFER(s)[0];

    return _strFastLen(s) == 0;
}

void strDestroy(string *ps)
{
    string s = STR_SAFE_DEREF(ps);
    if (!s) return;

    if (!(STR_HDR(s) & STR_ALLOC)) {
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

    atomic_fence(ATOMIC_ACQUIRE);
    if (STR_HDR(s) & STR_ROPE) {
        _strDestroyRope(s);
    }

    // this is the last reference
    xaFree(s);
    *ps = NULL;
}

const char *strC(string *ps)
{
    string s = STR_SAFE_DEREF(ps);
    if (!s) return "";

    if (!(STR_HDR(s) & STR_ROPE)) {
        // simple string, can just return the buffer
        return STR_BUFFER(s);
    } else {
        // have to flatten so that C string even exists
        if (!_strFlatten(ps, 0))
            return NULL;
        return STR_BUFFER(*ps);
    }
}

char *strBuffer(string *ps, uint32 minsz)
{
    if (!ps)
        return NULL;
    if (!STR_CHECK_VALID(*ps))
        _strReset(ps, minsz);

    // make sure ps points to a flat string with only 1 reference
    if (!_strFlatten(ps, minsz))
        return NULL;

    uint32 cursz = _strFastLen(*ps);
    if (cursz < minsz) {
        _strResize(ps, minsz, false);
        memset(&STR_BUFFER(*ps)[cursz], 0, minsz - cursz + 1);
        _strSetLen(*ps, minsz);
    }

    // we can't guarantee any encodings will still be valid after the buffer is modified
    *STR_HDRP(*ps) &= ~STR_ENCODING_MASK;

    return STR_BUFFER(*ps);
}

uint32 strCopyOut(string s, uint32 off, char *buf, uint32 bufsz)
{
    if (!STR_CHECK_VALID(s) || !buf || !bufsz)
        return 0;

    uint32 len = _strFastLen(s);
    off = min(off, len);
    len = min(len - off, bufsz - 1);
    buf[len] = 0;
    return _strFastCopy(s, off, buf, len);
}

bool strSetLen(string *ps, uint32 len)
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
        memset(&STR_BUFFER(*ps)[cursz], 0, len - cursz + 1);
    }

    _strSetLen(*ps, len);
    return true;
}

int strTestRefCount(string s)
{
    if (!STR_CHECK_VALID(s) || !(STR_HDR(s) & STR_ALLOC))
        return 0;

    return _strFastRef(s);
}
