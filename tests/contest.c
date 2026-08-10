#include <cx/console.h>
#include <cx/console/console_private.h>
#include <cx/string.h>
#include <cx/string/strtest.h>
#include <cx/thread.h>

#include <string.h>

#define TEST_FILE  contest
#define TEST_FUNCS contest_funcs
#include "common.h"

// ---------------------------------------------------------------------------------------
// caps: drive the pure heuristic directly, with no tty and no real environment involved
// ---------------------------------------------------------------------------------------

static int test_caps()
{
    ConCaps c;

    // not a tty, nothing forced -> no color, no vt, regardless of TERM
    _conDetectCaps(&c, false, "xterm-256color", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (c.color != CON_ColorNone || c.vt)
        return 1;

    // FORCE_COLOR forces color even without a tty
    _conDetectCaps(&c, false, "xterm-256color", NULL, NULL, "1", NULL, NULL, NULL, NULL, NULL);
    if (c.color != CON_Color256 || !c.vt)
        return 2;

    // CLICOLOR_FORCE=0 does not count as forcing
    _conDetectCaps(&c, false, "xterm-256color", NULL, NULL, NULL, "0", NULL, NULL, NULL, NULL);
    if (c.color != CON_ColorNone || c.vt)
        return 3;

    // TERM=dumb wins even on a real tty
    _conDetectCaps(&c, true, "dumb", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (c.color != CON_ColorNone || c.vt || c.cursor || c.altscreen)
        return 4;

    // unset TERM behaves the same as dumb
    _conDetectCaps(&c, true, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (c.color != CON_ColorNone || c.vt)
        return 5;

    // COLORTERM=truecolor wins outright
    _conDetectCaps(&c, true, "xterm", "truecolor", NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (c.color != CON_ColorTrue || !c.vt)
        return 6;

    // TERM containing "256color"
    _conDetectCaps(&c, true, "screen-256color", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (c.color != CON_Color256 || !c.vt)
        return 7;

    // known 16-color TERM prefix
    _conDetectCaps(&c, true, "xterm", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (c.color != CON_Color16 || !c.vt || !c.cursor || !c.altscreen)
        return 8;

    // unrecognized TERM on a real tty still gets the 16-color fallback
    _conDetectCaps(&c, true, "some-future-terminal", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                  NULL);
    if (c.color != CON_Color16 || !c.vt)
        return 9;

    // WT_SESSION upgrades an otherwise-16-color terminal to truecolor
    _conDetectCaps(&c, true, "xterm", NULL, NULL, NULL, NULL, "1", NULL, NULL, NULL);
    if (c.color != CON_ColorTrue)
        return 10;

    // NO_COLOR wins over everything, including an explicit truecolor signal, but leaves vt alone
    _conDetectCaps(&c, true, "xterm-256color", "truecolor", "1", NULL, NULL, NULL, NULL, NULL,
                  NULL);
    if (c.color != CON_ColorNone || !c.vt)
        return 11;

    // NO_COLOR beats WT_SESSION too
    _conDetectCaps(&c, true, "xterm", NULL, "1", NULL, NULL, "1", NULL, NULL, NULL);
    if (c.color != CON_ColorNone)
        return 12;

    // unicode tracks LANG regardless of color/tty state
    _conDetectCaps(&c, false, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "en_US.UTF-8");
    if (!c.unicode)
        return 13;
    _conDetectCaps(&c, true, "xterm", NULL, NULL, NULL, NULL, NULL, NULL, NULL, "C");
    if (c.unicode)
        return 14;

    // cursorquery is never claimed by the pure heuristic; only a platform probe may set it
    if (c.cursorquery)
        return 15;

    return 0;
}

// ---------------------------------------------------------------------------------------
// write: exercise the buffered output path through a memory-backed stream
// ---------------------------------------------------------------------------------------

static int test_write()
{
    ConCaps caps   = { 0 };
    ConStream* con = conCreateMem(&caps);

    if (!conPuts(con, _SL("hello ")) || !conPutsz(con, "world") || !conPutc(con, '!') ||
        !conNL(con)) {
        conDestroy(&con);
        return 1;
    }

    string out = 0;
    conMemGet(con, &out);
    bool ok = strEq(out, _SL("hello world!\n"));
    strDestroy(&out);
    if (!ok) {
        conDestroy(&con);
        return 2;
    }

    if (!conWrite(con, "XYZ", 3)) {
        conDestroy(&con);
        return 3;
    }

    conMemGet(con, &out);
    ok = strEq(out, _SL("hello world!\nXYZ"));
    strDestroy(&out);
    conDestroy(&con);
    return ok ? 0 : 4;
}

// A rope only forms above ROPE_JOIN_THRESH (128 bytes); this mirrors the construction used
// by strtest.c's own rope test.
static int test_write_rope()
{
    // Must be at least ROPE_MIN_SIZE (64) bytes, or _strAppend's small-tail-merge exception
    // keeps flattening instead of forming a rope; 66 mirrors strtest.c's own rope test.
    strref lit66 = _S"Thirty-two character test string"
                    "gnirts tset retcarahc owt-ytrihT";

    string rope = 0;
    strAppend(&rope, lit66);
    strAppend(&rope, lit66);
    if (strTestRopeDepth(rope) < 1) {
        // not actually exercising the multi-run walk; the test itself is broken
        strDestroy(&rope);
        return 1;
    }

    ConCaps caps   = { 0 };
    ConStream* con = conCreateMem(&caps);

    if (!conPuts(con, rope)) {
        strDestroy(&rope);
        conDestroy(&con);
        return 2;
    }

    string out    = 0;
    string expect = 0;
    conMemGet(con, &out);
    strAppend(&expect, lit66);
    strAppend(&expect, lit66);
    bool ok = strEq(out, expect);
    strDestroy(&out);
    strDestroy(&expect);
    strDestroy(&rope);
    conDestroy(&con);
    return ok ? 0 : 3;
}

static int test_write_utf8()
{
    ConCaps caps   = { 0 };
    ConStream* con = conCreateMem(&caps);

    conPutc(con, 0xE9);   // U+00E9 LATIN SMALL LETTER E WITH ACUTE -> 0xC3 0xA9

    string out = 0;
    conMemGet(con, &out);
    static const uint8 expect[2] = { 0xC3, 0xA9 };
    bool ok = strLen(out) == 2 && memcmp(strC(out), expect, 2) == 0;
    strDestroy(&out);
    conDestroy(&con);
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------------------------------
// lock: recursion via the external depth counter, and cross-thread atomicity
// ---------------------------------------------------------------------------------------

static int test_lock_recursive()
{
    ConCaps caps   = { 0 };
    ConStream* con = conCreateMem(&caps);

    conLock(con);
    conLock(con);
    conLock(con);
    bool wrote = conPuts(con, _SL("nested\n"));
    conUnlock(con);
    conUnlock(con);
    conUnlock(con);

    int ret = 0;
    if (!wrote) {
        ret = 1;
    } else {
        string out = 0;
        conMemGet(con, &out);
        if (!strEq(out, _SL("nested\n")))
            ret = 2;
        strDestroy(&out);
    }

    conDestroy(&con);
    return ret;
}

static int test_lock_withblock()
{
    ConCaps caps   = { 0 };
    ConStream* con = conCreateMem(&caps);

    withConLock(con) {
        conPuts(con, _SL("a"));
        conPuts(con, _SL("b"));
    }

    // if withConLock failed to unlock on every path, this would deadlock
    conLock(con);
    conUnlock(con);

    string out = 0;
    conMemGet(con, &out);
    bool ok = strEq(out, _SL("ab"));
    strDestroy(&out);
    conDestroy(&con);
    return ok ? 0 : 1;
}

#define LOCK_STRESS_ITERS   2000
#define LOCK_STRESS_THREADS 4

static ConStream* g_lockCon;
static strref g_lockLines[LOCK_STRESS_THREADS];

static int lockThreadProc(Thread* self)
{
    int32 idx;
    if (!stvlNext(&self->args, int32, &idx))
        return 0;

    for (int i = 0; i < LOCK_STRESS_ITERS; i++)
        conPuts(g_lockCon, g_lockLines[idx]);

    return 0;
}

static int test_lock_stress()
{
    ConCaps caps = { 0 };
    g_lockCon    = conCreateMem(&caps);

    g_lockLines[0] = _SL("AAAAAAA\n");
    g_lockLines[1] = _SL("BBBBBBB\n");
    g_lockLines[2] = _SL("CCCCCCC\n");
    g_lockLines[3] = _SL("DDDDDDD\n");

    Thread* threads[LOCK_STRESS_THREADS];
    for (int i = 0; i < LOCK_STRESS_THREADS; i++)
        threads[i] = thrCreate(lockThreadProc, _S"Console Lock Stress", stvar(int32, i));

    for (int i = 0; i < LOCK_STRESS_THREADS; i++) {
        thrWait(threads[i], timeForever);
        thrShutdown(threads[i]);
        thrRelease(&threads[i]);
    }

    string out = 0;
    conMemGet(g_lockCon, &out);

    int ret = 0;
    if (strLen(out) != (uint32)(LOCK_STRESS_THREADS * LOCK_STRESS_ITERS * 8)) {
        ret = 1;
    } else {
        int counts[LOCK_STRESS_THREADS] = { 0 };
        const uint8* bytes = (const uint8*)strC(out);
        for (uint32 off = 0; off < strLen(out) && ret == 0; off += 8) {
            int idx = -1;
            for (int i = 0; i < LOCK_STRESS_THREADS; i++) {
                if (memcmp(bytes + off, strC(g_lockLines[i]), 8) == 0) {
                    idx = i;
                    break;
                }
            }
            if (idx < 0)
                ret = 2;   // a line was split/interleaved -- lock did not protect the write
            else
                counts[idx]++;
        }
        for (int i = 0; ret == 0 && i < LOCK_STRESS_THREADS; i++) {
            if (counts[i] != LOCK_STRESS_ITERS)
                ret = 3;   // some writes were lost
        }
    }

    strDestroy(&out);
    conDestroy(&g_lockCon);
    return ret;
}

// ---------------------------------------------------------------------------------------
// style: the color downgrade ladder and styled writes, all against exact emitted bytes
// ---------------------------------------------------------------------------------------

static bool memEq(ConStream* con, strref expect)
{
    string out = 0;
    conMemGet(con, &out);
    bool ok = strEq(out, expect);
    strDestroy(&out);
    return ok;
}

// A pure-red truecolor request downgrades cleanly at every depth: it lands exactly on an
// xterm 256 cube entry and exactly on ANSI bright red, so the expected bytes at each rung
// are unambiguous rather than an artifact of the distance-matching algorithm.
static int test_style_downgrade()
{
    ConStyle red = CONSTYLE(CON_RGB(255, 0, 0), 0);

    ConCaps caps = { .vt = true, .color = CON_ColorTrue };
    ConStream* con = conCreateMem(&caps);
    conSetStyle(con, red);
    bool ok = memEq(con, _SL("\x1b[0;38;2;255;0;0m"));
    conDestroy(&con);
    if (!ok)
        return 1;

    caps = (ConCaps){ .vt = true, .color = CON_Color256 };
    con  = conCreateMem(&caps);
    conSetStyle(con, red);
    ok = memEq(con, _SL("\x1b[0;38;5;196m"));
    conDestroy(&con);
    if (!ok)
        return 2;

    caps = (ConCaps){ .vt = true, .color = CON_Color16 };
    con  = conCreateMem(&caps);
    conSetStyle(con, red);
    ok = memEq(con, _SL("\x1b[0;91m"));
    conDestroy(&con);
    if (!ok)
        return 3;

    caps = (ConCaps){ .vt = true, .color = CON_ColorNone };
    con  = conCreateMem(&caps);
    conSetStyle(con, red);
    ok = memEq(con, _SL("\x1b[0m"));
    conDestroy(&con);
    if (!ok)
        return 4;

    return 0;
}

static int test_style_attrs()
{
    ConCaps caps   = { .vt = true, .color = CON_ColorNone };
    ConStream* con = conCreateMem(&caps);

    conSetStyle(con, CONSTYLE(CON_ColorDefault, CON_Bold | CON_Underline));
    bool ok = memEq(con, _SL("\x1b[0;1;4m"));
    conDestroy(&con);
    return ok ? 0 : 1;
}

// A stream with no vt and no color at all (a genuinely dumb terminal) must emit nothing --
// never garbage escape bytes it can't back up.
static int test_style_none()
{
    ConCaps caps   = { .vt = false, .color = CON_ColorNone };
    ConStream* con = conCreateMem(&caps);

    conSetStyle(con, CONSTYLE2(CON_Red, CON_Blue, CON_Bold));
    bool ok = memEq(con, _SL(""));
    conDestroy(&con);
    return ok ? 0 : 1;
}

// conPutsS/conWriteS must restore whatever style was active before the call, not the
// stream's default -- so a nested styled write inside an outer conSetStyle() must come back
// out to the outer style afterward.
static int test_style_restore()
{
    ConCaps caps   = { .vt = true, .color = CON_Color16 };
    ConStream* con = conCreateMem(&caps);

    conSetStyle(con, CONSTYLE(CON_Green, 0));
    bool ok = conPutsS(con, CONSTYLE(CON_Red, 0), _SL("X"));

    ConStyle cur;
    conGetStyle(con, &cur);
    ok = ok && cur.fg == (uint32)CON_Green;

    bool bytesOk =
        memEq(con, _SL("\x1b[0;32m" "\x1b[0;31m" "X" "\x1b[0;32m"));

    conDestroy(&con);
    return (ok && bytesOk) ? 0 : 1;
}

// ---------------------------------------------------------------------------------------
// fmt: conFmt/conFmtS wrap strFormat() into a temporary string, then conPuts()/conPutsS()
// ---------------------------------------------------------------------------------------

static int test_fmt()
{
    ConCaps caps   = { 0 };
    ConStream* con = conCreateMem(&caps);

    string name = 0;
    strDup(&name, _SL("world"));
    bool ok = conFmt(con, _SL("hello ${string}, you have ${int} messages"),
                     stvar(string, name), stvar(int32, 3));
    strDestroy(&name);

    ok = ok && memEq(con, _SL("hello world, you have 3 messages"));
    conDestroy(&con);
    return ok ? 0 : 1;
}

static int test_fmt_styled()
{
    ConCaps caps   = { .vt = true, .color = CON_Color16 };
    ConStream* con = conCreateMem(&caps);

    bool ok = conFmtS(con, CONSTYLE(CON_Red, 0), _SL("err ${int}"), stvar(int32, 7));
    ok      = ok && memEq(con, _SL("\x1b[0;31m" "err 7" "\x1b[0m"));

    conDestroy(&con);
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------------------------------
// cursor: VT emission for cursor/screen ops, all against exact emitted bytes
// ---------------------------------------------------------------------------------------

static int test_cursor_set()
{
    ConCaps caps   = { .vt = true, .cursor = true };
    ConStream* con = conCreateMem(&caps);

    bool ok = conSetCursor(con, 3, 5);   // 0-based in, 1-based VT out
    ok      = ok && memEq(con, _SL("\x1b[4;6H"));

    conDestroy(&con);
    return ok ? 0 : 1;
}

static int test_cursor_move()
{
    ConCaps caps   = { .vt = true, .cursor = true };
    ConStream* con = conCreateMem(&caps);

    // a zero move is a trivial success that emits nothing
    bool ok = conMoveCursor(con, 0, 0);
    ok      = ok && memEq(con, _SL(""));

    ok = ok && conMoveCursor(con, 2, -3);
    ok = ok && memEq(con, _SL("\x1b[2B" "\x1b[3D"));

    conDestroy(&con);
    return ok ? 0 : 1;
}

static int test_cursor_show()
{
    ConCaps caps   = { .vt = true, .cursor = true };
    ConStream* con = conCreateMem(&caps);

    bool ok = conShowCursor(con, false);
    ok      = ok && memEq(con, _SL("\x1b[?25l"));
    ok      = ok && conShowCursor(con, true);
    ok      = ok && memEq(con, _SL("\x1b[?25l" "\x1b[?25h"));

    conDestroy(&con);
    return ok ? 0 : 1;
}

static int test_cursor_save_restore()
{
    ConCaps caps   = { .vt = true, .cursor = true };
    ConStream* con = conCreateMem(&caps);

    bool ok = conSaveCursor(con);
    ok      = ok && conRestoreCursor(con);
    ok      = ok && memEq(con, _SL("\x1b" "7" "\x1b" "8"));

    conDestroy(&con);
    return ok ? 0 : 1;
}

static int test_cursor_erase()
{
    ConCaps caps   = { .vt = true, .cursor = true };
    ConStream* con = conCreateMem(&caps);

    bool ok = conEraseLine(con, CON_EraseToEnd);
    ok      = ok && conEraseLine(con, CON_EraseToStart);
    ok      = ok && conEraseLine(con, CON_EraseAll);
    ok      = ok && conEraseScreen(con, CON_EraseToEnd);
    ok      = ok && conEraseScreen(con, CON_EraseToStart);
    ok      = ok && conEraseScreen(con, CON_EraseAll);
    ok      = ok && memEq(con, _SL("\x1b[0K" "\x1b[1K" "\x1b[2K"
                                   "\x1b[0J" "\x1b[1J" "\x1b[2J"));

    conDestroy(&con);
    return ok ? 0 : 1;
}

static int test_cursor_scroll()
{
    ConCaps caps   = { .vt = true, .cursor = true };
    ConStream* con = conCreateMem(&caps);

    // scrolling by zero lines is a trivial success that emits nothing
    bool ok = conScroll(con, 0);
    ok      = ok && memEq(con, _SL(""));

    ok = ok && conScroll(con, 3);
    ok = ok && conScroll(con, -2);
    ok = ok && memEq(con, _SL("\x1b[3S" "\x1b[2T"));

    conDestroy(&con);
    return ok ? 0 : 1;
}

static int test_cursor_altscreen()
{
    ConCaps caps   = { .vt = true, .cursor = true, .altscreen = true };
    ConStream* con = conCreateMem(&caps);

    bool ok = conAltScreen(con, true);
    ok      = ok && conAltScreen(con, false);
    ok      = ok && memEq(con, _SL("\x1b[?1049h" "\x1b[?1049l"));

    conDestroy(&con);
    return ok ? 0 : 1;
}

// A stream with no cursor capability at all must fail cleanly and emit nothing -- never
// garbage escape bytes it can't back up.
static int test_cursor_none()
{
    ConCaps caps   = { 0 };
    ConStream* con = conCreateMem(&caps);

    bool anyOk = conSetCursor(con, 0, 0) || conMoveCursor(con, 1, 0) ||
                 conShowCursor(con, true) || conSaveCursor(con) || conRestoreCursor(con) ||
                 conEraseLine(con, CON_EraseAll) || conEraseScreen(con, CON_EraseAll) ||
                 conScroll(con, 1) || conAltScreen(con, true);
    bool emptyOut = memEq(con, _SL(""));

    conDestroy(&con);
    return (!anyOk && emptyOut) ? 0 : 1;
}

// conGetCursor() is never satisfiable on a memory stream, even if a test fixture claims
// cursorquery support -- there is no real position behind it, per console_private.h's
// contract on _conPlatCursorGet().
static int test_cursor_getcursor_mem()
{
    ConCaps caps   = { .cursorquery = true };
    ConStream* con = conCreateMem(&caps);

    uint16 row = 0, col = 0;
    bool ok = !conGetCursor(con, &row, &col);

    conDestroy(&con);
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------------------------------
// decode_escape: drive the pure escape-sequence decoder directly, no tty involved
// ---------------------------------------------------------------------------------------

static int test_decode_escape()
{
    ConKeyEvent ev;
    uint32 consumed;

    // a lone ESC is a valid prefix of something else -- ambiguous until more bytes arrive
    static const uint8 lone[] = { 0x1B };
    if (_conDecodeEscape(lone, 1, &ev, &consumed) != CON_Decode_Incomplete)
        return 1;

    // CSI arrow keys
    static const uint8 up[] = { 0x1B, '[', 'A' };
    if (_conDecodeEscape(up, sizeof(up), &ev, &consumed) != CON_Decode_Matched ||
        ev.key != CON_Key_Up || consumed != sizeof(up))
        return 2;

    // CSI ~ form (Delete)
    static const uint8 del[] = { 0x1B, '[', '3', '~' };
    if (_conDecodeEscape(del, sizeof(del), &ev, &consumed) != CON_Decode_Matched ||
        ev.key != CON_Key_Delete || consumed != sizeof(del))
        return 3;

    // SS3 function keys
    static const uint8 f1[] = { 0x1B, 'O', 'P' };
    if (_conDecodeEscape(f1, sizeof(f1), &ev, &consumed) != CON_Decode_Matched ||
        ev.key != CON_Key_F1 || consumed != sizeof(f1))
        return 4;

    // xterm modifier parameter: CSI 1 ; 5 A == Ctrl+Up (5 == 1 + ctrl-bit(4))
    static const uint8 ctrlUp[] = { 0x1B, '[', '1', ';', '5', 'A' };
    if (_conDecodeEscape(ctrlUp, sizeof(ctrlUp), &ev, &consumed) != CON_Decode_Matched ||
        ev.key != CON_Key_Up || ev.mods != CON_Mod_Ctrl || consumed != sizeof(ctrlUp))
        return 5;

    // an incomplete CSI sequence (parameter digits but no final byte yet) needs more bytes
    static const uint8 partial[] = { 0x1B, '[', '1' };
    if (_conDecodeEscape(partial, sizeof(partial), &ev, &consumed) != CON_Decode_Incomplete)
        return 6;

    // a CSI final byte this module doesn't recognize
    static const uint8 unknown[] = { 0x1B, '[', 'Z' };
    if (_conDecodeEscape(unknown, sizeof(unknown), &ev, &consumed) != CON_Decode_NoMatch)
        return 7;

    // Alt+<char>: ESC directly followed by an ordinary byte
    static const uint8 altA[] = { 0x1B, 'a' };
    if (_conDecodeEscape(altA, sizeof(altA), &ev, &consumed) != CON_Decode_Matched ||
        ev.key != CON_Key_Char || ev.ch != 'a' || ev.mods != CON_Mod_Alt || consumed != 2)
        return 8;

    return 0;
}

// ---------------------------------------------------------------------------------------
// input: the public wrappers only work on CON_Kind_In -- everything else fails cleanly
// ---------------------------------------------------------------------------------------

static int test_input_wrong_kind()
{
    ConCaps caps   = { 0 };
    ConStream* con = conCreateMem(&caps);

    ConKeyEvent ev;
    bool anyOk = conSetMode(con, CON_Raw) || conSetEcho(con, false) ||
                 conInWait(con, 0) || conReadKey(con, &ev, 0);

    string line = 0;
    anyOk        = anyOk || conReadLine(con, &line) || conReadPassword(con, &line);
    strDestroy(&line);

    conDestroy(&con);
    return anyOk ? 1 : 0;
}

testfunc contest_funcs[] = {
    { "caps",             test_caps             },
    { "write",            test_write            },
    { "write_rope",       test_write_rope       },
    { "write_utf8",       test_write_utf8       },
    { "lock",             test_lock_recursive   },
    { "lock_block",       test_lock_withblock   },
    { "lock_stress",      test_lock_stress      },
    { "style_downgrade",  test_style_downgrade  },
    { "style_attrs",      test_style_attrs      },
    { "style_none",       test_style_none       },
    { "style_restore",    test_style_restore    },
    { "fmt",              test_fmt              },
    { "fmt_styled",       test_fmt_styled       },
    { "cursor_set",       test_cursor_set       },
    { "cursor_move",      test_cursor_move      },
    { "cursor_show",      test_cursor_show      },
    { "cursor_save",      test_cursor_save_restore },
    { "cursor_erase",     test_cursor_erase     },
    { "cursor_scroll",    test_cursor_scroll    },
    { "cursor_altscreen", test_cursor_altscreen },
    { "cursor_none",      test_cursor_none      },
    { "cursor_getmem",    test_cursor_getcursor_mem },
    { "decode_escape",    test_decode_escape    },
    { "input_wrong_kind", test_input_wrong_kind },
    { 0,                  0                     }
};
