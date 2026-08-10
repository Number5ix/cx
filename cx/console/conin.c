#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "console_private.h"

#include <cx/container/sarray.h>
#include <cx/debug/assert.h>
#include <cx/debug/crash.h>
#include <cx/utils/lazyinit.h>

#include <string.h>

// ---------------------------------------------------------------------------------------
// escape-sequence decoding: pure, portable, unit-testable directly (see contest.c)
// ---------------------------------------------------------------------------------------

// xterm's modifier encoding: the second CSI/SS3 parameter is 1 + a 3-bit mask where bit 0 is
// shift, bit 1 is alt, bit 2 is ctrl -- exactly CON_MOD_FLAGS's bit layout, so no remapping
// is needed once the leading 1 is subtracted.
static flags_t decodeMods(uint32 param)
{
    return param >= 1 ? (flags_t)(param - 1) & 0x7 : 0;
}

_Use_decl_annotations_
ConDecodeResult _conDecodeEscape(const uint8* buf, uint32 len, ConKeyEvent* out, uint32* consumed)
{
    devAssert(len > 0 && buf[0] == 0x1B);

    if (len < 2)
        return CON_Decode_Incomplete;   // need the second byte to know CSI/SS3/Alt+char

    uint8 b1 = buf[1];
    if (b1 != '[' && b1 != 'O') {
        // Alt+<char>: xterm's convention for a modified key it has no CSI encoding for.
        // Only single-byte chords are recognized -- see the header comment on ConKeyEvent.
        out->key  = CON_Key_Char;
        out->ch   = b1;
        out->mods = CON_Mod_Alt;
        *consumed = 2;
        return CON_Decode_Matched;
    }

    // CSI ('[') or SS3 ('O'): optional "param[;param]" digits, then one final byte.
    uint32 i         = 2;
    uint32 params[2] = { 0, 0 };
    uint32 nparams   = 0;
    while (i < len && (buf[i] == ';' || (buf[i] >= '0' && buf[i] <= '9'))) {
        if (buf[i] == ';') {
            if (++nparams >= 2)
                break;   // this module never needs a third parameter
        } else {
            params[nparams] = params[nparams] * 10 + (uint32)(buf[i] - '0');
        }
        i++;
    }
    if (i >= len)
        return CON_Decode_Incomplete;   // ran out of buffer before a final byte arrived

    uint8 final  = buf[i++];
    flags_t mods = decodeMods(params[1]);
    ConKey key;

    if (b1 == 'O') {
        switch (final) {
        case 'P':
            key = CON_Key_F1;
            break;
        case 'Q':
            key = CON_Key_F2;
            break;
        case 'R':
            key = CON_Key_F3;
            break;
        case 'S':
            key = CON_Key_F4;
            break;
        case 'H':
            key = CON_Key_Home;
            break;
        case 'F':
            key = CON_Key_End;
            break;
        default:
            return CON_Decode_NoMatch;
        }
    } else if (final == '~') {
        switch (params[0]) {
        case 1:
            key = CON_Key_Home;
            break;
        case 2:
            key = CON_Key_Insert;
            break;
        case 3:
            key = CON_Key_Delete;
            break;
        case 4:
            key = CON_Key_End;
            break;
        case 5:
            key = CON_Key_PageUp;
            break;
        case 6:
            key = CON_Key_PageDown;
            break;
        case 15:
            key = CON_Key_F5;
            break;
        case 17:
            key = CON_Key_F6;
            break;
        case 18:
            key = CON_Key_F7;
            break;
        case 19:
            key = CON_Key_F8;
            break;
        case 20:
            key = CON_Key_F9;
            break;
        case 21:
            key = CON_Key_F10;
            break;
        case 23:
            key = CON_Key_F11;
            break;
        case 24:
            key = CON_Key_F12;
            break;
        default:
            return CON_Decode_NoMatch;
        }
    } else {
        switch (final) {
        case 'A':
            key = CON_Key_Up;
            break;
        case 'B':
            key = CON_Key_Down;
            break;
        case 'C':
            key = CON_Key_Right;
            break;
        case 'D':
            key = CON_Key_Left;
            break;
        case 'H':
            key = CON_Key_Home;
            break;
        case 'F':
            key = CON_Key_End;
            break;
        default:
            return CON_Decode_NoMatch;
        }
    }

    out->key  = key;
    out->ch   = 0;
    out->mods = mods;
    *consumed = i;
    return CON_Decode_Matched;
}

// ---------------------------------------------------------------------------------------
// crash-safe raw-mode restore
//
// dbgCrashAddCallback()'s contract forbids complex locking from the callback (it may run on
// a signal stack, possibly on the thread that already holds con->lock). conLock()/
// conShutdown() are therefore off-limits here. _conPlatSetMode()/_conPlatSetEcho() never
// take con->lock themselves -- only the public wrappers below do -- so calling them directly
// is the one safe way to put the terminal back to something usable after a crash. This is a
// best-effort recovery, not a guarantee: g_rawActive is read without synchronization (a
// benign race against the crashing thread, since worst case is one missed or extra restore
// attempt) and conIn() itself is not re-entered if it was never created.
// ---------------------------------------------------------------------------------------

static atomic(bool) g_rawActive;
static ConStream* g_inForCrash;
static LazyInitState g_crashHookInit;

static bool crashRestoreInput(bool after)
{
    (void)after;
    if (atomicLoad(bool, &g_rawActive, Relaxed) && g_inForCrash) {
        _conPlatSetMode(g_inForCrash, CON_Cooked);
        _conPlatSetEcho(g_inForCrash, true);
    }
    return true;   // let normal crash processing continue
}

static void installCrashHook(void* unused)
{
    (void)unused;
    dbgCrashAddCallback(crashRestoreInput);
}

// ---------------------------------------------------------------------------------------
// public wrappers -- lock, restrict to CON_Kind_In, delegate to the platform
// ---------------------------------------------------------------------------------------

_Use_decl_annotations_
bool conSetMode(ConStream* con, ConInputMode mode)
{
    if (con->kind != CON_Kind_In)
        return false;

    conLock(con);
    bool ok = _conPlatSetMode(con, mode);
    if (ok) {
        lazyInit(&g_crashHookInit, installCrashHook, NULL);
        g_inForCrash = con;
        atomicStore(bool, &g_rawActive, mode == CON_Raw, Relaxed);
    }
    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conSetEcho(ConStream* con, bool echo)
{
    if (con->kind != CON_Kind_In)
        return false;

    conLock(con);
    bool ok = _conPlatSetEcho(con, echo);
    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conInWait(ConStream* con, int64 timeout)
{
    if (con->kind != CON_Kind_In)
        return false;

    conLock(con);
    bool ok = _conPlatInWait(con, timeout);
    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conReadKey(ConStream* con, ConKeyEvent* out, int64 timeout)
{
    if (con->kind != CON_Kind_In)
        return false;

    conLock(con);
    bool ok = _conPlatReadKey(con, out, timeout);
    conUnlock(con);
    return ok;
}

// ---------------------------------------------------------------------------------------
// redirected-input fallback: no tty means no raw mode, no key decoding, and nothing to
// echo (or to suppress echoing of) -- just the bytes up to the next '\n', same as fgets().
// conReadLine() and conReadPassword() are indistinguishable here since there is no terminal
// echo to manage either way.
// ---------------------------------------------------------------------------------------

static bool readLinePlain(ConStream* con, string* out)
{
    sa_uint8 bytes;
    saInit(&bytes, uint8, 128);

    bool gotAny = false;
    bool ok     = true;
    for (;;) {
        uint8 b;
        if (!_conPlatReadRawByte(con, &b)) {
            ok = gotAny;   // EOF/error: a trailing line with no '\n' still counts as success
            break;
        }
        gotAny = true;
        if (b == '\n')
            break;
        saPush(&bytes, uint8, b);
    }

    if (ok) {
        int32 n = saSize(bytes);
        if (n > 0 && bytes.a[n - 1] == '\r')
            n--;   // tolerate CRLF-terminated input

        uint8* dst = strBuffer(out, (uint32)n);
        memcpy(dst, bytes.a, (size_t)n);
        strSetLen(out, (uint32)n);
    }

    saDestroy(&bytes);
    return ok;
}

// ---------------------------------------------------------------------------------------
// line/password reading: a small raw-mode editor built entirely on conReadKey()
// ---------------------------------------------------------------------------------------

static bool readLineImpl(ConStream* con, string* out, bool echoTyped)
{
    strDestroy(out);

    if (con->kind != CON_Kind_In)
        return false;

    ConCaps caps;
    conGetCaps(con, &caps);
    if (!caps.istty)
        return readLinePlain(con, out);

    if (!conSetMode(con, CON_Raw))
        return false;
    conSetEcho(con, false);   // this function echoes itself (or not, for a password) -- the
                              // terminal's own echo would double up or leak the password

    sa_int32 chars;
    saInit(&chars, int32, 64);

    bool ok = true;
    for (;;) {
        ConKeyEvent ev;
        if (!conReadKey(con, &ev, timeForever)) {
            ok = false;
            break;
        }

        if (ev.key == CON_Key_Enter)
            break;

        if (ev.key == CON_Key_Backspace) {
            int32 n = saSize(chars);
            if (n > 0) {
                saSetSize(&chars, n - 1);
                if (echoTyped) {
                    static const uint8 erase[] = { '\b', ' ', '\b' };
                    conWrite(con, erase, sizeof(erase));
                }
            }
            continue;
        }

        if (ev.key == CON_Key_Char) {
            saPush(&chars, int32, ev.ch);
            if (echoTyped)
                conPutc(con, ev.ch);
            continue;
        }

        // arrows, function keys, etc. -- not handled by this minimal line editor
    }

    conSetMode(con, CON_Cooked);
    conSetEcho(con, true);
    if (ok && echoTyped)
        conNL(con);   // the terminal never echoed Enter itself; move to the next line

    if (ok) {
        uint32 total = 0;
        for (int32 i = 0; i < saSize(chars); i++) {
            uint8 tmp[4];
            total += _conUtf8Encode(tmp, chars.a[i]);
        }

        uint8* dst = strBuffer(out, total);
        uint32 pos = 0;
        for (int32 i = 0; i < saSize(chars); i++) pos += _conUtf8Encode(dst + pos, chars.a[i]);
        strSetLen(out, total);
    }

    saDestroy(&chars);
    return ok;
}

_Use_decl_annotations_
bool conReadLine(ConStream* con, string* out)
{
    return readLineImpl(con, out, true);
}

_Use_decl_annotations_
bool conReadPassword(ConStream* con, string* out)
{
    return readLineImpl(con, out, false);
}
