#include "entry.h"
#include <cx/container/sarray.h>
#include <cx/string.h>

string *cmdArgs = 0;

void _entryParseArgs(int argc, const char **argv)
{
    saDestroy(&cmdArgs);
    cmdArgs = saCreate(string, 1, 0);
    for (int i = 0; i < argc; i++) {
        saPush(&cmdArgs, string, (char*)argv[i], 0);
    }
}

void _entryParseArgsU16(int argc, const uint16 **argv)
{
    saDestroy(&cmdArgs);
    cmdArgs = saCreate(string, 1, 0);

    string temp = 0;
    for (int i = 0; i < argc; i++) {
        strFromUTF16(&temp, argv[i], cstrLenw(argv[i]));
        saPush(&cmdArgs, string, temp, 0);
    }
    strDestroy(&temp);
}
