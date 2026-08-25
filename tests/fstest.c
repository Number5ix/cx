#include <stdio.h>
#include <cx/fs.h>
#include <cx/string.h>

#define TEST_FILE fstest
#define TEST_FUNCS fstest_funcs
#include "common.h"

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

testfunc fstest_funcs[] = {
    { "pathmatch", test_fs_pathmatch },
    { 0, 0 }
};
