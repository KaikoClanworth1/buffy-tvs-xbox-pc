/* btvs_wma.c - WMA music.
 *
 * The game's music is 62 WMA v2 files (44.1 kHz stereo, 128 kbit/s). The game
 * reads the file itself and hands the bytes to the Xbox WMA decoder through a
 * callback (WmaCreateInMemoryDecoder); the decoder gives back PCM, which the
 * game queues on a DirectSound stream in 8 KB packets.
 *
 * The decoder (sections WMADEC and WMADECXM of the XBE) is ordinary x86 code
 * with no hardware under it, so it is NOT replaced: the translated code runs
 * as it is and its PCM arrives at the stream functions in btvs_audio_dsound.c.
 * No host codec (Media Foundation) is involved, so the music decodes to the
 * same samples as on the console and needs nothing installed on the PC.
 *
 * What this file holds is the proof: a self-test that drives the translated
 * decoder over a real music file, with no game running.
 *
 *   BTVS_WMA_TEST=<file.wma>   decode the whole file, play it through the
 *                              mixer (offline, as fast as it decodes), check
 *                              format and length, print a summary and exit
 *   BTVS_AUDIO_DUMP=<out.wav>  where the result goes
 *
 * Run it with port/tools/run.sh (main.c calls btvs_wma_selftest() before the
 * game's entry point). Exit code 0 = decoded and checked.
 */
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp_types.h"
#include "btvs.h"
#include "btvs_audio.h"

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
extern void btvs_audio_start(int flags);

/* translated game code */
extern void XapiInitProcess_0057CAE1(void);
extern void CWMAXMediaObject_ctor_005C3FF8(void);
extern void CWMAXMediaObject_InitializeIntermediate_005C3F4D(void);

/* While the self-test runs, indirect calls to this address (the game's own
 * WMAStreamCallback) are answered by btvs_wma_test_cb: see recomp_manual.c. */
#define TEST_CB_VA 0x00067100u
uint32_t btvs_wma_test_cb_va;

#define CHUNK 0x10000u

static const uint8_t *s_file;
static uint32_t s_file_size;
static uint32_t s_chunk[2], s_which;
static uint64_t s_cb_calls, s_cb_bytes;

/* DWORD CALLBACK cb(LPVOID ctx, DWORD offset, DWORD num_bytes, LPVOID *ppData)
 * The decoder may still be looking at the previous chunk when it asks for the
 * next, so two buffers take turns (the game does the same). */
void btvs_wma_test_cb(void)
{
    uint32_t off = MEM32(g_esp + 8), n = MEM32(g_esp + 12), pp = MEM32(g_esp + 16);
    uint32_t buf = s_chunk[s_which];
    s_which ^= 1;
    if (off >= s_file_size) n = 0;
    else if (n > s_file_size - off) n = s_file_size - off;
    if (n > CHUNK) n = CHUNK;
    if (n) memcpy((void *)XBOX_PTR(buf), s_file + off, n);
    if (pp) MEM32(pp) = buf;
    s_cb_calls++;
    s_cb_bytes += n;
    g_eax = n;
    g_esp += 4 + 16;
}

/* Call translated code: stdcall/thiscall arguments are pushed right to left. */
static uint32_t gcall(recomp_func_t fn, uint32_t this_ptr, int nargs, const uint32_t *args)
{
    uint32_t saved = g_esp;
    int i;
    for (i = nargs - 1; i >= 0; i--) PUSH32(g_esp, args[i]);
    PUSH32(g_esp, 0);                       /* return address (unused) */
    g_ecx = this_ptr;
    fn();
    g_esp = saved;
    return g_eax;
}

static uint32_t vcall(uint32_t obj, uint32_t slot, int nargs, const uint32_t *args)
{
    recomp_func_t fn = recomp_lookup(MEM32(MEM32(obj) + slot));
    if (!fn) { fprintf(stderr, "[wma] no function for vtable slot +%X\n", slot); return 0x80004005u; }
    return gcall(fn, obj, nargs, args);
}

/* Play duration from the ASF File Properties object, in seconds (0 = unknown). */
static double asf_duration(const uint8_t *d, uint32_t size)
{
    static const uint8_t guid[16] = { 0xA1, 0xDC, 0xAB, 0x8C, 0x47, 0xA9, 0xCF, 0x11,
                                      0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 };
    uint32_t i, lim = size < 8192 ? size : 8192;
    for (i = 0; i + 104 <= lim; i++) {
        if (d[i] == guid[0] && !memcmp(d + i, guid, 16)) {
            uint64_t play, preroll;
            memcpy(&play, d + i + 64, 8);
            memcpy(&preroll, d + i + 80, 8);
            return (double)play / 1e7 - (double)preroll / 1e3;
        }
    }
    return 0.0;
}

int btvs_wma_selftest(void)
{
    const char *path = getenv("BTVS_WMA_TEST");
    FILE *f;
    uint8_t *data;
    uint32_t xmo, wfx, pkt, pcm, done, hr, a[4];
    uint32_t rate, ch, bits;
    uint64_t total = 0;
    int stream, packets = 0, fail = 0;
    static volatile uint32_t dummy_status;
    double want, got;
    struct btvs_audio_stats st;
    LARGE_INTEGER t0, t1, tf;

    if (!path || !*path) return 2;
    f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[wma] cannot open %s\n", path); return 2; }
    fseek(f, 0, SEEK_END);
    s_file_size = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (uint8_t *)malloc(s_file_size);
    if (!data || fread(data, 1, s_file_size, f) != s_file_size) { fclose(f); return 2; }
    fclose(f);
    s_file = data;
    want = asf_duration(data, s_file_size);
    fprintf(stderr, "[wma] %s: %u bytes, %.2f s by its header\n", path, s_file_size, want);

    btvs_audio_start(BTVS_AUDIO_OFFLINE);

    /* The decoder allocates with the C runtime's malloc, which sits on the
     * XAPI process heap; that heap is created by XapiInitProcess. */
    fprintf(stderr, "[wma] XapiInitProcess...\n");
    gcall(XapiInitProcess_0057CAE1, 0, 0, NULL);

    /* What WmaCreateInMemoryDecoder does, minus the game's operator new
     * (whose allocator is not up before the game starts). */
    xmo = xbox_HeapAlloc(0x100, 16);
    wfx = xbox_HeapAlloc(64, 16);
    pkt = xbox_HeapAlloc(64, 16);
    pcm = xbox_HeapAlloc(0x2000, 16);
    s_chunk[0] = xbox_HeapAlloc(CHUNK, 16);
    s_chunk[1] = xbox_HeapAlloc(CHUNK, 16);
    if (!xmo || !wfx || !pkt || !pcm || !s_chunk[0] || !s_chunk[1]) return 2;
    memset((void *)XBOX_PTR(xmo), 0, 0x100);
    memset((void *)XBOX_PTR(wfx), 0, 64);
    done = pkt + 32;
    btvs_wma_test_cb_va = TEST_CB_VA;

    gcall(CWMAXMediaObject_ctor_005C3FF8, xmo, 0, NULL);
    a[0] = TEST_CB_VA; a[1] = 0x12345678u; a[2] = 1000; a[3] = wfx;
    hr = gcall(CWMAXMediaObject_InitializeIntermediate_005C3F4D, xmo, 4, a);
    if ((int32_t)hr < 0) {
        fprintf(stderr, "[wma] FAIL: the decoder refused the file (0x%08X)\n", hr);
        return 1;
    }
    rate = MEM32(wfx + 4); ch = MEM16(wfx + 2); bits = MEM16(wfx + 14);
    fprintf(stderr, "[wma] decoder format: tag %u, %u Hz, %u channels, %u bits\n",
            MEM16(wfx), rate, ch, bits);
    if (MEM16(wfx) != 1 || !rate || ch < 1 || ch > 2 || bits != 16) {
        fprintf(stderr, "[wma] FAIL: unexpected output format\n");
        return 1;
    }

    stream = btvs_audio_voice_new(1);
    btvs_audio_voice_format(stream, BTVS_FMT_PCM, (int)ch, (int)rate, 16, (int)(ch * 2));

    QueryPerformanceFrequency(&tf);
    QueryPerformanceCounter(&t0);
    for (;;) {
        uint32_t n;
        /* XMEDIAPACKET { pvBuffer, dwMaxSize, pdwCompletedSize, pdwStatus, ctx, prtTimestamp } */
        MEM32(pkt) = pcm; MEM32(pkt + 4) = 0x2000; MEM32(pkt + 8) = done;
        MEM32(pkt + 12) = 0; MEM32(pkt + 16) = 0; MEM32(pkt + 20) = 0;
        MEM32(done) = 0;
        a[0] = xmo; a[1] = 0; a[2] = pkt;
        hr = vcall(xmo, 0x10, 3, a);                    /* Process(this, NULL, &pkt) */
        if ((int32_t)hr < 0) {
            fprintf(stderr, "[wma] Process returned 0x%08X after %llu bytes\n", hr, (unsigned long long)total);
            break;
        }
        n = MEM32(done);
        if (n) {
            btvs_audio_stream_submit(stream, (const void *)XBOX_PTR(pcm), n, &dummy_status, NULL);
            total += n;
            packets++;
            /* play what has arrived, keeping a little queued like the game does */
            while (btvs_audio_stream_queued(stream) > 1) btvs_audio_render(BTVS_AUDIO_PERIOD);
        }
        if (n < 0x2000) break;                          /* end of the file */
        if (total > 400u * 1024u * 1024u) { fprintf(stderr, "[wma] FAIL: runaway decode\n"); fail = 1; break; }
    }
    QueryPerformanceCounter(&t1);
    while (btvs_audio_stream_queued(stream) > 0) btvs_audio_render(BTVS_AUDIO_PERIOD);
    btvs_audio_render(BTVS_AUDIO_RATE / 4);

    got = (double)total / (double)(rate * ch * 2);
    btvs_audio_stats(&st);
    fprintf(stderr, "[wma] decoded %llu bytes in %d packets = %.2f s of sound (header says %.2f s), "
                    "%.1fx real time; read callback: %llu calls, %llu bytes; mix peak %.3f\n",
            (unsigned long long)total, packets, got, want,
            got / ((double)(t1.QuadPart - t0.QuadPart) / (double)tf.QuadPart + 1e-9),
            (unsigned long long)s_cb_calls, (unsigned long long)s_cb_bytes, st.peak);
    if (total == 0) { fprintf(stderr, "[wma] FAIL: nothing decoded\n"); fail = 1; }
    if (want > 0.5 && fabs(got - want) > 1.0) { fprintf(stderr, "[wma] FAIL: length is off\n"); fail = 1; }
    if (st.peak < 0.01f) { fprintf(stderr, "[wma] FAIL: the decoded sound is silent\n"); fail = 1; }
    if (btvs_audio_stream_played(stream) != total) { fprintf(stderr, "[wma] FAIL: the stream did not play every byte\n"); fail = 1; }
    btvs_audio_shutdown();
    fprintf(stderr, fail ? "[wma] FAILED\n" : "[wma] OK\n");
    btvs_wma_test_cb_va = 0;
    return fail;
}
