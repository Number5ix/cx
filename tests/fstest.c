#include <stdio.h>
#include <cx/fs.h>
#include <cx/obj.h>
#include <cx/serialize.h>
#include <cx/string.h>
#include <cx/time/clock.h>
#include <cx/time/time.h>

#define TEST_FILE fstest
#define TEST_FUNCS fstest_funcs
#include "common.h"

// Checks fsStat's verdict on a path.
static void checkFsStat(int* ret, strref path, int want)
{
    int got = fsStat(path, NULL);
    if (got != want) {
        TEST_FAILV(*ret, 1, _SL("fsStat('${string}') == ${int}, wanted ${int}"),
                   stvar(strref, path), stvar(int32, got), stvar(int32, want));
    }
}

// Checks pathMatch(path, pattern, flags) against the expected result, logging all inputs and the
// actual result on mismatch.
static void checkPathMatch(int *ret, strref path, strref pattern, flags_t flags, bool want)
{
    bool got = pathMatch(path, pattern, flags);
    if (got != want)
        TEST_FAILV(*ret, 1, _SL("pathMatch('${string}', '${string}', flags=${uint})=${int} (want ${int})"),
                   stvar(strref, path), stvar(strref, pattern), stvar(uint32, flags), stvar(int32, (int32)got), stvar(int32, (int32)want));
}

static int test_fs_pathmatch()
{
    int ret = 0;
    string tpath1 = 0;
    string tpath2 = 0;
    string tpath3 = 0;
    string tpath4 = 0;

    strCopy(&tpath1, _S"/abs/path/to/dir1/file7.txt");
    strCopy(&tpath2, _S"/abs/other/some/dir2/file99.bin");
    strCopy(&tpath3, _S"rel/path/to/dir1/file7.txt");
    strCopy(&tpath4, _S"rel/other/some/dir2/file99.bin");

    // basic path matching
    checkPathMatch(&ret, tpath1, _S"/abs", 0, false);
    checkPathMatch(&ret, tpath1, _S"/abs", PATH_LeadingDir, true);

    checkPathMatch(&ret, tpath3, _S"/rel", 0, false);
    checkPathMatch(&ret, tpath3, _S"/rel", PATH_LeadingDir, false);

    checkPathMatch(&ret, tpath1, _S"abs", 0, false);
    checkPathMatch(&ret, tpath1, _S"abs", PATH_LeadingDir, false);

    checkPathMatch(&ret, tpath3, _S"rel", 0, false);
    checkPathMatch(&ret, tpath3, _S"rel", PATH_LeadingDir, true);

    // longer paths
    checkPathMatch(&ret, tpath1, _S"/abs/path/to", 0, false);
    checkPathMatch(&ret, tpath1, _S"/abs/path/to", PATH_LeadingDir, true);
    checkPathMatch(&ret, tpath1, _S"/abs/path/to/dir1", 0, false);
    checkPathMatch(&ret, tpath1, _S"/abs/path/to/dir1", PATH_LeadingDir, true);
    checkPathMatch(&ret, tpath1, _S"/abs/path/to/dir1/file*", 0, true);
    checkPathMatch(&ret, tpath1, _S"/abs/path/to/dir1/file*", PATH_LeadingDir, true);

    // wildcards
    checkPathMatch(&ret, tpath1, _S"/abs/path/to/di*/file8.txt", 0, false);
    checkPathMatch(&ret, tpath1, _S"/abs/path/to/di*/file7.txt", 0, true);
    checkPathMatch(&ret, tpath1, _S"/abs/path/p?/dir1/file7.txt", 0, false);
    checkPathMatch(&ret, tpath1, _S"/abs/path/t?/dir1/file7.txt", 0, true);
    checkPathMatch(&ret, tpath1, _S"/abs/p?h/to/dir1/file7.txt", 0, false);
    checkPathMatch(&ret, tpath1, _S"/abs/p*h/to/dir1/file7.txt", 0, true);
    checkPathMatch(&ret, tpath1, _S"/???\?/p*h/to/dir1/file7.txt", 0, false);
    checkPathMatch(&ret, tpath1, _S"/??\?/p*h/to/dir1/file7.txt", 0, true);

    // ignoring paths
    checkPathMatch(&ret, tpath1, _S"/abs/*/file7.txt", 0, false);
    checkPathMatch(&ret, tpath1, _S"/abs/*/file7.txt", PATH_IgnorePath, true);
    checkPathMatch(&ret, tpath2, _S"*ile99.bin", 0, false);
    checkPathMatch(&ret, tpath2, _S"*ile99.bin", PATH_IgnorePath, true);
    checkPathMatch(&ret, tpath3, _S"*/file7.txt", 0, false);
    checkPathMatch(&ret, tpath3, _S"*/file7.txt", PATH_IgnorePath, true);
    checkPathMatch(&ret, tpath4, _S"rel/*", 0, false);
    checkPathMatch(&ret, tpath4, _S"rel/*", PATH_IgnorePath, true);

    // case insensitive
    checkPathMatch(&ret, tpath2, _S"/abs/other/some/dir2/File99.bin", 0, false);
    checkPathMatch(&ret, tpath2, _S"/abs/other/some/dir2/File99.bin", PATH_CaseInsensitive, true);
    checkPathMatch(&ret, tpath4, _S"REL/other/some/dir2/file99.bin", 0, false);
    checkPathMatch(&ret, tpath4, _S"REL/other/some/dir2/file99.bin", PATH_CaseInsensitive, true);

    // smart mode
    checkPathMatch(&ret, tpath1, _S"/abs/path", 0, false);
    checkPathMatch(&ret, tpath1, _S"/abs/path", PATH_Smart, true);
    checkPathMatch(&ret, tpath2, _S"/abs/other/s??\?/dir2", 0, false);
    checkPathMatch(&ret, tpath2, _S"/abs/other/s??\?/dir2", PATH_Smart, true);
    checkPathMatch(&ret, tpath3, _S"/rel/path", 0, false);
    checkPathMatch(&ret, tpath3, _S"/rel/path", PATH_Smart, true);
    checkPathMatch(&ret, tpath4, _S"/*/*/*/*/file99.bin", 0, false);
    checkPathMatch(&ret, tpath4, _S"/*/*/*/*/file99.bin", PATH_Smart, true);

    checkPathMatch(&ret, tpath1, _S"file7.txt", 0, false);
    checkPathMatch(&ret, tpath1, _S"file7.txt", PATH_Smart, true);
    checkPathMatch(&ret, tpath2, _S"*.bin", 0, false);
    checkPathMatch(&ret, tpath2, _S"*.bin", PATH_Smart, true);
    checkPathMatch(&ret, tpath3, _S"????7.??t", 0, false);
    checkPathMatch(&ret, tpath3, _S"????7.??t", PATH_Smart, true);
    checkPathMatch(&ret, tpath4, _S"*99*", 0, false);
    checkPathMatch(&ret, tpath4, _S"*2*", PATH_Smart, false);
    checkPathMatch(&ret, tpath4, _S"*99*", PATH_Smart, true);

    strDestroy(&tpath1);
    strDestroy(&tpath2);
    strDestroy(&tpath3);
    strDestroy(&tpath4);
    return ret;
}

#define FSTEST_FILE_NAME "cx_fstest_file.tmp"
STR_CONST(fstestFileContents, "the quick brown fox");

// An open file is a File object whichever layer opened it, so the fs* names, the file* names and
// anything taking a File* all have to work on the same handle -- and the handle has two ways to
// die: fsClose(), which closes and releases in one go, and a bare objRelease() of the last
// reference, which has to close it on the way out.
static int test_fs_file()
{
    int ret         = 0;
    FSFile* f       = NULL;
    FSFile* f2      = NULL;
    StreamBuffer* sb = NULL;
    string readback = 0;
    uint8 buf[64];
    size_t n = 0;

    f = fsOpen(_SL(FSTEST_FILE_NAME), FS_Overwrite);
    if (!f) {
        TEST_FAILV(ret, 1, _SL("!fsOpen(_SL(\"" FSTEST_FILE_NAME "\"), FS_Overwrite)"), stvNone);
        goto out;
    }

    size_t wrote = 0;
    if (!fsWriteString(f, fstestFileContents, &wrote) || wrote != strLen(fstestFileContents))
        TEST_FAILV(ret, 1, _SL("fsWriteString wrote ${uint} of ${uint} bytes"), stvar(uint32, (uint32)wrote), stvar(uint32, strLen(fstestFileContents)));

    if (fsTell(f) != (int64)strLen(fstestFileContents))
        TEST_FAILV(ret, 1, _SL("fsTell()=${int64} after writing ${uint} bytes"), stvar(int64, fsTell(f)), stvar(uint32, strLen(fstestFileContents)));

    // fsClose closes and drops the caller's reference in one call, as it always has
    if (!fsClose(f))
        TEST_FAILV(ret, 1, _SL("!fsClose(f)"), stvNone);
    f = NULL;

    // The same handle reached through the file* names, and through a second reference
    f = fsOpen(_SL(FSTEST_FILE_NAME), FS_Read);
    if (!f) {
        TEST_FAILV(ret, 1, _SL("!fsOpen(_SL(\"" FSTEST_FILE_NAME "\"), FS_Read)"), stvNone);
        goto out;
    }
    f2 = objAcquire(f);

    if (!fileRead(f, buf, sizeof(buf), &n) || n != strLen(fstestFileContents))
        TEST_FAILV(ret, 1, _SL("fileRead read ${uint} of ${uint} bytes"), stvar(uint32, (uint32)n), stvar(uint32, strLen(fstestFileContents)));
    strFromBytes(&readback, buf, (uint32)n);
    if (!strEq(readback, fstestFileContents))
        TEST_FAILV(ret, 1, _SL("read back '${string}', expected '${string}'"), stvar(strref, readback), stvar(strref, fstestFileContents));

    if (fileSeek(f2, 0, FS_Set) != 0)
        TEST_FAILV(ret, 1, _SL("fileSeek(f2, 0, FS_Set)=${int64}, expected 0"), stvar(int64, fileSeek(f2, 0, FS_Set)));

    // Closing leaves both references valid; it is the reads that stop working
    if (!fileClose(f))
        TEST_FAILV(ret, 1, _SL("!fileClose(f)"), stvNone);
    if (!fileClose(f))
        TEST_FAILV(ret, 1, _SL("closing an already closed file was not harmless"), stvNone);

    // an unset handle is closeable, the way an error path would find it
    File* neverOpened = NULL;
    if (fileClose(neverOpened))
        TEST_FAILV(ret, 1, _SL("fileClose(NULL) did not report failure"), stvNone);
    if (fileRead(f2, buf, sizeof(buf), &n))
        TEST_FAILV(ret, 1, _SL("fileRead succeeded on a closed file"), stvNone);

    objRelease(&f2);
    objRelease(&f);

    // A handle that is only released, never closed: the destructor has to close the OS handle
    f = fsOpen(_SL(FSTEST_FILE_NAME), FS_Read);
    if (!f) {
        TEST_FAILV(ret, 1, _SL("!fsOpen(_SL(\"" FSTEST_FILE_NAME "\"), FS_Read)"), stvNone);
        goto out;
    }
    objRelease(&f);

    // A file from fsOpen goes into the stream buffer adapters, which take any File
    f = fsOpen(_SL(FSTEST_FILE_NAME), FS_Read);
    if (!f) {
        TEST_FAILV(ret, 1, _SL("!fsOpen(_SL(\"" FSTEST_FILE_NAME "\"), FS_Read)"), stvNone);
        goto out;
    }
    strClear(&readback);
    sb = sbufCreate(16);
    if (!sbufStrCRegisterPush(sb, &readback)) {
        TEST_FAILV(ret, 1, _SL("!sbufStrCRegisterPush(sb, &readback)"), stvNone);
        fsClose(f);
        f = NULL;
        goto out;
    }
    if (!sbufFileIn(sb, f, true))   // closes and releases the file
        TEST_FAILV(ret, 1, _SL("!sbufFileIn(sb, f, true)"), stvNone);
    f = NULL;
    sbufClose(sb);

    if (!strEq(readback, fstestFileContents))
        TEST_FAILV(ret, 1, _SL("streamed back '${string}', expected '${string}'"), stvar(strref, readback), stvar(strref, fstestFileContents));

out:
    if (sb)
        sbufRelease(&sb);
    objRelease(&f2);
    objRelease(&f);
    strDestroy(&readback);
    fsDelete(_SL(FSTEST_FILE_NAME));
    return ret;
}

// Native fs.h directory and search operations against the real OS filesystem: fsCreateDir /
// fsCreateAll, fsRemoveDir (including its real-filesystem "must be empty" failure, which the
// in-memory VFS test provider does not model), fsRename, fsSetTimes, and a directory search.
static int test_fs_ops()
{
    int ret = 0;
    string cwd = 0, dir = 0, nest1 = 0, nest2 = 0, sub = 0, file1 = 0, file2 = 0, renamed = 0;
    FSFile* fh = NULL;
    FSSearchIter iter;
    sa_string names;

    saInit(&names, string, 4, SA_Sorted);

    fsCurDir(&cwd);
    pathJoin(&dir, cwd, _S"cx_fstest_ops");
    pathJoin(&nest1, dir, _S"nest1");
    pathJoin(&nest2, nest1, _S"nest2");
    pathJoin(&sub, dir, _S"sub");
    pathJoin(&file1, dir, _S"one.txt");
    pathJoin(&file2, sub, _S"two.txt");
    pathJoin(&renamed, dir, _S"renamed.txt");

    if (!fsCreateDir(dir)) {
        TEST_FAILV(ret, 1, _SL("fsCreateDir('${string}') failed"), stvar(strref, dir));
        goto out;
    }
    checkFsStat(&ret, dir, FS_Directory);

    // fsCreateAll makes every missing parent along the way
    if (!fsCreateAll(nest2))
        TEST_FAILV(ret, 1, _SL("fsCreateAll('${string}') failed"), stvar(strref, nest2));
    checkFsStat(&ret, nest1, FS_Directory);
    checkFsStat(&ret, nest2, FS_Directory);

    if (!fsCreateDir(sub))
        TEST_FAILV(ret, 1, _SL("fsCreateDir('${string}') failed"), stvar(strref, sub));

    fh = fsOpen(file2, FS_Overwrite);
    if (!fh) {
        TEST_FAILV(ret, 1, _SL("could not create '${string}'"), stvar(strref, file2));
    } else {
        fsWrite(fh, "x", 1, NULL);
        fsClose(fh);
        fh = NULL;
    }

    // a non-empty directory cannot be removed -- real OS semantics the in-memory VFS test
    // provider does not model
    if (fsRemoveDir(sub))
        TEST_FAILV(ret, 1, _SL("fsRemoveDir succeeded on a non-empty directory"), stvNone);
    checkFsStat(&ret, sub, FS_Directory);

    fsDelete(file2);
    if (!fsRemoveDir(sub))
        TEST_FAILV(ret, 1, _SL("fsRemoveDir('${string}') failed once empty"), stvar(strref, sub));
    checkFsStat(&ret, sub, FS_Nonexistent);

    // rename
    fh = fsOpen(file1, FS_Overwrite);
    if (!fh) {
        TEST_FAILV(ret, 1, _SL("could not create '${string}'"), stvar(strref, file1));
    } else {
        fsWrite(fh, "y", 1, NULL);
        fsClose(fh);
        fh = NULL;
    }
    if (!fsRename(file1, renamed)) {
        TEST_FAILV(ret, 1, _SL("fsRename('${string}', '${string}') failed"), stvar(strref, file1),
                   stvar(strref, renamed));
    }
    checkFsStat(&ret, renamed, FS_File);
    checkFsStat(&ret, file1, FS_Nonexistent);

    // fsSetTimes, verified via a following fsStat. Set a time in the future, rounded to the
    // second: fsStat reports max(mtime, ctime) (see unix_fs_fs.c), and setting times always
    // bumps ctime to "now" as a side effect, so a modified time in the past would be masked by
    // that same call's own ctime bump. A couple of seconds of slack allows for filesystems that
    // don't store full microsecond precision.
    {
        FSStat stat = { 0 };
        int64 want  = (clockWall() + timeS(3600)) / timeS(1) * timeS(1);
        if (!fsSetTimes(renamed, want, want))
            TEST_FAILV(ret, 1, _SL("fsSetTimes('${string}') failed"), stvar(strref, renamed));
        fsStat(renamed, &stat);
        int64 diff = stat.modified - want;
        if (diff < 0)
            diff = -diff;
        if (diff > timeS(2)) {
            TEST_FAILV(ret, 1, _SL("fsStat().modified == ${int} after fsSetTimes(${int})"),
                       stvar(int64, stat.modified), stvar(int64, want));
        }
    }

    // search over a small fixed set of entries: nest1 and renamed.txt are all that's left
    if (!fsSearchInit(&iter, dir, NULL, false)) {
        TEST_FAILV(ret, 1, _SL("fsSearchInit('${string}') failed"), stvar(strref, dir));
    } else {
        while (fsSearchValid(&iter)) {
            saPush(&names, string, iter.name);
            fsSearchNext(&iter);
        }
    }
    fsSearchFinish(&iter);

    {
        string joined = 0;
        strJoin(&joined, names, _S",");
        if (!strEq(joined, _S"nest1,renamed.txt")) {
            TEST_FAILV(ret, 1, _SL("directory listing == '${string}', wanted 'nest1,renamed.txt'"),
                       stvar(strref, joined));
        }
        strDestroy(&joined);
    }

out:
    if (fh)
        fsClose(fh);
    fsDelete(renamed);
    fsDelete(file1);
    fsDelete(file2);
    fsRemoveDir(sub);
    fsRemoveDir(nest2);
    fsRemoveDir(nest1);
    fsRemoveDir(dir);

    saDestroy(&names);
    strDestroy(&cwd);
    strDestroy(&dir);
    strDestroy(&nest1);
    strDestroy(&nest2);
    strDestroy(&sub);
    strDestroy(&file1);
    strDestroy(&file2);
    strDestroy(&renamed);
    return ret;
}

testfunc fstest_funcs[] = {
    { "pathmatch", test_fs_pathmatch },
    { "file", test_fs_file },
    { "ops", test_fs_ops },
    { 0, 0 }
};
