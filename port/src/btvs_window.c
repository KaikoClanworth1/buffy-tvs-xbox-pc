/**
 * The window and the display settings.
 *
 * The window itself, its three modes (windowed, borderless, fullscreen with
 * the monitor's mode switched), Alt+Enter / F11 and the swap chain live in
 * the runtime's GPU code (xboxrecomp/src/kernel/nv2a_pb_d3d11.inc); this file
 * tells it what the player chose -- the [display] settings (btvs_settings.h)
 * -- and applies changes while the game runs.
 *
 *   mode        0 windowed, 1 borderless, 2 fullscreen
 *   width, height   the size the game is drawn at (0: the desktop's)
 *   vsync       0/1
 *   widescreen  0 the original 4:3 picture (bars at the sides), 1 16:9, 2 stretched to the window
 *
 * Test runs (BTVS_NO_WINDOW=1): the window is made hidden and is never
 * shown, activated or resized, whatever the settings say; frames are drawn
 * at 640x480 unless BTVS_DISPLAY_WIDTH / _HEIGHT ask for another size.
 */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define COBJMACROS
#include <d3d11.h>
#include <dxgi1_4.h>

#include "gpu.h"
#include "btvs.h"
#include "btvs_native.h"
#include "btvs_log.h"

extern const char *g_nv2a_window_title;
void nv2a_gpu_set_display(int width, int height, int vsync, int fullscreen);
void nv2a_gpu_set_display_callback(void (*cb)(int fullscreen));
void nv2a_gpu_set_frame_widescreen(int wide);
void nv2a_gpu_set_frame_aspect(float aspect);
void *nv2a_gpu_window(void);
void nv2a_gpu_set_close_callback(void (*cb)(void));
void nv2a_gpu_set_char_callback(void (*cb)(unsigned ch));
int  nv2a_gpu_take_wheel(void);
void nv2a_gpu_mouse_capture(int on);
static void on_char(unsigned ch);

/* The log call until the Logs area's is linked (then the linker takes that
 * one: /alternatename only fills in for a missing symbol). */
void btvs_log_fallback(const char *area, const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    fprintf(stderr, "[%s] %s\n", area, line);
}
#pragma comment(linker, "/alternatename:btvs_log=btvs_log_fallback")

static int s_hidden = -1;

int btvs_window_hidden(void)
{
    if (s_hidden < 0) {
        const char *e = getenv("BTVS_NO_WINDOW");
        s_hidden = e && *e && *e != '0';
    }
    return s_hidden;
}

static void env_default(const char *name, const char *value)
{
    if (!getenv(name))
        _putenv_s(name, value);
}

static volatile LONG s_dirty = 1;
static int s_applied_mode = -1;

static void on_display_changed(const char *section, const char *key, void *user)
{
    (void)section; (void)key; (void)user;
    InterlockedExchange(&s_dirty, 1);
}

/* Alt+Enter / F11 changed the mode: remember it. (Window thread.) */
static void on_mode_key(int mode)
{
    s_applied_mode = mode;
    btvs_setting_set_int("display", "mode", mode);
}

static void apply(void)
{
    int mode = btvs_setting_int("display", "mode", 1);
    int w = btvs_setting_int("display", "width", 0), h = btvs_setting_int("display", "height", 0);
    int vsync = btvs_setting_int("display", "vsync", 1);
    int wide = btvs_setting_int("display", "widescreen", 0);
    int dw = GetSystemMetrics(SM_CXSCREEN), dh = GetSystemMetrics(SM_CYSCREEN);
    if (btvs_window_hidden()) {
        if (w <= 0 || h <= 0) { w = 640; h = 480; }
        mode = 0;
        vsync = 0;
    }
    if (w <= 0 || h <= 0) { w = dw; h = dh; }
    if (mode < 0 || mode > 2) mode = 1;
    /* The game draws a 4:3 picture. Shown as 4:3 it is drawn at that shape
     * (the height asked for, 4/3 as wide), so nothing is drawn only to be
     * squeezed; 16:9 and stretch use the whole size. */
    if (wide == 0) {
        int w43 = h * 4 / 3;
        if (w43 < w) w = w43; else h = w * 3 / 4;
        nv2a_gpu_set_frame_aspect(0.0f);
        nv2a_gpu_set_frame_widescreen(0);
    } else if (wide == 1) {
        nv2a_gpu_set_frame_aspect((float)w / (float)h);
    } else {
        nv2a_gpu_set_frame_aspect(-1.0f);
    }
    nv2a_gpu_set_display(w, h, vsync, mode);
    s_applied_mode = mode;
    btvs_log("gfx", "display: %dx%d, %s, vsync %s, picture %s", w, h,
             mode == 0 ? "windowed" : mode == 1 ? "borderless" : "fullscreen", vsync ? "on" : "off",
             wide == 0 ? "4:3" : wide == 1 ? "widescreen" : "stretched");
}

void btvs_window_boot(void)
{
    g_nv2a_window_title = "Buffy the Vampire Slayer";
    if (btvs_window_hidden())
        _putenv_s("BUFFY_NO_WINDOW", "1");       /* the runtime's name for it */
    /* No Xbox GPU is emulated: no pushbuffer thread, no vertical-blank
     * interrupt, no register thread (btvs_native_gpuwait.c), and Direct3D 11
     * for the GPU layer. */
    env_default("RECOMP_PB_EXEC", "0");
    env_default("RECOMP_VBLANK", "0");
    env_default("RECOMP_NV2A_ACK", "0");
    env_default("BUFFY_GPU_BACKEND", "d3d11");
    {   /* The console's hard disk (the game's TDATA settings and UDATA saved
         * games) is a folder in the save folder. It used to fall into the game
         * folder: test runs filled game_files\UDATA with saved games until the
         * game refused to start a new one (it allows ten). */
        extern void xbox_path_set_hdd_dir(const char *dir);
        extern void xbox_path_keep_disk_images(int on);
        char hdd[MAX_PATH];
        if (btvs_save_dir()[0] && sprintf_s(hdd, sizeof hdd, "%s\\HardDisk", btvs_save_dir()) > 0)
            xbox_path_set_hdd_dir(hdd);
        xbox_path_keep_disk_images(1);           /* (XAPI reads the disk's configuration sectors at start) */
    }
    {   /* BTVS_NO_DISC_INSTALL=1: no install, Z: read from the game folder (btvs_native_game.c: TLogo::ExecuteMovies) */
        extern void xbox_path_set_cache_from_disc(int on);
        const char *e = getenv("BTVS_NO_DISC_INSTALL");
        xbox_path_set_cache_from_disc(e && e[0] == '1');
    }
    {   /* Which CPUs the game's own threads run on. All of the process's by
         * default. The game's lock (CLocker: a counter looked at and changed
         * in separate steps) only worked on the console's one CPU -- the
         * loader thread and the game both took it and both waited for ever --
         * and for a while all the game's threads were kept on one CPU for
         * that. The lock is now taken in one step (btvs_boot_hooks.c), and
         * one CPU cost a lot: a level loaded in 46 s instead of 9, and the
         * movie's decoder shared the CPU with the game. BTVS_GAME_CPUS=1 puts
         * them back on one CPU (a different one per process, so two games do
         * not pick the same); BTVS_GAME_CPUS=<hex mask> names the CPUs. */
        extern void xbox_set_guest_thread_affinity(uintptr_t mask);
        const char *e = getenv("BTVS_GAME_CPUS");
        DWORD_PTR proc = 0, sys = 0, mask = 0;
        GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys);
        if (e && !strcmp(e, "1")) {
            int n = 0, k, pick;
            for (k = 0; k < 64; k++)
                n += (int)((proc >> k) & 1);
            pick = n ? (int)(GetCurrentProcessId() / 4 % (DWORD)n) : 0;
            for (k = 0; k < 64; k++)
                if (((proc >> k) & 1) && pick-- == 0)
                    mask = (DWORD_PTR)1 << k;
        } else if (e && *e && _stricmp(e, "all"))
            mask = (DWORD_PTR)strtoull(e, NULL, 16) & proc;
        xbox_set_guest_thread_affinity(mask);
        btvs_log("boot", "the game's threads run on CPU mask %llX (0: all of the process's, %llX)",
                 (unsigned long long)mask, (unsigned long long)proc);
    }
    apply();
    InterlockedExchange(&s_dirty, 0);
    btvs_setting_on_change("display", NULL, on_display_changed, NULL);
    nv2a_gpu_set_display_callback(on_mode_key);
    nv2a_gpu_set_close_callback(btvs_quit);
    nv2a_gpu_set_char_callback(on_char);
}

/* ── leaving ───────────────────────────────────────────────────────────── */

#define QUIT_HOOKS 8
static void (*s_quit_hook[QUIT_HOOKS])(void);
static volatile LONG s_quit_hooks, s_quitting;

void btvs_on_quit(void (*fn)(void))
{
    LONG i = InterlockedIncrement(&s_quit_hooks) - 1;
    if (fn && i < QUIT_HOOKS)
        s_quit_hook[i] = fn;
}

/* The one way out for the player: the menu's Exit, the window's close
 * button, Alt+F4. Settings are saved, every area that asked is told (newest
 * first), then the process ends. Any thread; a second call while the first
 * is at work returns at once. */
void btvs_quit(void)
{
    LONG i;
    if (InterlockedExchange(&s_quitting, 1))
        return;
    btvs_log("boot", "quit");
    btvs_settings_save();
    for (i = (s_quit_hooks < QUIT_HOOKS ? s_quit_hooks : QUIT_HOOKS) - 1; i >= 0; i--)
        if (s_quit_hook[i])
            s_quit_hook[i]();
    ExitProcess(0);
}

static void on_char(unsigned ch)
{
    btvs_menu_char(ch);                          /* (Menu: text typed on a PC page) */
}

/* Once a frame (game thread): settings changed in the menu take effect, and
 * the input layer gets what the window collected. */
void btvs_window_frame(void)
{
    static int wheel;
    if (InterlockedExchange(&s_dirty, 0)) {
        if (btvs_setting_int("display", "mode", 1) == s_applied_mode && s_applied_mode >= 0) {
            /* (the mode key's own write-back: nothing else to do) */
        }
        apply();
    }
    wheel += nv2a_gpu_take_wheel();              /* 120 a notch */
    if (wheel >= 120 || wheel <= -120) {
        btvs_input_wheel(wheel / 120);
        wheel %= 120;
    }
    if (!btvs_window_hidden())
        nv2a_gpu_mouse_capture(btvs_input_mouse_captured());
    btvs_input_frame();
}

/* ── what the GPU is, for the memory page and reports ─────────────────── */

int btvs_gpu_info(struct btvs_gpu_info *out)
{
    ID3D11Device *dev = (ID3D11Device *)gpu_d3d11_device();
    IDXGIDevice *xd = NULL;
    IDXGIAdapter *ad = NULL;
    IDXGIAdapter3 *ad3 = NULL;
    DXGI_ADAPTER_DESC desc;
    const char *name = gpu_adapter_name();
    uint32_t w = 0, h = 0;
    memset(out, 0, sizeof *out);
    strncpy_s(out->adapter, sizeof out->adapter, name ? name : "", _TRUNCATE);
    strcpy_s(out->api, sizeof out->api, "Direct3D 11");
    btvs_native_frame_size(&w, &h);
    out->render_width = (int)w;
    out->render_height = (int)h;
    if (!dev || FAILED(ID3D11Device_QueryInterface(dev, &IID_IDXGIDevice, (void **)&xd)))
        return 0;
    if (SUCCEEDED(IDXGIDevice_GetAdapter(xd, &ad))) {
        if (SUCCEEDED(IDXGIAdapter_GetDesc(ad, &desc))) {
            out->vram_total = (uint64_t)desc.DedicatedVideoMemory;
            out->shared_total = (uint64_t)desc.SharedSystemMemory;
        }
        if (SUCCEEDED(IDXGIAdapter_QueryInterface(ad, &IID_IDXGIAdapter3, (void **)&ad3))) {
            DXGI_QUERY_VIDEO_MEMORY_INFO mi;
            if (SUCCEEDED(IDXGIAdapter3_QueryVideoMemoryInfo(ad3, 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &mi))) {
                out->vram_budget = mi.Budget;
                out->vram_used_by_game = mi.CurrentUsage;
            }
            if (SUCCEEDED(IDXGIAdapter3_QueryVideoMemoryInfo(ad3, 0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &mi)))
                out->shared_used_by_game = mi.CurrentUsage;
            IDXGIAdapter3_Release(ad3);
        }
        IDXGIAdapter_Release(ad);
    }
    IDXGIDevice_Release(xd);
    return 1;
}
