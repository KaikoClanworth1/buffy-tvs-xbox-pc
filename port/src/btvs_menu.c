/* btvs_menu - the PC entries in the game's own menus (Menu area).
 *
 * The game's front end (docs/menu.md) is GUI::TManager showing one GUI::TMenu
 * at a time; a menu is a list of controls read from interfc\buffy.mbf, and
 * its lines are GUI::TTextButton controls. Nothing here draws: every PC line
 * is a real TTextButton made by the game's own constructor, with the fonts of
 * the lines beside it, added to the menu's own control list. The game then
 * draws it, highlights it, moves the selection over it and plays its sounds
 * exactly as for its own lines.
 *
 *   Main menu     Exit
 *   Options       Graphics, PC Audio, PC Controls, Memory, Report a Problem
 *   Pause menu    Report a Problem (opens Options on that page)
 *
 * A PC page is the Options menu itself with its lines swapped: the game's
 * lines are hidden (state 3) and a pool of our lines is shown, relabelled for
 * the page. Left / Right change a value, A steps it forward (or presses the
 * line), B goes back to the Options list.
 *
 * The menu classes are hooked where the game calls them through their
 * vtables: recomp_lookup_manual() asks btvs_menu_lookup() first, so no
 * regeneration of the translated code is needed.
 *
 * Pages only write settings (btvs_settings.h); the owning area applies them
 * from its change callback.
 */
#include <windows.h>
#include <psapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "recomp_types.h"
#include "btvs.h"

/* ── other areas (each falls back to a stand-in here until its owner's
 *    definition is linked; the prototypes are the owners') ─────────────── */

void btvs_log(const char *area, const char *fmt, ...);
void btvs_trail(const char *event, const char *fmt, ...);
void btvs_report_prepare(void);
int  btvs_report_create(const char *player_text);
const char *btvs_report_last_path(void);
int  btvs_input_action_count(void);
const char *btvs_input_action_label(int action);
const char *btvs_input_binding_text(int action, int slot, char *buf, int size);
void btvs_input_capture_begin(int action, int slot);
int  btvs_input_capture_state(void);
void btvs_input_capture_cancel(void);
void btvs_input_binding_clear(int action, int slot);
void btvs_input_bindings_reset(void);

void btvs_menu_no_log(const char *area, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "[%s] ", area);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
}
void btvs_menu_no_trail(const char *event, const char *fmt, ...) { (void)event; (void)fmt; }
void btvs_menu_no_report_prepare(void) {}
int  btvs_menu_no_report_create(const char *t) { (void)t; return 0; }
const char *btvs_menu_no_report_last_path(void) { return ""; }
int  btvs_menu_no_action_count(void) { return 0; }
const char *btvs_menu_no_action_label(int a) { (void)a; return ""; }
const char *btvs_menu_no_binding_text(int a, int s, char *buf, int size)
{ (void)a; (void)s; if (size > 0) buf[0] = '\0'; return buf; }
void btvs_menu_no_capture_begin(int a, int s) { (void)a; (void)s; }
int  btvs_menu_no_capture_state(void) { return -1; }
void btvs_menu_no_void(void) {}
void btvs_menu_no_binding_clear(int a, int s) { (void)a; (void)s; }
int  btvs_menu_no_gpu_info(struct btvs_gpu_info *o) { (void)o; return 0; }
void btvs_menu_no_quit(void) { btvs_settings_save(); exit(0); }

#pragma comment(linker, "/alternatename:btvs_log=btvs_menu_no_log")
#pragma comment(linker, "/alternatename:btvs_trail=btvs_menu_no_trail")
#pragma comment(linker, "/alternatename:btvs_report_prepare=btvs_menu_no_report_prepare")
#pragma comment(linker, "/alternatename:btvs_report_create=btvs_menu_no_report_create")
#pragma comment(linker, "/alternatename:btvs_report_last_path=btvs_menu_no_report_last_path")
#pragma comment(linker, "/alternatename:btvs_input_action_count=btvs_menu_no_action_count")
#pragma comment(linker, "/alternatename:btvs_input_action_label=btvs_menu_no_action_label")
#pragma comment(linker, "/alternatename:btvs_input_binding_text=btvs_menu_no_binding_text")
#pragma comment(linker, "/alternatename:btvs_input_capture_begin=btvs_menu_no_capture_begin")
#pragma comment(linker, "/alternatename:btvs_input_capture_state=btvs_menu_no_capture_state")
#pragma comment(linker, "/alternatename:btvs_input_capture_cancel=btvs_menu_no_void")
#pragma comment(linker, "/alternatename:btvs_input_binding_clear=btvs_menu_no_binding_clear")
#pragma comment(linker, "/alternatename:btvs_input_bindings_reset=btvs_menu_no_void")
#pragma comment(linker, "/alternatename:btvs_gpu_info=btvs_menu_no_gpu_info")
#pragma comment(linker, "/alternatename:btvs_quit=btvs_menu_no_quit")

/* ── the game (guest addresses, XDK 4432 retail; names from final.map) ──── */

void GUI_TMenuOptions_OnLoad_00223230(void);
void GUI_TMenuOptions_OnMessage_002232E0(void);
void GUI_TMenuOptions_OnCommand_002233F0(void);
void GUI_TMenuMain_OnLoad_00222470(void);
void GUI_TMenuMain_OnMessage_002226B0(void);
void GUI_TMenuPause_OnLoad_00223760(void);
void GUI_TMenuPause_OnMessage_00223A60(void);
void GUI_TBuffyMenu_OnUpdate_00219480(void);
void GUI_TMenuNameSession_OnUpdate_00222F00(void);
void GUI_TBuffyMenu_OnBuffyCommand_00218C20(void);
void GUI_TCanvas_GotoControl_001EA460(void);
void GUI_TTextButton_SetText_001EE0A0(void);          /* SetText(char *) */
void GUI_UTControl_TLinkList_Add_001EB360(void);
void DefaultTTextButtonConstructor_001EE2A0(void);    /* (TControl **out, TControl *parent, ulong flags) */
void Z_YAPAXI_op2_002BC1A0(void);                     /* operator new(size) */

#define VA_OPTIONS_ONLOAD     0x00223230u
#define VA_OPTIONS_ONMESSAGE  0x002232E0u
#define VA_OPTIONS_ONCOMMAND  0x002233F0u
#define VA_MAIN_ONLOAD        0x00222470u
#define VA_MAIN_ONMESSAGE     0x002226B0u
#define VA_PAUSE_ONLOAD       0x00223760u
#define VA_PAUSE_ONMESSAGE    0x00223A60u
#define VA_BUFFYMENU_ONUPDATE 0x00219480u
#define VA_NAMESESSION_ONUPDATE 0x00222F00u

#define G_MANAGER        0x007E6BD0u    /* GUI::g_lpManager */
#define G_FADE_MODE      0x007F041Cu    /* TBuffyMenu::s_fadeMode: commands are ignored while it is set */
#define VT_TEXTBUTTON    0x00663C74u    /* GUI::TTextButton vtable */

/* TControl */
#define C_PARENT   0x04
#define C_X        0x08
#define C_Y        0x0C
#define C_W        0x10
#define C_H        0x14
#define C_STATE    0x18     /* 0 normal, 1 selected, 2 disabled, 3 hidden */
#define C_ID       0x1C
#define C_FLAGS    0x20     /* 4: the selection skips it */
/* TTextButton */
#define B_HALIGN   0x28
#define B_TEXT     0x34
#define B_FONT     0x38     /* the font drawn with: B_FONT_OFF or B_FONT_ON */
#define B_FONT_OFF 0x3C
#define B_FONT_ON  0x40
/* TMenu (TCanvas) */
#define M_LIST     0x2C     /* TLinkList<TControl>: +4 head; node: +0 control, +4 next */
#define M_HEAD     0x30
#define M_SELECTED 0x3C     /* node */
#define M_LOADED   0x48
/* vtable slots */
#define V_MENU_SOUNDS   0x98    /* TBuffyMenu::OnCommandSounds(COMMAND) */
#define V_MENU_FOCUSSND 0x9C    /* TBuffyMenu::PlayFocusedSound() */
#define V_MGR_SETMENU   0x38    /* TManager::SetMenu(id, bool rememberAsBack) */

enum { CMD_LEFT = 0x23, CMD_RIGHT = 0x24, CMD_UP = 0x25, CMD_DOWN = 0x26,
       CMD_SELECT = 0x27, CMD_DELETE = 0x28, CMD_BACK = 0x29 };
enum { MSG_FOCUS = 2, MSG_PRESSED = 5 };
enum { MENU_MAIN = 1, MENU_OPTIONS = 4, MENU_PAUSE = 9 };

/* the game's own control ids we copy the look of */
#define ID_MAIN_STARTGAME      1
#define ID_OPTIONS_CONTROLLER  41
#define ID_PAUSE_RESUME        85

/* our control ids (the game's stop at 215) */
#define ID_EXIT         0x7001
#define ID_PAUSE_REPORT 0x7002
#define ID_ENTRY0       0x7010      /* Options list: + page */
#define ID_HEADING      0x70FF
#define ID_ROW0         0x7100
#define NROWS           9

/* ── calling the game ────────────────────────────────────────────────── */

static uint32_t gcall(recomp_func_t f, uint32_t self, int n, ...)
{
    uint32_t args[8], esp0 = g_esp;
    va_list ap;
    int i;
    if (!f) return 0;
    va_start(ap, n);
    for (i = 0; i < n && i < 8; i++) args[i] = va_arg(ap, uint32_t);
    va_end(ap);
    for (i = n - 1; i >= 0; i--) { g_esp -= 4; MEM32(g_esp) = args[i]; }
    g_esp -= 4; MEM32(g_esp) = 0;       /* return address */
    g_ecx = self;
    f();
    g_esp = esp0;                       /* cdecl or callee-cleaned alike */
    return g_eax;
}

static recomp_func_t resolve(uint32_t va)
{
    recomp_func_t f = recomp_lookup_manual(va);
    if (!f) f = recomp_lookup(va);
    return f;
}
#define VFUNC(obj, slot) resolve(MEM32(MEM32(obj) + (slot)))

static uint32_t galloc(uint32_t size)
{
    return gcall(Z_YAPAXI_op2_002BC1A0, 0, 1, size);
}

/* ── state ───────────────────────────────────────────────────────────── */

enum { PG_NONE, PG_GRAPHICS, PG_AUDIO, PG_CONTROLS, PG_MEMORY, PG_REPORT,
       PG_BINDINGS, PG_REPORT_DONE, PG_COUNT };

static const char *const k_page_name[PG_COUNT] = {
    "options", "pc_graphics", "pc_audio", "pc_controls", "pc_memory", "pc_report",
    "pc_bindings", "pc_report_done" };
static const char *const k_page_title[PG_COUNT] = {
    "", "GRAPHICS", "PC AUDIO", "PC CONTROLS", "MEMORY", "REPORT A PROBLEM",
    "KEY BINDINGS", "REPORT A PROBLEM" };

typedef struct { char text[128]; int item; } row_t;    /* item < 0: not selectable */

static uint32_t s_options;          /* the TMenuOptions the pool lives in */
static int      s_page;
static int      s_pending_page;     /* page to open when Options loads (from the pause menu) */
static row_t    s_row[NROWS];
static int      s_nrows;
static char     s_shown[NROWS + 1][128];    /* what each pool line says now */
static uint32_t s_saved_id[32], s_saved_state[32];
static int      s_nsaved;
static uint32_t s_scratch;          /* guest buffer for strings and out-pointers */
static DWORD    s_next_refresh;

/* bindings page */
static int s_bind_top, s_bind_slot, s_bind_capture = -1;
/* report page */
static int  s_rep_cat, s_rep_edit, s_rep_char;
static char s_rep_text[96];
static const char k_charset[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?-'";
static const char *const k_category[] = {
    "Crash or freeze", "Graphics", "Sound", "Controls", "Stuck in the game", "Something else" };

static void pc_wording(void);

/* ── controls ────────────────────────────────────────────────────────── */

static uint32_t find_control(uint32_t menu, uint32_t id)
{
    uint32_t node = MEM32(menu + M_HEAD);
    int guard = 0;
    for (; node && guard < 512; node = MEM32(node + 4), guard++) {
        uint32_t c = MEM32(node);
        if (c && MEM32(c + C_ID) == id) return c;
    }
    return 0;
}

/* A font the menu file names by number (resource type 2), or 0. */
static uint32_t font_resource(uint32_t handle)
{
    uint32_t mgr = MEM32(G_MANAGER);
    if (!mgr || handle == 0 || handle > MEM32(mgr + 2 * 4 + 0x2C)) return 0;
    return MEM32(MEM32(mgr + 2 * 4 + 0x20) + handle * 4 - 4);
}

static void set_text(uint32_t btn, const char *text)
{
    size_t n = strlen(text);
    if (!s_scratch) s_scratch = galloc(512);
    if (!s_scratch || !btn) return;
    if (n > 255) n = 255;
    memcpy((void *)XBOX_PTR(s_scratch + 16), text, n);
    MEM8(s_scratch + 16 + (uint32_t)n) = 0;
    gcall(GUI_TTextButton_SetText_001EE0A0, btn, 1, s_scratch + 16);
}

/* A new line in `menu`, looking like `like` (one of the menu's own lines). */
static uint32_t add_button(uint32_t menu, uint32_t like, uint32_t id, int x, int y, int w, int h,
                           uint32_t font_off, uint32_t font_on, const char *text)
{
    uint32_t btn;
    if (!s_scratch) s_scratch = galloc(512);
    if (!s_scratch) return 0;
    MEM32(s_scratch) = 0;
    gcall(DefaultTTextButtonConstructor_001EE2A0, 0, 3, s_scratch, menu, 0u);
    btn = MEM32(s_scratch);
    if (!btn) return 0;
    if (!font_off) font_off = like ? MEM32(like + B_FONT_OFF) : 0;
    if (!font_on)  font_on  = like ? MEM32(like + B_FONT_ON) : font_off;
    MEM32(btn + C_ID) = id;
    MEM32(btn + C_X) = (uint32_t)x;  MEM32(btn + C_Y) = (uint32_t)y;
    MEM32(btn + C_W) = (uint32_t)w;  MEM32(btn + C_H) = (uint32_t)h;
    MEM32(btn + B_HALIGN) = 0;
    MEM32(btn + B_FONT_OFF) = font_off;
    MEM32(btn + B_FONT_ON) = font_on;
    MEM32(btn + B_FONT) = font_off;
    set_text(btn, text);
    gcall(GUI_UTControl_TLinkList_Add_001EB360, menu + M_LIST, 1, btn);
    return btn;
}

static void show(uint32_t btn, int visible, int selectable)
{
    if (!btn) return;
    if (!visible) {
        MEM32(btn + C_STATE) = 3;
    } else if (MEM32(btn + C_STATE) == 3) {
        MEM32(btn + C_STATE) = 0;
        MEM32(btn + B_FONT) = MEM32(btn + B_FONT_OFF);
    }
    if (selectable) MEM32(btn + C_FLAGS) &= ~4u;
    else            MEM32(btn + C_FLAGS) |= 4u;
}

static uint32_t selected_id(uint32_t menu)
{
    uint32_t node = MEM32(menu + M_SELECTED);
    uint32_t c = node ? MEM32(node) : 0;
    return c ? MEM32(c + C_ID) : 0;
}

static void go_to(uint32_t menu, uint32_t id)
{
    gcall(GUI_TCanvas_GotoControl_001EA460, menu, 1, id);
}

/* ── settings tables ─────────────────────────────────────────────────── */

static const struct { int w, h; } k_res[] = {
    { 0, 0 }, { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 960 }, { 1600, 1200 },
    { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 } };
#define NRES ((int)(sizeof k_res / sizeof k_res[0]))
static const int k_fps[] = { 30, 60, 90, 120, 144, 165, 240, 0 };
#define NFPS ((int)(sizeof k_fps / sizeof k_fps[0]))
static const char *const k_mode[] = { "Windowed", "Borderless", "Fullscreen" };
static const char *const k_wide[] = { "Original 4:3", "Widescreen", "Stretched" };
static const char *const k_overlay[] = { "Off", "FPS", "Debug" };

static int wrap(int v, int n) { return ((v % n) + n) % n; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int res_index(void)
{
    int w = btvs_setting_int("display", "width", 0), h = btvs_setting_int("display", "height", 0), i;
    for (i = 0; i < NRES; i++)
        if (k_res[i].w == w && k_res[i].h == h) return i;
    return -1;
}

static int fps_index(void)
{
    int f = btvs_setting_int("display", "fps_limit", 60), i;
    for (i = 0; i < NFPS; i++)
        if (k_fps[i] == f) return i;
    return 1;
}

static void cycle(const char *sec, const char *key, int def, int n, int dir)
{
    btvs_setting_set_int(sec, key, wrap(btvs_setting_int(sec, key, def) + dir, n));
}

static void slide(const char *sec, const char *key, int def, int lo, int hi, int step, int dir)
{
    btvs_setting_set_int(sec, key, clampi(btvs_setting_int(sec, key, def) + dir * step, lo, hi));
}

static const char *onoff(const char *sec, const char *key, int def)
{
    return btvs_setting_int(sec, key, def) ? "On" : "Off";
}

/* ── pages ───────────────────────────────────────────────────────────── */

static void rowf(int item, const char *fmt, ...)
{
    va_list ap;
    if (s_nrows >= NROWS) return;
    va_start(ap, fmt);
    vsnprintf(s_row[s_nrows].text, sizeof s_row[0].text, fmt, ap);
    va_end(ap);
    s_row[s_nrows++].item = item;
}

static void mb(char *out, size_t n, uint64_t bytes)
{
    if (bytes >= (1ull << 30)) snprintf(out, n, "%.1f GB", (double)bytes / (double)(1ull << 30));
    else                       snprintf(out, n, "%u MB", (unsigned)(bytes >> 20));
}

static void build_memory(void)
{
    MEMORYSTATUSEX ms;
    PROCESS_MEMORY_COUNTERS_EX pm;
    char a[32], b[32];
    uint64_t ram_total = 0, ram_used = 0, proc = 0;

    memset(&ms, 0, sizeof ms); ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) { ram_total = ms.ullTotalPhys; ram_used = ms.ullTotalPhys - ms.ullAvailPhys; }
    memset(&pm, 0, sizeof pm); pm.cb = sizeof pm;
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pm, sizeof pm))
        proc = pm.WorkingSetSize;
#ifdef BTVS_HAVE_MEM_STATS      /* Logs: struct btvs_mem / btvs_mem_stats() in btvs.h */
    {
        struct btvs_mem m;
        memset(&m, 0, sizeof m);
        btvs_mem_stats(&m);
        if (m.ram_total) { ram_total = m.ram_total; ram_used = m.ram_total - m.ram_available; }
        if (m.proc_working_set) proc = m.proc_working_set;
        mb(a, sizeof a, ram_used); mb(b, sizeof b, ram_total);
        rowf(-1, "System memory:  %s of %s in use", a, b);
        mb(a, sizeof a, proc);
        rowf(-1, "This game:  %s", a);
        if (m.vram_total) {
            mb(a, sizeof a, m.vram_used_system); mb(b, sizeof b, m.vram_total);
            if (m.vram_used_system) rowf(-1, "Video memory:  %s of %s in use", a, b);
            else                    rowf(-1, "Video memory:  %s", b);
            mb(a, sizeof a, m.vram_used);
            rowf(-1, "This game:  %s", a);
        } else {
            rowf(-1, "Video memory:  not known");
        }
        if (m.game_heap_valid) {
            mb(a, sizeof a, m.game_heap_used); mb(b, sizeof b, m.guest_ram_total);
            rowf(-1, "Game heap:  %s of %s", a, b);
            mb(a, sizeof a, m.game_heap_peak);
            rowf(-1, "Peak %s,  %u blocks", a, (unsigned)m.game_heap_allocs);
        } else {
            rowf(-1, "Game heap:  not known");
        }
    }
#else
    mb(a, sizeof a, ram_used); mb(b, sizeof b, ram_total);
    rowf(-1, "System memory:  %s of %s in use", a, b);
    mb(a, sizeof a, proc);
    rowf(-1, "This game:  %s", a);
    {
        struct btvs_gpu_info g;
        memset(&g, 0, sizeof g);
        btvs_gpu_info(&g);
        if (g.vram_total) {
            mb(a, sizeof a, g.vram_used_by_game); mb(b, sizeof b, g.vram_total);
            rowf(-1, "Video memory:  %s", b);
            rowf(-1, "This game:  %s", a);
        } else {
            rowf(-1, "Video memory:  not known");
        }
    }
    rowf(-1, "Game heap:  not known");
#endif
    rowf(0, "Back");
}

static void build_bindings(void)
{
    int n = btvs_input_action_count(), i, s;
    char t[3][40];
    if (n <= 0) {
        rowf(-1, "No bindings to show");
        rowf(1000, "Back");
        return;
    }
    s_bind_top = clampi(s_bind_top, 0, n > NROWS ? n - NROWS : 0);
    for (i = s_bind_top; i < n && s_nrows < NROWS; i++) {
        if (s_bind_capture == i) {
            rowf(i, "%s:  press a key or button...", btvs_input_action_label(i));
            continue;
        }
        for (s = 0; s < 3; s++) {
            char raw[32];
            btvs_input_binding_text(i, s, raw, sizeof raw);
            if (!raw[0]) strcpy(raw, "-");
            snprintf(t[s], sizeof t[s], s == s_bind_slot ? "[%s]" : "%s", raw);
        }
        rowf(i, "%s:  %s  %s  %s", btvs_input_action_label(i), t[0], t[1], t[2]);
    }
}

static void build_rows(void)
{
    int i;
    s_nrows = 0;
    switch (s_page) {
    case PG_GRAPHICS:
        rowf(0, "Display Mode:  %s", k_mode[wrap(btvs_setting_int("display", "mode", 1), 3)]);
        i = res_index();
        if (i == 0)      rowf(1, "Resolution:  Desktop");
        else             rowf(1, "Resolution:  %d x %d", btvs_setting_int("display", "width", 0),
                              btvs_setting_int("display", "height", 0));
        rowf(2, "VSync:  %s", onoff("display", "vsync", 1));
        i = k_fps[fps_index()];
        if (i) rowf(3, "FPS Limit:  %d", i); else rowf(3, "FPS Limit:  Uncapped");
        rowf(4, "Picture:  %s", k_wide[wrap(btvs_setting_int("display", "widescreen", 0), 3)]);
        rowf(5, "Overlay:  %s", k_overlay[wrap(btvs_setting_int("display", "show_fps", 0), 3)]);
        break;
    case PG_AUDIO:
        rowf(0, "Master Volume:  %d%%", btvs_setting_int("audio", "master", 100));
        rowf(1, "Music:  %d%%", btvs_setting_int("audio", "music", 100));
        rowf(2, "Effects:  %d%%", btvs_setting_int("audio", "effects", 100));
        rowf(3, "Speech:  %d%%", btvs_setting_int("audio", "speech", 100));
        break;
    case PG_CONTROLS:
        rowf(0, "Sensitivity:  %d", btvs_setting_int("controls", "sensitivity", 5));
        rowf(1, "Invert Look:  %s", onoff("controls", "invert_y", 0));
        rowf(2, "Rumble:  %s", onoff("controls", "rumble", 1));
        rowf(3, "Key Bindings");
        rowf(4, "Reset Bindings");
        break;
    case PG_BINDINGS:
        build_bindings();
        break;
    case PG_MEMORY:
        build_memory();
        break;
    case PG_REPORT:
        rowf(0, "Problem:  %s", k_category[wrap(s_rep_cat, 6)]);
        if (s_rep_edit) rowf(1, "Details:  %s[%c]", s_rep_text, k_charset[s_rep_char]);
        else            rowf(1, "Details:  %s", s_rep_text[0] ? s_rep_text : "(none)");
        rowf(2, "Send Report");
        rowf(-1, s_rep_edit ? "Left/Right letter, A add, X delete, B done"
                            : "A on Details to write, or type on the keyboard");
        break;
    case PG_REPORT_DONE: {
        const char *p = btvs_report_last_path();
        size_t n = p ? strlen(p) : 0, o;
        if (n) {
            rowf(-1, "Report saved in:");
            for (o = 0; o < n && s_nrows < 5; o += 46) rowf(-1, "%.46s", p + o);
            rowf(-1, "Nothing was sent anywhere.");
        } else {
            rowf(-1, "The report could not be saved.");
        }
        rowf(0, "Back");
        break;
    }
    default:
        break;
    }
}

/* Put the page's lines on screen (only the ones whose text changed). */
static void refresh(void)
{
    uint32_t menu = s_options, b;
    int i;
    if (!menu || s_page == PG_NONE) return;
    build_rows();
    b = find_control(menu, ID_HEADING);
    if (b && strcmp(s_shown[NROWS], k_page_title[s_page]) != 0) {
        strcpy(s_shown[NROWS], k_page_title[s_page]);
        set_text(b, k_page_title[s_page]);
    }
    show(b, 1, 0);
    for (i = 0; i < NROWS; i++) {
        b = find_control(menu, ID_ROW0 + (uint32_t)i);
        if (!b) continue;
        if (i < s_nrows) {
            if (strcmp(s_shown[i], s_row[i].text) != 0) {
                strcpy(s_shown[i], s_row[i].text);
                set_text(b, s_row[i].text);
            }
            show(b, 1, s_row[i].item >= 0);
        } else {
            show(b, 0, 0);
        }
    }
}

static int first_selectable(void)
{
    int i;
    for (i = 0; i < s_nrows; i++)
        if (s_row[i].item >= 0) return i;
    return -1;
}

/* Swap the Options menu between its own list (PG_NONE) and a PC page.
 * The selection is always moved onto a line that stays before the others are
 * hidden: a control that loses the selection goes back to state 0, which
 * would un-hide it. */
static void set_page(int page)
{
    uint32_t menu = s_options, node;
    int was = s_page, i, guard = 0;
    if (!menu || was == page) return;

    s_page = page;
    if (page != PG_NONE) {
        refresh();
        i = first_selectable();
        if (i >= 0) go_to(menu, ID_ROW0 + (uint32_t)i);
        if (was == PG_NONE) {
            /* hide the list, remembering what each line was */
            s_nsaved = 0;
            for (node = MEM32(menu + M_HEAD); node && guard < 512; node = MEM32(node + 4), guard++) {
                uint32_t c = MEM32(node), id;
                if (!c || MEM32(c) != VT_TEXTBUTTON) continue;
                id = MEM32(c + C_ID);
                if (id == ID_HEADING || (id >= ID_ROW0 && id < ID_ROW0 + NROWS)) continue;
                if (s_nsaved < 32) {
                    s_saved_id[s_nsaved] = id;
                    s_saved_state[s_nsaved++] = MEM32(c + C_STATE);
                }
                MEM32(c + C_STATE) = 3;
            }
        }
    } else {
        int entry = (was == PG_BINDINGS) ? PG_CONTROLS : (was == PG_REPORT_DONE) ? PG_REPORT : was;
        for (i = 0; i < s_nsaved; i++) {
            uint32_t c = find_control(menu, s_saved_id[i]);
            if (!c) continue;
            MEM32(c + C_STATE) = s_saved_state[i] == 1 ? 0 : s_saved_state[i];
            MEM32(c + B_FONT) = MEM32(c + B_FONT_OFF);
        }
        s_nsaved = 0;
        go_to(menu, ID_ENTRY0 + (uint32_t)entry);
        show(find_control(menu, ID_HEADING), 0, 0);
        for (i = 0; i < NROWS; i++) show(find_control(menu, ID_ROW0 + (uint32_t)i), 0, 0);
        btvs_settings_save();
    }
    btvs_trail("menu", "screen=%s", k_page_name[page]);
}

/* One PC page straight to another (Controls <-> Key Bindings, Report -> saved). */
static void switch_page(int page)
{
    if (s_page == PG_NONE || page == PG_NONE) { set_page(page); return; }
    s_page = page;
    refresh();
    { int i = first_selectable(); if (i >= 0) go_to(s_options, ID_ROW0 + (uint32_t)i); }
    refresh();      /* lines past the new page's end, now that none of them is selected */
    btvs_trail("menu", "screen=%s", k_page_name[page]);
}

/* Left / Right (dir -1 / +1) or A (dir 0) on a line. Returns 1 if it changed the page. */
static int act(int item, int dir)
{
    int step = dir ? dir : 1, i;
    switch (s_page) {
    case PG_GRAPHICS:
        switch (item) {
        case 0: cycle("display", "mode", 1, 3, step); break;
        case 1:
            i = wrap((res_index() < 0 ? 0 : res_index()) + step, NRES);
            btvs_setting_set_int("display", "width", k_res[i].w);
            btvs_setting_set_int("display", "height", k_res[i].h);
            break;
        case 2: cycle("display", "vsync", 1, 2, step); break;
        case 3: btvs_setting_set_int("display", "fps_limit", k_fps[wrap(fps_index() + step, NFPS)]); break;
        case 4: cycle("display", "widescreen", 0, 3, step); break;
        case 5: cycle("display", "show_fps", 0, 3, step); break;
        }
        break;
    case PG_AUDIO: {
        static const char *const k[] = { "master", "music", "effects", "speech" };
        if (item >= 0 && item < 4) {
            if (dir) slide("audio", k[item], 100, 0, 100, 5, dir);
            else     btvs_setting_set_int("audio", k[item], wrap(btvs_setting_int("audio", k[item], 100) + 5, 105));
        }
        break;
    }
    case PG_CONTROLS:
        switch (item) {
        case 0:
            if (dir) slide("controls", "sensitivity", 5, 1, 10, 1, dir);
            else     btvs_setting_set_int("controls", "sensitivity", 1 + wrap(btvs_setting_int("controls", "sensitivity", 5), 10));
            break;
        case 1: cycle("controls", "invert_y", 0, 2, step); break;
        case 2: cycle("controls", "rumble", 1, 2, step); break;
        case 3: if (!dir) { s_bind_top = 0; switch_page(PG_BINDINGS); return 1; } break;
        case 4: if (!dir) btvs_input_bindings_reset(); break;
        }
        break;
    case PG_BINDINGS:
        if (item == 1000) { if (!dir) { switch_page(PG_CONTROLS); return 1; } break; }
        if (dir) s_bind_slot = wrap(s_bind_slot + dir, 3);
        else { s_bind_capture = item; btvs_input_capture_begin(item, s_bind_slot); }
        break;
    case PG_MEMORY:
        if (!dir) { set_page(PG_NONE); return 1; }
        break;
    case PG_REPORT:
        if (item == 0) s_rep_cat = wrap(s_rep_cat + step, 6);
        else if (item == 1) {
            if (!s_rep_edit) { if (!dir) { s_rep_edit = 1; s_rep_char = 1; } }
            else if (dir) s_rep_char = wrap(s_rep_char + dir, (int)sizeof k_charset - 1);
            else {
                size_t n = strlen(s_rep_text);
                if (n + 1 < sizeof s_rep_text) { s_rep_text[n] = k_charset[s_rep_char]; s_rep_text[n + 1] = '\0'; }
            }
        } else if (item == 2 && !dir) {
            char text[200];
            snprintf(text, sizeof text, "%s: %s", k_category[wrap(s_rep_cat, 6)], s_rep_text);
            btvs_log("menu", "report a problem: %s", text);
            btvs_report_create(text);
            s_rep_edit = 0; s_rep_text[0] = '\0';
            switch_page(PG_REPORT_DONE);
            return 1;
        }
        break;
    case PG_REPORT_DONE:
        if (!dir) { set_page(PG_NONE); return 1; }
        break;
    }
    refresh();
    return 0;
}

/* A character typed on the keyboard (the window's WM_CHAR): the report's details. */
void btvs_menu_char(unsigned ch)
{
    size_t n;
    if (s_page != PG_REPORT) return;
    n = strlen(s_rep_text);
    if (ch == 8) { if (n) s_rep_text[n - 1] = '\0'; }
    else if (ch >= 32 && ch < 127 && n + 1 < sizeof s_rep_text) { s_rep_text[n] = (char)ch; s_rep_text[n + 1] = '\0'; }
    s_next_refresh = 0;
}

/* 1 while a PC page is open (Controls: no mouse look; anyone: don't treat B as game input). */
int btvs_menu_page_open(void) { return s_page != PG_NONE; }

static int current_item(void)
{
    uint32_t id = selected_id(s_options);
    if (id < ID_ROW0 || id >= ID_ROW0 + NROWS) return -1;
    return (int)(id - ID_ROW0) < s_nrows ? s_row[id - ID_ROW0].item : -1;
}

/* ── hooks: Options ──────────────────────────────────────────────────── */

static void options_setup(uint32_t menu)
{
    static const struct { int page; const char *text; int col, line; } k_entry[] = {
        { PG_GRAPHICS, "Graphics", 1, 0 }, { PG_AUDIO, "PC Audio", 1, 1 },
        { PG_CONTROLS, "PC Controls", 1, 2 }, { PG_MEMORY, "Memory", 1, 3 },
        { PG_REPORT, "Report a Problem", 0, 5 } };
    /* the game's own lines are at x 121, y 117 163 210 261 312 */
    static const int k_line_y[] = { 117, 163, 210, 261, 312, 363 };
    uint32_t like = find_control(menu, ID_OPTIONS_CONTROLLER);
    uint32_t small_off = font_resource(4), small_on = font_resource(3);
    int i;

    if (find_control(menu, ID_HEADING)) { s_options = menu; return; }
    if (!like) { btvs_log("menu", "options: no line to copy, PC entries not added"); return; }
    s_options = menu;
    s_page = PG_NONE;
    memset(s_shown, 0, sizeof s_shown);
    for (i = 0; i < (int)(sizeof k_entry / sizeof k_entry[0]); i++)
        add_button(menu, like, ID_ENTRY0 + (uint32_t)k_entry[i].page,
                   k_entry[i].col ? 340 : (int)MEM32(like + C_X), k_line_y[k_entry[i].line],
                   (int)MEM32(like + C_W), (int)MEM32(like + C_H), 0, 0, k_entry[i].text);
    show(add_button(menu, like, ID_HEADING, 121, 76, 400, 30, small_off, small_off, ""), 0, 0);
    for (i = 0; i < NROWS; i++)
        show(add_button(menu, like, ID_ROW0 + (uint32_t)i, 121, 110 + i * 29, 400, 29,
                        small_off, small_on, ""), 0, 0);
    btvs_log("menu", "options: PC entries added");
}

static void hook_options_onload(void)
{
    uint32_t menu = g_ecx;
    GUI_TMenuOptions_OnLoad_00223230();
    pc_wording();
    options_setup(menu);
    if (s_options != menu) return;
    if (s_page != PG_NONE) set_page(PG_NONE);
    if (s_pending_page) { int p = s_pending_page; s_pending_page = 0; set_page(p); }
}

static void hook_options_onmessage(void)
{
    uint32_t menu = g_ecx, msg = MEM32(g_esp + 8), id = MEM32(g_esp + 12);
    if (menu == s_options && MEM8(menu + M_LOADED) && msg == MSG_PRESSED) {
        if (id > ID_ENTRY0 && id < ID_ENTRY0 + PG_COUNT && s_page == PG_NONE) {
            set_page((int)(id - ID_ENTRY0));
            g_esp += 4 + 16;
            return;
        }
        if (id >= ID_ROW0 && id < ID_ROW0 + NROWS && s_page != PG_NONE) {
            int r = (int)(id - ID_ROW0);
            if (r < s_nrows && s_row[r].item >= 0) act(s_row[r].item, 0);
            g_esp += 4 + 16;
            return;
        }
    }
    GUI_TMenuOptions_OnMessage_002232E0();
}

static void hook_options_oncommand(void)
{
    uint32_t menu = g_ecx, cmd = MEM32(g_esp + 4);
    int n, sel;

    if (menu != s_options || s_page == PG_NONE || MEM32(G_FADE_MODE) != 0) {
        GUI_TMenuOptions_OnCommand_002233F0();
        return;
    }
    /* a binding is being captured: the next press belongs to Controls */
    if (s_bind_capture >= 0) { g_esp += 8; return; }

    if (s_page == PG_REPORT && s_rep_edit) {
        /* writing the details: Left/Right pick a letter, A adds it, X deletes, B ends */
        if (cmd == CMD_LEFT || cmd == CMD_RIGHT) act(1, cmd == CMD_LEFT ? -1 : 1);
        else if (cmd == CMD_SELECT) act(1, 0);
        else if (cmd == CMD_DELETE) { size_t l = strlen(s_rep_text); if (l) s_rep_text[l - 1] = '\0'; refresh(); }
        else if (cmd == CMD_BACK) { s_rep_edit = 0; refresh(); }
        if (cmd == CMD_SELECT || cmd == CMD_BACK)
            gcall(VFUNC(menu, V_MENU_SOUNDS), menu, 1, cmd);
        g_esp += 8;
        return;
    }

    switch (cmd) {
    case CMD_BACK:
        /* the B button lights and sounds as usual, but it leaves the page, not Options */
        gcall(GUI_TBuffyMenu_OnBuffyCommand_00218C20, menu, 1, cmd);
        gcall(VFUNC(menu, V_MENU_SOUNDS), menu, 1, cmd);
        if (s_page == PG_BINDINGS) switch_page(PG_CONTROLS);
        else                       set_page(PG_NONE);
        g_esp += 8;
        return;
    case CMD_LEFT:
    case CMD_RIGHT:
        sel = current_item();
        if (sel >= 0) {
            act(sel, cmd == CMD_LEFT ? -1 : 1);
            gcall(VFUNC(menu, V_MENU_FOCUSSND), menu, 0);
        }
        g_esp += 8;
        return;
    case CMD_DELETE:
        if (s_page == PG_BINDINGS && (sel = current_item()) >= 0 && sel != 1000) {
            btvs_input_binding_clear(sel, s_bind_slot);
            refresh();
        }
        g_esp += 8;
        return;
    case CMD_UP:
    case CMD_DOWN:
        /* the bindings list is longer than the page: scroll at its ends */
        if (s_page == PG_BINDINGS && (n = btvs_input_action_count()) > NROWS) {
            uint32_t id = selected_id(menu);
            if (cmd == CMD_DOWN && id == ID_ROW0 + NROWS - 1 && s_bind_top + NROWS < n) {
                s_bind_top++; refresh();
                gcall(VFUNC(menu, V_MENU_FOCUSSND), menu, 0);
                g_esp += 8;
                return;
            }
            if (cmd == CMD_UP && id == ID_ROW0 && s_bind_top > 0) {
                s_bind_top--; refresh();
                gcall(VFUNC(menu, V_MENU_FOCUSSND), menu, 0);
                g_esp += 8;
                return;
            }
        }
        break;
    }
    GUI_TMenuOptions_OnCommand_002233F0();
}

/* Every menu's per-frame update: keeps an open page's numbers current. */
static void hook_buffymenu_onupdate(void)
{
    uint32_t menu = g_ecx;
    GUI_TBuffyMenu_OnUpdate_00219480();
    if (menu != s_options || s_page == PG_NONE) return;
    if (s_bind_capture >= 0 && btvs_input_capture_state() != 0) {
        s_bind_capture = -1;
        s_next_refresh = 0;
    }
    if (GetTickCount() >= s_next_refresh) {
        uint32_t eax0 = g_eax;
        s_next_refresh = GetTickCount() + (s_page == PG_MEMORY ? 500 : 200);
        refresh();
        g_eax = eax0;
    }
}

/* ── PC wording ──────────────────────────────────────────────────────────
 * The game's texts that name the Xbox, its hard disk or its "blocks" are
 * overwritten where they lie (the new text is never longer): the strings of
 * the menu file, the copies the menus' controls made of them, and the
 * messages in the game's own data. */

static const struct { const char *from, *to; int shift_x; } k_word[] = {
    { "Launch a New Game on your Xbox Hard Disk called", "Launch a New Game on this PC called", 0 },
    { "Hard Disk", "This PC", 0 },
    { "Total Blocks", "", 0 },
    { "Free Blocks", "", 0 },
    { "- 50,000+", "", 0 },
};
static const struct { uint32_t va; const char *from, *to; } k_message[] = {
    { 0x0061B278u, "There's a problem with the disc",
      "There's a problem with the game's files.  They may be missing or damaged." },
    { 0x0061B2C8u, "There's a problem with the disc",
      "There's a problem with the game's files.  They may be missing or damaged.  Press A to continue" },
    { 0x0068D4DCu, "Deleting saved game.  Please don't turn off",
      "Deleting saved game.  Please don't close the game." },
    { 0x0068D600u, "Your Xbox doesn't have enough free blocks to save options",
      "There isn't enough free disk space to save options. Press A to continue without saving." },
    { 0x0068D6E8u, "Buffy The Vampire Slayer only allows %i saved games on your Xbox",
      "Buffy The Vampire Slayer only allows %i saved games.  Please delete one to start a new game.  Press A to continue." },
    { 0x0068D778u, "Your Xbox Hard Disk has too many",
      "There are too many saved games.  Press A to continue." },
    { 0x0068D7C8u, "Your Xbox Hard Disk doesn't have enough",
      "There isn't enough free disk space to save games (%i blocks are required). Press A to continue without saving." },
    { 0x0068DD38u, "Your Xbox has too many Buffy",
      "There are too many Buffy saved games.\nPress A to continue." },
    { 0x0068DDCCu, "Your Xbox has too many saved",
      "There are too many saved games.\nPress A to continue." },
    { 0x0068DE58u, "Your Xbox doesn't have enough free blocks to save games",
      "There isn't enough free disk space to save games. Press A to continue without saving." },
};

/* Is guest memory [va, va+len) there to read and write? */
static int gok(uint32_t va, uint32_t len)
{
    MEMORY_BASIC_INFORMATION mi;
    const void *p;
    if (va < 0x10000u || va >= 0x10000000u) return 0;
    p = (const void *)XBOX_PTR(va);
    if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT) return 0;
    if (!(mi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))) return 0;
    return (const char *)p + len <= (const char *)mi.BaseAddress + mi.RegionSize;
}

/* If `va` is one of the texts to reword, reword it; returns its shift_x + 1, else 0. */
static int reword_at(uint32_t va)
{
    int i;
    char *s;
    if (!gok(va, 64)) return 0;
    s = (char *)XBOX_PTR(va);
    for (i = 0; i < (int)(sizeof k_word / sizeof k_word[0]); i++)
        if (s[0] == k_word[i].from[0] && !strcmp(s, k_word[i].from)) {
            strcpy(s, k_word[i].to);
            return k_word[i].shift_x + 1;
        }
    return 0;
}

static void pc_wording(void)
{
    static int messages_done;
    uint32_t mgr = MEM32(G_MANAGER), arr, n, i, off;
    int strings = 0, controls = 0, messages = 0;

    if (!messages_done) {
        messages_done = 1;
        for (i = 0; i < sizeof k_message / sizeof k_message[0]; i++) {
            char *s;
            if (!gok(k_message[i].va, 160)) continue;
            s = (char *)XBOX_PTR(k_message[i].va);
            if (strncmp(s, k_message[i].from, strlen(k_message[i].from)) ||
                strlen(k_message[i].to) > strlen(s)) continue;
            strcpy(s, k_message[i].to);
            messages++;
        }
        btvs_log("menu", "PC wording: %d of %d game messages", messages, (int)(sizeof k_message / sizeof k_message[0]));
    }
    if (!mgr || !gok(mgr, 0x60)) return;
    /* the menu file's strings (resource type 0) */
    arr = MEM32(mgr + 0x20); n = MEM32(mgr + 0x2C);
    if (n <= 512 && gok(arr, n * 4))
        for (i = 0; i < n; i++) {
            uint32_t r = MEM32(arr + i * 4);
            if (!gok(r, 0x20)) continue;
            for (off = 0; off < 0x20; off += 4)
                if (reword_at(MEM32(r + off))) strings++;
        }
    /* the copies in the menus' controls */
    arr = MEM32(mgr + 0x48);
    if (gok(arr, 15 * 4))
        for (i = 0; i < 15; i++) {
            uint32_t menu = MEM32(arr + i * 4), node;
            int guard = 0;
            if (!gok(menu, 0x50)) continue;
            for (node = MEM32(menu + M_HEAD); gok(node, 12) && guard < 256; node = MEM32(node + 4), guard++) {
                uint32_t c = MEM32(node);
                if (!gok(c, 0x44)) continue;
                for (off = 0x24; off < 0x44; off += 4) {
                    int r = reword_at(MEM32(c + off));
                    if (r) { MEM32(c + C_X) += (uint32_t)(r - 1); controls++; }
                }
            }
        }
    if (strings || controls)
        btvs_log("menu", "PC wording: %d menu strings, %d control texts", strings, controls);
}

/* The "Start Game" page (TMenuNameSession): no count of free "blocks" on a
 * PC, and the reworded first line is shorter, so it is centred again. */
static void hook_namesession_onupdate(void)
{
    uint32_t menu = g_ecx, eax0, c;
    static const uint32_t k_blocks[] = { 267, 268, 269, 270 };
    int i;
    GUI_TMenuNameSession_OnUpdate_00222F00();
    eax0 = g_eax;
    for (i = 0; i < 4; i++)
        if ((c = find_control(menu, k_blocks[i])) != 0) MEM32(c + C_STATE) = 3;
    if ((c = find_control(menu, 115)) != 0 && MEM32(c + C_X) == 113) MEM32(c + C_X) = 165;
    g_eax = eax0;
}

/* ── hooks: main menu ────────────────────────────────────────────────── */

static void hook_main_onload(void)
{
    uint32_t menu = g_ecx, like;
    GUI_TMenuMain_OnLoad_00222470();
    pc_wording();
    if (find_control(menu, ID_EXIT)) return;
    like = find_control(menu, ID_MAIN_STARTGAME);
    if (!like) { btvs_log("menu", "main menu: no line to copy, Exit not added"); return; }
    /* the game's lines: Start 170, Load 230, Options 290, Extras 348 */
    add_button(menu, like, ID_EXIT, 69, 406, (int)MEM32(like + C_W), (int)MEM32(like + C_H), 0, 0, "Exit");
    btvs_log("menu", "main menu: Exit added");
}

static void hook_main_onmessage(void)
{
    uint32_t menu = g_ecx, msg = MEM32(g_esp + 8), id = MEM32(g_esp + 12);
    if (MEM8(menu + M_LOADED) && msg == MSG_PRESSED && id == ID_EXIT) {
        btvs_log("menu", "Exit chosen on the main menu");
        btvs_trail("menu", "exit=1");
        btvs_settings_save();
        btvs_quit();        /* Graphics: closes the window and device in order, then ends the process */
        exit(0);
    }
    GUI_TMenuMain_OnMessage_002226B0();
}

/* ── hooks: pause menu ───────────────────────────────────────────────── */

static void hook_pause_onload(void)
{
    uint32_t menu = g_ecx, like, f_off, f_on;
    btvs_report_prepare();      /* the screenshot, while the game picture is still up */
    GUI_TMenuPause_OnLoad_00223760();
    pc_wording();
    if (find_control(menu, ID_PAUSE_REPORT)) return;
    like = find_control(menu, ID_PAUSE_RESUME);
    if (!like) { btvs_log("menu", "pause menu: no line to copy, Report a Problem not added"); return; }
    f_off = font_resource(4); f_on = font_resource(3);
    /* the game's lines: Resume 76, Options 128, Quit 180 at x 273; objective text from 233 */
    add_button(menu, like, ID_PAUSE_REPORT, 64, 376, 220, 28, f_off, f_on, "Report a Problem");
    btvs_log("menu", "pause menu: Report a Problem added");
}

static void hook_pause_onmessage(void)
{
    uint32_t menu = g_ecx, msg = MEM32(g_esp + 8), id = MEM32(g_esp + 12);
    if (MEM8(menu + M_LOADED) && msg == MSG_PRESSED && id == ID_PAUSE_REPORT) {
        uint32_t mgr = MEM32(G_MANAGER);
        s_pending_page = PG_REPORT;
        if (mgr) gcall(VFUNC(mgr, V_MGR_SETMENU), mgr, 2, (uint32_t)MENU_OPTIONS, 1u);
        g_esp += 4 + 16;
        return;
    }
    GUI_TMenuPause_OnMessage_00223A60();
}

/* ── the hook table (recomp_lookup_manual asks this first) ───────────── */

recomp_func_t btvs_menu_lookup(uint32_t va)
{
    static int off = -1;
    if (va < VA_BUFFYMENU_ONUPDATE || va > VA_PAUSE_ONMESSAGE) return 0;
    if (off < 0) off = btvs_setting_int("menu", "pc_entries", 1) ? 0 : 1;   /* BTVS_MENU_PC_ENTRIES=0: untouched menus */
    if (off) return 0;
    switch (va) {
    case VA_OPTIONS_ONLOAD:     return hook_options_onload;
    case VA_OPTIONS_ONMESSAGE:  return hook_options_onmessage;
    case VA_OPTIONS_ONCOMMAND:  return hook_options_oncommand;
    case VA_BUFFYMENU_ONUPDATE: return hook_buffymenu_onupdate;
    case VA_MAIN_ONLOAD:        return hook_main_onload;
    case VA_NAMESESSION_ONUPDATE: return hook_namesession_onupdate;
    case VA_MAIN_ONMESSAGE:     return hook_main_onmessage;
    case VA_PAUSE_ONLOAD:       return hook_pause_onload;
    case VA_PAUSE_ONMESSAGE:    return hook_pause_onmessage;
    }
    return 0;
}
