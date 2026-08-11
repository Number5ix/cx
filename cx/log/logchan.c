// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/container/foreach.h>
#include <cx/string.h>

// Channel registry and path matching.
//
// A channel's identity is its path, interned in a process-wide registry. Channels are permanent:
// they are never freed and the registry survives logShutdown()/logRestart(), which is what makes
// it safe for a call site to cache the pointer forever. Every ancestor along a path is interned
// too, so the parent chain -- and therefore the visibility gate depth -- is known at intern time
// and never has to be recomputed per message.

STR_CONST(kChanSep, "/");
STR_CONST(kChanWildOne, "*");
STR_CONST(kChanWildAll, "**");
STR_CONST(kChanCX, "cx");

// the root channel is a plain static so that LogDefault is a valid pointer before the log system
// has initialized; a log call that beats lazy init still has somewhere to point
static LogChannel _log_root;
LogChannel* LogDefault = &_log_root;

hashtable _log_channels;   // path -> LogChannel*
sa_LogChannel _log_chans;  // indexed by LogChannel.idx

static uint32 logChanDepth(_In_ LogChannel* chan)
{
    uint32 depth = 0;
    for (LogChannel* c = chan; c && c->parent; c = c->parent)
        ++depth;
    return depth;
}

// A restriction is a gate at one node: the deepest restricted ancestor-or-self, cleared back to
// 0 by a LOG_Broadcast declaration further down. A rule reaches the channel only if it names
// that node in its literal, wildcard-free prefix.
static void logChanUpdateGate(_Inout_ LogChannel* chan)
{
    if (chan->flags & LOG_Restricted)
        chan->gatedepth = logChanDepth(chan);
    else if ((chan->flags & LOG_Broadcast) || !chan->parent)
        chan->gatedepth = 0;
    else
        chan->gatedepth = chan->parent->gatedepth;
}

static void logChanUpdateGateAll(void)
{
    // channels are appended in creation order and a parent is always created before its
    // children, so one forward pass resolves the whole tree
    foreach (sarray, idx, LogChannel*, chan, _log_chans) {
        logChanUpdateGate(chan);
    }
}

static _Ret_valid_ LogChannel* logChanInternLocked(_In_opt_ strref path, flags_t flags,
                                                   bool declare, _Out_ bool* created)
{
    LogChannel* chan = NULL;
    if (htFind(_log_channels, strref, path, ptr, &chan)) {
        if (declare) {
            chan->flags = flags;
            logChanUpdateGateAll();
        }
        *created = false;
        return chan;
    }

    // intern the parent first so the chain is complete before the channel is reachable
    LogChannel* parent = NULL;
    int32 sep          = strFindCharR(path, strEnd, '/');
    if (sep >= 0) {
        string ppath = 0;
        strSubStr(&ppath, path, 0, sep);
        bool pcreated;
        parent = logChanInternLocked(ppath, 0, false, &pcreated);
        strDestroy(&ppath);
    } else if (!strEmpty(path)) {
        parent = &_log_root;
    }

    chan         = xaAllocStruct(LogChannel, XA_Zero);
    chan->parent = parent;
    chan->flags  = declare ? flags : 0;
    chan->idx    = saSize(_log_chans);
    strDup(&chan->path, path);
    atomicStore(int32, &chan->maxlevel, -1, Relaxed);
    atomicStore(int32, &chan->destlevel, -1, Relaxed);

    // A debug ring is inherited down the path, and the parent chain is complete by now, so a
    // channel interned under one is covered from its first record.
    if (parent)
        atomicStore(ptr, &chan->ring, atomicLoad(ptr, &parent->ring, Relaxed), Relaxed);
    logChanUpdateGate(chan);

    // The routing row is computed before the channel is published, so it is already correct by
    // the time anything can name the channel. Adding a channel never changes any other channel's
    // row, so this needs no new routing version unless the version has run out of room.
    logRoutingAddChan(chan);

    saPush(&_log_chans, ptr, chan);
    htInsert(&_log_channels, string, chan->path, ptr, chan);

    *created = true;
    return chan;
}

void logChanInit(void)
{
    // permanent for the lifetime of the process; only ever built once
    if (_log_channels)
        return;

    htInit(&_log_channels, string, ptr, 16);
    saInit(&_log_chans, ptr, 16);

    atomicStore(int32, &_log_root.maxlevel, -1, Relaxed);
    atomicStore(int32, &_log_root.destlevel, -1, Relaxed);
    _log_root.flags = LOG_Broadcast;
    saPush(&_log_chans, ptr, &_log_root);
    htInsert(&_log_channels, string, (string)_S "", ptr, &_log_root);

    // cx's own logging lives under `cx`, and the subtree is gated from the moment the registry
    // exists: a library's internal chatter is not the application's log. A destination reaches it
    // only by naming the gate literally -- `cx/**` or narrower -- so an application that has
    // registered nothing but a plain console sees exactly what it did before cx logged anything at
    // all, and one that wants the framework's view asks for it by name.
    //
    // Interned here rather than through logDeclareChan() because that path refuses to run before
    // _log_running is set, and this has to be in place before the first call site can name a
    // channel beneath it.
    bool created;
    logChanInternLocked(kChanCX, LOG_Restricted, true, &created);
}

static _Ret_opt_valid_ LogChannel* logChanIntern(_In_ strref path, flags_t flags, bool declare)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire))
        return NULL;

    LogChannel* chan;
    bool created = false;

    withMutex (&_log_op_lock) {
        chan = logChanInternLocked(path, flags, declare, &created);

        // a declaration can change which destinations match this channel and everything under
        // it, so the whole table has to be rebuilt; merely naming a channel cannot
        if (declare)
            logRoutingPublish();
    }

    return chan;
}

_Use_decl_annotations_
LogChannel* logChan(strref path)
{
    return logChanIntern(path, 0, false);
}

_Use_decl_annotations_
LogChannel* logDeclareChan(strref path, flags_t flags)
{
    // declaring a channel is the act of carving out a stream, so restricted is the default
    if (!(flags & (LOG_Broadcast | LOG_Restricted)))
        flags |= LOG_Restricted;

    return logChanIntern(path, flags, true);
}

// ---------------------------------------------------------------------------------------
// path matching
// ---------------------------------------------------------------------------------------

_Use_decl_annotations_
void logChanSplitPath(_Inout_ sa_string* out, strref path)
{
    // the separator is a single byte, so the character-set form scans for it directly instead of
    // running a substring search per component
    saInit(out, string, 8);
    strSplitAny(out, path, kChanSep, false);
}

_Use_decl_annotations_
uint32 logChanLitDepth(strref pattern)
{
    // number of path components before the first wildcard: ** is 0, net/** is 1, net/http/* is 2
    uint32 depth = 0;
    int32 pos    = 0;
    string comp  = 0;

    while (strSplitNextAny(pattern, &pos, kChanSep, &comp)) {
        if (strEmpty(comp))
            continue;   // the cursor form always yields empty segments; the array form skipped them
        if (strEq(comp, kChanWildOne) || strEq(comp, kChanWildAll))
            break;
        ++depth;
    }

    strDestroy(&comp);
    return depth;
}

static bool logChanMatchFrom(_In_ sa_string* pat, uint32 pi, _In_ sa_string* path, uint32 ci)
{
    uint32 np = saSize(*pat), nc = saSize(*path);

    while (pi < np) {
        if (strEq(pat->a[pi], kChanWildAll)) {
            // ** matches zero or more components, so net/** covers net and its whole subtree
            if (pi + 1 == np)
                return true;
            for (uint32 k = ci; k <= nc; k++) {
                if (logChanMatchFrom(pat, pi + 1, path, k))
                    return true;
            }
            return false;
        }

        if (ci >= nc)
            return false;
        if (!strEq(pat->a[pi], kChanWildOne) && !strEq(pat->a[pi], path->a[ci]))
            return false;

        ++pi;
        ++ci;
    }

    return ci == nc;
}

_Use_decl_annotations_
bool logChanMatch(strref pattern, strref path)
{
    sa_string pat, comps;
    logChanSplitPath(&pat, pattern);
    logChanSplitPath(&comps, path);

    bool ret = logChanMatchFrom(&pat, 0, &comps, 0);

    saDestroy(&comps);
    saDestroy(&pat);
    return ret;
}

_Use_decl_annotations_
bool logChanRuleMatchComps(LogDest* dest, LogChannel* chan, sa_string* comps)
{
    if (dest->nrules == 0) {
        // an unfiltered destination is "**", which reaches every channel that is not gated
        return chan->gatedepth == 0;
    }

    // most-specific-wins: the matching rule with the longest literal prefix decides, and an
    // exclude wins a tie
    int32 best  = -1;
    bool inc = false;

    for (uint32 i = 0; i < dest->nrules; i++) {
        LogFilterRule* rule = &dest->rules[i];

        // a rule only passes a gate by naming it literally
        if (rule->litdepth < chan->gatedepth)
            continue;
        if (!logChanMatchFrom(&rule->comps, 0, comps, 0))
            continue;

        if ((int32)rule->litdepth > best || ((int32)rule->litdepth == best && rule->exclude)) {
            best = (int32)rule->litdepth;
            inc  = !rule->exclude;
        }
    }

    return best >= 0 && inc;
}

_Use_decl_annotations_
bool logChanRuleMatch(LogDest* dest, LogChannel* chan)
{
    // an unfiltered destination never looks at the path, so do not pay to split it
    if (dest->nrules == 0)
        return chan->gatedepth == 0;

    sa_string comps;
    logChanSplitPath(&comps, chan->path);
    bool ret = logChanRuleMatchComps(dest, chan, &comps);
    saDestroy(&comps);

    return ret;
}
