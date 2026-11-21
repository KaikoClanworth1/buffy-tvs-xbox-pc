/* The trail: a compact, always-on record of what the player did, so a report
 * says how to get back to the same spot.
 *
 *   btvs_trail.txt beside the exe (the run before: btvs_trail_previous.txt)
 *
 *        12.345 f=731     level        name=Mansion
 *        17.345 f=1031    pos          x=12.50 y=0.00 z=-3.25 health=100
 *
 * columns: seconds since start, frames shown, event, key=value pairs.
 *
 * Anchors ("boot", "level", "checkpoint", "load") are the places a replay can
 * start from: at each one the input recording (Controls) starts again into
 * btvs_input.rec, so the recording always covers "from the last anchor until
 * now", and BTVS_REPLAY goes to that anchor and plays it.
 *
 * The game-side events come from hooks on the game's own functions (GAME
 * HOOKS below); menus, settings and the rest call btvs_trail() themselves.
 */
#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xbox/xboxrecomp.h>
#include "btvs_log.h"

long xbox_HangMonitorFrames(void);
void btvs_trail_start_watch(void);

/* Controls' recorder has no "write what you have" call yet: a stand-in until
 * it does (its file is complete at btvs_input_record_end). */
void btvs_input_record_flush(void);
void btvs_input_record_flush_none(void) {}
#pragma comment(linker, "/alternatename:btvs_input_record_flush=btvs_input_record_flush_none")

#define RING 96
#define LINE 240

static INIT_ONCE s_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION s_cs;
static HANDLE s_file = INVALID_HANDLE_VALUE;
static char s_ring[RING][LINE];
static unsigned s_ring_n;
static struct btvs_where s_where;
static int s_recording;          /* Controls took the recording */
int btvs_recording_cut;          /* a report had to restart the recording: it no longer begins at the anchor */
static double s_pos_every = 5.0, s_pos_last = -1000.0;
static float s_last_pos[3], s_last_health;

static BOOL CALLBACK init_once(PINIT_ONCE o, PVOID p, PVOID *c)
{
    char path[MAX_PATH], prev[MAX_PATH];
    const char *e = getenv("BTVS_TRAIL_POS_SECS");
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&s_cs);
    sprintf_s(path, MAX_PATH, "%s\\" BTVS_TRAIL_FILE, btvs_exe_dir());
    sprintf_s(prev, MAX_PATH, "%s\\" BTVS_TRAIL_PREVIOUS, btvs_exe_dir());
    if (!btvs_restarts())
        MoveFileExA(path, prev, MOVEFILE_REPLACE_EXISTING);
    s_file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (e && atof(e) > 0.0)
        s_pos_every = atof(e);
    return TRUE;
}

/* "name=Some Thing key=..." -> "Some Thing" (a value runs to the next " key="). */
static void value_of(const char *text, const char *key, char *out, size_t n)
{
    const char *p = text, *end;
    size_t klen = strlen(key), len;
    out[0] = 0;
    for (;;) {
        p = strstr(p, key);
        if (!p)
            return;
        if ((p == text || p[-1] == ' ') && p[klen] == '=')
            break;
        p += klen;
    }
    p += klen + 1;
    end = p;
    for (;;) {
        const char *sp = strchr(end, ' '), *q;
        if (!sp) {
            end = p + strlen(p);
            break;
        }
        for (q = sp + 1; *q && *q != ' ' && *q != '='; q++) ;
        if (*q == '=') {
            end = sp;
            break;
        }
        end = sp + 1;
    }
    len = (size_t)(end - p);
    if (len >= n)
        len = n - 1;
    memcpy(out, p, len);
    out[len] = 0;
}

/* Where a replay can start. The game loads every level by restarting itself
 * with launch data (and has no checkpoints), so the start of a process is the
 * one place that can be reached again exactly: a level's "boot" carries the
 * launch data that leads to it. */
static int is_anchor(const char *event)
{
    return !strcmp(event, "boot");
}

void btvs_trail(const char *event, const char *fmt, ...)
{
    char text[180], line[LINE], name[96];
    va_list ap;
    DWORD w;
    double now;
    long frame;
    int n, anchor;

    InitOnceExecuteOnce(&s_once, init_once, NULL, NULL);
    text[0] = 0;
    if (fmt) {
        va_start(ap, fmt);
        _vsnprintf_s(text, sizeof text, _TRUNCATE, fmt, ap);
        va_end(ap);
    }
    for (n = 0; text[n]; n++)
        if (text[n] == '\n' || text[n] == '\r')
            text[n] = ' ';
    now = btvs_uptime();
    frame = xbox_HangMonitorFrames();
    n = _snprintf_s(line, sizeof line, _TRUNCATE, "%10.3f f=%-7ld %-12s %s\n", now, frame, event, text);
    if (n < 0)
        n = (int)strlen(line);
    anchor = is_anchor(event);
    value_of(text, "name", name, sizeof name);

    EnterCriticalSection(&s_cs);
    if (s_file != INVALID_HANDLE_VALUE)
        WriteFile(s_file, line, (DWORD)n, &w, NULL);
    memcpy(s_ring[s_ring_n % RING], line, (size_t)n + 1);
    s_ring_n++;
    if (!strcmp(event, "level")) {
        strcpy_s(s_where.level, sizeof s_where.level, name);
        s_where.checkpoint[0] = 0;
        s_where.menu[0] = 0;
    } else if (!strcmp(event, "checkpoint")) {
        strcpy_s(s_where.checkpoint, sizeof s_where.checkpoint, name);
    } else if (!strcmp(event, "menu")) {
        value_of(text, "screen", s_where.menu, sizeof s_where.menu);
    }
    if (anchor) {
        strcpy_s(s_where.anchor_event, sizeof s_where.anchor_event, event);
        strcpy_s(s_where.anchor_name, sizeof s_where.anchor_name, name);
        s_where.anchor_time = now;
        s_where.anchor_frame = frame;
    }
    LeaveCriticalSection(&s_cs);

    btvs_log("trail", "%s %s", event, text);

    if (anchor) {
        /* The recording starts again here; a replay that has just arrived at
         * its anchor starts playing instead. */
        char path[MAX_PATH];
        sprintf_s(path, MAX_PATH, "%s\\" BTVS_INPUT_FILE, btvs_exe_dir());
        if (btvs_replay_active())
            btvs_replay_on_anchor(event, name);
        {   /* (also while a replay plays: what the game is fed is recorded, so a
             * report made during or after a replay can itself be replayed) */
            int ok = btvs_input_record_begin(path);
            btvs_recording_cut = 0;
            if (ok != s_recording) {
                s_recording = ok;
                btvs_log("trail", ok ? "input recording on (%s)" : "input recording not available (%s)", path);
            }
        }
    }
}

void btvs_trail_init(void)
{
    InitOnceExecuteOnce(&s_once, init_once, NULL, NULL);
    {
        /* The launch data's command line, if the game started this run itself
         * (kernel_bridge.c reads the same file). */
        const char *file = getenv("RECOMP_LAUNCH_DATA_FILE");
        char page[0x1000], cmd[120];
        FILE *f = NULL;
        size_t got = 0, i;
        cmd[0] = 0;
        if (file && *file && fopen_s(&f, file, "rb") == 0 && f) {
            got = fread(page, 1, sizeof page, f);
            fclose(f);
        }
        if (got > 0x400) {
            for (i = 0; i + 1 < sizeof cmd && 0x400 + i < got && page[0x400 + i]; i++)
                cmd[i] = (page[0x400 + i] >= 32 && page[0x400 + i] < 127) ? page[0x400 + i] : '.';
            cmd[i] = 0;
        }
        if (got)
            btvs_trail("boot", "version=%s run=%d launch=%s", BTVS_VERSION, btvs_restarts() + 1, cmd);
        else
            btvs_trail("boot", "version=%s run=%d", BTVS_VERSION, btvs_restarts() + 1);
    }
    btvs_trail_start_watch();
}

int btvs_trail_tail(char *buf, size_t n, int lines)
{
    unsigned first, i;
    size_t len = 0;
    InitOnceExecuteOnce(&s_once, init_once, NULL, NULL);
    if (n)
        buf[0] = 0;
    if (lines > RING)
        lines = RING;
    /* (no lock: a crash report must not wait for a thread that died holding it) */
    first = s_ring_n > (unsigned)lines ? s_ring_n - (unsigned)lines : 0;
    for (i = first; i < s_ring_n; i++) {
        const char *l = s_ring[i % RING];
        size_t ll = strnlen(l, LINE - 1);
        if (len + ll + 1 >= n)
            break;
        memcpy(buf + len, l, ll);
        len += ll;
        buf[len] = 0;
    }
    return (int)(s_ring_n - first);
}

void btvs_trail_where(struct btvs_where *out)
{
    InitOnceExecuteOnce(&s_once, init_once, NULL, NULL);
    EnterCriticalSection(&s_cs);
    *out = s_where;
    LeaveCriticalSection(&s_cs);
    out->frame = xbox_HangMonitorFrames();
}

/* ── GAME HOOKS ─────────────────────────────────────────────────────────────
 * What is read from the game itself. Addresses are the game's own (guest)
 * addresses; names are from its linker map (python port/tools/sym.py <addr>).
 *
 *   gGame (TGame*)                    [0x7D0090]
 *     +0x40  TGameMapManager*           +0x3E map valid (byte), +0x40 map name (TString: char* first)
 *     +0xF0  the player (IObject*)      +0x240 position (3 floats), +0x90 health, +0x94 max health
 *   GUI s_menuID                      [0x7F0408]
 *   g_memoryPeakAllocation            [0x837FC4]
 *
 * Two ways in, neither needing a regen:
 *  - virtual functions go through the dispatcher, so btvs_trail_lookup()
 *    (asked by recomp_lookup_manual on every indirect call) swaps in a wrapper
 *    that runs on the game thread: exact events, and the once-a-frame tick;
 *  - a watcher thread reads the globals above ten times a second: the
 *    fall-back for level and menu changes, and the position samples.
 * docs/logs.md says which of these have been seen working.
 */
typedef void (*btvs_guest_fn)(void);
extern ptrdiff_t g_xbox_mem_offset;
extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
void xbox_HeapStats(uint32_t *used_bytes, uint32_t *blocks, uint32_t *high_water);

#define G_GAME        0x7D0090u
#define G_MENU_ID     0x7F0408u
#define G_MEM_PEAK    0x837FC4u

/* Guest memory, without faulting on a pointer that is not one. */
static int g_read(uint32_t va, void *out, size_t n)
{
    SIZE_T got = 0;
    if (!g_xbox_mem_offset || va < 0x10000u)
        return 0;
    return ReadProcessMemory(GetCurrentProcess(), (const void *)((uintptr_t)g_xbox_mem_offset + va), out, n, &got)
           && got == n;
}

static uint32_t g32(uint32_t va)
{
    uint32_t v = 0;
    return g_read(va, &v, 4) ? v : 0;
}

/* The map the game has loaded ("" in the menus or between maps). */
static int game_level(char *out, size_t n)
{
    uint32_t game = g32(G_GAME), mgr = game ? g32(game + 0x40) : 0, str;
    unsigned char valid = 0;
    size_t i;
    memset(out, 0, n);
    if (!mgr || !g_read(mgr + 0x3E, &valid, 1) || !valid)
        return 0;
    str = g32(mgr + 0x40);
    if (!str)
        return 0;
    for (i = 0; i + 1 < n; i++) {
        unsigned char c = 0;
        if (!g_read(str + (uint32_t)i, &c, 1) || !c)
            break;
        if (c < 0x20 || c > 0x7E) {
            out[0] = 0;                            /* not a name: the manager isn't set up yet */
            return 0;
        }
        out[i] = (char)c;
    }
    out[i] = 0;
    return i > 0;
}

uint64_t btvs_guest_ram_bytes(void) { return 64ull << 20; }

/* The game's memory. Its own per-category counters are compiled out of the
 * retail build (its free is the C library's), so the figures are the Xbox
 * memory manager's: what the game has taken from the console's 64 MB (the
 * toolkit's heap, which stands in for the Xbox kernel's), and the game's own
 * record of its largest total. */
int btvs_game_heap_stats(uint64_t *used, uint64_t *peak, uint32_t *allocs)
{
    uint32_t u = 0, b = 0, hw = 0, game_peak;
    xbox_HeapStats(&u, &b, &hw);
    game_peak = g32(G_MEM_PEAK);
    *used = u;
    *allocs = b;
    *peak = hw > game_peak ? hw : game_peak;
    return g_xbox_mem_offset != 0 && b != 0;
}

static int btvs_game_player(float pos[3], float *health)
{
    uint32_t game = g32(G_GAME), pl = game ? g32(game + 0xF0) : 0;
    if (!pl || !g_read(pl + 0x240, pos, 12) || !g_read(pl + 0x90, health, 4))
        return 0;
    if (!(pos[0] == pos[0]) || fabsf(pos[0]) > 1e7f || fabsf(pos[1]) > 1e7f || fabsf(pos[2]) > 1e7f)
        return 0;
    return 1;
}

/* The player's and the camera's world matrices as they are now (IObject keeps
 * one at +0x21C: 3x3 rotation then position, 12 floats;
 * IObject::Mtx_GetWorld is "lea eax,[ecx+0x21C]"), and the player's health.
 * Any thread. 0 when there is no player. */
int btvs_game_state(float player[12], float camera[12], float *health)
{
    uint32_t game = g32(G_GAME), pl = game ? g32(game + 0xF0) : 0, cam = game ? g32(game + 0xF4) : 0;
    int i;
    if (!pl || !g_read(pl + 0x21C, player, 48) || !g_read(pl + 0x90, health, 4))
        return 0;
    if (!cam || !g_read(cam + 0x21C, camera, 48))
        memset(camera, 0, 48);
    for (i = 0; i < 12; i++)
        if (!(player[i] == player[i]) || fabsf(player[i]) > 1e7f)
            return 0;
    return 1;
}

/* BTVS_REPLAY: put the player where the report was made, with the game's own
 * ICharacter::Teleport(const T_Matrix4&, bool rotation, bool camera, bool
 * restart_script) (thiscall, 0x1305D0: stops the character, sets its matrix,
 * puts the camera behind it). Game thread only (btvs_frame_tick).
 * 1 = done, 0 = no player. */
void ICharacter_Teleport_001305D0(void);
int btvs_game_place(const float m[12], float health)
{
    uint32_t game = g32(G_GAME), pl = game ? g32(game + 0xF0) : 0, mva, r[7];
    uint8_t *mem = (uint8_t *)(uintptr_t)g_xbox_mem_offset;
    if (!pl || !mem)
        return 0;
    r[0] = g_eax; r[1] = g_ecx; r[2] = g_edx; r[3] = g_ebx; r[4] = g_esi; r[5] = g_edi; r[6] = g_esp;
    mva = (g_esp - 1024) & ~15u;                 /* the matrix, below anything live on the guest stack */
    memcpy(mem + mva, m, 48);
    g_esp = mva - 16;
    g_esp -= 4; *(uint32_t *)(mem + g_esp) = 1;      /* start the character's script state afresh */
    g_esp -= 4; *(uint32_t *)(mem + g_esp) = 1;      /* camera behind the player */
    g_esp -= 4; *(uint32_t *)(mem + g_esp) = 1;      /* rotation too */
    g_esp -= 4; *(uint32_t *)(mem + g_esp) = mva;
    g_esp -= 4; *(uint32_t *)(mem + g_esp) = 0;      /* return address */
    g_ecx = pl;
    ICharacter_Teleport_001305D0();
    g_eax = r[0]; g_ecx = r[1]; g_edx = r[2]; g_ebx = r[3]; g_esi = r[4]; g_edi = r[5]; g_esp = r[6];
    if (health > 0.0f)
        memcpy(mem + pl + 0x90, &health, 4);
    return 1;
}

/* 1 while a level is loaded and its player exists. */
int btvs_game_in_level(char *name, size_t n)
{
    uint32_t game = g32(G_GAME);
    return game_level(name, n) && game && g32(game + 0xF0) != 0;
}

/* Level and menu changes, from whichever notices first: the wrappers (exact,
 * on the game thread) or the watcher. */
static char s_seen_level[96];
static uint32_t s_seen_menu = 0xFFFFFFFFu;

/* When the level became playable (Map_Loaded), 0 before. */
static double s_level_loaded_at;
double btvs_trail_level_loaded_at(void) { return s_level_loaded_at; }

static void note_level(const char *how)
{
    char name[96];
    game_level(name, sizeof name);
    EnterCriticalSection(&s_cs);
    if (!strcmp(name, s_seen_level)) {
        LeaveCriticalSection(&s_cs);
        return;
    }
    strcpy_s(s_seen_level, sizeof s_seen_level, name);
    LeaveCriticalSection(&s_cs);
    if (name[0])
        btvs_trail("level", "name=%s via=%s", name, how);
    else
        btvs_trail("level_left", "via=%s", how);
}

/* Menu ids seen on screen so far (the rest are numbers until someone meets them). */
static const char *menu_name(uint32_t id)
{
    switch (id) {
    case 0: return "none";
    case 1: return "main";
    case 4: return "options";
    case 5: return "load_game";
    case 8: return "display";
    case 9: return "pause";
    case 10: return "title";
    case 12: return "start_game";
    case 13: return "difficulty";
    case 15: return "resurrect";
    default: return "?";
    }
}

/* SetMenu is asked for a screen some frames before s_menuID changes (the
 * screens fade), so for a moment after a request the watcher still reads the
 * old id: it leaves menus alone for that long. */
static double s_menu_asked;

static void note_menu(uint32_t id, const char *how)
{
    int watch = !strcmp(how, "watch");
    if (id > 0x1000u)
        return;
    if (watch && btvs_uptime() - s_menu_asked < 3.0)
        return;
    if (!watch)
        s_menu_asked = btvs_uptime();
    if (id == s_seen_menu)
        return;
    s_seen_menu = id;
    btvs_trail("menu", "screen=%u name=%s via=%s", id, menu_name(id), how);
}

static void sample_player(void)
{
    float pos[3], health = 0.0f;
    int moved;
    if (!btvs_game_player(pos, &health))
        return;
    moved = fabsf(pos[0] - s_last_pos[0]) + fabsf(pos[1] - s_last_pos[1]) + fabsf(pos[2] - s_last_pos[2]) > 0.05f
            || health != s_last_health || !s_where.have_pos;
    EnterCriticalSection(&s_cs);
    memcpy(s_where.pos, pos, sizeof pos);
    s_where.health = health;
    s_where.have_pos = 1;
    LeaveCriticalSection(&s_cs);
    if (moved) {
        memcpy(s_last_pos, pos, sizeof pos);
        s_last_health = health;
        btvs_trail("pos", "x=%.2f y=%.2f z=%.2f health=%.0f", pos[0], pos[1], pos[2], health);
    }
}

static DWORD WINAPI watch_thread(LPVOID arg)
{
    (void)arg;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;) {
        double now;
        Sleep(100);
        if (!g_xbox_mem_offset || !g32(G_GAME))
            continue;
        note_level("watch");
        note_menu(g32(G_MENU_ID), "watch");
        now = btvs_uptime();
        if (now - s_pos_last >= s_pos_every) {
            s_pos_last = now;
            sample_player();
        }
    }
}

/* The wrappers. Each calls the game's function untouched and reads afterwards
 * (or, for arguments, before): they change nothing the game can see. */
void TGameManagerImplementation_Frame_000183A0(void);
void xbox_HangMonitorBeat(void);
void TManager_Buffy_Map_Loaded_0023D050(void);
void TManager_Buffy_Map_Completed_0023CC20(void);
void GUI_TManager_SetMenu_001EBE00(void);
void GUI_TBuffyManager_SetMenu_00218130(void);
void ICharacterBtvS_DieAsPlayer_00206010(void);
void ICinematicsManager_Event_CutsceneStart_001D9910(void);
void ICinematicsManager_Event_CutsceneStop_001D9A70(void);
void IAutoSave_Trigger_000ED520(void);

static void first_time(volatile LONG *flag, const char *what)
{
    if (InterlockedCompareExchange(flag, 1, 0) == 0)
        btvs_log("trail", "game hook live: %s", what);
}

/* The game loop (ExecuteGame) calls this through a vtable once a frame.
 * (TGame::ExecFrame, hooked first, is reached by a direct call: never seen here.) */
static void hook_GameFrame(void)
{
    static volatile LONG seen;
    btvs_exact_frame_begin(g_ecx);
    TGameManagerImplementation_Frame_000183A0();
    btvs_exact_frame_end();
    first_time(&seen, "TGameManagerImplementation::Frame (the once-a-frame tick)");
    xbox_HangMonitorBeat();
    btvs_frame_tick();
}

static void hook_Map_Loaded(void)
{
    static volatile LONG seen;
    TManager_Buffy_Map_Loaded_0023D050();
    first_time(&seen, "TManager_Buffy::Map_Loaded");
    note_level("Map_Loaded");
    btvs_trail("level_loaded", "name=%s", s_seen_level);
    s_level_loaded_at = btvs_uptime();
}

static void hook_Map_Completed(void)
{
    static volatile LONG seen;
    first_time(&seen, "TManager_Buffy::Map_Completed");
    /* (also called when a new game leaves the menus: no level then) */
    if (s_seen_level[0])
        btvs_trail("level_done", "name=%s", s_seen_level);
    TManager_Buffy_Map_Completed_0023CC20();
    btvs_trail("save", "why=%s", s_seen_level[0] ? "level completed" : "leaving the menus");
}

static void hook_SetMenu(void)
{
    static volatile LONG seen;
    uint32_t id = g32(g_esp + 4);
    first_time(&seen, "GUI::TManager::SetMenu");
    GUI_TManager_SetMenu_001EBE00();
    note_menu(id, "SetMenu");
}

static void hook_BuffySetMenu(void)
{
    static volatile LONG seen;
    uint32_t id = g32(g_esp + 4);
    first_time(&seen, "GUI::TBuffyManager::SetMenu");
    if (id == 10 || id <= 1)
        btvs_report_game_snap_clear();  /* (the picture kept before a pause: btvs_report_exact.c) */
    GUI_TBuffyManager_SetMenu_00218130();
    note_menu(id, "SetMenu");
}

static void hook_DieAsPlayer(void)
{
    static volatile LONG seen;
    float pos[3] = { 0 }, health = 0;
    first_time(&seen, "ICharacterBtvS::DieAsPlayer");
    btvs_game_player(pos, &health);
    btvs_trail("death", "level=%s x=%.2f y=%.2f z=%.2f", s_seen_level, pos[0], pos[1], pos[2]);
    ICharacterBtvS_DieAsPlayer_00206010();
}

/* Cutscenes running now (start/stop hooks), for the replay: it places the
 * player once none is. */
static volatile LONG s_cutscenes;
int btvs_trail_in_cutscene(void) { return s_cutscenes > 0; }

/* ICinematicsManager::DoesUserWantToSkip: a replay on its way to the report's
 * spot answers yes, so the game skips its cutscenes the way it does for the
 * player's button. */
void ICinematicsManager_DoesUserWantToSkip_001D94B0(void);
static void hook_UserWantsSkip(void)
{
    static volatile LONG seen;
    ICinematicsManager_DoesUserWantToSkip_001D94B0();
    first_time(&seen, "ICinematicsManager::DoesUserWantToSkip");
    if (btvs_replay_wants_skip())
        g_eax = (g_eax & ~0xFFu) | 1u;
}

static void hook_CutsceneStart(void)
{
    static volatile LONG seen;
    InterlockedIncrement(&s_cutscenes);
    first_time(&seen, "ICinematicsManager::Event_CutsceneStart");
    btvs_trail("cutscene", "state=start object=%08X", g32(g_esp + 4));
    ICinematicsManager_Event_CutsceneStart_001D9910();
}

static void hook_CutsceneStop(void)
{
    if (InterlockedDecrement(&s_cutscenes) < 0)
        s_cutscenes = 0;
    btvs_trail("cutscene", "state=stop object=%08X", g32(g_esp + 4));
    ICinematicsManager_Event_CutsceneStop_001D9A70();
}

static void hook_AutoSave(void)
{
    static volatile LONG seen;
    first_time(&seen, "IAutoSave::Trigger");
    btvs_trail("autosave", "level=%s", s_seen_level);
    IAutoSave_Trigger_000ED520();
}

/* Asked by recomp_lookup_manual for every indirect call: keep it this small. */
btvs_guest_fn btvs_trail_lookup(uint32_t va)
{
    switch (va) {
    case 0x000183A0u: return hook_GameFrame;
    case 0x0023D050u: return hook_Map_Loaded;
    case 0x0023CC20u: return hook_Map_Completed;
    case 0x001EBE00u: return hook_SetMenu;
    case 0x00218130u: return hook_BuffySetMenu;
    case 0x00206010u: return hook_DieAsPlayer;
    case 0x001D9910u: return hook_CutsceneStart;
    case 0x001D9A70u: return hook_CutsceneStop;
    case 0x001D94B0u: return hook_UserWantsSkip;
    case 0x000ED520u: return hook_AutoSave;
    case 0x00024B50u: return btvs_exact_hook_input_update;
    case 0x00024E90u: return btvs_exact_hook_input_poll;
    case 0x000A61E0u: return btvs_objlog_hook_object_tick;
    default: return (btvs_guest_fn)0;
    }
}

void btvs_trail_start_watch(void)
{
    HANDLE h = CreateThread(NULL, 0, watch_thread, NULL, 0, NULL);
    if (h)
        CloseHandle(h);
}

/* Once a frame on the game thread (hook_GameFrame). */
void btvs_frame_tick(void)
{
    InitOnceExecuteOnce(&s_once, init_once, NULL, NULL);
    btvs_replay_frame();
}
