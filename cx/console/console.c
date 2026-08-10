#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "console_private.h"

#include <cx/debug/assert.h>
#include <cx/thread/thread.h>
#include <cx/utils/lazyinit.h>

static ConStream* streamAlloc(ConKind kind)
{
    ConStream* con = xaAllocStruct(ConStream, XA_Zero);
    con->kind      = kind;
    mutexInit(&con->lock);

    if (kind != CON_Kind_Mem) {
        con->bufsz = CONBUF_DEFAULT_SIZE;
        con->buf   = xaAlloc(con->bufsz);
    }

    return con;
}

static LazyInitState g_outInit;
static ConStream* g_out;
static LazyInitState g_errInit;
static ConStream* g_err;
static LazyInitState g_inInit;
static ConStream* g_in;

static void initOut(void* unused)
{
    (void)unused;
    g_out = streamAlloc(CON_Kind_Out);
    _conPlatInit(g_out, CON_Kind_Out);
}

static void initErr(void* unused)
{
    (void)unused;
    g_err = streamAlloc(CON_Kind_Err);
    _conPlatInit(g_err, CON_Kind_Err);
}

static void initIn(void* unused)
{
    (void)unused;
    g_in = streamAlloc(CON_Kind_In);
    _conPlatInit(g_in, CON_Kind_In);
}

_Use_decl_annotations_
ConStream* conOut(void)
{
    lazyInit(&g_outInit, initOut, NULL);
    return g_out;
}

_Use_decl_annotations_
ConStream* conErr(void)
{
    lazyInit(&g_errInit, initErr, NULL);
    return g_err;
}

_Use_decl_annotations_
ConStream* conIn(void)
{
    lazyInit(&g_inInit, initIn, NULL);
    return g_in;
}

_Use_decl_annotations_
void conShutdown(void)
{
    if (g_out) {
        if (g_out->styleActive)
            conResetStyle(g_out);
        conFlush(g_out);
        _conPlatShutdown(g_out);
    }
    if (g_err) {
        if (g_err->styleActive)
            conResetStyle(g_err);
        conFlush(g_err);
        _conPlatShutdown(g_err);
    }
    if (g_in)
        _conPlatShutdown(g_in);
}

_Use_decl_annotations_
ConStream* conCreateMem(ConCaps* caps)
{
    ConStream* con    = streamAlloc(CON_Kind_Mem);
    con->caps         = *caps;
    con->linebuffered = false;
    con->autoflush    = false;
    return con;
}

_Use_decl_annotations_
void conMemGet(ConStream* con, string* out)
{
    conLock(con);
    strDestroy(out);
    strDup(out, con->memcapture);
    conUnlock(con);
}

_Use_decl_annotations_
void conDestroy(ConStream** con)
{
    if (!con || !*con)
        return;

    ConStream* c = *con;
    devAssertMsg(
        c->kind == CON_Kind_Mem,
        "conDestroy() may only be called on a stream created by conCreateMem(). conOut()/conErr()/conIn() are never destroyed!");

    strDestroy(&c->memcapture);
    if (c->buf)
        xaFree(c->buf);
    mutexDestroy(&c->lock);
    xaFree(c);
    *con = NULL;
}

_Use_decl_annotations_
void conGetCaps(ConStream* con, ConCaps* out)
{
    conLock(con);
    if (con->kind != CON_Kind_Mem)
        _conPlatQuerySize(con);
    *out = con->caps;
    conUnlock(con);
}

_Use_decl_annotations_
uint16 conWidth(ConStream* con)
{
    ConCaps caps;
    conGetCaps(con, &caps);
    return caps.width;
}

_Use_decl_annotations_
uint16 conHeight(ConStream* con)
{
    ConCaps caps;
    conGetCaps(con, &caps);
    return caps.height;
}

_Use_decl_annotations_
void conLock(ConStream* con)
{
    intptr self = thrCurrentOSThreadID();

    // self != 0 guards the (unverified-but-assumed-impossible) case where an OS thread id
    // is itself 0, which is also the sentinel this module uses for "unlocked" -- without
    // the guard such a thread would misread an unlocked stream as already owned by itself
    // and skip mutexAcquire() entirely.
    if (self != 0 && atomicLoad(intptr, &con->owner, Relaxed) == self) {
        ++con->depth;
        return;
    }

    mutexAcquire(&con->lock);
    atomicStore(intptr, &con->owner, self, Relaxed);
    con->depth = 1;
}

_Use_decl_annotations_
void conUnlock(ConStream* con)
{
    devAssert(con->depth > 0);
    if (--con->depth > 0)
        return;

    atomicStore(intptr, &con->owner, 0, Relaxed);
    mutexRelease(&con->lock);
}
