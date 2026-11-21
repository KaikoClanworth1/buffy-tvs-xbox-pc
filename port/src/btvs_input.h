/**
 * Controls: the PC side of the game's pads. See docs/controls.md.
 *
 * Everything the game reads from a controller passes through ONE function,
 * btvs_input_poll(port, &pad): PC controllers (XInput, SDL), keyboard and
 * mouse, the test script (BTVS_PAD_SCRIPT), recording and playback all meet
 * there. The XAPI replacements in btvs_input_xapi.c only call it.
 */
#ifndef BTVS_INPUT_H
#define BTVS_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BTVS_PORTS 4

/* One Xbox pad, exactly as XINPUT_GAMEPAD lays it out on the console (18 bytes). */
#pragma pack(push, 1)
typedef struct btvs_pad {
    uint16_t buttons;       /* BTVS_BTN_* */
    uint8_t  analog[8];     /* BTVS_AN_*: A B X Y Black White LT RT, 0-255 */
    int16_t  lx, ly;        /* left stick, up/right positive */
    int16_t  rx, ry;        /* right stick */
} btvs_pad;
#pragma pack(pop)

#define BTVS_BTN_UP      0x0001
#define BTVS_BTN_DOWN    0x0002
#define BTVS_BTN_LEFT    0x0004
#define BTVS_BTN_RIGHT   0x0008
#define BTVS_BTN_START   0x0010
#define BTVS_BTN_BACK    0x0020
#define BTVS_BTN_LSTICK  0x0040
#define BTVS_BTN_RSTICK  0x0080
enum { BTVS_AN_A, BTVS_AN_B, BTVS_AN_X, BTVS_AN_Y, BTVS_AN_BLACK, BTVS_AN_WHITE, BTVS_AN_LT, BTVS_AN_RT };

/* ── the one per-poll function ───────────────────────────────────────────── */

/* The pad on `port` (0-3) right now. Returns 1 when the port has a pad, 0 when
 * it is empty (pad is then all zero). Thread-safe. */
int btvs_input_poll(int port, btvs_pad *pad);

/* Which ports have a pad (bit per port). Port 0 always does: the keyboard. */
uint32_t btvs_input_connected(void);

/* The pad as the game last read it; does not poll anything. */
void btvs_input_last(int port, btvs_pad *pad);

/* Rumble for the PC controller on `port`, 0-65535 each (left = low frequency). */
void btvs_input_rumble(int port, uint16_t left, uint16_t right);

/* Graphics calls this once per presented frame: the frame clock of scripts and
 * recordings. (Until it does, a "frame" is one poll of port 0.) */
void btvs_input_frame(void);

/* Graphics forwards WM_MOUSEWHEEL here (delta / 120 notches) so the wheel can be bound. */
void btvs_input_wheel(int notches);

/* 1 while the mouse is turning the camera: the window should hide the cursor. */
int btvs_input_mouse_captured(void);

/* The game (or the Menu area) says whether mouse look may capture the mouse:
 * 1 while playing, 0 in menus / pause / cutscenes. Default 1. */
void btvs_input_allow_mouse_look(int allow);
/* 0: a level is being played; 1: a menu is up (Enter / Space are A, Esc /
 * Backspace are B); 2: the title screen (Enter / Space are Start too). */
void btvs_input_menu_mode(int mode);

/* ── tests: scripted input ───────────────────────────────────────────────── */

/* A named moment for `after <name>` lines in a pad script ("menu", "level"...). */
void btvs_input_script_mark(const char *name);
/* Load a script from text (what BTVS_PAD_SCRIPT does at start). Returns the number of events. */
int btvs_input_script_load_text(const char *text);
int btvs_input_script_load_file(const char *path);
/* 1 when every scripted event has played. */
int btvs_input_script_done(void);

/* ── Logs: record and replay what the player did ─────────────────────────── */

/* Start writing every pad the game reads to `path` (see docs/controls.md for
 * the format). Returns 1 on success. Ends an earlier recording first. */
int  btvs_input_record_begin(const char *path);
void btvs_input_record_end(void);
int  btvs_input_recording(void);
/* Write what has been recorded so far to the file (it stays open). */
void btvs_input_record_flush(void);

/* Feed the game a recording instead of the player until it runs out.
 * Returns 1 when the file was loaded. */
int  btvs_input_playback(const char *path);
void btvs_input_playback_stop(void);
int  btvs_input_playback_active(void);

/* ── Menu: rebinding and options ─────────────────────────────────────────── */

/* Actions are the Xbox pad's inputs; each has two keyboard/mouse slots (0, 1)
 * and one controller slot (2). */
#define BTVS_BIND_SLOTS 3
int         btvs_input_action_count(void);
const char *btvs_input_action_name(int action);    /* ini name: "A", "MoveForward" */
const char *btvs_input_action_label(int action);   /* for the screen: "A button" */
/* What is bound, as text ("Space", "Pad LB", "" when empty). Returns buf. */
const char *btvs_input_binding_text(int action, int slot, char *buf, int size);
/* Wait for the next key / mouse button (slots 0, 1) or controller button /
 * stick direction (slot 2); the game sees a resting pad meanwhile. */
void btvs_input_capture_begin(int action, int slot);
/* 0 still waiting, 1 bound and saved, -1 cancelled (Escape, or 8 s passed). */
int  btvs_input_capture_state(void);
void btvs_input_capture_cancel(void);
void btvs_input_binding_clear(int action, int slot);
void btvs_input_bindings_reset(void);              /* defaults, saved */
/* Re-read [Controls] from the settings (after the Menu changed an option). */
void btvs_input_settings_reload(void);
/* Names of the connected controller on a port ("" when none). */
const char *btvs_input_device_name(int port);

#ifdef __cplusplus
}
#endif
#endif
