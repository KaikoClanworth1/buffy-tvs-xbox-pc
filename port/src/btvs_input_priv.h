/* Controls, between its own files only. */
#ifndef BTVS_INPUT_PRIV_H
#define BTVS_INPUT_PRIV_H

#include <stdint.h>
#include <stdio.h>
#include "btvs_input.h"

/* Until the Logs area's btvs_log() exists this goes to stderr (runs/<name>.err). */
#define IN_LOG(...) do { fprintf(stderr, "[INPUT] " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)

/* A PC controller, whatever API it came from. Sticks: right / up positive. */
typedef struct in_host_pad {
    uint32_t buttons;           /* 1 << HB_* */
    int16_t  lx, ly, rx, ry;
    uint8_t  lt, rt;
} in_host_pad;

enum {
    HB_A, HB_B, HB_X, HB_Y, HB_LB, HB_RB, HB_START, HB_BACK, HB_L3, HB_R3,
    HB_UP, HB_DOWN, HB_LEFT, HB_RIGHT,
    HB_GUIDE, HB_MISC, HB_PADDLE1, HB_PADDLE2, HB_PADDLE3, HB_PADDLE4, HB_TOUCHPAD,
    HB_COUNT
};

/* ── XInput backend (btvs_input_xinput.c) ── */
int  in_xi_read(int slot, in_host_pad *out);            /* 1 when slot 0-3 has a pad */
void in_xi_rumble(int slot, uint16_t left, uint16_t right);

/* ── SDL backend (btvs_input_sdl.c): SDL3.dll loaded at run time ── */
#define IN_SDL_MAX 8
typedef struct in_sdl_dev {
    uint32_t    id;             /* SDL joystick instance id; 0 = unused */
    in_host_pad pad;
    char        name[64];
} in_sdl_dev;
int  in_sdl_start(int include_xbox_pads);               /* 1 when SDL3.dll was found and started */
int  in_sdl_snapshot(in_sdl_dev *out, int max);         /* connected pads right now */
void in_sdl_rumble(uint32_t id, uint16_t left, uint16_t right);
void in_sdl_stop(void);

/* ── keyboard and mouse (btvs_input_kbm.c) ── */
#define IN_KEY_WHEEL_UP    0x1001
#define IN_KEY_WHEEL_DOWN  0x1002
int         in_key_code(const char *name);              /* 0 when unknown */
const char *in_key_name(int code);                      /* "" when unknown */
int         in_key_is_mouse(int code);
int         in_key_down(int code);
/* The first key or mouse button that is down, 0 if none. */
int         in_key_any_down(void);
/* Take or release the mouse; while taken, in_mouse_delta gives its movement
 * since the last call and puts the cursor back in the window's middle. */
void        in_mouse_capture(int on);
int         in_mouse_is_captured(void);
void        in_mouse_delta(int *dx, int *dy);

/* ── tests ── */
/* Replace the clock (milliseconds) and the focus test; NULL restores the real ones. */
void btvs_input__test_hooks(double (*clock_ms)(void), int (*focused)(void));
/* Give the layer a made-up PC controller on XInput slot 0 (NULL: none). */
void btvs_input__test_host_pad(const in_host_pad *pad);
/* Drop every piece of state: script, recording, playback, devices. */
void btvs_input__test_reset(void);
/* Print a recording as text. Returns the number of records, -1 on error. */
int  btvs_input_dump_recording(const char *path, FILE *out);

#endif
