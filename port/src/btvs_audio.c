/* btvs_audio.c - software mixer on one XAudio2 source voice. See btvs_audio.h.
 *
 * Why a mixer of our own rather than one XAudio2 voice per game buffer:
 *   - a muted test run must behave exactly like an audible one (cursors move,
 *     stream packets complete, the clock advances) with no device open, and
 *   - BTVS_AUDIO_DUMP must capture the final mix even when muted.
 * With the mix made here both are the same code path; XAudio2 only carries
 * the finished 48 kHz stereo signal to the device.
 *
 * The mixer thread mixes BTVS_AUDIO_PERIOD frames at a time. With a device it
 * keeps a few periods queued on the source voice and is woken by XAudio2 when
 * a buffer ends; without one it follows the performance counter.
 */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <mmreg.h>
#include <xaudio2.h>
#include <avrt.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btvs_audio.h"

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

#define MAX_VOICES     384
#define MAX_PACKETS    128
#define SKIP_RESERVE   0.35         /* seconds a stream keeps queued when late sound is skipped */
#define PAN_NEAR       16.0f        /* game units (about cm): closer than this a 3D sound moves to the centre */
#define DEV_BUFFERS    8             /* ring of submit buffers */
#define DEV_QUEUE      5             /* periods kept queued: 50 ms */

#define PKT_PENDING    0x8000000Au
#define PKT_FLUSHED    0x80004004u

typedef struct {
    uint8_t  *data;
    uint32_t  bytes;
    volatile uint32_t *status, *completed;
} Packet;

typedef struct {
    int      used, is_stream, playing, looping, paused;
    int      tag, channels, rate, bits, block;
    int      cat, volume, headroom, freq;
    int      mode3d;
    float    pos[3], mind, maxd;

    /* buffer */
    const uint8_t *data;
    uint32_t bytes, frames;
    uint32_t idx;                    /* next frame to fetch */
    uint32_t wraps;                  /* times a looping buffer started again */
    int32_t  cblock;                 /* decoded ADPCM block, -1 = none */
    int16_t  cache[2][64];

    /* stream */
    Packet  *q;
    int      qhead, qcount;
    uint32_t qoff;                   /* bytes consumed of the head packet */
    uint64_t played;
    uint32_t starves;                /* times the queue ran dry while playing */
    uint64_t skipped;                /* late frames dropped after running dry */
    int      fed;                    /* has had data since it last ran dry */
    int      late;                   /* ran dry a moment ago: silence is being counted */
    double   debt;                   /* source frames of silence played while dry */
    uint32_t debt_age;               /* frames played since data came back */
    uint32_t qbytes;                 /* bytes in the queue, counting the head packet whole */

    /* resampler: s0 is the frame being heard, s1 the next one */
    int      primed, ended;
    double   phase;
    float    s0[2], s1[2];
    float    gl, gr;                 /* gains at the end of the last period */
    int      gain_set;
} Voice;

static CRITICAL_SECTION s_lock;
static int      s_inited, s_offline, s_muted, s_log;
static Voice    s_voice[MAX_VOICES];
static float    s_master = 1.0f, s_cat[BTVS_CAT_COUNT] = { 1.0f, 1.0f, 1.0f };
static float    s_lpos[3], s_lfront[3] = { 0, 0, 1 }, s_ltop[3] = { 0, 1, 0 };
static float    s_distance = 1.0f, s_rolloff = 1.0f;
static volatile LONG64 s_clock;
static uint64_t s_clipped, s_underruns, s_restarts, s_stalled, s_glitches;
static float    s_peak;

static HANDLE   s_thread, s_wake;
static volatile LONG s_stop, s_audible, s_dev_error;

/* The dump is written by its own thread: a disk write can block for seconds
 * on a busy disk, and the mixer thread must never wait for one. */
#define DUMP_RING (BTVS_AUDIO_RATE * 8)          /* frames: 8 s of slack */
static FILE    *s_dump;
static uint32_t s_dump_frames;
static int16_t *s_dump_ring;
static volatile LONG64 s_dump_w, s_dump_r;       /* frames written to / taken from the ring */
static uint64_t s_dump_lost;
static HANDLE   s_dump_thread;
static volatile LONG s_dump_stop;

static IXAudio2               *s_xa;
static IXAudio2MasteringVoice *s_xmaster;
static IXAudio2SourceVoice    *s_xsrc;
static int16_t  s_devbuf[DEV_BUFFERS][BTVS_AUDIO_PERIOD * 2];
static int      s_devnext;

/* Where log lines go: the game gives btvs_log (btvs_audio_dsound.c); the
 * stand-alone test program has none and gets stderr. */
static void (*s_log_sink)(const char *line);
void btvs_audio_set_log(void (*sink)(const char *line)) { s_log_sink = sink; }

static void alog(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (s_log_sink) { s_log_sink(line); return; }
    fprintf(stderr, "[audio] %s\n", line);
    fflush(stderr);
}

static Voice *vget(int id)
{
    return (id >= 0 && id < MAX_VOICES && s_voice[id].used) ? &s_voice[id] : NULL;
}

/* ---- Xbox ADPCM ------------------------------------------------------------
 * IMA ADPCM, 4 bits per sample. Per channel: a 4-byte header (first sample,
 * step index) and 64 samples; the nibbles come 8 samples (4 bytes) at a time,
 * the channels taking turns. A block is 36 bytes per channel and yields 64
 * frames (the header sample only seeds the predictor). */

static const int s_ima_steps[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767 };
static const int s_ima_index[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

static int16_t ima_step(int nib, int *pred, int *idx)
{
    int step = s_ima_steps[*idx];
    int diff = step >> 3;
    if (nib & 4) diff += step;
    if (nib & 2) diff += step >> 1;
    if (nib & 1) diff += step >> 2;
    *pred += (nib & 8) ? -diff : diff;
    if (*pred > 32767) *pred = 32767;
    if (*pred < -32768) *pred = -32768;
    *idx += s_ima_index[nib];
    if (*idx < 0) *idx = 0;
    if (*idx > 88) *idx = 88;
    return (int16_t)*pred;
}

static void adpcm_block(Voice *v, int32_t b)
{
    const uint8_t *p = v->data + (size_t)b * (size_t)v->block;
    int ch, nch = v->channels, i, k;
    int pred[2], idx[2];

    for (ch = 0; ch < nch; ch++) {
        pred[ch] = (int16_t)(p[ch * 4] | (p[ch * 4 + 1] << 8));
        idx[ch] = p[ch * 4 + 2];
        if (idx[ch] > 88) idx[ch] = 88;
    }
    p += 4 * nch;
    for (i = 0; i < 8; i++) {
        for (ch = 0; ch < nch; ch++) {
            for (k = 0; k < 4; k++) {
                uint8_t byte = p[k];
                v->cache[ch][i * 8 + k * 2]     = ima_step(byte & 0xF, &pred[ch], &idx[ch]);
                v->cache[ch][i * 8 + k * 2 + 1] = ima_step(byte >> 4, &pred[ch], &idx[ch]);
            }
            p += 4;
        }
    }
    v->cblock = b;
}

static uint32_t frame_bytes(const Voice *v)
{
    return (uint32_t)(v->channels * (v->bits == 8 ? 1 : 2));
}

static void voice_measure(Voice *v)
{
    if (!v->data || !v->channels) { v->frames = 0; return; }
    if (v->tag == BTVS_FMT_XBOX_ADPCM)
        v->frames = v->block ? v->bytes / (uint32_t)v->block * 64u : 0;
    else
        v->frames = v->bytes / frame_bytes(v);
}

static void pcm_frame(const Voice *v, const uint8_t *p, float out[2])
{
    if (v->bits == 8) {
        out[0] = (float)((int)p[0] - 128) * 256.0f;
        out[1] = v->channels > 1 ? (float)((int)p[1] - 128) * 256.0f : out[0];
    } else {
        out[0] = (float)(int16_t)(p[0] | (p[1] << 8));
        out[1] = v->channels > 1 ? (float)(int16_t)(p[2] | (p[3] << 8)) : out[0];
    }
}

/* Finish the head packet of a stream. */
static void packet_done(Voice *v, uint32_t status)
{
    Packet *p = &v->q[v->qhead];
    v->qbytes -= p->bytes;
    if (p->completed) *p->completed = (status == 0) ? p->bytes : v->qoff;
    if (p->status) *p->status = status;
    free(p->data);
    p->data = NULL;
    v->qhead = (v->qhead + 1) % MAX_PACKETS;
    v->qcount--;
    v->qoff = 0;
}

/* Fetch the next source frame into out[]. Returns 0 when there is none:
 * a buffer has ended (v->ended is set) or a stream has run dry. */
static int voice_fetch(Voice *v, float out[2])
{
    if (v->is_stream) {
        uint32_t fb = frame_bytes(v);
        for (;;) {
            Packet *p;
            if (!v->qcount) {
                if (v->fed) { v->fed = 0; v->starves++; v->late = 1; v->debt_age = 0; }
                return 0;
            }
            v->fed = 1;
            /* Sound that arrives after the stream ran dry is late by the
             * length of the gap. A movie's picture follows the audio clock,
             * which did not stop: the player then decodes faster than real
             * time to catch up and its sound piles up in the queue, trailing
             * the picture for the rest of the movie. So what is queued beyond
             * a reserve (SKIP_RESERVE) is skipped, up to the length of the
             * gap. Music never has that much queued (6 packets, 0.28 s) and
             * simply carries on where it stopped. */
            v->late = 0;
            if (v->debt >= 1.0) {
                uint32_t reserve = (uint32_t)(SKIP_RESERVE * (double)v->rate) * fb;
                uint32_t avail = v->qbytes - v->qoff;
                if (++v->debt_age > (uint32_t)v->rate * 3u) {
                    v->debt = 0.0;               /* no backlog came: forget it */
                } else if (avail > reserve) {
                    uint32_t want = (uint32_t)v->debt, have, can = (avail - reserve) / fb;
                    p = &v->q[v->qhead];
                    have = (p->bytes - v->qoff) / fb;
                    if (have > want) have = want;
                    if (have > can) have = can;
                    if (have) {
                        v->qoff += have * fb;
                        v->played += (uint64_t)have * fb;
                        v->skipped += have;
                        v->debt -= (double)have;
                        if (v->qoff + fb > p->bytes) packet_done(v, 0);
                        continue;
                    }
                }
            }
            p = &v->q[v->qhead];
            if (v->qoff + fb <= p->bytes) {
                pcm_frame(v, p->data + v->qoff, out);
                v->qoff += fb;
                v->played += fb;
                if (v->qoff + fb > p->bytes) packet_done(v, 0);
                return 1;
            }
            packet_done(v, 0);
        }
    }
    if (v->idx >= v->frames) {
        if (!v->looping || !v->frames) { v->ended = 1; return 0; }
        v->idx = 0;
        v->wraps++;
    }
    if (v->tag == BTVS_FMT_XBOX_ADPCM) {
        int32_t b = (int32_t)(v->idx / 64);
        if (b != v->cblock) adpcm_block(v, b);
        out[0] = (float)v->cache[0][v->idx % 64];
        out[1] = v->channels > 1 ? (float)v->cache[1][v->idx % 64] : out[0];
    } else {
        pcm_frame(v, v->data + (size_t)v->idx * frame_bytes(v), out);
    }
    v->idx++;
    return 1;
}

static float mb_gain(int mb)
{
    if (mb <= -10000) return 0.0f;
    if (mb > 0) mb = 0;
    return powf(10.0f, (float)mb / 2000.0f);
}

static float dot3(const float a[3], const float b[3]) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

/* The gains a voice should have now. */
static void voice_gains(const Voice *v, float *gl, float *gr)
{
    float g = mb_gain(v->volume - v->headroom) * s_master * s_cat[v->cat];
    float l = g, r = g;

    if (v->mode3d != 2) {
        float d[3], dist, att = 1.0f, pan = 0.0f, mind = v->mind > 0.0f ? v->mind : 1.0f;
        if (v->mode3d == 1) {
            d[0] = v->pos[0]; d[1] = v->pos[1]; d[2] = v->pos[2];
        } else {
            d[0] = v->pos[0] - s_lpos[0]; d[1] = v->pos[1] - s_lpos[1]; d[2] = v->pos[2] - s_lpos[2];
        }
        dist = sqrtf(dot3(d, d)) * s_distance;
        /* DirectSound's distance model: full volume inside min_dist, halved
         * for each doubling beyond it (scaled by the rolloff factor), and no
         * further change past max_dist. */
        if (v->maxd > mind && dist > v->maxd) dist = v->maxd;
        if (dist > mind)
            att = mind / (mind + s_rolloff * (dist - mind));
        {
            float right[3], fl, tl;
            /* left-handed: right = top x front */
            right[0] = s_ltop[1] * s_lfront[2] - s_ltop[2] * s_lfront[1];
            right[1] = s_ltop[2] * s_lfront[0] - s_ltop[0] * s_lfront[2];
            right[2] = s_ltop[0] * s_lfront[1] - s_ltop[1] * s_lfront[0];
            fl = sqrtf(dot3(right, right));
            /* The direction comes from the positions as they are, not from
             * the scaled distance: the game sets the distance factor to the
             * smallest float there is (it wants no roll-off from DSOUND), and
             * a scaled distance of nothing must not mean "no direction". */
            tl = sqrtf(dot3(d, d));
            if (v->mode3d == 1) { right[0] = 1; right[1] = 0; right[2] = 0; fl = 1; }
            if (fl > 1e-6f && tl > 1e-6f) {
                pan = dot3(d, right) / (fl * tl);
                /* A source inside the listener's head (cutscene speech is
                 * put less than a unit from the camera) has no side. */
                if (tl < PAN_NEAR) pan *= tl / PAN_NEAR;
            }
        }
        if (pan < -1.0f) pan = -1.0f;
        if (pan > 1.0f) pan = 1.0f;
        /* Constant-power pan that never fully empties the far ear (a hard
         * left/right is tiring on headphones): pan runs over 80% of the arc. */
        {
            float a = (pan * 0.8f + 1.0f) * 0.78539816f;     /* 0..pi/2 */
            l = g * att * cosf(a) * 1.41421356f;
            r = g * att * sinf(a) * 1.41421356f;
        }
    }
    *gl = l; *gr = r;
}

/* A stream had nothing for `frames` output frames. If it ran dry in the
 * middle of playing, the gap is remembered so the late sound can be skipped
 * (voice_fetch). A gap of more than 2 s is a stop, not a hitch: whatever
 * comes next is played from its start. */
static void stream_dry(Voice *v, int frames, double step)
{
    if (!v->is_stream || !v->late) return;
    v->debt += (double)frames * step;
    if (v->debt > 2.0 * (double)(v->freq > 0 ? v->freq : v->rate)) { v->debt = 0.0; v->late = 0; }
}

/* Add `frames` frames of voice v to acc (interleaved float stereo). */
static void voice_mix(Voice *v, float *acc, int frames)
{
    float tl, tr, gl, gr, dl, dr;
    double step;
    int i, is3d = v->mode3d != 2, stereo = v->channels > 1;

    voice_gains(v, &tl, &tr);
    if (!v->gain_set) { v->gl = tl; v->gr = tr; v->gain_set = 1; }
    gl = v->gl; gr = v->gr;
    dl = (tl - gl) / (float)frames;
    dr = (tr - gr) / (float)frames;
    v->gl = tl; v->gr = tr;

    step = (double)(v->freq > 0 ? v->freq : v->rate) / (double)BTVS_AUDIO_RATE;
    if (step <= 0.0) return;

    if (!v->primed) {
        if (!voice_fetch(v, v->s1)) {
            if (v->ended) { v->playing = 0; v->idx = 0; }
            stream_dry(v, frames, step);
            return;
        }
        v->s0[0] = v->s1[0]; v->s0[1] = v->s1[1];
        v->phase = 1.0;
        v->primed = 1;
    }

    for (i = 0; i < frames; i++) {
        float a, b, f;
        while (v->phase >= 1.0) {
            float n[2];
            if (v->ended == 2) {             /* the last frame has been heard */
                if (s_log && v->cat == BTVS_CAT_SPEECH)
                    alog("end  #%d: speech played to its end", (int)(v - s_voice));
                v->playing = 0; v->ended = 0; v->primed = 0; v->idx = 0;
                return;
            }
            if (!voice_fetch(v, n)) {
                if (!v->ended) {             /* stream ran dry: hold, resume later */
                    stream_dry(v, frames - i, step);
                    return;
                }
                v->ended = 2;
                n[0] = n[1] = 0.0f;
            }
            v->s0[0] = v->s1[0]; v->s0[1] = v->s1[1];
            v->s1[0] = n[0]; v->s1[1] = n[1];
            v->phase -= 1.0;
        }
        f = (float)v->phase;
        a = v->s0[0] + (v->s1[0] - v->s0[0]) * f;
        b = v->s0[1] + (v->s1[1] - v->s0[1]) * f;
        if (is3d && stereo) a = b = (a + b) * 0.5f;
        acc[i * 2]     += a * gl;
        acc[i * 2 + 1] += b * gr;
        gl += dl; gr += dr;
        v->phase += step;
    }
}

/* ---- the WAV dump ---------------------------------------------------------- */

static void dump_header(void)
{
    uint8_t h[44];
    uint32_t data = s_dump_frames * 4u, v32;
    uint16_t v16;
    memcpy(h, "RIFF", 4);  v32 = data + 36; memcpy(h + 4, &v32, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v32 = 16; memcpy(h + 16, &v32, 4);
    v16 = 1;  memcpy(h + 20, &v16, 2);
    v16 = 2;  memcpy(h + 22, &v16, 2);
    v32 = BTVS_AUDIO_RATE; memcpy(h + 24, &v32, 4);
    v32 = BTVS_AUDIO_RATE * 4; memcpy(h + 28, &v32, 4);
    v16 = 4;  memcpy(h + 32, &v16, 2);
    v16 = 16; memcpy(h + 34, &v16, 2);
    memcpy(h + 36, "data", 4); memcpy(h + 40, &data, 4);
    fseek(s_dump, 0, SEEK_SET);
    fwrite(h, 1, 44, s_dump);
    fseek(s_dump, 0, SEEK_END);
    fflush(s_dump);
}

static void dump_drain(void)
{
    LONG64 r = s_dump_r, w = s_dump_w;
    while (r < w) {
        uint32_t at = (uint32_t)(r % DUMP_RING), n = DUMP_RING - at;
        if ((LONG64)n > w - r) n = (uint32_t)(w - r);
        fwrite(s_dump_ring + at * 2, 4, n, s_dump);
        s_dump_frames += n;
        r += n;
        InterlockedExchange64(&s_dump_r, r);
    }
}

static DWORD WINAPI dump_thread(LPVOID arg)
{
    int ticks = 0;
    (void)arg;
    while (!s_dump_stop) {
        Sleep(50);
        dump_drain();
        if (++ticks % 10 == 0) dump_header();
    }
    dump_drain();
    return 0;
}

/* ---- one period ------------------------------------------------------------ */

static void mix_period(int16_t *out, int frames)
{
    static float acc[BTVS_AUDIO_PERIOD * 2];
    int i;

    memset(acc, 0, sizeof(float) * 2 * (size_t)frames);
    EnterCriticalSection(&s_lock);
    for (i = 0; i < MAX_VOICES; i++) {
        Voice *v = &s_voice[i];
        if (!v->used || !v->playing || v->paused) continue;
        if (!v->is_stream && !v->data) continue;
        voice_mix(v, acc, frames);
    }
    for (i = 0; i < frames * 2; i++) {
        float s = acc[i], m = fabsf(s) / 32768.0f;
        if (m > s_peak) s_peak = m;
        if (s > 32767.0f) { s = 32767.0f; s_clipped++; }
        else if (s < -32768.0f) { s = -32768.0f; s_clipped++; }
        out[i] = (int16_t)lrintf(s);
    }
    LeaveCriticalSection(&s_lock);
    InterlockedAdd64(&s_clock, frames);

    if (s_dump && !s_dump_ring) {                /* offline: straight to the file */
        fwrite(out, 4, (size_t)frames, s_dump);
        s_dump_frames += (uint32_t)frames;
        if (s_dump_frames % (BTVS_AUDIO_RATE / 2) < (uint32_t)frames) dump_header();
    } else if (s_dump) {
        LONG64 w = s_dump_w;
        if (w - s_dump_r + frames > DUMP_RING) {
            s_dump_lost += (uint64_t)frames;     /* the disk is more than 8 s behind */
        } else {
            uint32_t at = (uint32_t)(w % DUMP_RING), first = DUMP_RING - at;
            if (first > (uint32_t)frames) first = (uint32_t)frames;
            memcpy(s_dump_ring + at * 2, out, first * 4u);
            memcpy(s_dump_ring, out + first * 2, ((uint32_t)frames - first) * 4u);
            InterlockedExchange64(&s_dump_w, w + frames);
        }
    }
}

/* ---- XAudio2 --------------------------------------------------------------- */

static void STDMETHODCALLTYPE cb_pass_start(IXAudio2VoiceCallback *t, UINT32 n) { (void)t; (void)n; }
static void STDMETHODCALLTYPE cb_pass_end(IXAudio2VoiceCallback *t) { (void)t; }
static void STDMETHODCALLTYPE cb_stream_end(IXAudio2VoiceCallback *t) { (void)t; }
static void STDMETHODCALLTYPE cb_buf_start(IXAudio2VoiceCallback *t, void *c) { (void)t; (void)c; }
static void STDMETHODCALLTYPE cb_buf_end(IXAudio2VoiceCallback *t, void *c) { (void)t; (void)c; SetEvent(s_wake); }
static void STDMETHODCALLTYPE cb_loop_end(IXAudio2VoiceCallback *t, void *c) { (void)t; (void)c; }
static void STDMETHODCALLTYPE cb_error(IXAudio2VoiceCallback *t, void *c, HRESULT hr)
{
    (void)t; (void)c; (void)hr;
    InterlockedExchange(&s_dev_error, 1);
    SetEvent(s_wake);
}
static IXAudio2VoiceCallbackVtbl s_cb_vtbl = {
    cb_pass_start, cb_pass_end, cb_stream_end, cb_buf_start, cb_buf_end, cb_loop_end, cb_error };
static IXAudio2VoiceCallback s_cb = { &s_cb_vtbl };

static void device_close(void)
{
    if (s_xsrc)    { IXAudio2SourceVoice_DestroyVoice(s_xsrc); s_xsrc = NULL; }
    if (s_xmaster) { IXAudio2MasteringVoice_DestroyVoice(s_xmaster); s_xmaster = NULL; }
    if (s_xa)      { IXAudio2_Release(s_xa); s_xa = NULL; }
    InterlockedExchange(&s_audible, 0);
}

static int device_open(void)
{
    WAVEFORMATEX wfx;
    HRESULT hr;

    hr = XAudio2Create(&s_xa, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) { s_xa = NULL; goto fail; }
    hr = IXAudio2_CreateMasteringVoice(s_xa, &s_xmaster, 2, BTVS_AUDIO_RATE, 0, NULL, NULL,
                                       AudioCategory_GameEffects);
    if (FAILED(hr)) { s_xmaster = NULL; goto fail; }
    memset(&wfx, 0, sizeof wfx);
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = BTVS_AUDIO_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 4;
    wfx.nAvgBytesPerSec = BTVS_AUDIO_RATE * 4;
    hr = IXAudio2_CreateSourceVoice(s_xa, &s_xsrc, &wfx, XAUDIO2_VOICE_NOPITCH | XAUDIO2_VOICE_NOSRC,
                                    1.0f, &s_cb, NULL, NULL);
    if (FAILED(hr)) { s_xsrc = NULL; goto fail; }
    hr = IXAudio2SourceVoice_Start(s_xsrc, 0, XAUDIO2_COMMIT_NOW);
    if (FAILED(hr)) goto fail;
    {   /* BTVS_AUDIO_DEVICE_SILENT=1 (the test program's "device" mode): the
         * device is opened but its master voice is at volume 0 */
        const char *e = getenv("BTVS_AUDIO_DEVICE_SILENT");
        if (e && *e == '1') IXAudio2MasteringVoice_SetVolume(s_xmaster, 0.0f, XAUDIO2_COMMIT_NOW);
    }
    InterlockedExchange(&s_dev_error, 0);
    InterlockedExchange(&s_audible, 1);
    return 1;
fail:
    alog("XAudio2 device not available (0x%08lX); running silent", (unsigned long)hr);
    device_close();
    return 0;
}

static double now_seconds(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

static DWORD WINAPI mixer_thread(LPVOID arg)
{
    int have_com, started = 0;
    double silent_t0 = 0, retry_at = 0, last_room = 0, perf_at = 0;
    uint64_t silent_f0 = 0;
    static int16_t scratch[BTVS_AUDIO_PERIOD * 2];
    (void)arg;

    have_com = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));
    if (!s_muted && device_open()) {
        /* The thread that feeds the device must not wait behind the game's
         * own threads: register it with the multimedia scheduler (it uses
         * well under 1% of a core), or at least raise it. */
        DWORD task = 0;
        if (!AvSetMmThreadCharacteristicsA("Pro Audio", &task))
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    } else {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        silent_t0 = now_seconds();
    }
    if (s_muted) alog("muted (BTVS_MUTE): no audio device opened");
    else if (s_xsrc) alog("XAudio2 output: 48000 Hz stereo, %d ms periods, %d queued",
                          BTVS_AUDIO_PERIOD * 1000 / BTVS_AUDIO_RATE, DEV_QUEUE);

    while (!s_stop) {
        if (s_xsrc) {
            XAUDIO2_VOICE_STATE st;
            UINT32 queued;
            int failed = s_dev_error;
            IXAudio2SourceVoice_GetState(s_xsrc, &st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            queued = st.BuffersQueued;
            if (started && queued == 0) s_underruns++;
            {
                /* The game's timing hangs on this clock (cursors, packet
                 * completion, movie sync), so it must not stop when the device
                 * does: an audio engine that stops taking data for a moment
                 * (seen: 0.1-2 s, queue full, under CPU load) would freeze
                 * every sound-timed thing in the game. While the queue stays
                 * full for more than 60 ms the mix goes on by the wall clock
                 * and those periods are dropped, exactly as a muted run mixes. */
                double t = now_seconds();
                const double per = (double)BTVS_AUDIO_PERIOD / BTVS_AUDIO_RATE;
                if (queued < DEV_QUEUE || last_room == 0) last_room = t;
                else if (t - last_room > 0.06) {
                    while (last_room + per <= t) {
                        mix_period(scratch, BTVS_AUDIO_PERIOD);
                        s_stalled += BTVS_AUDIO_PERIOD;
                        last_room += per;
                    }
                }
                if (t - perf_at > 1.0) {
                    XAUDIO2_PERFORMANCE_DATA pd;
                    IXAudio2_GetPerformanceData(s_xa, &pd);
                    s_glitches = pd.GlitchesSinceEngineStarted;
                    perf_at = t;
                }
            }
            while (!failed && queued < DEV_QUEUE) {
                XAUDIO2_BUFFER b;
                int16_t *buf = s_devbuf[s_devnext];
                s_devnext = (s_devnext + 1) % DEV_BUFFERS;
                mix_period(buf, BTVS_AUDIO_PERIOD);
                memset(&b, 0, sizeof b);
                b.AudioBytes = BTVS_AUDIO_PERIOD * 4;
                b.pAudioData = (const BYTE *)buf;
                if (FAILED(IXAudio2SourceVoice_SubmitSourceBuffer(s_xsrc, &b, NULL))) failed = 1;
                queued++;
                started = 1;
            }
            if (failed) {
                /* device removed or the engine stopped: go silent, try again later */
                alog("XAudio2 device lost; retrying");
                device_close();
                s_restarts++;
                started = 0;
                silent_t0 = now_seconds();
                silent_f0 = (uint64_t)s_clock;
                retry_at = silent_t0 + 2.0;
                continue;
            }
            WaitForSingleObject(s_wake, 20);
        } else {
            double t = now_seconds();
            uint64_t due;
            if (silent_f0 == 0 && silent_t0 == 0) silent_t0 = t;
            due = silent_f0 + (uint64_t)((t - silent_t0) * BTVS_AUDIO_RATE);
            if (due > (uint64_t)s_clock + BTVS_AUDIO_RATE * 3) {
                /* the thread was held up for seconds (debugger, suspend):
                 * don't burst. Shorter hold-ups (a starved CPU) are caught up,
                 * so the clock keeps to real time. */
                silent_t0 = t; silent_f0 = (uint64_t)s_clock; due = silent_f0;
            }
            while ((uint64_t)s_clock + BTVS_AUDIO_PERIOD <= due && !s_stop)
                mix_period(scratch, BTVS_AUDIO_PERIOD);
            if (!s_muted && retry_at != 0 && t >= retry_at) {
                if (device_open()) { started = 0; continue; }
                retry_at = t + 2.0;
            }
            WaitForSingleObject(s_wake, 5);
        }
    }
    device_close();
    if (have_com) CoUninitialize();
    return 0;
}

/* ---- public API ------------------------------------------------------------ */

int btvs_audio_init(int flags)
{
    const char *e;
    if (s_inited) return 1;
    InitializeCriticalSection(&s_lock);
    s_offline = (flags & BTVS_AUDIO_OFFLINE) != 0;
    e = getenv("BTVS_MUTE");      s_muted = e && *e && *e != '0';
    e = getenv("BTVS_AUDIO_LOG"); s_log = e && *e && *e != '0';
    e = getenv("BTVS_AUDIO_DUMP");
    if (e && *e) {
        /* The game restarts itself to load a level (a new process each time):
         * restart N writes <name>_r<N>.wav, so one run's dumps do not replace
         * each other. */
        char path[MAX_PATH];
        const char *r = getenv("BTVS_RESTARTS"), *dot = strrchr(e, '.');
        if (r && *r && dot && !strpbrk(dot, "/\\"))
            snprintf(path, sizeof path, "%.*s_r%s%s", (int)(dot - e), e, r, dot);
        else
            snprintf(path, sizeof path, "%s%s%s", e, r && *r ? "_r" : "", r && *r ? r : "");
        e = path;
        s_dump = fopen(e, "wb");
        if (s_dump) { setvbuf(s_dump, NULL, _IOFBF, 1 << 16); dump_header(); }
        else alog("cannot write BTVS_AUDIO_DUMP file %s", e);
    }
    s_inited = 1;
    if (!s_offline) {
        if (s_dump) {
            s_dump_ring = (int16_t *)malloc(sizeof(int16_t) * 2 * DUMP_RING);
            s_dump_stop = 0;
            s_dump_w = s_dump_r = 0;
            if (s_dump_ring) s_dump_thread = CreateThread(NULL, 0, dump_thread, NULL, 0, NULL);
            if (!s_dump_thread) { free(s_dump_ring); s_dump_ring = NULL; }
        }
        s_wake = CreateEventA(NULL, FALSE, FALSE, NULL);
        s_stop = 0;
        s_thread = CreateThread(NULL, 0, mixer_thread, NULL, 0, NULL);
        if (!s_thread) { alog("cannot start the mixer thread"); return 0; }
    }
    return 1;
}

void btvs_audio_shutdown(void)
{
    int i;
    if (!s_inited) return;
    if (s_thread) {
        InterlockedExchange(&s_stop, 1);
        SetEvent(s_wake);
        WaitForSingleObject(s_thread, 3000);
        CloseHandle(s_thread); s_thread = NULL;
        CloseHandle(s_wake); s_wake = NULL;
    }
    EnterCriticalSection(&s_lock);
    for (i = 0; i < MAX_VOICES; i++) {
        Voice *v = &s_voice[i];
        if (v->used && v->is_stream) {
            while (v->qcount) packet_done(v, PKT_FLUSHED);
            free(v->q);
        }
        memset(v, 0, sizeof *v);
    }
    LeaveCriticalSection(&s_lock);
    if (s_dump_thread) {
        InterlockedExchange(&s_dump_stop, 1);
        WaitForSingleObject(s_dump_thread, 30000);
        CloseHandle(s_dump_thread); s_dump_thread = NULL;
    }
    if (s_dump) {
        if (s_dump_lost) alog("dump: %llu frames not written (disk too slow)", (unsigned long long)s_dump_lost);
        dump_header(); fclose(s_dump); s_dump = NULL;
    }
    free(s_dump_ring); s_dump_ring = NULL;
    s_inited = 0;
}

void btvs_audio_render(int frames)
{
    static int16_t scratch[BTVS_AUDIO_PERIOD * 2];
    if (!s_inited || !s_offline) return;
    while (frames > 0) {
        int n = frames < BTVS_AUDIO_PERIOD ? frames : BTVS_AUDIO_PERIOD;
        mix_period(scratch, n);
        frames -= n;
    }
}

uint64_t btvs_audio_clock(void) { return (uint64_t)s_clock; }
int btvs_audio_audible(void) { return (int)s_audible; }

static float clamp01(float g) { return g < 0.0f ? 0.0f : (g > 1.0f ? 1.0f : g); }

void btvs_audio_set_master(float gain) { s_master = clamp01(gain); }
void btvs_audio_set_category(int cat, float gain)
{
    if (cat >= 0 && cat < BTVS_CAT_COUNT) s_cat[cat] = clamp01(gain);
}

#define LOCK()   do { if (!s_inited) btvs_audio_init(0); EnterCriticalSection(&s_lock); } while (0)
#define UNLOCK() LeaveCriticalSection(&s_lock)

void btvs_audio_listener(const float pos[3], const float front[3], const float top[3])
{
    LOCK();
    if (pos)   memcpy(s_lpos, pos, sizeof s_lpos);
    if (front) memcpy(s_lfront, front, sizeof s_lfront);
    if (top)   memcpy(s_ltop, top, sizeof s_ltop);
    UNLOCK();
}

void btvs_audio_listener_factors(float distance, float rolloff)
{
    LOCK();
    if (distance > 0.0f) s_distance = distance;
    if (rolloff >= 0.0f) s_rolloff = rolloff;
    UNLOCK();
}

int btvs_audio_voice_new(int is_stream)
{
    int i, id = -1;
    LOCK();
    for (i = 0; i < MAX_VOICES; i++) {
        if (!s_voice[i].used) {
            Voice *v = &s_voice[i];
            memset(v, 0, sizeof *v);
            v->used = 1;
            v->is_stream = is_stream;
            v->tag = BTVS_FMT_PCM; v->channels = 1; v->rate = 22050; v->bits = 16; v->block = 2;
            v->cat = is_stream ? BTVS_CAT_MUSIC : BTVS_CAT_EFFECTS;
            v->mode3d = 2;
            v->mind = 1.0f; v->maxd = 1e9f;
            v->cblock = -1;
            if (is_stream) {
                v->q = (Packet *)calloc(MAX_PACKETS, sizeof(Packet));
                v->playing = 1;              /* a stream plays whatever it is given */
                if (!v->q) { v->used = 0; break; }
            }
            id = i;
            break;
        }
    }
    UNLOCK();
    return id;
}

void btvs_audio_voice_free(int id)
{
    Voice *v;
    LOCK();
    if ((v = vget(id)) != NULL) {
        if (v->is_stream) {
            while (v->qcount) packet_done(v, PKT_FLUSHED);
            free(v->q);
        }
        memset(v, 0, sizeof *v);
    }
    UNLOCK();
}

void btvs_audio_voice_format(int id, int tag, int channels, int rate, int bits, int block_align)
{
    Voice *v;
    LOCK();
    if ((v = vget(id)) != NULL) {
        v->tag = tag;
        v->channels = channels < 1 ? 1 : (channels > 2 ? 2 : channels);
        v->rate = rate > 0 ? rate : 22050;
        v->bits = bits == 8 ? 8 : 16;
        v->block = tag == BTVS_FMT_XBOX_ADPCM ? 36 * v->channels
                 : (block_align > 0 ? block_align : (int)frame_bytes(v));
        v->cblock = -1;
        voice_measure(v);
    }
    UNLOCK();
}

void btvs_audio_voice_category(int id, int cat)
{
    Voice *v;
    LOCK();
    if ((v = vget(id)) != NULL && cat >= 0 && cat < BTVS_CAT_COUNT) v->cat = cat;
    UNLOCK();
}

void btvs_audio_voice_volume(int id, int mb)
{
    Voice *v;
    LOCK();
    if ((v = vget(id)) != NULL) v->volume = mb;
    UNLOCK();
}

void btvs_audio_voice_headroom(int id, int mb)
{
    Voice *v;
    LOCK();
    if ((v = vget(id)) != NULL) v->headroom = mb < 0 ? 0 : mb;
    UNLOCK();
}

void btvs_audio_voice_frequency(int id, int hz)
{
    Voice *v;
    LOCK();
    if ((v = vget(id)) != NULL) v->freq = hz < 0 ? 0 : (hz > 192000 ? 192000 : hz);
    UNLOCK();
}

void btvs_audio_voice_3d(int id, int mode, const float pos[3], float min_dist, float max_dist)
{
    Voice *v;
    LOCK();
    if ((v = vget(id)) != NULL) {
        v->mode3d = (mode < 0 || mode > 2) ? 2 : mode;
        if (pos) memcpy(v->pos, pos, sizeof v->pos);
        if (min_dist > 0.0f) v->mind = min_dist;
        if (max_dist > 0.0f) v->maxd = max_dist;
    }
    UNLOCK();
}

void btvs_audio_voice_data(int id, const void *data, uint32_t bytes)
{
    Voice *v;
    LOCK();
    if ((v = vget(id)) != NULL && !v->is_stream) {
        v->data = (const uint8_t *)data;
        v->bytes = data ? bytes : 0;
        v->cblock = -1;
        v->idx = 0; v->primed = 0; v->ended = 0;
        if (!data) v->playing = 0;
        voice_measure(v);
    }
    UNLOCK();
}

void btvs_audio_voice_play(int id, int looping)
{
    Voice *v;
    LOCK();
    if ((v = vget(id)) != NULL && !v->is_stream) {
        v->looping = looping;
        if (!v->playing) {
            v->playing = 1;
            v->ended = 0;
            if (!v->primed) v->gain_set = 0;     /* a fresh start does not ramp */
            v->wraps = 0;
            if (s_log) {
                float gl, gr;
                voice_gains(v, &gl, &gr);
                alog("play #%d %s %d Hz %dch %.2f s vol %d %s%s gain L %.3f R %.3f", id,
                     v->tag == BTVS_FMT_XBOX_ADPCM ? "ADPCM" : "PCM", v->freq ? v->freq : v->rate,
                     v->channels, (double)v->frames / (double)(v->freq ? v->freq : v->rate), v->volume,
                     v->cat == BTVS_CAT_SPEECH ? "speech" : v->cat == BTVS_CAT_MUSIC ? "music" : "effect",
                     looping ? " loop" : "", gl, gr);
                if (v->mode3d != 2)
                    alog("     #%d 3D mode %d at %.1f %.1f %.1f, listener %.1f %.1f %.1f front %.2f %.2f %.2f, min %.0f max %.0f",
                         id, v->mode3d, v->pos[0], v->pos[1], v->pos[2], s_lpos[0], s_lpos[1], s_lpos[2],
                         s_lfront[0], s_lfront[1], s_lfront[2], v->mind, v->maxd);
            }
        }
    }
    UNLOCK();
}

void btvs_audio_voice_stop(int id)
{
    Voice *v;
    LOCK();
    if ((v = vget(id)) != NULL && !v->is_stream) {
        if (s_log && v->playing && v->looping)
            alog("stop #%d: the loop had started again %u times", id, v->wraps);
        else if (s_log && v->playing && v->frames)
            alog("stop #%d: cut off at %.2f of %.2f s", id, (double)v->idx / (double)v->rate,
                 (double)v->frames / (double)v->rate);
        v->playing = 0;
    }
    UNLOCK();
}

void btvs_audio_voice_rewind(int id)
{
    Voice *v;
    LOCK();
    if ((v = vget(id)) != NULL && !v->is_stream) { v->idx = 0; v->primed = 0; v->ended = 0; }
    UNLOCK();
}

int btvs_audio_voice_playing(int id)
{
    Voice *v;
    int r = 0;
    LOCK();
    if ((v = vget(id)) != NULL) r = v->is_stream ? (v->qcount > 0 && !v->paused) : v->playing;
    UNLOCK();
    return r;
}

uint32_t btvs_audio_voice_cursor(int id)
{
    Voice *v;
    uint32_t r = 0;
    LOCK();
    if ((v = vget(id)) != NULL && !v->is_stream && v->frames) {
        /* idx is one or two frames ahead of what is being heard */
        uint32_t f = v->idx > 1 ? v->idx - 2 : 0;
        if (f >= v->frames) f = v->frames - 1;
        r = v->tag == BTVS_FMT_XBOX_ADPCM ? (uint32_t)((uint64_t)f * (uint32_t)v->block / 64u)
                                          : f * frame_bytes(v);
    }
    UNLOCK();
    return r;
}

int btvs_audio_stream_submit(int id, const void *data, uint32_t bytes,
                             volatile uint32_t *status, volatile uint32_t *completed)
{
    Voice *v;
    int ok = 0;
    LOCK();
    if ((v = vget(id)) != NULL && v->is_stream && v->qcount < MAX_PACKETS) {
        Packet *p = &v->q[(v->qhead + v->qcount) % MAX_PACKETS];
        p->data = (uint8_t *)malloc(bytes ? bytes : 1);
        if (p->data) {
            if (bytes) memcpy(p->data, data, bytes);
            p->bytes = bytes;
            p->status = status;
            p->completed = completed;
            if (status) *status = PKT_PENDING;
            if (completed) *completed = 0;
            v->qbytes += bytes;
            v->qcount++;
            ok = 1;
        }
    }
    UNLOCK();
    return ok;
}

uint32_t btvs_audio_stream_starves(int id)
{
    Voice *v;
    uint32_t r = 0;
    LOCK();
    if ((v = vget(id)) != NULL && v->is_stream) r = v->starves;
    UNLOCK();
    return r;
}

uint64_t btvs_audio_stream_skipped(int id)
{
    Voice *v;
    uint64_t r = 0;
    LOCK();
    if ((v = vget(id)) != NULL && v->is_stream) r = v->skipped;
    UNLOCK();
    return r;
}

void btvs_audio_stream_flush(int id)
{
    Voice *v;
    LOCK();
    if ((v = vget(id)) != NULL && v->is_stream) {
        v->fed = 0;                  /* running dry after a flush is not a starve */
        v->late = 0; v->debt = 0.0;
        while (v->qcount) packet_done(v, PKT_FLUSHED);
        v->primed = 0; v->ended = 0;
    }
    UNLOCK();
}

void btvs_audio_stream_pause(int id, int paused)
{
    Voice *v;
    LOCK();
    if ((v = vget(id)) != NULL && v->is_stream) v->paused = paused != 0;
    UNLOCK();
}

int btvs_audio_stream_queued(int id)
{
    Voice *v;
    int r = 0;
    LOCK();
    if ((v = vget(id)) != NULL && v->is_stream) r = v->qcount;
    UNLOCK();
    return r;
}

uint64_t btvs_audio_stream_played(int id)
{
    Voice *v;
    uint64_t r = 0;
    LOCK();
    if ((v = vget(id)) != NULL && v->is_stream) r = v->played;
    UNLOCK();
    return r;
}

void btvs_audio_stats(struct btvs_audio_stats *out)
{
    int i;
    memset(out, 0, sizeof *out);
    LOCK();
    for (i = 0; i < MAX_VOICES; i++) {
        if (!s_voice[i].used) continue;
        out->voices++;
        if (s_voice[i].is_stream) out->streams++;
        if (s_voice[i].playing && !s_voice[i].paused &&
            (s_voice[i].is_stream ? s_voice[i].qcount > 0 : 1)) out->playing++;
    }
    out->frames_mixed = (uint64_t)s_clock;
    out->frames_clipped = s_clipped;
    out->underruns = s_underruns;
    out->device_restarts = s_restarts;
    out->stalled_frames = s_stalled;
    out->device_glitches = s_glitches;
    out->peak = s_peak;
    s_peak = 0.0f;
    UNLOCK();
}
