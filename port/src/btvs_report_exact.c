/*
 * btvs_report_exact.c - the frame recording: what makes a replay land on the
 * same spot every time.
 *
 * Why a recording of the pad alone is not enough (measured: a replayed walk
 * ended 3 to 130 units from where the player had stood):
 *
 *  1. THE CLOCK. The game times itself with the CPU's cycle counter
 *     (TTimerManager::GetTimeStamp is a bare rdtsc). The frame's time step
 *     comes from it (TGameManagerImplementation::Frame), and so do things in
 *     the middle of a frame: how long a button has been held
 *     (TCommandManager::UpdateCommandState), how long collision may go on
 *     looking for a free place (TGO::Collision_PositionObjectInValidSpace),
 *     the object and particle ticks. No two runs read the same values.
 *  2. THE PAD. The game samples the pads from a 16 ms timer thread and once
 *     more for every millisecond of the frame (TIF_Input::Update fills up to
 *     "frame time in ms" samples): how many samples a frame gets, and in
 *     which of them a press lands, depends on that clock and on the timer.
 *  3. RANDOM NUMBERS. The C runtime's rand(), one state per thread, seeded
 *     by nothing: where it stands when the level starts depends on what ran
 *     before.
 *
 * What this file does about it, from the first frame after the level has
 * loaded ("the start") to the end of the run:
 *
 *  - the game thread reads a clock of its own (the runtime lets a thread have
 *    one): the real counter taken once at the start of each frame, plus one
 *    tick for every read in that frame. So the frame's step is the real one,
 *    and what the game does inside a frame no longer depends on how fast the
 *    PC got through it. The step of every frame is written down;
 *  - the pad samples the game used in each frame are written down (only what
 *    changed: a resting pad costs a byte a frame);
 *  - the random state at the start is written down;
 *  - every few frames the player's position and the random state are written
 *    down too, as a check.
 *
 * A replay (BTVS_REPLAY with BTVS_REPLAY_INPUT=1) gives all of it back: the
 * same steps, the same samples in the same frames (the timer thread's own
 * sampling is held off meanwhile), the same random state, and compares the
 * checks as it goes: the log says at which frame the player first stood
 * somewhere else, if ever.
 *
 * What is still not the same from run to run: how many random numbers a frame
 * draws. Measured: the player's path came back bit for bit, but from a few
 * seconds in (during the opening cutscene) the random state after a frame
 * differed, at another frame in each replay; something outside the game
 * thread's clock (sound, streaming) decides when some effect draws its
 * numbers. So the recording keeps the random state after EVERY frame and a
 * replay puts it back after every frame: a difference lasts one frame at
 * most. The log counts how often that was needed.
 *
 * File (btvs_frames.rec beside the exe, frames.rec in a report):
 *   "BTVSFR2\n", u32 random state at the start, then one record per frame:
 *     varint  clock ticks since the previous frame began (1 tick = 16 cycles)
 *     u8      flags: 1 a check follows, 2 the game was paused, 4 the random state follows
 *     u8      pad samples in this frame (0: the game did not read the pads)
 *     u16     which devices (bit = class * 4 + port)
 *     per device: tokens until every sample is covered:
 *        t < 0x80   t + 1 samples the same as the one before
 *        t >= 0x80  one sample with (t & 0x7F) changed 32-bit words: (u8 index, u32 value) each
 *     u32     the random state after this frame
 *     check:  3 floats (the player's position)
 *
 * BTVS_EXACT=0 turns all of it off (the game reads the real clock, nothing is
 * written, replays play the pad recording by time). BTVS_EXACT_CHECK=1 writes
 * a check every frame instead of every 8th.
 */
#include <windows.h>
#include <math.h>
#include <share.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xbox/xboxrecomp.h>
#include "btvs_log.h"

extern ptrdiff_t g_xbox_mem_offset;
extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
#define M8(va)  ((uint8_t *)(uintptr_t)g_xbox_mem_offset + (uint32_t)(va))
#define M32(va) (*(uint32_t *)M8(va))

uint64_t xbox_ReadTimeStampCounterReal(void);
void xbox_SetThreadTimeStampSource(uint64_t (*source)(void));

void TIF_Input_Implement_Update_00024B50(void);
void TIF_Input_Poll_00024E90(void);
void getptd_00582A77(void);

/* The game's input interface (TIF_Input): three classes of device, each a
 * count and an array of device pointers; a device keeps up to 60 samples of
 * 0x100 bytes and how many it has. */
#define IF_USED      0xBCu      /* samples the last Update used */
#define IF_COUNT     0xC0u      /* samples gathered for the next */
#define IF_CLASSES   0x938u     /* 3 x { count, device**, ? } */
#define DEV_SAMPLES  0x10u
#define DEV_COUNT    0x3C10u
#define SAMPLE_BYTES 0x100
#define MAX_SAMPLES  60
#define SLOTS        12
#define G_PAUSE      0x007F08F0u    /* IPauseScreen::Get() */
#define PAUSE_OPEN   0x274u

enum { X_OFF, X_WAIT, X_RECORD, X_REPLAY, X_LIVE };

struct frame_rec {
    uint32_t delta;
    int flags, count;
    uint32_t slots;
    float pos[3];
    uint32_t seed;
};

static struct {
    volatile LONG mode;
    int checked_env, check_every;
    CRITICAL_SECTION cs;
    int cs_ready;
    FILE *f;
    char path[MAX_PATH];
    volatile LONG frames;           /* frames written (recording) or given back (replay) since the start */
    volatile LONG frames_unpaused;  /* recording: frames up to the last one the game ended unpaused */
    uint32_t clock, reads, offset;  /* the game thread's clock: this frame's base, reads since, real + offset when live */
    uint32_t seed_va;
    DWORD game_tid;
    uint32_t if_va;
    int in_frame, have_input, was_paused;
    struct frame_rec cur;
    /* replay */
    uint8_t *data;
    size_t size, pos;
    uint32_t limit, file_seed;
    int want_replay, replay_done, diverged;
    uint32_t first_bad;
    float max_dev, last_dev;
    uint32_t seed_fixes;
    uint32_t checks;
    int warned_slot;
    float end_pos[3], end_pos_unpaused[3];  /* the player after the last frame (recording / replay) */
    int have_end_pos;
    CRITICAL_SECTION poll_cs;       /* the timer thread's sampling stays out while a frame's samples are copied */
} X;

static uint8_t s_prev[SLOTS][SAMPLE_BYTES];
static uint8_t s_smp[SLOTS][MAX_SAMPLES][SAMPLE_BYTES];

/* ── the clock ──────────────────────────────────────────────────────────── */

static uint32_t real_clock(void) { return (uint32_t)(xbox_ReadTimeStampCounterReal() >> 4); }

/* What the game thread's rdtsc returns from the start on: the game takes
 * bits 4..35 of it as its clock. */
static uint64_t game_tsc(void)
{
    return (uint64_t)(uint32_t)(X.clock + X.reads++) << 4;
}

/* ── small helpers ──────────────────────────────────────────────────────── */

static int paused_now(void)
{
    uint32_t p = M32(G_PAUSE);
    return p > 0x10000u && *M8(p + PAUSE_OPEN) != 0;
}

static uint32_t slot_device(uint32_t ifc, int slot)
{
    uint32_t cls = ifc + IF_CLASSES + (uint32_t)(slot / 4) * 12u;
    uint32_t n = M32(cls), arr = M32(cls + 4);
    if ((uint32_t)(slot % 4) >= n || arr < 0x10000u)
        return 0;
    return M32(arr + (uint32_t)(slot % 4) * 4u);
}

static void put_varint(uint8_t **p, uint32_t v)
{
    while (v >= 0x80u) { *(*p)++ = (uint8_t)(v | 0x80u); v >>= 7; }
    *(*p)++ = (uint8_t)v;
}

static int get_varint(uint32_t *v)
{
    uint32_t r = 0;
    int shift = 0;
    while (X.pos < X.size && shift < 35) {
        uint8_t b = X.data[X.pos++];
        r |= (uint32_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) { *v = r; return 1; }
        shift += 7;
    }
    return 0;
}

/* The game thread's rand() state: _getptd()->_holdrand. */
static uint32_t find_seed(void)
{
    uint32_t r[7], ptd;
    uint8_t *mem = (uint8_t *)(uintptr_t)g_xbox_mem_offset;
    r[0] = g_eax; r[1] = g_ecx; r[2] = g_edx; r[3] = g_ebx; r[4] = g_esi; r[5] = g_edi; r[6] = g_esp;
    g_esp = ((g_esp - 1024) & ~15u) - 4;
    *(uint32_t *)(mem + g_esp) = 0;             /* return address */
    getptd_00582A77();
    ptd = g_eax;
    g_eax = r[0]; g_ecx = r[1]; g_edx = r[2]; g_ebx = r[3]; g_esi = r[4]; g_edi = r[5]; g_esp = r[6];
    return ptd > 0x10000u ? ptd + 0x14u : 0;
}

static void env_once(void)
{
    const char *e;
    if (X.checked_env)
        return;
    X.checked_env = 1;
    InitializeCriticalSection(&X.cs);
    InitializeCriticalSection(&X.poll_cs);
    X.cs_ready = 1;
    e = getenv("BTVS_EXACT");
    X.mode = (e && *e == '0') ? X_OFF : X_WAIT;
    e = getenv("BTVS_EXACT_CHECK");
    X.check_every = (e && *e && *e != '0') ? 1 : 8;
    sprintf_s(X.path, MAX_PATH, "%s\\" BTVS_FRAMES_FILE, btvs_exe_dir());
    if (X.mode == X_OFF)
        btvs_log("exact", "BTVS_EXACT=0: the game reads the real clock and no frame recording is made");
    else if (!X.want_replay)
        DeleteFileA(X.path);            /* (the last run's) */
}

/* ── replay: one frame's record out of the file ─────────────────────────── */

static int read_frame(struct frame_rec *r)
{
    int slot, i;
    if (!get_varint(&r->delta) || X.pos + 4 > X.size)
        return 0;
    r->flags = X.data[X.pos++];
    r->count = X.data[X.pos++];
    r->slots = X.data[X.pos] | (uint32_t)X.data[X.pos + 1] << 8;
    X.pos += 2;
    if (r->count > MAX_SAMPLES)
        return 0;
    for (slot = 0; slot < SLOTS; slot++) {
        if (!(r->slots >> slot & 1))
            continue;
        for (i = 0; i < r->count;) {
            uint8_t t;
            if (X.pos >= X.size)
                return 0;
            t = X.data[X.pos++];
            if (t < 0x80) {
                int n = t + 1;
                while (n-- > 0 && i < r->count)
                    memcpy(s_smp[slot][i++], s_prev[slot], SAMPLE_BYTES);
            } else {
                int n = t & 0x7F;
                if (X.pos + (size_t)n * 5 > X.size)
                    return 0;
                while (n-- > 0) {
                    uint8_t idx = X.data[X.pos];
                    if (idx < SAMPLE_BYTES / 4)
                        memcpy(s_prev[slot] + idx * 4, X.data + X.pos + 1, 4);
                    X.pos += 5;
                }
                memcpy(s_smp[slot][i++], s_prev[slot], SAMPLE_BYTES);
            }
        }
    }
    if (r->flags & 4) {
        if (X.pos + 4 > X.size)
            return 0;
        memcpy(&r->seed, X.data + X.pos, 4);
        X.pos += 4;
    }
    if (r->flags & 1) {
        if (X.pos + 12 > X.size)
            return 0;
        memcpy(r->pos, X.data + X.pos, 12);
        X.pos += 12;
    }
    return 1;
}

static void replay_over(const char *why)
{
    X.replay_done = 1;
    /* The clock goes on from where the replay left it, at the real rate. */
    X.offset = X.clock + 1u + X.reads - real_clock();
    InterlockedExchange(&X.mode, X_LIVE);
    btvs_log("exact", "frame replay over after %ld frames (%s): %s", X.frames, why,
             X.diverged ? "the player's path DIFFERED from the recording (see above)"
             : X.checks ? "the player was on the recorded place at every check" : "the recording had no checks to compare");
    if (X.checks)
        btvs_log("exact", "  %u position checks; furthest from the recorded position: %.6f units; at the last check: %.6f; "
                 "random state put back after %u of the frames",
                 X.checks, X.max_dev, X.last_dev, X.seed_fixes);
}

/* ── once a frame, around the game's own frame function ─────────────────── */

static void start(uint32_t mgr)
{
    X.game_tid = GetCurrentThreadId();
    X.seed_va = find_seed();
    X.clock = M32(mgr + 0x4C);          /* the clock value the game's last frame began at */
    if (!X.clock)
        X.clock = real_clock();
    X.reads = 0;
    X.offset = 0;
    memset(s_prev, 0, sizeof s_prev);
    if (X.want_replay && X.data) {
        if (X.seed_va)
            M32(X.seed_va) = X.file_seed;
        InterlockedExchange(&X.mode, X_REPLAY);
        btvs_log("exact", "the level is up: giving the game the recording's %u frames (clock, pad samples, random state %08X)",
                 X.limit, X.file_seed);
        btvs_trail("exact_replay", "frames=%u", X.limit);
    } else {
        uint32_t seed = X.seed_va ? M32(X.seed_va) : 0;
        EnterCriticalSection(&X.cs);
        X.f = _fsopen(X.path, "wb", _SH_DENYWR);    /* (a report copies it while it grows) */
        if (X.f) {
            fwrite("BTVSFR2\n", 1, 8, X.f);
            fwrite(&seed, 4, 1, X.f);
        }
        LeaveCriticalSection(&X.cs);
        InterlockedExchange(&X.mode, X.f ? X_RECORD : X_LIVE);
        btvs_log("exact", X.f ? "the level is up: frame recording on (%s), random state %08X"
                 : "the frame recording could not be opened (%s): none this run", X.path, seed);
    }
    xbox_SetThreadTimeStampSource(game_tsc);
}

void btvs_exact_frame_begin(uint32_t mgr)
{
    env_once();
    if (X.mode == X_OFF)
        return;
    if (X.mode == X_WAIT) {
        if (btvs_trail_level_loaded_at() <= 0.0 || !g_xbox_mem_offset)
            return;
        start(mgr);
    }
    X.in_frame = 1;
    X.have_input = 0;
    memset(&X.cur, 0, sizeof X.cur);
    X.was_paused = paused_now();
    if (X.mode == X_REPLAY) {
        if ((uint32_t)X.frames >= X.limit)
            replay_over("the report was made here");
        else if (!read_frame(&X.cur))
            replay_over("the recording ends here");
        else {
            X.clock += X.cur.delta;
            X.reads = 0;
            return;
        }
    }
    {   /* recording, or live after a replay: the real clock, taken once */
        uint32_t now = real_clock() + X.offset, d = now - X.clock;
        if ((int32_t)d <= (int32_t)X.reads)
            d = X.reads + 1u;           /* (never backwards) */
        X.cur.delta = d;
        X.clock += d;
        X.reads = 0;
    }
}

static void write_frame(void)
{
    static uint8_t buf[16 + SLOTS * (MAX_SAMPLES * (1 + 64 * 5))];
    uint8_t *p = buf;
    int slot, i, k;
    put_varint(&p, X.cur.delta);
    *p++ = (uint8_t)X.cur.flags;
    *p++ = (uint8_t)X.cur.count;
    *p++ = (uint8_t)X.cur.slots;
    *p++ = (uint8_t)(X.cur.slots >> 8);
    for (slot = 0; slot < SLOTS; slot++) {
        int run = 0;
        if (!(X.cur.slots >> slot & 1))
            continue;
        for (i = 0; i < X.cur.count; i++) {
            const uint32_t *a = (const uint32_t *)s_smp[slot][i];
            uint32_t *b = (uint32_t *)s_prev[slot];
            uint8_t *tok;
            int n = 0;
            if (!memcmp(a, b, SAMPLE_BYTES)) {
                if (++run == 0x80) { *p++ = 0x7F; run = 0; }
                continue;
            }
            if (run) { *p++ = (uint8_t)(run - 1); run = 0; }
            tok = p++;
            for (k = 0; k < SAMPLE_BYTES / 4; k++)
                if (a[k] != b[k]) {
                    *p++ = (uint8_t)k;
                    memcpy(p, &a[k], 4);
                    p += 4;
                    b[k] = a[k];
                    n++;
                }
            *tok = (uint8_t)(0x80 | n);
        }
        if (run)
            *p++ = (uint8_t)(run - 1);
    }
    if (X.cur.flags & 4) {
        memcpy(p, &X.cur.seed, 4);
        p += 4;
    }
    if (X.cur.flags & 1) {
        memcpy(p, X.cur.pos, 12);
        p += 12;
    }
    EnterCriticalSection(&X.cs);
    if (X.f) {
        fwrite(buf, 1, (size_t)(p - buf), X.f);
        InterlockedIncrement(&X.frames);
        if (!paused_now() && !X.was_paused) {
            X.frames_unpaused = X.frames;
            memcpy(X.end_pos_unpaused, X.end_pos, 12);
        }
        if ((X.frames & 63) == 0)
            fflush(X.f);
    }
    LeaveCriticalSection(&X.cs);
}

void btvs_exact_frame_end(void)
{
    float pm[12], cm[12], hp;
    uint32_t seed;
    int have;
    if (!X.in_frame)
        return;
    X.in_frame = 0;
    if (X.mode != X_RECORD && X.mode != X_REPLAY)
        return;
    have = btvs_game_state(pm, cm, &hp);
    seed = X.seed_va ? M32(X.seed_va) : 0;
    if (have) {
        EnterCriticalSection(&X.cs);
        memcpy(X.end_pos, pm + 9, 12);
        X.have_end_pos = 1;
        LeaveCriticalSection(&X.cs);
    }
    if (X.mode == X_RECORD) {
        if (have && X.frames % X.check_every == 0) {
            X.cur.flags |= 1;
            memcpy(X.cur.pos, pm + 9, 12);
        }
        if (X.seed_va) {
            X.cur.flags |= 4;
            X.cur.seed = seed;
        }
        if (X.was_paused)
            X.cur.flags |= 2;
        write_frame();
        return;
    }
    if ((X.cur.flags & 4) && X.seed_va && seed != X.cur.seed) {
        /* Some effect drew more or fewer random numbers than when recorded:
         * the next frame starts from the recorded state all the same. */
        if (!X.seed_fixes++)
            btvs_log("exact", "frame %ld (%.1f s after the level came up): the random state after the frame was %08X, recorded %08X; "
                     "put back to the recorded one (and after every frame where it differs: counted at the end)",
                     X.frames, btvs_uptime() - btvs_trail_level_loaded_at(), seed, X.cur.seed);
        M32(X.seed_va) = X.cur.seed;
    }
    if (X.cur.flags & 1) {
        float d = -1.0f;
        if (have)
            d = sqrtf((pm[9] - X.cur.pos[0]) * (pm[9] - X.cur.pos[0]) + (pm[10] - X.cur.pos[1]) * (pm[10] - X.cur.pos[1])
                      + (pm[11] - X.cur.pos[2]) * (pm[11] - X.cur.pos[2]));
        X.checks++;
        X.last_dev = d;
        if (d > X.max_dev || d < 0.0f)
            X.max_dev = d < 0.0f ? 1e9f : d;
        if (d != 0.0f && !X.diverged) {
            X.diverged = 1;
            X.first_bad = (uint32_t)X.frames;
            btvs_log("exact", "DIFFERS from the recording at frame %ld (%.1f s after the level came up): the player is %.6f units from "
                     "the recorded place (%.3f %.3f %.3f, recorded %.3f %.3f %.3f)",
                     X.frames, btvs_uptime() - btvs_trail_level_loaded_at(), d, have ? pm[9] : 0.0f, have ? pm[10] : 0.0f,
                     have ? pm[11] : 0.0f, X.cur.pos[0], X.cur.pos[1], X.cur.pos[2]);
            btvs_trail("exact_differs", "frame=%ld units=%.4f", X.frames, d);
        }
    }
    InterlockedIncrement(&X.frames);
}

/* ── the pads ───────────────────────────────────────────────────────────── */

/* THE PICTURE BEFORE THE PAUSE MENU. While the game is paused its camera is
 * not in the level at all (IPauseScreen::SetOpen moves it to the menu's own
 * little scene), so once the pause menu is up there is no picture of the game
 * left to take. The last frame of play has to be kept before the pause opens:
 * a Start press made in play is held back for one frame, in the samples as
 * the game gathers them. That frame is still play, and it is the one a
 * report made from the pause menu gets as its picture; the frame after, the
 * game sees Start and pauses. One frame later than pressed: 1/60 s.
 * (Start also skips cutscenes: the same happens there, harmlessly.) */
#define SAMPLE_START 0x38u      /* float: 1.0 while Start is down */
static struct {
    uint32_t updates;           /* Updates done: samples gathered now are used by Update number `updates` */
    int holding, slot;
    uint32_t at;                /* the Update the press would have been used by */
    uint8_t was_down[SLOTS];
} P;

static void start_filter(uint32_t ifc)
{
    int slot;
    for (slot = 0; slot < SLOTS; slot++) {
        uint32_t dev = slot_device(ifc, slot), n, smp;
        int down;
        if (!dev || (n = M32(dev + DEV_COUNT)) == 0 || n > MAX_SAMPLES)
            continue;
        smp = dev + DEV_SAMPLES + (n - 1u) * SAMPLE_BYTES;
        down = M32(smp + SAMPLE_START) != 0;
        if (down && !P.was_down[slot] && !P.holding && btvs_trail_level_loaded_at() > 0.0 && !paused_now()) {
            P.holding = 1;
            P.slot = slot;
            P.at = P.updates;
            btvs_report_game_snap();    /* the next frame shown: still the game */
            btvs_log("exact", "Start pressed in play (device %d): held back one frame, that frame kept as the picture for a report", slot);
        }
        P.was_down[slot] = (uint8_t)down;
        if (P.holding && P.slot == slot) {
            if (P.updates == P.at)
                M32(smp + SAMPLE_START) = 0;                /* not yet */
            else if (P.updates == P.at + 1u)
                M32(smp + SAMPLE_START) = 0x3F800000u;      /* now, even if already let go */
            else
                P.holding = 0;
        }
    }
}

/* TIF_Input_Implement::Update(long ms), the game's once-a-frame "use the
 * samples" (thiscall, ret 4; called through the vtable from Frame). */
void btvs_exact_hook_input_update(void)
{
    uint32_t ifc = g_ecx;
    int slot, c;
    X.if_va = ifc;
    if (X.mode == X_REPLAY && GetCurrentThreadId() == X.game_tid) {
        /* Give the game this frame's samples; it must not gather its own
         * (ms = 1 with at least one sample there: its fill loop does nothing). */
        int from_file = X.in_frame && !X.have_input && X.cur.count > 0;
        c = from_file ? X.cur.count : 1;
        for (slot = 0; slot < SLOTS; slot++) {
            uint32_t dev = slot_device(ifc, slot);
            int in_file = from_file ? (int)(X.cur.slots >> slot & 1) : 1;
            if (!dev) {
                if (from_file && in_file && !X.warned_slot) {
                    X.warned_slot = 1;
                    btvs_log("exact", "the recording has a pad in a port (%d) that has none now: left out", slot);
                }
                continue;
            }
            if (from_file && in_file)
                memcpy(M8(dev + DEV_SAMPLES), s_smp[slot], (size_t)c * SAMPLE_BYTES);
            else {
                int i;
                for (i = 0; i < c; i++)
                    memcpy(M8(dev + DEV_SAMPLES + (uint32_t)i * SAMPLE_BYTES), s_prev[slot], SAMPLE_BYTES);
            }
            M32(dev + DEV_COUNT) = (uint32_t)c;
        }
        M32(ifc + IF_COUNT) = (uint32_t)c;
        M32(g_esp + 4) = 1;
        X.have_input = 1;
        TIF_Input_Implement_Update_00024B50();
        return;
    }
    if (!X.cs_ready) {
        TIF_Input_Implement_Update_00024B50();
        return;
    }
    EnterCriticalSection(&X.poll_cs);
    TIF_Input_Implement_Update_00024B50();
    P.updates++;
    if (X.mode == X_RECORD && GetCurrentThreadId() == X.game_tid && X.in_frame && !X.have_input) {
        c = (int)M32(ifc + IF_USED);
        if (c < 0 || c > MAX_SAMPLES)
            c = 0;
        X.cur.count = c;
        X.cur.slots = 0;
        for (slot = 0; slot < SLOTS && c; slot++) {
            uint32_t dev = slot_device(ifc, slot);
            if (!dev)
                continue;
            X.cur.slots |= 1u << slot;
            memcpy(s_smp[slot], M8(dev + DEV_SAMPLES), (size_t)c * SAMPLE_BYTES);
        }
        X.have_input = 1;
    }
    LeaveCriticalSection(&X.poll_cs);
}

/* TIF_Input::Poll (thiscall, no arguments): one more sample from every pad.
 * The 16 ms timer thread calls it, and Update does to fill a frame up. While
 * a replay gives the samples back nobody gathers any. */
void btvs_exact_hook_input_poll(void)
{
    if (X.mode == X_REPLAY) {
        g_esp += 4;
        return;
    }
    if (!X.cs_ready) {
        TIF_Input_Poll_00024E90();
        return;
    }
    {
        uint32_t ifc = g_ecx, before;
        EnterCriticalSection(&X.poll_cs);
        before = M32(ifc + IF_COUNT);
        TIF_Input_Poll_00024E90();
        if (M32(ifc + IF_COUNT) != before)      /* (it gathers nothing once 60 samples wait) */
            start_filter(ifc);
        LeaveCriticalSection(&X.poll_cs);
    }
}

/* ── for the report and the replay ──────────────────────────────────────── */

/* Copies the recording made so far to `dst`. *frames: how many of its frames
 * a replay should give back: all of them, or, while the game is paused, those
 * up to the last one before the pause; pos: where the player stood after the
 * last of them. 0 = there is none. Any thread. */
int btvs_exact_snapshot(const char *dst, unsigned *frames, float pos[3])
{
    int ok = 0;
    *frames = 0;
    if (!X.cs_ready || X.mode != X_RECORD)
        return 0;
    EnterCriticalSection(&X.cs);
    if (X.f) {
        fflush(X.f);
        ok = CopyFileA(X.path, dst, FALSE) != 0;
        if (paused_now()) {
            *frames = (unsigned)X.frames_unpaused;
            memcpy(pos, X.end_pos_unpaused, 12);
        } else {
            *frames = (unsigned)X.frames;
            memcpy(pos, X.end_pos, 12);
        }
    }
    LeaveCriticalSection(&X.cs);
    return ok && *frames > 0;
}

/* A replay will give this recording back, `frames` frames of it, once the
 * level is up. Before the game starts. */
int btvs_exact_replay(const char *path, unsigned frames)
{
    FILE *f;
    long n;
    env_once();
    if (X.mode == X_OFF || fopen_s(&f, path, "rb") != 0)
        return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 12 || !(X.data = (uint8_t *)malloc((size_t)n)) || fread(X.data, 1, (size_t)n, f) != (size_t)n
        || memcmp(X.data, "BTVSFR2\n", 8) != 0) {
        fclose(f);
        free(X.data);
        X.data = NULL;
        return 0;
    }
    fclose(f);
    memcpy(&X.file_seed, X.data + 8, 4);
    X.size = (size_t)n;
    X.pos = 12;
    X.limit = frames;
    X.want_replay = 1;
    return 1;
}

/* 0 no frame replay, 1 waiting for the level, 2 giving frames back, 3 over. */
int btvs_exact_replay_state(void)
{
    if (!X.want_replay)
        return 0;
    return X.replay_done ? 3 : X.mode == X_REPLAY ? 2 : 1;
}

/* After it is over: 1 if every check matched. */
int btvs_exact_replay_result(unsigned *frames, unsigned *checks, float *max_dev, unsigned *first_bad, float pos[3])
{
    memcpy(pos, X.end_pos, 12);
    *frames = (unsigned)X.frames;
    *checks = X.checks;
    *max_dev = X.max_dev;
    *first_bad = X.first_bad;
    return X.replay_done && !X.diverged;
}

int btvs_exact_recording(void) { return X.mode == X_RECORD; }
int btvs_game_paused(void) { return g_xbox_mem_offset && paused_now(); }
unsigned btvs_exact_replay_seed_fixes(void) { return X.seed_fixes; }
