# Test logging

By default, running a test logs nothing anywhere -- no destination is registered, so the cost of
every `TEST_FAIL`/`TEST_WARN`/`TEST_INFO` call site is one relaxed atomic load. To see what a
test logged, ask for it:

```sh
build/gcc-debug/tests/test_runner logtest levels -log=Info
CX_TEST_LOGLEVEL=Diag ctest --test-dir build/gcc-debug
```

- `-log=<Level>` is a trailing flag on a manual `test_runner <group> <subtest>` invocation, for
  troubleshooting one test at a time.
- `CX_TEST_LOGLEVEL=<Level>` is an environment variable fallback, for a bulk `ctest`/`alltests`
  run without editing any `add_test()` line. The flag takes priority if both are set.
- `<Level>` is any name from `LOG_LEVEL_ENUM` (`Fatal`, `Error`, `Warn`, `Notice`, `Info`,
  `Verbose`, `Diag`, `Debug`, `Trace`), case-insensitive. An unrecognized name aborts the run
  immediately with an error, rather than silently logging nothing.

When a level is requested, the harness registers a console destination scoped to exactly
`tests/**` (where `TEST_FAIL`/`TEST_WARN`/`TEST_INFO` log) plus `cx/**`, so cx's own internal
logging (normally invisible -- see the `cx` channel in the logging reference) shows up too. It is
deliberately *not* `**`: test files -- `logtest.c` especially -- route plenty of traffic across
their own private channels (`LogDefault`, `hier`, `grp/**`, `vol/**`, ...) as test data for the
log system itself, and a destination reaching `**` would both drown a troubleshooting run in that
noise and falsify the "nothing is listening on this channel" premise several `logtest.c`
assertions depend on. Test output (TAP lines, pass/fail) still goes to stdout, so the two don't
interleave.

## Writing test bodies

Every test file logs to the same channel (`tests`, via `LOG_CHANNEL`) with no per-file setup.
It's deliberately not under `cx/` -- that root is `LOG_Restricted`, and the `NULL`/unrestricted
filter that nearly every existing test destination uses cannot reach into a restricted subtree.
`common.h` provides four macros:

```c
TEST_FAIL(code, fmt, ...)          // logs an Error, then returns code
TEST_FAILV(var, code, fmt, ...)    // logs an Error, then assigns code to var (no return)
TEST_WARN(fmt, ...)                // logs a Warn
TEST_INFO(fmt, ...)                // logs an Info
```

`TEST_FAIL` replaces an immediate `return N;` at a failure site. `TEST_FAILV` is for a failure
that can't unwind right away -- inside a callback, or an accumulator variable (`ret = 1;`) that a
test checks after cleanup runs.

```c
if (val != expected)
    TEST_FAIL(3, _SL("expected ${int}, got ${int}"), stvar(int32, expected), stvar(int32, val));
```

**Format tokens are generic, not sized.** Use `${int}`, `${uint}`, `${string}`, etc. -- not
`${int32}` or similar. A sized token isn't recognized by the formatter and silently renders
empty, which looks like the log call worked but drops the value.

## `logtest.c`'s self-tests

`logtest.c` exercises the logging system itself, including `logShutdown()`/`logRestart()`. Most
of its tests call `logShutdown()` only as their last statement, so the harness's own destination
simply disappears right as the subtest ends -- nothing is lost.

Two tests (`test_log_hierarchy`, `test_log_groups`) call `logRestart()` **mid-test**, to check
that channels/groups survive a shutdown/restart cycle. From that point until the *next* subtest's
harness setup runs, the harness's destination is gone, so any `TEST_FAIL`/`TEST_WARN` logged
later in that same function would be silently dropped even with `-log=` requested. Both call
`cxTestHarnessReattach()` right after their internal `logRestart()` to restore it for the rest of
the function -- do the same if a future test needs to shut down and restart logging partway
through.

## How it's wired

`tests/testharness.c` implements `cxTestHarnessBefore()`/`cxTestHarnessAfter()`, spliced into the
CMake-generated `test_runner.c` driver via `CMAKE_TESTDRIVER_BEFORE_TESTMAIN`/
`_AFTER_TESTMAIN` in `tests/CMakeLists.txt`. This wraps the single-test dispatch path CTest uses
(`test_runner <group> <subtest>`), and since `alltests.c` calls the same generated `main()` once
per entry, every iteration of an `alltests` run too. It does not wrap the interactive `-A`/manual
modes, since CTest and `alltests` don't use them.
