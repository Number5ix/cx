#include <cx/platform/ksema.h>
#include <cx/time.h>
#include <semaphore.h>

bool kernelSemaInit(kernelSema *sema, int32 count)
{
    sem_t *sem = (sem_t*)sema;
    return !sem_init(sem, 0, count);
}

bool kernelSemaDestroy(kernelSema *sema)
{
    sem_t *sem = (sem_t*)sema;
    return !sem_destroy(sem);
}

bool kernelSemaDec(kernelSema *sema)
{
    sem_t *sem = (sem_t*)sema;
    return !sem_wait(sem);
}

bool kernelSemaTryDec(kernelSema *sema)
{
    sem_t *sem = (sem_t*)sema;
    return !sem_trywait(sem);
}

bool kernelSemaTryDecTimeout(kernelSema *sema, int64 timeout)
{
    sem_t *sem = (sem_t*)sema;
    struct timespec to;

    timeToRelTimespec(&to, timeout);
    return !sem_timedwait(sem, &to);
}

bool kernelSemaInc(kernelSema *sema, int32 count)
{
    sem_t *sem = (sem_t*)sema;
    bool ret = true;
    for (int32 i = 0; i < count; i++) {
        ret &= !sem_post(sem);
    }
    return ret;
}
