#include "scratch.h"

_Thread_local ScratchPerThreadInfo *_scratch_pti;

extern inline void *scratchGet(size_t sz);
