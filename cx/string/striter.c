#include "string_private.h"

static void _striClear(_Inout_ striter *iter);
static bool _striRewindU8(_Inout_ striter *i, uint32 off);

static void _striSetup(_Inout_ striter *iter, uint32 off, uint32 newcursor, bool reverse)
{
    uint32 slen = _strFastLen(iter->_str);

    if (!(_strHdr(iter->_str) & STR_ROPE)) {
        if (reverse)
            off = 0;
        iter->bytes = _strBuffer(iter->_str) + off;
        iter->off = off;
        iter->len = slen - off;
    } else {
        string realstr;
        uint32 realoff, realstart;
        if (_strRopeRealStr(&iter->_str, off, &realstr, &realoff, &iter->len, &realstart, false)) {
            // this is safe because we're holding a reference to the top-level rope,
            // which in turn makes sure the child strings don't get destroyed
            if (!reverse) {
                iter->bytes = _strBuffer(realstr) + realoff;
                iter->off = off;
            } else {
                iter->bytes = _strBuffer(realstr) + realstart;
                iter->off = off - (realoff - realstart);
                iter->len += (realoff - realstart);
            }
        } else {
            _striClear(iter);
        }
    }
    iter->cursor = newcursor;
}

static void _striClear(_Inout_ striter *iter)
{
    iter->bytes = NULL;
    iter->off = _strFastLen(iter->_str);
    iter->len = 0;
    iter->cursor = 0;
}

static void _striInit(_Out_ striter *iter, _In_opt_ strref s, bool reverse)
{
    if (!STR_CHECK_VALID(s))
        s = _strEmpty;

    iter->_borrowed = false;
    // if this is an allocated string, grab a ref, otherwise just store it
    if ((_strHdr(s) & STR_ALLOC) && !(_strHdr(s) & STR_STACK)) {
        iter->_str = 0;
        strDup(&iter->_str, s);
    } else {
        iter->_str = (string)s;         // non STR_ALLOC strings are readonly anyway
    }

    // prime iterator with first run
    if (!reverse)
        _striSetup(iter, 0, 0, false);
    else
        _striSetup(iter, strLen(s) - 1, 0, true);
}

void striInit(_Out_ striter *iter, _In_opt_ strref s)
{
    _striInit(iter, s, false);
}

void striInitRev(_Out_ striter *iter, _In_opt_ strref s)
{
    _striInit(iter, s, true);
}

static void _striBorrow(_Out_ striter *iter, _In_opt_ strref s, bool reverse)
{
    if (!STR_CHECK_VALID(s))
        s = _strEmpty;

    iter->_borrowed = true;
    iter->_str = (string)s;

    // prime iterator with first run
    if (!reverse)
        _striSetup(iter, 0, 0, false);
    else
        _striSetup(iter, strLen(s) - 1, 0, true);
}

void striBorrow(_Out_ striter *iter, _In_opt_ strref s)
{
    _striBorrow(iter, s, false);
}

void striBorrowRev(_Out_ striter *iter, _In_opt_ strref s)
{
    _striBorrow(iter, s, true);
}

bool striNext(_Inout_ striter *iter)
{
    if (!(iter && STR_CHECK_VALID(iter->_str)))
        return false;

    uint32 slen = _strFastLen(iter->_str);
    uint32 nextoff = iter->off + iter->len;
    uint32 nextcursor = 0;

    if (iter->len > iter->cursor)
        nextcursor = iter->cursor - iter->len;

    if (nextoff >= slen) {
        // hit end of string
        _striClear(iter);
        return false;
    }

    _striSetup(iter, nextoff, nextcursor, false);
    return true;
}

bool striPrev(_Inout_ striter *iter)
{
    if (!(iter && STR_CHECK_VALID(iter->_str)))
        return false;

    uint32 lastcursor = iter->cursor;

    if (iter->off <= 0) {
        // hit beginning of string
        _striClear(iter);
        return false;
    }

    // get previous rope segment
    _striSetup(iter, iter->off - 1, 0, true);

    if (lastcursor < 0)
        iter->cursor = lastcursor + _strFastLen(iter->_str);

    return true;
}

bool striSeek(_Inout_ striter *iter, int32 off, STRI_SEEK_TYPE type, STRI_SEEK_WHENCE whence)
{
    if (!(iter && STR_CHECK_VALID(iter->_str)))
        return false;

    uint32 slen = _strFastLen(iter->_str);
    uint32 newoff = slen;           // default fail

    if (type == STRI_BYTE) {
        switch (whence) {
        case STRI_SET:
            newoff = (uint32)off;
            break;
        case STRI_CUR:
            newoff = iter->off + off;
            break;
        case STRI_END:
            newoff = slen - off;
            break;
        default:
            return false;
        }

        if (newoff >= 0 && newoff < slen) {
            _striSetup(iter, newoff, 0, false);
            return true;
        } else {
            _striClear(iter);
            return false;
        }
    } else if (type == STRI_U8CHAR) {
        // these are slow, but what can you do?
        switch (whence) {
        case STRI_SET:
            _striSetup(iter, 0, 0, false);
            if (striAdvanceU8(iter, (uint32)off))
                newoff = iter->off + iter->cursor;
            break;
        case STRI_CUR:
            if (off >= 0) {
                iter->cursor = 0;
                if (striAdvanceU8(iter, (uint32)off))
                    newoff = iter->off + iter->cursor;
            } else {
                return _striRewindU8(iter, (uint32)-off);
            }
            break;
        case STRI_END:
            _striClear(iter);
            return _striRewindU8(iter, (uint32)off);
        default:
            return false;
        }

        if (newoff >= 0 && newoff < slen) {
            _striSetup(iter, newoff, 0, false);
            return true;
        } else {
            _striClear(iter);
            return false;
        }
        return false;
    }

    // bad type
    return false;
}

void striFinish(_Inout_ striter *iter)
{
    if (!iter)
        return;

    _striClear(iter);
    if (iter->_borrowed) {
        iter->_str = 0;
        iter->_borrowed = false;
    } else {
        strDestroy(&iter->_str);        // will not deallocate unless this is STR_ALLOC
    }
}

_Success_(return) _Must_inspect_result_ bool striU8Char(_Inout_ striter *iter, _Out_ int32 *out)
{
    return _strUTF8Decode(iter, out);
}

_Success_(return) _Must_inspect_result_ bool striPeekU8Char(_Inout_ striter *iter, _Out_ int32 *out)
{
    striter saved = *iter;

    bool ret = _strUTF8Decode(iter, out);
    *iter = saved;
    return ret;
}

_Check_return_ bool striAdvanceU8(_Inout_ striter *iter, uint32 by)
{
    for (uint32 idx = 0; idx < by; idx++) {
        uint32 seqlen = _strUTF8Decode(iter, NULL);
        if (seqlen == 0)
            return false;
    }

    return true;
}

// this is going to be painful
static bool _striRewindU8(_Inout_ striter *iter, uint32 by)
{
    int ncont = 0;
    uint32 off = iter->off, seqlen;
    bool ret = false;
    uint8 u;

    striter temp;
    striInit(&temp, iter->_str);

    while (by > 0) {
        if (off == 0)
            goto out;           // hit start of string

        off--;
        _striSetup(&temp, off, 0, false);
        u = temp.bytes[0];
        if (u >= 0x80 && u <= 0xbf) {
            // continuation character
            ncont++;
            continue;
        }

        seqlen = _strUTF8SeqLen(u);
        if (seqlen != ncont + 1)
            goto out;           // invalid UTF-8

        ncont = 0;
        by--;
    }

    _striSetup(iter, off, 0, false);
    ret = true;

out:
    striFinish(&temp);
    return ret;
}
