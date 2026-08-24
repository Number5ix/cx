// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "logfile.h"
#include <cx/container.h>
#include <cx/format.h>
#include <cx/fs/path.h>
#include <cx/string.h>
#include <cx/time.h>
#include <cx/utils.h>

// string constants
STR_CONST(kLogExtDefault, "log");
STR_CONST(kLogRotatePattern, ".*.");
STR_CONST(kLogSplitDelim, ".");
STR_CONST(kLogRotateFmt, "${string}/${string}.${int}.${string}");

typedef struct LogFileData {
    LogFileConfig config;
    LogSerializer* ser;   // owned; how a record becomes the bytes this file stores
    VFS* vfs;
    string fname;
    string pathname;
    string basename;
    string ext;
    VFSFile* curfile;
    int numseen;
    int64 lastrotate;
    int64 cursize;
} LogFileData;

#ifdef _PLATFORM_WIN
STR_CONST(loglineend, "\r\n");
#else
STR_CONST(loglineend, "\n");
#endif

static void deleteOldFiles(_Inout_ LogFileData* lfd);

static void logfileDestroy(_Pre_valid_ _Post_invalid_ LogFileData* data)
{
    logSerializerDestroy(&data->ser);
    vfsClose(data->curfile);
    objRelease(&data->vfs);
    strDestroy(&data->fname);
    strDestroy(&data->pathname);
    strDestroy(&data->basename);
    strDestroy(&data->ext);
    xaFree(data);
}

static bool logfileOpen(_Inout_ LogFileData* data)
{
    devAssert(!data->curfile);
    data->curfile = vfsOpen(data->vfs, data->fname, FS_Create | FS_Write);
    if (!data->curfile)
        return false;
    vfsSeek(data->curfile, 0, FS_End);
    data->cursize = vfsTell(data->curfile);

    return true;
}

static bool logfileClose(_Inout_ LogFileData* data)
{
    devAssert(data->curfile);
    vfsClose(data->curfile);
    data->curfile = NULL;

    return true;
}

_Use_decl_annotations_
LogFileData* logfileCreate(VFS* vfs, strref filename, LogFileConfig* config, LogSerializer* ser)
{
    LogFileData* ret = xaAlloc(sizeof(LogFileData), XA_Zero);
    string realfile  = 0;
    if (!ret) {
        logSerializerDestroy(&ser);
        return NULL;
    }

    vfsAbsolutePath(vfs, &realfile, filename);

    ret->config = *config;
    // ownership transfers here, including on the failure paths below, so that a caller can
    // always write logfileCreate(..., logTextSerializer(&tcfg)) without leaking on failure
    ret->ser    = ser ? ser : logTextSerializer(NULL);
    ret->vfs    = objAcquire(vfs);
    strDup(&ret->fname, realfile);

    // save path breakdown to make rotation easier
    pathGetExt(&ret->ext, realfile);
    pathParent(&ret->pathname, realfile);
    pathFilename(&ret->basename, realfile);
    pathRemoveExt(&ret->basename, ret->basename);
    strDestroy(&realfile);

    if (strEmpty(ret->ext)) {
        pathAddExt(&ret->fname, ret->fname, kLogExtDefault);
        strDup(&ret->ext, kLogExtDefault);
    }

    // for time-based rotation need to figure out the last rotate date based on
    // the modify timestamp to make sure it works correctly across sessions
    FSStat stat;
    if (config->rotateMode == LOG_RotateTime && vfsStat(vfs, ret->fname, &stat) == FS_File) {
        if (config->flags & LOG_LocalTime)
            stat.modified = timeLocal(stat.modified, NULL);

        // calculate the last rotation time before the file's modify timestamp
        TimeParts tp;
        timeDecompose(&tp, stat.modified);
        tp.hour         = config->rotateHour;
        tp.minute       = config->rotateMinute;
        tp.second       = config->rotateSecond;
        ret->lastrotate = timeCompose(&tp);

        // if the file was modified before the rotation time, assume it must have been rotated the
        // previous day
        if (stat.modified < ret->lastrotate)
            ret->lastrotate -= timeS(86400);
    }

    if (!logfileOpen(ret)) {
        logfileDestroy(ret);
        return NULL;
    }

    // clear out any old rotated files
    deleteOldFiles(ret);

    return ret;
}

_Use_decl_annotations_
static void deleteOldFiles(LogFileData* lfd)
{
    FSSearchIter fsi;
    string pattern = 0, temp = 0;
    sa_string todelete, splits;

    // now must be in UTC because it's being compared to the file timestamps
    int64 now = clockWall();

    strNConcat(&pattern, lfd->basename, kLogRotatePattern, lfd->ext);
    saInit(&todelete, string, 4);
    saInit(&splits, string, 4);

    // count the number of rotated (NON-DATE) files so that unlimited retention can work...
    // unlimited up to 10k files anyway
    lfd->numseen = 0;

    vfsSearchInit(&fsi, lfd->vfs, lfd->pathname, pattern, FS_File, true);
    while (vfsSearchValid(&fsi)) {
        if (lfd->config.rotateKeepTime > 0 &&
            (now - fsi.stat.modified) > lfd->config.rotateKeepTime) {
            saPush(&todelete, string, fsi.name);
        } else if (strSplit(&splits, fsi.name, kLogSplitDelim, false) == 3) {
            int num;
            if (strToInt32(&num, splits.a[1], 10, STRNUM_NoTrailing) && num < 10000) {
                // this is a numbered file, not one with a date
                // deletion is usually handled at rotation time, but check anyway in case
                // the config changed
                if (lfd->config.rotateKeepFiles > 0 && num > lfd->config.rotateKeepFiles) {
                    saPush(&todelete, string, fsi.name);
                } else {
                    lfd->numseen = max(lfd->numseen, num);
                }
            }
        }
        vfsSearchNext(&fsi);
    }
    vfsSearchFinish(&fsi);

    foreach (sarray, idx, string, fn, todelete) {
        pathJoin(&temp, lfd->pathname, fn);
        vfsDelete(lfd->vfs, temp);
    }

    saDestroy(&todelete);
    saDestroy(&splits);
    strDestroy(&pattern);
    strDestroy(&temp);
}

static void doSizeRotation(_Inout_ LogFileData* lfd)
{
    if (lfd->config.rotateSize == 0 || lfd->cursize < lfd->config.rotateSize)
        return;   // file isn't big enough yet

    logfileClose(lfd);
    deleteOldFiles(lfd);

    int nfiles = lfd->config.rotateKeepFiles > 0 ? lfd->config.rotateKeepFiles : lfd->numseen + 1;

    string namei = 0, nameimo = 0;
    for (int i = nfiles; i >= 1; --i) {
        strFormat(&namei,
                  kLogRotateFmt,
                  stvar(string, lfd->pathname),
                  stvar(string, lfd->basename),
                  stvar(int32, i),
                  stvar(string, lfd->ext));
        if (i > 1) {
            strFormat(&nameimo,
                      kLogRotateFmt,
                      stvar(string, lfd->pathname),
                      stvar(string, lfd->basename),
                      stvar(int32, i - 1),
                      stvar(string, lfd->ext));
        } else {
            strDup(&nameimo, lfd->fname);
        }

        if (vfsExist(lfd->vfs, namei))
            vfsDelete(lfd->vfs, namei);
        if (vfsExist(lfd->vfs, nameimo))
            vfsRename(lfd->vfs, nameimo, namei);
    }
    logfileOpen(lfd);

    strDestroy(&namei);
    strDestroy(&nameimo);
}

static void doTimeRotation(_Inout_ LogFileData* lfd)
{
    int64 lastrotate = lfd->lastrotate;

    // now must be in local time because that's how rotateTime is specified when LOG_LocalTime is
    // set
    int64 now = (lfd->config.flags & LOG_LocalTime) ? clockWallLocal() : clockWall();

    // figure out next rotation time, which will time from the config
    // on the day after the last rotation
    TimeParts tp;
    timeDecompose(&tp, lastrotate + timeS(86400));

    tp.hour          = lfd->config.rotateHour;
    tp.minute        = lfd->config.rotateMinute;
    tp.second        = lfd->config.rotateSecond;
    int64 nextrotate = timeCompose(&tp);

    if (now < nextrotate)
        return;   // not time to rotate yet

    logfileClose(lfd);

    // use last rotation time for date in filename
    timeDecompose(&tp, lastrotate);
    deleteOldFiles(lfd);
    lfd->lastrotate = now;

    string rfname = 0;
    strFormat(&rfname,
              kLogRotateFmt,
              stvar(string, lfd->pathname),
              stvar(string, lfd->basename),
              stvar(int32, tp.year * 10000 + tp.month * 100 + tp.day),
              stvar(string, lfd->ext));
    vfsRename(lfd->vfs, lfd->fname, rfname);
    strDestroy(&rfname);

    logfileOpen(lfd);
}

static void checkRotate(_Inout_ LogFileData* lfd)
{
    if (lfd->config.rotateMode == LOG_RotateSize)
        doSizeRotation(lfd);
    else if (lfd->config.rotateMode == LOG_RotateTime)
        doTimeRotation(lfd);
}

// this function is always called from the log thread and does not need to worry about concurrency
_Use_decl_annotations_
void logfileMsgFunc(const LogRecord* rec, void* userdata)
{
    LogFileData* lfd = (LogFileData*)userdata;
    if (!lfd)
        return;

    // the serializer decides what a record looks like; this transport only decides where it goes
    // and how records are separated
    string logline = 0;
    logSerialize(&logline, lfd->ser, rec);
    strAppend(&logline, loglineend);

    vfsWrite(lfd->curfile, (void*)strC(logline), strLen(logline), NULL);
    lfd->cursize += strLen(logline);
    strDestroy(&logline);
}

_Use_decl_annotations_
void logfileBatchFunc(uint32 batchid, void* userdata)
{
    LogFileData* lfd = (LogFileData*)userdata;
    if (!lfd)
        return;

    // do log file rotation between batches
    checkRotate(lfd);
}

_Use_decl_annotations_
void logfileCloseFunc(void* userdata)
{
    LogFileData* lfd = (LogFileData*)userdata;
    if (!lfd)
        return;

    // we're being asked to close the log and quit
    logfileDestroy(lfd);
}

_Use_decl_annotations_
LogDest* logfileRegister(int maxlevel, strref chanfilter, VFS* vfs, strref filename,
                         LogFileConfig* config, LogSerializer* ser)
{
    // logfileCreate consumes the serializer even when it fails, so there is nothing to clean up
    // here on this path
    LogFileData* lfd = logfileCreate(vfs, filename, config, ser);
    if (!lfd)
        return NULL;

    LogDest* ret = logRegisterDest(maxlevel,
                                   chanfilter,
                                   logfileMsgFunc,
                                   logfileBatchFunc,
                                   logfileCloseFunc,
                                   lfd);

    // the destination owns the file once it is registered; if registration failed, nothing ever
    // will, so close it here rather than leaving the handle open
    if (!ret)
        logfileCloseFunc(lfd);

    return ret;
}
