#pragma once

#include <cx/fs/fs_private.h>

// NT native path for windows API, returns scratch buffer!
wchar_t* fsPathToNT(string path);
