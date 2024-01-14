#include "log_private.h"
#include "logdefer.h"

typedef struct LogDeferData {
    LogEntry *head;
    LogEntry *tail;
} LogDeferData;

_Use_decl_annotations_
LogDeferData *logDeferCreate(void)
{
    LogDeferData *ret = xaAllocStruct(LogDeferData, XA_Zero);
    return ret;
}

_Use_decl_annotations_
void logDeferDest(int level, LogCategory *cat, int64 timestamp, strref msg, void *userdata)
{
    LogDeferData *dd = (LogDeferData *)userdata;
    if (!dd || level == -1)
        return;

    LogEntry *ent = xaAllocStruct(LogEntry, XA_Zero);
    ent->level = level;
    ent->cat = cat;
    ent->timestamp = timestamp;
    strDup(&ent->msg, msg);
    if (!dd->head)
        dd->head = ent;
    if (dd->tail)
        dd->tail->_next = ent;
    dd->tail = ent;
}

_Use_decl_annotations_
LogDest *logRegisterDestWithDefer(int maxlevel, LogCategory *catfilter, LogDestFunc dest, void *userdata, LogDest *deferdest)
{
    logCheckInit();

    LogDest *ndest = xaAlloc(sizeof(LogDest), XA_Zero);

    ndest->maxlevel = maxlevel;
    ndest->catfilter = catfilter;
    ndest->func = dest;
    ndest->userdata = userdata;

    // swap out the new destination for the deferred one
    withMutex(&_log_dests_lock) {
        saPush(&_log_dests, ptr, ndest);
        logUnregisterDestLocked(deferdest);     // will recalculate maxlevel cache

        // keep the lock held to ensure that the writer thread doesn't send anything to the new dest before
        // the deferred entries are flushed, and also to preserve the guarantee that LogDestFunc callbacks will
        // never be run from two threads at once.

        // sanity check
        if (deferdest->userdata && deferdest->func == logDeferDest) {
            LogDeferData *dd = (LogDeferData *)deferdest->userdata;
            LogEntry *head = dd->head;
            dd->tail = NULL;
            dd->head = NULL;

            // go through linked list and send them all to the real destination
            LogEntry *next;
            while (head) {
                next = head->_next;
                if (head->level <= maxlevel && applyCatFilter(catfilter, head->cat))
                    dest(head->level, head->cat, head->timestamp, head->msg, userdata);
                logDestroyEnt(head);
                head = next;
            }

            xaFree(dd);
        }
    }

    xaFree(deferdest);

    return ndest;
}
