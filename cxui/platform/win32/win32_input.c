#include "win32_ui.h"

#include <windowsx.h>

// ---- keys ----

static CxUiKey vkToKey(WPARAM vk, LPARAM lp)
{
    bool extended = (lp & 0x01000000) != 0;
    if (vk >= 'A' && vk <= 'Z')
        return (CxUiKey)(CXUI_KEY_A + (vk - 'A'));
    if (vk >= '0' && vk <= '9')
        return (CxUiKey)(CXUI_KEY_0 + (vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F24)
        return (CxUiKey)(CXUI_KEY_F1 + (vk - VK_F1));
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return (CxUiKey)(CXUI_KEY_Pad0 + (vk - VK_NUMPAD0));
    switch (vk) {
    case VK_UP:         return CXUI_KEY_Up;
    case VK_DOWN:       return CXUI_KEY_Down;
    case VK_LEFT:       return CXUI_KEY_Left;
    case VK_RIGHT:      return CXUI_KEY_Right;
    case VK_HOME:       return CXUI_KEY_Home;
    case VK_END:        return CXUI_KEY_End;
    case VK_PRIOR:      return CXUI_KEY_PageUp;
    case VK_NEXT:       return CXUI_KEY_PageDown;
    case VK_INSERT:     return CXUI_KEY_Insert;
    case VK_DELETE:     return CXUI_KEY_Delete;
    case VK_BACK:       return CXUI_KEY_Backspace;
    case VK_TAB:        return CXUI_KEY_Tab;
    case VK_RETURN:     return extended ? CXUI_KEY_PadEnter : CXUI_KEY_Enter;
    case VK_ESCAPE:     return CXUI_KEY_Escape;
    case VK_SPACE:      return CXUI_KEY_Space;
    case VK_DECIMAL:    return CXUI_KEY_PadDecimal;
    case VK_ADD:        return CXUI_KEY_PadAdd;
    case VK_SUBTRACT:   return CXUI_KEY_PadSubtract;
    case VK_MULTIPLY:   return CXUI_KEY_PadMultiply;
    case VK_DIVIDE:     return CXUI_KEY_PadDivide;
    case VK_OEM_PERIOD: return CXUI_KEY_Period;
    case VK_OEM_COMMA:  return CXUI_KEY_Comma;
    case VK_OEM_2:      return CXUI_KEY_Slash;
    case VK_OEM_MINUS:  return CXUI_KEY_Minus;
    case VK_OEM_PLUS:   return CXUI_KEY_Equals;
    case VK_OEM_1:      return CXUI_KEY_Semicolon;
    case VK_OEM_7:      return CXUI_KEY_Quote;
    case VK_OEM_4:      return CXUI_KEY_BracketLeft;
    case VK_OEM_6:      return CXUI_KEY_BracketRight;
    case VK_OEM_5:      return CXUI_KEY_Backslash;
    case VK_OEM_3:      return CXUI_KEY_Grave;
    case VK_SHIFT:      return CXUI_KEY_Shift;
    case VK_CONTROL:    return CXUI_KEY_Ctrl;
    case VK_MENU:       return CXUI_KEY_Alt;
    case VK_LWIN:
    case VK_RWIN:       return CXUI_KEY_Meta;
    case VK_CAPITAL:    return CXUI_KEY_CapsLock;
    case VK_NUMLOCK:    return CXUI_KEY_NumLock;
    case VK_SCROLL:     return CXUI_KEY_ScrollLock;
    case VK_SNAPSHOT:   return CXUI_KEY_PrintScreen;
    case VK_PAUSE:      return CXUI_KEY_Pause;
    case VK_APPS:       return CXUI_KEY_ContextMenu;
    }
    return CXUI_KEY_None;
}

static uint32 keyMods(void)
{
    uint32 mods = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000)
        mods |= CXUI_MOD_Shift;
    if (GetKeyState(VK_CONTROL) & 0x8000)
        mods |= CXUI_MOD_Ctrl;
    if (GetKeyState(VK_MENU) & 0x8000)
        mods |= CXUI_MOD_Alt;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000)
        mods |= CXUI_MOD_Meta;
    return mods;
}

_Use_decl_annotations_
bool _cxuiWin32Key(CxUiPeer* p, UINT msg, WPARAM wp, LPARAM lp)
{
    CxUiKey key = vkToKey(wp, lp);
    if (key == CXUI_KEY_None)
        return false;
    bool down       = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
    uint32 scancode = ((uint32)(lp >> 16) & 0xFF) | ((lp & 0x01000000) ? 0x100 : 0);
    _cxuiNativeEnter();
    CxUiResult r = _cxuiKey(p, key, keyMods(), scancode, down);
    _cxuiNativeLeave();
    return r == CXUI_Handled;
}

// Also the child activation gives focus back to, for a window not active yet
void _cxuiWin32PeerFocus(CxUiPeer* p)
{
    HWND hwnd = _cxuiWin32PeerHwnd(p);
    _cxuiWin32RememberFocus(hwnd);
    SetFocus(hwnd);
}

static WPARAM keyToVk(CxUiKey key)
{
    for (WPARAM vk = 1; vk < 255; vk++) {
        if (vkToKey(vk, 0) == key)
            return vk;
    }
    return 0;
}

// Down and up as the message loop would deliver them, with TranslateMessage making the
// character, and the modifiers held in this thread's keyboard state meanwhile.
bool _cxuiWin32TestNativeKey(CxUiPeer* p, CxUiKey key, uint32 mods)
{
    WPARAM vk = keyToVk(key);
    HWND hwnd = _cxuiWin32PeerHwnd(p);
    if (!vk || !hwnd)
        return false;
    BYTE saved[256], ks[256];
    GetKeyboardState(saved);
    memcpy(ks, saved, sizeof(ks));
    if (mods & CXUI_MOD_Shift)
        ks[VK_SHIFT] = ks[VK_LSHIFT] = 0x80;
    if (mods & CXUI_MOD_Ctrl)
        ks[VK_CONTROL] = ks[VK_LCONTROL] = 0x80;
    if (mods & CXUI_MOD_Alt)
        ks[VK_MENU] = ks[VK_LMENU] = 0x80;
    SetKeyboardState(ks);

    UINT scan   = MapVirtualKeyW((UINT)vk, 0);
    bool sys    = (mods & CXUI_MOD_Alt) != 0;
    MSG down    = { hwnd, sys ? WM_SYSKEYDOWN : WM_KEYDOWN, vk, 1 | (scan << 16) };
    MSG up      = { hwnd, sys ? WM_SYSKEYUP : WM_KEYUP, vk, 1 | (scan << 16) | 0xC0000000 };
    TranslateMessage(&down);
    DispatchMessageW(&down);
    MSG ch;
    while (PeekMessageW(&ch, hwnd, WM_CHAR, WM_DEADCHAR, PM_REMOVE) ||
           PeekMessageW(&ch, hwnd, WM_SYSCHAR, WM_SYSDEADCHAR, PM_REMOVE))
        DispatchMessageW(&ch);
    // The key down may have moved the focus; the release goes where it is now, as Windows sends it.
    HWND focus = GetFocus();
    up.hwnd    = focus ? focus : hwnd;
    DispatchMessageW(&up);
    SetKeyboardState(saved);
    return true;
}

// ---- pointer ----

static uint32 mouseButtons(WPARAM wp)
{
    uint32 b = 0;
    if (wp & MK_LBUTTON)
        b |= CXUI_BTN_Left;
    if (wp & MK_MBUTTON)
        b |= CXUI_BTN_Middle;
    if (wp & MK_RBUTTON)
        b |= CXUI_BTN_Right;
    if (wp & MK_XBUTTON1)
        b |= CXUI_BTN_X1;
    if (wp & MK_XBUTTON2)
        b |= CXUI_BTN_X2;
    return b;
}

static uint32 mouseMods(WPARAM wp)
{
    uint32 mods = keyMods() & ~(CXUI_MOD_Shift | CXUI_MOD_Ctrl);
    if (wp & MK_SHIFT)
        mods |= CXUI_MOD_Shift;
    if (wp & MK_CONTROL)
        mods |= CXUI_MOD_Ctrl;
    return mods;
}

_Use_decl_annotations_
bool _cxuiWin32Pointer(CxUiPeer* p, HWND hwnd, bool* tracking, UINT msg, WPARAM wp, LPARAM lp)
{
    CxUiPointerKind kind;
    uint32 button = 0;
    switch (msg) {
    case WM_MOUSEMOVE:
        kind = CXUI_PTR_Move;
        if (!*tracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            *tracking           = TrackMouseEvent(&tme) != 0;
        }
        break;
    case WM_MOUSELEAVE:
        *tracking = false;
        _cxuiNativeEnter();
        _cxuiCanvasPointer(p, CXUI_PTR_Left, 0, 0, keyMods(), (CxUiPoint){ 0, 0 });
        _cxuiNativeLeave();
        return true;
    case WM_LBUTTONDOWN: kind = CXUI_PTR_Press;   button = CXUI_BTN_Left;   break;
    case WM_LBUTTONUP:   kind = CXUI_PTR_Release; button = CXUI_BTN_Left;   break;
    case WM_MBUTTONDOWN: kind = CXUI_PTR_Press;   button = CXUI_BTN_Middle; break;
    case WM_MBUTTONUP:   kind = CXUI_PTR_Release; button = CXUI_BTN_Middle; break;
    case WM_RBUTTONDOWN: kind = CXUI_PTR_Press;   button = CXUI_BTN_Right;  break;
    case WM_RBUTTONUP:   kind = CXUI_PTR_Release; button = CXUI_BTN_Right;  break;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        kind   = msg == WM_XBUTTONDOWN ? CXUI_PTR_Press : CXUI_PTR_Release;
        button = GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? CXUI_BTN_X1 : CXUI_BTN_X2;
        break;
    default:
        return false;
    }
    // Signed: under capture, positions can be outside the canvas.
    CxUiPoint px = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    _cxuiNativeEnter();
    _cxuiCanvasPointer(p, kind, button, mouseButtons(wp), mouseMods(wp), px);
    _cxuiNativeLeave();
    return true;
}

// Down and up as the message loop would deliver them to the canvas
bool _cxuiWin32TestNativeClick(CxUiPeer* p, CxUiPoint px)
{
    HWND hwnd = _cxuiWin32CanvasHwnd(p);
    if (!hwnd)
        return false;
    LPARAM at = MAKELPARAM(px.x, px.y);
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, at);
    SendMessageW(hwnd, WM_LBUTTONUP, 0, at);
    return true;
}

// Sent to the canvas as Windows sends it, with the pointer on the screen
bool _cxuiWin32TestNativeWheel(CxUiPeer* p, CxUiPoint px, int32 notches)
{
    HWND hwnd = _cxuiWin32CanvasHwnd(p);
    if (!hwnd)
        return false;
    POINT pt = { px.x, px.y };
    ClientToScreen(hwnd, &pt);
    SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, -notches * WHEEL_DELTA), MAKELPARAM(pt.x, pt.y));
    return true;
}

// Windows sends the wheel to the focused window; it goes to the canvas under the pointer instead.
_Use_decl_annotations_
bool _cxuiWin32Wheel(UINT msg, WPARAM wp, LPARAM lp)
{
    POINT pt      = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    CxUiPeer* p   = _cxuiWin32CanvasAt(pt);
    if (!p)
        return false;
    HWND hwnd = _cxuiWin32CanvasHwnd(p);
    ScreenToClient(hwnd, &pt);
    float64 notches = GET_WHEEL_DELTA_WPARAM(wp) / (float64)WHEEL_DELTA;
    _cxuiNativeEnter();
    // Windows counts the vertical wheel turned away from the user as positive.
    CxUiResult r = msg == WM_MOUSEHWHEEL
                       ? _cxuiCanvasWheel(p, notches, 0, mouseMods(GET_KEYSTATE_WPARAM(wp)), (CxUiPoint){ pt.x, pt.y })
                       : _cxuiCanvasWheel(p, 0, -notches, mouseMods(GET_KEYSTATE_WPARAM(wp)), (CxUiPoint){ pt.x, pt.y });
    _cxuiNativeLeave();
    return r == CXUI_Handled;
}

void _cxuiWin32DoubleClick(int64* time, int32* distance)
{
    *time     = timeMS(GetDoubleClickTime());
    *distance = max(GetSystemMetrics(SM_CXDOUBLECLK), GetSystemMetrics(SM_CYDOUBLECLK)) / 2;
}
