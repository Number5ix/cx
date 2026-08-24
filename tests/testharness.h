#pragma once

#include <cx/log/log.h>

#ifdef __cplusplus
extern "C" {
#endif

// The channel every test file logs to; bound once by common.h via LOG_CHANNEL.
extern LogChannel* cxTestLogChan;

// Resolves the requested verbosity from a trailing "-log=<Level>" flag in av (else
// CX_TEST_LOGLEVEL from the environment), and if one was requested, registers a console
// destination that sends everything at that level to stderr. Called from the spliced
// CMAKE_TESTDRIVER_BEFORE_TESTMAIN code, which has the generated main()'s ac/av in scope --
// already reduced to the subtest's own argv, so a trailing flag can appear anywhere from av[1]
// onward and must be scanned for rather than read at a fixed index.
void cxTestHarnessBefore(int ac, char* av[]);

// Flushes and shuts down the logging system after a subtest runs. Safe even if the subtest
// already shut the log system down itself.
void cxTestHarnessAfter(void);

// Re-registers the harness's console destination at the level resolved by the last
// cxTestHarnessBefore() call, without re-parsing flags or the environment. Needed only by tests
// that call logShutdown()/logRestart() themselves partway through (see logtest.c), since that
// drops the harness's destination before the subtest has finished.
void cxTestHarnessReattach(void);

#ifdef __cplusplus
}
#endif
