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

#endif
