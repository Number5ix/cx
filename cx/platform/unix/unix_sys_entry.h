#pragma once

#include "cx/cx.h"

#define _CX_DEFINE_ENTRY_POINT_RUNNER(runner)      \
    int main(int argc, char* argv[])               \
    {                                              \
        _entryParseArgs(argc, (const char**)argv); \
        return runner(entryPoint);                 \
    }
