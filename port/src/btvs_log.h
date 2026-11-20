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
const char *btvs_settings_path(void);     /* BTVS_SETTINGS or <exe>\btvs_settings.ini */
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

#endif /* BTVS_LOG_H */
