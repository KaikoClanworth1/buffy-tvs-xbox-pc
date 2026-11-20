/**
 * The frame: shown, counted and paced at the game's D3DDevice_Swap.
 *
 * The Xbox paced the game by its display: Swap waited for the TV's vertical
 * blank. Here the frame limit is the player's ([display] fps_limit, 60 by
 * default; 0 none) and, with vsync on, the monitor's refresh. BTVS_UNCAPPED=1
 * turns the limiter off (benchmarks).
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

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

/* Hold the frame to the limit: sleep most of what is left, spin the rest. */
static void pace(void)
{
    static double next;
    static int uncapped = -1;
    int limit = btvs_setting_int("display", "fps_limit", 60);
    double t = now_s(), step;
    if (uncapped < 0)
        uncapped = getenv("BTVS_UNCAPPED") != NULL;
    if (uncapped || limit <= 0) {
        next = 0;
        return;
    }
    step = 1.0 / (double)limit;
    if (next == 0 || t - next > 0.1)
        next = t;                                  /* first frame, or far behind: start again from now */
    next += step;
    for (;;) {
        double left = next - now_s();
        if (left <= 0)
            break;
        if (left > 0.002)
            Sleep((DWORD)(left * 1000.0 - 1.0));
        else
            YieldProcessor();
    }
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
    btvs_native_overlay();          /* Menu's and Logs' text and rectangles: part of the frame */
    btvs_shot_frame();
    btvs_native_present();
    btvs_window_frame();
    pace();
    stats();
    s_frames++;
    D3DDevice_Swap_00595DB0_orig();
}
