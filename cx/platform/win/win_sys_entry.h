#pragma once

#include <cx/cx.h>
#include <cx/platform/win.h>

extern int entryCmdShow;

#if defined(SUBSYSTEM_WINDOWS)
// Windows subsystem

#if defined(_UNICODE)
#define _CX_DEFINE_ENTRY_POINT_RUNNER(runner)                                                   \
    int WINAPI                                                                                  \
    wWinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE hPrev, _In_ PWSTR cmdline, _In_ int show) \
    {                                                                                           \
        entryCmdShow = show;                                                                    \
        _entryParseArgsU16(__argc, (const uint16**)__wargv);                                    \
        return runner(entryPoint);                                                              \
    }
#else
#define _CX_DEFINE_ENTRY_POINT_RUNNER(runner)                                                 \
    int WINAPI                                                                                \
    WinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE hPrev, _In_ PSTR cmdline, _In_ int show) \
    {                                                                                         \
        entryCmdShow = show;                                                                  \
        _entryParseArgs(__argc, (const char**)__argv);                                        \
        return runner(entryPoint);                                                            \
    }
#endif

#else
// Console subsystem

#if defined(_UNICODE)
#define _CX_DEFINE_ENTRY_POINT_RUNNER(runner)            \
    int wmain(int argc, wchar_t* wargv[])                \
    {                                                    \
        _entryParseArgsU16(argc, (const uint16**)wargv); \
        return runner(entryPoint);                       \
    }
#else
#define _CX_DEFINE_ENTRY_POINT_RUNNER(runner)      \
    int main(int argc, char* argv[])               \
    {                                              \
        _entryParseArgs(argc, (const char**)argv); \
        return runner(entryPoint);                 \
    }
#endif

#endif
