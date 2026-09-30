#pragma once

#include "cx/cx.h"

#define DEFINE_ENTRY_POINT                         \
    int main(int argc, char* argv[])               \
    {                                              \
        _entryParseArgs(argc, (const char**)argv); \
        return entryPoint();                       \
    }
