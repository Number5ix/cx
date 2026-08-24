#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cx/log.h>

#include "testharness.h"

// Every test file logs to the same channel with no per-file setup.
#undef LOG_CHANNEL
#define LOG_CHANNEL cxTestLogChan

// void TEST_FAIL(code, fmt, ...);
//
// Logs an Error and returns code -- for a failure site that can return immediately.
//
// @param code Value to return
// @param fmt Format template (see strFormat); use the generic type tokens (${int}, ${string},
//            ...), not sized ones like ${int32}, which silently render empty
//
// Example:
// @code
//   if (val != expected)
//       TEST_FAIL(3, _SL("expected ${int}, got ${int}"), stvar(int32, expected), stvar(int32, val));
// @endcode
#define TEST_FAIL(code, fmt, ...) \
    do { \
        logFmt(Error, fmt, ##__VA_ARGS__); \
        return (code); \
    } while (0)

// void TEST_FAILV(var, code, fmt, ...);
//
// Logs an Error and assigns code to var, without returning -- for a deferred failure site (a
// callback, or an accumulator variable checked after cleanup runs) that can't return immediately.
//
// @param var Variable to assign code to
// @param code Value to assign
// @param fmt Format template (see strFormat); use the generic type tokens, not sized ones
//
// Example:
// @code
//   if (val != expected)
//       TEST_FAILV(ret, 1, _SL("expected ${int}, got ${int}"), stvar(int32, expected), stvar(int32, val));
// @endcode
#define TEST_FAILV(var, code, fmt, ...) \
    do { \
        logFmt(Error, fmt, ##__VA_ARGS__); \
        (var) = (code); \
    } while (0)

// void TEST_WARN(fmt, ...);
//
// Logs a Warn message. Use the generic format type tokens (${int}, ${string}, ...), not sized
// ones like ${int32}, which silently render empty.
#define TEST_WARN(fmt, ...) logFmt(Warn, fmt, ##__VA_ARGS__)

// void TEST_INFO(fmt, ...);
//
// Logs an Info message. Use the generic format type tokens (${int}, ${string}, ...), not sized
// ones like ${int32}, which silently render empty.
#define TEST_INFO(fmt, ...) logFmt(Info, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* name;
    int(*func)();
} testfunc;

extern testfunc TEST_FUNCS[];

int TEST_FILE(int argc, char *argv[])
{
    char buf[64];
    testfunc *f;
    char *t = 0;
    int ntests = 0, testnum = 0;

    if (argc < 2) {
        printf("Available tests:\n");
        for (f = TEST_FUNCS; f->name; ++f) {
            printf("%3d. %s\n", ntests, f->name);
            ++ntests;
        }
        printf("To run a test, enter the test number: ");
        fflush(stdout);
        do {
            if (!fgets(buf, 64, stdin))
                return -1;
        } while (buf[0] == '\n' || buf[0] == '\r');
        testnum = strtol(buf, &t, 10);
        if (t == buf) {
            printf("Couldn't parse that input as a number\n");
            return -1;
        }
        if (testnum < 0 || testnum >= ntests) {
            printf("%3d is an invalid test number.\n", testnum);
            return -1;
        }
        return TEST_FUNCS[testnum].func();
    }

    for (f = TEST_FUNCS; f->name; ++f) {
        if (!strcmp(argv[1], f->name)) {
            return f->func();
        }
    }
    printf("Invalid test name!\n");
    return -1;
}

#ifdef __cplusplus
}
#endif
