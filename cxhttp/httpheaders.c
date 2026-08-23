// ---------------------------------------------------------------------------------------------
// Header field lists
//
// An ordered list of name/value pairs held as two index-aligned sa_string arrays. See the
// HttpHeaders comment in http_shared.h for why this is not a hashtable; the short version is that
// Set-Cookie repeats and must never be folded.
//
// The pairing is an invariant of this file and nothing else: every mutation below pushes to or
// removes from both arrays, so saSize(names) == saSize(values) always holds.
// ---------------------------------------------------------------------------------------------

#include "http_private.h"

#include <cx/container.h>

// Index of the first field whose name matches, or -1. Case-insensitive, because HTTP field names
// are, and ASCII-only, because HTTP field names are that too -- strEqi is exactly the right
// comparison rather than an approximation of one.
static int32 findHeader(const HttpHeaders* h, strref name, int32 from)
{
    for (int32 i = from; i < saSize(h->names); i++) {
        if (strEqi(h->names.a[i], name))
            return i;
    }
    return -1;
}

_Use_decl_annotations_
bool httpHeadersAdd(HttpHeaders* h, strref name, strref value)
{
    if (strEmpty(name))
        return false;

    saPush(&h->names, strref, name);
    saPush(&h->values, strref, value);
    return true;
}

_Use_decl_annotations_
bool httpHeadersSet(HttpHeaders* h, strref name, strref value)
{
    if (strEmpty(name))
        return false;

    int32 first = findHeader(h, name, 0);
    if (first < 0)
        return httpHeadersAdd(h, name, value);

    // Overwrite in place so the field keeps its position in the block, then drop any later
    // duplicates. Walking backwards means each removal cannot disturb an index still to be visited.
    strDup(&h->values.a[first], value);

    for (int32 i = saSize(h->names) - 1; i > first; i--) {
        if (strEqi(h->names.a[i], name)) {
            saRemove(&h->names, i);
            saRemove(&h->values, i);
        }
    }

    return true;
}

_Use_decl_annotations_
bool httpHeadersGet(const HttpHeaders* h, strref name, strhandle out)
{
    int32 i = findHeader(h, name, 0);
    if (i < 0)
        return false;

    strDup(out, h->values.a[i]);
    return true;
}

_Use_decl_annotations_
int32 httpHeadersGetAll(const HttpHeaders* h, strref name, sa_string* out)
{
    saInit(out, string, 2);

    for (int32 i = 0; i < saSize(h->names); i++) {
        if (strEqi(h->names.a[i], name))
            saPush(out, string, h->values.a[i]);
    }

    return saSize(*out);
}

_Use_decl_annotations_
bool httpHeadersHas(const HttpHeaders* h, strref name)
{
    return findHeader(h, name, 0) >= 0;
}

_Use_decl_annotations_
int32 httpHeadersRemove(HttpHeaders* h, strref name)
{
    int32 n = 0;

    for (int32 i = saSize(h->names) - 1; i >= 0; i--) {
        if (strEqi(h->names.a[i], name)) {
            saRemove(&h->names, i);
            saRemove(&h->values, i);
            n++;
        }
    }

    return n;
}

_Use_decl_annotations_
void httpHeadersClear(HttpHeaders* h)
{
    saClear(&h->names);
    saClear(&h->values);
}

_Use_decl_annotations_
bool httpHeadersHasToken(const HttpHeaders* h, strref name, strref token)
{
    bool found = false;
    string tok = 0;

    // Every occurrence, not just the first: a peer is free to send `Connection: keep-alive` and
    // `Connection: Upgrade` as two fields rather than one comma-separated value.
    for (int32 i = 0; i < saSize(h->names) && !found; i++) {
        if (!strEqi(h->names.a[i], name))
            continue;

        int32 pos = 0;
        while (!found && strSplitNext(h->values.a[i], &pos, _SL(","), &tok)) {
            _httpTrimOWS(&tok);
            if (strEqi(tok, token))
                found = true;
        }
    }

    strDestroy(&tok);
    return found;
}

_Use_decl_annotations_
bool httpHeadersFormat(strhandle out, const HttpHeaders* h)
{
    for (int32 i = 0; i < saSize(h->names); i++) {
        strAppend(out, h->names.a[i]);
        strAppend(out, _SL(": "));
        strAppend(out, h->values.a[i]);
        strAppend(out, _SL("\r\n"));
    }

    return true;
}
