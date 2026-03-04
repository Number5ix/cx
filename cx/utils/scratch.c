#include "scratch.h"

_Thread_local ScratchPerThreadInfo* _scratch_pti;

_Use_decl_annotations_
extern inline void* scratchGet(size_t sz);

static void scratchThreadFinish(void* unused)
{
    if (_scratch_pti) {
        for (int i = 0; i < SCRATCH_NBUFFERS; ++i) {
            if (_scratch_pti->buf[i]) {
                xaFree(_scratch_pti->buf[i]);
            }
        }
        xaFree(_scratch_pti);
        _scratch_pti = NULL;
    }
}

_Ret_notnull_ void* scratchGet(size_t sz)
{
    if (!_scratch_pti) {
        _scratch_pti = (ScratchPerThreadInfo*)xaAlloc(sizeof(ScratchPerThreadInfo), XA_Zero);
        thrRegisterCleanup(scratchThreadFinish, NULL);
    }

    int32 cur          = _scratch_pti->next;
    _scratch_pti->next = (_scratch_pti->next + 1) & (SCRATCH_NBUFFERS - 1);

    sz = clamplow(sz, SCRATCH_MIN_BUFFER_SIZE);
    if (!_scratch_pti->buf[cur]) {
        _scratch_pti->buf[cur] = xaAlloc(sz);
    } else {
        size_t cursz = xaSize(_scratch_pti->buf[cur]);

        if (cursz < sz ||
            (cursz > SCRATCH_MAX_REASONABLE_BUFFER_SIZE &&
             sz < SCRATCH_MAX_REASONABLE_BUFFER_SIZE)) {
            // xaResize would copy data we don't care about
            xaFree(_scratch_pti->buf[cur]);
            _scratch_pti->buf[cur] = xaAlloc(sz);
        }
    }

    return _scratch_pti->buf[cur];
}
