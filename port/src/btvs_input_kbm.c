/* Controls: keyboard and mouse. Keys are read with GetAsyncKeyState, so no
 * window messages are needed; the caller (btvs_input.c) only asks while the
 * game window has focus. */
#include <windows.h>
#include <string.h>
#include "btvs_input_priv.h"

static const struct { const char *name; int vk; } k_keys[] = {
    { "Space", VK_SPACE }, { "Enter", VK_RETURN }, { "Escape", VK_ESCAPE }, { "Backspace", VK_BACK }, { "Tab", VK_TAB },
    { "LShift", VK_LSHIFT }, { "RShift", VK_RSHIFT }, { "LCtrl", VK_LCONTROL }, { "RCtrl", VK_RCONTROL },
    { "LAlt", VK_LMENU }, { "RAlt", VK_RMENU }, { "Up", VK_UP }, { "Down", VK_DOWN }, { "Left", VK_LEFT }, { "Right", VK_RIGHT },
    { "Insert", VK_INSERT }, { "Delete", VK_DELETE }, { "Home", VK_HOME }, { "End", VK_END }, { "PageUp", VK_PRIOR },
    { "PageDown", VK_NEXT }, { "CapsLock", VK_CAPITAL },
    { "MouseLeft", VK_LBUTTON }, { "MouseRight", VK_RBUTTON }, { "MouseMiddle", VK_MBUTTON }, { "Mouse4", VK_XBUTTON1 },
    { "Mouse5", VK_XBUTTON2 }, { "WheelUp", IN_KEY_WHEEL_UP }, { "WheelDown", IN_KEY_WHEEL_DOWN },
    { "Num0", VK_NUMPAD0 }, { "Num1", VK_NUMPAD1 }, { "Num2", VK_NUMPAD2 }, { "Num3", VK_NUMPAD3 }, { "Num4", VK_NUMPAD4 },
    { "Num5", VK_NUMPAD5 }, { "Num6", VK_NUMPAD6 }, { "Num7", VK_NUMPAD7 }, { "Num8", VK_NUMPAD8 }, { "Num9", VK_NUMPAD9 },
    { "Num*", VK_MULTIPLY }, { "Num+", VK_ADD }, { "Num-", VK_SUBTRACT }, { "Num.", VK_DECIMAL }, { "Num/", VK_DIVIDE },
    { "F1", VK_F1 }, { "F2", VK_F2 }, { "F3", VK_F3 }, { "F4", VK_F4 }, { "F5", VK_F5 }, { "F6", VK_F6 }, { "F7", VK_F7 },
    { "F8", VK_F8 }, { "F9", VK_F9 }, { "F10", VK_F10 }, { "F11", VK_F11 }, { "F12", VK_F12 },
    { ";", VK_OEM_1 }, { "=", VK_OEM_PLUS }, { "Comma", VK_OEM_COMMA }, { "-", VK_OEM_MINUS }, { ".", VK_OEM_PERIOD },
    { "/", VK_OEM_2 }, { "`", VK_OEM_3 }, { "[", VK_OEM_4 }, { "\\", VK_OEM_5 }, { "]", VK_OEM_6 }, { "'", VK_OEM_7 },
};
#define NKEYS ((int)(sizeof k_keys / sizeof k_keys[0]))

int in_key_code(const char *n)
{
    int i;
    if (!n || !n[0])
        return 0;
    if (!n[1] && ((n[0] >= 'A' && n[0] <= 'Z') || (n[0] >= '0' && n[0] <= '9')))
        return n[0];
    if (!n[1] && n[0] >= 'a' && n[0] <= 'z')
        return n[0] - 32;
    for (i = 0; i < NKEYS; i++)
        if (!_stricmp(n, k_keys[i].name))
            return k_keys[i].vk;
    return 0;
}

const char *in_key_name(int code)
{
    static char one[36][2];
    int i;
    if (code >= 'A' && code <= 'Z') { one[code - 'A'][0] = (char)code; return one[code - 'A']; }
    if (code >= '0' && code <= '9') { one[26 + code - '0'][0] = (char)code; return one[26 + code - '0']; }
    for (i = 0; i < NKEYS; i++)
        if (k_keys[i].vk == code)
            return k_keys[i].name;
    return "";
}

int in_key_is_mouse(int c)
{
    return c == VK_LBUTTON || c == VK_RBUTTON || c == VK_MBUTTON || c == VK_XBUTTON1 || c == VK_XBUTTON2;
}

int in_key_down(int code)
{
    if (code <= 0 || code >= 0x1000)
        return 0;
    return (GetAsyncKeyState(code) & 0x8000) != 0;
}

int in_key_any_down(void)
{
    int i, c;
    for (c = 'A'; c <= 'Z'; c++)
        if (in_key_down(c)) return c;
    for (c = '0'; c <= '9'; c++)
        if (in_key_down(c)) return c;
    for (i = 0; i < NKEYS; i++)
        if (in_key_down(k_keys[i].vk)) return k_keys[i].vk;
    return 0;
}

/* ── the mouse ── */

static int   s_captured;
static POINT s_centre;

void in_mouse_capture(int on)
{
    if (on && !s_captured) {
        HWND w = GetForegroundWindow();
        RECT r;
        POINT tl = { 0, 0 };
        if (!w || !GetClientRect(w, &r) || !ClientToScreen(w, &tl))
            return;
        OffsetRect(&r, tl.x, tl.y);
        ClipCursor(&r);
        s_centre.x = (r.left + r.right) / 2;
        s_centre.y = (r.top + r.bottom) / 2;
        SetCursorPos(s_centre.x, s_centre.y);
        s_captured = 1;
    } else if (!on && s_captured) {
        ClipCursor(NULL);
        s_captured = 0;
    }
}

int in_mouse_is_captured(void)
{
    return s_captured;
}

void in_mouse_delta(int *dx, int *dy)
{
    POINT p;
    *dx = *dy = 0;
    if (!s_captured || !GetCursorPos(&p))
        return;
    *dx = p.x - s_centre.x;
    *dy = p.y - s_centre.y;
    if (*dx || *dy)
        SetCursorPos(s_centre.x, s_centre.y);
}
