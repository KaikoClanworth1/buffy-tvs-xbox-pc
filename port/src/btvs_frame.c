/**
 * The frame: shown, counted and paced at the game's D3DDevice_Swap.
 *
 * The Xbox paced the game by its display: Swap waited for the TV's vertical
 * blank. Here the frame limit is the player's ([display] fps_limit, 60 by
 * default; 0 none) and, with vsync on, the monitor's refresh. BTVS_UNCAPPED=1
 * turns the limiter off (benchmarks).
 *
 * Frame times are always measured ("frame statistics" below): once a minute,
 * and when the game ends, the log gets how the frames of that time were
 * spread and what the slow ones were spent on. BTVS_FRAME_LOG=<file> writes
 * every frame.
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "recomp_types.h"
#include "btvs.h"
#include "btvs_native.h"

void D3DDevice_Swap_00595DB0_orig(void);
void btvs_shot_frame(void);

static LARGE_INTEGER s_freq, s_t0;
static unsigned long s_frames;
static float s_fps;
static double s_frame_ms_avg, s_frame_ms_max;

static double now_s(void)
{
    LARGE_INTEGER q;
    if (!s_freq.QuadPart) {
        QueryPerformanceFrequency(&s_freq);
        QueryPerformanceCounter(&s_t0);
        timeBeginPeriod(1);
    }
    QueryPerformanceCounter(&q);
    return (double)(q.QuadPart - s_t0.QuadPart) / (double)s_freq.QuadPart;
}

double btvs_frame_seconds(void) { return now_s(); }
unsigned long btvs_frame_count(void) { return s_frames; }
float btvs_fps(void) { return s_fps; }

/* ── frame statistics ──────────────────────────────────────────────────
 * A frame is from one Swap to the next. Its time is split into what the port
 * can tell apart: textures and shaders made during it (the runtime counts
 * them), showing the frame (Present: with vsync, the wait for the monitor),
 * the limiter sleeping longer than it meant to, the port's own work at the
 * Swap (its text, a frame dump, the window's messages), and the rest: the
 * game's own code and its drawing. A frame is "slow" when it took more than
 * 1.25 steps of the frame limit; each slow frame is put down to its largest
 * part. */
enum { FS_GAME, FS_PRESENT, FS_TEX, FS_SHADER, FS_OVERSLEEP, FS_PORT, FS_PARTS };
static const char *const k_part[FS_PARTS] = { "game", "showing", "textures", "shaders", "limiter overslept", "port" };

#define FS_BINS 7
static const double k_bin[FS_BINS - 1] = { 1.1, 1.25, 1.5, 2.0, 3.0, 6.0 };   /* frame time over the step */
#define FS_WORST 5

typedef struct {
    unsigned n, bin[FS_BINS], slow_by[FS_PARTS];
    double sum_ms, max_ms, since;
    double cpu_sum_ms, cpu_max_ms;     /* the game thread's own working time in a frame (not waiting, not pushed aside) */
    unsigned cpu_over;                 /* frames in which that alone was more than a step */
    double proc_cpu0;                  /* the whole program's CPU seconds when this began */
    struct { double ms, at, part[FS_PARTS]; } worst[FS_WORST];
} FrameStats;

static FrameStats s_fs_minute, s_fs_all;
static double s_fs_step_ms = 1000.0 / 60.0;
static FILE *s_fs_file;
static double s_fs_present_ms, s_fs_port_ms, s_fs_pace_ms, s_fs_oversleep_ms;   /* of the Swap being done */

extern double   g_nv2a_stat_tex_ms, g_gpu_stat_shader_ms;       /* the runtime's counts since the start */
extern unsigned g_nv2a_stat_tex_n, g_gpu_stat_shader_n;

/* CPU seconds the whole program has used. */
static double proc_cpu_s(void)
{
    FILETIME c, e, k, u;
    ULARGE_INTEGER a, b;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u))
        return 0.0;
    a.LowPart = k.dwLowDateTime; a.HighPart = k.dwHighDateTime;
    b.LowPart = u.dwLowDateTime; b.HighPart = u.dwHighDateTime;
    return (double)(a.QuadPart + b.QuadPart) / 1e7;
}

/* Milliseconds of work the calling thread has done (its cycle count over the
 * processor's speed): unlike the clock it does not grow while the thread
 * waits or another program has the CPU, so it says what a frame costs
 * whatever else the PC is doing. */
static double thread_cpu_ms(void)
{
    static double per_ms;
    ULONG64 c = 0;
    if (per_ms == 0.0) {
        DWORD mhz = 0, n = sizeof mhz;
        HKEY h;
        if (!RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &h)) {
            RegQueryValueExA(h, "~MHz", NULL, NULL, (BYTE *)&mhz, &n);
            RegCloseKey(h);
        }
        per_ms = mhz ? (double)mhz * 1000.0 : 3.0e6;
    }
    QueryThreadCycleTime(GetCurrentThread(), &c);
    return (double)c / per_ms;
}

static void fs_add(FrameStats *f, double ms, double cpu_ms, const double part[FS_PARTS], double at)
{
    double ratio = ms / s_fs_step_ms;
    int b = 0, i, k;
    while (b < FS_BINS - 1 && ratio > k_bin[b])
        b++;
    if (!f->n) {
        f->since = at;
        f->proc_cpu0 = proc_cpu_s();
    }
    f->cpu_sum_ms += cpu_ms;
    if (cpu_ms > f->cpu_max_ms)
        f->cpu_max_ms = cpu_ms;
    if (cpu_ms > s_fs_step_ms)
        f->cpu_over++;
    f->n++;
    f->bin[b]++;
    f->sum_ms += ms;
    if (ms > f->max_ms)
        f->max_ms = ms;
    if (b >= 2) {
        k = 0;
        for (i = 1; i < FS_PARTS; i++)
            if (part[i] > part[k])
                k = i;
        f->slow_by[k]++;
    }
    for (i = 0; i < FS_WORST; i++)
        if (ms > f->worst[i].ms) {
            memmove(&f->worst[i + 1], &f->worst[i], (size_t)(FS_WORST - 1 - i) * sizeof f->worst[0]);
            f->worst[i].ms = ms;
            f->worst[i].at = at;
            memcpy(f->worst[i].part, part, sizeof f->worst[i].part);
            break;
        }
}

static void fs_report(const FrameStats *f, const char *what, double now)
{
    unsigned slow = 0;
    int i;
    if (!f->n)
        return;
    for (i = 2; i < FS_BINS; i++)
        slow += f->bin[i];
    btvs_log("gfx", "frames, %s (%.0f s): %u, average %.2f ms, longest %.1f ms. Against the %.2f ms step: "
             "on time %u, to 1.25x %u, to 1.5x %u, to 2x %u, to 3x %u, to 6x %u, longer %u",
             what, now - f->since, f->n, f->sum_ms / f->n, f->max_ms, s_fs_step_ms,
             f->bin[0], f->bin[1], f->bin[2], f->bin[3], f->bin[4], f->bin[5], f->bin[6]);
    btvs_log("gfx", "  the game thread's own work: %.2f ms a frame, most %.1f ms, over a step in %u frames; the program used %.2f CPUs",
             f->cpu_sum_ms / f->n, f->cpu_max_ms, f->cpu_over,
             now > f->since ? (proc_cpu_s() - f->proc_cpu0) / (now - f->since) : 0.0);
    if (!slow)
        return;
    btvs_log("gfx", "  %u slow (over 1.25x), by the largest part: %s %u, %s %u, %s %u, %s %u, %s %u, %s %u", slow,
             k_part[0], f->slow_by[0], k_part[1], f->slow_by[1], k_part[2], f->slow_by[2],
             k_part[3], f->slow_by[3], k_part[4], f->slow_by[4], k_part[5], f->slow_by[5]);
    for (i = 0; i < FS_WORST && f->worst[i].ms > s_fs_step_ms * 1.25; i++)
        btvs_log("gfx", "  %.1f ms at %.1f s: %s %.1f, %s %.1f, %s %.1f, %s %.1f, %s %.1f, %s %.1f",
                 f->worst[i].ms, f->worst[i].at,
                 k_part[0], f->worst[i].part[0], k_part[1], f->worst[i].part[1], k_part[2], f->worst[i].part[2],
                 k_part[3], f->worst[i].part[3], k_part[4], f->worst[i].part[4], k_part[5], f->worst[i].part[5]);
}

static void fs_quit(void)
{
    double t = now_s();
    fs_report(&s_fs_minute, "since the last report", t);
    fs_report(&s_fs_all, "the whole run", t);
    if (s_fs_file)
        fclose(s_fs_file);
    s_fs_file = NULL;
}

/* At a Swap's entry: the frame that ends here. */
static void fs_frame(double t)
{
    static double last, tex0, sh0, cpu0;
    double cpu = thread_cpu_ms();
    static int started;
    static double every = 60.0;                  /* BTVS_FRAME_REPORT=<seconds>: how often the log gets the report */
    double part[FS_PARTS], ms;
    if (!started) {
        const char *e = getenv("BTVS_FRAME_LOG");
        started = 1;
        btvs_on_quit(fs_quit);
        if (getenv("BTVS_FRAME_REPORT") && atof(getenv("BTVS_FRAME_REPORT")) >= 1.0)
            every = atof(getenv("BTVS_FRAME_REPORT"));
        if (e && *e && !fopen_s(&s_fs_file, e, "w") && s_fs_file)
            fprintf(s_fs_file, "seconds,frame_ms,game,showing,textures,shaders,limiter_overslept,port,limiter_ms,thread_cpu_ms\n");
    }
    if (last > 0.0) {
        ms = (t - last) * 1000.0;
        part[FS_PRESENT] = s_fs_present_ms;
        part[FS_TEX] = g_nv2a_stat_tex_ms - tex0;
        part[FS_SHADER] = g_gpu_stat_shader_ms - sh0;
        part[FS_OVERSLEEP] = s_fs_oversleep_ms;
        part[FS_PORT] = s_fs_port_ms;
        part[FS_GAME] = ms - s_fs_present_ms - s_fs_pace_ms - s_fs_port_ms - part[FS_TEX] - part[FS_SHADER];
        if (part[FS_GAME] < 0.0)
            part[FS_GAME] = 0.0;
        fs_add(&s_fs_minute, ms, cpu - cpu0, part, t);
        fs_add(&s_fs_all, ms, cpu - cpu0, part, t);
        if (s_fs_file)
            fprintf(s_fs_file, "%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", t, ms, part[FS_GAME], part[FS_PRESENT],
                    part[FS_TEX], part[FS_SHADER], part[FS_OVERSLEEP], part[FS_PORT], s_fs_pace_ms, cpu - cpu0);
        if (t - s_fs_minute.since >= every) {
            fs_report(&s_fs_minute, "the last minute", t);
            memset(&s_fs_minute, 0, sizeof s_fs_minute);
        }
    }
    last = t;
    cpu0 = cpu;
    tex0 = g_nv2a_stat_tex_ms;
    sh0 = g_gpu_stat_shader_ms;
}

/* ── pacing ────────────────────────────────────────────────────────────── */

/* Sleep for `s` seconds, closely. Sleep() works in steps of the system's
 * timer, 1 ms at best and only when Windows grants this program's request
 * for that (it does not for a window that is hidden or covered: Sleep then
 * runs up to 15 ms over, seen as "limiter overslept" in the frame
 * statistics). A high-resolution waitable timer does not depend on it;
 * Sleep is what is left on a Windows without one. */
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
static void wait_s(double s)
{
    static HANDLE timer;
    static int tried;
    LARGE_INTEGER due;
    if (!tried) {
        tried = 1;
        timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        btvs_log("gfx", "frame limiter: %s", timer ? "high-resolution timer" : "Sleep (no high-resolution timer on this Windows)");
    }
    due.QuadPart = -(LONGLONG)(s * 1e7);
    if (timer && due.QuadPart < 0 && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
        WaitForSingleObject(timer, 100);
        return;
    }
    if (s >= 0.001)
        Sleep((DWORD)(s * 1000.0));
}

/* Hold the frame to the limit: sleep most of what is left, spin the rest. */
static void pace(void)
{
    static double next;
    static int uncapped = -1;
    int limit = btvs_setting_int("display", "fps_limit", 60);
    double t = now_s(), step, meant;
    s_fs_pace_ms = s_fs_oversleep_ms = 0.0;
    if (uncapped < 0)
        uncapped = getenv("BTVS_UNCAPPED") != NULL;
    if (uncapped || limit <= 0) {
        next = 0;
        s_fs_step_ms = 1000.0 / 60.0;
        return;
    }
    step = 1.0 / (double)limit;
    s_fs_step_ms = step * 1000.0;
    /* The next frame is due one step after the last one was shown. A frame
     * that is already late is shown at once, and the count goes on from
     * there: the frames after a slow one are a step long again. (They used to
     * be cut short until the lost time was made up -- a late frame, then
     * short ones: the game moves by the time each frame took, so that showed
     * as a judder.) */
    if (next == 0)
        next = t;
    next += step;
    if (t >= next) {
        next = t;
        return;
    }
    meant = next - t;
    for (;;) {
        double left = next - now_s();
        if (left <= 0)
            break;
        if (left > 0.0015)
            wait_s(left - 0.001);
        else
            YieldProcessor();
    }
    s_fs_pace_ms = (now_s() - t) * 1000.0;
    s_fs_oversleep_ms = s_fs_pace_ms - meant * 1000.0;
    if (s_fs_oversleep_ms < 0.0)
        s_fs_oversleep_ms = 0.0;
}

static void stats(void)
{
    static double last, window_start, worst;
    static unsigned n;
    double t = now_s(), dt = last > 0 ? t - last : 0;
    last = t;
    if (dt > worst) worst = dt;
    n++;
    if (t - window_start >= 1.0) {
        s_fps = (float)(n / (t - window_start));
        s_frame_ms_avg = (t - window_start) * 1000.0 / n;
        s_frame_ms_max = worst * 1000.0;
        if (getenv("BTVS_FPS_LOG"))
            btvs_log("gfx", "t=%.0f fps %.1f avg %.2f ms worst %.2f ms", t, s_fps, s_frame_ms_avg, s_frame_ms_max);
        window_start = t;
        worst = 0;
        n = 0;
    }
}

/* DWORD D3DDevice_Swap(DWORD Flags) */
void D3DDevice_Swap_00595DB0(void)
{
    double t0 = now_s(), t1, t2, t3, tp;
    { extern volatile DWORD g_btvs_game_tid; g_btvs_game_tid = GetCurrentThreadId(); }
    fs_frame(t0);
    btvs_native_overlay();          /* Menu's and Logs' text and rectangles: part of the frame */
    btvs_shot_frame();
    tp = now_s();
    /* The limiter waits before the frame is shown, not after: frames then
     * reach the screen a step apart however long each took to make. (Shown
     * as soon as it was made, a frame's place in time moved with the work in
     * it, and with vsync on a fast monitor that came out as frames held for
     * three, four or five refreshes in turn.) */
    pace();
    t1 = now_s();
    btvs_native_present();
    t2 = now_s();
    btvs_window_frame();
    t3 = now_s();
    s_fs_present_ms = (t2 - t1) * 1000.0;
    s_fs_port_ms = (tp - t0 + t3 - t2) * 1000.0;
    stats();
    s_frames++;
    D3DDevice_Swap_00595DB0_orig();
    {   /* the frame that starts now (BTVS_HITCH_LOG, btvs_boot.c); the one that ended, if it was long */
        extern volatile LONGLONG g_btvs_frame_start;
        extern volatile LONG g_btvs_frame_no;
        static int on = -1;
        static double limit, last;
        LARGE_INTEGER q;
        double t = now_s();
        if (on < 0) {
            const char *e = getenv("BTVS_HITCH_LOG");
            on = e != NULL;
            limit = e && atof(e) > 0.0 ? atof(e) : 30.0;
        }
        if (on && last > 0.0 && (t - last) * 1000.0 >= limit)
            fprintf(stderr, "[HITCH] frame %ld took %.1f ms (ended at %.2f s)\n", (long)g_btvs_frame_no, (t - last) * 1000.0, t);
        last = t;
        QueryPerformanceCounter(&q);
        g_btvs_frame_start = q.QuadPart;
        InterlockedIncrement(&g_btvs_frame_no);
    }
}
