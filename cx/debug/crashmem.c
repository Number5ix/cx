#include "crash_private.h"

static LazyInitState crashMemInitState;
CrashMemRange *_dbgCrashDumpMem;

static intptr crashMemCmp(stype st, const void *a, const void *b, uint32 flags)
{
    CrashMemRange *r1 = (CrashMemRange*)a;
    CrashMemRange *r2 = (CrashMemRange*)b;

    if (r1->start < r2->start)
        return -1;
    if (r1->start > r2->start)
        return 1;
    return 0;
}

static STypeOps crashMemOps = {
    .cmp = crashMemCmp
};

static void crashMemInit(void *data)
{
    _dbgCrashDumpMem = saCreate(custom(opaque(CrashMemRange), crashMemOps), 10, SA_Sorted | saGrow(Slow));
}

void dbgCrashIncludeMemory(void *ptr, size_t sz)
{
    // removing any completely contained blocks first makes the
    // add algorithm considerably simpler
    dbgCrashExcludeMemory(ptr, sz);

    CrashMemRange r = { .start = (uintptr)ptr,.end = (uintptr)ptr + sz };
    int32 idx = saFind(&_dbgCrashDumpMem, opaque, r, SA_Inexact);

    if (idx > 0 && _dbgCrashDumpMem[idx - 1].end == r.start) {
        // extend the previous block
        _dbgCrashDumpMem[idx - 1].end = r.end;
    } else if (idx >= 0 && idx < saSize(&_dbgCrashDumpMem) && _dbgCrashDumpMem[idx].start == r.end) {
        // extend the following block
        _dbgCrashDumpMem[idx].start = r.start;
    } else {
        // insert a new block
        saPush(&_dbgCrashDumpMem, opaque, r, SA_Unique);
    }
}

void dbgCrashExcludeMemory(void *ptr, size_t sz)
{
    lazyInit(&crashMemInitState, crashMemInit, 0);

    int32 idx;
    CrashMemRange r = { .start = (uintptr)ptr,.end = (uintptr)ptr + sz };

    // use bsearch to quickly get to the starting index to scan
    idx = saFind(&_dbgCrashDumpMem, opaque, r, SA_Inexact);
    // back up one index entry to catch partial overlap
    idx = max(idx - 1, 0);

    for (; idx < saSize(&_dbgCrashDumpMem); idx++) {
        CrashMemRange *ir = &_dbgCrashDumpMem[idx];
        if (r.start <= ir->start && r.end >= ir->end) {
            // completely inside removal range, delete it
            saRemove(&_dbgCrashDumpMem, idx--, 0);
        } else if (r.start <= ir->start && r.end > ir->start) {
            // cut off the start of the range
            ir->start = r.end;
        } else if (r.start < ir->end && r.end >= ir->end) {
            // cut off the end of the range
            ir->end = r.start;
        } else if (r.start > ir->start && r.end < ir->end) {
            // have to split the block
            CrashMemRange nr;
            nr.start = r.end;
            nr.end = ir->end;
            ir->end = r.start;

            int32 nidx = saPush(&_dbgCrashDumpMem, opaque, nr, SA_Unique);
            devAssert(nidx == idx + 1);
        } else if (r.end <= ir->start) {
            // beyond what we're looking for
            break;
        }
    }
}
