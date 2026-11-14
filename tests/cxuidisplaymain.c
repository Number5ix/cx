// Runs the cxuidisplaytest subtests on macOS, where AppKit needs the main thread and only
// CXUI_DEFINE_ENTRY_POINT hands it over; test_runner can't. Run it from a terminal in the desktop
// session, as "cxuidisplay <subtest> [-log=<level>]"; the exit code is the subtest's.

#include <cxui.h>
#include <cxui/entry.h>
#include <stdio.h>
#include "testharness.h"

CXUI_DEFINE_ENTRY_POINT;

int cxuidisplaytest(int argc, char* argv[]);

// tqtestobj.c's task fixtures expect tqtest.c's ordering counter, which isn't linked here.
atomic(int32) tqtests_order;

int entryPoint(void)
{
    char* argv[8] = { "cxuidisplay" };
    int argc      = 1;
    for (int32 i = 0; i < saSize(cmdArgs) && argc < 8; i++)
        argv[argc++] = (char*)strC(cmdArgs.a[i]);
    if (argc < 2) {
        printf("usage: cxuidisplay <subtest> [-log=<level>]\n");
        return 1;
    }

    cxTestHarnessBefore(argc, argv);
    int ret = cxuidisplaytest(argc, argv);
    cxTestHarnessAfter();
    return ret;
}
