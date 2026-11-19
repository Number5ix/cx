#include "cxui_private.h"

// Each key's name is its CXUI_KEY_ suffix. Names are stored in settings files, so they never change.
static const char* const keyNames[CXUI_KEY_Count] = {
    [CXUI_KEY_A] = "A", [CXUI_KEY_B] = "B", [CXUI_KEY_C] = "C", [CXUI_KEY_D] = "D",
    [CXUI_KEY_E] = "E", [CXUI_KEY_F] = "F", [CXUI_KEY_G] = "G", [CXUI_KEY_H] = "H",
    [CXUI_KEY_I] = "I", [CXUI_KEY_J] = "J", [CXUI_KEY_K] = "K", [CXUI_KEY_L] = "L",
    [CXUI_KEY_M] = "M", [CXUI_KEY_N] = "N", [CXUI_KEY_O] = "O", [CXUI_KEY_P] = "P",
    [CXUI_KEY_Q] = "Q", [CXUI_KEY_R] = "R", [CXUI_KEY_S] = "S", [CXUI_KEY_T] = "T",
    [CXUI_KEY_U] = "U", [CXUI_KEY_V] = "V", [CXUI_KEY_W] = "W", [CXUI_KEY_X] = "X",
    [CXUI_KEY_Y] = "Y", [CXUI_KEY_Z] = "Z", [CXUI_KEY_0] = "0", [CXUI_KEY_1] = "1",
    [CXUI_KEY_2] = "2", [CXUI_KEY_3] = "3", [CXUI_KEY_4] = "4", [CXUI_KEY_5] = "5",
    [CXUI_KEY_6] = "6", [CXUI_KEY_7] = "7", [CXUI_KEY_8] = "8", [CXUI_KEY_9] = "9",
    [CXUI_KEY_F1] = "F1", [CXUI_KEY_F2] = "F2", [CXUI_KEY_F3] = "F3", [CXUI_KEY_F4] = "F4",
    [CXUI_KEY_F5] = "F5", [CXUI_KEY_F6] = "F6", [CXUI_KEY_F7] = "F7", [CXUI_KEY_F8] = "F8",
    [CXUI_KEY_F9] = "F9", [CXUI_KEY_F10] = "F10", [CXUI_KEY_F11] = "F11", [CXUI_KEY_F12] = "F12",
    [CXUI_KEY_F13] = "F13", [CXUI_KEY_F14] = "F14", [CXUI_KEY_F15] = "F15", [CXUI_KEY_F16] = "F16",
    [CXUI_KEY_F17] = "F17", [CXUI_KEY_F18] = "F18", [CXUI_KEY_F19] = "F19", [CXUI_KEY_F20] = "F20",
    [CXUI_KEY_F21] = "F21", [CXUI_KEY_F22] = "F22", [CXUI_KEY_F23] = "F23", [CXUI_KEY_F24] = "F24",
    [CXUI_KEY_Up] = "Up", [CXUI_KEY_Down] = "Down", [CXUI_KEY_Left] = "Left",
    [CXUI_KEY_Right] = "Right", [CXUI_KEY_Home] = "Home", [CXUI_KEY_End] = "End",
    [CXUI_KEY_PageUp] = "PageUp", [CXUI_KEY_PageDown] = "PageDown", [CXUI_KEY_Insert] = "Insert",
    [CXUI_KEY_Delete] = "Delete", [CXUI_KEY_Backspace] = "Backspace", [CXUI_KEY_Tab] = "Tab",
    [CXUI_KEY_Enter] = "Enter", [CXUI_KEY_Escape] = "Escape", [CXUI_KEY_Space] = "Space",
    [CXUI_KEY_Pad0] = "Pad0", [CXUI_KEY_Pad1] = "Pad1", [CXUI_KEY_Pad2] = "Pad2",
    [CXUI_KEY_Pad3] = "Pad3", [CXUI_KEY_Pad4] = "Pad4", [CXUI_KEY_Pad5] = "Pad5",
    [CXUI_KEY_Pad6] = "Pad6", [CXUI_KEY_Pad7] = "Pad7", [CXUI_KEY_Pad8] = "Pad8",
    [CXUI_KEY_Pad9] = "Pad9", [CXUI_KEY_PadDecimal] = "PadDecimal", [CXUI_KEY_PadAdd] = "PadAdd",
    [CXUI_KEY_PadSubtract] = "PadSubtract", [CXUI_KEY_PadMultiply] = "PadMultiply",
    [CXUI_KEY_PadDivide] = "PadDivide", [CXUI_KEY_PadEnter] = "PadEnter",
    [CXUI_KEY_Period] = "Period", [CXUI_KEY_Comma] = "Comma", [CXUI_KEY_Slash] = "Slash",
    [CXUI_KEY_Minus] = "Minus", [CXUI_KEY_Equals] = "Equals", [CXUI_KEY_Semicolon] = "Semicolon",
    [CXUI_KEY_Quote] = "Quote", [CXUI_KEY_BracketLeft] = "BracketLeft",
    [CXUI_KEY_BracketRight] = "BracketRight", [CXUI_KEY_Backslash] = "Backslash",
    [CXUI_KEY_Grave] = "Grave", [CXUI_KEY_Shift] = "Shift", [CXUI_KEY_Ctrl] = "Ctrl",
    [CXUI_KEY_Alt] = "Alt", [CXUI_KEY_Meta] = "Meta", [CXUI_KEY_CapsLock] = "CapsLock",
    [CXUI_KEY_NumLock] = "NumLock", [CXUI_KEY_ScrollLock] = "ScrollLock",
    [CXUI_KEY_PrintScreen] = "PrintScreen", [CXUI_KEY_Pause] = "Pause",
    [CXUI_KEY_ContextMenu] = "ContextMenu",
};

typedef struct KeyAlias {
    const char* name;
    uint32 value;
} KeyAlias;

// In the order cxuiKeyFormat writes them.
static const KeyAlias modNames[] = {
    { "Primary", CXUI_MOD_Primary },
    { "Ctrl", CXUI_MOD_Ctrl },
    { "Alt", CXUI_MOD_Alt },
    { "Shift", CXUI_MOD_Shift },
    { "Meta", CXUI_MOD_Meta },
};

static const KeyAlias modAliases[] = {
    { "Control", CXUI_MOD_Ctrl }, { "Cmd", CXUI_MOD_Meta },   { "Command", CXUI_MOD_Meta },
    { "Win", CXUI_MOD_Meta },     { "Super", CXUI_MOD_Meta }, { "Option", CXUI_MOD_Alt },
};

static const KeyAlias keyAliases[] = {
    { "Esc", CXUI_KEY_Escape }, { "Return", CXUI_KEY_Enter },  { "Del", CXUI_KEY_Delete },
    { "Ins", CXUI_KEY_Insert }, { "PgUp", CXUI_KEY_PageUp }, { "PgDn", CXUI_KEY_PageDown },
};

#define countof(a) (sizeof(a) / sizeof((a)[0]))

// ---- labels ----

// The platform's modifier symbols or names, in its order, and what goes between them
#if defined(_PLATFORM_MACOS)
#define PRIMARY_MOD CXUI_MOD_Meta
static const KeyAlias labelMods[] = {
    { "\xE2\x8C\x83", CXUI_MOD_Ctrl },    // ⌃
    { "\xE2\x8C\xA5", CXUI_MOD_Alt },     // ⌥
    { "\xE2\x87\xA7", CXUI_MOD_Shift },   // ⇧
    { "\xE2\x8C\x98", CXUI_MOD_Meta },    // ⌘
};
#define LABEL_SEP ""
#elif defined(_PLATFORM_WIN)
#define PRIMARY_MOD CXUI_MOD_Ctrl
static const KeyAlias labelMods[] = {
    { "Ctrl", CXUI_MOD_Ctrl }, { "Alt", CXUI_MOD_Alt }, { "Shift", CXUI_MOD_Shift }, { "Win", CXUI_MOD_Meta },
};
#define LABEL_SEP "+"
#else
#define PRIMARY_MOD CXUI_MOD_Ctrl
static const KeyAlias labelMods[] = {
    { "Ctrl", CXUI_MOD_Ctrl }, { "Alt", CXUI_MOD_Alt }, { "Shift", CXUI_MOD_Shift }, { "Super", CXUI_MOD_Meta },
};
#define LABEL_SEP "+"
#endif

static uint32 realMods(uint32 mods)
{
    if (mods & CXUI_MOD_Primary)
        mods = (mods & ~CXUI_MOD_Primary) | PRIMARY_MOD;
    return mods;
}

static const char* const padDigits[10] = { "Num 0", "Num 1", "Num 2", "Num 3", "Num 4",
                                           "Num 5", "Num 6", "Num 7", "Num 8", "Num 9" };

// A key's label where it differs from its name
static const char* labelName(CxUiKey key)
{
    if (key >= CXUI_KEY_Pad0 && key <= CXUI_KEY_Pad9)
        return padDigits[key - CXUI_KEY_Pad0];
    switch (key) {
    case CXUI_KEY_Period:       return ".";
    case CXUI_KEY_Comma:        return ",";
    case CXUI_KEY_Slash:        return "/";
    case CXUI_KEY_Minus:        return "-";
    case CXUI_KEY_Equals:       return "=";
    case CXUI_KEY_Semicolon:    return ";";
    case CXUI_KEY_Quote:        return "'";
    case CXUI_KEY_BracketLeft:  return "[";
    case CXUI_KEY_BracketRight: return "]";
    case CXUI_KEY_Backslash:    return "\\";
    case CXUI_KEY_Grave:        return "`";
    case CXUI_KEY_PadDecimal:   return "Num .";
    case CXUI_KEY_PadAdd:       return "Num +";
    case CXUI_KEY_PadSubtract:  return "Num -";
    case CXUI_KEY_PadMultiply:  return "Num *";
    case CXUI_KEY_PadDivide:    return "Num /";
#if defined(_PLATFORM_MACOS)
    case CXUI_KEY_Up:        return "\xE2\x86\x91";   // ↑
    case CXUI_KEY_Down:      return "\xE2\x86\x93";   // ↓
    case CXUI_KEY_Left:      return "\xE2\x86\x90";   // ←
    case CXUI_KEY_Right:     return "\xE2\x86\x92";   // →
    case CXUI_KEY_Home:      return "\xE2\x86\x96";   // ↖
    case CXUI_KEY_End:       return "\xE2\x86\x98";   // ↘
    case CXUI_KEY_PageUp:    return "\xE2\x87\x9E";   // ⇞
    case CXUI_KEY_PageDown:  return "\xE2\x87\x9F";   // ⇟
    case CXUI_KEY_Delete:    return "\xE2\x8C\xA6";   // ⌦
    case CXUI_KEY_Backspace: return "\xE2\x8C\xAB";   // ⌫
    case CXUI_KEY_Tab:       return "\xE2\x87\xA5";   // ⇥
    case CXUI_KEY_Enter:     return "\xE2\x86\xA9";   // ↩
    case CXUI_KEY_PadEnter:  return "\xE2\x8C\xA4";   // ⌤
    case CXUI_KEY_Escape:    return "\xE2\x8E\x8B";   // ⎋
    case CXUI_KEY_CapsLock:  return "\xE2\x87\xAA";   // ⇪
    case CXUI_KEY_Shift:     return "\xE2\x87\xA7";   // ⇧
    case CXUI_KEY_Ctrl:      return "\xE2\x8C\x83";   // ⌃
    case CXUI_KEY_Alt:       return "\xE2\x8C\xA5";   // ⌥
    case CXUI_KEY_Meta:      return "\xE2\x8C\x98";   // ⌘
#else
    case CXUI_KEY_PageUp:      return "Page Up";
    case CXUI_KEY_PageDown:    return "Page Down";
    case CXUI_KEY_Escape:      return "Esc";
    case CXUI_KEY_PadEnter:    return "Num Enter";
    case CXUI_KEY_CapsLock:    return "Caps Lock";
    case CXUI_KEY_NumLock:     return "Num Lock";
    case CXUI_KEY_ScrollLock:  return "Scroll Lock";
    case CXUI_KEY_PrintScreen: return "Print Screen";
    case CXUI_KEY_ContextMenu: return "Menu";
#if defined(_PLATFORM_WIN)
    case CXUI_KEY_Meta:        return "Win";
#else
    case CXUI_KEY_Meta:        return "Super";
#endif
#endif
    default:
        return NULL;
    }
}

_Use_decl_annotations_
bool cxuiKeyLabel(string* out, CxUiKey key, uint32 mods)
{
    strClear(out);
    if (key <= CXUI_KEY_None || key >= CXUI_KEY_Count)
        return false;

    mods = realMods(mods);
    for (size_t i = 0; i < countof(labelMods); i++) {
        if (mods & labelMods[i].value) {
            strAppend(out, (strref)labelMods[i].name);
            strAppend(out, (strref)LABEL_SEP);
        }
    }
    const char* name = labelName(key);
    strAppend(out, (strref)(name ? name : keyNames[key]));
    return true;
}

_Use_decl_annotations_
bool cxuiKeyFormat(string* out, CxUiKey key, uint32 mods)
{
    strClear(out);
    if (key <= CXUI_KEY_None || key >= CXUI_KEY_Count)
        return false;

    for (size_t i = 0; i < countof(modNames); i++) {
        if (mods & modNames[i].value) {
            strAppend(out, (strref)modNames[i].name);
            strAppend(out, _SL("+"));
        }
    }
    strAppend(out, (strref)keyNames[key]);
    return true;
}

static bool lookup(const KeyAlias* table, size_t n, strref name, uint32* out)
{
    for (size_t i = 0; i < n; i++) {
        if (strEqi(name, (strref)table[i].name)) {
            *out = table[i].value;
            return true;
        }
    }
    return false;
}

static bool parseMod(strref name, uint32* mod)
{
    return lookup(modNames, countof(modNames), name, mod) || lookup(modAliases, countof(modAliases), name, mod);
}

static bool parseKey(strref name, CxUiKey* key)
{
    for (int32 k = CXUI_KEY_None + 1; k < CXUI_KEY_Count; k++) {
        if (strEqi(name, (strref)keyNames[k])) {
            *key = (CxUiKey)k;
            return true;
        }
    }
    uint32 alias;
    if (lookup(keyAliases, countof(keyAliases), name, &alias)) {
        *key = (CxUiKey)alias;
        return true;
    }
    return false;
}

_Use_decl_annotations_
bool cxuiKeyParse(CxUiKey* key, uint32* mods, strref name)
{
    *key  = CXUI_KEY_None;
    *mods = 0;

    sa_string parts;
    saInit(&parts, string, 4);
    int32 n = strSplit(&parts, name, _SL("+"), true);

    bool ok = n > 0;
    uint32 m = 0;
    CxUiKey k = CXUI_KEY_None;
    for (int32 i = 0; ok && i < n; i++) {
        strTrim(&parts.a[i], parts.a[i], _SL(" \t"));
        // The last part is the key; a modifier on its own is a key too ("Shift").
        if (i < n - 1) {
            uint32 mod;
            ok = parseMod(parts.a[i], &mod);
            m |= mod;
        } else {
            ok = parseKey(parts.a[i], &k);
        }
    }
    saDestroy(&parts);

    if (!ok)
        return false;
    *key  = k;
    *mods = m;
    return true;
}

bool cxuiKeyMatches(CxUiKey key, uint32 mods, CxUiKey evKey, uint32 evMods)
{
    const uint32 real = CXUI_MOD_Shift | CXUI_MOD_Ctrl | CXUI_MOD_Alt | CXUI_MOD_Meta;
    return key != CXUI_KEY_None && key == evKey && (realMods(mods) & real) == (evMods & real);
}

// ---- mnemonics ----

_Use_decl_annotations_
void _cxuiMnemonicStrip(string* out, strref text)
{
    strClear(out);
    string part = 0;
    int32 len   = strLen(text);
    for (int32 at = 0; at < len;) {
        int32 amp = strFindChar(text, at, '&');
        int32 end = amp < 0 ? len : amp;
        strSubStr(&part, text, at, end);
        strAppend(out, part);
        if (amp < 0 || amp + 1 >= len)
            break;
        // The character after the `&`, which is itself for `&&`
        strSubStr(&part, text, amp + 1, amp + 2);
        strAppend(out, part);
        at = amp + 2;
    }
    strDestroy(&part);
}

_Use_decl_annotations_
CxUiKey _cxuiMnemonicKey(strref text)
{
    int32 len = strLen(text);
    for (int32 at = 0; at + 1 < len;) {
        int32 amp = strFindChar(text, at, '&');
        if (amp < 0 || amp + 1 >= len)
            break;
        char c = strC(text)[amp + 1];
        if (c >= 'a' && c <= 'z')
            return (CxUiKey)(CXUI_KEY_A + (c - 'a'));
        if (c >= 'A' && c <= 'Z')
            return (CxUiKey)(CXUI_KEY_A + (c - 'A'));
        if (c >= '0' && c <= '9')
            return (CxUiKey)(CXUI_KEY_0 + (c - '0'));
        if (c != '&')
            break;
        at = amp + 2;
    }
    return CXUI_KEY_None;
}

uint32 cxuiKeyLocks(void)
{
    return _cxui.backend && _cxui.backend->keyLocks ? _cxui.backend->keyLocks() : 0;
}
