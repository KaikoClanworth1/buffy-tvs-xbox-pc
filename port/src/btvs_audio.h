/* btvs_audio - the port's sound output (Audio area).
 *
 * A software mixer whose final mix goes to one XAudio2 source voice.
 *
 *   btvs_audio.c         the mixer and the XAudio2 device (this API; no game code)
 *   btvs_audio_dsound.c  the game's Xbox DirectSound calls, mapped onto this API
 *   btvs_wma.c           WMA music: runs the game's own decoder; self-test
 *   btvs_audio_test.c    stand-alone test program (game assets -> WAV)
 *
 * Environment:
 *   BTVS_MUTE=1            no audio device is opened at all; voices, cursors,
 *                          stream packets and the audio clock run as usual
 *   BTVS_AUDIO_DUMP=<wav>  the final mix (48 kHz, 16-bit stereo) is also
 *                          written to this file, muted or not
 *   BTVS_AUDIO_LOG=1       log every voice start
 *
 * Voices are identified by small integers; every call is thread-safe and
 * ignores an invalid id.
 */
#ifndef BTVS_AUDIO_H
#define BTVS_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BTVS_AUDIO_RATE     48000
#define BTVS_AUDIO_PERIOD   480          /* frames mixed at a time (10 ms) */

enum { BTVS_CAT_EFFECTS = 0, BTVS_CAT_MUSIC = 1, BTVS_CAT_SPEECH = 2, BTVS_CAT_COUNT };

#define BTVS_FMT_PCM        0x0001
#define BTVS_FMT_XBOX_ADPCM 0x0069       /* 36-byte blocks of 64 samples per channel */

/* BTVS_AUDIO_OFFLINE: no thread and no device; the caller pulls the mix with
 * btvs_audio_render() (tests). Otherwise a mixer thread runs in real time. */
#define BTVS_AUDIO_OFFLINE  1
int  btvs_audio_init(int flags);                    /* 1 = running. Safe to call again. */
void btvs_audio_shutdown(void);                     /* stops the thread, closes the dump */
void btvs_audio_render(int frames);                 /* offline mode only */
int  btvs_audio_reset_voices(void);                 /* frees every voice, listener to defaults; device, clock, dump stay. Returns voices freed */
int  btvs_audio_voices_in_use(void);
void btvs_audio_set_log(void (*sink)(const char *line));   /* where "[audio]" lines go (default: stderr) */

/* The audio clock: frames mixed since init (at BTVS_AUDIO_RATE). */
uint64_t btvs_audio_clock(void);
/* 1 if sound reaches a device, 0 if muted or no device. */
int  btvs_audio_audible(void);

/* 0..1 linear gains. cat = BTVS_CAT_*. */
void btvs_audio_set_master(float gain);
void btvs_audio_set_category(int cat, float gain);

/* The 3D listener. `front` and `top` need not be normalised. */
void btvs_audio_listener(const float pos[3], const float front[3], const float top[3]);
void btvs_audio_listener_factors(float distance, float rolloff);

/* ---- voices -------------------------------------------------------------- */

int  btvs_audio_voice_new(int is_stream);           /* -1 if none free */
void btvs_audio_voice_free(int id);
/* tag = BTVS_FMT_*; bits is 8 or 16 for PCM. */
void btvs_audio_voice_format(int id, int tag, int channels, int rate, int bits, int block_align);
void btvs_audio_voice_category(int id, int cat);
void btvs_audio_voice_volume(int id, int millibels);     /* -10000..0 */
void btvs_audio_voice_headroom(int id, int millibels);   /* 0..10000 */
void btvs_audio_voice_frequency(int id, int hz);         /* 0 = the format's rate */
/* mode: 0 normal, 1 head-relative, 2 = 3D off. */
void btvs_audio_voice_3d(int id, int mode, const float pos[3], float min_dist, float max_dist);

/* Buffers: `data` is read in place while the voice plays, so it must stay
 * valid until the voice is stopped, given other data, or freed. */
void btvs_audio_voice_data(int id, const void *data, uint32_t bytes);
void btvs_audio_voice_play(int id, int looping);
void btvs_audio_voice_stop(int id);                      /* keeps the cursor */
void btvs_audio_voice_rewind(int id);
int  btvs_audio_voice_playing(int id);
uint32_t btvs_audio_voice_cursor(int id);                /* play position in bytes */

/* Streams: packets are copied. When the mixer has played a packet (or the
 * stream is flushed) *status and *completed are written, if not NULL:
 * status 0 = played, 0x80004004 = flushed; until then *status = 0x8000000A. */
int  btvs_audio_stream_submit(int id, const void *data, uint32_t bytes,
                              volatile uint32_t *status, volatile uint32_t *completed);
void btvs_audio_stream_flush(int id);
void btvs_audio_stream_pause(int id, int paused);
int  btvs_audio_stream_queued(int id);                   /* packets not yet played */
uint64_t btvs_audio_stream_played(int id);               /* bytes played since creation */
uint32_t btvs_audio_stream_starves(int id);              /* times it ran out of packets while playing */
/* Source frames dropped because they arrived late: after a stream runs dry
 * for up to 2 s, that much of what comes next is skipped, so it stays in step
 * with the audio clock (a movie's picture follows that clock). */
uint64_t btvs_audio_stream_skipped(int id);

/* Counters for docs/tests and the debug overlay. */
struct btvs_audio_stats {
    int      voices, playing, streams;
    uint64_t frames_mixed, frames_clipped, underruns, device_restarts;
    uint64_t stalled_frames;                             /* mixed but dropped: the device was not taking data */
    uint64_t device_glitches;                            /* XAudio2's own glitch counter */
    float    peak;                                       /* since the last call */
};
void btvs_audio_stats(struct btvs_audio_stats *out);

#ifdef __cplusplus
}
#endif
#endif
