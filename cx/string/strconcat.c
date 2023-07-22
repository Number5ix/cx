#include "string_private.h"

static bool _strAppendNoRope(string *io, strref s);

// This is the master algorithm for concatenating strings!
// All of the other versions are simplified copies with parts removed.
// If you change this function, be sure to update the two-argument versions,
// append, etc.
bool _strNConcat(string *o, int n, strref *_args)
{
    string *args = (string*)_args;          // we know what we doing
    uint8 *ptr;
    int i, start = 0;

    // Pass 1: Build a plan for concatenating the strings
    uint32 len = 0;
    string firstarg = 0;
    uint8 encoding = STR_ENCODING_MASK;
    for (i = 0; i < n; ++i) {
        if (!STR_CHECK_VALID(args[i])) {
            if (n - i > 1) {
                memmove(&args[i], &args[i + 1], (n - i - 1) * sizeof(string));
                --i;
            }
            --n;
            continue;
        }
        if (i == 0)
            firstarg = args[i];

        len += _strFastLen(args[i]);

        // AND all the encoding bits together to get the least common denominator
        encoding &= STR_HDR(args[i]) & STR_ENCODING_MASK;
    }

    // Pass 2: Do the concatenation
    if (n == 2 && *o && *o == firstarg && _strFastLen(*o) == 0) {
        // special optimization for appending to an empty string
        strDup(o, args[1]);
    } else if (n == 1 || len < ROPE_JOIN_THRESH || (*o && (STR_HDR(*o) & STR_STACK))) {
        // regular string concatenation
        // set up destination
        if (*o && *o == firstarg && !(STR_HDR(*o) & STR_ROPE)) {
            // optimize this case by leaving first string in place and skipping the arg
            _strResize(o, len, true);
            ptr = &STR_BUFFER(*o)[_strFastLen(*o)];
            start = 1;
        } else {
            _strReset(o, len);
            ptr = STR_BUFFER(*o);
        }

        *STR_HDRP(*o) &= ~STR_ENCODING_MASK;
        *STR_HDRP(*o) |= encoding;

        for (i = start; i < n; ++i) {
            uint32 alen = _strFastLen(args[i]);
            _strFastCopy(args[i], 0, ptr, alen);
            ptr += alen;
        }
        *ptr = 0;           // add null terminator

        _strSetLen(*o, len);
    } else {
        // final length is over the threshold to make this a rope instead
        string curtop = 0, curright = 0, newtop = 0;
        strDup(&curtop, args[0]);

        // build up new rope
        for (i = 1; i < n;) {
            if (!curright && _strFastLen(curtop) < ROPE_MIN_SIZE && _strFastLen(args[i]) < ROPE_MAX_MERGE) {
                _strAppendNoRope(&curtop, args[i++]);
            } else if (!curright) {
                strDup(&curright, args[i++]);
            } else if (_strFastLen(curright) < ROPE_MIN_SIZE && _strFastLen(args[i]) < ROPE_MAX_MERGE) {
                _strAppendNoRope(&curright, args[i++]);
            } else {
                // insert a new top node
                // push everything to the right; _strCreateRope will rotate as necessary
                newtop = _strCreateRope(curtop, 0, 0, curright, 0, 0, true);
                strDestroy(&curtop);
                strDestroy(&curright);
                curtop = newtop;
            }
        }

        if (curright) {
            newtop = _strCreateRope(curtop, 0, 0, curright, 0, 0, true);
            strDestroy(&curtop);
            strDestroy(&curright);
            curtop = newtop;
        }

        strDestroy(o);
        *o = curtop;
    }

    return true;
}

bool _strNConcatC(string *o, int n, string **args)
{
    uint8 *ptr;
    int i, start = 0;

    // Pass 1: Build a plan for concatenating the strings
    uint32 len = 0;
    string firstarg = 0;
    uint8 encoding = STR_ENCODING_MASK;
    for (i = 0; i < n; ++i) {
        if (!args[i] || !STR_CHECK_VALID(*args[i])) {
            if (n - i > 1) {
                memmove(&args[i], &args[i + 1], (n - i - 1) * sizeof(string*));
                --i;
            }
            --n;
            continue;
        }
        if (i == 0) {
            firstarg = *args[i];
        }

        len += _strFastLen(*args[i]);

        // AND all the encoding bits together to get the least common denominator
        encoding &= STR_HDR(*args[i]) & STR_ENCODING_MASK;
    }

    // Pass 2: Do the concatenation
    if (n == 2 && *o && *o == firstarg && _strFastLen(*o) == 0) {
        // special optimization for appending to an empty string
        strDup(o, *args[1]);
    } else if (n == 1 || len < ROPE_JOIN_THRESH || (*o && (STR_HDR(*o) & STR_STACK))) {
        // regular string concatenation
        // set up destination
        if (*o && *o == firstarg && !(STR_HDR(*o) & STR_ROPE)) {
            // optimize this case by leaving first string in place and skipping the arg
            _strResize(o, len, true);
            ptr = &STR_BUFFER(*o)[_strFastLen(*o)];
            start = 1;
        } else {
            _strReset(o, len);
            ptr = STR_BUFFER(*o);
        }

        *STR_HDRP(*o) &= ~STR_ENCODING_MASK;
        *STR_HDRP(*o) |= encoding;

        for (i = start; i < n; ++i) {
            uint32 alen = _strFastLen(*args[i]);
            _strFastCopy(*args[i], 0, ptr, alen);
            ptr += alen;
        }
        *ptr = 0;           // add null terminator

        _strSetLen(*o, len);
    } else {
        // final length is over the threshold to make this a rope instead
        string curtop = 0, curright = 0, newtop = 0;
        strDup(&curtop, *args[0]);

        // build up new rope
        for (i = 1; i < n;) {
            if (!curright && _strFastLen(curtop) < ROPE_MIN_SIZE && _strFastLen(*args[i]) < ROPE_MAX_MERGE) {
                _strAppendNoRope(&curtop, *args[i++]);
            } else if (!curright) {
                strDup(&curright, *args[i++]);
            } else if (_strFastLen(curright) < ROPE_MIN_SIZE && _strFastLen(*args[i]) < ROPE_MAX_MERGE) {
                _strAppendNoRope(&curright, *args[i++]);
            } else {
                // insert a new top node
                // push everything to the right; _strCreateRope will rotate as necessary
                newtop = _strCreateRope(curtop, 0, 0, curright, 0, 0, true);
                strDestroy(&curtop);
                strDestroy(&curright);
                curtop = newtop;
            }
        }

        if (curright) {
            newtop = _strCreateRope(curtop, 0, 0, curright, 0, 0, true);
            strDestroy(&curtop);
            strDestroy(&curright);
            curtop = newtop;
        }

        strDestroy(o);
        *o = curtop;
    }

    // consume arguments at the very end
    for (i = 0; i < n; ++i) {
        strDestroy(args[i]);
    }
    return true;
}

static bool _strAppendNoRope(string *io, strref s)
{
    uint32 iolen = strLen(*io), slen = _strFastLen(s);
    uint32 len = iolen + slen;
    uint8 encoding = STR_ENCODING_MASK;

    if (iolen == 0) {
        // special optimization for appending to an empty string
        strDup(io, s);
        return true;
    }

    encoding &= STR_HDR(*io) & STR_ENCODING_MASK;
    encoding &= STR_HDR(s) & STR_ENCODING_MASK;

    if (STR_HDR(*io) & STR_ROPE)                // have to flatten first to append in place
        _strFlatten(io, len);

    _strResize(io, len, true);
    uint8 *buf = STR_BUFFER(*io);
    _strFastCopy(s, 0, &buf[iolen], slen);      // copy second string after first
    buf[len] = 0;                               // add null terminator

    *STR_HDRP(*io) &= ~STR_ENCODING_MASK;
    *STR_HDRP(*io) |= encoding;
    _strSetLen(*io, len);

    return true;
}

static bool _strAppend(string *io, strref s)
{
    uint32 iolen = strLen(*io), slen = _strFastLen(s);
    uint32 len = iolen + slen;
    uint8 encoding = STR_ENCODING_MASK;

    if (!*io) {
        // dest doesn't exist, replace it with source
        strDup(io, s);
        return true;
    }

    encoding &= STR_HDR(*io) & STR_ENCODING_MASK;
    encoding &= STR_HDR(s) & STR_ENCODING_MASK;

    if (len < ROPE_JOIN_THRESH ||
        (iolen < ROPE_MIN_SIZE && slen < ROPE_MAX_MERGE) ||
        (slen < ROPE_MIN_SIZE && iolen < ROPE_MAX_MERGE) ||
        (*io && (STR_HDR(*io) & STR_STACK))) {
        // regular string concatenation

        if (STR_HDR(*io) & STR_ROPE)                // have to flatten first to append in place
            _strFlatten(io, len);

        _strResize(io, len, true);
        uint8 *buf = STR_BUFFER(*io);
        _strFastCopy(s, 0, &buf[iolen], slen);      // copy second string after first
        buf[len] = 0;                               // add null terminator

        *STR_HDRP(*io) &= ~STR_ENCODING_MASK;
        *STR_HDRP(*io) |= encoding;
        _strSetLen(*io, len);
    } else {
        // final length is over the threshold to make this a rope instead
        string top = _strCreateRope(*io, 0, 0, s, 0, 0, true);
        strDestroy(io);
        *io = top;
    }

    return true;
}

bool strAppend(string *io, strref s)
{
    if (!io)
        return false;

    if (!STR_CHECK_VALID(s))
        return true;            // appending nothing is easy

    return _strAppend(io, s);
}

bool strPrepend(strref s, string *io)
{
    string out;
    bool ret;

    strInit(&out);
    ret = strConcat(&out, s, *io);
    strDestroy(io);
    *io = out;
    return ret;
}

// for use by the rope module
bool _strConcatNoRope(string *o, strref s1, strref s2)
{
    // you really should be calling append...
    if (*o == s1)
        return _strAppendNoRope(o, s2);

    // short-circuit missing args
    if (!s1) {
        strDup(o, s2);
        return true;
    } else if (!s2) {
        strDup(o, s1);
        return true;
    }

    uint32 s1len = _strFastLen(s1), s2len = _strFastLen(s2);
    uint32 len = s1len + s2len;
    uint8 encoding = STR_ENCODING_MASK;

    encoding &= STR_HDR(s1) & STR_ENCODING_MASK;
    encoding &= STR_HDR(s2) & STR_ENCODING_MASK;

    // regular string concatenation

    _strReset(o, len);
    uint8 *buf = STR_BUFFER(*o);
    _strFastCopy(s1, 0, buf, s1len);
    _strFastCopy(s2, 0, &buf[s1len], s2len);
    buf[len] = 0;                               // add null terminator

    *STR_HDRP(*o) &= ~STR_ENCODING_MASK;
    *STR_HDRP(*o) |= encoding;
    _strSetLen(*o, len);

    return true;
}

bool strConcat(string *o, strref s1, strref s2)
{
    if (!o)
        return false;

    // you really should be calling append...
    if (*o == s1)
        return strAppend(o, s2);

    // short-circuit missing args
    if (!s1) {
        strDup(o, s2);
        return true;
    } else if (!s2) {
        strDup(o, s1);
        return true;
    }

    uint32 s1len = _strFastLen(s1), s2len = _strFastLen(s2);
    uint32 len = s1len + s2len;
    uint8 encoding = STR_ENCODING_MASK;

    encoding &= STR_HDR(s1) & STR_ENCODING_MASK;
    encoding &= STR_HDR(s2) & STR_ENCODING_MASK;

    if (len < ROPE_JOIN_THRESH ||
        (s1len < ROPE_MIN_SIZE && s2len < ROPE_MAX_MERGE) ||
        (s2len < ROPE_MIN_SIZE && s1len < ROPE_MAX_MERGE) ||
        (*o && (STR_HDR(*o) & STR_STACK))) {
        // regular string concatenation

        _strReset(o, len);
        uint8 *buf = STR_BUFFER(*o);
        _strFastCopy(s1, 0, buf, s1len);
        _strFastCopy(s2, 0, &buf[s1len], s2len);
        buf[len] = 0;                               // add null terminator

        *STR_HDRP(*o) &= ~STR_ENCODING_MASK;
        *STR_HDRP(*o) |= encoding;
        _strSetLen(*o, len);
    } else {
        // final length is over the threshold to make this a rope instead
        string top = _strCreateRope(s1, 0, 0, s2, 0, 0, true);
        strDestroy(o);
        *o = top;
    }

    return true;
}

bool strConcatC(string *o, string *sc1, string *sc2)
{
    bool append = (o == sc1);
    if (!(o && sc1 && sc2))
        return false;

    bool ret = strConcat(o, *sc1, *sc2);
    if (!append)
        strDestroy(sc1);
    strDestroy(sc2);
    return ret;
}
