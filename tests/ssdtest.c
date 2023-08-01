#include <stdio.h>
#include <cx/ssdtree.h>
#include <cx/string.h>
#include <cx/string/strtest.h>

#include <cx/ssdtree/node/ssdarraynode.h>
#include <cx/ssdtree/node/ssdhashnode.h>

#define TEST_FILE ssdtest
#define TEST_FUNCS ssdtest_funcs
#include "common.h"

static int test_ssd_tree()
{
    int ret = 0;
    SSDNode *tree = ssdCreateHashtable();

    // basic test
    stvar outvar = { 0 };
    if (ssdGet(tree, _S"test", &outvar, NULL) || outvar.type != 0)
        ret = 1;

    ssdSet(tree, _S"l1/l2/l3/test1", true, stvar(int32, 1920), NULL);
    if (!ssdGet(tree, _S"l1/l2/l3/test1", &outvar, NULL) ||
        !stEq(outvar.type, stType(int32)) ||
        outvar.data.st_int32 != 1920)
        ret = 1;

    SSDLock tlock;
    ssdLockInit(&tlock);
    stvar *pval = ssdPtr(tree, _S"l1/l2/l3/test1", &tlock);
    if (!pval ||
        !stEq(pval->type, stType(int32)) ||
        pval->data.st_int32 != 1920)
        ret = 1;
    ssdLockEnd(tree, &tlock);

    objRelease(&tree);

    // make sure copies are happening where they're supposed to
    string teststr = 0;
    strCopy(&teststr, _S"test123");
    tree = ssdCreateHashtable();

    ssdSet(tree, _S"l1/l2/l3/test2", true, stvar(string, teststr), NULL);
    if (strTestRefCount(teststr) != 2)
        ret = 1;

    // test getting an object node as a value
    if (!ssdGet(tree, _S"l1/l2/l3", &outvar, NULL) ||
        !stEq(outvar.type, stType(object)))
        ret = 1;
    stDestroy(stvar, &outvar);

    // this should get a copy of the string, increasing its ref count
    if (!ssdGet(tree, _S"l1/l2/l3/test2", &outvar, NULL) ||
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
    pval = ssdPtr(tree, _S"l1/l2/l3/test2", &tlock);
    if (!stEq(pval->type, stType(string)) ||
        !strEq(pval->data.st_string, _S"test123") ||
        pval->data.st_string != teststr ||
        strTestRefCount(teststr) != 2)
        ret = 1;
    ssdLockEnd(tree, &tlock);

    objRelease(&tree);

    if (strTestRefCount(teststr) != 1)
        ret = 1;
    strDestroy(&teststr);

    return ret;
}

static int test_ssd_single()
{
    int ret = 0;
    SSDNode *tree = ssdCreateSingle(stvar(int64, 200000));

    stvar outvar = { 0 };
    if (!ssdGet(tree, _S"", &outvar, NULL) ||
        !stEq(outvar.type, stType(int64)) ||
        outvar.data.st_int32 != 200000)
        ret = 1;

    objRelease(&tree);

    string teststr = 0;
    strCopy(&teststr, _S"test123");
    tree = ssdCreateSingle(stvar(string, teststr));

    if (strTestRefCount(teststr) != 2)
        ret = 1;

    outvar = (stvar){0};
    if (!ssdGet(tree, _S"", &outvar, NULL) ||
        !stEq(outvar.type, stType(string)) ||
        !strEq(outvar.data.st_string, _S"test123") ||
        strTestRefCount(teststr) != 3)
        ret = 1;

    stDestroy(stvar, &outvar);

    objRelease(&tree);

    if (strTestRefCount(teststr) != 1)
        ret = 1;

    strDestroy(&teststr);

    return ret;
}

static int test_ssd_subtree()
{
    int ret = 0;
    SSDNode *tree = ssdCreateHashtable();

    stvar outvar = { 0 };
    string teststr = 0;
    strCopy(&teststr, _S"test123");

    ssdSet(tree, _S"l1/b1/l3/test1", true, stvar(int32, 10743), NULL);
    ssdSet(tree, _S"l1/b1/l3/test2", true, stvar(string, teststr), NULL);
    ssdSet(tree, _S"l1/b2/l3/test1", true, stvar(int32, 39294), NULL);
    ssdSet(tree, _S"l1/b2/l3/test2", true, stvar(string, teststr), NULL);

    if (strTestRefCount(teststr) != 3)
        ret = 1;

    SSDNode *subtree = ssdSubtree(tree, _S"l1/b2", false, NULL);
    if (!subtree) {
        ret = 1;
        goto out;
    }

    if (!ssdGet(subtree, _S"l3/test1", &outvar, NULL) ||
        !stEq(outvar.type, stType(int32)) ||
        outvar.data.st_int32 != 39294)
        ret = 1;

    if (!ssdGet(subtree, _S"l3/test2", &outvar, NULL) ||
        !stEq(outvar.type, stType(string)) ||
        !strEq(outvar.data.st_string, _S"test123") ||
        outvar.data.st_string != teststr ||
        strTestRefCount(teststr) != 4)
        ret = 1;

    stDestroy(stvar, &outvar);
    if (strTestRefCount(teststr) != 3)
        ret = 1;

    // releasing the main tree shouldn't affect the subtree
    objRelease(&tree);

    if (!ssdGet(subtree, _S"l3/test1", &outvar, NULL) ||
        !stEq(outvar.type, stType(int32)) ||
        outvar.data.st_int32 != 39294)
        ret = 1;

    // but it should drop the refcount of the string when the other branch was destroyed
    if (!ssdGet(subtree, _S"l3/test2", &outvar, NULL) ||
        !stEq(outvar.type, stType(string)) ||
        !strEq(outvar.data.st_string, _S"test123") ||
        outvar.data.st_string != teststr ||
        strTestRefCount(teststr) != 3)
        ret = 1;

    stDestroy(stvar, &outvar);
    if (strTestRefCount(teststr) != 2)
        ret = 1;

out:
    objRelease(&subtree);
    objRelease(&tree);

    if (strTestRefCount(teststr) != 1)
        ret = 1;
    strDestroy(&teststr);

    return ret;
}

static int test_ssd_array()
{
    int ret = 0;
    SSDNode *tree = ssdCreateHashtable();

    string teststr = 0;
    strCopy(&teststr, _S"test123");

    SSDNode *sub = ssdSubtree(tree, _S"test/arr", SSD_Create_Array, NULL);

    SSDHashNode *h1 = ssdhashnodeCreate(sub->info);
    ssdnodeSet(h1, SSD_ByName, _S"test1", stvar(int32, 1), NULL);
    ssdnodeSet(sub, 0, NULL, stvar(object, h1), NULL);
    objRelease(&h1);

    ssdnodeSet(sub, 1, NULL, stvar(int64, 128), NULL);
    ssdnodeSet(sub, 2, NULL, stvar(float64, 5), NULL);
    h1 = ssdhashnodeCreate(sub->info);
    ssdnodeSet(h1, SSD_ByName, _S"test2", stvar(strref, _S"it's a test"), NULL);
    ssdnodeSet(sub, 3, NULL, stvar(object, h1), NULL);
    objRelease(&h1);
    SSDArrayNode *a1 = ssdarraynodeCreate(sub->info);
    ssdnodeSet(a1, 0, NULL, stvar(int32, 101), NULL);
    ssdnodeSet(a1, 1, NULL, stvar(int32, 102), NULL);
    ssdnodeSet(a1, 2, NULL, stvar(int32, 103), NULL);
    ssdnodeSet(sub, 4, NULL, stvar(object, a1), NULL);
    objRelease(&h1);

    SSDLock lock;
    stvar *out;
    ssdLockInit(&lock);
    out = ssdPtr(tree, _S"test/arr", &lock);
    if (!out || !stEq(out->type, stType(object)) || !objDynCast(out->data.st_object, SSDNode) ||
        !(ssdnodeIsArray(objDynCast(out->data.st_object, SSDNode))))
        ret = 1;

    out = ssdPtr(tree, _S"test/arr[0]/test1", &lock);
    if (!out || !stEq(out->type, stType(int32)) || out->data.st_int32 != 1)
        ret = 1;

    out = ssdPtr(tree, _S"test/arr[0]/test2", &lock);
    if (out)
        ret = 1;

    out = ssdPtr(tree, _S"test/arr[1]", &lock);
    if (!out || !stEq(out->type, stType(int64)) || out->data.st_int64 != 128)
        ret = 1;

    out = ssdPtr(tree, _S"test/arr[1]/test", &lock);
    if (out)
        ret = 1;

    out = ssdPtr(tree, _S"test/arr[2]", &lock);
    if (!out || !stEq(out->type, stType(float64)) || out->data.st_float64 != 5)
        ret = 1;

    out = ssdPtr(tree, _S"test/arr[2]/test", &lock);
    if (out)
        ret = 1;

    out = ssdPtr(tree, _S"test/arr[3]/test2", &lock);
    if (!out || !stEq(out->type, stType(string)) || !strEq(out->data.st_string, _S"it's a test"))
        ret = 1;

    out = ssdPtr(tree, _S"test/arr[3]/test1", &lock);
    if (out)
        ret = 1;

    out = ssdPtr(tree, _S"test/arr[4]/test1", &lock);
    if (out)
        ret = 1;

    out = ssdPtr(tree, _S"test/arr[4][0]", &lock);
    if (!out || !stEq(out->type, stType(int32)) || out->data.st_int32 != 101)
        ret = 1;

    out = ssdPtr(tree, _S"test/arr[4][1]", &lock);
    if (!out || !stEq(out->type, stType(int32)) || out->data.st_int32 != 102)
        ret = 1;

    out = ssdPtr(tree, _S"test/arr[4][2]", &lock);
    if (!out || !stEq(out->type, stType(int32)) || out->data.st_int32 != 103)
        ret = 1;

    ssdLockEnd(tree, &lock);

    objRelease(&sub);
    objRelease(&tree);

    return ret;
}

testfunc ssdtest_funcs[] = {
    { "tree", test_ssd_tree },
    { "single", test_ssd_single },
    { "subtree", test_ssd_subtree },
    { "array", test_ssd_array },
    { 0, 0 }
};
