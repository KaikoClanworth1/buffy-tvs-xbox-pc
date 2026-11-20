/**
 * Controls: the game's XAPI input functions, answered by the PC.
 *
 * On the console these sit on XAPI's USB stack (XID / OHCI). Here each is
 * replaced whole -- named in manual_functions.json, so the recompiler leaves
 * its body out and every call lands in this file -- and answers from
 * btvs_input_poll(). No USB or controller hardware is emulated.
 *
 * The game's side is i_xinput.obj: TIF_Input_Implement::Init asks XGetDevices
 * once, HandleDeviceChanges asks XGetDeviceChanges every update and opens or
 * closes a TInputDeviceController per port, and TInputDeviceController::Poll
 * calls XInputGetState from a 16 ms timer. It ignores XInputGetState's result
 * and reads the state it passed in, so that is always filled.
 *
 * All are stdcall: [esp] is the return address, the arguments follow, and the
 * callee pops both. Addresses are this disc's (XDK 4432), from final.map.
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "recomp_types.h"
#include "btvs_input_priv.h"

#define GAMEPAD_TYPE_TABLE  0x0060F7F4u     /* _XDEVICE_TYPE_GAMEPAD_TABLE */
#define PAD_HANDLE          0x0BAD0001u     /* + port; opaque to the game */

static void ret_stdcall(uint32_t result, uint32_t arg_bytes)
{
    g_eax = result;
    g_esp += 4 + arg_bytes;
}

static int handle_port(uint32_t h)
{
    return (h >= PAD_HANDLE && h < PAD_HANDLE + BTVS_PORTS) ? (int)(h - PAD_HANDLE) : -1;
}

/* What the game was last told is plugged in, to work out changes from. */
static uint32_t s_reported;

/* VOID XInitDevices(DWORD count, PXDEVICE_PREALLOC_TYPE types) */
void XInitDevices_006106B6(void)
{
    IN_LOG("XInitDevices");
    ret_stdcall(0, 8);
}

/* DWORD XGetDevices(PXPP_DEVICE_TYPE type): the ports that have this device. */
void XGetDevices_006106BB(void)
{
    uint32_t type = MEM32(g_esp + 4), mask = 0;
    if (type == GAMEPAD_TYPE_TABLE) {
        mask = btvs_input_connected();
        s_reported = mask;
    }
    ret_stdcall(mask, 4);                       /* memory units, keyboards, remotes: none */
}

/* BOOL XGetDeviceChanges(PXPP_DEVICE_TYPE type, PDWORD insertions, PDWORD removals) */
void XGetDeviceChanges_006106DD(void)
{
    uint32_t type = MEM32(g_esp + 4), ins = MEM32(g_esp + 8), rem = MEM32(g_esp + 12);
    uint32_t in_mask = 0, out_mask = 0;
    if (type == GAMEPAD_TYPE_TABLE) {
        uint32_t now = btvs_input_connected();
        in_mask = now & ~s_reported;
        out_mask = s_reported & ~now;
        s_reported = now;
        if (in_mask || out_mask)
            IN_LOG("pads now %X (in %X, out %X)", now, in_mask, out_mask);
    }
    if (ins) MEM32(ins) = in_mask;
    if (rem) MEM32(rem) = out_mask;
    ret_stdcall((in_mask || out_mask) ? 1 : 0, 12);
}

/* HANDLE XInputOpen(PXPP_DEVICE_TYPE type, DWORD port, DWORD slot, PXINPUT_POLLING_PARAMETERS) */
void XInputOpen_0061058D(void)
{
    uint32_t type = MEM32(g_esp + 4), port = MEM32(g_esp + 8);
    uint32_t h = (type == GAMEPAD_TYPE_TABLE && port < BTVS_PORTS && (btvs_input_connected() >> port & 1))
                     ? PAD_HANDLE + port : 0;
    IN_LOG("XInputOpen port %u -> %s", port + 1, h ? "open" : "no pad");
    ret_stdcall(h, 16);
}

/* VOID XInputClose(HANDLE) */
void XInputClose_006105E3(void)
{
    int port = handle_port(MEM32(g_esp + 4));
    if (port >= 0)
        btvs_input_rumble(port, 0, 0);
    ret_stdcall(0, 4);
}

/* DWORD XInputGetState(HANDLE, PXINPUT_STATE): DWORD packet number, then the 18-byte pad. */
void XInputGetState_006105EF(void)
{
    static uint32_t packet[BTVS_PORTS];
    static btvs_pad last[BTVS_PORTS];
    int port = handle_port(MEM32(g_esp + 4));
    uint32_t out = MEM32(g_esp + 8);
    btvs_pad g;
    int have = 0;

    memset(&g, 0, sizeof g);
    if (port >= 0)
        have = btvs_input_poll(port, &g);
    if (out) {
        if (port >= 0 && memcmp(&g, &last[port], sizeof g)) {
            packet[port]++;
            last[port] = g;
        }
        MEM32(out) = port >= 0 ? packet[port] : 0;
        memcpy((void *)XBOX_PTR(out + 4), &g, sizeof g);
    }
    ret_stdcall(have ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED, 8);
}

/* DWORD XInputSetState(HANDLE, PXINPUT_FEEDBACK).
 * XINPUT_FEEDBACK: header { DWORD status; HANDLE event; BYTE reserved[58] },
 * then XINPUT_RUMBLE { WORD left, right } at +66. The console sends it to the
 * pad in the background and returns ERROR_IO_PENDING; the game
 * (TIF_Input_Implement::SetMotorSpeed) counts exactly that as success and will
 * not send again while the header still says pending. Here it is done at once:
 * the header reads success, the result says pending. */
void XInputSetState_0061065E(void)
{
    int port = handle_port(MEM32(g_esp + 4));
    uint32_t fb = MEM32(g_esp + 8);
    if (fb) {
        if (port >= 0)
            btvs_input_rumble(port, MEM16(fb + 66), MEM16(fb + 68));
        MEM32(fb) = ERROR_SUCCESS;
    }
    ret_stdcall(port >= 0 && fb ? ERROR_IO_PENDING : ERROR_DEVICE_NOT_CONNECTED, 8);
}

/* DWORD XMountMUA(DWORD port, DWORD slot, PCHAR drive): there are no memory
 * units; saves go to the hard disk (the game's own "Xbox hard disk" path). */
void XMountMUA_00610BFF(void)
{
    ret_stdcall(ERROR_DEVICE_NOT_CONNECTED, 12);
}

/* DWORD XUnmountMU(DWORD port, DWORD slot) */
void XUnmountMU_00610D31(void)
{
    ret_stdcall(ERROR_DEVICE_NOT_CONNECTED, 8);
}
