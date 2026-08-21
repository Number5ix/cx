// Interactive console capability probe and color cycler.
//
// Prints what cx detected for conOut(), what the platform reports underneath, and then
// cycles through every color/attribute path so the two can be compared by eye. This is not
// a unit test -- contest.c covers the memory-backed stream. condemo exists to be pointed at
// a real terminal when detection and reality disagree.
//
// Usage: condemo [--term=VALUE] [--no-term] [--force] [--probe-only]
//
//   --term=VALUE   set TERM before the console singleton is created
//   --no-term      clear TERM before the console singleton is created
//   --force        set FORCE_COLOR=1
//   --probe-only   print the capability report and stop

#include <cx/console.h>
#include <cx/format.h>
#include <cx/string.h>
#include <cx/sys/entry.h>

#include <stdlib.h>

#if defined(_PLATFORM_WIN)
#include <cx/platform/win.h>
#include <wchar.h>
#endif

DEFINE_ENTRY_POINT;

static strref yn(bool b)
{
    return b ? _S"yes" : _S"no";
}

// ---------------------------------------------------------------------------------------
// environment
// ---------------------------------------------------------------------------------------

// Must run before the first conOut() call -- the singleton snapshots its caps on creation.
static void setEnv(const char* name, const char* val)
{
#if defined(_MSC_VER)
    _putenv_s(name, val ? val : "");
#else
    if (val)
        setenv(name, val, 1);
    else
        unsetenv(name);
#endif
}

static void reportEnv(ConStream* con)
{
    static const char* const names[] = { "TERM",       "COLORTERM",      "NO_COLOR",
                                         "FORCE_COLOR", "CLICOLOR_FORCE", "WT_SESSION",
                                         "ConEmuANSI", "TERM_PROGRAM",   "LANG",
                                         NULL };

    conPuts(con, _SL("Environment\n"));
    for (int i = 0; names[i]; i++) {
        const char* v = getenv(names[i]);
        conFmt(con,
               _SL("  ${string(16,left)}${string}\n"),
               stvar(strref, (strref)names[i]),
               stvar(strref, v ? (strref)v : _S"<unset>"));
    }
    conPuts(con, _SL("\n"));
}

// ---------------------------------------------------------------------------------------
// capability report
// ---------------------------------------------------------------------------------------

static strref depthName(ConColorDepth d)
{
    switch (d) {
    case CON_ColorNone:
        return _S"None";
    case CON_Color16:
        return _S"16";
    case CON_Color256:
        return _S"256";
    case CON_ColorTrue:
        return _S"TrueColor";
    default:
        return _S"?";
    }
}

static void reportCaps(ConStream* con, ConCaps* caps)
{
    conPuts(con, _SL("Detected caps for conOut()\n"));
    conFmt(con, _SL("  istty        ${string}\n"), stvar(strref, yn(caps->istty)));
    conFmt(con, _SL("  vt           ${string}\n"), stvar(strref, yn(caps->vt)));
    conFmt(con, _SL("  color        ${string}\n"), stvar(strref, depthName(caps->color)));
    conFmt(con, _SL("  unicode      ${string}\n"), stvar(strref, yn(caps->unicode)));
    conFmt(con, _SL("  cursor       ${string}\n"), stvar(strref, yn(caps->cursor)));
    conFmt(con, _SL("  altscreen    ${string}\n"), stvar(strref, yn(caps->altscreen)));
    conFmt(con, _SL("  cursorquery  ${string}\n"), stvar(strref, yn(caps->cursorquery)));
    conFmt(con,
           _SL("  size         ${uint}x${uint}\n"),
           stvar(uint16, caps->width),
           stvar(uint16, caps->height));

    // The combination that produces silence: vt is true so applyLocked() takes the SGR
    // path, but resolveColorTo() collapses every color against CON_ColorNone first, so the
    // sequences that go out carry no color at all.
    if (caps->istty && caps->vt && caps->color == CON_ColorNone) {
        conPutsS(con,
                 CONSTYLE(CON_ColorDefault, CON_Bold),
                 _SL("\n  !! vt is on but color depth is None -- cx will emit bare SGR\n"
                     "     resets and nothing below this line will have color.\n"));
    }
    conPuts(con, _SL("\n"));
}

// ---------------------------------------------------------------------------------------
// platform ground truth
// ---------------------------------------------------------------------------------------

#if defined(_PLATFORM_WIN)

static void reportMode(ConStream* con, strref label, DWORD which, bool isInput)
{
    HANDLE h = GetStdHandle(which);
    conFmt(con, _SL("  ${string(8,left)}"), stvar(strref, label));

    if (h == NULL || h == INVALID_HANDLE_VALUE) {
        conPuts(con, _SL("<no handle>\n"));
        return;
    }

    DWORD mode = 0;
    if (!GetConsoleMode(h, &mode)) {
        conFmt(con,
               _SL("not a console (GetConsoleMode err ${uint})\n"),
               stvar(uint32, (uint32)GetLastError()));
        return;
    }

    conFmt(con, _SL("mode=0x${0uint(8,hex)}"), stvar(uint32, (uint32)mode));

    if (isInput) {
        // 0x0004 is ENABLE_ECHO_INPUT on an input handle, not VT processing. Reading the
        // wrong handle is an easy way to conclude VT is already enabled when it is not.
        conFmt(con,
               _SL("  echo=${string} vtinput=${string}\n"),
               stvar(strref, yn((mode & ENABLE_ECHO_INPUT) != 0)),
               stvar(strref, yn((mode & ENABLE_VIRTUAL_TERMINAL_INPUT) != 0)));
        return;
    }

    conFmt(con,
           _SL("  processed=${string} wrap=${string} vt=${string}\n"),
           stvar(strref, yn((mode & ENABLE_PROCESSED_OUTPUT) != 0)),
           stvar(strref, yn((mode & ENABLE_WRAP_AT_EOL_OUTPUT) != 0)),
           stvar(strref, yn((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0)));

    // Set the bit and read it straight back. A host that accepts the call without honoring
    // VT reports the bit clear here.
    DWORD probe = mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (SetConsoleMode(h, probe)) {
        DWORD after = 0;
        GetConsoleMode(h, &after);
        conFmt(con,
               _SL("          vt set ok, reads back ${string}\n"),
               stvar(strref, yn((after & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0)));
        SetConsoleMode(h, mode);
    } else {
        conFmt(con,
               _SL("          vt set FAILED (err ${uint}) -- legacy console\n"),
               stvar(uint32, (uint32)GetLastError()));
    }

    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(h, &info)) {
        conFmt(con,
               _SL("          attrs=0x${0uint(4,hex)}\n"),
               stvar(uint32, (uint32)info.wAttributes));
    }
}

static void reportPlatform(ConStream* con)
{
    conPuts(con, _SL("Win32 console state\n"));
    reportMode(con, _S"stdout", STD_OUTPUT_HANDLE, false);
    reportMode(con, _S"stderr", STD_ERROR_HANDLE, false);
    reportMode(con, _S"stdin", STD_INPUT_HANDLE, true);
    conFmt(con, _SL("  outcp   ${uint}\n\n"), stvar(uint32, (uint32)GetConsoleOutputCP()));
}

// Writes SGR bytes straight at the console handle, bypassing cx styling entirely. If this
// shows color but the cx section below does not, the host is fine and the fault is in
// capability detection.
static void rawVTCheck(ConStream* con)
{
    conPuts(con, _SL("Raw WriteConsoleW SGR (bypasses cx styling)\n  "));
    conFlush(con);

    HANDLE h   = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h == NULL || h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode)) {
        conPuts(con, _SL("<not a console>\n\n"));
        return;
    }
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    static const wchar_t seq[] = L"\x1b[31mRED\x1b[32m GREEN\x1b[34m BLUE\x1b[0m"
                                 L" \x1b[1mBOLD\x1b[0m \x1b[7mREVERSE\x1b[0m\r\n";
    DWORD written = 0;
    WriteConsoleW(h, seq, (DWORD)(sizeof(seq) / sizeof(seq[0]) - 1), &written, NULL);
    SetConsoleMode(h, mode);

    conPuts(con, _SL("\nLegacy SetConsoleTextAttribute (bypasses cx styling)\n  "));
    conFlush(con);

    CONSOLE_SCREEN_BUFFER_INFO info;
    WORD orig = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    if (GetConsoleScreenBufferInfo(h, &info))
        orig = info.wAttributes;

    static const WORD legacy[] = { FOREGROUND_RED,
                                   FOREGROUND_GREEN,
                                   FOREGROUND_BLUE,
                                   FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY };
    static const wchar_t* const legacyNames[] = { L"RED ", L"GREEN ", L"BLUE ", L"YELLOW " };
    for (int i = 0; i < 4; i++) {
        SetConsoleTextAttribute(h, legacy[i]);
        WriteConsoleW(h, legacyNames[i], (DWORD)wcslen(legacyNames[i]), &written, NULL);
    }
    SetConsoleTextAttribute(h, orig);
    WriteConsoleW(h, L"\r\n\r\n", 4, &written, NULL);
}

#else

static void reportPlatform(ConStream* con)
{
    (void)con;
}

static void rawVTCheck(ConStream* con)
{
    conPuts(con, _SL("Raw SGR write (bypasses cx styling)\n  "));
    conPutsz(con,
             "\x1b[31mRED\x1b[32m GREEN\x1b[34m BLUE\x1b[0m"
             " \x1b[1mBOLD\x1b[0m \x1b[7mREVERSE\x1b[0m\n\n");
}

#endif

// ---------------------------------------------------------------------------------------
// color cycling
// ---------------------------------------------------------------------------------------

static const char* const kBaseNames[16] = { "black",    "red",      "green",    "yellow",
                                            "blue",     "magenta",  "cyan",     "white",
                                            "br.black", "br.red",   "br.green", "br.yellow",
                                            "br.blue",  "br.mag",   "br.cyan",  "br.white" };

static void cycleBase16(ConStream* con)
{
    conPuts(con, _SL("16-color foreground\n"));
    for (int i = 0; i < 16; i++) {
        if (i == 8)
            conPuts(con, _SL("\n"));
        conFmtS(con,
                CONSTYLE(CON_Idx((uint8)i), 0),
                _SL("  ${string(9,left)}"),
                stvar(strref, (strref)kBaseNames[i]));
    }

    conPuts(con, _SL("\n\n16-color background\n"));
    for (int i = 0; i < 16; i++) {
        if (i == 8)
            conPuts(con, _SL("\n"));
        // dark backgrounds get white text, light ones black, so the label stays readable
        uint32 fg = (i == 0 || i == 4 || i == 8) ? CON_BrightWhite : CON_Black;
        conFmtS(con,
                CONSTYLE2(fg, CON_Idx((uint8)i), 0),
                _SL("  ${string(9,left)}"),
                stvar(strref, (strref)kBaseNames[i]));
    }
    conPuts(con, _SL("\n\n"));
}

static void cycleAttrs(ConStream* con)
{
    static const struct {
        flags_t attr;
        const char* name;
    } attrs[] = {
        { CON_Bold, "bold" },   { CON_Dim, "dim" },         { CON_Italic, "italic" },
        { CON_Underline, "under" }, { CON_Blink, "blink" }, { CON_Reverse, "reverse" },
        { CON_Strike, "strike" },
    };

    conPuts(con, _SL("Attributes (default color)\n"));
    for (size_t i = 0; i < sizeof(attrs) / sizeof(attrs[0]); i++) {
        conFmtS(con,
                CONSTYLE(CON_ColorDefault, attrs[i].attr),
                _SL("  ${string(9,left)}"),
                stvar(strref, (strref)attrs[i].name));
    }

    conPuts(con, _SL("\n\nAttributes (on cyan)\n"));
    for (size_t i = 0; i < sizeof(attrs) / sizeof(attrs[0]); i++) {
        conFmtS(con,
                CONSTYLE(CON_Cyan, attrs[i].attr),
                _SL("  ${string(9,left)}"),
                stvar(strref, (strref)attrs[i].name));
    }
    conPuts(con, _SL("\n\n"));
}

static void cycle256(ConStream* con)
{
    conPuts(con, _SL("256-color palette (downgrades to 16 on a Color16 stream)\n"));
    for (int i = 0; i < 256; i++) {
        if (i % 32 == 0)
            conPuts(con, _SL("  "));
        conPutcS(con, CONSTYLE2(CON_Black, CON_Idx((uint8)i), 0), ' ');
        conPutcS(con, CONSTYLE2(CON_Black, CON_Idx((uint8)i), 0), ' ');
        if (i % 32 == 31)
            conPuts(con, _SL("\n"));
    }
    conPuts(con, _SL("\n"));
}

static void cycleTrueColor(ConStream* con)
{
    uint16 w = conWidth(con);
    if (w < 8)
        w = 80;
    w = (uint16)(w - 4);

    conPuts(con, _SL("Truecolor ramp (downgrades to the stream's depth)\n"));

    // hue sweep, then a gray ramp -- a Color16 stream should show coarse banding here, not
    // garbage, and a CON_ColorNone stream shows nothing but spaces
    for (uint16 x = 0; x < w; x++) {
        uint32 t   = (uint32)x * 6 * 256 / w;
        uint8 f    = (uint8)(t & 0xff);
        uint8 r = 0, g = 0, b = 0;
        switch (t >> 8) {
        case 0:
            r = 255;
            g = f;
            break;
        case 1:
            r = (uint8)(255 - f);
            g = 255;
            break;
        case 2:
            g = 255;
            b = f;
            break;
        case 3:
            g = (uint8)(255 - f);
            b = 255;
            break;
        case 4:
            r = f;
            b = 255;
            break;
        default:
            r = 255;
            b = (uint8)(255 - f);
            break;
        }
        conPutcS(con, CONSTYLE2(CON_Black, CON_RGB(r, g, b), 0), ' ');
    }
    conPuts(con, _SL("\n"));

    for (uint16 x = 0; x < w; x++) {
        uint8 v = (uint8)((uint32)x * 255 / w);
        conPutcS(con, CONSTYLE2(CON_Black, CON_RGB(v, v, v), 0), ' ');
    }
    conPuts(con, _SL("\n\n"));
}

// ---------------------------------------------------------------------------------------

int entryPoint()
{
    bool probeOnly = false;

    // Env overrides must be applied before the first conOut() touch -- the singleton
    // snapshots its capabilities when it is created.
    for (int32 i = 0; i < saSize(cmdArgs); i++) {
        strref arg = cmdArgs.a[i];

        if (strEq(arg, _SL("--probe-only"))) {
            probeOnly = true;
        } else if (strEq(arg, _SL("--no-term"))) {
            setEnv("TERM", NULL);
        } else if (strEq(arg, _SL("--force"))) {
            setEnv("FORCE_COLOR", "1");
        } else if (strBeginsWith(arg, _SL("--term="))) {
            string val = 0;
            strSubStr(&val, arg, 7, strEnd);
            setEnv("TERM", strC(val));
            strDestroy(&val);
        }
    }

    ConStream* con = conOut();
    ConCaps caps;
    conGetCaps(con, &caps);

    conPuts(con, _SL("\n=== cx console probe ===\n\n"));
    reportEnv(con);
    reportPlatform(con);
    reportCaps(con, &caps);
    rawVTCheck(con);

    if (!probeOnly) {
        conPuts(con, _SL("=== cx styled output ===\n\n"));
        cycleBase16(con);
        cycleAttrs(con);
        cycle256(con);
        cycleTrueColor(con);
        conResetStyle(con);
    }

    conFlush(con);
    conShutdown();
    return 0;
}
