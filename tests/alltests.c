#include <stdio.h>
#include <cx/time.h>

// Run all tests in a single process, to make manual testing easier

typedef struct testspec {
    const char *descr;
    const char *module;
    const char *test;
} testspec;

static const testspec tests[] = {
{ "cxmem: Allocation", "cxmemtest", "alloc" },
{ "cxmem: Usable Size", "cxmemtest", "usable_size" },
{ "cxmem: Free", "cxmemtest", "free" },
{ "cxmem: Reallocation", "cxmemtest", "realloc" },
{ "format: Integers", "fmttest", "int" },
{ "format: Floating Point", "fmttest", "float" },
{ "format: Objects", "fmttest", "object" },
{ "format: Pointers", "fmttest", "ptr" },
{ "format: Strings", "fmttest", "string" },
{ "format: SUID", "fmttest", "suid" },
{ "format: Array indexing", "fmttest", "array" },
{ "format: Hashtable lookup", "fmttest", "hash" },
{ "format: Defaults", "fmttest", "default" },
{ "format: Error handling", "fmttest", "error" },
{ "log: Log Levels", "logtest", "levels" },
{ "log: Batch", "logtest", "batch" },
{ "log: Shutdown", "logtest", "shutdown" },
{ "object: Interface", "objtest", "iface" },
{ "object: Class Inheritance", "objtest", "inherit" },
{ "object: Interface Inheritance", "objtest", "ifinherit" },
{ "object: Override", "objtest", "override" },
{ "object: Abstract Class", "objtest", "abstract" },
{ "object: Dynamic Cast", "objtest", "dyncast" },
{ "object: Object Array", "objtest", "obj_array" },
{ "sarray: Int Array", "sarraytest", "int" },
{ "sarray: Sorted Int Array", "sarraytest", "sorted_int" },
{ "sarray: String Array", "sarraytest", "string" },
{ "sarray: Sort", "sarraytest", "sort" },
{ "sarray: String Sort", "sarraytest", "string_sort" },
{ "string: Join", "strtest", "join" },
{ "string: Append/Prepend", "strtest", "append" },
{ "string: Substrings", "strtest", "substr" },
{ "string: Compare", "strtest", "compare" },
{ "string: Long Strings", "strtest", "longstring" },
{ "string: Ropes", "strtest", "rope" },
{ "thread: Basic multithreading", "thrtest", "basic" },
{ "thread: Futex", "thrtest", "futex" },
{ "thread: Futex with timeout", "thrtest", "timeout" },
{ "thread: Semaphore", "thrtest", "sema" },
{ "thread: Mutex", "thrtest", "mutex" },
{ "thread: Reader/writer lock", "thrtest", "rwlock" },
{ "thread: Event", "thrtest", "event" },
{ "thread: Event (with spin)", "thrtest", "event_s" },
{ "thread: Condition Variable", "thrtest", "condvar" },
{ 0 }
};

extern int main(int argc, const char *argv[]);

int alltests(int argc, const char *argv[])
{
    const testspec *s;
    int ntests = 0;

    for (s = &tests[0]; s->descr; s++) {
        ntests++;
    }

    int npassed = 0;
    int64 totalstart = clockTimer();
    const char *fake_argv[3];

    fake_argv[0] = argv[0];

    puts("Running tests...");

    for (int i = 0; i < ntests; i++)
    {
        int64 tstart = clockTimer();
        printf("%4d/%d: %s ", i + 1, ntests, tests[i].descr);
        int ndots = 40 - (int)strlen(tests[i].descr);
        for (int j = 0; j < ndots; j++)
            putchar('.');
        fflush(stdout);

        fake_argv[1] = tests[i].module;
        fake_argv[2] = tests[i].test;
        bool res = main(3, fake_argv);
        int64 tend = clockTimer();

        printf("    %s    %4d msec\n",
                res ? "Failed" : "Passed", (int)timeToMsec(tend - tstart));
        if (!res)
            npassed++;
    }

    int64 totalend = clockTimer();
    printf("\n%d%% tests passed, %d tests failed out of %d, total elapsed %d msec\n",
            npassed * 100 / ntests, ntests - npassed, ntests,
            (int)timeToMsec(totalend - totalstart));

    return ntests - npassed;
}
