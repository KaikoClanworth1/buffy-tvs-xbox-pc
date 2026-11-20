/* Controls: the XInput backend (Xbox 360 / One / Series pads and anything that
 * speaks XInput). The DLL is loaded at run time so a PC without it still runs. */
#include <windows.h>
#include <string.h>
#include "btvs_input_priv.h"

typedef struct { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XI_GAMEPAD;
typedef struct { DWORD dwPacketNumber; XI_GAMEPAD Gamepad; } XI_STATE;
typedef struct { WORD wLeftMotorSpeed, wRightMotorSpeed; } XI_VIBRATION;

static DWORD (WINAPI *s_get)(DWORD, XI_STATE *);
static DWORD (WINAPI *s_set)(DWORD, XI_VIBRATION *);
static int s_loaded;            /* 0 not tried, 1 ok, -1 missing */

static int xi_load(void)
{
    static const char *const dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    int i;
    if (s_loaded)
        return s_loaded > 0;
    s_loaded = -1;
    for (i = 0; i < 3; i++) {
        HMODULE h = LoadLibraryA(dlls[i]);
        if (!h)
            continue;
        /* ordinal 100 is XInputGetStateEx: it also reports the Guide button */
        s_get = (DWORD (WINAPI *)(DWORD, XI_STATE *))GetProcAddress(h, i < 2 ? (LPCSTR)100 : "XInputGetState");
        if (!s_get)
            s_get = (DWORD (WINAPI *)(DWORD, XI_STATE *))GetProcAddress(h, "XInputGetState");
        s_set = (DWORD (WINAPI *)(DWORD, XI_VIBRATION *))GetProcAddress(h, "XInputSetState");
        if (s_get) {
            s_loaded = 1;
            IN_LOG("XInput: %s", dlls[i]);
            return 1;
        }
    }
    IN_LOG("XInput: not available");
    return 0;
}

int in_xi_read(int slot, in_host_pad *out)
{
    /* Asking an empty slot is slow (it enumerates devices), so an empty slot
     * is asked again only once a second. */
    static DWORD retry[4];
    XI_STATE st;
    WORD b;
    memset(out, 0, sizeof *out);
    if (slot < 0 || slot > 3 || !xi_load())
        return 0;
    if (retry[slot] && GetTickCount() - retry[slot] < 1000)
        return 0;
    memset(&st, 0, sizeof st);
    if (s_get((DWORD)slot, &st) != ERROR_SUCCESS) {
        retry[slot] = GetTickCount() | 1;
        return 0;
    }
    retry[slot] = 0;
    b = st.Gamepad.wButtons;
    if (b & 0x0001) out->buttons |= 1u << HB_UP;
    if (b & 0x0002) out->buttons |= 1u << HB_DOWN;
    if (b & 0x0004) out->buttons |= 1u << HB_LEFT;
    if (b & 0x0008) out->buttons |= 1u << HB_RIGHT;
    if (b & 0x0010) out->buttons |= 1u << HB_START;
    if (b & 0x0020) out->buttons |= 1u << HB_BACK;
    if (b & 0x0040) out->buttons |= 1u << HB_L3;
    if (b & 0x0080) out->buttons |= 1u << HB_R3;
    if (b & 0x0100) out->buttons |= 1u << HB_LB;
    if (b & 0x0200) out->buttons |= 1u << HB_RB;
    if (b & 0x0400) out->buttons |= 1u << HB_GUIDE;
    if (b & 0x1000) out->buttons |= 1u << HB_A;
    if (b & 0x2000) out->buttons |= 1u << HB_B;
    if (b & 0x4000) out->buttons |= 1u << HB_X;
    if (b & 0x8000) out->buttons |= 1u << HB_Y;
    out->lx = st.Gamepad.sThumbLX; out->ly = st.Gamepad.sThumbLY;
    out->rx = st.Gamepad.sThumbRX; out->ry = st.Gamepad.sThumbRY;
    out->lt = st.Gamepad.bLeftTrigger; out->rt = st.Gamepad.bRightTrigger;
    return 1;
}

void in_xi_rumble(int slot, uint16_t left, uint16_t right)
{
    XI_VIBRATION v;
    if (slot < 0 || slot > 3 || !xi_load() || !s_set)
        return;
    v.wLeftMotorSpeed = left;
    v.wRightMotorSpeed = right;
    s_set((DWORD)slot, &v);
}
