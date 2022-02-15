#include "cxobjgen.h"
#include <cx/container.h>
#include <cx/string.h>
#include <cx/utils.h>

sa_Interface ifaces;
hashtable ifidx;
sa_Class classes;
hashtable clsidx;
sa_string includes;
sa_string implincludes;
sa_string deps;
sa_string structs;
sa_ComplexArrayType artypes;
hashtable knownartypes;
string cpassthrough;
bool needmixinimpl;

bool upToDate(string fname);

int main(int argc, char *argv[])
{
    bool force = false;
    sa_string sidlfiles;
    sa_string searchpath;
    string fname = 0;
    saInit(&ifaces, object, 16, 0);
    htInit(&ifidx, string, object, 16, 0);
    saInit(&classes, object, 16, 0);
    htInit(&clsidx, string, object, 16, 0);
    saInit(&includes, string, 8, 0);
    saInit(&implincludes, string, 4, 0);
    saInit(&deps, string, 8, 0);
    saInit(&structs, string, 8, 0);
    saInit(&artypes, object, 8, 0);
    htInit(&knownartypes, string, bool, 16, 0);

    saInit(&searchpath, string, 8, 0);
    saInit(&sidlfiles, string, 4, 0);

    string tmp = 0;
    for (int i = 1; i < argc; i++) {
        strSubStr(&tmp, (string)argv[i], 0, 2);
        if (strEq(tmp, _S"-I")) {
            strSubStr(&tmp, (string)argv[i], 2, strEnd);
            pathFromPlatform(&tmp, tmp);
            saPush(&searchpath, string, tmp, 0);
        } else if (strEq((string)argv[i], _S"-f")) {
            force = true;
        } else {
            pathFromPlatform(&tmp, (string)argv[i]);
            saPush(&sidlfiles, string, tmp, 0);
        }
    }
    strDestroy(&tmp);

    for (int i = 0; i < saSize(sidlfiles); i++) {
        saClear(&ifaces);
        htClear(&ifidx);
        saClear(&classes);
        htClear(&clsidx);
        saClear(&includes);
        saClear(&implincludes);
        saClear(&structs);
        saClear(&artypes);
        htClear(&knownartypes);
        strDestroy(&cpassthrough);
        needmixinimpl = false;

        // standard interfaces should always be available, but it's non-fatal if
        // the file can't be located
        if (!strEndsWith(sidlfiles.a[i], _S"objstdif.sidl")) {
            strDup(&fname, _S"cx/core/objstdif.sidl");
            parseFile(fname, NULL, searchpath, true, false);
            strClear(&fname);
        }

        if (!parseFile(sidlfiles.a[i], &fname, searchpath, false, true))
            break;

        if (strEmpty(fname))        // already parsed this file
            continue;

        if (!force && upToDate(fname))
            continue;

        if (!processInterfaces())
            break;
        if (!processClasses())
            break;
        if (!writeHeader(fname))
            break;
        if (!writeImpl(fname, false))
            break;
        if (needmixinimpl && !writeImpl(fname, true))
            break;
    }

    htDestroy(&ifidx);
    saDestroy(&ifaces);
    htDestroy(&clsidx);
    saDestroy(&classes);
    saDestroy(&implincludes);
    saDestroy(&includes);
    saDestroy(&deps);
    saDestroy(&structs);
    strDestroy(&cpassthrough);
    strDestroy(&fname);
    saDestroy(&searchpath);
    saDestroy(&sidlfiles);

    return 0;
}

bool upToDate(string fname)
{
    bool ret = false;
    string hname = 0;
    pathSetExt(&hname, fname, _S"h");
    string cname = 0;
    pathSetExt(&cname, fname, _S"c");

    // get the oldest timestamp of the generated .c and .h files
    FSStat statv;
    if (fsStat(hname, &statv) == FS_Nonexistent)
        goto out;
    int64 oldestgen = statv.modified;
    if (fsStat(cname, &statv) == FS_Nonexistent)
        goto out;
    oldestgen = min(oldestgen, statv.modified);

    // get the newest timestamp of the input file and all includes
    fsStat(fname, &statv);
    int64 newestsrc = statv.modified;
    for (int i = 0; i < saSize(deps); i++) {
        if (fsStat(deps.a[i], &statv))
            newestsrc = max(newestsrc, statv.modified);
    }

    ret = newestsrc <= oldestgen;

out:
    strDestroy(&hname);
    strDestroy(&cname);
    return ret;
}

char *lazyPlatformPath(string path)
{
    string tmp = 0;
    pathToPlatform(&tmp, path);
    char *out = scratchGet(strLen(tmp) + 1);
    strCopyOut(tmp, 0, out, strLen(tmp) + 1);
    strDestroy(&tmp);
    return out;
}
