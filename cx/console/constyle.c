#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "console_private.h"

#include <cx/debug/assert.h>
#include <cx/format.h>
#include <cx/string/strbase.h>
#include <cx/string/striter.h>
#include <cx/utils/compare.h>

#include <string.h>

// ---------------------------------------------------------------------------------------
// color downgrade ladder: RGB <-> xterm 256 palette <-> standard 16 <-> none
// ---------------------------------------------------------------------------------------

typedef struct RGB {
    uint8 r, g, b;
} RGB;

// Standard xterm default 16-color palette, used only as distance-matching targets for the
// downgrade ladder -- not tied to whatever palette the real terminal actually renders.
static const RGB ANSI16[16] = {
    { 0,   0,   0   },
    { 205, 0,   0   },
    { 0,   205, 0   },
    { 205, 205, 0   },
    { 0,   0,   238 },
    { 205, 0,   205 },
    { 0,   205, 205 },
    { 229, 229, 229 },
    { 127, 127, 127 },
    { 255, 0,   0   },
    { 0,   255, 0   },
    { 255, 255, 0   },
    { 92,  92,  255 },
    { 255, 0,   255 },
    { 0,   255, 255 },
    { 255, 255, 255 },
};

static uint32 rgbDist2(RGB a, RGB b)
{
    int32 dr = (int32)a.r - (int32)b.r;
    int32 dg = (int32)a.g - (int32)b.g;
    int32 db = (int32)a.b - (int32)b.b;
    return (uint32)(dr * dr + dg * dg + db * db);
}

static uint8 nearestAnsi16(RGB c)
{
    uint8 best      = 0;
    uint32 bestDist = rgbDist2(c, ANSI16[0]);
    for (uint8 i = 1; i < 16; i++) {
        uint32 d = rgbDist2(c, ANSI16[i]);
        if (d < bestDist) {
            bestDist = d;
            best     = i;
        }
    }
    return best;
}

static RGB xterm256ToRGB(uint8 n)
{
    if (n < 16)
        return ANSI16[n];

    if (n >= 232) {
        uint8 v = (uint8)(8 + (n - 232) * 10);
        return (RGB) { v, v, v };
    }

    static const uint8 steps[6] = { 0, 95, 135, 175, 215, 255 };
    uint8 i                     = (uint8)(n - 16);
    return (RGB) { steps[i / 36], steps[(i / 6) % 6], steps[i % 6] };
}

static uint8 nearestStep(uint8 v, const uint8 steps[6])
{
    uint8 best     = 0;
    int32 bestDist = v > steps[0] ? v - steps[0] : steps[0] - v;
    for (uint8 i = 1; i < 6; i++) {
        int32 d = (int32)v - (int32)steps[i];
        d       = d < 0 ? -d : d;
        if (d < bestDist) {
            bestDist = d;
            best     = i;
        }
    }
    return best;
}

// Maps an arbitrary RGB triple to the nearest entry in the xterm 256 palette -- the 6x6x6
// color cube (16-231) or the 24-step grayscale ramp (232-255), whichever is closer.
static uint8 rgbToXterm256(RGB c)
{
    static const uint8 steps[6] = { 0, 95, 135, 175, 215, 255 };

    uint8 ir      = nearestStep(c.r, steps);
    uint8 ig      = nearestStep(c.g, steps);
    uint8 ib      = nearestStep(c.b, steps);
    RGB cube      = { steps[ir], steps[ig], steps[ib] };
    uint8 cubeIdx = (uint8)(16 + 36 * ir + 6 * ig + ib);

    uint32 gray   = ((uint32)c.r + c.g + c.b) / 3;
    uint8 gi      = (uint8)clamp((int32)(gray - 3) * 24 / 242, 0, 23);
    uint8 grayVal = (uint8)(8 + gi * 10);
    RGB grayRGB   = { grayVal, grayVal, grayVal };
    uint8 grayIdx = (uint8)(232 + gi);

    return rgbDist2(c, cube) <= rgbDist2(c, grayRGB) ? cubeIdx : grayIdx;
}

static RGB colorToRGB(uint32 color)
{
    if ((color & 0xFF000000u) == 0x02000000u)
        return (RGB) { (uint8)(color >> 16), (uint8)(color >> 8), (uint8)color };
    return xterm256ToRGB((uint8)color);   // CON_Idx tag; xterm256ToRGB handles n < 16 too
}

// Downgrades (never upgrades) a requested color to something the given depth can render
// exactly, or the nearest available approximation. CON_ColorDefault is returned unchanged by
// every branch that reaches it because the caller already short-circuits depth == None.
static uint32 resolveColorTo(uint32 color, ConColorDepth depth)
{
    if (color == CON_ColorDefault || depth == CON_ColorNone)
        return CON_ColorDefault;

    bool isRGB   = (color & 0xFF000000u) == 0x02000000u;
    bool isIdx   = (color & 0xFF000000u) == 0x01000000u;
    uint8 idxVal = (uint8)color;

    if (isIdx && idxVal < 16)
        return color;   // the base 16 are representable at every non-None depth
    if (isRGB && depth == CON_ColorTrue)
        return color;   // exact match available
    if (isIdx && depth != CON_Color16)
        return color;   // idxVal >= 16, and the stream can render the 256 palette exactly

    RGB rgb = colorToRGB(color);
    if (depth == CON_Color256)
        return CON_Idx(rgbToXterm256(rgb));
    return CON_Idx(nearestAnsi16(rgb));
}

// ---------------------------------------------------------------------------------------
// VT/ANSI SGR sequence emission
// ---------------------------------------------------------------------------------------

// Worst case: "\x1b[0" + 7 attrs (";N", 2 chars each) + fg RGB (";38;2;255;255;255", 18
// chars) + bg RGB (same, 18 chars) + "m" == 3 + 14 + 18 + 18 + 1 = 54.
#define SGR_BUF_MAX 64

static uint32 appendColorCode(char* buf, uint32 pos, uint32 color, bool bg)
{
    if ((color & 0xFF000000u) == 0x02000000u) {
        pos = _conAppendCode(buf, pos, bg ? 48 : 38);
        pos = _conAppendCode(buf, pos, 2);
        pos = _conAppendCode(buf, pos, (uint8)(color >> 16));
        pos = _conAppendCode(buf, pos, (uint8)(color >> 8));
        pos = _conAppendCode(buf, pos, (uint8)color);
        return pos;
    }

    uint8 n = (uint8)color;
    if (n < 16) {
        uint32 code = n < 8 ? (bg ? 40u : 30u) + n : (bg ? 100u : 90u) + (n - 8);
        return _conAppendCode(buf, pos, code);
    }

    pos = _conAppendCode(buf, pos, bg ? 48 : 38);
    pos = _conAppendCode(buf, pos, 5);
    return _conAppendCode(buf, pos, n);
}

// style's colors must already be resolved to the stream's capabilities (see resolveColorTo).
static uint32 buildSGR(char buf[SGR_BUF_MAX], ConStyle style)
{
    uint32 pos = 0;
    buf[pos++] = '\x1b';
    buf[pos++] = '[';
    buf[pos++] = '0';   // always start from a clean slate; no incremental SGR state to track

    if (style.attr & CON_Bold)
        pos = _conAppendCode(buf, pos, 1);
    if (style.attr & CON_Dim)
        pos = _conAppendCode(buf, pos, 2);
    if (style.attr & CON_Italic)
        pos = _conAppendCode(buf, pos, 3);
    if (style.attr & CON_Underline)
        pos = _conAppendCode(buf, pos, 4);
    if (style.attr & CON_Blink)
        pos = _conAppendCode(buf, pos, 5);
    if (style.attr & CON_Reverse)
        pos = _conAppendCode(buf, pos, 7);
    if (style.attr & CON_Strike)
        pos = _conAppendCode(buf, pos, 9);

    if (style.fg != CON_ColorDefault)
        pos = appendColorCode(buf, pos, style.fg, false);
    if (style.bg != CON_ColorDefault)
        pos = appendColorCode(buf, pos, style.bg, true);

    devAssert(pos < SGR_BUF_MAX);
    buf[pos++] = 'm';
    return pos;
}

// ---------------------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------------------

// Must be called with con locked. Applies style, updates con->curStyle/styleActive
// bookkeeping. Failures to emit (a dead pipe, a failed legacy attribute call) are not
// reported -- conSetStyle()/conResetStyle() are void, matching a "best effort" contract for
// terminal decoration that must never be the reason a program's real output is lost.
static void applyLocked(ConStream* con, ConStyle style)
{
    ConStyle resolved;
    resolved.fg   = resolveColorTo(style.fg, con->caps.color);
    resolved.bg   = resolveColorTo(style.bg, con->caps.color);
    resolved.attr = style.attr;

    if (con->caps.vt) {
        char buf[SGR_BUF_MAX];
        uint32 len = buildSGR(buf, resolved);
        _conWriteLocked(con, (const uint8*)buf, len);
    } else if (con->caps.color != CON_ColorNone && con->kind != CON_Kind_Mem) {
        // resolved was downgraded against con->caps.color, which win_console.c only ever
        // sets to CON_Color16 when caps.vt is false -- so resolved.fg/bg are always either
        // CON_ColorDefault or CON_Idx(0-15) here, exactly what the legacy backend expects.
        _conPlatSetStyleLegacy(con, resolved);
    }
    // vt == false && (color == None || kind == Mem): nothing representable to emit

    con->curStyle    = style;
    con->styleActive = style.fg != CON_ColorDefault || style.bg != CON_ColorDefault ||
        style.attr != 0;
}

_Use_decl_annotations_
void conSetStyle(ConStream* con, ConStyle style)
{
    conLock(con);
    applyLocked(con, style);
    conUnlock(con);
}

_Use_decl_annotations_
void conResetStyle(ConStream* con)
{
    static const ConStyle def = { CON_ColorDefault, CON_ColorDefault, 0 };
    conLock(con);
    applyLocked(con, def);
    conUnlock(con);
}

_Use_decl_annotations_
void conGetStyle(ConStream* con, ConStyle* out)
{
    conLock(con);
    *out = con->curStyle;
    conUnlock(con);
}

_Use_decl_annotations_
bool conWriteS(ConStream* con, ConStyle style, const void* buf, size_t sz)
{
    conLock(con);
    ConStyle saved = con->curStyle;
    applyLocked(con, style);
    bool ok = _conWriteLocked(con, (const uint8*)buf, sz);
    applyLocked(con, saved);
    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conPutsS(ConStream* con, ConStyle style, strref s)
{
    if (strEmpty(s))
        return true;

    conLock(con);
    ConStyle saved = con->curStyle;
    applyLocked(con, style);

    striter it;
    striBorrow(&it, s);
    bool ok = true;
    while (it.len > 0 && ok) {
        ok = _conWriteLocked(con, it.bytes, it.len);
        striNext(&it);
    }

    applyLocked(con, saved);
    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conPutszS(ConStream* con, ConStyle style, const char* sz)
{
    if (!sz || !*sz)
        return true;

    conLock(con);
    ConStyle saved = con->curStyle;
    applyLocked(con, style);
    bool ok = _conWriteLocked(con, (const uint8*)sz, strlen(sz));
    applyLocked(con, saved);
    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conPutcS(ConStream* con, ConStyle style, int32 codepoint)
{
    uint8 buf[4];
    uint32 n = _conUtf8Encode(buf, codepoint);

    conLock(con);
    ConStyle saved = con->curStyle;
    applyLocked(con, style);
    bool ok = _conWriteLocked(con, buf, n);
    applyLocked(con, saved);
    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool _conFmtS(ConStream* con, ConStyle style, strref fmt, int n, stvar* args)
{
    string tmp = 0;
    bool ok    = _strFormat(&tmp, fmt, n, args);
    ok         = conPutsS(con, style, tmp) && ok;
    strDestroy(&tmp);
    return ok;
}
