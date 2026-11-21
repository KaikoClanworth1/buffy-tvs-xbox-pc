/* The Logs area's own API (docs/logs.md). What other areas need is in btvs.h. */
#ifndef BTVS_LOG_H
#define BTVS_LOG_H

#include <stdarg.h>
#include <stdio.h>
#include "btvs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── btvs_log.c ── */
void btvs_log_init(void);                 /* first thing in WinMain (btvs_log() also does it) */
void btvs_logv(const char *area, const char *fmt, va_list ap);
void btvs_log_block(const char *text);    /* several lines as they are, no prefix */
double btvs_uptime(void);                 /* seconds since the log began */
const char *btvs_exe_dir(void);           /* the folder of the exe (or BTVS_LOG_DIR), no trailing slash */
const char *btvs_save_dir(void);          /* where the Xbox's save drives live (BTVS_SAVE_DIR or <exe>\XboxData) */
int btvs_restarts(void);            /* times the game restarted itself this session (level loads) */
int btvs_is_test_run(void);               /* BTVS_NO_WINDOW: no windows, no sounds, no real input */
const char *btvs_version_text(void);      /* "0.0.1 (commit abc1234, built ...)" */
void btvs_system_text(char *buf, size_t n);   /* Windows, CPU, RAM, GPUs: several lines */
#define BTVS_LOG_FILE "btvs_log.txt"
#define BTVS_LOG_PREVIOUS "btvs_log_previous.txt"

/* ── btvs_trail.c ── */
void btvs_trail_init(void);
void btvs_frame_tick(void);               /* once a frame on the game thread (the TGame::ExecFrame wrapper) */
int btvs_trail_tail(char *buf, size_t n, int lines);   /* the last events, oldest first */
/* Where the player is, from the trail: "" when not known. */
struct btvs_where {
    char level[96], checkpoint[96], menu[96], anchor_event[16], anchor_name[96];
    double anchor_time;           /* uptime of the last anchor */
    long anchor_frame, frame;
    int have_pos;
    float pos[3], health;
};
void btvs_trail_where(struct btvs_where *out);
#define BTVS_TRAIL_FILE "btvs_trail.txt"
#define BTVS_TRAIL_PREVIOUS "btvs_trail_previous.txt"
#define BTVS_INPUT_FILE "btvs_input.rec"
#define BTVS_FRAMES_FILE "btvs_frames.rec"

/* ── btvs_report_exact.c: the frame recording (clock, pad samples, random state) ── */
void btvs_exact_frame_begin(uint32_t game_manager);    /* around the game's own frame function */
void btvs_exact_frame_end(void);
void btvs_exact_hook_input_update(void);               /* guest hooks (btvs_trail_lookup) */
void btvs_exact_hook_input_poll(void);
int  btvs_exact_snapshot(const char *dst, unsigned *frames, float pos[3]);
int  btvs_exact_replay(const char *path, unsigned frames);
int  btvs_exact_replay_state(void);                    /* 0 none, 1 waiting for the level, 2 playing, 3 over */
int  btvs_exact_replay_result(unsigned *frames, unsigned *checks, float *max_dev, unsigned *first_bad, float pos[3]);
int  btvs_exact_recording(void);
unsigned btvs_exact_replay_seed_fixes(void);
void btvs_objlog_hook_object_tick(void);                /* btvs_objlog.c: BTVS_OBJLOG, BTVS_FORCE_DT */
/* The game restarting itself inside the process (btvs_boot_restart.c calls btvs_logs_reboot, btvs_trail.c):
 * from that call until the next run's first frame the game's memory is not to be read. */
int  btvs_logs_game_up(void);                           /* 0 between a restart and the next run's first frame */
void btvs_exact_reboot(void);
void btvs_report_reboot(void);
void btvs_crash_reboot(void);
void btvs_replay_reboot(void);
void btvs_objlog_reboot(void);
int  btvs_game_paused(void);                           /* the pause screen is open */
void btvs_input_record_flush(void);       /* Controls, when it has one: make the recording file complete up to now */

/* ── btvs_mem.c ── */
void btvs_mem_stats_now(struct btvs_mem *out);          /* sampled on the spot (reports, crashes) */
void btvs_mem_text(const struct btvs_mem *m, char *buf, size_t n);
void btvs_gpu_text(char *buf, size_t n);                /* every adapter: name, driver, memory */

/* ── btvs_report_crash.c ── */
void btvs_crash_init(void);               /* symbols, unhandled-exception filter, hang watch, test crash */
struct _EXCEPTION_POINTERS;
void btvs_crash_report(struct _EXCEPTION_POINTERS *ep, int unhandled);   /* from main.c's VEH */
int btvs_sym_guest(uint32_t va, char *out, size_t n);  /* "Name+0x1C"; 0 if no table */
int btvs_sym_ready(void);
void btvs_crash_test_now(void);            /* BTVS_TEST_CRASH=1: from main, on the game thread */

/* ── btvs_report.c ── */
void btvs_report_init(void);              /* hotkey thread, BTVS_TEST_REPORT_AT */
int btvs_report_make(const char *player_text, const char *trigger);

/* ── btvs_report_replay.c ── */
void btvs_replay_init(void);              /* BTVS_REPLAY: before the settings and the save path are read */
int btvs_replay_active(void);
void btvs_replay_on_anchor(const char *event, const char *name);
void btvs_replay_frame(void);
/* A report folder -> a store-only zip beside it, and back. */
int btvs_zip_folder(const char *folder, const char *zip_path);
int btvs_unzip(const char *zip_path, const char *folder);
int btvs_copy_tree(const char *from, const char *to, unsigned long long max_bytes, unsigned long long *copied);

#ifdef __cplusplus
}
#endif

/* btvs_trail.c, GAME HOOKS: the exact spot (player and camera world matrices,
 * 12 floats each) and putting the player back there (game thread only). */
int btvs_game_state(float player[12], float camera[12], float *health);
int btvs_game_place(const float m[12], float health);
int btvs_game_in_level(char *name, size_t n);
int btvs_trail_in_cutscene(void);
int btvs_replay_wants_skip(void);         /* a replay is on its way to the spot: skip cutscenes */
double btvs_trail_level_loaded_at(void);  /* uptime when the level became playable, 0 before */
void btvs_report_game_snap(void);         /* game thread: keep the next frame as the report's picture */
void btvs_report_game_snap_clear(void);

#endif /* BTVS_LOG_H */
