#include "fs_private.h"
#include "cx/container/sarray.h"

// platform-agnostic path manipulation

string fsPathSepStr = _S"/";
string fsNSSepStr = _S":";

// Get parent directory
bool pathParent(string *out, string path)
{
    int32 sep = strFindR(path, 0, fsPathSepStr);
    if (sep <= 0 || strGetChar(path, sep - 1) == ':')
        return false;

    return strSubStr(out, path, 0, sep);
}

bool pathFilename(string *out, string path)
{
    int sep = strFindR(path, 0, fsPathSepStr);
    if (sep != -1)
        return strSubStr(out, path, sep + 1, 0);

    strDup(out, path);
    return false;
}

bool _pathJoin(string *out, int n, string* elements)
{
    string npath = 0;
    bool donefirst = false;
    bool lastroot = false;

    for (int i = 0; i < n; i++) {
        if (!strEmpty(elements[i])) {
            // absolute path in the middle of a join makes no sense
            if (i > 0 && pathIsAbsolute(elements[i])) {
                strDestroy(&npath);
                return false;
            }
            if (donefirst) {
                if (!lastroot) {
                    strNConcat(&npath, npath, fsPathSepStr, elements[i]);
                } else {
                    // already have a path separator
                    strAppend(&npath, elements[i]);
                    lastroot = false;
                }
            } else {
                strDup(&npath, elements[i]);
                if (strFindR(npath, 0, fsPathSepStr) == strLen(npath) - strLen(fsPathSepStr))
                    lastroot = true;        // this is a root that ends with the path separator
                donefirst = true;
            }
        }
    }

    strDestroy(out);
    *out = npath;
    return true;
}

void pathAddExt(string *out, string path, string ext)
{
    strNConcat(out, path, _S".", ext);
}

bool pathRemoveExt(string *out, string path)
{
    if (strEmpty(path))
        return false;

    int32 dot = strFindR(path, 0, _S".");
    int32 sep = strFindR(path, 0, fsPathSepStr);
    if (dot < clamplow(sep, 1))
        return false;

    strSubStr(out, path, 0, dot);
    return true;
}

bool pathGetExt(string *out, string path)
{
    if (!path)
        return false;

    int32 dot = strFindR(path, 0, _S".");
    int32 sep = strFindR(path, 0, fsPathSepStr);
    if (dot < clamplow(sep, 1))
        return false;

    strSubStr(out, path, dot + 1, 0);
    return true;
}

void pathSetExt(string *out, string path, string ext)
{
    pathRemoveExt(out, path);
    pathAddExt(out, *out, ext);
}

bool pathIsAbsolute(string path)
{
    // namespaced paths are always absolute
    if (strFind(path, 0, fsNSSepStr) != -1)
        return true;
    if (strGetChar(path, 0) == '/')
        return true;
    return false;
}

bool pathSplitNS(string *nspart, string *pathpart, string path)
{
    int32 idx = strFind(path, 0, fsNSSepStr);

    string rns = 0, rpath = 0;
    if (!nspart || !pathpart)
        return false;

    if (idx != -1) {
        strSubStr(&rns, path, 0, idx);
        strSubStr(&rpath, path, idx + 1, 0);
    } else {
        strDup(&rpath, path);
    }

    strDestroy(nspart);
    *nspart = rns;
    strDestroy(pathpart);
    *pathpart = rpath;
    return true;
}

static bool pathNormalized(string path)
{
    striter pi;
    striBorrow(&pi, path);
    char backbuf[5] = { 0 };
    bool nsdone = false, nscheck = false;

    while (pi.len > 0) {
        for (uint32 i = 0; i < pi.len; i++) {
            char ch = pi.bytes[i];

            if (ch == '\\')
                return false;       // backslash needs converting

            if (nscheck) {
                if (ch != '/')
                    return false;   // degenerate namespaced relative path
                nscheck = false;
            }

            if (!nsdone && ch == ':') {
                nsdone = true;
                nscheck = true;
            }

            if (ch == '/') {
                // check for parent and current directory references
                if (backbuf[4] == '.' && backbuf[3] == '.' && backbuf[2] == '/')
                    return false;
                if (backbuf[4] == '.' && backbuf[3] == '/')
                    return false;

                // and double slash
                if (backbuf[4] == '/')
                    return false;
            }

            // cheesy but fast 4-byte copy
            *(uint32*)backbuf = *(uint32*)(backbuf + 1);
            backbuf[4] = ch;
        }
        striNext(&pi);
    }

    // check for directory refs at the end
    if (backbuf[4] == '.' && backbuf[3] == '.' && backbuf[2] == '/')
        return false;
    if (backbuf[4] == '.' && backbuf[3] == '/')
        return false;

    // all good!
    return true;
}

bool pathDecompose(string *ns, string **components, string pathin)
{
    string rpath = 0;

    pathSplitNS(ns, &rpath, pathin);
    // if there are any backslashes, turn them to forward slashes
    int32 idx = 0;
    while ((idx = strFind(rpath, idx, _S"\\")) != -1)
        strSetChar(&rpath, idx, '/');

    bool absolute = pathIsAbsolute(rpath);
    strSplit(components, rpath, fsPathSepStr, true);

    for (int32 i = 0, csz = saSize(components); i < csz; i++) {
        if (i > 0 && strEmpty((*components)[i])) {
            // remove empty components, but not from position 0 (absolute path)
            saRemove(components, i--, 0);
            csz--;
        } else if (i > 0 && strEq((*components)[i], _S"..")) {
            // eat previous component
            // only allowed work in position 1 or later to avoid breaking relative paths
            saRemove(components, i--, 0);
            csz--;
            if (!absolute || i > 0) {
                // and absolute paths cannot remove the first empty component
                saRemove(components, i--, 0);
                csz--;
            }
        } else if (strEq((*components)[i], _S".")) {
            // does nothing, just delete this component
            saRemove(components, i--, 0);
            csz--;
        }
    }

    // handle degenerate case of namespace: or namespace:path with no /
    if (absolute && (saSize(components) == 0 || !strEmpty((*components)[0])))
        saInsert(components, 0, string, _S);

    strDestroy(&rpath);
    return absolute;
}

bool pathCompose(string *out, string ns, string *components)
{
    string rpath = 0;

    strJoin(&rpath, components, fsPathSepStr);
    if (saSize(&components) == 1 && strEmpty(components[0]))
        strAppend(&rpath, fsPathSepStr);                // this was absolute with only a root

    if (!strEmpty(ns))
        strNConcat(out, ns, fsNSSepStr, rpath);
    else
        strDup(out, rpath);

    strDestroy(&rpath);
    return true;
}

void pathNormalize(string *path)
{
    string nspace = 0;
    string *components = 0;

    if (!pathNormalized(*path)) {
        pathDecompose(&nspace, &components, *path);
        pathCompose(path, nspace, components);
        saDestroy(&components);
        strDestroy(&nspace);
    }
}
