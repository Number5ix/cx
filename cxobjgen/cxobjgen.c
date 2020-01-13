#include "cxobjgen.h"
#include <cx/container.h>
#include <cx/string.h>
#include <cx/utils.h>

Interface **ifaces;
hashtable ifidx;
Class **classes;
hashtable clsidx;
string *includes;
string *implincludes;
string *deps;
string *structs;
bool needmixinimpl;

bool upToDate(string fname);

int main(int argc, char *argv[])
{
    bool force = false;
    string *sidlfiles;
    string *searchpath;
    string fname = 0;
    ifaces = saCreate(object, 16);
    ifidx = htCreate(string, object, 16);
    classes = saCreate(object, 16);
    clsidx = htCreate(string, object, 16);
    includes = saCreate(string, 8);
    implincludes = saCreate(string, 4);
    deps = saCreate(string, 8);
    structs = saCreate(string, 8);

    searchpath = saCreate(string, 8);
    sidlfiles = saCreate(string, 4);

    string tmp = 0;
    for (int i = 1; i < argc; i++) {
        strSubStr(&tmp, argv[i], 0, 2);
        if (strEq(tmp, _S"-I")) {
            strSubStr(&tmp, argv[i], 2, strEnd);
            pathFromPlatform(&tmp, tmp);
            saPush(&searchpath, string, tmp);
        } else if (strEq(argv[i], _S"-f")) {
            force = true;
        } else {
            pathFromPlatform(&tmp, argv[i]);
            saPush(&sidlfiles, string, tmp);
        }
    }
    strDestroy(&tmp);

    for (int i = 0; i < saSize(&sidlfiles); i++) {
        saClear(&ifaces);
        htClear(&ifidx);
        saClear(&classes);
        htClear(&clsidx);
        saClear(&includes);
        saClear(&implincludes);
        saClear(&structs);
        needmixinimpl = false;

        // standard interfaces should always be available, but it's non-fatal if
        // the file can't be located
        strDup(&fname, _S"cx/core/objstdif.sidl");
        parseFile(fname, NULL, searchpath, true, false);
        strClear(&fname);

        if (!parseFile(sidlfiles[i], &fname, searchpath, false, true))
            break;

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
    for (int i = 0; i < saSize(&deps); i++) {
        if (fsStat(deps[i], &statv))
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
