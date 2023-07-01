#include <stdio.h>
#include <cx/ssdtree.h>
#include <cx/string.h>
#include <cx/string/strtest.h>

#define TEST_FILE ssdtest
#define TEST_FUNCS ssdtest_funcs
#include "common.h"

static int test_ssd_tree()
{
    int ret = 0;
    SSDNode *tree = ssdCreateTree();

    // basic test
    stvar outvar = { 0 };
    if (ssdGetValue(tree, _S"test", &outvar, NULL) || outvar.type != 0)
        ret = 1;

    ssdSetValue(tree, _S"l1/l2/l3/test1", stvar(int32, 1920), NULL);
    if (!ssdGetValue(tree, _S"l1/l2/l3/test1", &outvar, NULL) ||
        !stEq(outvar.type, stType(int32)) ||
        outvar.data.st_int32 != 1920)
        ret = 1;

    SSDLock tlock;
    ssdLockInit(&tlock);
    stvar *pval = ssdGetPtr(tree, _S"l1/l2/l3/test1", &tlock);
    if (!pval ||
        !stEq(pval->type, stType(int32)) ||
        pval->data.st_int32 != 1920)
        ret = 1;
    ssdLockEnd(tree, &tlock);

    ssdRelease(&tree);

    // make sure copies are happening where they're supposed to
    string teststr = 0;
    strCopy(&teststr, _S"test123");
    tree = ssdCreateTree();

    ssdSetValue(tree, _S"l1/l2/l3/test2", stvar(string, teststr), NULL);
    if (strTestRefCount(teststr) != 2)
        ret = 1;

    // test getting an object node as a value
    if (!ssdGetValue(tree, _S"l1/l2/l3", &outvar, NULL) ||
        !stEq(outvar.type, stType(object)))
        ret = 1;
    stDestroy(stvar, &outvar);

    // this should get a copy of the string, increasing its ref count
    if (!ssdGetValue(tree, _S"l1/l2/l3/test2", &outvar, NULL) ||
        !stEq(outvar.type, stType(string)) ||
        !strEq(outvar.data.st_string, _S"test123") ||
        outvar.data.st_string != teststr ||
        strTestRefCount(teststr) != 3)
        ret = 1;

    stDestroy(stvar, &outvar);
    if (strTestRefCount(teststr) != 2)
        ret = 1;

    // this should get a pointer to the string, NOT increasing the ref count
    ssdLockInit(&tlock);
    pval = ssdGetPtr(tree, _S"l1/l2/l3/test2", &tlock);
    if (!stEq(pval->type, stType(string)) ||
        !strEq(pval->data.st_string, _S"test123") ||
        pval->data.st_string != teststr ||
        strTestRefCount(teststr) != 2)
        ret = 1;
    ssdLockEnd(tree, &tlock);

    ssdRelease(&tree);

    if (strTestRefCount(teststr) != 1)
        ret = 1;
    strDestroy(&teststr);

    return ret;
}

static int test_ssd_single()
{
    int ret = 0;
    SSDNode *tree = ssdCreateSingleValue(stvar(int64, 200000));

    stvar outvar = { 0 };
    if (ssdGetValue(tree, _S"test", &outvar, NULL) || outvar.type != 0)
        ret = 1;

    if (!tree->singleval)
        ret = 1;

    if (!ssdGetValue(tree, _S"", &outvar, NULL) ||
        !stEq(outvar.type, stType(int64)) ||
        outvar.data.st_int32 != 200000)
        ret = 1;

    ssdRelease(&tree);

    string teststr = 0;
    strCopy(&teststr, _S"test123");
    tree = ssdCreateSingleValue(stvar(string, teststr));

    if (strTestRefCount(teststr) != 2)
        ret = 1;

    outvar = (stvar){0};
    if (ssdGetValue(tree, _S"test", &outvar, NULL) || outvar.type != 0)
        ret = 1;

    if (!tree->singleval)
        ret = 1;

    if (!ssdGetValue(tree, _S"", &outvar, NULL) ||
        !stEq(outvar.type, stType(string)) ||
        !strEq(outvar.data.st_string, _S"test123") ||
        strTestRefCount(teststr) != 3)
        ret = 1;

    stDestroy(stvar, &outvar);

    ssdRelease(&tree);

    if (strTestRefCount(teststr) != 1)
        ret = 1;

    strDestroy(&teststr);

    return ret;
}

static int test_ssd_subtree()
{
    int ret = 0;
    SSDNode *tree = ssdCreateTree();

    stvar outvar = { 0 };
    string teststr = 0;
    strCopy(&teststr, _S"test123");

    ssdSetValue(tree, _S"l1/b1/l3/test1", stvar(int32, 10743), NULL);
    ssdSetValue(tree, _S"l1/b1/l3/test2", stvar(string, teststr), NULL);
    ssdSetValue(tree, _S"l1/b2/l3/test1", stvar(int32, 39294), NULL);
    ssdSetValue(tree, _S"l1/b2/l3/test2", stvar(string, teststr), NULL);

    if (strTestRefCount(teststr) != 3)
        ret = 1;

    SSDNode *subtree = ssdGetSubtree(tree, _S"l1/b2", false, NULL);
    if (!subtree) {
        ret = 1;
        goto out;
    }

    if (!ssdGetValue(subtree, _S"l3/test1", &outvar, NULL) ||
        !stEq(outvar.type, stType(int32)) ||
        outvar.data.st_int32 != 39294)
        ret = 1;

    if (!ssdGetValue(subtree, _S"l3/test2", &outvar, NULL) ||
        !stEq(outvar.type, stType(string)) ||
        !strEq(outvar.data.st_string, _S"test123") ||
        outvar.data.st_string != teststr ||
        strTestRefCount(teststr) != 4)
        ret = 1;

    stDestroy(stvar, &outvar);
    if (strTestRefCount(teststr) != 3)
        ret = 1;

    // releasing the main tree shouldn't affect the subtree
    ssdRelease(&tree);

    if (!ssdGetValue(subtree, _S"l3/test1", &outvar, NULL) ||
        !stEq(outvar.type, stType(int32)) ||
        outvar.data.st_int32 != 39294)
        ret = 1;

    // but it should drop the refcount of the string when the other branch was destroyed
    if (!ssdGetValue(subtree, _S"l3/test2", &outvar, NULL) ||
        !stEq(outvar.type, stType(string)) ||
        !strEq(outvar.data.st_string, _S"test123") ||
        outvar.data.st_string != teststr ||
        strTestRefCount(teststr) != 3)
        ret = 1;

    stDestroy(stvar, &outvar);
    if (strTestRefCount(teststr) != 2)
        ret = 1;

out:
    ssdRelease(&subtree);
    ssdRelease(&tree);

    if (strTestRefCount(teststr) != 1)
        ret = 1;
    strDestroy(&teststr);

    return ret;
}

testfunc ssdtest_funcs[] = {
    { "tree", test_ssd_tree },
    { "single", test_ssd_single },
    { "subtree", test_ssd_subtree },
    { 0, 0 }
};
