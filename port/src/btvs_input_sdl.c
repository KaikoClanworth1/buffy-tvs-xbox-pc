/* Controls: the SDL backend, for the pads XInput does not know (DualSense,
 * DualShock 4, Switch Pro, Joy-Cons, generic HID pads).
 *
 * SDL3.dll is loaded at run time and only the handful of functions below are
 * used, declared here by hand: the build needs no SDL headers or import
 * library, and the game runs without the DLL (XInput and the keyboard still
 * work). The build copies SDL3.dll beside the exe (port/CMakeLists.txt).
 *
 * SDL lives on its own thread: it starts SDL, pumps it every 4 ms and keeps a
 * snapshot of every pad. The game's threads only read that snapshot.
 */
#include <windows.h>
#include <string.h>
#include "btvs_input_priv.h"

/* SDL3 ABI (SDL_gamepad.h, SDL_init.h) */
#define SDL_INIT_GAMEPAD 0x00002000u
enum { SDLB_SOUTH, SDLB_EAST, SDLB_WEST, SDLB_NORTH, SDLB_BACK, SDLB_GUIDE, SDLB_START, SDLB_LSTICK, SDLB_RSTICK,
       SDLB_LSHOULDER, SDLB_RSHOULDER, SDLB_UP, SDLB_DOWN, SDLB_LEFT, SDLB_RIGHT, SDLB_MISC1,
       SDLB_RPADDLE1, SDLB_LPADDLE1, SDLB_RPADDLE2, SDLB_LPADDLE2, SDLB_TOUCHPAD };
enum { SDLA_LX, SDLA_LY, SDLA_RX, SDLA_RY, SDLA_LT, SDLA_RT };
enum { SDLT_UNKNOWN, SDLT_STANDARD, SDLT_XBOX360, SDLT_XBOXONE };

typedef struct SDL_Gamepad SDL_Gamepad;
#define SDLCALL __cdecl
static struct {
    HMODULE dll;
    unsigned char (SDLCALL *Init)(uint32_t);            /* SDL's bool is one byte */
    void  (SDLCALL *Quit)(void);
    unsigned char (SDLCALL *SetHint)(const char *, const char *);
    const char *(SDLCALL *GetError)(void);
    void  (SDLCALL *PumpEvents)(void);
    uint32_t *(SDLCALL *GetGamepads)(int *);
    void  (SDLCALL *free)(void *);
    SDL_Gamepad *(SDLCALL *OpenGamepad)(uint32_t);
    void  (SDLCALL *CloseGamepad)(SDL_Gamepad *);
    unsigned char (SDLCALL *GamepadConnected)(SDL_Gamepad *);
    unsigned char (SDLCALL *GetGamepadButton)(SDL_Gamepad *, int);
    int16_t (SDLCALL *GetGamepadAxis)(SDL_Gamepad *, int);
    unsigned char (SDLCALL *RumbleGamepad)(SDL_Gamepad *, uint16_t, uint16_t, uint32_t);
    const char *(SDLCALL *GetGamepadName)(SDL_Gamepad *);
    int   (SDLCALL *GetGamepadTypeForID)(uint32_t);
} S;

static CRITICAL_SECTION s_lock;
static HANDLE       s_thread;
static volatile LONG s_quit;
static int          s_include_xbox;
static struct { uint32_t id; SDL_Gamepad *gp; in_host_pad pad; char name[64];
                uint16_t want_l, want_r, sent_l, sent_r; DWORD sent_at; } s_dev[IN_SDL_MAX];

static int sdl_bind(void)
{
    char path[MAX_PATH], *slash;
#define F(field, name) if (!(*(FARPROC *)&S.field = GetProcAddress(S.dll, name))) { IN_LOG("SDL: %s missing", name); return 0; }
    GetModuleFileNameA(NULL, path, MAX_PATH);
    slash = strrchr(path, '\\');
    if (slash) {
        strcpy_s(slash + 1, MAX_PATH - (size_t)(slash + 1 - path), "SDL3.dll");
        S.dll = LoadLibraryA(path);
    }
    if (!S.dll)
        S.dll = LoadLibraryA("SDL3.dll");
    if (!S.dll) {
        IN_LOG("SDL: SDL3.dll not found; DualSense / Switch pads need it (XInput pads and the keyboard work without)");
        return 0;
    }
    F(Init, "SDL_Init") F(Quit, "SDL_Quit") F(SetHint, "SDL_SetHint") F(GetError, "SDL_GetError")
    F(PumpEvents, "SDL_PumpEvents") F(GetGamepads, "SDL_GetGamepads") F(free, "SDL_free")
    F(OpenGamepad, "SDL_OpenGamepad") F(CloseGamepad, "SDL_CloseGamepad")
    F(GamepadConnected, "SDL_GamepadConnected") F(GetGamepadButton, "SDL_GetGamepadButton")
    F(GetGamepadAxis, "SDL_GetGamepadAxis") F(RumbleGamepad, "SDL_RumbleGamepad")
    F(GetGamepadName, "SDL_GetGamepadName") F(GetGamepadTypeForID, "SDL_GetGamepadTypeForID")
#undef F
    return 1;
}

static int16_t flip(int16_t v)       /* SDL's Y is down-positive */
{
    return v == -32768 ? 32767 : (int16_t)-v;
}

static void read_pad(SDL_Gamepad *g, in_host_pad *p)
{
    static const struct { int sdl, hb; } map[] = {
        { SDLB_SOUTH, HB_A }, { SDLB_EAST, HB_B }, { SDLB_WEST, HB_X }, { SDLB_NORTH, HB_Y },
        { SDLB_BACK, HB_BACK }, { SDLB_GUIDE, HB_GUIDE }, { SDLB_START, HB_START },
        { SDLB_LSTICK, HB_L3 }, { SDLB_RSTICK, HB_R3 }, { SDLB_LSHOULDER, HB_LB }, { SDLB_RSHOULDER, HB_RB },
        { SDLB_UP, HB_UP }, { SDLB_DOWN, HB_DOWN }, { SDLB_LEFT, HB_LEFT }, { SDLB_RIGHT, HB_RIGHT },
        { SDLB_MISC1, HB_MISC }, { SDLB_RPADDLE1, HB_PADDLE1 }, { SDLB_LPADDLE1, HB_PADDLE2 },
        { SDLB_RPADDLE2, HB_PADDLE3 }, { SDLB_LPADDLE2, HB_PADDLE4 }, { SDLB_TOUCHPAD, HB_TOUCHPAD },
    };
    int i;
    memset(p, 0, sizeof *p);
    for (i = 0; i < (int)(sizeof map / sizeof map[0]); i++)
        if (S.GetGamepadButton(g, map[i].sdl))
            p->buttons |= 1u << map[i].hb;
    p->lx = S.GetGamepadAxis(g, SDLA_LX);
    p->ly = flip(S.GetGamepadAxis(g, SDLA_LY));
    p->rx = S.GetGamepadAxis(g, SDLA_RX);
    p->ry = flip(S.GetGamepadAxis(g, SDLA_RY));
    p->lt = (uint8_t)(S.GetGamepadAxis(g, SDLA_LT) >> 7);      /* 0..32767 -> 0..255 */
    p->rt = (uint8_t)(S.GetGamepadAxis(g, SDLA_RT) >> 7);
}

static void scan(void)
{
    int n = 0, i, k;
    uint32_t *ids = S.GetGamepads(&n);
    /* gone */
    for (k = 0; k < IN_SDL_MAX; k++) {
        if (s_dev[k].id && !S.GamepadConnected(s_dev[k].gp)) {
            SDL_Gamepad *gp = s_dev[k].gp;
            IN_LOG("SDL: \"%s\" disconnected", s_dev[k].name);
            EnterCriticalSection(&s_lock);
            memset(&s_dev[k], 0, sizeof s_dev[k]);
            LeaveCriticalSection(&s_lock);
            S.CloseGamepad(gp);
        }
    }
    /* new */
    for (i = 0; ids && i < n; i++) {
        int type, free_k = -1;
        for (k = 0; k < IN_SDL_MAX; k++) {
            if (s_dev[k].id == ids[i])
                break;
            if (!s_dev[k].id && free_k < 0)
                free_k = k;
        }
        if (k < IN_SDL_MAX || free_k < 0)
            continue;
        type = S.GetGamepadTypeForID(ids[i]);
        if (!s_include_xbox && (type == SDLT_XBOX360 || type == SDLT_XBOXONE))
            continue;                                  /* the XInput backend has it */
        {
            SDL_Gamepad *gp = S.OpenGamepad(ids[i]);
            const char *nm;
            if (!gp)
                continue;
            nm = S.GetGamepadName(gp);
            EnterCriticalSection(&s_lock);
            memset(&s_dev[free_k], 0, sizeof s_dev[free_k]);
            s_dev[free_k].gp = gp;
            strncpy_s(s_dev[free_k].name, sizeof s_dev[free_k].name, nm ? nm : "Controller", _TRUNCATE);
            s_dev[free_k].id = ids[i];
            LeaveCriticalSection(&s_lock);
            IN_LOG("SDL: \"%s\" connected (type %d)", s_dev[free_k].name, type);
        }
    }
    if (ids)
        S.free(ids);
}

static DWORD WINAPI sdl_thread(void *arg)
{
    DWORD last_scan = 0;
    int k;
    (void)arg;
    S.SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");    /* focus is decided in btvs_input.c */
    S.SetHint("SDL_JOYSTICK_THREAD", "1");
    S.SetHint("SDL_NO_SIGNAL_HANDLERS", "1");
    if (!S.Init(SDL_INIT_GAMEPAD)) {
        IN_LOG("SDL: init failed: %s", S.GetError());
        return 0;
    }
    IN_LOG("SDL: started");
    while (!s_quit) {
        DWORD now = GetTickCount();
        S.PumpEvents();
        if (!last_scan || now - last_scan >= 500) {
            last_scan = now | 1;
            scan();
        }
        for (k = 0; k < IN_SDL_MAX; k++) {
            in_host_pad p;
            uint16_t l, r;
            if (!s_dev[k].id)
                continue;
            read_pad(s_dev[k].gp, &p);
            EnterCriticalSection(&s_lock);
            s_dev[k].pad = p;
            l = s_dev[k].want_l; r = s_dev[k].want_r;
            LeaveCriticalSection(&s_lock);
            /* SDL rumble has a duration: refresh it while it is on */
            if (l != s_dev[k].sent_l || r != s_dev[k].sent_r || ((l || r) && now - s_dev[k].sent_at > 400)) {
                S.RumbleGamepad(s_dev[k].gp, l, r, (l || r) ? 1000 : 0);
                s_dev[k].sent_l = l; s_dev[k].sent_r = r; s_dev[k].sent_at = now;
            }
        }
        Sleep(4);
    }
    for (k = 0; k < IN_SDL_MAX; k++)
        if (s_dev[k].id) {
            S.RumbleGamepad(s_dev[k].gp, 0, 0, 0);
            S.CloseGamepad(s_dev[k].gp);
            s_dev[k].id = 0;
        }
    S.Quit();
    return 0;
}

int in_sdl_start(int include_xbox_pads)
{
    if (s_thread)
        return 1;
    if (!sdl_bind())
        return 0;
    s_include_xbox = include_xbox_pads;
    InitializeCriticalSection(&s_lock);
    s_quit = 0;
    s_thread = CreateThread(NULL, 0, sdl_thread, NULL, 0, NULL);
    return s_thread != NULL;
}

int in_sdl_snapshot(in_sdl_dev *out, int max)
{
    int k, n = 0;
    if (!s_thread)
        return 0;
    EnterCriticalSection(&s_lock);
    for (k = 0; k < IN_SDL_MAX && n < max; k++)
        if (s_dev[k].id) {
            out[n].id = s_dev[k].id;
            out[n].pad = s_dev[k].pad;
            memcpy(out[n].name, s_dev[k].name, sizeof out[n].name);
            n++;
        }
    LeaveCriticalSection(&s_lock);
    return n;
}

void in_sdl_rumble(uint32_t id, uint16_t left, uint16_t right)
{
    int k;
    if (!s_thread)
        return;
    EnterCriticalSection(&s_lock);
    for (k = 0; k < IN_SDL_MAX; k++)
        if (s_dev[k].id == id) {
            s_dev[k].want_l = left;
            s_dev[k].want_r = right;
        }
    LeaveCriticalSection(&s_lock);
}

void in_sdl_stop(void)
{
    if (!s_thread)
        return;
    InterlockedExchange(&s_quit, 1);
    WaitForSingleObject(s_thread, 2000);
    CloseHandle(s_thread);
    s_thread = NULL;
}
