#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "console_private.h"

#include <cx/string.h>

#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
// getenv() is fine here: single-threaded-at-startup environment reads
#pragma warning(disable : 4996)
#endif

// v is set and not empty
static bool boolenv(const char* v)
{
    return v != NULL && v[0] != '\0';
}

// v is set, not empty, and not "0"
static bool boolenvNotZero(const char* v)
{
    return boolenv(v) && !(v[0] == '0' && v[1] == '\0');
}

// TERM values known to mean "16-color ANSI terminal" without a more specific hint
static bool isKnown16ColorTerm(const char* term)
{
    static const char* const prefixes[] = { "xterm", "screen", "tmux", "rxvt", NULL };
    static const char* const exact[]    = { "alacritty", "kitty", "foot", "linux",
                                            "vt100",     "ansi",  NULL };

    if (!term)
        return false;
    for (int i = 0; prefixes[i]; i++) {
        if (strBeginsWithi((strref)term, (strref)prefixes[i]))
            return true;
    }
    for (int i = 0; exact[i]; i++) {
        if (strEqi((strref)term, (strref)exact[i]))
            return true;
    }
    return false;
}

_Use_decl_annotations_
void _conDetectCaps(ConCaps* out, bool istty, ConColorDepth termless, const char* term,
                    const char* colorterm, const char* no_color, const char* force_color,
                    const char* clicolor_force, const char* wt_session, const char* conemuansi,
                    const char* term_program, const char* lang)
{
    memset(out, 0, sizeof(*out));
    out->istty = istty;

    bool forced      = boolenvNotZero(force_color) || boolenvNotZero(clicolor_force);
    bool ttyOrForced = istty || forced;

    ConColorDepth color = CON_ColorNone;
    bool vt             = false;

    if (!ttyOrForced) {
        // not a terminal and nobody asked us to pretend otherwise
        color = CON_ColorNone;
        vt    = false;
    } else if (!term) {
        // no TERM to consult -- the platform file decides what that means here, because the
        // answer differs by platform rather than by terminal (see console_private.h)
        color = termless;
        vt    = termless != CON_ColorNone;
    } else if (strEqi((strref)term, _SL("dumb"))) {
        // explicitly incapable, even if attached to a tty
        color = CON_ColorNone;
        vt    = false;
    } else if (strEqi((strref)colorterm, _SL("truecolor")) ||
               strEqi((strref)colorterm, _SL("24bit"))) {
        color = CON_ColorTrue;
        vt    = true;
    } else if (strFindi((strref)term, 0, _SL("direct")) >= 0) {
        color = CON_ColorTrue;
        vt    = true;
    } else if (strFindi((strref)term, 0, _SL("256color")) >= 0) {
        color = CON_Color256;
        vt    = true;
    } else if (isKnown16ColorTerm(term)) {
        color = CON_Color16;
        vt    = true;
    } else {
        // a tty (or forced) with an unrecognized TERM -- assume the common case
        color = CON_Color16;
        vt    = true;
    }

    // terminal emulators that are reliably truecolor-capable regardless of TERM
    if (ttyOrForced) {
        if (boolenv(wt_session) || strEqi((strref)conemuansi, _SL("ON")) ||
            strEqi((strref)term_program, _SL("vscode"))) {
            if (color < CON_ColorTrue)
                color = CON_ColorTrue;
            vt = true;
        }
    }

    // NO_COLOR (see https://no-color.org) always wins, but only overrides color -- a
    // terminal that cannot render color may still accept cursor/screen VT sequences
    if (boolenv(no_color))
        color = CON_ColorNone;

    out->color   = color;
    out->vt      = vt;
    out->unicode = strFindi((strref)lang, 0, _SL("utf-8")) >= 0 ||
        strFindi((strref)lang, 0, _SL("utf8")) >= 0;
    out->cursor      = vt;
    out->altscreen   = vt;
    out->cursorquery = false;   // a DSR query race; platform init may override
}

_Use_decl_annotations_
void _conDetectCapsAuto(ConCaps* out, bool istty, ConColorDepth termless)
{
    _conDetectCaps(out,
                   istty,
                   termless,
                   getenv("TERM"),
                   getenv("COLORTERM"),
                   getenv("NO_COLOR"),
                   getenv("FORCE_COLOR"),
                   getenv("CLICOLOR_FORCE"),
                   getenv("WT_SESSION"),
                   getenv("ConEmuANSI"),
                   getenv("TERM_PROGRAM"),
                   getenv("LANG"));
}
