#include "ccallbacks.h"
#include <cx/thread.h>

bool ccbSignalEvent(stvlist* cvars, stvlist* args)
{
    Event* ev = stvlNextPtr(cvars);
    if (!ev)
        return false;

    eventSignal(ev);
    return true;
}
 