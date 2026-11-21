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
#include <math.h>
#include <stdlib.h>
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
    btvs_input__glyphs_boot();
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

static int guest_readable(uint32_t va)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (va < 0x10000u || !VirtualQuery((void *)XBOX_PTR(va), &mbi, sizeof mbi))
        return 0;
    return mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
}

/* BTVS_INPUT_VERIFY=1: check what the game made of the pads it was given.
 *
 * TInputDeviceController::Poll (the only caller of XInputGetState; `this` is in
 * edi at the call, the handle at this+0x3C20) turns each read into a sample of
 * 22 floats:
 *   +0 LX +4 LY +0xC RX +0x10 RY (dead zone 0x618C78 taken out, scaled by
 *   0x618C74, RX/RY negated), +0x18.. A B X Y Black White LT RT (value *
 *   0x617270 when above 30), +0x38 Start Back L3 R3, +0x48 Up Down Left Right.
 * Straight after the read TInputDevice::Process (0x24F60) copies the sample to
 * the input system's table, which is where the game takes its input from:
 *   [[this+4] + 0xC4 + [this+8]*12 + 8] + byte[this+0xC] * 0x100
 * (the first sample of the update, for this kind of device and this port). A
 * helper thread, started only in this mode, looks there 0.3 ms after each read
 * and compares. It can look at a bad moment (the next read begun), so a
 * difference only counts when it is seen after three reads running. One log
 * line per pad value:
 *   [INPUT] verify p1 ok: <the pad> -> <what the game stored>
 * and "verify ... MISMATCH" when the game stored something else. */
static float guest_float(uint32_t va)
{
    float f;
    memcpy(&f, (void *)XBOX_PTR(va), 4);
    return f;
}

static void verify_expect(const btvs_pad *g, float e[22])
{
    float dz = guest_float(0x00618C78u), k = guest_float(0x00618C74u), ak = guest_float(0x00617270u);
    int16_t s[4];
    int i, st;
    memset(e, 0, 22 * sizeof(float));
    s[0] = g->lx; s[1] = g->ly; s[2] = g->rx; s[3] = g->ry;
    for (i = 0; i < 4; i++)
        if (s[i] == -32768)
            s[i] = -32767;
    s[2] = (int16_t)-s[2]; s[3] = (int16_t)-s[3];
    for (st = 0; st < 2; st++) {
        float x = s[st * 2], y = s[st * 2 + 1], m = sqrtf(x * x + y * y);
        if (m > dz) {
            float sc = (m - dz) / m * k, fx = x * sc, fy = y * sc;
            e[st * 3]     = fx < -1.0f ? -1.0f : fx > 1.0f ? 1.0f : fx;
            e[st * 3 + 1] = fy < -1.0f ? -1.0f : fy > 1.0f ? 1.0f : fy;
        }
    }
    for (i = 0; i < 8; i++)
        if (g->analog[i] > 30)
            e[6 + i] = g->analog[i] * ak;
    if (g->buttons & BTVS_BTN_START)  e[14] = 1.0f;
    if (g->buttons & BTVS_BTN_BACK)   e[15] = 1.0f;
    if (g->buttons & BTVS_BTN_LSTICK) e[16] = 1.0f;
    if (g->buttons & BTVS_BTN_RSTICK) e[17] = 1.0f;
    if (g->buttons & BTVS_BTN_UP)     e[18] = 1.0f; else if (g->buttons & BTVS_BTN_DOWN)  e[19] = 1.0f;
    if (g->buttons & BTVS_BTN_LEFT)   e[20] = 1.0f; else if (g->buttons & BTVS_BTN_RIGHT) e[21] = 1.0f;
}

static struct { uint32_t self; btvs_pad pad; int checked, bad, busy; } s_ver[BTVS_PORTS];

static void verify_game(int port)
{
    const btvs_pad *prev = &s_ver[port].pad;
    int *checked = &s_ver[port].checked;
    uint32_t handle = PAD_HANDLE + (uint32_t)port;
    static const char *const names[22] = { "LX", "LY", "?2", "RX", "RY", "?5", "A", "B", "X", "Y", "Black", "White", "LT", "RT",
                                           "Start", "Back", "L3", "R3", "Up", "Down", "Left", "Right" };
    uint32_t self = s_ver[port].self, owner, table, s;
    float e[22], got[22];
    char text[400];
    int i, n = 0, bad = 0;

    if (*checked || s_ver[port].busy)
        return;
    if (!guest_readable(self) || !guest_readable(self + 0x3C20u) || MEM32(self + 0x3C20u) != handle)
        return;
    owner = MEM32(self + 4u);
    table = owner + 0xC4u + MEM32(self + 8u) * 12u + 8u;
    if (!guest_readable(owner) || MEM32(self + 8u) > 2u || !guest_readable(table))
        return;
    s = MEM32(table) + (uint32_t)*(uint8_t *)XBOX_PTR(self + 0xCu) * 0x100u;
    if (!guest_readable(s) || !guest_readable(s + 0x58u))
        return;
    verify_expect(prev, e);
    text[0] = 0;
    for (i = 0; i < 22; i++) {
        got[i] = guest_float(s + 4u * (uint32_t)i);
        if (fabsf(got[i] - e[i]) > 0.002f)
            bad = 1;
        if ((got[i] != 0.0f || e[i] != 0.0f) && n < (int)sizeof text - 40)
            n += sprintf_s(text + n, sizeof text - (size_t)n, " %s=%.3f", names[i], got[i]);
    }
    if (bad && ++s_ver[port].bad < 3)
        return;                                 /* perhaps half written: look again */
    *checked = 1;
    IN_LOG("verify p%d %s: btn %04X an %u %u %u %u %u %u %u %u L %d %d R %d %d ->%s", port + 1, bad ? "MISMATCH" : "ok",
           prev->buttons, prev->analog[0], prev->analog[1], prev->analog[2], prev->analog[3], prev->analog[4],
           prev->analog[5], prev->analog[6], prev->analog[7], prev->lx, prev->ly, prev->rx, prev->ry, n ? text : " (rest)");
}

static HANDLE s_ver_event;
static SRWLOCK s_ver_lock = SRWLOCK_INIT;   /* held while the helper thread reads the game's memory */

static DWORD WINAPI verify_thread(LPVOID arg)
{
    (void)arg;
    for (;;) {
        int p;
        LARGE_INTEGER f, t0, t;
        WaitForSingleObject(s_ver_event, INFINITE);
        QueryPerformanceFrequency(&f);          /* Sleep is far too coarse: spin for 0.3 ms */
        QueryPerformanceCounter(&t0);
        do {
            YieldProcessor();
            QueryPerformanceCounter(&t);
        } while ((t.QuadPart - t0.QuadPart) * 10000 < f.QuadPart * 3);
        AcquireSRWLockExclusive(&s_ver_lock);
        for (p = 0; p < BTVS_PORTS; p++)
            if (s_ver[p].self)
                verify_game(p);
        ReleaseSRWLockExclusive(&s_ver_lock);
    }
}

static int verify_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("BTVS_INPUT_VERIFY");
        on = v && *v && *v != '0';
        if (on) {
            HANDLE t;
            s_ver_event = CreateEventA(NULL, FALSE, FALSE, NULL);
            t = CreateThread(NULL, 0, verify_thread, NULL, 0, NULL);
            if (t) CloseHandle(t); else on = 0;
        }
    }
    return on;
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

/* Where the game is.
 * Ten times a second, from player 1's pad read: which menu is up and whether a
 * level is running, read from the game's own globals. It names moments for pad
 * scripts ("title", "menu", "pause", "menu<id>", "level") and keeps the mouse
 * free outside play. Addresses are from the Logs area's reading of final.map
 * (the menu id is confirmed in the running game, the player object not yet: no
 * level loads). Every change is logged, so a run shows whether they are
 * right. BTVS_INPUT_NO_PROBE=1 turns it off. */
#define G_MENU_ID   0x007F0408u     /* GUI s_menuID: 1 main, 9 pause, 10 title, 15 resurrect (docs/menu.md) */
#define G_GAME      0x007D0090u     /* gGame (TGame *) */
#define GAME_PLAYER 0xF0u           /* TGO::GetPlayer() = [gGame + 0xF0] */
#define G_PAUSE     0x007F08F0u     /* IPauseScreen::Get() */
#define PAUSE_OPEN  0x274u          /* byte: the screen is open (IPauseScreen::SetOpen's first argument) */

static struct { DWORD last; uint32_t was_menu, was_player; int was_paused; } s_probe = { 0, 0xFFFFFFFFu, 0, 0 };

static void game_probe(void)
{
    static int off = -1;
#define last       s_probe.last
#define was_menu   s_probe.was_menu
#define was_player s_probe.was_player
#define was_paused s_probe.was_paused
    uint32_t menu = 0, game = 0, player = 0, pause = 0;
    int paused = 0;
    DWORD now = GetTickCount();
    char name[24];

    if (off < 0) {
        const char *e = getenv("BTVS_INPUT_NO_PROBE");
        off = e && *e && *e != '0';
    }
    if (off || now - last < 100)
        return;
    last = now;
    if (guest_readable(G_MENU_ID))
        menu = MEM32(G_MENU_ID);
    if (guest_readable(G_GAME) && (game = MEM32(G_GAME)) != 0 && guest_readable(game + GAME_PLAYER))
        player = MEM32(game + GAME_PLAYER);
    /* The menu id is the last menu asked for and stays 9 after the pause menu
     * closes, so "paused" is read from the pause screen itself. */
    if (guest_readable(G_PAUSE) && (pause = MEM32(G_PAUSE)) != 0 && guest_readable(pause + PAUSE_OPEN))
        paused = *(const uint8_t *)XBOX_PTR(pause + PAUSE_OPEN) != 0;
    if (menu != was_menu) {
        IN_LOG("game: menu id %u", menu);
        if (menu && menu < 64) {
            sprintf_s(name, sizeof name, "menu%u", menu);
            btvs_input_script_mark(name);
            if (menu == 1)  btvs_input_script_mark("menu");
            if (menu == 10) btvs_input_script_mark("title");
        }
        was_menu = menu;
    }
    if (!player != !was_player) {
        IN_LOG("game: %s", player ? "level running (player exists)" : "no level");
        if (player)
            btvs_input_script_mark("level");
        was_player = player;
    }
    if (paused != was_paused) {
        IN_LOG("game: pause screen %s", paused ? "open" : "closed");
        btvs_input_script_mark(paused ? "pause" : "resume");
        was_paused = paused;
    }
    btvs_input_allow_mouse_look(player != 0 && !paused);
    btvs_input_menu_mode(player != 0 && !paused ? 0 : !player && menu == 10 ? 2 : 1);
#undef last
#undef was_menu
#undef was_player
#undef was_paused
}

/* BTVS_INPUT_POS_LOG=<n>: every n reads of player 1's pad, where the player
 * is and its health, with the read count. Two runs given the same input
 * (a recording and its playback) can then be compared read for read. */
static uint32_t s_pos_reads;

static void pos_log(void)
{
    static int every = -1;
#define reads s_pos_reads
    uint32_t game, player;
    if (every < 0) {
        const char *e = getenv("BTVS_INPUT_POS_LOG");
        every = e ? atoi(e) : 0;
    }
    reads++;
    if (every <= 0 || reads % (uint32_t)every)
        return;
    if (!guest_readable(G_GAME) || (game = MEM32(G_GAME)) == 0 || !guest_readable(game + GAME_PLAYER)
        || (player = MEM32(game + GAME_PLAYER)) == 0 || !guest_readable(player + 0x248u))
        return;
    IN_LOG("pos r%u x %.2f y %.2f z %.2f health %.0f", reads, guest_float(player + 0x240u), guest_float(player + 0x244u),
           guest_float(player + 0x248u), guest_float(player + 0x90u));
#undef reads
}

/* DWORD XInputGetState(HANDLE, PXINPUT_STATE): DWORD packet number, then the 18-byte pad. */
static uint32_t s_packet[BTVS_PORTS];
static btvs_pad s_given[BTVS_PORTS];

void XInputGetState_006105EF(void)
{
#define packet s_packet
#define last   s_given
    int port = handle_port(MEM32(g_esp + 4));
    uint32_t out = MEM32(g_esp + 8);
    btvs_pad g;
    int have = 0;

    memset(&g, 0, sizeof g);
    if (port >= 0 && verify_on()) {
        s_ver[port].busy = 1;
        s_ver[port].self = g_edi;               /* TInputDeviceController::Poll's this */
    }
    if (port == 0) {
        game_probe();
        pos_log();
        btvs_input__glyphs_poll();
    }
    if (port >= 0)
        have = btvs_input_poll(port, &g);
    if (out) {
        if (port >= 0 && memcmp(&g, &last[port], sizeof g)) {
            packet[port]++;
            last[port] = g;
            s_ver[port].pad = g;
            s_ver[port].checked = s_ver[port].bad = 0;
        }
        MEM32(out) = port >= 0 ? packet[port] : 0;
        memcpy((void *)XBOX_PTR(out + 4), &g, sizeof g);
    }
    if (port >= 0 && verify_on()) {
        s_ver[port].busy = 0;
        SetEvent(s_ver_event);
    }
    ret_stdcall(have ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED, 8);
#undef packet
#undef last
}

/* DWORD XInputSetState(HANDLE, PXINPUT_FEEDBACK).
 * XINPUT_FEEDBACK: header { DWORD status; HANDLE event; BYTE reserved[58] },
 * then XINPUT_RUMBLE { WORD left, right } at +66. The console sends it to the
 * pad in the background and returns ERROR_IO_PENDING; the game
 * (TIF_Input_Implement::SetMotorSpeed) counts exactly that as success and will
 * not send again while the header still says pending. Here it is done at once:
 * the header reads success, the result says pending. */
static uint32_t s_rumble_was[BTVS_PORTS] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };

void XInputSetState_0061065E(void)
{
#define was s_rumble_was
    int port = handle_port(MEM32(g_esp + 4));
    uint32_t fb = MEM32(g_esp + 8);
    if (fb) {
        if (port >= 0) {
            uint32_t v = (uint32_t)MEM16(fb + 66) << 16 | MEM16(fb + 68);
            if (v != was[port]) {
                IN_LOG("rumble p%d left %u right %u", port + 1, v >> 16, v & 0xFFFFu);
                was[port] = v;
            }
            btvs_input_rumble(port, MEM16(fb + 66), MEM16(fb + 68));
        }
        MEM32(fb) = ERROR_SUCCESS;
    }
    ret_stdcall(port >= 0 && fb ? ERROR_IO_PENDING : ERROR_DEVICE_NOT_CONNECTED, 8);
#undef was
}

/* The game restarts itself inside the process (btvs_boot_restart.c, a level
 * load): called on the game thread when nothing of the game runs any more and
 * its memory is about to be wiped and put back. Everything here that was
 * about that run of the game is forgotten, as a new process forgot it: what
 * the game was told is plugged in, the packet numbers, where the game was
 * (so "menu", "level" ... are named again for the next run's pad script), the
 * tips' sentences, the run's script, recording and playback. The helper
 * thread of BTVS_INPUT_VERIFY is waited for and has nothing to look at until
 * the next run reads a pad. */
void btvs_input_reboot(void)
{
    AcquireSRWLockExclusive(&s_ver_lock);
    memset(s_ver, 0, sizeof s_ver);
    ReleaseSRWLockExclusive(&s_ver_lock);
    s_reported = 0;
    memset(s_packet, 0, sizeof s_packet);
    memset(s_given, 0, sizeof s_given);
    memset(s_rumble_was, 0xFF, sizeof s_rumble_was);
    memset(&s_probe, 0, sizeof s_probe);
    s_probe.was_menu = 0xFFFFFFFFu;
    s_pos_reads = 0;
    btvs_input__core_reboot();
    btvs_input__glyphs_reboot();
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
