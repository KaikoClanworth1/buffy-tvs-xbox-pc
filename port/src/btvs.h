/* btvs.h - what the port's areas offer each other (see docs/AREAS.md).
 * Add your area's declarations in its own block; don't rewrite others'. */
#ifndef BTVS_H
#define BTVS_H

/* ---- Menu: the settings store -------------------------------------------
 * btvs_setting_int/float/str(section, key, default), setters, change
 * callbacks, btvs_settings_save(). Saved to btvs_settings.ini beside the exe;
 * BTVS_<SECTION>_<KEY> in the environment overrides a value for a test run. */
#include "btvs_settings.h"

/* ---- Graphics ------------------------------------------------------------
 * btvs_screenshot: the next frame shown is written to `path` as a PNG (at
 * the size the game is drawn at); returns 0 only for a bad path. The file is
 * there one frame later -- btvs_screenshot_count() goes up when it is.
 * btvs_gpu_info: the graphics card and its memory, for the memory page and
 * reports; 0 if the device is not up yet (the names are still filled in).
 * btvs_fps: frames shown per second, over the last second. */
#include <stdint.h>
struct btvs_gpu_info {
    char     adapter[128];          /* "NVIDIA GeForce RTX ..." */
    char     api[32];               /* "Direct3D 11" */
    int      render_width, render_height;   /* what the game is drawn at now */
    uint64_t vram_total;            /* the card's dedicated memory, bytes */
    uint64_t vram_budget;           /* what Windows lets this game use of it now */
    uint64_t vram_used_by_game;     /* what this game uses of it now */
    uint64_t shared_total;          /* system memory the card may borrow */
    uint64_t shared_used_by_game;
};
int   btvs_screenshot(const char *path);
int   btvs_screenshot_count(void);
int   btvs_gpu_info(struct btvs_gpu_info *out);
float btvs_fps(void);
/* btvs_quit: the way out (the menu's Exit; the window's close button calls it
 * too). Saves the settings, calls what btvs_on_quit registered (newest first,
 * up to 8), ends the process. Any thread.
 * The window also gives Menu its WM_CHAR (btvs_menu_char) and Controls the
 * mouse wheel, the cursor capture and btvs_input_frame() once a frame. */
/* Overlay: the port's own text and rectangles over the game's picture, for
 * Menu's and Logs' pages and messages. Immediate mode: ask again every frame
 * for what should be on screen (any thread). Coordinates and sizes are in the
 * game's 640x480 screen whatever the picture's real size; colours are
 * 0xAARRGGBB; `height` is the letter height. Part of the frame, so also in
 * screenshots. btvs_overlay_text_width: for centring and right-aligning. */
void  btvs_overlay_rect(float x, float y, float w, float h, uint32_t argb);
void  btvs_overlay_text(float x, float y, float height, uint32_t argb, const char *text);
float btvs_overlay_text_width(float height, const char *text);
void  btvs_quit(void);
void  btvs_on_quit(void (*fn)(void));

/* ---- Menu: the PC pages in the game's own menus (btvs_menu.c) -------------
 * btvs_menu_char: a character typed on the keyboard (the window's WM_CHAR;
 * 8 = backspace), for the Report a Problem text. Game thread or window thread.
 * btvs_menu_page_open: 1 while a PC page is on screen. */
void btvs_menu_char(unsigned ch);
int  btvs_menu_page_open(void);

/* ---- Controls: the game's pads (docs/controls.md) --------------------------
 * Logs:     btvs_input_record_begin(path) / _end(), btvs_input_playback(path)
 * Menu:     btvs_input_action_count/label, _binding_text, _capture_begin/_state,
 *           _binding_clear, _bindings_reset, _device_name
 * Graphics: btvs_input_frame() once per presented frame, btvs_input_wheel(),
 *           btvs_input_mouse_captured()
 * Tests:    BTVS_PAD_SCRIPT, btvs_input_script_mark("menu") */
#include "btvs_input.h"

#include <stddef.h>

/* ── Logs → everyone ───────────────────────────────────────────────────────
 * btvs_log: one line into btvs_log.txt beside the exe ("[  12.345] [area] text"),
 * from any thread, at any time (also before main's btvs_log_init). A test run
 * also gets the line on stderr. `area` is a short lower-case word: "gfx",
 * "audio", "input", "menu", "boot", ... A trailing newline is not needed.
 *
 * btvs_trail: one event in the record of what the player did (btvs_trail.txt,
 * part of every report). `event` is one word; the rest is "key=value" pairs:
 *     btvs_trail("menu", "screen=%s", name);
 *     btvs_trail("setting", "%s.%s=%s", section, key, value);
 * The events "level", "checkpoint" and "load" are anchors: places a replay
 * can start from (the input recording restarts at each one). */
void btvs_log(const char *area, const char *fmt, ...);
void btvs_trail(const char *event, const char *fmt, ...);

/* ── Logs → Menu ───────────────────────────────────────────────────────────
 * Report a Problem. btvs_report_prepare() when the pause menu opens (takes
 * the screenshot while the game picture is still on screen);
 * btvs_report_create(text) when the player confirms: writes
 * reports\<yyyymmdd-hhmmss>\ and a .zip of it beside the exe and returns 1.
 * Nothing is sent anywhere. Both may be called from any thread.
 * btvs_report_last_path(): the folder of the newest report made this run
 * ("" if none), to show to the player. */
void btvs_report_prepare(void);
int btvs_report_create(const char *player_text);
const char *btvs_report_last_path(void);

/* Memory statistics, cheap enough to call every frame (sampled once a second
 * on a thread of its own). Sizes in bytes; 0 = not known. */
struct btvs_mem {
    /* the PC */
    uint64_t ram_total, ram_available;
    /* this game */
    uint64_t proc_working_set;          /* Task Manager, Details: "Working set (memory)" */
    uint64_t proc_private_working_set;  /* Task Manager's "Memory" column */
    uint64_t proc_commit;               /* "Commit size" */
    uint64_t proc_peak_working_set;
    /* the graphics card the game draws with */
    char gpu_name[128];
    uint64_t vram_total;                /* dedicated video memory */
    uint64_t vram_used_system;          /* in use by every program (0 if Windows won't say) */
    uint64_t vram_used;                 /* in use by this game */
    uint64_t vram_budget;               /* what Windows lets this game use right now */
    uint64_t vram_shared_used;          /* this game's use of shared (system) memory */
    /* the game's own memory: the Xbox's 64 MB as the game sees it */
    uint64_t guest_ram_total;
    int game_heap_valid;                /* 0 until the game's allocator is up */
    uint64_t game_heap_used;            /* bytes the game has allocated */
    uint64_t game_heap_peak;
    uint32_t game_heap_allocs;          /* live allocations */
};
void btvs_mem_stats(struct btvs_mem *out);
#define BTVS_HAVE_MEM_STATS 1


#endif
