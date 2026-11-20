/* btvs_audio_test - plays real game sound data through the port's mixer into
 * a WAV file, with no game running and no audio device (offline mixer).
 *
 *   btvs_audio_test <out.wav> rpe <file.rpe> [first] [count]
 *       the dialogue lines of a .rpe container, one after another
 *   btvs_audio_test <out.wav> pak <file.pak> [first] [count]
 *       the RIFF/WAVE sounds found inside a .pak, one after another
 *   btvs_audio_test <out.wav> scene <file.rpe|file.pak>
 *       a mixing scene: a looping sound circling the listener in 3D, one-shots
 *       over it, a pitch sweep, a volume fade, and a PCM stream fed in packets
 *   btvs_audio_test <out.wav> live <file.rpe|file.pak> [seconds]
 *       a looping sound through the real-time mixer thread with BTVS_MUTE=1
 *       (no device): checks pacing, cursors and that no device is opened
 *   btvs_audio_test <out.wav> device <file.rpe|file.pak> [seconds]
 *       the same with the XAudio2 device open but the mix at zero volume
 *       (nothing audible): checks the device path keeps time without underruns
 *
 * Prints one line per sound and a summary; exit code 0 = everything checked out.
 */
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btvs_audio.h"

typedef struct {
    char     name[64];
    const uint8_t *data;
    uint32_t bytes;
    int      tag, channels, rate, bits, block;
} Sound;

static uint8_t *s_file;
static size_t   s_file_size;
static Sound   *s_sounds;
static int      s_nsounds;
static int      s_failures;

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static int load_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 0; }
    fseek(f, 0, SEEK_END);
    s_file_size = (size_t)_ftelli64(f);
    fseek(f, 0, SEEK_SET);
    s_file = (uint8_t *)malloc(s_file_size + 16);
    if (!s_file || fread(s_file, 1, s_file_size, f) != s_file_size) { fclose(f); return 0; }
    fclose(f);
    return 1;
}

/* Parse a RIFF/WAVE at p (at most `limit` bytes). */
static int parse_wave(const uint8_t *p, size_t limit, Sound *s)
{
    size_t o = 12, riff;
    int have_fmt = 0;
    if (limit < 44 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4)) return 0;
    riff = (size_t)rd32(p + 4) + 8;
    if (riff < limit) limit = riff;
    while (o + 8 <= limit) {
        uint32_t size = rd32(p + o + 4);
        if (o + 8 + size > limit) size = (uint32_t)(limit - o - 8);
        if (!memcmp(p + o, "fmt ", 4) && size >= 16) {
            s->tag = rd16(p + o + 8);
            s->channels = rd16(p + o + 10);
            s->rate = (int)rd32(p + o + 12);
            s->block = rd16(p + o + 20);
            s->bits = rd16(p + o + 22);
            have_fmt = 1;
        } else if (!memcmp(p + o, "data", 4)) {
            s->data = p + o + 8;
            s->bytes = size;
            return have_fmt;
        }
        o += 8 + size + (size & 1);
    }
    return 0;
}

static void add_sound(const Sound *s)
{
    s_sounds = (Sound *)realloc(s_sounds, sizeof(Sound) * (size_t)(s_nsounds + 1));
    s_sounds[s_nsounds++] = *s;
}

/* .rpe: u32 version (1), u32 count, 24 bytes reserved; then `count` entries of
 * { char name[32]; u32 offset; u32 size; u32 stamp; }; each entry is a RIFF/WAVE. */
static int load_rpe(const char *path)
{
    uint32_t n, i;
    if (!load_file(path) || s_file_size < 32) return 0;
    n = rd32(s_file + 4);
    if (rd32(s_file) != 1 || 32 + (size_t)n * 44 > s_file_size) {
        fprintf(stderr, "%s: not a .rpe container\n", path);
        return 0;
    }
    for (i = 0; i < n; i++) {
        const uint8_t *e = s_file + 32 + i * 44;
        uint32_t off = rd32(e + 32), size = rd32(e + 36);
        Sound s;
        memset(&s, 0, sizeof s);
        memcpy(s.name, e, 32);
        if ((size_t)off + size > s_file_size || !parse_wave(s_file + off, size, &s)) {
            fprintf(stderr, "%s: entry %u (%s) is not a WAVE\n", path, i, s.name);
            s_failures++;
            continue;
        }
        add_sound(&s);
    }
    return 1;
}

/* .pak: the sounds are stored as whole RIFF/WAVE files; the file name precedes
 * each one. Found by scanning (the pak index is the Extras/Logs areas' business). */
static int load_pak(const char *path)
{
    size_t i;
    if (!load_file(path)) return 0;
    for (i = 0; i + 44 < s_file_size; i++) {
        Sound s;
        if (s_file[i] != 'R' || memcmp(s_file + i, "RIFF", 4) || memcmp(s_file + i + 8, "WAVEfmt ", 8))
            continue;
        memset(&s, 0, sizeof s);
        if (!parse_wave(s_file + i, s_file_size - i, &s)) continue;
        {   /* the path string just before the RIFF, if there is one */
            size_t e = i, b;
            while (e > 0 && s_file[e - 1] == 0 && i - e < 16) e--;
            b = e;
            while (b > 0 && s_file[b - 1] >= 32 && s_file[b - 1] < 127 && e - b < 200) b--;
            if (e > b) {
                const char *nm = (const char *)s_file + b, *sl;
                size_t len = e - b;
                for (sl = nm + len; sl > nm && sl[-1] != '\\' && sl[-1] != '/'; sl--) {}
                len = (size_t)(nm + len - sl);
                if (len > 63) len = 63;
                memcpy(s.name, sl, len);
            } else {
                sprintf(s.name, "@%08zX", i);
            }
        }
        add_sound(&s);
        i += 12 + (s.data - (s_file + i)) + s.bytes - 13;
    }
    return 1;
}

static uint32_t sound_frames(const Sound *s)
{
    if (s->tag == BTVS_FMT_XBOX_ADPCM) return s->bytes / (uint32_t)(36 * s->channels) * 64u;
    return s->bytes / (uint32_t)(s->channels * (s->bits == 8 ? 1 : 2));
}

static int voice_for(const Sound *s)
{
    int v = btvs_audio_voice_new(0);
    btvs_audio_voice_format(v, s->tag, s->channels, s->rate, s->bits, s->block);
    btvs_audio_voice_data(v, s->data, s->bytes);
    return v;
}

static void check(int ok, const char *what, const char *name)
{
    if (!ok) { s_failures++; printf("  FAIL: %s (%s)\n", what, name); }
}

/* Each sound alone, start to finish; checks length, cursor and that it is heard. */
static void play_sequence(int first, int count)
{
    int i, played = 0;
    double total = 0;
    for (i = first; i < s_nsounds && played < count; i++, played++) {
        const Sound *s = &s_sounds[i];
        struct btvs_audio_stats st;
        uint32_t frames = sound_frames(s), out_frames, mid, cur;
        uint64_t t0;
        int v;
        if (s->tag != BTVS_FMT_XBOX_ADPCM && s->tag != BTVS_FMT_PCM) {
            printf("%-28s format 0x%04X not supported\n", s->name, s->tag);
            s_failures++;
            continue;
        }
        v = voice_for(s);
        btvs_audio_stats(&st);
        t0 = btvs_audio_clock();
        btvs_audio_voice_play(v, 0);
        out_frames = (uint32_t)((uint64_t)frames * BTVS_AUDIO_RATE / (uint32_t)s->rate);
        mid = out_frames / 2;
        btvs_audio_render((int)mid);
        cur = btvs_audio_voice_cursor(v);
        check(btvs_audio_voice_playing(v), "stopped before its end", s->name);
        check(cur > s->bytes / 2 - s->bytes / 20 - 64 && cur < s->bytes / 2 + s->bytes / 20 + 64,
              "cursor not at the middle at half time", s->name);
        btvs_audio_render((int)(out_frames - mid) + BTVS_AUDIO_PERIOD);
        check(!btvs_audio_voice_playing(v), "still playing after its length", s->name);
        btvs_audio_stats(&st);
        check(st.peak > 0.001f, "silent", s->name);
        printf("%-28s %s %5d Hz %dch %7u bytes %6.2f s  peak %.3f\n", s->name,
               s->tag == BTVS_FMT_XBOX_ADPCM ? "ADPCM" : "PCM  ", s->rate, s->channels, s->bytes,
               (double)frames / s->rate, st.peak);
        total += (double)(btvs_audio_clock() - t0) / BTVS_AUDIO_RATE;
        btvs_audio_voice_free(v);
        btvs_audio_render(BTVS_AUDIO_RATE / 10);        /* a short gap */
    }
    printf("%d sounds, %.1f s\n", played, total);
}

static const Sound *longest(int skip)
{
    int i, best = -1;
    for (i = 0; i < s_nsounds; i++) {
        if (i == skip) continue;
        if (best < 0 || sound_frames(&s_sounds[i]) > sound_frames(&s_sounds[best])) best = i;
    }
    return best >= 0 ? &s_sounds[best] : NULL;
}

static void scene(void)
{
    const Sound *loop = longest(-1), *shot = &s_sounds[0];
    static int16_t tone[4410 * 2];
    static volatile uint32_t status[64], done[64];
    float pos[3] = { 0, 0, 0 }, front[3] = { 0, 0, 1 }, top[3] = { 0, 1, 0 };
    struct btvs_audio_stats st;
    int vloop, vshot = -1, vstream, t, i, npk = 0, ndone = 0;
    const int ticks = 12 * 100;                         /* 12 s in 10 ms steps */

    btvs_audio_listener(pos, front, top);
    vloop = voice_for(loop);
    btvs_audio_voice_play(vloop, 1);

    /* a 44.1 kHz stereo PCM stream, 100 ms packets: 440 Hz left, 660 Hz right */
    vstream = btvs_audio_voice_new(1);
    btvs_audio_voice_format(vstream, BTVS_FMT_PCM, 2, 44100, 16, 4);
    btvs_audio_voice_volume(vstream, -1200);

    for (t = 0; t < ticks; t++) {
        float sec = (float)t / 100.0f, p[3];
        /* the loop circles the listener at 5 m, once every 4 s */
        p[0] = 5.0f * sinf(sec * 1.5708f); p[1] = 0; p[2] = 5.0f * cosf(sec * 1.5708f);
        btvs_audio_voice_3d(vloop, 0, p, 2.0f, 50.0f);
        /* 4-6 s: pitch sweep up an octave; 8-10 s: fade out and back */
        if (sec >= 4.0f && sec < 6.0f)
            btvs_audio_voice_frequency(vloop, (int)((float)loop->rate * (1.0f + (sec - 4.0f) / 2.0f)));
        else
            btvs_audio_voice_frequency(vloop, 0);
        if (sec >= 8.0f && sec < 10.0f)
            btvs_audio_voice_volume(vloop, (int)(-3000.0f * (1.0f - fabsf(sec - 9.0f))));
        /* a one-shot every second */
        if (t % 100 == 50) {
            if (vshot >= 0) btvs_audio_voice_free(vshot);
            shot = &s_sounds[(t / 100) % s_nsounds];
            vshot = voice_for(shot);
            btvs_audio_voice_play(vshot, 0);
        }
        /* keep three packets queued on the stream between 1 s and 7 s */
        while (sec >= 1.0f && sec < 7.0f && btvs_audio_stream_queued(vstream) < 3 && npk < 64) {
            for (i = 0; i < 4410; i++) {
                int n = npk * 4410 + i;
                tone[i * 2]     = (int16_t)(12000.0 * sin(n * 6.283185307 * 440.0 / 44100.0));
                tone[i * 2 + 1] = (int16_t)(12000.0 * sin(n * 6.283185307 * 660.0 / 44100.0));
            }
            btvs_audio_stream_submit(vstream, tone, sizeof tone, &status[npk], &done[npk]);
            npk++;
        }
        btvs_audio_render(BTVS_AUDIO_PERIOD);
    }
    btvs_audio_render(BTVS_AUDIO_RATE);
    for (i = 0; i < npk; i++) {
        if (status[i] == 0 && done[i] == sizeof tone) ndone++;
    }
    check(ndone == npk && npk > 50, "stream packets not all completed", "stream");
    check(btvs_audio_stream_played(vstream) == (uint64_t)npk * sizeof tone, "stream byte count", "stream");
    check(btvs_audio_voice_playing(vloop), "the looping sound stopped", loop->name);
    btvs_audio_stats(&st);
    printf("scene: loop '%s', %d stream packets (%d completed), clipped frames %llu, peak %.3f\n",
           loop->name, npk, ndone, (unsigned long long)st.frames_clipped, st.peak);
    btvs_audio_voice_free(vloop);
    if (vshot >= 0) btvs_audio_voice_free(vshot);
    btvs_audio_voice_free(vstream);
}

/* Real-time: the mixer thread paces itself. Checks that the clock and a
 * voice's cursor follow the wall clock, muted or not. */
static void live(double seconds, int device)
{
    const Sound *s = longest(-1);
    LARGE_INTEGER f, a, b;
    uint64_t c0, c1;
    uint32_t cur;
    double wall, mixed, expect;
    struct btvs_audio_stats st;
    int v = voice_for(s);

    uint64_t under0;
    int i;

    QueryPerformanceFrequency(&f);
    /* let the device start: wait until the clock has been running for half a
     * second (the first XAudio2 session of a boot can take seconds to open) */
    for (i = 0; i < 100 && btvs_audio_clock() < BTVS_AUDIO_RATE / 2; i++) Sleep(100);
    btvs_audio_stats(&st);
    under0 = st.underruns;
    btvs_audio_voice_play(v, 1);
    QueryPerformanceCounter(&a);
    c0 = btvs_audio_clock();
    {   /* watch the clock in 100 ms steps and report any step that stalls */
        int steps = (int)(seconds * 10.0), k;
        uint64_t prev = c0;
        for (k = 0; k < steps; k++) {
            uint64_t now;
            Sleep(100);
            now = btvs_audio_clock();
            if (now - prev < BTVS_AUDIO_RATE / 25)
                printf("  stall: at %.1f s only %d ms were mixed in 100 ms
", (k + 1) / 10.0,
                       (int)((now - prev) * 1000 / BTVS_AUDIO_RATE));
            prev = now;
        }
    }
    QueryPerformanceCounter(&b);
    c1 = btvs_audio_clock();
    cur = btvs_audio_voice_cursor(v);
    wall = (double)(b.QuadPart - a.QuadPart) / (double)f.QuadPart;
    mixed = (double)(c1 - c0) / BTVS_AUDIO_RATE;
    btvs_audio_stats(&st);
    printf("live: wall %.3f s, mixed %.3f s (%+.1f ms), audible %d, underruns %llu, peak %.3f\n",
           wall, mixed, (mixed - wall) * 1000.0, btvs_audio_audible(),
           (unsigned long long)(st.underruns - under0), st.peak);
    check(fabs(mixed - wall) < 0.08, "the mixer does not follow real time", "clock");
    expect = fmod(mixed * s->rate, (double)sound_frames(s)) / (double)sound_frames(s) * s->bytes;
    check(fabs((double)cur - expect) < s->bytes * 0.02 + 0.1 * s->rate ||
          fabs((double)cur - expect) > s->bytes * 0.9, "cursor does not follow real time", s->name);
    if (device) {
        check(btvs_audio_audible(), "the XAudio2 device did not open", "device");
        check(st.underruns == under0, "the device queue ran empty", "device");
        check(st.peak == 0.0f, "the zero-volume mix is not silent", "device");
    } else {
        check(st.peak > 0.001f, "silent mix", s->name);
        check(!btvs_audio_audible(), "a device is open although BTVS_MUTE=1", "mute");
    }
    btvs_audio_voice_free(v);
}

int main(int argc, char **argv)
{
    const char *mode, *path, *ext;
    int is_live, is_device;
    if (argc < 4) {
        fprintf(stderr, "usage: btvs_audio_test <out.wav> rpe|pak|scene|live|device <file> [first|seconds] [count]\n");
        return 2;
    }
    mode = argv[2]; path = argv[3];
    ext = strrchr(path, '.');
    if (!strcmp(mode, "rpe") || (strcmp(mode, "pak") && ext && !_stricmp(ext, ".rpe"))) {
        if (!load_rpe(path)) return 2;
    } else if (!load_pak(path)) return 2;
    if (!s_nsounds) { fprintf(stderr, "no sounds found in %s\n", path); return 2; }
    printf("%s: %d sounds\n", path, s_nsounds);

    is_device = !strcmp(mode, "device");
    is_live = !strcmp(mode, "live") || is_device;
    _putenv_s("BTVS_AUDIO_DUMP", argv[1]);
    /* Tests never make a sound: "live" runs muted (no device), "device" opens
     * the XAudio2 device but mixes at zero volume, so only silence reaches it. */
    if (is_device) _putenv_s("BTVS_MUTE", "0");
    else if (is_live) _putenv_s("BTVS_MUTE", "1");
    if (!btvs_audio_init(is_live ? 0 : BTVS_AUDIO_OFFLINE)) return 2;
    if (is_device) btvs_audio_set_master(0.0f);

    if (!strcmp(mode, "scene")) scene();
    else if (is_live) live(argc > 4 ? atof(argv[4]) : 3.0, is_device);
    else play_sequence(argc > 4 ? atoi(argv[4]) : 0, argc > 5 ? atoi(argv[5]) : 1 << 30);

    btvs_audio_shutdown();
    printf(s_failures ? "FAILED: %d check(s)\n" : "OK\n", s_failures);
    return s_failures ? 1 : 0;
}
