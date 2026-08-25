#include <cx/container/hashtable.h>
#include <cx/container/sarray.h>
#include <cx/string.h>
#include <cx/string/strtest.h>

#define TEST_FILE httest
#define TEST_FUNCS httest_funcs
#include "common.h"

static int test_init()
{
    hashtable ht = 0;
    
    // Test basic initialization
    htInit(&ht, string, int32, 8);
    
    if (!ht)
        TEST_FAIL(1, _SL("htInit(string,int32) returned NULL"), stvNone);
    
    if (htSize(ht) != 0)
        TEST_FAIL(1, _SL("htSize(ht)=${uint} != 0"), stvar(uint64, (uint64)htSize(ht)));
    
    if (htKeyType(ht) != stType(string))
        TEST_FAIL(1, _SL("htKeyType(ht) != stType(string)"), stvNone);
    
    if (htValType(ht) != stType(int32))
        TEST_FAIL(1, _SL("htValType(ht) != stType(int32)"), stvNone);
    
    htDestroy(&ht);
    
    if (ht != 0)
        TEST_FAIL(1, _SL("htDestroy did not NULL the handle"), stvNone);
    
    // Test with different types
    htInit(&ht, int32, string, 16);
    
    if (htKeyType(ht) != stType(int32))
        TEST_FAIL(1, _SL("htKeyType(ht) != stType(int32)"), stvNone);
    
    if (htValType(ht) != stType(string))
        TEST_FAIL(1, _SL("htValType(ht) != stType(string)"), stvNone);
    
    htDestroy(&ht);
    
    return 0;
}

static int test_insert()
{
    hashtable ht = 0;
    htInit(&ht, string, int32, 8);
    
    // Test basic insert
    htelem elem = htInsert(&ht, string, _S"one", int32, 1);
    if (!elem)
        TEST_FAIL(1, _SL("htInsert(\"one\") returned NULL"), stvNone);
    
    if (htSize(ht) != 1)
        TEST_FAIL(1, _SL("htSize(ht)=${uint} != 1"), stvar(uint64, (uint64)htSize(ht)));
    
    // Insert multiple values
    htInsert(&ht, string, _S"two", int32, 2);
    htInsert(&ht, string, _S"three", int32, 3);
    htInsert(&ht, string, _S"four", int32, 4);
    htInsert(&ht, string, _S"five", int32, 5);
    
    if (htSize(ht) != 5)
        TEST_FAIL(1, _SL("htSize(ht)=${uint} != 5"), stvar(uint64, (uint64)htSize(ht)));
    
    // Test overwrite behavior (default is to overwrite)
    htInsert(&ht, string, _S"three", int32, 33);
    
    if (htSize(ht) != 5)  // Size should remain the same
        TEST_FAIL(1, _SL("after overwrite: htSize(ht)=${uint} != 5"), stvar(uint64, (uint64)htSize(ht)));
    
    int32 val;
    if (!htFind(ht, string, _S"three", int32, &val))
        TEST_FAIL(1, _SL("htFind(\"three\") failed after overwrite"), stvNone);
    
    if (val != 33)
        TEST_FAIL(1, _SL("ht[three]=${int} != 33"), stvar(int32, val));
    
    // Test HT_Ignore flag - should not overwrite
    htInsert(&ht, string, _S"three", int32, 333, HT_Ignore);
    
    if (!htFind(ht, string, _S"three", int32, &val))
        TEST_FAIL(1, _SL("htFind(\"three\") failed after HT_Ignore insert"), stvNone);
    
    if (val != 33)  // Should still be 33, not 333
        TEST_FAIL(1, _SL("HT_Ignore insert overwrote value: ht[three]=${int} != 33"), stvar(int32, val));
    
    htDestroy(&ht);
    
    return 0;
}

static int test_find()
{
    hashtable ht = 0;
    htInit(&ht, string, int32, 8);
    
    // Insert test data
    htInsert(&ht, string, _S"alpha", int32, 100);
    htInsert(&ht, string, _S"beta", int32, 200);
    htInsert(&ht, string, _S"gamma", int32, 300);
    htInsert(&ht, string, _S"delta", int32, 400);
    
    // Test finding existing keys
    int32 val;
    if (!htFind(ht, string, _S"alpha", int32, &val))
        TEST_FAIL(1, _SL("htFind(\"alpha\") failed"), stvNone);
    if (val != 100)
        TEST_FAIL(1, _SL("ht[alpha]=${int} != 100"), stvar(int32, val));
    
    if (!htFind(ht, string, _S"gamma", int32, &val))
        TEST_FAIL(1, _SL("htFind(\"gamma\") failed"), stvNone);
    if (val != 300)
        TEST_FAIL(1, _SL("ht[gamma]=${int} != 300"), stvar(int32, val));
    
    // Test finding non-existent key
    if (htFind(ht, string, _S"epsilon", int32, &val))
        TEST_FAIL(1, _SL("htFind unexpectedly found non-existent key \"epsilon\""), stvNone);
    
    // Test finding without extracting value (just checking existence)
    htelem elem = htFind(ht, string, _S"beta", none, NULL);
    if (!elem)
        TEST_FAIL(1, _SL("htFind(\"beta\", none) returned NULL"), stvNone);
    
    // Test htHasKey
    if (!htHasKey(ht, string, _S"delta"))
        TEST_FAIL(1, _SL("htHasKey(\"delta\") false"), stvNone);
    
    if (htHasKey(ht, string, _S"nothere"))
        TEST_FAIL(1, _SL("htHasKey unexpectedly true for \"nothere\""), stvNone);
    
    htDestroy(&ht);
    
    return 0;
}

static int test_htelem()
{
    hashtable ht = 0;
    htInit(&ht, string, int32, 8);
    
    // Insert and get htelem
    htelem elem1 = htInsert(&ht, string, _S"first", int32, 111);
    htelem elem2 = htInsert(&ht, string, _S"second", int32, 222);
    htelem elem3 = htInsert(&ht, string, _S"third", int32, 333);
    
    if (!elem1 || !elem2 || !elem3)
        TEST_FAIL(1, _SL("htInsert returned NULL for one of first/second/third"), stvNone);
    
    // Test hteVal macro to get values from elements
    int32 val1 = hteVal(ht, int32, elem1);
    int32 val2 = hteVal(ht, int32, elem2);
    int32 val3 = hteVal(ht, int32, elem3);
    
    if (val1 != 111 || val2 != 222 || val3 != 333)
        TEST_FAIL(1, _SL("hteVal: val1=${int} val2=${int} val3=${int} (expected 111/222/333)"), stvar(int32, val1), stvar(int32, val2), stvar(int32, val3));
    
    // Test hteValPtr to get pointer to value
    int32 *ptr1 = hteValPtr(ht, int32, elem1);
    if (!ptr1 || *ptr1 != 111)
        TEST_FAIL(1, _SL("hteValPtr: ptr1=${ptr} *ptr1=${int} (expected non-NULL, 111)"), stvar(ptr, ptr1), stvar(int32, ptr1 ? *ptr1 : -1));
    
    // Modify through pointer
    *ptr1 = 1111;
    
    // Verify modification
    htFind(ht, string, _S"first", int32, &val1);
    if (val1 != 1111)
        TEST_FAIL(1, _SL("after pointer modification: ht[first]=${int} != 1111"), stvar(int32, val1));
    
    // Test hteKey macro to get keys from elements
    string key2 = hteKey(ht, string, elem2);
    if (!strEq(key2, _S"second"))
        TEST_FAIL(1, _SL("hteKey(elem2)='${string}' != 'second'"), stvar(strref, key2));
    
    // Find an element and use it
    htelem elem4 = htFind(ht, string, _S"third", none, NULL);
    if (!elem4)
        TEST_FAIL(1, _SL("htFind(\"third\", none) returned NULL"), stvNone);
    
    int32 val4 = hteVal(ht, int32, elem4);
    if (val4 != 333)
        TEST_FAIL(1, _SL("hteVal(elem4)=${int} != 333"), stvar(int32, val4));
    
    htDestroy(&ht);
    
    return 0;
}

static int test_remove()
{
    hashtable ht = 0;
    htInit(&ht, string, int32, 8);
    
    // Insert test data
    htInsert(&ht, string, _S"red", int32, 10);
    htInsert(&ht, string, _S"green", int32, 20);
    htInsert(&ht, string, _S"blue", int32, 30);
    htInsert(&ht, string, _S"yellow", int32, 40);
    htInsert(&ht, string, _S"cyan", int32, 50);
    
    if (htSize(ht) != 5)
        TEST_FAIL(1, _SL("htSize(ht)=${uint} != 5"), stvar(uint64, (uint64)htSize(ht)));
    
    // Test basic removal
    if (!htRemove(&ht, string, _S"green"))
        TEST_FAIL(1, _SL("htRemove(\"green\") failed"), stvNone);
    
    if (htSize(ht) != 4)
        TEST_FAIL(1, _SL("after remove: htSize(ht)=${uint} != 4"), stvar(uint64, (uint64)htSize(ht)));
    
    // Verify it's actually gone
    if (htHasKey(ht, string, _S"green"))
        TEST_FAIL(1, _SL("htHasKey(\"green\") still true after removal"), stvNone);
    
    // Test removing non-existent key
    if (htRemove(&ht, string, _S"purple"))
        TEST_FAIL(1, _SL("htRemove unexpectedly succeeded for non-existent key \"purple\""), stvNone);
    
    if (htSize(ht) != 4)
        TEST_FAIL(1, _SL("after no-op remove: htSize(ht)=${uint} != 4"), stvar(uint64, (uint64)htSize(ht)));
    
    // Test htExtract to get value while removing
    int32 extracted;
    if (!htExtract(&ht, string, _S"blue", int32, &extracted))
        TEST_FAIL(1, _SL("htExtract(\"blue\") failed"), stvNone);
    
    if (extracted != 30)
        TEST_FAIL(1, _SL("htExtract(\"blue\")=${int} != 30"), stvar(int32, extracted));
    
    if (htSize(ht) != 3)
        TEST_FAIL(1, _SL("after extract: htSize(ht)=${uint} != 3"), stvar(uint64, (uint64)htSize(ht)));
    
    if (htHasKey(ht, string, _S"blue"))
        TEST_FAIL(1, _SL("htHasKey(\"blue\") still true after extract"), stvNone);
    
    // Remove remaining items
    htRemove(&ht, string, _S"red");
    htRemove(&ht, string, _S"yellow");
    htRemove(&ht, string, _S"cyan");
    
    if (htSize(ht) != 0)
        TEST_FAIL(1, _SL("after removing all: htSize(ht)=${uint} != 0"), stvar(uint64, (uint64)htSize(ht)));
    
    htDestroy(&ht);
    
    return 0;
}

static int test_iterator()
{
    hashtable ht = 0;
    htInit(&ht, string, int32, 8);
    
    // Insert test data
    htInsert(&ht, string, _S"apple", int32, 1);
    htInsert(&ht, string, _S"banana", int32, 2);
    htInsert(&ht, string, _S"cherry", int32, 3);
    htInsert(&ht, string, _S"date", int32, 4);
    htInsert(&ht, string, _S"elderberry", int32, 5);
    
    // Test iterator
    htiter iter;
    int count = 0;
    int sum = 0;
    
    if (!htiInit(&iter, ht))
        TEST_FAIL(1, _SL("htiInit failed on non-empty hashtable"), stvNone);
    
    do {
        string key = htiKey(string, iter);
        int32 val = htiVal(int32, iter);
        
        // Verify key is not empty
        if (strEmpty(key))
            TEST_FAIL(1, _SL("iterator produced an empty key"), stvNone);
        
        // Verify value is in expected range
        if (val < 1 || val > 5)
            TEST_FAIL(1, _SL("iterator value ${int} out of expected range [1,5]"), stvar(int32, val));
        
        count++;
        sum += val;
    } while (htiNext(&iter));
    
    htiFinish(&iter);
    
    // Should have iterated over all 5 items
    if (count != 5)
        TEST_FAIL(1, _SL("iterator count=${int} != 5"), stvar(int32, count));
    
    // Sum should be 1+2+3+4+5 = 15
    if (sum != 15)
        TEST_FAIL(1, _SL("iterator sum=${int} != 15"), stvar(int32, sum));
    
    // Test iterator on empty hashtable
    htClear(&ht);
    
    if (!htiInit(&iter, ht)) {
        // This is expected - empty hashtable returns false
        // But we should still be able to check validity
        if (htiValid(&iter))
            TEST_FAIL(1, _SL("htiValid true after htiInit failed on empty table"), stvNone);
    } else {
        TEST_FAIL(1, _SL("htiInit unexpectedly succeeded on empty table"), stvNone);
    }
    
    htDestroy(&ht);
    
    return 0;
}

static int test_order()
{
    hashtable ht = 0;
    htInit(&ht, int32, string, 16);
    
    // Insert items in a specific order
    htInsert(&ht, int32, 10, string, _S"ten");
    htInsert(&ht, int32, 20, string, _S"twenty");
    htInsert(&ht, int32, 30, string, _S"thirty");
    htInsert(&ht, int32, 40, string, _S"forty");
    htInsert(&ht, int32, 50, string, _S"fifty");
    
    // Collect insertion order using iterator
    sa_int32 keys;
    saInit(&keys, int32, 5);
    
    htiter iter;
    if (htiInit(&iter, ht)) {
        do {
            int32 key = htiKey(int32, iter);
            saPush(&keys, int32, key);
        } while (htiNext(&iter));
        htiFinish(&iter);
    }
    
    // Verify we got all keys
    if (saSize(keys) != 5)
        TEST_FAIL(1, _SL("saSize(keys)=${uint} != 5"), stvar(uint64, (uint64)saSize(keys)));
    
    // Verify the insertion order is preserved (should be 10, 20, 30, 40, 50)
    // This hashtable implementation guarantees insertion order is retained
    if (keys.a[0] != 10 || keys.a[1] != 20 || keys.a[2] != 30 ||
        keys.a[3] != 40 || keys.a[4] != 50)
        TEST_FAIL(1, _SL("insertion order not preserved: keys={${int},${int},${int},${int},${int}}"), stvar(int32, keys.a[0]), stvar(int32, keys.a[1]), stvar(int32, keys.a[2]), stvar(int32, keys.a[3]), stvar(int32, keys.a[4]));
    
    // Test that removing and re-adding maintains consistency
    htRemove(&ht, int32, 30);
    htInsert(&ht, int32, 30, string, _S"thirty-new");
    
    string val;
    if (!htFind(ht, int32, 30, string, &val))
        TEST_FAIL(1, _SL("htFind(30) failed after remove+reinsert"), stvNone);
    
    if (!strEq(val, _S"thirty-new"))
        TEST_FAIL(1, _SL("ht[30]='${string}' != 'thirty-new'"), stvar(strref, val));
    
    strDestroy(&val);
    saDestroy(&keys);
    htDestroy(&ht);
    
    return 0;
}

static int test_complex()
{
    hashtable ht = 0;
    htInit(&ht, string, string, 8);
    
    // Test with string keys and values
    htInsert(&ht, string, _S"greeting", string, _S"Hello");
    htInsert(&ht, string, _S"farewell", string, _S"Goodbye");
    htInsert(&ht, string, _S"thanks", string, _S"Thank you");
    
    string val = 0;
    if (!htFind(ht, string, _S"greeting", string, &val))
        TEST_FAIL(1, _SL("htFind(\"greeting\") failed"), stvNone);
    
    if (!strEq(val, _S"Hello"))
        TEST_FAIL(1, _SL("ht[greeting]='${string}' != 'Hello'"), stvar(strref, val));
    
    strDestroy(&val);
    
    // Test htClone
    hashtable ht2 = 0;
    htClone(&ht2, ht);
    
    if (htSize(ht2) != 3)
        TEST_FAIL(1, _SL("htSize(ht2)=${uint} != 3"), stvar(uint64, (uint64)htSize(ht2)));
    
    if (!htFind(ht2, string, _S"farewell", string, &val))
        TEST_FAIL(1, _SL("htFind(ht2, \"farewell\") failed"), stvNone);
    
    if (!strEq(val, _S"Goodbye"))
        TEST_FAIL(1, _SL("ht2[farewell]='${string}' != 'Goodbye'"), stvar(strref, val));
    
    strDestroy(&val);
    
    // Modify original, clone should be unaffected
    htInsert(&ht, string, _S"greeting", string, _S"Hi");
    
    if (!htFind(ht, string, _S"greeting", string, &val))
        TEST_FAIL(1, _SL("htFind(ht, \"greeting\") failed after modification"), stvNone);
    if (!strEq(val, _S"Hi"))
        TEST_FAIL(1, _SL("ht[greeting]='${string}' != 'Hi'"), stvar(strref, val));
    strDestroy(&val);
    
    if (!htFind(ht2, string, _S"greeting", string, &val))
        TEST_FAIL(1, _SL("htFind(ht2, \"greeting\") failed"), stvNone);
    if (!strEq(val, _S"Hello"))  // Should still be original
        TEST_FAIL(1, _SL("clone was affected by original's modification: ht2[greeting]='${string}' != 'Hello'"), stvar(strref, val));
    strDestroy(&val);
    
    htDestroy(&ht);
    htDestroy(&ht2);
    
    return 0;
}

testfunc httest_funcs[] = {
    { "init", test_init },
    { "insert", test_insert },
    { "find", test_find },
    { "htelem", test_htelem },
    { "remove", test_remove },
    { "iterator", test_iterator },
    { "order", test_order },
    { "complex", test_complex },
    { 0, 0 }
};
