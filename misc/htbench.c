#include <cx/container.h>
#include <cx/format.h>
#include <cx/platform/os.h>
#include <cx/string.h>
#include <cx/sys/entry.h>
#include <cx/time.h>

#include <math.h>
#include <stdio.h>

DEFINE_ENTRY_POINT;

#define NUM_ITERATIONS 100
#define NUM_TESTS      12
#define NUM_ITEMS      2500000
#define NUM_STR_ITEMS  175000

// Global string keys for benchmarking
static sa_string g_stringKeys;

typedef struct BenchTest {
    int a;
    int b;
    int c;
    int d;
    int e;
    int f;
} BenchTest;

typedef struct BenchResults {
    double times[NUM_TESTS];   // Times in milliseconds
} BenchResults;

typedef struct BenchStats {
    double mean;
    double median;
    double stddev;
} BenchStats;

static int compareDouble(const void* a, const void* b)
{
    double da = *(const double*)a;
    double db = *(const double*)b;
    if (da < db)
        return -1;
    if (da > db)
        return 1;
    return 0;
}

static void calculateStats(double* values, int count, BenchStats* stats)
{
    // Calculate mean
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += values[i];
    }
    stats->mean = sum / count;

    // Calculate median
    double* sorted = xaAlloc(sizeof(double) * count);
    for (int i = 0; i < count; i++) {
        sorted[i] = values[i];
    }
    qsort(sorted, count, sizeof(double), compareDouble);
    if (count % 2 == 0) {
        stats->median = (sorted[count / 2 - 1] + sorted[count / 2]) / 2.0;
    } else {
        stats->median = sorted[count / 2];
    }
    xaFree(sorted);

    // Calculate standard deviation
    double variance = 0.0;
    for (int i = 0; i < count; i++) {
        double diff = values[i] - stats->mean;
        variance += diff * diff;
    }
    variance /= count;
    stats->stddev = sqrt(variance);
}

static void runBenchmark(BenchResults* results)
{
    BenchTest xyz;
    int64 istart, iend;
    BenchTest tmp;
    htiter hti;
    int count;

    // ========== INT64 TESTS ==========

    hashtable htbl;
    htInit(&htbl, int64, opaque(BenchTest), 16);

    // Test 0: Initial insertion (int64)
    istart = clockTimer();
    for (int i = 0; i < NUM_ITEMS; i++) {
        htInsert(&htbl, int64, i, opaque, xyz);
    }
    iend              = clockTimer();
    results->times[0] = (double)timeToMsec(iend - istart);

    // Test 1: Lookup (int64, full table)
    istart = clockTimer();
    for (int i = 0; i < NUM_ITEMS; i++) {
        htFind(htbl, int64, i, opaque, &tmp, HT_Borrow);
    }
    iend              = clockTimer();
    results->times[1] = (double)timeToMsec(iend - istart);

    // Test 2: First deletion pass (int64)
    istart = clockTimer();
    for (int i = 0; i < NUM_ITEMS; i++) {
        if (i < 1000 || i > (NUM_ITEMS - 1000) || (i & 3) == 1) {
            htRemove(&htbl, int64, i);
        }
    }
    iend = clockTimer();
    results->times[2] = (double)timeToMsec(iend - istart);

    // Test 3: First iteration (int64)
    istart = clockTimer();
    htiInit(&hti, htbl);
    count = 0;
    while (htiValid(&hti)) {
        count++;
        htiNext(&hti);
    }
    iend              = clockTimer();
    results->times[3] = (double)timeToMsec(iend - istart);

    // Test 4: Second deletion pass (int64)
    istart = clockTimer();
    for (int i = 0; i < NUM_ITEMS; i++) {
        if (i < (NUM_ITEMS / 2.5) || i > (NUM_ITEMS * 0.8) || (i & 3)) {
            htRemove(&htbl, int64, i);
        }
    }
    iend = clockTimer();
    results->times[4] = (double)timeToMsec(iend - istart);

    // Test 5: Second iteration (int64)
    istart = clockTimer();
    htiInit(&hti, htbl);
    count = 0;
    while (htiValid(&hti)) {
        count++;
        htiNext(&hti);
    }
    iend = clockTimer();
    results->times[5] = (double)timeToMsec(iend - istart);

    htDestroy(&htbl);

    // ========== STRING TESTS ==========

    hashtable htbl_str;
    htInit(&htbl_str, string, opaque(BenchTest), 16);

    // Test 6: Initial insertion (string)
    istart = clockTimer();
    for (int i = 0; i < NUM_STR_ITEMS; i++) {
        htInsert(&htbl_str, string, g_stringKeys.a[i], opaque, xyz);
    }
    iend              = clockTimer();
    results->times[6] = (double)timeToMsec(iend - istart);

    // Test 7: Lookup (string, full table)
    istart = clockTimer();
    for (int i = 0; i < NUM_STR_ITEMS; i++) {
        htFind(htbl_str, string, g_stringKeys.a[i], opaque, &tmp, HT_Borrow);
    }
    iend = clockTimer();
    results->times[7] = (double)timeToMsec(iend - istart);

    // Test 8: First deletion pass (string)
    istart = clockTimer();
    for (int i = 0; i < NUM_STR_ITEMS; i++) {
        if (i < 1000 || i > (NUM_STR_ITEMS - 1000) || (i & 3) == 1) {
            htRemove(&htbl_str, string, g_stringKeys.a[i]);
        }
    }
    iend              = clockTimer();
    results->times[8] = (double)timeToMsec(iend - istart);

    // Test 9: First iteration (string)
    istart = clockTimer();
    htiInit(&hti, htbl_str);
    count = 0;
    while (htiValid(&hti)) {
        count++;
        htiNext(&hti);
    }
    iend = clockTimer();
    results->times[9] = (double)timeToMsec(iend - istart);

    // Test 10: Second deletion pass (string)
    istart = clockTimer();
    for (int i = 0; i < NUM_STR_ITEMS; i++) {
        if (i < (NUM_STR_ITEMS / 2.5) || i > (NUM_STR_ITEMS * 0.8) || (i & 3)) {
            htRemove(&htbl_str, string, g_stringKeys.a[i]);
        }
    }
    iend               = clockTimer();
    results->times[10] = (double)timeToMsec(iend - istart);

    // Test 11: Second iteration (string)
    istart = clockTimer();
    htiInit(&hti, htbl_str);
    count = 0;
    while (htiValid(&hti)) {
        count++;
        htiNext(&hti);
    }
    iend = clockTimer();
    results->times[11] = (double)timeToMsec(iend - istart);

    htDestroy(&htbl_str);
}

int entryPoint()
{
    const char* testNames[NUM_TESTS] = {
        "Insertion (2.5M items, int64)",  "Lookup (2.5M items, int64)",
        "Deletion (partial, int64)",      "Iteration (after partial delete, int64)",
        "Deletion (heavy, int64)",        "Iteration (after heavy delete, int64)",
        "Insertion (175K items, string)", "Lookup (175K items, string)",
        "Deletion (partial, string)",     "Iteration (after partial delete, string)",
        "Deletion (heavy, string)",       "Iteration (after heavy delete, string)"
    };

    double allResults[NUM_TESTS][NUM_ITERATIONS];

    // Prepare string keys once before all iterations
    printf("Preparing string keys...\n");
    saInit(&g_stringKeys, string, NUM_STR_ITEMS);
    for (int i = 0; i < NUM_STR_ITEMS; i++) {
        string key    = 0;
        string num    = 0;
        string prefix = _S"key_";
        strFromInt32(&num, i, 10);
        strConcatC(&key, &prefix, &num);
        saPushC(&g_stringKeys, string, &key);
    }

    printf("Running %d iterations of hashtable benchmark...\n\n", NUM_ITERATIONS);

    // Run all iterations
    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        BenchResults results;
        runBenchmark(&results);

        for (int test = 0; test < NUM_TESTS; test++) {
            allResults[test][iter] = results.times[test];
        }

        if ((iter + 1) % 10 == 0) {
            printf("Completed %d/%d iterations...\n", iter + 1, NUM_ITERATIONS);
        }
    }

    printf("\n=== Benchmark Results (times in milliseconds) ===\n\n");

    // Calculate and display statistics for each test
    for (int test = 0; test < NUM_TESTS; test++) {
        BenchStats stats;
        calculateStats(allResults[test], NUM_ITERATIONS, &stats);

        printf("%s:\n", testNames[test]);
        printf("  Mean:   %.2f ms\n", stats.mean);
        printf("  Median: %.2f ms\n", stats.median);
        printf("  StdDev: %.2f ms\n", stats.stddev);
        printf("\n");
    }

    // Clean up string keys
    saDestroy(&g_stringKeys);

    return 0;
}
