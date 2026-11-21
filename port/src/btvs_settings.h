/* btvs_settings - the port's settings store (Menu area).
 *
 * Every PC option lives here: a value named by (section, key), saved in
 * btvs_settings.ini beside the exe. The in-game menu pages only write values;
 * the area that owns an option reads it and registers a change callback to
 * apply it live.
 *
 *   int vsync = btvs_setting_int("display", "vsync", 1);
 *   btvs_setting_on_change("display", NULL, on_display_changed, NULL);
 *
 * Test runs: the environment variable BTVS_<SECTION>_<KEY> (upper case, e.g.
 * BTVS_DISPLAY_VSYNC=0) overrides the saved value for that run and is never
 * written back to the file. BTVS_SETTINGS_INI names another file to use.
 *
 * Thread-safe. Section and key names are case-insensitive. No init call is
 * needed: the file is loaded on first use.
 *
 * ---- Agreed names (add yours here when you start reading one) -------------
 *  [display]  mode        0 windowed, 1 borderless, 2 exclusive fullscreen
 *             width, height   back-buffer size; 0 = desktop size
 *             vsync       0/1
 *             fps_limit   0 = uncapped, else 30/60/90/120/144/165/240
 *             widescreen  0 original 4:3 (pillarbox), 1 widescreen (wider view), 2 stretch
 *             show_fps    0 off, 1 FPS counter, 2 debug overlay
 *  [audio]    master, music, effects, speech    0..100
 *  [controls] sensitivity 1..10, invert_y 0/1, rumble 0/1
 *             (bindings: keys of the Controls area's choosing)
 *  [menu]     highlight_shadow  0 the selected menu line drawn once, 1 the game's own black copy behind it
 */
#ifndef BTVS_SETTINGS_H
#define BTVS_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Read a value. If it is in neither the environment nor the file, `def` is
 * returned and remembered (it is written to the file on the next save, so the
 * ini lists every option the game has asked for). */
int         btvs_setting_int  (const char *section, const char *key, int def);
float       btvs_setting_float(const char *section, const char *key, float def);
/* The returned pointer stays valid for the life of the process. */
const char *btvs_setting_str  (const char *section, const char *key, const char *def);

/* Write a value. If it differs from the current one, change callbacks run
 * (on the calling thread, before the setter returns) and the store is marked
 * for saving. Setting a value replaces an environment override. */
void btvs_setting_set_int  (const char *section, const char *key, int value);
void btvs_setting_set_float(const char *section, const char *key, float value);
void btvs_setting_set_str  (const char *section, const char *key, const char *value);

/* Call `fn` whenever a matching value changes. section and/or key may be NULL
 * to match any. Callbacks may read and write settings. */
typedef void (*btvs_setting_cb)(const char *section, const char *key, void *user);
void btvs_setting_on_change(const char *section, const char *key,
                            btvs_setting_cb fn, void *user);

/* Write the file now if anything changed (also done at exit). The menu calls
 * this when a page is left. Returns 1 if the file is up to date. */
int  btvs_settings_save(void);

/* Full path of the ini file in use. */
const char *btvs_settings_path(void);

#ifdef __cplusplus
}
#endif
#endif
