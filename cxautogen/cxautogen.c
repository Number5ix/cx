#include "cxautogen.h"
#include <cx/container.h>
#include <cx/fs/file.h>
#include <cx/string.h>
#include <cx/utils.h>

sa_Interface ifaces;
hashtable ifidx;
sa_Class classes;
sa_StructDef structs;
sa_StructSetDef structsets;
sa_ClassSetDef classsets;
hashtable clsidx;
hashtable weakrefidx;
sa_string includes;
sa_string implincludes;
sa_string deps;
sa_string fwdstruct;
sa_string fwdclass;
sa_string globaldocs;
sa_string globaldocs_end;
sa_TypeNode artypes;
hashtable knownartypes;
string cpassthrough;
bool needmixinimpl;
bool usedocs;

bool upToDate(string fname);

// Escape a path for a Make-format depfile: spaces are the one character that can plausibly
// appear in a build path and must be quoted ("a b" -> "a\ b").
static void depfileEscape(string* out, string path)
{
    if (strFind(path, 0, _S" ") == -1) {
        strDup(out, path);
        return;
    }

    sa_string parts;
    saInit(&parts, string, 4);
    strSplit(&parts, path, _S" ", true);
    strJoin(out, parts, _S"\\ ");
    saDestroy(&parts);
}

// Write a Make-format dependency file (a la cc -MF) recording every .cxh that generating
// `target` consumed -- the input itself plus everything it transitively included, which is where
// a parent class's layout comes from. The build system feeds this back to ninja/make so that
// editing an included .cxh regenerates the files derived from it, not just its own outputs.
static bool writeDepfile(string depfile, string target, string input)
{
    FSFile* file = fsOpen(depfile, FS_Overwrite);
    if (!file) {
        fprintf(stderr, "Failed to open %s for writing", lazyPlatformPath(depfile));
        return false;
    }
    StreamBuffer* bf = sbufCreate(1024);
    if (!sbufFSFileCRegisterPush(bf, file, true))
        return false;
    if (!sbufPRegisterPush(bf, NULL, NULL))
        return false;

    string esc = 0;
    depfileEscape(&esc, target);
    sbufPWriteStr(bf, esc);
    sbufPWriteStr(bf, _S":");
    depfileEscape(&esc, input);
    sbufPWriteStr(bf, _S" ");
    sbufPWriteStr(bf, esc);
    for (int i = 0; i < saSize(deps); i++) {
        // an include that was already parsed earlier in the run leaves an empty entry
        if (strEmpty(deps.a[i]))
            continue;
        depfileEscape(&esc, deps.a[i]);
        sbufPWriteStr(bf, _S" ");
        sbufPWriteStr(bf, esc);
    }
    sbufPWriteEOL(bf);
    strDestroy(&esc);

    sbufPFinish(bf);
    sbufRelease(&bf);
    return true;
}

int main(int argc, char* argv[])
{
    bool force = false;
    sa_string inputfiles;
    sa_string searchpath;
    string fname     = 0;
    string srcpath   = 0;
    string binpath   = 0;
    string depfile   = 0;
    string deptarget = 0;
    string depinput  = 0;
    saInit(&ifaces, object, 16);
    htInit(&ifidx, string, object, 16);
    saInit(&classes, object, 16);
    saInit(&structs, object, 16);
    saInit(&structsets, object, 8);
    saInit(&classsets, object, 8);
    htInit(&clsidx, string, object, 16);
    htInit(&weakrefidx, string, object, 16);
    saInit(&includes, string, 8);
    saInit(&implincludes, string, 4);
    saInit(&globaldocs, string, 8);
    saInit(&globaldocs_end, string, 8);
    saInit(&deps, string, 8);
    saInit(&fwdstruct, string, 8);
    saInit(&fwdclass, string, 8);
    saInit(&artypes, object, 8);
    htInit(&knownartypes, string, bool, 16);

    saInit(&searchpath, string, 8);
    saInit(&inputfiles, string, 4);

    string tmp = 0;
    for (int i = 1; i < argc; i++) {
        strSubStr(&tmp, (string)argv[i], 0, 2);
        if (strEq(tmp, _S"-I")) {
            strSubStr(&tmp, (string)argv[i], 2, strEnd);
            pathFromPlatform(&tmp, tmp);
            saPush(&searchpath, string, tmp);
        } else if (strEq(tmp, _S"-S")) {
            strSubStr(&tmp, (string)argv[i], 2, strEnd);
            pathFromPlatform(&srcpath, tmp);
        } else if (strEq(tmp, _S"-B")) {
            strSubStr(&tmp, (string)argv[i], 2, strEnd);
            pathFromPlatform(&binpath, tmp);
        } else if (strEq(tmp, _S"-M")) {
            // write a Make-format depfile here, covering the first input file
            strSubStr(&tmp, (string)argv[i], 2, strEnd);
            pathFromPlatform(&depfile, tmp);
        } else if (strEq((string)argv[i], _S"-f")) {
            force = true;
        } else {
            pathFromPlatform(&tmp, (string)argv[i]);
            saPush(&inputfiles, string, tmp);
        }
    }
    strDestroy(&tmp);

    bool failed = false;

    for (int i = 0; i < saSize(inputfiles); i++) {
        saClear(&ifaces);
        htClear(&ifidx);
        saClear(&classes);
        saClear(&structs);
        saClear(&structsets);
        saClear(&classsets);
        htClear(&clsidx);
        htClear(&weakrefidx);
        saClear(&includes);
        saClear(&implincludes);
        saClear(&fwdstruct);
        saClear(&fwdclass);
        saClear(&artypes);
        saClear(&globaldocs);
        saClear(&globaldocs_end);
        htClear(&knownartypes);
        strDestroy(&cpassthrough);
        needmixinimpl = false;
        usedocs       = false;

        // standard interfaces should always be available, but it's non-fatal if
        // the file can't be located
        if (!strEndsWith(inputfiles.a[i], _S"objstdif.cxh")) {
            strDup(&fname, _S"cx/obj/objstdif.cxh");
            string stdifreal = 0;
            parseFile(fname, &stdifreal, NULL, searchpath, true, false);
            // it is a real input to the generation like any include, so it belongs in deps --
            // both for upToDate() and for the depfile
            if (!strEmpty(stdifreal))
                saPushC(&deps, string, &stdifreal, SA_Unique);
            strDestroy(&stdifreal);
            strClear(&fname);
        }

        if (!parseFile(inputfiles.a[i], &fname, srcpath, searchpath, false, true)) {
            failed = true;
            break;
        }

        if (strEmpty(fname))   // already parsed this file
            continue;

        // The depfile covers the first input; the build system invokes one file per command.
        // Captured before the upToDate short-circuit: the depfile must be (re)written whenever
        // the command runs, or the build tool would record the generation as having no
        // dependencies beyond the input itself.
        if (!strEmpty(depfile) && strEmpty(deptarget)) {
            pathSetExt(&deptarget, fname, _S"h");
            binPath(&deptarget, deptarget, srcpath, binpath);
            strDup(&depinput, fname);
        }

        if (!force && upToDate(fname))
            continue;

        if (!processInterfaces() || !processClasses() || !processStructs() ||
            !writeHeader(fname, srcpath, binpath) || !writeImpl(fname, srcpath, binpath, false) ||
            (needmixinimpl && !writeImpl(fname, srcpath, binpath, true))) {
            failed = true;
            break;
        }
    }

    // Written even when generation was skipped as up to date -- see the deptarget capture above.
    if (!strEmpty(deptarget))
        writeDepfile(depfile, deptarget, depinput);

    htDestroy(&ifidx);
    saDestroy(&ifaces);
    htDestroy(&clsidx);
    htDestroy(&weakrefidx);
    saDestroy(&classes);
    saDestroy(&structs);
    saDestroy(&structsets);
    saDestroy(&classsets);
    saDestroy(&implincludes);
    saDestroy(&includes);
    saDestroy(&deps);
    saDestroy(&fwdstruct);
    saDestroy(&fwdclass);
    saDestroy(&globaldocs);
    saDestroy(&globaldocs_end);
    strDestroy(&cpassthrough);
    strDestroy(&fname);
    strDestroy(&srcpath);
    strDestroy(&binpath);
    strDestroy(&depfile);
    strDestroy(&deptarget);
    strDestroy(&depinput);
    saDestroy(&searchpath);
    saDestroy(&inputfiles);

    return failed ? 1 : 0;
}

bool upToDate(string fname)
{
    bool ret     = false;
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

uint8* lazyPlatformPath(string path)
{
    string tmp = 0;
    pathToPlatform(&tmp, path);
    uint8* out = scratchGet(strLen(tmp) + 1);
    strCopyOut(tmp, 0, out, strLen(tmp) + 1);
    strDestroy(&tmp);
    return out;
}

void relSrcPath(string* out, strref fname, strref srcpath)
{
    string lfname = 0;
    strDup(&lfname, fname);

    if (strEmpty(srcpath) || !strBeginsWith(fname, srcpath))
        goto out;

    strSubStrI(&lfname, strLen(srcpath), strEnd);
    while (strGetChar(lfname, 0) == '/') strSubStrI(&lfname, 1, strEnd);

out:
    strDup(out, lfname);
    strDestroy(&lfname);
}

void binPath(string* out, strref fname, strref srcpath, strref binpath)
{
    string lfname = 0;
    strDup(&lfname, fname);

    if (strEmpty(srcpath) || strEmpty(binpath))
        goto out;

    if (!strBeginsWith(fname, srcpath))
        goto out;

    strSubStrI(&lfname, strLen(srcpath), strEnd);
    while (strGetChar(lfname, 0) == '/') strSubStrI(&lfname, 1, strEnd);
    pathJoin(&lfname, binpath, lfname);

out:
    strDup(out, lfname);
    strDestroy(&lfname);
}
