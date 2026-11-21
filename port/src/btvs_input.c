/**
 * Controls: the pad the game sees. docs/controls.md is the manual.
 *
 * btvs_input_poll(port, &pad) is the only way a pad state reaches the game.
 * In order, it
 *   1. plays a recording, if one is playing (nothing else is looked at), else
 *   2. reads the PC controller on that port (XInput or SDL) and, for the
 *      keyboard's player, the keyboard and mouse -- only while the game window
 *      has focus, and never in a test run (BTVS_NO_WINDOW=1),
 *   3. turns those into Xbox pad inputs through the bindings table,
 *   4. lays the test script (BTVS_PAD_SCRIPT) on top -- focus not needed,
 *   5. writes the result to the recording, if one is being made.
 *
 * What the game does with a pad (i_xinput.obj, TInputDeviceController::Poll):
 * it polls every port from a 16 ms multimedia timer, not once a frame; sticks
 * get its own radial dead zone of 13107 (40 %) and are rescaled past it;
 * analog buttons count above 30. So sticks leave here already past that dead
 * zone: the PC stick's own (smaller) dead zone maps onto the game's.
 */
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include "btvs.h"
#include "btvs_input_priv.h"

#define GAME_DEADZONE 13107.0f      /* TInputDeviceController::Poll, 0x00618C78 */

/* ── lock, clock, focus ──────────────────────────────────────────────────── */

static CRITICAL_SECTION s_cs;
static INIT_ONCE s_once = INIT_ONCE_STATIC_INIT;
static void at_exit(void);

static BOOL CALLBACK init_once(PINIT_ONCE o, PVOID p, PVOID *c)
{
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&s_cs);
    atexit(at_exit);
    return TRUE;
}
#define LOCK()   do { InitOnceExecuteOnce(&s_once, init_once, NULL, NULL); EnterCriticalSection(&s_cs); } while (0)
#define UNLOCK() LeaveCriticalSection(&s_cs)

static double (*s_clock_fn)(void);
static int (*s_focus_fn)(void);

static double now_ms(void)
{
    static LARGE_INTEGER f, t0;
    LARGE_INTEGER t;
    if (s_clock_fn)
        return s_clock_fn();
    if (!f.QuadPart) {
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t0);
    }
    QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
}

static int env_on(const char *name)
{
    const char *e = getenv(name);
    return e && *e && *e != '0';
}

/* The player's devices count only while the game's window is the one the
 * player is using. A test run has no window to use: never. */
static int focused(void)
{
    static int no_window = -1;
    DWORD pid = 0;
    HWND fg;
    if (s_focus_fn)
        return s_focus_fn();
    if (no_window < 0)
        no_window = env_on("BTVS_NO_WINDOW");
    if (no_window)
        return 0;
    fg = GetForegroundWindow();
    if (!fg)
        return 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

/* ── actions and bindings ────────────────────────────────────────────────── */

enum {
    ACT_A, ACT_B, ACT_X, ACT_Y, ACT_BLACK, ACT_WHITE, ACT_LT, ACT_RT, ACT_START, ACT_BACK,
    ACT_DUP, ACT_DDOWN, ACT_DLEFT, ACT_DRIGHT,
    ACT_MOVE_UP, ACT_MOVE_DOWN, ACT_MOVE_LEFT, ACT_MOVE_RIGHT,
    ACT_LOOK_UP, ACT_LOOK_DOWN, ACT_LOOK_LEFT, ACT_LOOK_RIGHT,
    ACT_LSTICK, ACT_RSTICK, ACT_COUNT
};

/* what a controller binding can name: a button, a trigger or half a stick axis */
enum {
    PS_NONE,
    PS_BTN,                                 /* PS_BTN + HB_x */
    PS_LT = PS_BTN + HB_COUNT, PS_RT,
    PS_LS_UP, PS_LS_DOWN, PS_LS_LEFT, PS_LS_RIGHT,
    PS_RS_UP, PS_RS_DOWN, PS_RS_LEFT, PS_RS_RIGHT,
    PS_COUNT
};
static const char *const k_ps_names[PS_COUNT] = {
    "None", "A", "B", "X", "Y", "LB", "RB", "Start", "Back", "L3", "R3",
    "DpadUp", "DpadDown", "DpadLeft", "DpadRight",
    "Guide", "Misc", "Paddle1", "Paddle2", "Paddle3", "Paddle4", "Touchpad",
    "LT", "RT", "LS-Up", "LS-Down", "LS-Left", "LS-Right", "RS-Up", "RS-Down", "RS-Left", "RS-Right",
};

/* The keys are what a PC player expects of a third-person action game, for
 * what the buttons do in this one: B jumps (Space), X punches (left mouse),
 * A kicks (right mouse), Y uses (E), the right trigger targets (Shift, middle
 * mouse), the left one looks / aims (Ctrl, F), White is the inventory (Tab),
 * Black swaps ammo (R), Start pauses (Esc). No key is on two actions
 * (the self-test checks). In menus Enter / Space and Esc / Backspace are
 * A and B whatever is bound: see menu_keys. */
static const struct { const char *name, *label, *keys; int pad; } k_act[ACT_COUNT] = {
    { "A",            "A button",          "MouseRight, Enter",      PS_BTN + HB_A },
    { "B",            "B button",          "Space, Backspace",       PS_BTN + HB_B },
    { "X",            "X button",          "MouseLeft, Q",           PS_BTN + HB_X },
    { "Y",            "Y button",          "E",                      PS_BTN + HB_Y },
    { "Black",        "Black button",      "R",                      PS_BTN + HB_LB },
    { "White",        "White button",      "Tab",                    PS_BTN + HB_RB },
    { "LeftTrigger",  "Left trigger",      "LCtrl, F",               PS_LT },
    { "RightTrigger", "Right trigger",     "LShift, MouseMiddle",    PS_RT },
    { "Start",        "Start",             "Escape",                 PS_BTN + HB_START },
    { "Back",         "Back",              "B",                      PS_BTN + HB_BACK },
    { "DpadUp",       "D-pad up",          "Up",                     PS_BTN + HB_UP },
    { "DpadDown",     "D-pad down",        "Down",                   PS_BTN + HB_DOWN },
    { "DpadLeft",     "D-pad left",        "Left, WheelDown",        PS_BTN + HB_LEFT },
    { "DpadRight",    "D-pad right",       "Right, WheelUp",         PS_BTN + HB_RIGHT },
    { "MoveForward",  "Move forward",      "W",                      PS_LS_UP },
    { "MoveBack",     "Move back",         "S",                      PS_LS_DOWN },
    { "MoveLeft",     "Move left",         "A",                      PS_LS_LEFT },
    { "MoveRight",    "Move right",        "D",                      PS_LS_RIGHT },
    { "LookUp",       "Look up",           "I",                      PS_RS_UP },
    { "LookDown",     "Look down",         "K",                      PS_RS_DOWN },
    { "LookLeft",     "Look left",         "J",                      PS_RS_LEFT },
    { "LookRight",    "Look right",        "L",                      PS_RS_RIGHT },
    { "LeftStickClick",  "Left stick click",  "C",                   PS_BTN + HB_L3 },
    { "RightStickClick", "Right stick click", "V",                   PS_BTN + HB_R3 },
};

/* The keys before 21 Nov, evening (the Chaos Bleeds port's): a settings file that
 * still holds exactly these is moved to the ones above, once. */
static const char *const k_old_keys[ACT_COUNT] = {
    "Space", "Backspace, MouseMiddle", "E, MouseLeft", "Q, MouseRight", "Z", "C", "LShift", "F, RShift", "Enter", "Escape",
    "Up", "Down", "Left, WheelDown", "Right, WheelUp", "W", "S", "A", "D", "I", "K", "J", "L", "LCtrl", "V",
};
#define KEY_DEFAULTS 2                  /* [controls] key_defaults: which defaults the file was last checked against */

enum { BACKEND_AUTO, BACKEND_XINPUT, BACKEND_SDL, BACKEND_NONE };

static struct {
    volatile LONG loaded;
    int   backend, rumble, rumble_strength;
    float stick_dz;                 /* 0..0.9 of the PC stick's travel */
    int   trigger_dz;               /* 0..254 */
    int   invert_x, invert_y;
    int   mouse_look;
    int   menu_keys;
    float mouse_sens;
    int   kb_port;                  /* 0-3, -1 nobody */
    int   key[ACT_COUNT][2];
    int   pad[ACT_COUNT];
} C;

static int ps_code(const char *s)
{
    int i;
    for (i = 0; i < PS_COUNT; i++)
        if (!_stricmp(s, k_ps_names[i]))
            return i;
    return -1;
}

static char *trim(char *s)
{
    size_t l;
    while (*s == ' ' || *s == '\t')
        s++;
    l = strlen(s);
    while (l && (s[l - 1] == ' ' || s[l - 1] == '\t' || s[l - 1] == '\r' || s[l - 1] == '\n'))
        s[--l] = 0;
    return s;
}

static void parse_keys(const char *v, int *out)
{
    char buf[128], *tok, *ctx = NULL;
    int n = 0;
    out[0] = out[1] = 0;
    strncpy_s(buf, sizeof buf, v ? v : "", _TRUNCATE);
    for (tok = strtok_s(buf, ",", &ctx); tok && n < 2; tok = strtok_s(NULL, ",", &ctx))
        if ((out[n] = in_key_code(trim(tok))) != 0)
            n++;
}

static void keys_text(const int *k, char *buf, int size)
{
    buf[0] = 0;
    if (k[0]) strncat_s(buf, size, in_key_name(k[0]), _TRUNCATE);
    if (k[0] && k[1]) strncat_s(buf, size, ", ", _TRUNCATE);
    if (k[1]) strncat_s(buf, size, in_key_name(k[1]), _TRUNCATE);
    if (!buf[0]) strncpy_s(buf, size, "None", _TRUNCATE);
}

static void on_setting_changed(const char *section, const char *key, void *user)
{
    (void)section; (void)key; (void)user;
    InterlockedExchange(&C.loaded, 0);              /* re-read at the next poll */
}

/* Old default keys in the file, none changed: the new defaults are written
 * over them. Anything else (a player's own keys) is left alone and pinned as
 * it reads today. The file itself is asked, so a test run's BTVS_CONTROLS_*
 * overrides do not count as a player's change. */
static void move_old_defaults(void)
{
    char name[64], have[96];
    const char *path = btvs_settings_path();
    int a, old[2], cur[2], untouched = 1;
    if (btvs_setting_int("controls", "key_defaults", 0) >= KEY_DEFAULTS)
        return;
    for (a = 0; a < ACT_COUNT && untouched; a++) {
        sprintf_s(name, sizeof name, "key_%s", k_act[a].name);
        have[0] = 0;
        if (path && *path)
            GetPrivateProfileStringA("controls", name, "", have, sizeof have, path);
        if (!have[0])
            continue;                               /* never written: nothing of the player's */
        parse_keys(have, cur);
        parse_keys(k_old_keys[a], old);
        untouched = cur[0] == old[0] && cur[1] == old[1];
    }
    for (a = 0; a < ACT_COUNT; a++) {
        sprintf_s(name, sizeof name, "key_%s", k_act[a].name);
        if (untouched)
            btvs_setting_set_str("controls", name, k_act[a].keys);
        else                                        /* a key the file does not name keeps its old default */
            btvs_setting_set_str("controls", name, btvs_setting_str("controls", name, k_old_keys[a]));
    }
    btvs_setting_set_int("controls", "key_defaults", KEY_DEFAULTS);
    btvs_settings_save();
    IN_LOG("keys: %s", untouched ? "the default keys are the new ones (21 Nov)" : "the settings file has the player's own keys: kept");
}

static void load_settings(void)
{
    static int registered;
    char name[64];
    const char *s;
    int a, v;
    if (InterlockedCompareExchange(&C.loaded, 1, 0))
        return;
    if (!registered) {
        registered = 1;
        move_old_defaults();
        btvs_setting_on_change("controls", NULL, on_setting_changed, NULL);
    }
    s = btvs_setting_str("controls", "backend", "auto");
    C.backend = !_stricmp(s, "xinput") ? BACKEND_XINPUT : !_stricmp(s, "sdl") ? BACKEND_SDL
              : !_stricmp(s, "none") ? BACKEND_NONE : BACKEND_AUTO;
    C.rumble = btvs_setting_int("controls", "rumble", 1) != 0;
    v = btvs_setting_int("controls", "rumble_strength", 100);
    C.rumble_strength = v < 0 ? 0 : v > 100 ? 100 : v;
    v = btvs_setting_int("controls", "stick_deadzone", 15);
    C.stick_dz = (float)(v < 0 ? 0 : v > 90 ? 90 : v) / 100.0f;
    v = btvs_setting_int("controls", "trigger_deadzone", 12);
    C.trigger_dz = v < 0 ? 0 : v > 90 ? 229 : v * 255 / 100;
    C.invert_y = btvs_setting_int("controls", "invert_y", 0) != 0;
    C.invert_x = btvs_setting_int("controls", "invert_x", 0) != 0;
    C.mouse_look = btvs_setting_int("controls", "mouse_look", 1) != 0;
    C.menu_keys = btvs_setting_int("controls", "menu_keys", 1) != 0;
    v = btvs_setting_int("controls", "sensitivity", 5);           /* 1..10, 5 = normal */
    C.mouse_sens = (float)(v < 1 ? 1 : v > 10 ? 10 : v) / 5.0f;
    v = btvs_setting_int("controls", "keyboard_player", 1);       /* 1-4, 0 = keyboard off */
    C.kb_port = v < 0 || v > 4 ? 0 : v - 1;
    for (a = 0; a < ACT_COUNT; a++) {
        sprintf_s(name, sizeof name, "key_%s", k_act[a].name);
        parse_keys(btvs_setting_str("controls", name, k_act[a].keys), C.key[a]);
        sprintf_s(name, sizeof name, "pad_%s", k_act[a].name);
        v = ps_code(btvs_setting_str("controls", name, k_ps_names[k_act[a].pad]));
        C.pad[a] = v < 0 ? k_act[a].pad : v;
    }
}

static void save_binding(int a, int slot)
{
    char name[64], text[80];
    if (slot < 2) {
        sprintf_s(name, sizeof name, "key_%s", k_act[a].name);
        keys_text(C.key[a], text, sizeof text);
    } else {
        sprintf_s(name, sizeof name, "pad_%s", k_act[a].name);
        strcpy_s(text, sizeof text, k_ps_names[C.pad[a]]);
    }
    btvs_setting_set_str("controls", name, text);
    InterlockedExchange(&C.loaded, 1);              /* our own change: C is already right */
}

/* ── PC controllers and the ports they sit on ────────────────────────────── */

enum { DEV_NONE, DEV_XINPUT, DEV_SDL };
typedef struct {
    int kind;
    uint32_t id;
    int port;                       /* -1: more than four pads */
    int seen;
    in_host_pad pad;
    char name[64];
    uint16_t sent_l, sent_r;
} host_dev;

#define MAX_DEVS (4 + IN_SDL_MAX)
static host_dev s_dev[MAX_DEVS];
static const in_host_pad *s_test_pad;
static in_host_pad s_test_pad_copy;
static uint16_t s_rumble[BTVS_PORTS][2];
static int s_sdl_tried;

static int s_pad_new;               /* a controller has just come onto port 1 */

static host_dev *dev_find(int kind, uint32_t id)
{
    int i;
    for (i = 0; i < MAX_DEVS; i++)
        if (s_dev[i].kind == kind && s_dev[i].id == id)
            return &s_dev[i];
    return NULL;
}

static host_dev *dev_on_port(int port)
{
    int i;
    for (i = 0; i < MAX_DEVS; i++)
        if (s_dev[i].kind && s_dev[i].port == port)
            return &s_dev[i];
    return NULL;
}

static void dev_seen(int kind, uint32_t id, const in_host_pad *pad, const char *name)
{
    host_dev *d = dev_find(kind, id);
    if (!d) {
        int p;
        d = dev_find(DEV_NONE, 0);
        if (!d)
            return;
        memset(d, 0, sizeof *d);
        d->port = -1;
        d->kind = kind;
        d->id = id;
        strncpy_s(d->name, sizeof d->name, name, _TRUNCATE);
        for (p = 0; p < BTVS_PORTS && dev_on_port(p); p++)
            ;
        d->port = p < BTVS_PORTS ? p : -1;
        if (d->port == 0)
            s_pad_new = 1;                      /* (the device in use: a controller plugged in) */
        if (d->port >= 0)
            IN_LOG("player %d: %s connected", d->port + 1, d->name);
        else
            IN_LOG("%s connected, but all four ports are taken", d->name);
    }
    d->pad = *pad;
    d->seen = 1;
}

static int s_test_rumble[3] = { -1, 0, 0 };    /* the last send: player, left, right (for the self-test) */

static void dev_send_rumble(host_dev *d, uint16_t l, uint16_t r)
{
    if (d->sent_l == l && d->sent_r == r)
        return;
    d->sent_l = l; d->sent_r = r;
    IN_LOG("rumble -> %s (%s, player %d): left %u right %u", d->name, d->kind == DEV_XINPUT ? "XInput" : "SDL", d->port + 1, l, r);
    s_test_rumble[0] = d->port; s_test_rumble[1] = l; s_test_rumble[2] = r;
    if (s_test_pad)
        return;
    if (d->kind == DEV_XINPUT)
        in_xi_rumble((int)d->id, l, r);
    else if (d->kind == DEV_SDL)
        in_sdl_rumble(d->id, l, r);
}

static void rumble_apply(int on)
{
    int i;
    for (i = 0; i < MAX_DEVS; i++) {
        host_dev *d = &s_dev[i];
        uint32_t l = 0, r = 0;
        if (!d->kind || d->port < 0)
            continue;
        if (on && C.rumble) {
            l = (uint32_t)s_rumble[d->port][0] * (uint32_t)C.rumble_strength / 100;
            r = (uint32_t)s_rumble[d->port][1] * (uint32_t)C.rumble_strength / 100;
        }
        dev_send_rumble(d, (uint16_t)l, (uint16_t)r);
    }
}

/* Look at what is plugged in. Called with focus only. */
static void devices_refresh(void)
{
    static double last = -1000.0;
    double t = now_ms();
    int i;
    if (t - last < 2.0 && t >= last)
        return;                                 /* the other ports' polls of this same tick */
    last = t;
    for (i = 0; i < MAX_DEVS; i++)
        s_dev[i].seen = 0;
    if (s_test_pad) {
        dev_seen(DEV_XINPUT, 0, s_test_pad, "Test controller");
    } else if (s_clock_fn) {
        /* the self-test: only its own controller, never one that is really plugged in */
    } else if (C.backend != BACKEND_NONE) {
        in_host_pad p;
        char name[32];
        if (C.backend != BACKEND_SDL)
            for (i = 0; i < 4; i++)
                if (in_xi_read(i, &p)) {
                    sprintf_s(name, sizeof name, "Xbox controller %d", i + 1);
                    dev_seen(DEV_XINPUT, (uint32_t)i, &p, name);
                }
        if (C.backend != BACKEND_XINPUT) {
            in_sdl_dev sd[IN_SDL_MAX];
            int n;
            if (!s_sdl_tried) {
                s_sdl_tried = 1;
                in_sdl_start(C.backend == BACKEND_SDL);
            }
            n = in_sdl_snapshot(sd, IN_SDL_MAX);
            for (i = 0; i < n; i++)
                dev_seen(DEV_SDL, sd[i].id, &sd[i].pad, sd[i].name);
        }
    }
    for (i = 0; i < MAX_DEVS; i++)
        if (s_dev[i].kind && !s_dev[i].seen) {
            if (s_dev[i].port >= 0)
                IN_LOG("player %d: %s disconnected", s_dev[i].port + 1, s_dev[i].name);
            memset(&s_dev[i], 0, sizeof s_dev[i]);
        }
}

/* ── PC inputs -> action values ──────────────────────────────────────────── */

/* A PC stick as a vector of length 0..1 past its dead zone. */
static void stick_unit(int16_t x, int16_t y, float *ox, float *oy)
{
    float fx = (float)x / 32767.0f, fy = (float)y / 32767.0f;
    float m = sqrtf(fx * fx + fy * fy), n;
    if (m <= C.stick_dz || m <= 0.0001f) { *ox = *oy = 0.0f; return; }
    n = (m - C.stick_dz) / (1.0f - C.stick_dz);
    if (n > 1.0f) n = 1.0f;
    *ox = fx / m * n;
    *oy = fy / m * n;
}

static float trigger_unit(uint8_t t)
{
    if (t <= C.trigger_dz)
        return 0.0f;
    return (float)(t - C.trigger_dz) / (float)(255 - C.trigger_dz);
}

static float pos(float v) { return v > 0.0f ? v : 0.0f; }

static float source_value(const in_host_pad *p, int src)
{
    float lx, ly, rx, ry;
    if (src <= PS_NONE || src >= PS_COUNT)
        return 0.0f;
    if (src < PS_LT)
        return (p->buttons >> (src - PS_BTN) & 1) ? 1.0f : 0.0f;
    if (src == PS_LT) return trigger_unit(p->lt);
    if (src == PS_RT) return trigger_unit(p->rt);
    stick_unit(p->lx, p->ly, &lx, &ly);
    stick_unit(p->rx, p->ry, &rx, &ry);
    switch (src) {
    case PS_LS_UP:    return pos(ly);
    case PS_LS_DOWN:  return pos(-ly);
    case PS_LS_LEFT:  return pos(-lx);
    case PS_LS_RIGHT: return pos(lx);
    case PS_RS_UP:    return pos(ry);
    case PS_RS_DOWN:  return pos(-ry);
    case PS_RS_LEFT:  return pos(-rx);
    default:          return pos(rx);
    }
}

/* A stick for the game from a vector of length 0..1: straight past the game's
 * own dead zone, so the smallest push the PC stick reports already moves. */
static void stick_out(float x, float y, int16_t *ox, int16_t *oy)
{
    float m = sqrtf(x * x + y * y), k, fx, fy;
    if (m < 0.001f) { *ox = *oy = 0; return; }
    k = (GAME_DEADZONE + (m > 1.0f ? 1.0f : m) * (32767.0f - GAME_DEADZONE)) / (m > 1.0f ? 1.0f : m);
    fx = x * k; fy = y * k;                     /* (two keys at once: the corner, as the Xbox pad's gate gives) */
    *ox = (int16_t)(fx > 32767.0f ? 32767.0f : fx < -32767.0f ? -32767.0f : fx);
    *oy = (int16_t)(fy > 32767.0f ? 32767.0f : fy < -32767.0f ? -32767.0f : fy);
}

static int s_allow_mouse_look = 1;
static int s_wheel_pending;
static int s_wheel_dir;
static double s_wheel_until;
static int s_capture_active, s_capture_release;

/* Keys a pad script holds down right now ("K:Space"): they go through the
 * bindings like real keys, with or without focus. */
static int s_script_key[16], s_script_nkey;
/* Mouse movement a pad script makes right now ("M:20,0"), counts per pad read. */
static int s_script_mouse, s_script_mdx, s_script_mdy;

static int key_value(int code, int mouse_ok, int hw)
{
    int i;
    if (!code)
        return 0;
    for (i = 0; i < s_script_nkey; i++)
        if (s_script_key[i] == code)
            return 1;
    if (!hw)
        return 0;
    if (code == IN_KEY_WHEEL_UP)   return s_wheel_dir > 0;
    if (code == IN_KEY_WHEEL_DOWN) return s_wheel_dir < 0;
    if (in_key_is_mouse(code) && !mouse_ok)
        return 0;                               /* mouse buttons act only while the mouse is the camera */
    return in_key_down(code);
}

/* The menu keys. In the game's menus (no level running, or the pause screen
 * open) Enter and Space are A and Esc and Backspace are B, whatever those keys
 * are bound to in play: Space jumps (B) but confirms, Esc pauses (Start) but
 * cancels. On the title screen Enter and Space are Start as well. A key held
 * while the game goes from play to a menu or back does nothing until it is let
 * go, or Esc would pause and at once cancel the pause. [controls] menu_keys=0
 * turns it off. */
static int s_menu_mode;                         /* 0 play, 1 a menu, 2 the title screen */
static int s_mk_code[4], s_mk_blocked[4];       /* Enter, Space, Escape, Backspace */
static int s_key_sig;                           /* the bound keys down at the last look, added up */

static void menu_key_codes(void)
{
    if (!s_mk_code[0]) {
        s_mk_code[0] = in_key_code("Enter"); s_mk_code[1] = in_key_code("Space");
        s_mk_code[2] = in_key_code("Escape"); s_mk_code[3] = in_key_code("Backspace");
    }
}

/* Game thread (the same that reads the pads). */
void btvs_input_menu_mode(int mode)
{
    int i;
    if ((mode != 0) != (s_menu_mode != 0)) {    /* into a menu or out of one: what is held now waits */
        menu_key_codes();
        for (i = 0; i < 4; i++)
            s_mk_blocked[i] = key_value(s_mk_code[i], 1, !s_clock_fn && !s_test_pad);
    }
    s_menu_mode = mode;
}

static void keys_to_actions(float *v, int mouse_ok, int hw, int port)
{
    int *const code = s_mk_code, *const blocked = s_mk_blocked;
    int down[4], menu = C.menu_keys ? s_menu_mode : 0, a, i, k;
    menu_key_codes();
    s_key_sig = 0;
    for (i = 0; i < 4; i++)
        down[i] = key_value(code[i], 1, hw);
    for (i = 0; i < 4; i++)
        if (!down[i])
            blocked[i] = 0;
    for (a = 0; a < ACT_COUNT; a++)
        for (k = 0; k < 2; k++) {
            int c = C.key[a][k];
            for (i = 0; i < 4 && c; i++)
                if (c == code[i] && (menu || blocked[i]))
                    c = 0;
            if (key_value(c, mouse_ok, hw)) {
                v[a] = 1.0f;
                s_key_sig += c;
            }
        }
    for (i = 0; menu && i < 4; i++)
        if (down[i] && !blocked[i]) {
            v[i < 2 ? ACT_A : ACT_B] = 1.0f;
            if (menu == 2 && i < 2)
                v[ACT_START] = 1.0f;
        }
    (void)port;
}

/* hw 0: the window has no focus, so no device, key or mouse is asked; only
 * keys held by a pad script count. */
/* ── the device in use ───────────────────────────────────────────────────── */

/* Player 1's controller and the keyboard and mouse both play at all times;
 * nothing is chosen. What is tracked here is which of the two the player is
 * on right now, for the button pictures, the tips and the Controls page:
 *   - a controller: a button pressed, a trigger pulled past a third, a stick
 *     pushed past half (and not again until it has been back under a third),
 *     or the controller plugged in;
 *   - keyboard and mouse: a key or mouse button pressed, or the mouse moved
 *     30 counts within a quarter second while it is the camera; also when
 *     player 1's controller is unplugged.
 * Only a new touch counts, never something held or resting, so a stick that
 * drifts or sticks and a mouse that trembles change nothing; and after a
 * change the next one waits a second, so two devices touched at once do not
 * flicker. It is kept in the settings (last_device) for the next start. */
#define USE_PAD  1
#define USE_KEYS 2
#define USE_HOLD_MS    1000.0       /* between two changes */
#define USE_STICK_ON   0.50f
#define USE_STICK_OFF  0.30f
#define USE_TRIGGER_ON  85
#define USE_TRIGGER_OFF 50
#define USE_MOUSE_COUNTS 30
#define USE_MOUSE_MS   250.0

static struct {
    int use;                        /* 0 not known yet, USE_PAD, USE_KEYS */
    int ps;                         /* that controller is a PlayStation one */
    double changed_ms;
    uint32_t changes;
    /* the last look at each device */
    uint32_t pad_buttons;
    int pad_hot[4];                 /* left stick, right stick, left trigger, right trigger */
    int key_sig;
    int mouse_acc;
    double mouse_from_ms;
    int script;                     /* BTVS_INPUT_SCRIPT_DEVICE: 0 off, 1 Xbox, 2 PlayStation */
    int script_pad, script_keys;
} U;

static int name_is_ps(const char *n)
{
    return strstr(n, "PS") || strstr(n, "DualSense") || strstr(n, "DualShock") || strstr(n, "Sony")
        || strstr(n, "PlayStation") || !strcmp(n, "Wireless Controller");
}

/* A new touch of a device. force: no waiting (the controller was unplugged). */
static void use_event(int kind, int ps, const char *why, const char *name, int force, int keep)
{
    static const char *const k_what[] = { "nothing yet", "controller", "keyboard and mouse" };
    double t = now_ms();
    if (U.use == kind && (kind != USE_PAD || U.ps == ps))
        return;
    if (U.use && !force && t - U.changed_ms < USE_HOLD_MS && t >= U.changed_ms)
        return;
    IN_LOG("device in use: %s%s%s%s (%s); it was %s", kind == USE_PAD ? (ps ? "PlayStation controller" : "Xbox controller") : k_what[USE_KEYS],
           name && *name ? ", " : "", name && *name ? name : "", "", why, k_what[U.use]);
    U.use = kind;
    U.ps = kind == USE_PAD && ps;
    U.changed_ms = t;
    U.changes++;
    if (keep && !s_clock_fn) {
        btvs_setting_set_str("controls", "last_device", kind == USE_PAD ? "controller" : "keyboard");
        btvs_settings_save();
    }
}

/* Player 1's controller, read just now. */
static void use_look_pad(const host_dev *d)
{
    const in_host_pad *p = &d->pad;
    const char *why = NULL;
    float m[2];
    int t[2], i;
    m[0] = sqrtf((float)p->lx * p->lx + (float)p->ly * p->ly) / 32767.0f;
    m[1] = sqrtf((float)p->rx * p->rx + (float)p->ry * p->ry) / 32767.0f;
    t[0] = p->lt; t[1] = p->rt;
    for (i = 0; i < 2; i++) {
        if (!U.pad_hot[i] && m[i] >= USE_STICK_ON)  { U.pad_hot[i] = 1; why = i ? "right stick" : "left stick"; }
        else if (U.pad_hot[i] && m[i] < USE_STICK_OFF) U.pad_hot[i] = 0;
        if (!U.pad_hot[2 + i] && t[i] >= USE_TRIGGER_ON) { U.pad_hot[2 + i] = 1; why = i ? "right trigger" : "left trigger"; }
        else if (U.pad_hot[2 + i] && t[i] < USE_TRIGGER_OFF) U.pad_hot[2 + i] = 0;
    }
    if (p->buttons & ~U.pad_buttons)
        why = "a button";
    U.pad_buttons = p->buttons;
    if (s_pad_new) {
        s_pad_new = 0;
        if (U.use && !why)
            why = "plugged in";
    }
    if (why)
        use_event(USE_PAD, name_is_ps(d->name), why, d->name, 0, 1);
}

/* Keys and mouse buttons down right now (0: none), and the mouse's movement. */
static void use_look_keys(int sig, int moved)
{
    double t = now_ms();
    if (sig && sig != U.key_sig)
        use_event(USE_KEYS, 0, "a key", NULL, 0, 1);
    U.key_sig = sig;
    if (t - U.mouse_from_ms > USE_MOUSE_MS || t < U.mouse_from_ms) {
        U.mouse_from_ms = t;
        U.mouse_acc = 0;
    }
    U.mouse_acc += moved;
    if (U.mouse_acc >= USE_MOUSE_COUNTS) {
        U.mouse_acc = 0;
        use_event(USE_KEYS, 0, "the mouse", NULL, 0, 1);
    }
}

/* BTVS_INPUT_SCRIPT_DEVICE=xbox|ps: testing. What a pad script presses counts
 * as the player's device (its buttons and sticks as that controller, its K:
 * and M: lines as keyboard and mouse), so a hidden run can show the change.
 * Nothing of it goes to the settings. */
static void use_look_script(const btvs_pad *before, const btvs_pad *after)
{
    int pad, keys = s_script_nkey || s_script_mouse;
    if (!U.script)
        return;
    pad = memcmp(before, after, sizeof *before) != 0;
    if (pad && !U.script_pad)
        use_event(USE_PAD, U.script == 2, "a script's button", "the test script", 0, 0);
    if (keys && !U.script_keys)
        use_event(USE_KEYS, 0, "a script's key", NULL, 0, 0);
    U.script_pad = pad;
    U.script_keys = keys;
}

/* Where it starts: on the controller when one is there, unless the player
 * was last on the keyboard. */
static void use_start(void)
{
    host_dev *d = NULL;
    const char *e = getenv("BTVS_INPUT_SCRIPT_DEVICE");
    int tries;
    U.script = !e || !*e || *e == '0' ? 0 : (*e == 'p' || *e == 'P') ? 2 : 1;
    /* (a hidden test run never asks for the owner's controllers, here either) */
    for (tries = 0; tries < 5 && !d && (s_test_pad || s_focus_fn || !env_on("BTVS_NO_WINDOW")); tries++) {
        if (tries) {                            /* SDL's pads turn up a moment after it starts */
            UNLOCK();
            Sleep(50);
            LOCK();
        }
        devices_refresh();
        d = dev_on_port(0);
        if (C.backend == BACKEND_XINPUT || C.backend == BACKEND_NONE || s_test_pad || !s_sdl_tried)
            break;
    }
    s_pad_new = 0;
    if (d && _stricmp(btvs_setting_str("controls", "last_device", ""), "keyboard"))
        use_event(USE_PAD, name_is_ps(d->name), "connected at the start", d->name, 1, 0);
    else
        use_event(USE_KEYS, 0, d ? "a controller is connected, but the keyboard was used last" : "no controller", NULL, 1, 0);
}

int btvs_input__glyph_device(void)
{
    int r;
    LOCK();
    load_settings();
    if (!U.use)
        use_start();
    r = U.use == USE_PAD ? 1 + U.ps : 0;
    UNLOCK();
    return r;
}

uint32_t btvs_input__device_changes(void)
{
    return U.changes;
}

static void live_pad(int port, btvs_pad *g, int hw)
{
    float v[ACT_COUNT];
    float mlx = 0.0f, mly = 0.0f;               /* the mouse, as a look vector */
    host_dev *d;
    int a;

    memset(v, 0, sizeof v);
    if (hw)
        devices_refresh();
    d = hw ? dev_on_port(port) : NULL;
    if (d)
        for (a = 0; a < ACT_COUNT; a++)
            v[a] = source_value(&d->pad, C.pad[a]);
    if (hw && port == 0) {
        if (d)
            use_look_pad(d);
        else if (U.use == USE_PAD)
            use_event(USE_KEYS, 0, "the controller was unplugged", NULL, 1, 1);
    }

    if (port == C.kb_port && !hw) {
        keys_to_actions(v, 1, 0, port);
        /* the script's mouse is the camera under the same rules as a real one */
        if (s_script_mouse && C.mouse_look && s_allow_mouse_look) {
            mlx = (float)s_script_mdx * 0.04f * C.mouse_sens;
            mly = (float)-s_script_mdy * 0.04f * C.mouse_sens;
        }
    } else if (port == C.kb_port && !s_test_pad) {
        double t = now_ms();
        int cap, dx, dy;
        in_mouse_capture(C.mouse_look && s_allow_mouse_look && !s_capture_active);
        cap = in_mouse_is_captured();
        if (s_wheel_pending) {
            s_wheel_dir = s_wheel_pending > 0 ? 1 : -1;
            s_wheel_pending -= s_wheel_dir;
            s_wheel_until = t + 70.0;           /* a notch is a short press */
        } else if (t >= s_wheel_until) {
            s_wheel_dir = 0;
        }
        keys_to_actions(v, cap, 1, port);
        dx = dy = 0;
        if (cap) {
            in_mouse_delta(&dx, &dy);
            mlx = (float)dx * 0.04f * C.mouse_sens;
            mly = (float)-dy * 0.04f * C.mouse_sens;
        }
        if (port == 0)
            use_look_keys(in_key_any_down() + s_key_sig + (s_wheel_dir ? 7 : 0), abs(dx) + abs(dy));
    }

    if (v[ACT_START] >= 0.5f)  g->buttons |= BTVS_BTN_START;
    if (v[ACT_BACK] >= 0.5f)   g->buttons |= BTVS_BTN_BACK;
    if (v[ACT_DUP] >= 0.5f)    g->buttons |= BTVS_BTN_UP;
    if (v[ACT_DDOWN] >= 0.5f)  g->buttons |= BTVS_BTN_DOWN;
    if (v[ACT_DLEFT] >= 0.5f)  g->buttons |= BTVS_BTN_LEFT;
    if (v[ACT_DRIGHT] >= 0.5f) g->buttons |= BTVS_BTN_RIGHT;
    if (v[ACT_LSTICK] >= 0.5f) g->buttons |= BTVS_BTN_LSTICK;
    if (v[ACT_RSTICK] >= 0.5f) g->buttons |= BTVS_BTN_RSTICK;
    for (a = 0; a < 8; a++) {                   /* ACT_A..ACT_RT are analog[0..7] in order */
        float f = v[ACT_A + a] * 255.0f + 0.5f;
        g->analog[a] = (uint8_t)(f > 255.0f ? 255.0f : f);
    }
    stick_out(v[ACT_MOVE_RIGHT] - v[ACT_MOVE_LEFT], v[ACT_MOVE_UP] - v[ACT_MOVE_DOWN], &g->lx, &g->ly);
    {
        float x = v[ACT_LOOK_RIGHT] - v[ACT_LOOK_LEFT] + mlx;
        float y = v[ACT_LOOK_UP] - v[ACT_LOOK_DOWN] + mly;
        if (C.invert_x) x = -x;
        if (C.invert_y) y = -y;
        stick_out(x, y, &g->rx, &g->ry);
    }
}

/* ── frames ──────────────────────────────────────────────────────────────── */

static uint32_t s_frame;
static int s_frame_external;        /* Graphics calls btvs_input_frame() */
static int s_read1;                 /* this run of the game has read a pad ... */
static double s_read1_ms;           /* ... and when the first read was. The game starts its
                                     * pad timer when it is ready to run (after a level's
                                     * load), so this is the same moment in every run. */

static void frame_count(void)
{
    s_frame++;
}

void btvs_input_frame(void)
{
    LOCK();
    s_frame_external = 1;
    frame_count();
    UNLOCK();
}

/* ── the test script ─────────────────────────────────────────────────────── */

#define SC_MAX_EV  512
#define SC_MAX_SEG 16
typedef struct {
    int seg, port, frames;          /* frames: at/dur/every count frames, not ms */
    double at, dur, every;
    btvs_pad pad;
    int has_ls, has_rs;
    int key[4], nkey;               /* "K:<key name>": keyboard keys held, through the bindings */
    int has_mouse, mdx, mdy;        /* "M:<dx>,<dy>": mouse movement, counts for each pad read */
    int seen;                       /* a pad read has fallen inside its time */
    double late;                    /* it began this much late (the game was not reading the pad) */
} sc_event;
typedef struct { char mark[24]; int fired; double t0; uint32_t f0; } sc_segment;

static sc_event   s_ev[SC_MAX_EV];
static sc_segment s_seg[SC_MAX_SEG];
static int s_nev, s_nseg;
static uint32_t s_script_ports;     /* ports the script gives a pad */
static int s_script_env_done;

/* "1.5" "1.5s" seconds, "f90" frames, a leading "+" = after the previous line */
static int parse_time(const char *t, double *val, int *frames, int *relative)
{
    char *end;
    *relative = 0; *frames = 0;
    if (*t == '+') { *relative = 1; t++; }
    if (*t == 'f' || *t == 'F') { *frames = 1; t++; }
    if (!isdigit((unsigned char)*t) && *t != '.')
        return 0;
    *val = strtod(t, &end);
    if (*end == 's' || *end == 'S')
        end++;
    if (*end)
        return 0;
    if (!*frames)
        *val *= 1000.0;
    return 1;
}

static int16_t unit16(double f)
{
    f *= 32767.0;
    return (int16_t)(f > 32767.0 ? 32767.0 : f < -32767.0 ? -32767.0 : f);
}

static int script_token(sc_event *e, char *tok)
{
    static const struct { const char *n; int kind, v; } names[] = {
        { "UP", 0, BTVS_BTN_UP }, { "DOWN", 0, BTVS_BTN_DOWN }, { "LEFT", 0, BTVS_BTN_LEFT }, { "RIGHT", 0, BTVS_BTN_RIGHT },
        { "START", 0, BTVS_BTN_START }, { "BACK", 0, BTVS_BTN_BACK }, { "L3", 0, BTVS_BTN_LSTICK }, { "R3", 0, BTVS_BTN_RSTICK },
        { "A", 1, BTVS_AN_A }, { "B", 1, BTVS_AN_B }, { "X", 1, BTVS_AN_X }, { "Y", 1, BTVS_AN_Y },
        { "BLACK", 1, BTVS_AN_BLACK }, { "WHITE", 1, BTVS_AN_WHITE }, { "LT", 1, BTVS_AN_LT }, { "RT", 1, BTVS_AN_RT },
        { "LSU", 2, 0 }, { "LSD", 2, 1 }, { "LSL", 2, 2 }, { "LSR", 2, 3 },
        { "RSU", 3, 0 }, { "RSD", 3, 1 }, { "RSL", 3, 2 }, { "RSR", 3, 3 },
    };
    char *eq = strchr(tok, '=');
    int i;
    if (eq)
        *eq++ = 0;
    if (!_stricmp(tok, "NONE"))
        return 1;
    if ((tok[0] == 'K' || tok[0] == 'k') && tok[1] == ':') {
        int code = in_key_code(tok + 2);
        if (!code || e->nkey >= 4)
            return 0;
        e->key[e->nkey++] = code;
        return 1;
    }
    if ((tok[0] == 'M' || tok[0] == 'm') && tok[1] == ':') {
        char *comma = strchr(tok + 2, ',');
        if (!comma)
            return 0;
        e->has_mouse = 1;
        e->mdx = atoi(tok + 2);
        e->mdy = atoi(comma + 1);
        return 1;
    }
    if (!_stricmp(tok, "LS") || !_stricmp(tok, "RS")) {
        double x, y;
        char *comma = eq ? strchr(eq, ',') : NULL;
        if (!comma)
            return 0;
        x = atof(eq); y = atof(comma + 1);
        if (tok[0] == 'L' || tok[0] == 'l') { e->pad.lx = unit16(x); e->pad.ly = unit16(y); e->has_ls = 1; }
        else                                { e->pad.rx = unit16(x); e->pad.ry = unit16(y); e->has_rs = 1; }
        return 1;
    }
    for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
        if (_stricmp(tok, names[i].n))
            continue;
        switch (names[i].kind) {
        case 0: e->pad.buttons |= (uint16_t)names[i].v; break;
        case 1: { int a = eq ? atoi(eq) : 255; e->pad.analog[names[i].v] = (uint8_t)(a < 0 ? 0 : a > 255 ? 255 : a); break; }
        case 2: e->has_ls = 1;
                if (names[i].v == 0) e->pad.ly = 32767; else if (names[i].v == 1) e->pad.ly = -32767;
                else if (names[i].v == 2) e->pad.lx = -32767; else e->pad.lx = 32767;
                break;
        default: e->has_rs = 1;
                if (names[i].v == 0) e->pad.ry = 32767; else if (names[i].v == 1) e->pad.ry = -32767;
                else if (names[i].v == 2) e->pad.rx = -32767; else e->pad.rx = 32767;
                break;
        }
        return 1;
    }
    return 0;
}

static int script_load_locked(const char *text)
{
    char *buf = _strdup(text ? text : ""), *line, *ctx = NULL;
    int port = 0, lineno = 0, prev_frames = 0;
    double hold_ms = 150.0, prev_at = 0.0;

    s_nev = 0;
    s_nseg = 1;
    s_script_ports = 0;
    memset(s_seg, 0, sizeof s_seg);
    for (line = strtok_s(buf, "\n;", &ctx); line; line = strtok_s(NULL, "\n;", &ctx)) {
        char *tok, *tctx = NULL, *hash = strchr(line, '#');
        sc_event e;
        double val;
        int frames, rel, bad = 0;
        lineno++;
        if (hash)
            *hash = 0;
        tok = strtok_s(line, " \t\r", &tctx);
        if (!tok)
            continue;
        if (!_stricmp(tok, "after")) {
            tok = strtok_s(NULL, " \t\r", &tctx);
            if (tok && s_nseg < SC_MAX_SEG) {
                strncpy_s(s_seg[s_nseg].mark, sizeof s_seg[s_nseg].mark, tok, _TRUNCATE);
                s_nseg++;
                prev_at = 0.0;
            } else IN_LOG("script line %d: after <name>", lineno);
            continue;
        }
        if (!_stricmp(tok, "port")) {
            tok = strtok_s(NULL, " \t\r", &tctx);
            port = tok ? atoi(tok) - 1 : 0;
            if (port < 0 || port >= BTVS_PORTS) { IN_LOG("script line %d: port 1-4", lineno); port = 0; }
            continue;
        }
        if (!_stricmp(tok, "hold")) {
            tok = strtok_s(NULL, " \t\r", &tctx);
            if (tok && parse_time(tok, &val, &frames, &rel) && !frames) hold_ms = val;
            else IN_LOG("script line %d: hold <seconds>", lineno);
            continue;
        }
        if (!parse_time(tok, &val, &frames, &rel)) {
            IN_LOG("script line %d: \"%s\" is not a time", lineno, tok);
            continue;
        }
        memset(&e, 0, sizeof e);
        e.seg = s_nseg - 1;
        e.port = port;
        e.frames = frames;
        e.at = rel ? (frames == prev_frames ? prev_at + val : val) : val;
        e.dur = frames ? 9.0 : hold_ms;
        while ((tok = strtok_s(NULL, " \t\r", &tctx)) != NULL) {
            if (!_stricmp(tok, "for") || !_stricmp(tok, "every")) {
                int is_for = !_stricmp(tok, "for"), f2, r2;
                tok = strtok_s(NULL, " \t\r", &tctx);
                if (!tok || !parse_time(tok, &val, &f2, &r2) || f2 != frames) { bad = 1; break; }
                if (is_for) e.dur = val; else e.every = val;
            } else if ((tok[0] == 'p' || tok[0] == 'P') && tok[1] >= '1' && tok[1] <= '4' && !tok[2]) {
                e.port = tok[1] - '1';
            } else if (!script_token(&e, tok)) {
                bad = 1;
                break;
            }
        }
        if (bad) {
            IN_LOG("script line %d: cannot read \"%s\"", lineno, tok ? tok : "(end of line)");
            continue;
        }
        prev_at = e.at;
        prev_frames = frames;
        if (s_nev < SC_MAX_EV) {
            s_ev[s_nev++] = e;
            if (!e.nkey && !e.has_mouse)
                s_script_ports |= 1u << e.port;
        }
    }
    free(buf);
    if (s_nev)
        IN_LOG("pad script: %d events in %d part(s)", s_nev, s_nseg);
    return s_nev;
}

int btvs_input_script_load_text(const char *text)
{
    int n;
    LOCK();
    s_script_env_done = 1;
    n = script_load_locked(text);
    UNLOCK();
    return n;
}

int btvs_input_script_load_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long size;
    int n;
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)calloc(1, (size_t)size + 1);
    fread(buf, 1, (size_t)size, f);
    fclose(f);
    n = btvs_input_script_load_text(buf);
    free(buf);
    return n;
}

/* BTVS_PAD_SCRIPT: a file, or the script itself with ';' between lines.
 * Called with the lock held. */
static void script_env_load_locked(void)
{
    const char *e;
    FILE *f;
    if (s_script_env_done)
        return;
    s_script_env_done = 1;
    e = getenv("BTVS_PAD_SCRIPT");
    if (!e || !*e)
        return;
    f = fopen(e, "rb");
    if (f) {
        long size;
        char *buf;
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
        buf = (char *)calloc(1, (size_t)size + 1);
        fread(buf, 1, (size_t)size, f);
        fclose(f);
        script_load_locked(buf);
        free(buf);
    } else {
        script_load_locked(e);
    }
}

static int script_current(void)
{
    int i, cur = 0;
    for (i = 1; i < s_nseg; i++)
        if (s_seg[i].fired)
            cur = i;
    return cur;
}

/* Parts run in the order they are written: a mark only starts the part that
 * comes next, so the same name can be waited for more than once. */
void btvs_input_script_mark(const char *name)
{
    int i;
    LOCK();
    script_env_load_locked();                   /* a mark can come before the first pad read */
    i = script_current() + 1;
    if (i < s_nseg && !_stricmp(s_seg[i].mark, name)) {
        s_seg[i].fired = 1;
        s_seg[i].t0 = now_ms();
        s_seg[i].f0 = s_frame;
        IN_LOG("pad script: \"%s\" reached", name);
    }
    UNLOCK();
}

int btvs_input_script_done(void)
{
    int i, cur, done = 1;
    LOCK();
    cur = script_current();
    if (s_nev && (cur != s_nseg - 1 || !s_seg[cur].fired))
        done = 0;
    for (i = 0; done && i < s_nev; i++) {
        const sc_event *e = &s_ev[i];
        double rel = e->frames ? (double)(s_frame - s_seg[cur].f0) : now_ms() - s_seg[cur].t0;
        if (e->seg == cur && e->every <= 0.0 && (!e->seen || rel < e->at + e->late + e->dur))
            done = 0;
    }
    UNLOCK();
    return done;
}

static void script_env_load(void)
{
    script_env_load_locked();
}

/* The clock starts at the game's first read that the script may answer. A
 * recording plays first: marks reached meanwhile still count, and the part the
 * script is in by then starts its clock when the recording ends. */
static void script_start(void)
{
    script_env_load();
    if (s_nev && !s_seg[0].fired) {
        int cur = script_current();
        s_seg[0].fired = 1;
        s_seg[cur].t0 = now_ms();
        s_seg[cur].f0 = s_frame;
    }
}

/* A press whose whole time passed with no pad read (the game stalled: a load,
 * a frame dump) is not lost: it starts at the first read after and lasts as
 * long as written. */
static int script_event_on(sc_event *e, int cur)
{
    double rel;
    if (e->seg != cur)
        return 0;
    rel = (e->frames ? (double)(s_frame - s_seg[cur].f0) : now_ms() - s_seg[cur].t0) - e->at;
    if (rel < 0.0)
        return 0;
    if (e->every > 0.0)
        return fmod(rel, e->every) < e->dur;
    if (!e->seen) {
        e->seen = 1;
        e->late = rel >= e->dur ? rel : 0.0;
    }
    return rel - e->late < e->dur;
}

/* The keys the script holds down now (they belong to the keyboard's player). */
static void script_keys(void)
{
    int i, k, cur;
    s_script_nkey = 0;
    s_script_mouse = s_script_mdx = s_script_mdy = 0;
    script_start();
    if (!s_nev)
        return;
    cur = script_current();
    for (i = 0; i < s_nev; i++) {
        if ((!s_ev[i].nkey && !s_ev[i].has_mouse) || !script_event_on(&s_ev[i], cur))
            continue;
        for (k = 0; k < s_ev[i].nkey && s_script_nkey < 16; k++)
            s_script_key[s_script_nkey++] = s_ev[i].key[k];
        if (s_ev[i].has_mouse) {
            s_script_mouse = 1;
            s_script_mdx += s_ev[i].mdx;
            s_script_mdy += s_ev[i].mdy;
        }
    }
}

static void script_apply(int port, btvs_pad *g)
{
    int i, a, cur;
    script_start();
    if (!s_nev)
        return;
    cur = script_current();
    for (i = 0; i < s_nev; i++) {
        sc_event *e = &s_ev[i];
        if (e->nkey || e->has_mouse || e->port != port || !script_event_on(e, cur))
            continue;
        g->buttons |= e->pad.buttons;
        for (a = 0; a < 8; a++)
            if (e->pad.analog[a] > g->analog[a])
                g->analog[a] = e->pad.analog[a];
        if (e->has_ls) { g->lx = e->pad.lx; g->ly = e->pad.ly; }
        if (e->has_rs) { g->rx = e->pad.rx; g->ry = e->pad.ry; }
    }
}

/* ── recording ───────────────────────────────────────────────────────────── */
/* File: "BTVSINP1", u32 version (1), u32 reserved; then records.
 *   state record   tag = port | connected << 2
 *                  varint polls   this port's reads since its previous record
 *                  varint frames  frames since the previous record (any port)
 *                  varint ms      milliseconds since the previous record
 *                  u16 mask       bit 0 buttons, 1-8 analog[0..7], 9-12 lx ly rx ry
 *                  the fields the mask names (u16, u8 x n, i16 x n)
 *   end record     tag = 0xFF, varint polls x 4 (reads since each port's last
 *                  record), varint frames, varint ms
 *   first read     tag = 0xFE, varint ms since the recording began: when the
 *                  game read a pad for the first time in this run. Only in a
 *                  recording begun by then (a run recorded from its start).
 * A record is written when a port's pad differs from its previous record, so a
 * resting pad costs nothing. A file without an end record (the game died) is
 * still good: playback stops at the last record. */

#define REC_MAGIC "BTVSINP1"
static struct {
    FILE *f;
    uint32_t polls[BTVS_PORTS];     /* since the port's last record */
    int have[BTVS_PORTS], conn[BTVS_PORTS];
    btvs_pad last[BTVS_PORTS];
    uint32_t frame;
    double ms, flushed, t0;
    int unwritten;                  /* records since the last flush */
    int anchored;                   /* the first-read record is written, or there will be none */
} R;

static void put_varint(FILE *f, uint32_t v)
{
    while (v >= 0x80) { fputc((int)(v & 0x7F) | 0x80, f); v >>= 7; }
    fputc((int)v, f);
}

static void record_poll(int port, int connected, const btvs_pad *g)
{
    double t;
    uint16_t mask = 0;
    int a;
    if (!R.f)
        return;
    R.polls[port]++;
    t = now_ms();
    if (!R.anchored && s_read1) {
        fputc(0xFE, R.f);
        put_varint(R.f, (uint32_t)(s_read1_ms > R.t0 ? s_read1_ms - R.t0 + 0.5 : 0.0));
        R.anchored = 1;
        R.unwritten = 1;
    }
    if (R.unwritten && t - R.flushed > 1000.0) {    /* a crash loses at most a second */
        fflush(R.f);
        R.flushed = t;
        R.unwritten = 0;
    }
    if (R.have[port] && R.conn[port] == connected && !memcmp(g, &R.last[port], sizeof *g))
        return;
    if (!R.have[port]) {
        mask = 0x1FFF;
    } else {
        if (g->buttons != R.last[port].buttons) mask |= 1;
        for (a = 0; a < 8; a++)
            if (g->analog[a] != R.last[port].analog[a]) mask |= (uint16_t)(2 << a);
        if (g->lx != R.last[port].lx) mask |= 0x0200;
        if (g->ly != R.last[port].ly) mask |= 0x0400;
        if (g->rx != R.last[port].rx) mask |= 0x0800;
        if (g->ry != R.last[port].ry) mask |= 0x1000;
    }
    fputc(port | (connected ? 4 : 0), R.f);
    put_varint(R.f, R.polls[port]);
    put_varint(R.f, s_frame - R.frame);
    put_varint(R.f, (uint32_t)(t - R.ms + 0.5));
    fwrite(&mask, 2, 1, R.f);
    if (mask & 1) fwrite(&g->buttons, 2, 1, R.f);
    for (a = 0; a < 8; a++)
        if (mask & (2 << a)) fputc(g->analog[a], R.f);
    if (mask & 0x0200) fwrite(&g->lx, 2, 1, R.f);
    if (mask & 0x0400) fwrite(&g->ly, 2, 1, R.f);
    if (mask & 0x0800) fwrite(&g->rx, 2, 1, R.f);
    if (mask & 0x1000) fwrite(&g->ry, 2, 1, R.f);
    R.polls[port] = 0;
    R.have[port] = 1;
    R.conn[port] = connected;
    R.last[port] = *g;
    R.frame = s_frame;
    R.ms += floor(t - R.ms + 0.5);
    R.unwritten = 1;
}

static void record_end_locked(void)
{
    int p;
    if (!R.f)
        return;
    fputc(0xFF, R.f);
    for (p = 0; p < BTVS_PORTS; p++)
        put_varint(R.f, R.polls[p]);
    put_varint(R.f, s_frame - R.frame);
    put_varint(R.f, (uint32_t)(now_ms() - R.ms + 0.5));
    fclose(R.f);
    R.f = NULL;
    IN_LOG("recording ended");
}

int btvs_input_record_begin(const char *path)
{
    uint32_t hdr[2] = { 1, 0 };
    int ok;
    LOCK();
    record_end_locked();
    memset(&R, 0, sizeof R);
    R.f = fopen(path, "wb");
    if (R.f) {
        fwrite(REC_MAGIC, 1, 8, R.f);
        fwrite(hdr, 4, 2, R.f);
        R.frame = s_frame;
        R.ms = R.flushed = R.t0 = now_ms();
        R.anchored = s_read1;         /* begun in mid-game: no first read to mark */
        IN_LOG("recording to %s", path);
    } else {
        IN_LOG("cannot record to %s", path);
    }
    ok = R.f != NULL;
    UNLOCK();
    return ok;
}

void btvs_input_record_end(void)
{
    LOCK();
    record_end_locked();
    UNLOCK();
}

/* Logs: put what has been recorded so far on disk (before copying the file). */
void btvs_input_record_flush(void)
{
    LOCK();
    if (R.f) {
        fflush(R.f);
        R.flushed = now_ms();
        R.unwritten = 0;
    }
    UNLOCK();
}

int btvs_input_recording(void)
{
    return R.f != NULL;
}

/* ── playback ────────────────────────────────────────────────────────────── */

typedef struct { uint32_t poll, frame, ms; btvs_pad pad; uint8_t conn; } pb_rec;
enum { PB_POLL, PB_FRAME, PB_TIME, PB_GAME };
static struct {
    int active, mode;
    pb_rec *rec[BTVS_PORTS];
    int n[BTVS_PORTS], idx[BTVS_PORTS];
    uint32_t total[BTVS_PORTS];     /* reads of each port in the recording */
    int main_port;                  /* the port read most: the recording ends with it */
    uint32_t polls[BTVS_PORTS];     /* reads of each port so far */
    uint32_t end_frame, end_ms;
    uint32_t f0;
    double t0;
    uint32_t anchor;                /* PB_GAME: the recording's first frame, ms from its start */
    double frame1;                  /* PB_GAME: this run's first frame as counted from; 0 = not yet */
} P;

static int get_varint(const uint8_t **p, const uint8_t *end, uint32_t *v)
{
    int shift = 0;
    *v = 0;
    while (*p < end && shift < 35) {
        uint8_t b = *(*p)++;
        *v |= (uint32_t)(b & 0x7F) << shift;
        if (!(b & 0x80))
            return 1;
        shift += 7;
    }
    return 0;
}

static int s_parse_anchor;          /* the recording just read: ms of its first-read record, -1 none */

/* Read a whole recording. Returns the record count, -1 when it is not one. */
static int recording_parse(const char *path, pb_rec *out[BTVS_PORTS], int n[BTVS_PORTS],
                           uint32_t total[BTVS_PORTS], uint32_t *end_frame, uint32_t *end_ms)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    const uint8_t *p, *end;
    long size;
    uint32_t polls[BTVS_PORTS] = { 0 }, frame = 0, ms = 0;
    btvs_pad last[BTVS_PORTS];
    int cap[BTVS_PORTS] = { 0 }, count = 0, port;

    memset(last, 0, sizeof last);
    for (port = 0; port < BTVS_PORTS; port++) { out[port] = NULL; n[port] = 0; total[port] = 0; }
    *end_frame = *end_ms = 0;
    s_parse_anchor = -1;
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (uint8_t *)malloc((size_t)size + 1);
    if (fread(buf, 1, (size_t)size, f) != (size_t)size || size < 16 || memcmp(buf, REC_MAGIC, 8)) {
        fclose(f);
        free(buf);
        return -1;
    }
    fclose(f);
    p = buf + 16;
    end = buf + size;
    while (p < end) {
        uint8_t tag = *p++;
        uint32_t dp, df, dm;
        uint16_t mask;
        pb_rec *r;
        int a;
        if (tag == 0xFF) {
            for (port = 0; port < BTVS_PORTS; port++)
                if (get_varint(&p, end, &dp))
                    polls[port] += dp;
            if (get_varint(&p, end, &df)) frame += df;
            if (get_varint(&p, end, &dm)) ms += dm;
            break;
        }
        if (tag == 0xFE) {
            if (!get_varint(&p, end, &dm))
                break;
            s_parse_anchor = (int)dm;
            continue;
        }
        port = tag & 3;
        if (!get_varint(&p, end, &dp) || !get_varint(&p, end, &df) || !get_varint(&p, end, &dm) || end - p < 2)
            break;
        memcpy(&mask, p, 2); p += 2;
        {
            int need = (mask & 1 ? 2 : 0), b;
            for (b = 1; b <= 8; b++) if (mask >> b & 1) need++;
            for (b = 9; b <= 12; b++) if (mask >> b & 1) need += 2;
            if (end - p < need)
                break;                          /* cut off mid-record: stop at the last whole one */
        }
        if (mask & 1) { memcpy(&last[port].buttons, p, 2); p += 2; }
        for (a = 0; a < 8; a++)
            if (mask & (2 << a)) last[port].analog[a] = *p++;
        if (mask & 0x0200) { memcpy(&last[port].lx, p, 2); p += 2; }
        if (mask & 0x0400) { memcpy(&last[port].ly, p, 2); p += 2; }
        if (mask & 0x0800) { memcpy(&last[port].rx, p, 2); p += 2; }
        if (mask & 0x1000) { memcpy(&last[port].ry, p, 2); p += 2; }
        polls[port] += dp;
        frame += df;
        ms += dm;
        if (n[port] == cap[port]) {
            cap[port] = cap[port] ? cap[port] * 2 : 256;
            out[port] = (pb_rec *)realloc(out[port], (size_t)cap[port] * sizeof(pb_rec));
        }
        r = &out[port][n[port]++];
        r->poll = polls[port];
        r->frame = frame;
        r->ms = ms;
        r->pad = last[port];
        r->conn = (uint8_t)(tag >> 2 & 1);
        count++;
    }
    for (port = 0; port < BTVS_PORTS; port++)
        total[port] = polls[port];
    *end_frame = frame;
    *end_ms = ms;
    free(buf);
    return count;
}

static void playback_stop_locked(const char *why)
{
    int p;
    if (!P.active)
        return;
    for (p = 0; p < BTVS_PORTS; p++)
        free(P.rec[p]);
    memset(&P, 0, sizeof P);
    IN_LOG("playback %s", why);
}

int btvs_input_playback(const char *path)
{
    const char *mode = getenv("BTVS_INPUT_REPLAY_CLOCK");
    int count, p;
    LOCK();
    playback_stop_locked("replaced");
    count = recording_parse(path, P.rec, P.n, P.total, &P.end_frame, &P.end_ms);
    if (count >= 0) {
        P.active = 1;
        P.mode = mode && !_stricmp(mode, "frame") ? PB_FRAME : mode && !_stricmp(mode, "time") ? PB_TIME
               : mode && !_stricmp(mode, "poll") ? PB_POLL : PB_GAME;
        /* The game clock needs both runs caught before their first pad
         * read; anything else (a recording begun or played in mid-game) goes
         * by read count. */
        if (P.mode == PB_GAME && (s_parse_anchor < 0 || s_read1))
            P.mode = PB_POLL;
        P.anchor = s_parse_anchor > 0 ? (uint32_t)s_parse_anchor : 0;
        P.f0 = s_frame;
        P.t0 = now_ms();
        IN_LOG("playback clock: %s", P.mode == PB_POLL ? "pad reads" : P.mode == PB_FRAME ? "frames" : P.mode == PB_TIME ? "time"
               : "time from the game's first pad read");
        for (p = 1; p < BTVS_PORTS; p++)
            if (P.total[p] > P.total[P.main_port])
                P.main_port = p;
        IN_LOG("playing %s: %d records, %u reads, %u frames, %.1f s", path, count, P.total[0], P.end_frame, P.end_ms / 1000.0);
    } else {
        IN_LOG("cannot play %s", path);
    }
    UNLOCK();
    return count >= 0;
}

void btvs_input_playback_stop(void)
{
    LOCK();
    playback_stop_locked("stopped");
    UNLOCK();
}

int btvs_input_playback_active(void)
{
    return P.active;
}

/* The recorded pad for this read. Returns -1 when the recording is over. */
static int playback_poll(int port, btvs_pad *g)
{
    uint32_t key;
    int over;
    P.polls[port]++;
    if (P.mode == PB_POLL) {
        key = P.polls[port];
        over = P.polls[P.main_port] > P.total[P.main_port];
    } else if (P.mode == PB_FRAME) {
        key = s_frame - P.f0;
        over = key > P.end_frame;
    } else if (P.mode == PB_TIME) {
        key = (uint32_t)(now_ms() - P.t0);
        over = key > P.end_ms;
    } else {
        /* Both runs count from their own first pad read, so how long the
         * level took to load does not matter, and the recording keeps up
         * with the clock when the game stops reading the pad for a while. */
        double t = now_ms();
        key = P.anchor + (uint32_t)(t > s_read1_ms ? t - s_read1_ms : 0.0);
        over = key > P.end_ms;
    }
    if (over) {
        playback_stop_locked("finished");
        return -1;
    }
    while (P.idx[port] < P.n[port]) {
        const pb_rec *r = &P.rec[port][P.idx[port]];
        if ((P.mode == PB_POLL ? r->poll : P.mode == PB_FRAME ? r->frame : r->ms) > key)
            break;
        P.idx[port]++;
    }
    if (!P.idx[port])
        return 0;
    *g = P.rec[port][P.idx[port] - 1].pad;
    return P.rec[port][P.idx[port] - 1].conn;
}

int btvs_input_dump_recording(const char *path, FILE *out)
{
    pb_rec *rec[BTVS_PORTS];
    int n[BTVS_PORTS], idx[BTVS_PORTS] = { 0 }, count, i;
    uint32_t total[BTVS_PORTS], end_frame, end_ms;
    count = recording_parse(path, rec, n, total, &end_frame, &end_ms);
    if (count < 0)
        return -1;
    for (i = 0; i < count; i++) {               /* in time order across the ports */
        int p, best = -1;
        const pb_rec *r;
        for (p = 0; p < BTVS_PORTS; p++)
            if (idx[p] < n[p] && (best < 0 || rec[p][idx[p]].ms < rec[best][idx[best]].ms))
                best = p;
        r = &rec[best][idx[best]++];
        fprintf(out, "%8.3fs f%-6u p%d read %-6u %s btn %04X  A%3u B%3u X%3u Y%3u Bk%3u Wh%3u LT%3u RT%3u  L %6d %6d  R %6d %6d\n",
                r->ms / 1000.0, r->frame, best + 1, r->poll, r->conn ? "pad" : "---", r->pad.buttons,
                r->pad.analog[0], r->pad.analog[1], r->pad.analog[2], r->pad.analog[3], r->pad.analog[4],
                r->pad.analog[5], r->pad.analog[6], r->pad.analog[7], r->pad.lx, r->pad.ly, r->pad.rx, r->pad.ry);
    }
    if (s_parse_anchor >= 0)
        fprintf(out, "first pad read of the game: %.3f s\n", s_parse_anchor / 1000.0);
    fprintf(out, "end: %.3f s, %u frames, reads %u %u %u %u\n", end_ms / 1000.0, end_frame,
            total[0], total[1], total[2], total[3]);
    for (i = 0; i < BTVS_PORTS; i++)
        free(rec[i]);
    return count;
}

/* ── rebinding ───────────────────────────────────────────────────────────── */

static struct { int action, slot, armed, result; double t0; } K;

static int kb_any_down(void)
{
    return s_test_pad ? 0 : in_key_any_down();
}

static int any_pad_source(void)
{
    int i, s;
    for (i = 0; i < MAX_DEVS; i++)
        if (s_dev[i].kind)
            for (s = PS_BTN; s < PS_COUNT; s++)
                if (source_value(&s_dev[i].pad, s) > 0.6f)
                    return s;
    return 0;
}

static void capture_step(void)
{
    int code;
    if (!s_capture_active)
        return;
    if (now_ms() - K.t0 > 8000.0) {
        s_capture_active = 0;
        K.result = -1;
        return;
    }
    if (!focused())
        return;
    devices_refresh();
    if (K.slot < 2) {
        code = kb_any_down();
        if (!code && s_wheel_pending) {
            code = s_wheel_pending > 0 ? IN_KEY_WHEEL_UP : IN_KEY_WHEEL_DOWN;
            s_wheel_pending = 0;
        }
    } else {
        code = (!s_test_pad && in_key_down(VK_ESCAPE)) ? -1 : any_pad_source();
    }
    if (!K.armed) {                             /* first let go of whatever opened this */
        K.armed = !code;
        return;
    }
    if (!code)
        return;
    s_capture_active = 0;
    s_capture_release = 1;
    if (code == -1 || (K.slot < 2 && code == VK_ESCAPE)) {
        K.result = -1;
        return;
    }
    if (K.slot < 2) {
        if (C.key[K.action][1 - K.slot] == code)
            C.key[K.action][1 - K.slot] = 0;
        C.key[K.action][K.slot] = code;
        if (!C.key[K.action][0]) {              /* keep the first slot the filled one */
            C.key[K.action][0] = C.key[K.action][1];
            C.key[K.action][1] = 0;
        }
    } else {
        C.pad[K.action] = code;
    }
    save_binding(K.action, K.slot);
    btvs_settings_save();
    K.result = 1;
    IN_LOG("bound %s", k_act[K.action].name);
}

int btvs_input_action_count(void) { return ACT_COUNT; }

const char *btvs_input_action_name(int a)
{
    return a >= 0 && a < ACT_COUNT ? k_act[a].name : "";
}

const char *btvs_input_action_label(int a)
{
    return a >= 0 && a < ACT_COUNT ? k_act[a].label : "";
}

const char *btvs_input_binding_text(int a, int slot, char *buf, int size)
{
    if (size > 0)
        buf[0] = 0;
    if (a < 0 || a >= ACT_COUNT || slot < 0 || slot >= BTVS_BIND_SLOTS || size <= 0)
        return buf;
    LOCK();
    load_settings();
    if (slot < 2) {
        if (C.key[a][slot])
            strncpy_s(buf, (size_t)size, in_key_name(C.key[a][slot]), _TRUNCATE);
    } else if (C.pad[a] != PS_NONE) {
        sprintf_s(buf, (size_t)size, "Pad %s", k_ps_names[C.pad[a]]);
    }
    UNLOCK();
    return buf;
}

void btvs_input_capture_begin(int a, int slot)
{
    if (a < 0 || a >= ACT_COUNT || slot < 0 || slot >= BTVS_BIND_SLOTS)
        return;
    LOCK();
    load_settings();
    K.action = a; K.slot = slot; K.armed = 0; K.result = 0;
    K.t0 = now_ms();
    s_wheel_pending = 0;
    s_capture_active = 1;
    UNLOCK();
}

int btvs_input_capture_state(void)
{
    int r;
    LOCK();
    capture_step();
    r = s_capture_active ? 0 : K.result;
    UNLOCK();
    return r;
}

void btvs_input_capture_cancel(void)
{
    LOCK();
    if (s_capture_active) {
        s_capture_active = 0;
        K.result = -1;
    }
    UNLOCK();
}

void btvs_input_binding_clear(int a, int slot)
{
    if (a < 0 || a >= ACT_COUNT || slot < 0 || slot >= BTVS_BIND_SLOTS)
        return;
    LOCK();
    load_settings();
    if (slot < 2) {
        C.key[a][slot] = 0;
        if (!C.key[a][0]) { C.key[a][0] = C.key[a][1]; C.key[a][1] = 0; }
    } else {
        C.pad[a] = PS_NONE;
    }
    save_binding(a, slot);
    UNLOCK();
    btvs_settings_save();
}

void btvs_input_bindings_reset(void)
{
    int a;
    LOCK();
    load_settings();
    for (a = 0; a < ACT_COUNT; a++) {
        parse_keys(k_act[a].keys, C.key[a]);
        C.pad[a] = k_act[a].pad;
        save_binding(a, 0);
        save_binding(a, 2);
    }
    UNLOCK();
    btvs_settings_save();
    IN_LOG("bindings reset to defaults");
}

void btvs_input_settings_reload(void)
{
    InterlockedExchange(&C.loaded, 0);
}

const char *btvs_input_device_name(int port)
{
    static char names[BTVS_PORTS][64];
    host_dev *d;
    if (port < 0 || port >= BTVS_PORTS)
        return "";
    LOCK();
    d = dev_on_port(port);
    strncpy_s(names[port], sizeof names[port], d ? d->name : "", _TRUNCATE);
    UNLOCK();
    return names[port];
}

/* ── the one function ────────────────────────────────────────────────────── */

static btvs_pad s_last[BTVS_PORTS];
static int s_last_conn[BTVS_PORTS];
static int s_was_focused;

static void log_change(int port, int conn, const btvs_pad *g)
{
    static int on = -1;
    if (on < 0)
        on = env_on("BTVS_INPUT_LOG");
    if (on && (conn != s_last_conn[port] || memcmp(g, &s_last[port], sizeof *g)))
        IN_LOG("t %.3f f%u p%d %s btn %04X an %u %u %u %u %u %u %u %u L %d %d R %d %d", now_ms() / 1000.0, s_frame, port + 1,
               conn ? "pad" : "---", g->buttons, g->analog[0], g->analog[1], g->analog[2], g->analog[3], g->analog[4],
               g->analog[5], g->analog[6], g->analog[7], g->lx, g->ly, g->rx, g->ry);
}

static const char *run_env(const char *name)
{
    const char *r = getenv("BTVS_RESTARTS");
    char full[64];
    if (!r || !*r || !strcmp(r, "0"))
        return getenv(name);
    sprintf_s(full, sizeof full, "%s_%s", name, r);
    return getenv(full);
}

static void env_start(void)
{
    static int done;
    const char *e;
    if (done)
        return;
    done = 1;
    /* Like pad scripts, these are for one run of the game: the game restarts
     * itself for a level, and that run (BTVS_RESTARTS = n) reads
     * BTVS_INPUT_PLAYBACK_<n> / BTVS_INPUT_RECORD_<n>. */
    if ((e = run_env("BTVS_INPUT_PLAYBACK")) != NULL && *e)
        btvs_input_playback(e);
    if ((e = run_env("BTVS_INPUT_RECORD")) != NULL && *e)
        btvs_input_record_begin(e);
}

int btvs_input_poll(int port, btvs_pad *g)
{
    int conn, f;
    memset(g, 0, sizeof *g);
    if (port < 0 || port >= BTVS_PORTS)
        return 0;
    LOCK();
    env_start();
    if (!s_read1) {
        s_read1 = 1;
        s_read1_ms = now_ms();
    }
    load_settings();
    if (port == 0 && !s_frame_external)
        frame_count();                          /* nobody counts frames: one read of port 1 is one */

    conn = -2;
    if (P.active)
        conn = playback_poll(port, g);
    if (conn < 0) {                             /* no recording playing (or it just ended) */
        memset(g, 0, sizeof *g);
        f = focused();
        script_keys();
        if (!f && (s_script_nkey || s_script_mouse))
            live_pad(port, g, 0);
        if (f) {
            capture_step();
            live_pad(port, g, 1);
            /* while a binding is being chosen, and until that key or button
             * is let go, the game sees a resting pad */
            if (s_capture_release && !kb_any_down() && !any_pad_source())
                s_capture_release = 0;
            if (s_capture_active || s_capture_release)
                memset(g, 0, sizeof *g);
        } else if (s_was_focused) {
            in_mouse_capture(0);                /* the player went elsewhere: give the mouse back */
        }
        if (f != s_was_focused || f)
            rumble_apply(f);
        s_was_focused = f;
        {
            btvs_pad before = *g;
            script_apply(port, g);
            if (port == 0) {
                if (!U.use)
                    use_start();
                use_look_script(&before, g);
            }
        }
        conn = port == 0 || (f && dev_on_port(port)) || (s_script_ports >> port & 1) || (port == C.kb_port && (s_script_nkey || s_script_mouse));
        if (!conn)
            memset(g, 0, sizeof *g);
    }
    record_poll(port, conn, g);
    log_change(port, conn, g);
    s_last[port] = *g;
    s_last_conn[port] = conn;
    UNLOCK();
    return conn;
}

uint32_t btvs_input_connected(void)
{
    uint32_t m = 1;
    int p;
    LOCK();
    load_settings();
    if (P.active) {
        for (p = 1; p < BTVS_PORTS; p++)
            if (P.idx[p] ? P.rec[p][P.idx[p] - 1].conn : (P.n[p] && P.rec[p][0].conn && P.rec[p][0].poll <= 1))
                m |= 1u << p;
    } else {
        if (focused()) {
            devices_refresh();
            for (p = 1; p < BTVS_PORTS; p++)
                if (dev_on_port(p))
                    m |= 1u << p;
        }
        script_env_load();                      /* so the script's ports count from the start */
        m |= s_script_ports;
    }
    UNLOCK();
    return m;
}

void btvs_input_last(int port, btvs_pad *g)
{
    memset(g, 0, sizeof *g);
    if (port < 0 || port >= BTVS_PORTS)
        return;
    LOCK();
    *g = s_last[port];
    UNLOCK();
}

void btvs_input_rumble(int port, uint16_t left, uint16_t right)
{
    if (port < 0 || port >= BTVS_PORTS)
        return;
    LOCK();
    load_settings();
    s_rumble[port][0] = left;
    s_rumble[port][1] = right;
    if (s_was_focused)
        rumble_apply(focused());
    UNLOCK();
}

void btvs_input_wheel(int notches)
{
    LOCK();
    s_wheel_pending += notches;
    UNLOCK();
}

int btvs_input_mouse_captured(void)
{
    return in_mouse_is_captured();
}

void btvs_input_allow_mouse_look(int allow)
{
    s_allow_mouse_look = allow != 0;
}

static void at_exit(void)
{
    EnterCriticalSection(&s_cs);
    rumble_apply(0);
    in_mouse_capture(0);
    record_end_locked();
    LeaveCriticalSection(&s_cs);
    in_sdl_stop();
}

/* ── tests ───────────────────────────────────────────────────────────────── */

void btvs_input__test_hooks(double (*clock_ms)(void), int (*focus)(void))
{
    s_clock_fn = clock_ms;
    s_focus_fn = focus;
}

void btvs_input__test_host_pad(const in_host_pad *pad)
{
    LOCK();
    if (pad) {
        s_test_pad_copy = *pad;
        s_test_pad = &s_test_pad_copy;
    } else {
        s_test_pad = NULL;
    }
    UNLOCK();
}

void btvs_input__test_last_rumble(int *port, int *left, int *right)
{
    *port = s_test_rumble[0]; *left = s_test_rumble[1]; *right = s_test_rumble[2];
}

void btvs_input__test_reset(void)
{
    LOCK();
    record_end_locked();
    playback_stop_locked("reset");
    s_nev = 0; s_nseg = 0; s_script_ports = 0; s_script_env_done = 1;
    memset(s_seg, 0, sizeof s_seg);
    memset(s_dev, 0, sizeof s_dev);
    memset(s_rumble, 0, sizeof s_rumble);
    memset(s_last, 0, sizeof s_last);
    memset(&K, 0, sizeof K);
    s_capture_active = 0;
    s_menu_mode = 0;
    memset(s_mk_blocked, 0, sizeof s_mk_blocked);
    memset(&U, 0, sizeof U);
    s_pad_new = 0; s_key_sig = 0;
    s_frame = 0; s_frame_external = 0; s_read1 = 0;
    s_test_pad = NULL;
    InterlockedExchange(&C.loaded, 0);
    UNLOCK();
}
