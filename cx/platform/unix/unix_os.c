#include "cx/platform/os.h"
#include <pthread.h>

void osYield()
{
    pthread_yield();
}
