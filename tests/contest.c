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
    _conDetectCaps(&c, false, CON_ColorNone, "xterm-256color", NULL, NULL, NULL, NULL, NULL, NULL,
                   NULL, NULL);
    if (c.color != CON_ColorNone || c.vt)
        TEST_FAIL(1, _SL("no-tty: expected color ${int}, vt false; got color ${int}, vt ${int}"),
                  stvar(int32, CON_ColorNone), stvar(int32, c.color), stvar(int32, (int32)(c.vt)));

    // FORCE_COLOR forces color even without a tty
    _conDetectCaps(&c, false, CON_ColorNone, "xterm-256color", NULL, NULL, "1", NULL, NULL, NULL,
                   NULL, NULL);
    if (c.color != CON_Color256 || !c.vt)
        TEST_FAIL(2, _SL("FORCE_COLOR: expected color ${int}, vt true; got color ${int}, vt ${int}"),
                  stvar(int32, CON_Color256), stvar(int32, c.color), stvar(int32, (int32)(c.vt)));

    // CLICOLOR_FORCE=0 does not count as forcing
    _conDetectCaps(&c, false, CON_ColorNone, "xterm-256color", NULL, NULL, NULL, "0", NULL, NULL,
                   NULL, NULL);
    if (c.color != CON_ColorNone || c.vt)
        TEST_FAIL(3, _SL("CLICOLOR_FORCE=0: expected color ${int}, vt false; got color ${int}, vt ${int}"),
                  stvar(int32, CON_ColorNone), stvar(int32, c.color), stvar(int32, (int32)(c.vt)));

    // TERM=dumb wins even on a real tty
    _conDetectCaps(&c, true, CON_ColorNone, "dumb", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (c.color != CON_ColorNone || c.vt || c.cursor || c.altscreen)
        TEST_FAIL(4, _SL("TERM=dumb: expected color ${int}/vt/cursor/altscreen all false; got color ${int}, vt ${int}, cursor ${int}, altscreen ${int}"),
                  stvar(int32, CON_ColorNone), stvar(int32, c.color), stvar(int32, (int32)(c.vt)),
                  stvar(int32, (int32)(c.cursor)), stvar(int32, (int32)(c.altscreen)));

    // unset TERM resolves to whatever the platform passed as `termless` -- CON_ColorNone is
    // the unix answer, and makes an unset TERM behave the same as dumb
    _conDetectCaps(&c, true, CON_ColorNone, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (c.color != CON_ColorNone || c.vt)
        TEST_FAIL(5, _SL("unset TERM: expected color ${int}, vt false; got color ${int}, vt ${int}"),
                  stvar(int32, CON_ColorNone), stvar(int32, c.color), stvar(int32, (int32)(c.vt)));

    // COLORTERM=truecolor wins outright
    _conDetectCaps(&c, true, CON_ColorNone, "xterm", "truecolor", NULL, NULL, NULL, NULL, NULL,
                   NULL, NULL);
    if (c.color != CON_ColorTrue || !c.vt)
        TEST_FAIL(6, _SL("COLORTERM=truecolor: expected color ${int}, vt true; got color ${int}, vt ${int}"),
                  stvar(int32, CON_ColorTrue), stvar(int32, c.color), stvar(int32, (int32)(c.vt)));

    // TERM containing "256color"
    _conDetectCaps(&c, true, CON_ColorNone, "screen-256color", NULL, NULL, NULL, NULL, NULL, NULL,
                   NULL, NULL);
    if (c.color != CON_Color256 || !c.vt)
        TEST_FAIL(7, _SL("TERM=screen-256color: expected color ${int}, vt true; got color ${int}, vt ${int}"),
                  stvar(int32, CON_Color256), stvar(int32, c.color), stvar(int32, (int32)(c.vt)));

    // known 16-color TERM prefix
    _conDetectCaps(&c, true, CON_ColorNone, "xterm", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                   NULL);
    if (c.color != CON_Color16 || !c.vt || !c.cursor || !c.altscreen)
        TEST_FAIL(8, _SL("TERM=xterm: expected color ${int}/vt/cursor/altscreen all true; got color ${int}, vt ${int}, cursor ${int}, altscreen ${int}"),
                  stvar(int32, CON_Color16), stvar(int32, c.color), stvar(int32, (int32)(c.vt)),
                  stvar(int32, (int32)(c.cursor)), stvar(int32, (int32)(c.altscreen)));

    // unrecognized TERM on a real tty still gets the 16-color fallback
    _conDetectCaps(&c, true, CON_ColorNone, "some-future-terminal", NULL, NULL, NULL, NULL, NULL,
                   NULL, NULL, NULL);
    if (c.color != CON_Color16 || !c.vt)
        TEST_FAIL(9, _SL("unrecognized TERM: expected color ${int}, vt true; got color ${int}, vt ${int}"),
                  stvar(int32, CON_Color16), stvar(int32, c.color), stvar(int32, (int32)(c.vt)));

    // WT_SESSION upgrades an otherwise-16-color terminal to truecolor
    _conDetectCaps(&c, true, CON_ColorNone, "xterm", NULL, NULL, NULL, NULL, "1", NULL, NULL, NULL);
    if (c.color != CON_ColorTrue)
        TEST_FAIL(10, _SL("WT_SESSION: expected color ${int}, got ${int}"),
                  stvar(int32, CON_ColorTrue), stvar(int32, c.color));

    // NO_COLOR wins over everything, including an explicit truecolor signal, but leaves vt alone
    _conDetectCaps(&c, true, CON_ColorNone, "xterm-256color", "truecolor", "1", NULL, NULL, NULL,
                   NULL, NULL, NULL);
    if (c.color != CON_ColorNone || !c.vt)
        TEST_FAIL(11, _SL("NO_COLOR over truecolor: expected color ${int}, vt true; got color ${int}, vt ${int}"),
                  stvar(int32, CON_ColorNone), stvar(int32, c.color), stvar(int32, (int32)(c.vt)));

    // NO_COLOR beats WT_SESSION too
    _conDetectCaps(&c, true, CON_ColorNone, "xterm", NULL, "1", NULL, NULL, "1", NULL, NULL, NULL);
    if (c.color != CON_ColorNone)
        TEST_FAIL(12, _SL("NO_COLOR over WT_SESSION: expected color ${int}, got ${int}"),
                  stvar(int32, CON_ColorNone), stvar(int32, c.color));

    // unicode tracks LANG regardless of color/tty state
    _conDetectCaps(&c, false, CON_ColorNone, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                   "en_US.UTF-8");
    if (!c.unicode)
        TEST_FAIL(13, _SL("LANG=en_US.UTF-8: expected unicode true, got ${int}"), stvar(int32, (int32)(c.unicode)));
    _conDetectCaps(&c, true, CON_ColorNone, "xterm", NULL, NULL, NULL, NULL, NULL, NULL, NULL, "C");
    if (c.unicode)
        TEST_FAIL(14, _SL("LANG=C: expected unicode false, got ${int}"), stvar(int32, (int32)(c.unicode)));

    // cursorquery is never claimed by the pure heuristic; only a platform probe may set it
    if (c.cursorquery)
        TEST_FAIL(15, _SL("expected cursorquery false, got ${int}"), stvar(int32, (int32)(c.cursorquery)));

    // --- termless: what an unset TERM means, which is a per-platform answer ---

    // the windows shape -- no TERM, but a console-mode probe that found VT. Reporting
    // CON_ColorNone here while caps.vt stays true is what made styled output emit bare SGR
    // resets and render nothing at all.
    _conDetectCaps(&c, true, CON_ColorTrue, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (c.color != CON_ColorTrue || !c.vt || !c.cursor || !c.altscreen)
        TEST_FAIL(16, _SL("termless windows shape: expected color ${int}/vt/cursor/altscreen all true; got color ${int}, vt ${int}, cursor ${int}, altscreen ${int}"),
                  stvar(int32, CON_ColorTrue), stvar(int32, c.color), stvar(int32, (int32)(c.vt)),
                  stvar(int32, (int32)(c.cursor)), stvar(int32, (int32)(c.altscreen)));

    // the legacy-console shape -- no VT, so 16 colors via SetConsoleTextAttribute
    _conDetectCaps(&c, true, CON_Color16, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (c.color != CON_Color16 || !c.vt)
        TEST_FAIL(17, _SL("termless legacy-console shape: expected color ${int}, vt true; got color ${int}, vt ${int}"),
                  stvar(int32, CON_Color16), stvar(int32, c.color), stvar(int32, (int32)(c.vt)));

    // termless is consulted only when TERM is unset -- a TERM that is set always wins,
    // including TERM=dumb on an otherwise fully capable console
    _conDetectCaps(&c, true, CON_ColorTrue, "dumb", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (c.color != CON_ColorNone || c.vt)
        TEST_FAIL(18, _SL("TERM=dumb overrides termless: expected color ${int}, vt false; got color ${int}, vt ${int}"),
                  stvar(int32, CON_ColorNone), stvar(int32, c.color), stvar(int32, (int32)(c.vt)));
    _conDetectCaps(&c, true, CON_ColorTrue, "xterm", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                   NULL);
    if (c.color != CON_Color16 || !c.vt)
        TEST_FAIL(19, _SL("TERM=xterm overrides termless: expected color ${int}, vt true; got color ${int}, vt ${int}"),
                  stvar(int32, CON_Color16), stvar(int32, c.color), stvar(int32, (int32)(c.vt)));

    // NO_COLOR still wins over termless, and still leaves vt alone
    _conDetectCaps(&c, true, CON_ColorTrue, NULL, NULL, "1", NULL, NULL, NULL, NULL, NULL, NULL);
    if (c.color != CON_ColorNone || !c.vt)
        TEST_FAIL(20, _SL("NO_COLOR over termless: expected color ${int}, vt true; got color ${int}, vt ${int}"),
                  stvar(int32, CON_ColorNone), stvar(int32, c.color), stvar(int32, (int32)(c.vt)));

    // termless does not resurrect a non-tty: not a terminal is still not a terminal
    _conDetectCaps(&c, false, CON_ColorTrue, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (c.color != CON_ColorNone || c.vt)
        TEST_FAIL(21, _SL("termless non-tty: expected color ${int}, vt false; got color ${int}, vt ${int}"),
                  stvar(int32, CON_ColorNone), stvar(int32, c.color), stvar(int32, (int32)(c.vt)));

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
        TEST_FAIL(1, _SL("one of the basic conPuts/conPutsz/conPutc/conNL writes failed"), stvNone);
    }

    string out = 0;
    conMemGet(con, &out);
    bool ok = strEq(out, _SL("hello world!\n"));
    if (!ok)
        TEST_WARN(_SL("expected '${string}', got '${string}'"), stvar(strref, _SL("hello world!\n")), stvar(strref, out));
    strDestroy(&out);
    if (!ok) {
        conDestroy(&con);
        TEST_FAIL(2, _SL("first write did not round-trip -- see the preceding Warn for the actual bytes"), stvNone);
    }

    if (!conWrite(con, "XYZ", 3)) {
        conDestroy(&con);
        TEST_FAIL(3, _SL("conWrite(\"XYZ\", 3) failed"), stvNone);
    }

    conMemGet(con, &out);
    ok = strEq(out, _SL("hello world!\nXYZ"));
    if (!ok)
        TEST_WARN(_SL("expected '${string}', got '${string}'"), stvar(strref, _SL("hello world!\nXYZ")), stvar(strref, out));
    strDestroy(&out);
    conDestroy(&con);
    if (!ok)
        TEST_FAIL(4, _SL("second write did not round-trip -- see the preceding Warn for the actual bytes"), stvNone);
    return 0;
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
        int32 depth = strTestRopeDepth(rope);
        strDestroy(&rope);
        TEST_FAIL(1, _SL("expected rope depth >= 1, got ${int}"), stvar(int32, depth));
    }

    ConCaps caps   = { 0 };
    ConStream* con = conCreateMem(&caps);

    if (!conPuts(con, rope)) {
        strDestroy(&rope);
        conDestroy(&con);
        TEST_FAIL(2, _SL("conPuts(rope) failed"), stvNone);
    }

    string out    = 0;
    string expect = 0;
    conMemGet(con, &out);
    strAppend(&expect, lit66);
    strAppend(&expect, lit66);
    bool ok  = strEq(out, expect);
    int code = 0;
    if (!ok)
        TEST_FAILV(code, 3, _SL("expected '${string}', got '${string}'"), stvar(strref, expect), stvar(strref, out));
    strDestroy(&out);
    strDestroy(&expect);
    strDestroy(&rope);
    conDestroy(&con);
    return code;
}

static int test_write_utf8()
{
    ConCaps caps   = { 0 };
    ConStream* con = conCreateMem(&caps);

    conPutc(con, 0xE9);   // U+00E9 LATIN SMALL LETTER E WITH ACUTE -> 0xC3 0xA9

    string out = 0;
    conMemGet(con, &out);
    static const uint8 expect[2] = { 0xC3, 0xA9 };
    uint32 len = strLen(out);
    bool ok    = len == 2 && memcmp(strC(out), expect, 2) == 0;
    int code   = 0;
    if (!ok)
        TEST_FAILV(code, 1, _SL("expected 2-byte UTF-8 encoding 0xC3 0xA9, got ${uint} bytes: '${string}'"),
                   stvar(uint32, len), stvar(strref, out));
    strDestroy(&out);
    conDestroy(&con);
    return code;
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
        TEST_FAILV(ret, 1, _SL("conPuts under triple-nested lock returned false"), stvNone);
    } else {
        string out = 0;
        conMemGet(con, &out);
        if (!strEq(out, _SL("nested\n")))
            TEST_FAILV(ret, 2, _SL("expected '${string}', got '${string}'"), stvar(strref, _SL("nested\n")), stvar(strref, out));
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
    bool ok  = strEq(out, _SL("ab"));
    int code = 0;
    if (!ok)
        TEST_FAILV(code, 1, _SL("expected '${string}', got '${string}'"), stvar(strref, _SL("ab")), stvar(strref, out));
    strDestroy(&out);
    conDestroy(&con);
    return code;
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
    uint32 expectLen = (uint32)(LOCK_STRESS_THREADS * LOCK_STRESS_ITERS * 8);
    if (strLen(out) != expectLen) {
        TEST_FAILV(ret, 1, _SL("expected total output length ${uint}, got ${uint}"),
                   stvar(uint32, expectLen), stvar(uint32, strLen(out)));
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
                // a line was split/interleaved -- lock did not protect the write
                TEST_FAILV(ret, 2, _SL("output byte offset ${uint} does not match any of the ${int} known 8-byte lines -- write was split/interleaved"),
                           stvar(uint32, off), stvar(int32, LOCK_STRESS_THREADS));
            else
                counts[idx]++;
        }
        for (int i = 0; ret == 0 && i < LOCK_STRESS_THREADS; i++) {
            if (counts[i] != LOCK_STRESS_ITERS)
                // some writes were lost
                TEST_FAILV(ret, 3, _SL("thread ${int}: expected ${int} writes, counted ${int}"),
                           stvar(int32, i), stvar(int32, (int32)LOCK_STRESS_ITERS), stvar(int32, (int32)counts[i]));
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
    if (!ok)
        TEST_WARN(_SL("emitted bytes mismatch: expected '${string}', got '${string}'"),
                  stvar(strref, expect), stvar(strref, out));
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
        TEST_FAIL(1, _SL("truecolor downgrade mismatch -- see preceding Warn for actual bytes"), stvNone);

    caps = (ConCaps){ .vt = true, .color = CON_Color256 };
    con  = conCreateMem(&caps);
    conSetStyle(con, red);
    ok = memEq(con, _SL("\x1b[0;38;5;196m"));
    conDestroy(&con);
    if (!ok)
        TEST_FAIL(2, _SL("256-color downgrade mismatch -- see preceding Warn for actual bytes"), stvNone);

    caps = (ConCaps){ .vt = true, .color = CON_Color16 };
    con  = conCreateMem(&caps);
    conSetStyle(con, red);
    ok = memEq(con, _SL("\x1b[0;91m"));
    conDestroy(&con);
    if (!ok)
        TEST_FAIL(3, _SL("16-color downgrade mismatch -- see preceding Warn for actual bytes"), stvNone);

    caps = (ConCaps){ .vt = true, .color = CON_ColorNone };
    con  = conCreateMem(&caps);
    conSetStyle(con, red);
    ok = memEq(con, _SL("\x1b[0m"));
    conDestroy(&con);
    if (!ok)
        TEST_FAIL(4, _SL("no-color downgrade mismatch -- see preceding Warn for actual bytes"), stvNone);

    return 0;
}

static int test_style_attrs()
{
    ConCaps caps   = { .vt = true, .color = CON_ColorNone };
    ConStream* con = conCreateMem(&caps);

    conSetStyle(con, CONSTYLE(CON_ColorDefault, CON_Bold | CON_Underline));
    bool ok = memEq(con, _SL("\x1b[0;1;4m"));
    conDestroy(&con);
    if (!ok)
        TEST_FAIL(1, _SL("bold+underline style mismatch -- see preceding Warn for actual bytes"), stvNone);
    return 0;
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
    if (!ok)
        TEST_FAIL(1, _SL("dumb terminal emitted styling bytes -- see preceding Warn for actual bytes"), stvNone);
    return 0;
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
    if (!ok)
        TEST_FAIL(1, _SL("expected restored style fg ${uint} (CON_Green), got ${uint}"),
                  stvar(uint32, (uint32)CON_Green), stvar(uint32, cur.fg));
    if (!bytesOk)
        TEST_FAIL(1, _SL("restore-style byte sequence mismatch -- see preceding Warn for actual bytes"), stvNone);
    return 0;
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

    if (!ok) {
        conDestroy(&con);
        TEST_FAIL(1, _SL("conFmt() itself failed"), stvNone);
    }
    ok = memEq(con, _SL("hello world, you have 3 messages"));
    conDestroy(&con);
    if (!ok)
        TEST_FAIL(1, _SL("conFmt output mismatch -- see preceding Warn for actual bytes"), stvNone);
    return 0;
}

static int test_fmt_styled()
{
    ConCaps caps   = { .vt = true, .color = CON_Color16 };
    ConStream* con = conCreateMem(&caps);

    bool ok = conFmtS(con, CONSTYLE(CON_Red, 0), _SL("err ${int}"), stvar(int32, 7));
    if (!ok) {
        conDestroy(&con);
        TEST_FAIL(1, _SL("conFmtS() itself failed"), stvNone);
    }
    ok = memEq(con, _SL("\x1b[0;31m" "err 7" "\x1b[0m"));

    conDestroy(&con);
    if (!ok)
        TEST_FAIL(1, _SL("conFmtS output mismatch -- see preceding Warn for actual bytes"), stvNone);
    return 0;
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
    if (!ok)
        TEST_FAIL(1, _SL("conSetCursor failed or emitted the wrong bytes -- see preceding Warn if any"), stvNone);
    return 0;
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
    if (!ok)
        TEST_FAIL(1, _SL("conMoveCursor failed or emitted the wrong bytes -- see preceding Warn if any"), stvNone);
    return 0;
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
    if (!ok)
        TEST_FAIL(1, _SL("conShowCursor failed or emitted the wrong bytes -- see preceding Warn if any"), stvNone);
    return 0;
}

static int test_cursor_save_restore()
{
    ConCaps caps   = { .vt = true, .cursor = true };
    ConStream* con = conCreateMem(&caps);

    bool ok = conSaveCursor(con);
    ok      = ok && conRestoreCursor(con);
    ok      = ok && memEq(con, _SL("\x1b" "7" "\x1b" "8"));

    conDestroy(&con);
    if (!ok)
        TEST_FAIL(1, _SL("conSaveCursor/conRestoreCursor failed or emitted the wrong bytes -- see preceding Warn if any"), stvNone);
    return 0;
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
    if (!ok)
        TEST_FAIL(1, _SL("conEraseLine/conEraseScreen failed or emitted the wrong bytes -- see preceding Warn if any"), stvNone);
    return 0;
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
    if (!ok)
        TEST_FAIL(1, _SL("conScroll failed or emitted the wrong bytes -- see preceding Warn if any"), stvNone);
    return 0;
}

static int test_cursor_altscreen()
{
    ConCaps caps   = { .vt = true, .cursor = true, .altscreen = true };
    ConStream* con = conCreateMem(&caps);

    bool ok = conAltScreen(con, true);
    ok      = ok && conAltScreen(con, false);
    ok      = ok && memEq(con, _SL("\x1b[?1049h" "\x1b[?1049l"));

    conDestroy(&con);
    if (!ok)
        TEST_FAIL(1, _SL("conAltScreen failed or emitted the wrong bytes -- see preceding Warn if any"), stvNone);
    return 0;
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
    if (anyOk || !emptyOut)
        TEST_FAIL(1, _SL("expected every cursor op to fail and emit nothing on a no-cursor-capability stream; anyOk=${int}, emptyOut=${int}"),
                  stvar(int32, (int32)(anyOk)), stvar(int32, (int32)(emptyOut)));
    return 0;
}

// conGetCursor() is never satisfiable on a memory stream, even if a test fixture claims
// cursorquery support -- there is no real position behind it, per console_private.h's
// contract on _conPlatCursorGet().
static int test_cursor_getcursor_mem()
{
    ConCaps caps   = { .cursorquery = true };
    ConStream* con = conCreateMem(&caps);

    uint16 row = 0, col = 0;
    bool didGet = conGetCursor(con, &row, &col);

    conDestroy(&con);
    if (didGet)
        TEST_FAIL(1, _SL("expected conGetCursor to fail on a memory stream, but it reported row ${uint}, col ${uint}"),
                  stvar(uint32, (uint32)row), stvar(uint32, (uint32)col));
    return 0;
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
    ConDecodeResult r = _conDecodeEscape(lone, 1, &ev, &consumed);
    if (r != CON_Decode_Incomplete)
        TEST_FAIL(1, _SL("lone ESC: expected result ${int} (Incomplete), got ${int}"),
                  stvar(int32, CON_Decode_Incomplete), stvar(int32, r));

    // CSI arrow keys
    static const uint8 up[] = { 0x1B, '[', 'A' };
    r = _conDecodeEscape(up, sizeof(up), &ev, &consumed);
    if (r != CON_Decode_Matched || ev.key != CON_Key_Up || consumed != sizeof(up))
        TEST_FAIL(2, _SL("CSI up: expected result ${int}, key ${int}, consumed ${uint}; got result ${int}, key ${int}, consumed ${uint}"),
                  stvar(int32, CON_Decode_Matched), stvar(int32, CON_Key_Up), stvar(uint32, (uint32)sizeof(up)),
                  stvar(int32, r), stvar(int32, ev.key), stvar(uint32, consumed));

    // CSI ~ form (Delete)
    static const uint8 del[] = { 0x1B, '[', '3', '~' };
    r = _conDecodeEscape(del, sizeof(del), &ev, &consumed);
    if (r != CON_Decode_Matched || ev.key != CON_Key_Delete || consumed != sizeof(del))
        TEST_FAIL(3, _SL("CSI ~ delete: expected result ${int}, key ${int}, consumed ${uint}; got result ${int}, key ${int}, consumed ${uint}"),
                  stvar(int32, CON_Decode_Matched), stvar(int32, CON_Key_Delete), stvar(uint32, (uint32)sizeof(del)),
                  stvar(int32, r), stvar(int32, ev.key), stvar(uint32, consumed));

    // SS3 function keys
    static const uint8 f1[] = { 0x1B, 'O', 'P' };
    r = _conDecodeEscape(f1, sizeof(f1), &ev, &consumed);
    if (r != CON_Decode_Matched || ev.key != CON_Key_F1 || consumed != sizeof(f1))
        TEST_FAIL(4, _SL("SS3 F1: expected result ${int}, key ${int}, consumed ${uint}; got result ${int}, key ${int}, consumed ${uint}"),
                  stvar(int32, CON_Decode_Matched), stvar(int32, CON_Key_F1), stvar(uint32, (uint32)sizeof(f1)),
                  stvar(int32, r), stvar(int32, ev.key), stvar(uint32, consumed));

    // xterm modifier parameter: CSI 1 ; 5 A == Ctrl+Up (5 == 1 + ctrl-bit(4))
    static const uint8 ctrlUp[] = { 0x1B, '[', '1', ';', '5', 'A' };
    r = _conDecodeEscape(ctrlUp, sizeof(ctrlUp), &ev, &consumed);
    if (r != CON_Decode_Matched || ev.key != CON_Key_Up || ev.mods != CON_Mod_Ctrl || consumed != sizeof(ctrlUp))
        TEST_FAIL(5, _SL("Ctrl+Up: expected result ${int}, key ${int}, mods ${uint}, consumed ${uint}; got result ${int}, key ${int}, mods ${uint}, consumed ${uint}"),
                  stvar(int32, CON_Decode_Matched), stvar(int32, CON_Key_Up), stvar(uint32, (uint32)CON_Mod_Ctrl), stvar(uint32, (uint32)sizeof(ctrlUp)),
                  stvar(int32, r), stvar(int32, ev.key), stvar(uint32, ev.mods), stvar(uint32, consumed));

    // an incomplete CSI sequence (parameter digits but no final byte yet) needs more bytes
    static const uint8 partial[] = { 0x1B, '[', '1' };
    r = _conDecodeEscape(partial, sizeof(partial), &ev, &consumed);
    if (r != CON_Decode_Incomplete)
        TEST_FAIL(6, _SL("partial CSI: expected result ${int} (Incomplete), got ${int}"),
                  stvar(int32, CON_Decode_Incomplete), stvar(int32, r));

    // a CSI final byte this module doesn't recognize
    static const uint8 unknown[] = { 0x1B, '[', 'Z' };
    r = _conDecodeEscape(unknown, sizeof(unknown), &ev, &consumed);
    if (r != CON_Decode_NoMatch)
        TEST_FAIL(7, _SL("unrecognized CSI final byte: expected result ${int} (NoMatch), got ${int}"),
                  stvar(int32, CON_Decode_NoMatch), stvar(int32, r));

    // Alt+<char>: ESC directly followed by an ordinary byte
    static const uint8 altA[] = { 0x1B, 'a' };
    r = _conDecodeEscape(altA, sizeof(altA), &ev, &consumed);
    if (r != CON_Decode_Matched || ev.key != CON_Key_Char || ev.ch != 'a' || ev.mods != CON_Mod_Alt || consumed != 2)
        TEST_FAIL(8, _SL("Alt+a: expected result ${int}, key ${int}, ch ${int}, mods ${uint}, consumed 2; got result ${int}, key ${int}, ch ${int}, mods ${uint}, consumed ${uint}"),
                  stvar(int32, CON_Decode_Matched), stvar(int32, CON_Key_Char), stvar(int32, (int32)'a'), stvar(uint32, (uint32)CON_Mod_Alt),
                  stvar(int32, r), stvar(int32, ev.key), stvar(int32, ev.ch), stvar(uint32, ev.mods), stvar(uint32, consumed));

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
    if (anyOk)
        TEST_FAIL(1, _SL("expected every input wrapper to fail on a non-input stream, but at least one succeeded"), stvNone);
    return 0;
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
