#include <cx/container.h>
#include <cx/cx.h>
#include <cx/debug/error.h>
#include <cx/string.h>
#include <cx/sys.h>

#define TEST_FILE proctest
#define TEST_FUNCS proctest_funcs
#include "common.h"

// Spelled as _SL() literals rather than STR_CONST, which declares a const strref that a
// hashtable key argument will not take.
#define kEnvVar  _SL("CX_PROCTEST_VAR")
#define kEnvVal1 _SL("value one")
#define kEnvVal2 _SL("value two")

// The same name in two spellings, for pinning down each platform's name matching.
#define kEnvMixed _SL("CX_ProcTest_Case")
#define kEnvUpper _SL("CX_PROCTEST_CASE")

// Checks that a variable reads back with the value it was given.
static void checkEnvVal(int* ret, strref name, strref want)
{
    string got = 0;

    if (!envGet(&got, name)) {
        TEST_FAILV(*ret, 1, _SL("envGet('${string}') reports the variable is not set"),
                   stvar(strref, name));
    } else if (!strEq(got, want)) {
        TEST_FAILV(*ret, 1, _SL("envGet('${string}') == '${string}', wanted '${string}'"),
                   stvar(strref, name), stvar(string, got), stvar(strref, want));
    }

    strDestroy(&got);
}

static int test_proc_env_getset(void)
{
    int ret = 0;

    if (!envSet(kEnvVar, kEnvVal1)) {
        TEST_FAILV(ret, 1, _SL("envSet('${string}', '${string}') failed"), stvar(strref, kEnvVar),
                   stvar(strref, kEnvVal1));
    } else {
        checkEnvVal(&ret, kEnvVar, kEnvVal1);
    }

    // Setting an existing variable replaces its value rather than adding a second one.
    if (!envSet(kEnvVar, kEnvVal2)) {
        TEST_FAILV(ret, 1, _SL("envSet('${string}', '${string}') failed overwriting"),
                   stvar(strref, kEnvVar), stvar(strref, kEnvVal2));
    } else {
        checkEnvVal(&ret, kEnvVar, kEnvVal2);
    }

    if (!envExists(kEnvVar))
        TEST_FAILV(ret, 1, _SL("envExists('${string}') is false for a variable that is set"),
                   stvar(strref, kEnvVar));

    // A variable set to an empty value still exists -- that is the case envUnset is separate for.
    if (!envSet(kEnvVar, _SL(""))) {
        TEST_FAILV(ret, 1, _SL("envSet('${string}') failed with an empty value"),
                   stvar(strref, kEnvVar));
    } else {
        checkEnvVal(&ret, kEnvVar, _SL(""));

        if (!envExists(kEnvVar))
            TEST_FAILV(ret, 1, _SL("envExists('${string}') is false for a variable set to empty"),
                       stvar(strref, kEnvVar));
    }

    envUnset(kEnvVar);
    return ret;
}

static int test_proc_env_unset(void)
{
    int ret    = 0;
    string val = 0;

    if (!envSet(kEnvVar, kEnvVal1))
        TEST_FAILV(ret, 1, _SL("envSet('${string}') failed"), stvar(strref, kEnvVar));

    if (!envExists(kEnvVar))
        TEST_FAILV(ret, 1, _SL("envExists('${string}') is false right after envSet"),
                   stvar(strref, kEnvVar));

    if (!envUnset(kEnvVar))
        TEST_FAILV(ret, 1, _SL("envUnset('${string}') failed"), stvar(strref, kEnvVar));

    if (envExists(kEnvVar))
        TEST_FAILV(ret, 1, _SL("envExists('${string}') is still true after envUnset"),
                   stvar(strref, kEnvVar));

    // A lookup that finds nothing has to clear the output, not leave the caller's old value.
    strDup(&val, kEnvVal1);
    if (envGet(&val, kEnvVar)) {
        TEST_FAILV(ret, 1, _SL("envGet('${string}') found a value after envUnset"),
                   stvar(strref, kEnvVar));
    } else if (strLen(val) != 0) {
        TEST_FAILV(ret, 1, _SL("envGet left '${string}' in the output after finding nothing"),
                   stvar(string, val));
    }

    strDestroy(&val);
    return ret;
}

static int test_proc_env_enum(void)
{
    int ret = 0;
    hashtable env;
    string val = 0;

    if (!envSet(kEnvVar, kEnvVal1))
        TEST_FAILV(ret, 1, _SL("envSet('${string}') failed"), stvar(strref, kEnvVar));

    if (!envEnum(&env)) {
        envUnset(kEnvVar);
        TEST_FAIL(1, _SL("envEnum failed, cxerr ${int}"), stvar(int32, cxerr));
    }

    if (htSize(env) == 0)
        TEST_FAILV(ret, 1, _SL("envEnum returned an empty table, though '${string}' is set"),
                   stvar(strref, kEnvVar));

    if (!htFind(env, strref, kEnvVar, string, &val)) {
        TEST_FAILV(ret, 1, _SL("envEnum's table has no '${string}', though it is set"),
                   stvar(strref, kEnvVar));
    } else if (!strEq(val, kEnvVal1)) {
        TEST_FAILV(ret, 1, _SL("envEnum gave '${string}' for '${string}', wanted '${string}'"),
                   stvar(string, val), stvar(strref, kEnvVar), stvar(strref, kEnvVal1));
    }

    strDestroy(&val);
    htDestroy(&env);
    envUnset(kEnvVar);
    return ret;
}

// Pins down each platform's name matching rather than assuming it: Windows treats the two
// spellings as one variable, Unix treats them as two.
static int test_proc_env_caseinsens(void)
{
    int ret    = 0;
    string val = 0;

    if (!envSet(kEnvMixed, kEnvVal1))
        TEST_FAILV(ret, 1, _SL("envSet('${string}') failed"), stvar(strref, kEnvMixed));

    bool found = envGet(&val, kEnvUpper);

#if defined(_PLATFORM_WIN)
    if (!found) {
        TEST_FAILV(ret, 1, _SL("envGet('${string}') found nothing; Windows should match '${string}'"),
                   stvar(strref, kEnvUpper), stvar(strref, kEnvMixed));
    } else if (!strEq(val, kEnvVal1)) {
        TEST_FAILV(ret, 1, _SL("envGet('${string}') == '${string}', wanted '${string}'"),
                   stvar(strref, kEnvUpper), stvar(string, val), stvar(strref, kEnvVal1));
    }
#else
    if (found)
        TEST_FAILV(ret, 1, _SL("envGet('${string}') == '${string}'; Unix should not match '${string}'"),
                   stvar(strref, kEnvUpper), stvar(string, val), stvar(strref, kEnvMixed));
#endif

    strDestroy(&val);
    envUnset(kEnvMixed);
    return ret;
}

// Runs the environment subtests in one process, so ctest spends one process launch on the group
// rather than one on each. Every subtest stays registered below for running one in isolation.
int test_proc_grp_env(void)
{
    TEST_CHAIN(test_proc_env_getset, test_proc_env_unset, test_proc_env_enum,
               test_proc_env_caseinsens);
}

testfunc proctest_funcs[] = {
    { "env_getset",     test_proc_env_getset     },
    { "env_unset",      test_proc_env_unset      },
    { "env_enum",       test_proc_env_enum       },
    { "env_caseinsens", test_proc_env_caseinsens },
    { "grp_env",        test_proc_grp_env        },
    { 0,                0                        }
};
