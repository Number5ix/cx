#include <stdio.h>
#include <cx/ssdtree.h>
#include <cx/string.h>
#include <cx/string/strtest.h>
#include <cx/container/foreach.h>

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
    if (ssdGet(tree, _S"test", &outvar) || !stvarIs(&outvar, none))
        TEST_FAILV(ret, 1, _SL("ssdGet(missing key) found=${int} type=${int} (want not found, type=none)"), stvar(int32, (int32)ssdGet(tree, _S"test", &outvar)), stvar(int32, (int32)stvarType(&outvar)->id));

    ssdSet(tree, _S"l1/l2/l3/test1", true, stvar(int32, 1920));
    if (!ssdGet(tree, _S"l1/l2/l3/test1", &outvar) ||
        !stvarIs(&outvar, int32) ||
        outvar.data.st_int32 != 1920)
        TEST_FAILV(ret, 1, _SL("ssdGet(l1/l2/l3/test1): type=${int} val=${int} (want int32, 1920)"), stvar(int32, (int32)stvarType(&outvar)->id), stvar(int32, outvar.data.st_int32));

    stvar *pval;
    ssdLockedTransaction(tree)
    {
        pval = ssdPtr(tree, _S"l1/l2/l3/test1");
        if (!pval ||
            !stvarIs(pval, int32) ||
            pval->data.st_int32 != 1920)
            TEST_FAILV(ret, 1, _SL("ssdPtr(l1/l2/l3/test1): pval=${ptr} val=${int} (want non-NULL int32, 1920)"), stvar(ptr, pval), stvar(int32, pval ? pval->data.st_int32 : 0));
    }

    objRelease(&tree);

    // make sure copies are happening where they're supposed to
    string teststr = 0;
    strCopy(&teststr, _S"test123");
    tree = ssdCreateHashtable();

    stvar tempst = { ._type = stType(string) };
    strDup(&tempst.data.st_string, teststr);
    if (strTestRefCount(teststr) != 2)
        TEST_FAILV(ret, 1, _SL("teststr refcount=${int} after strDup (want 2)"), stvar(int32, strTestRefCount(teststr)));

    ssdSetC(tree, _S"l1/l2/l3/test2", true, &tempst);
    if (strTestRefCount(teststr) != 2)
        TEST_FAILV(ret, 1, _SL("teststr refcount=${int} after ssdSetC (want 2)"), stvar(int32, strTestRefCount(teststr)));

    // test getting an object node as a value
    if (!ssdGet(tree, _S"l1/l2/l3", &outvar) ||
        !stvarIs(&outvar, object))
        TEST_FAILV(ret, 1, _SL("ssdGet(l1/l2/l3) as object: type=${int} (want object)"), stvar(int32, (int32)stvarType(&outvar)->id));
    stDestroy(stvar, &outvar);

    // this should get a copy of the string, increasing its ref count
    if (!ssdGet(tree, _S"l1/l2/l3/test2", &outvar) ||
        !stvarIs(&outvar, string) ||
        !strEq(outvar.data.st_string, _S"test123") ||
        outvar.data.st_string != teststr ||
        strTestRefCount(teststr) != 3)
        TEST_FAILV(ret, 1, _SL("ssdGet(l1/l2/l3/test2) copy: type=${int} val='${string}' refcount=${int} (want string, 'test123', 3)"), stvar(int32, (int32)stvarType(&outvar)->id), stvar(strref, outvar.data.st_string), stvar(int32, strTestRefCount(teststr)));

    stDestroy(stvar, &outvar);
    if (strTestRefCount(teststr) != 2)
        TEST_FAILV(ret, 1, _SL("teststr refcount=${int} after stDestroy(outvar) (want 2)"), stvar(int32, strTestRefCount(teststr)));

    // this should get a pointer to the string, NOT increasing the ref count
    ssdLockedTransaction(tree)
    {
        pval = ssdPtr(tree, _S"l1/l2/l3/test2");
        if (!stvarIs(pval, string) ||
            !strEq(pval->data.st_string, _S"test123") ||
            pval->data.st_string != teststr ||
            strTestRefCount(teststr) != 2)
            TEST_FAILV(ret, 1, _SL("ssdPtr(l1/l2/l3/test2): val='${string}' refcount=${int} (want 'test123', 2)"), stvar(strref, pval->data.st_string), stvar(int32, strTestRefCount(teststr)));

        // this should also get a pointer
        strref pstr = ssdStrRef(tree, _S"l1/l2/l3/test2");
        if (!pstr || !strEq(pstr, _S"test123")
            || pstr != teststr ||
            strTestRefCount(teststr) != 2)
            TEST_FAILV(ret, 1, _SL("ssdStrRef(l1/l2/l3/test2): pstr='${string}' refcount=${int} (want 'test123', 2)"), stvar(strref, pstr), stvar(int32, strTestRefCount(teststr)));
    }
    objRelease(&tree);

    if (strTestRefCount(teststr) != 1)
        TEST_FAILV(ret, 1, _SL("teststr refcount=${int} after objRelease(tree) (want 1)"), stvar(int32, strTestRefCount(teststr)));
    strDestroy(&teststr);

    return ret;
}

static int test_ssd_single()
{
    int ret = 0;
    SSDNode *tree = ssdCreateSingle();
    SSDLockState tstate;
    _ssdLockStateInit(&tstate);
    ssdnodeSet(tree, 0, NULL, stvar(int64, 200000), &tstate);
    _ssdLockEnd(tree, &tstate);

    stvar outvar = { 0 };
    if (!ssdGet(tree, _S"", &outvar) ||
        !stvarIs(&outvar, int64) ||
        outvar.data.st_int32 != 200000)
        TEST_FAILV(ret, 1, _SL("ssdGet(single) type=${int} val=${int} (want int64, 200000)"), stvar(int32, (int32)stvarType(&outvar)->id), stvar(int32, outvar.data.st_int32));

    objRelease(&tree);

    string teststr = 0;
    strCopy(&teststr, _S"test123");
    tree = ssdCreateSingle();
    _ssdLockStateInit(&tstate);
    ssdnodeSet(tree, 0, NULL, stvar(string, teststr), &tstate);
    _ssdLockEnd(tree, &tstate);

    if (strTestRefCount(teststr) != 2)
        TEST_FAILV(ret, 1, _SL("teststr refcount=${int} after ssdnodeSet (want 2)"), stvar(int32, strTestRefCount(teststr)));

    outvar = (stvar){0};
    if (!ssdGet(tree, _S"", &outvar) ||
        !stvarIs(&outvar, string) ||
        !strEq(outvar.data.st_string, _S"test123") ||
        strTestRefCount(teststr) != 3)
        TEST_FAILV(ret, 1, _SL("ssdGet(single string) type=${int} val='${string}' refcount=${int} (want string, 'test123', 3)"), stvar(int32, (int32)stvarType(&outvar)->id), stvar(strref, outvar.data.st_string), stvar(int32, strTestRefCount(teststr)));

    stDestroy(stvar, &outvar);

    objRelease(&tree);

    if (strTestRefCount(teststr) != 1)
        TEST_FAILV(ret, 1, _SL("teststr refcount=${int} after objRelease(tree) (want 1)"), stvar(int32, strTestRefCount(teststr)));

    strDestroy(&teststr);

    return ret;
}

static int test_ssd_subtree()
{
    int ret = 0;
    SSDNode *tree = ssdCreateHashtable();

    stvar outvar = { 0 };
    string teststr = 0;
    string teststr2 = 0;
    strCopy(&teststr, _S"test123");

    ssdSet(tree, _S"l1/b1/l3/test1", true, stvar(int32, 10743));
    ssdSet(tree, _S"l1/b1/l3/test2", true, stvar(string, teststr));
    ssdSet(tree, _S"l1/b2/l3/test1", true, stvar(int32, 39294));
    ssdSet(tree, _S"l1/b2/l3/test2", true, stvar(string, teststr));

    SSDNode *tree2 = ssdCreateHashtable();
    ssdSet(tree2, _S"k1/aabb", true, stvar(int32, 1122));
    ssdSet(tree2, _S"k1/bbaa", true, stvar(int32, 2211));
    ssdSet(tree2, _S"k1/nums[0]", true, stvar(int32, 100));
    ssdSet(tree2, _S"k1/nums[1]", true, stvar(int32, 101));
    ssdSet(tree2, _S"k1/nums[2]", true, stvar(int32, 102));
    ssdSet(tree2, _S"k1/nums[3]", true, stvar(int32, 103));
    ssdSet(tree2, _S"k1/nums[4]", true, stvar(int32, 104));
    ssdSet(tree2, _S"k2/aabb", true, stvar(int32, 4488));
    ssdSet(tree2, _S"k2/bbaa", true, stvar(int32, 8844));
    ssdSet(tree2, _S"k2/nums[0]", true, stvar(int32, 200));
    ssdSet(tree2, _S"k2/nums[1]", true, stvar(int32, 201));
    ssdSet(tree2, _S"k2/nums[2]", true, stvar(int32, 202));
    ssdSet(tree2, _S"k2/nums[3]", true, stvar(int32, 203));
    ssdSet(tree2, _S"k2/nums[4]", true, stvar(int32, 204));
    ssdSet(tree2, _S"alt", true, stvar(string, _S"yes, alt"));
    // graft tree2 onto tree
    ssdGraft(tree, _S"grafted", tree2, 0);
    // and graft the subtrees a couple places
    ssdGraft(tree, _S"l1/b3", tree2, _S"k1");
    ssdGraft(tree, _S"l1/b4", tree2, _S"k2");
    // then destroy it
    objRelease(&tree2);

    if (strTestRefCount(teststr) != 3)
        TEST_FAILV(ret, 1, _SL("teststr refcount=${int} after grafting (want 3)"), stvar(int32, strTestRefCount(teststr)));

    SSDNode *subtree = ssdSubtree(tree, _S"l1/b2", false);
    if (!subtree) {
        TEST_FAILV(ret, 1, _SL("ssdSubtree(l1/b2) returned NULL"), stvNone);
        goto out;
    }

    if (!ssdGet(subtree, _S"l3/test1", &outvar) ||
        !stvarIs(&outvar, int32) ||
        outvar.data.st_int32 != 39294)
        TEST_FAILV(ret, 1, _SL("ssdGet(subtree l3/test1) type=${int} val=${int} (want int32, 39294)"), stvar(int32, (int32)stvarType(&outvar)->id), stvar(int32, outvar.data.st_int32));

    if (!ssdGet(subtree, _S"l3/test2", &outvar) ||
        !stvarIs(&outvar, string) ||
        !strEq(outvar.data.st_string, _S"test123") ||
        outvar.data.st_string != teststr ||
        strTestRefCount(teststr) != 4)
        TEST_FAILV(ret, 1, _SL("ssdGet(subtree l3/test2) type=${int} val='${string}' refcount=${int} (want string, 'test123', 4)"), stvar(int32, (int32)stvarType(&outvar)->id), stvar(strref, outvar.data.st_string), stvar(int32, strTestRefCount(teststr)));

    stDestroy(stvar, &outvar);
    if (strTestRefCount(teststr) != 3)
        TEST_FAILV(ret, 1, _SL("teststr refcount=${int} after stDestroy(outvar) (want 3)"), stvar(int32, strTestRefCount(teststr)));

    // test some various type conversions
    if (ssdVal(float64, tree, _S"l1/b1/l3/test1", 1829.5) != 10743)
        TEST_FAILV(ret, 1, _SL("ssdVal(float64, l1/b1/l3/test1)=${float} (want 10743)"), stvar(float64, ssdVal(float64, tree, _S"l1/b1/l3/test1", 1829.5)));
    if (ssdVal(float64, tree, _S"l1/b1/l3/DOESNOTEXIST", 1829.5) != 1829.5)
        TEST_FAILV(ret, 1, _SL("ssdVal(float64, missing key)=${float} (want default 1829.5)"), stvar(float64, ssdVal(float64, tree, _S"l1/b1/l3/DOESNOTEXIST", 1829.5)));
    if (ssdVal(int64, tree, _S"l1/b1/l3/test1", 43434) != 10743)
        TEST_FAILV(ret, 1, _SL("ssdVal(int64, l1/b1/l3/test1)=${int} (want 10743)"), stvar(int64, ssdVal(int64, tree, _S"l1/b1/l3/test1", 43434)));
    if (ssdVal(uint16, tree, _S"l1/b1/l3/test1", 332) != 10743)
        TEST_FAILV(ret, 1, _SL("ssdVal(uint16, l1/b1/l3/test1)=${uint} (want 10743)"), stvar(uint32, ssdVal(uint16, tree, _S"l1/b1/l3/test1", 332)));

    ssdStringOutD(tree, _S"l1/b1/l3/test1", &teststr2, _S"Default String");
    if (!strEq(teststr2, _S"10743"))
        TEST_FAILV(ret, 1, _SL("ssdStringOutD(l1/b1/l3/test1)='${string}' (want '10743')"), stvar(strref, teststr2));
    ssdStringOutD(tree, _S"l1/b1/l3/DOESNOTEXIST", &teststr2, _S"Default String");
    if (!strEq(teststr2, _S"Default String"))
        TEST_FAILV(ret, 1, _SL("ssdStringOutD(missing key)='${string}' (want default 'Default String')"), stvar(strref, teststr2));

    // borrowed subtree should NOT increase refcount
    uintptr oldref = atomicLoad(uintptr, &subtree->_ref, Acquire);

    ssdLockedTransaction(tree)
    {
        SSDNode *btree = ssdSubtreeB(tree, _S"l1/b2");

        if (btree != subtree ||
            atomicLoad(uintptr, &btree->_ref, Acquire) != oldref)
            TEST_FAILV(ret, 1, _SL("ssdSubtreeB(l1/b2): btree=${ptr} subtree=${ptr} ref=${uint} oldref=${uint}"), stvar(ptr, btree), stvar(ptr, subtree), stvar(uint64, (uint64)atomicLoad(uintptr, &btree->_ref, Acquire)), stvar(uint64, (uint64)oldref));
        btree = NULL;

        // try object instance casting
        // this is effectively the same thing as ssdSubtreeB
        btree = ssdObjPtr(tree, _S"l1/b2", SSDNode);
        if (btree != subtree ||
            atomicLoad(uintptr, &btree->_ref, Acquire) != oldref)
            TEST_FAILV(ret, 1, _SL("ssdObjPtr(l1/b2): btree=${ptr} subtree=${ptr} ref=${uint} oldref=${uint}"), stvar(ptr, btree), stvar(ptr, subtree), stvar(uint64, (uint64)atomicLoad(uintptr, &btree->_ref, Acquire)), stvar(uint64, (uint64)oldref));

        btree = ssdSubtreeB(tree, _S"l1/b2/l3");

        // try the iterator
        int icount = 0;
        foreach(ssd, oiter, idx, name, val, btree)
        {
            // this also checks insertion order retention
            if (icount == 0 &&
                !(strEq(name, _S"test1") &&
                  stvarIs(val, int32) && val->data.st_int32 == 39294))
                TEST_FAILV(ret, 1, _SL("iterator[0]: name='${string}' type=${int} val=${int} (want 'test1', int32, 39294)"), stvar(strref, name), stvar(int32, (int32)stvarType(val)->id), stvar(int32, stvarIs(val, int32) ? val->data.st_int32 : 0));

            if (icount == 1 &&
                !(strEq(name, _S"test2") &&
                  strEq(stvarString(val), teststr)))
                TEST_FAILV(ret, 1, _SL("iterator[1]: name='${string}' val='${string}' (want 'test2', '${string}')"), stvar(strref, name), stvar(strref, stvarString(val)), stvar(strref, teststr));
            icount++;
        }
        if (icount != 2)
            TEST_FAILV(ret, 1, _SL("iterator count=${int} (want 2)"), stvar(int32, icount));
    }

    // check for the grafted values
    if (ssdVal(int32, tree, _S"grafted/k1/aabb", -1) != 1122 ||
        ssdVal(int32, tree, _S"grafted/k1/bbaa", -1) != 2211 ||
        ssdVal(int32, tree, _S"grafted/k1/nums[1]", -1) != 101 ||
        ssdVal(int32, tree, _S"grafted/k2/aabb", -1) != 4488 ||
        ssdVal(int32, tree, _S"grafted/k2/bbaa", -1) != 8844 ||
        ssdVal(int32, tree, _S"grafted/k2/nums[3]", -1) != 203)
        TEST_FAILV(ret, 1, _SL("grafted/k1,k2 values: aabb=${int}/${int} bbaa=${int}/${int} nums=${int}/${int} (want 1122/4488, 2211/8844, 101/203)"),
                   stvar(int32, ssdVal(int32, tree, _S"grafted/k1/aabb", -1)), stvar(int32, ssdVal(int32, tree, _S"grafted/k2/aabb", -1)),
                   stvar(int32, ssdVal(int32, tree, _S"grafted/k1/bbaa", -1)), stvar(int32, ssdVal(int32, tree, _S"grafted/k2/bbaa", -1)),
                   stvar(int32, ssdVal(int32, tree, _S"grafted/k1/nums[1]", -1)), stvar(int32, ssdVal(int32, tree, _S"grafted/k2/nums[3]", -1)));
    if (!ssdGet(tree, _S"grafted/alt", &outvar) ||
        !strEq(stvarString(&outvar), _S"yes, alt"))
        TEST_FAILV(ret, 1, _SL("ssdGet(grafted/alt)='${string}' (want 'yes, alt')"), stvar(strref, stvarString(&outvar)));
    stvarDestroy(&outvar);

    if (ssdVal(int32, tree, _S"l1/b3/aabb", -1) != 1122 ||
        ssdVal(int32, tree, _S"l1/b3/bbaa", -1) != 2211 ||
        ssdVal(int32, tree, _S"l1/b3/nums[0]", -1) != 100 ||
        ssdVal(int32, tree, _S"l1/b4/aabb", -1) != 4488 ||
        ssdVal(int32, tree, _S"l1/b4/bbaa", -1) != 8844 ||
        ssdVal(int32, tree, _S"l1/b4/nums[4]", -1) != 204)
        TEST_FAILV(ret, 1, _SL("l1/b3,b4 values: aabb=${int}/${int} bbaa=${int}/${int} nums=${int}/${int} (want 1122/4488, 2211/8844, 100/204)"),
                   stvar(int32, ssdVal(int32, tree, _S"l1/b3/aabb", -1)), stvar(int32, ssdVal(int32, tree, _S"l1/b4/aabb", -1)),
                   stvar(int32, ssdVal(int32, tree, _S"l1/b3/bbaa", -1)), stvar(int32, ssdVal(int32, tree, _S"l1/b4/bbaa", -1)),
                   stvar(int32, ssdVal(int32, tree, _S"l1/b3/nums[0]", -1)), stvar(int32, ssdVal(int32, tree, _S"l1/b4/nums[4]", -1)));

    // releasing the main tree shouldn't affect the subtree
    objRelease(&tree);

    if (!ssdGet(subtree, _S"l3/test1", &outvar) ||
        !stvarIs(&outvar, int32) ||
        outvar.data.st_int32 != 39294)
        TEST_FAILV(ret, 1, _SL("after objRelease(tree): ssdGet(subtree l3/test1) type=${int} val=${int} (want int32, 39294)"), stvar(int32, (int32)stvarType(&outvar)->id), stvar(int32, outvar.data.st_int32));

    // but it should drop the refcount of the string when the other branch was destroyed
    if (!ssdGet(subtree, _S"l3/test2", &outvar) ||
        !stvarIs(&outvar, string) ||
        !strEq(outvar.data.st_string, _S"test123") ||
        outvar.data.st_string != teststr ||
        strTestRefCount(teststr) != 3)
        TEST_FAILV(ret, 1, _SL("after objRelease(tree): ssdGet(subtree l3/test2) type=${int} val='${string}' refcount=${int} (want string, 'test123', 3)"), stvar(int32, (int32)stvarType(&outvar)->id), stvar(strref, outvar.data.st_string), stvar(int32, strTestRefCount(teststr)));

    stDestroy(stvar, &outvar);
    if (strTestRefCount(teststr) != 2)
        TEST_FAILV(ret, 1, _SL("teststr refcount=${int} after final stDestroy (want 2)"), stvar(int32, strTestRefCount(teststr)));

out:
    objRelease(&subtree);
    objRelease(&tree);

    if (strTestRefCount(teststr) != 1)
        TEST_FAILV(ret, 1, _SL("teststr refcount=${int} at end of test (want 1)"), stvar(int32, strTestRefCount(teststr)));
    strDestroy(&teststr);
    strDestroy(&teststr2);

    return ret;
}

static int test_ssd_array()
{
    int ret = 0;
    SSDNode *tree = ssdCreateHashtable();

    SSDLockState tlock;
    _ssdLockStateInit(&tlock);
    SSDNode *sub = ssdSubtree(tree, _S"test/arr", SSD_Create_Array);

    SSDNode *h1 = ssdtreeCreateNode(sub->tree, SSD_Create_Hashtable);
    ssdnodeSet(h1, SSD_ByName, _S"test1", stvar(int32, 1), &tlock);
    ssdnodeSet(sub, 0, NULL, stvar(object, h1), &tlock);
    objRelease(&h1);

    ssdnodeSet(sub, 1, NULL, stvar(int64, 128), &tlock);
    ssdnodeSet(sub, 2, NULL, stvar(float64, 5), &tlock);
    h1 = ssdtreeCreateNode(sub->tree, SSD_Create_Hashtable);
    ssdnodeSet(h1, SSD_ByName, _S"test2", stvar(strref, _S"it's a test"), &tlock);
    ssdnodeSet(sub, 3, NULL, stvar(object, h1), &tlock);
    objRelease(&h1);
    SSDNode *a1 = ssdtreeCreateNode(sub->tree, SSD_Create_Array);
    ssdnodeSet(a1, 0, NULL, stvar(int32, 101), &tlock);
    ssdnodeSet(a1, 1, NULL, stvar(int32, 102), &tlock);
    ssdnodeSet(a1, 2, NULL, stvar(int32, 103), &tlock);
    ssdnodeSet(sub, 4, NULL, stvar(object, a1), &tlock);
    objRelease(&a1);
    _ssdLockEnd(tree, &tlock);

    ssdLockedTransaction(tree)
    {
        stvar *out;
        out = ssdPtr(tree, _S"test/arr");
        if (!out || !stvarIs(out, object) || !objDynCast(SSDNode, out->data.st_object) ||
            !(ssdnodeIsArray(objDynCast(SSDNode, out->data.st_object))))
            TEST_FAILV(ret, 1, _SL("ssdPtr(test/arr): out=${ptr} type=${int} (want non-NULL object array)"), stvar(ptr, out), stvar(int32, out ? (int32)stvarType(out)->id : -1));

        out = ssdPtr(tree, _S"test/arr[0]/test1");
        if (!out || !stvarIs(out, int32) || out->data.st_int32 != 1)
            TEST_FAILV(ret, 1, _SL("ssdPtr(test/arr[0]/test1): out=${ptr} val=${int} (want non-NULL int32, 1)"), stvar(ptr, out), stvar(int32, out && stvarIs(out, int32) ? out->data.st_int32 : 0));

        out = ssdPtr(tree, _S"test/arr[0]/test2");
        if (out)
            TEST_FAILV(ret, 1, _SL("ssdPtr(test/arr[0]/test2)=${ptr} (want NULL)"), stvar(ptr, out));

        out = ssdPtr(tree, _S"test/arr[1]");
        if (!out || !stvarIs(out, int64) || out->data.st_int64 != 128)
            TEST_FAILV(ret, 1, _SL("ssdPtr(test/arr[1]): out=${ptr} val=${int} (want non-NULL int64, 128)"), stvar(ptr, out), stvar(int64, out && stvarIs(out, int64) ? out->data.st_int64 : 0));

        out = ssdPtr(tree, _S"test/arr[1]/test");
        if (out)
            TEST_FAILV(ret, 1, _SL("ssdPtr(test/arr[1]/test)=${ptr} (want NULL)"), stvar(ptr, out));

        out = ssdPtr(tree, _S"test/arr[2]");
        if (!out || !stvarIs(out, float64) || out->data.st_float64 != 5)
            TEST_FAILV(ret, 1, _SL("ssdPtr(test/arr[2]): out=${ptr} val=${float} (want non-NULL float64, 5)"), stvar(ptr, out), stvar(float64, out && stvarIs(out, float64) ? out->data.st_float64 : 0));

        out = ssdPtr(tree, _S"test/arr[2]/test");
        if (out)
            TEST_FAILV(ret, 1, _SL("ssdPtr(test/arr[2]/test)=${ptr} (want NULL)"), stvar(ptr, out));

        out = ssdPtr(tree, _S"test/arr[3]/test2");
        if (!out || !stvarIs(out, string) || !strEq(out->data.st_string, _S"it's a test"))
            TEST_FAILV(ret, 1, _SL("ssdPtr(test/arr[3]/test2): out=${ptr} val='${string}' (want non-NULL string, 'it's a test')"), stvar(ptr, out), stvar(strref, out && stvarIs(out, string) ? out->data.st_string : (strref)_S""));

        out = ssdPtr(tree, _S"test/arr[3]/test1");
        if (out)
            TEST_FAILV(ret, 1, _SL("ssdPtr(test/arr[3]/test1)=${ptr} (want NULL)"), stvar(ptr, out));

        out = ssdPtr(tree, _S"test/arr[4]/test1");
        if (out)
            TEST_FAILV(ret, 1, _SL("ssdPtr(test/arr[4]/test1)=${ptr} (want NULL)"), stvar(ptr, out));

        out = ssdPtr(tree, _S"test/arr[4][0]");
        if (!out || !stvarIs(out, int32) || out->data.st_int32 != 101)
            TEST_FAILV(ret, 1, _SL("ssdPtr(test/arr[4][0]): out=${ptr} val=${int} (want non-NULL int32, 101)"), stvar(ptr, out), stvar(int32, out && stvarIs(out, int32) ? out->data.st_int32 : 0));

        out = ssdPtr(tree, _S"test/arr[4][1]");
        if (!out || !stvarIs(out, int32) || out->data.st_int32 != 102)
            TEST_FAILV(ret, 1, _SL("ssdPtr(test/arr[4][1]): out=${ptr} val=${int} (want non-NULL int32, 102)"), stvar(ptr, out), stvar(int32, out && stvarIs(out, int32) ? out->data.st_int32 : 0));

        out = ssdPtr(tree, _S"test/arr[4][2]");
        if (!out || !stvarIs(out, int32) || out->data.st_int32 != 103)
            TEST_FAILV(ret, 1, _SL("ssdPtr(test/arr[4][2]): out=${ptr} val=${int} (want non-NULL int32, 103)"), stvar(ptr, out), stvar(int32, out && stvarIs(out, int32) ? out->data.st_int32 : 0));

        sa_stvar arr1 = saInitNone;
        if (!ssdExportArray(tree, _S"test/arr", &arr1) ||
            saSize(arr1) != 5 ||
            !stvarIs(&arr1.a[2], float64) ||
            arr1.a[2].data.st_float64 != 5)
            TEST_FAILV(ret, 1, _SL("ssdExportArray(test/arr): size=${uint} arr1[2].type=${int} val=${float} (want 5, float64, 5)"), stvar(uint32, saSize(arr1)), stvar(int32, saSize(arr1) > 2 ? (int32)stvarType(&arr1.a[2])->id : -1), stvar(float64, saSize(arr1) > 2 && stvarIs(&arr1.a[2], float64) ? arr1.a[2].data.st_float64 : 0));

        // paste the array back in as a child of itself
        ssdImportArray(tree, _S"test/arr[5]", arr1);

        out = ssdPtr(tree, _S"test/arr[5][3]/test2");
        if (!out || !stvarIs(out, string) || !strEq(out->data.st_string, _S"it's a test"))
            TEST_FAILV(ret, 1, _SL("ssdPtr(test/arr[5][3]/test2): out=${ptr} val='${string}' (want non-NULL string, 'it's a test')"), stvar(ptr, out), stvar(strref, out && stvarIs(out, string) ? out->data.st_string : (strref)_S""));

        saDestroy(&arr1);

        sa_int32 arr2 = saInitNone;
        if (!ssdExportTypedArray(tree, _S"test/arr[4]", int32, &arr2, true) ||
            saSize(arr2) != 3 ||
            arr2.a[0] != 101 ||
            arr2.a[1] != 102 ||
            arr2.a[2] != 103)
            TEST_FAILV(ret, 1, _SL("ssdExportTypedArray(test/arr[4], int32, strict): size=${uint} vals=${int},${int},${int} (want 3, 101,102,103)"), stvar(uint32, saSize(arr2)), stvar(int32, saSize(arr2) > 0 ? arr2.a[0] : 0), stvar(int32, saSize(arr2) > 1 ? arr2.a[1] : 0), stvar(int32, saSize(arr2) > 2 ? arr2.a[2] : 0));

        ssdImportTypedArray(tree, _S"test/another", int32, arr2);
        saDestroy(&arr2);

        if (ssdVal(int32, tree, _S"test/another[1]", -1) != 102)
            TEST_FAILV(ret, 1, _SL("ssdVal(int32, test/another[1])=${int} (want 102)"), stvar(int32, ssdVal(int32, tree, _S"test/another[1]", -1)));

        // should fail with strict == true
        if (ssdExportTypedArray(tree, _S"test/arr", int32, &arr2, true) ||
            saSize(arr2) != 0)
            TEST_FAILV(ret, 1, _SL("ssdExportTypedArray(test/arr, int32, strict) with mixed types: succeeded=${int} size=${uint} (want failure, size=0)"), stvar(int32, (int32)ssdExportTypedArray(tree, _S"test/arr", int32, &arr2, true)), stvar(uint32, saSize(arr2)));

        // should filter out everything except the one int64
        sa_int64 arr3 = saInitNone;
        if (!ssdExportTypedArray(tree, _S"test/arr", int64, &arr3, false) ||
            saSize(arr3) != 1 ||
            arr3.a[0] != 128)
            TEST_FAILV(ret, 1, _SL("ssdExportTypedArray(test/arr, int64, non-strict): size=${uint} val=${int} (want 1, 128)"), stvar(uint32, saSize(arr3)), stvar(int64, saSize(arr3) > 0 ? arr3.a[0] : 0));
        saDestroy(&arr3);
    }

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
