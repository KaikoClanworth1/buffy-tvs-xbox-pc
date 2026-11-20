/* btvs_audio_dsound.c - the game's Xbox DirectSound, on the port's mixer.
 *
 * On the console the DSOUND library drives the MCPX audio chip. Here the
 * library is cut off at its API: every entry point the game calls (the flat
 * C functions, and the IDirectSoundStream methods reached through the vtable)
 * is replaced by a host function below. Nothing under them (CMcpxAPU, voices,
 * the DSP image) runs any more. See docs/audio.md for the full list and how
 * the game uses each call.
 *
 * How the replacement is linked: each function is listed in
 * port/src/manual_functions.json, so the recompiler leaves its body out and
 * the name becomes an undefined symbol; "/alternatename" then points it at the
 * host function here. Until the translated code has been regenerated with
 * that list the original body is still in btvs_recomp.lib and wins, so this
 * file links either way (and the port has no sound before the regen).
 *
 * Calling convention: everything is stdcall, `this` first. [esp] is the
 * return address, the arguments follow, the callee pops them.
 */
#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp_types.h"
#include "btvs.h"
#include "btvs_audio.h"

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);

#define HOOK(name) \
    void btvs_ds_##name(void); \
    __pragma(comment(linker, "/alternatename:" #name "=btvs_ds_" #name)) \
    void btvs_ds_##name(void)

/* ---- guest-side objects ----------------------------------------------------
 * The game only ever holds these as opaque handles (and, for streams, calls
 * through the vtable in the first word). One 16-byte slot each:
 *   +0  vtable (streams: the game's own IDirectSoundStream vtable, whose
 *       entries are the functions replaced below), 0 otherwise
 *   +4  magic   +8  mixer voice id   +12 reference count */
#define SLOTS            400
#define SLOT_SIZE        16
#define MAGIC_DEVICE     0x44565442u      /* 'BTVD' */
#define MAGIC_BUFFER     0x42565442u      /* 'BTVB' */
#define MAGIC_STREAM     0x53565442u      /* 'BTVS' */
#define STREAM_VTABLE    0x0071295Cu      /* ??_7CDirectSoundStream@DirectSound@@6BIDirectSoundStream@@@ */

#define DS_OK                 0u
#define DSERR_OUTOFMEMORY     0x8007000Eu
#define DSERR_INVALIDPARAM    0x80070057u
#define DSBSTATUS_PLAYING     0x00000001u
#define DSBSTATUS_LOOPING     0x00000004u
#define DSBPLAY_LOOPING       0x00000001u
#define DSSTREAMSTATUS_READY    0x00000001u
#define DSSTREAMSTATUS_PLAYING  0x00010000u
#define DSSTREAMSTATUS_PAUSED   0x00020000u
#define DSSTREAMSTATUS_STARVED  0x00040000u
#define DS3D_DEFERRED         1u

static uint32_t s_pool, s_device, s_fx_desc;
static uint8_t  s_slot_used[SLOTS];
static uint32_t s_stream_max[SLOTS];
static CRITICAL_SECTION s_cs;
static int      s_ready, s_warned_event;
static int      s_trace = -1;               /* BTVS_AUDIO_LOG: stream traffic, once a second */
static struct { uint32_t packets, bytes, starves; uint64_t at; } s_tr[SLOTS];

/* the listener as the game last set it, pushed to the mixer on commit */
static float    s_lpos[3], s_lfront[3] = { 0, 0, 1 }, s_ltop[3] = { 0, 1, 0 };
static float    s_distance = 1.0f, s_rolloff = 1.0f;

static void dlog(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("[audio] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(ap);
}

static void ret_stdcall(uint32_t result, uint32_t arg_bytes)
{
    g_eax = result;
    g_esp += 4 + arg_bytes;
}

static uint32_t arg(int i) { return MEM32(g_esp + 4 + 4 * (uint32_t)i); }

static float farg(int i)
{
    uint32_t v = arg(i);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static float memf(uint32_t va)
{
    uint32_t v = MEM32(va);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

/* ---- settings ---------------------------------------------------------------
 * [audio] master / music / effects / speech, 0..100 (Menu area's store). The
 * slider is squared so that the middle of the range sounds like half volume. */
static float pct_gain(const char *key)
{
    int p = btvs_setting_int("audio", key, 100);
    float g;
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    g = (float)p / 100.0f;
    return g * g;
}

static void apply_settings(const char *section, const char *key, void *user)
{
    (void)section; (void)key; (void)user;
    btvs_audio_set_master(pct_gain("master"));
    btvs_audio_set_category(BTVS_CAT_MUSIC, pct_gain("music"));
    btvs_audio_set_category(BTVS_CAT_EFFECTS, pct_gain("effects"));
    btvs_audio_set_category(BTVS_CAT_SPEECH, pct_gain("speech"));
}

static void at_exit(void) { btvs_audio_shutdown(); }

/* Start the mixer and read the volume settings. Also called by the WMA
 * self-test, which has no DirectSoundCreate. */
void btvs_audio_start(int flags)
{
    if (s_ready) return;
    InitializeCriticalSection(&s_cs);
    btvs_audio_init(flags);
    apply_settings(NULL, NULL, NULL);
    btvs_setting_on_change("audio", NULL, apply_settings, NULL);
    atexit(at_exit);
    s_ready = 1;
}

/* ---- slots ----------------------------------------------------------------- */

static uint32_t slot_new(uint32_t magic, int voice)
{
    uint32_t i, va = 0;
    EnterCriticalSection(&s_cs);
    if (!s_pool) {
        s_pool = xbox_HeapAlloc(SLOTS * SLOT_SIZE + 64, 16);
        if (s_pool) {
            memset((void *)XBOX_PTR(s_pool), 0, SLOTS * SLOT_SIZE + 64);
            s_fx_desc = s_pool + SLOTS * SLOT_SIZE;      /* an empty DSEFFECTIMAGEDESC */
        }
    }
    for (i = 0; s_pool && i < SLOTS; i++) {
        if (!s_slot_used[i]) {
            s_slot_used[i] = 1;
            va = s_pool + i * SLOT_SIZE;
            MEM32(va) = magic == MAGIC_STREAM ? STREAM_VTABLE : 0;
            MEM32(va + 4) = magic;
            MEM32(va + 8) = (uint32_t)voice;
            MEM32(va + 12) = 1;
            break;
        }
    }
    LeaveCriticalSection(&s_cs);
    return va;
}

/* The mixer voice behind a guest handle, or -1. */
static int voice_of(uint32_t va, uint32_t magic)
{
    if (!s_pool || va < s_pool || va >= s_pool + SLOTS * SLOT_SIZE || ((va - s_pool) % SLOT_SIZE))
        return -1;
    if (!s_slot_used[(va - s_pool) / SLOT_SIZE] || MEM32(va + 4) != magic)
        return -1;
    return (int)MEM32(va + 8);
}

static uint32_t slot_addref(uint32_t va)
{
    return ++MEM32(va + 12);
}

static uint32_t slot_release(uint32_t va, uint32_t magic)
{
    int v = voice_of(va, magic);
    uint32_t n;
    if (v < 0 && magic != MAGIC_DEVICE) return 0;
    if (magic == MAGIC_DEVICE && (va != s_device || !va)) return 0;
    n = MEM32(va + 12);
    if (n) n--;
    MEM32(va + 12) = n;
    if (n == 0 && magic != MAGIC_DEVICE) {       /* the device handle stays valid */
        btvs_audio_voice_free(v);
        MEM32(va + 4) = 0;
        s_slot_used[(va - s_pool) / SLOT_SIZE] = 0;
    }
    return n;
}

/* WAVEFORMATEX in game memory -> the voice's format. Returns 0 if unsupported. */
static int set_format(int v, uint32_t wfx)
{
    uint32_t tag, ch, rate, block, bits;
    if (!wfx) return 0;
    tag = MEM16(wfx); ch = MEM16(wfx + 2); rate = MEM32(wfx + 4);
    block = MEM16(wfx + 12); bits = MEM16(wfx + 14);
    if ((tag != BTVS_FMT_PCM && tag != BTVS_FMT_XBOX_ADPCM) || ch < 1 || ch > 2) {
        dlog("unsupported sound format: tag 0x%04X, %u channels, %u Hz", tag, ch, rate);
        return 0;
    }
    btvs_audio_voice_format(v, (int)tag, (int)ch, (int)rate, (int)bits, (int)block);
    return 1;
}

/* ---- the device ------------------------------------------------------------ */

/* HRESULT DirectSoundCreate(LPGUID, LPDIRECTSOUND *ppDS, LPUNKNOWN) */
HOOK(DirectSoundCreate_005A8D07)
{
    uint32_t pp = arg(1);
    btvs_audio_start(0);
    if (!s_device) {
        s_device = slot_new(MAGIC_DEVICE, -1);
        dlog("DirectSound replaced by the port's mixer (XAudio2 output)");
    } else {
        slot_addref(s_device);
    }
    if (pp) MEM32(pp) = s_device;
    ret_stdcall(s_device ? DS_OK : DSERR_OUTOFMEMORY, 12);
}

HOOK(IDirectSound_Release_005A6D18)
{
    ret_stdcall(slot_release(arg(0), MAGIC_DEVICE), 4);
}

HOOK(IDirectSound_SetCooperativeLevel_005A6D13) { ret_stdcall(DS_OK, 12); }
HOOK(DirectSoundUseFullHRTF_005A6D44)           { ret_stdcall(DS_OK, 0); }
HOOK(IDirectSound_EnableHeadphones_005A7E50)    { ret_stdcall(DS_OK, 8); }

/* The mixer runs on its own thread; there is no work to pump. */
HOOK(DirectSoundDoWork_005A7835)                { ret_stdcall(DS_OK, 0); }

/* HRESULT DownloadEffectsImage(pDS, pvImage, dwSize, pImageLoc, ppImageDesc)
 * dsstdfx.bin is DSP microcode for the console's effects processor (reverb
 * sends, the 3D cross-talk filters). It is not run; see docs/audio.md. */
HOOK(IDirectSound_DownloadEffectsImage_005A7738)
{
    uint32_t pp = arg(4);
    if (pp) MEM32(pp) = s_fx_desc;
    ret_stdcall(DS_OK, 20);
}

/* HRESULT GetTime(pDS, REFERENCE_TIME *prt): the audio clock in 100 ns units. */
HOOK(IDirectSound_GetTime_005A775F)
{
    uint32_t p = arg(1);
    uint64_t t = btvs_audio_clock() * 625u / 3u;       /* frames at 48 kHz -> 100 ns */
    if (p) { MEM32(p) = (uint32_t)t; MEM32(p + 4) = (uint32_t)(t >> 32); }
    ret_stdcall(DS_OK, 8);
}

static void listener_push(void)
{
    btvs_audio_listener(s_lpos, s_lfront, s_ltop);
    btvs_audio_listener_factors(s_distance, s_rolloff);
}

HOOK(IDirectSound_SetPosition_005A8789)
{
    s_lpos[0] = farg(1); s_lpos[1] = farg(2); s_lpos[2] = farg(3);
    if (arg(4) != DS3D_DEFERRED) listener_push();
    ret_stdcall(DS_OK, 20);
}

HOOK(IDirectSound_SetOrientation_005A873F)
{
    s_lfront[0] = farg(1); s_lfront[1] = farg(2); s_lfront[2] = farg(3);
    s_ltop[0] = farg(4); s_ltop[1] = farg(5); s_ltop[2] = farg(6);
    if (arg(7) != DS3D_DEFERRED) listener_push();
    ret_stdcall(DS_OK, 32);
}

HOOK(IDirectSound_SetDistanceFactor_005A86F7)
{
    s_distance = farg(1);
    if (arg(2) != DS3D_DEFERRED) listener_push();
    ret_stdcall(DS_OK, 12);
}

HOOK(IDirectSound_SetRolloffFactor_005A87BE)
{
    s_rolloff = farg(1);
    if (arg(2) != DS3D_DEFERRED) listener_push();
    ret_stdcall(DS_OK, 12);
}

/* Doppler shift, listener velocity and the I3DL2 reverb are not modelled. */
HOOK(IDirectSound_SetDopplerFactor_005A871B)    { ret_stdcall(DS_OK, 12); }
HOOK(IDirectSound_SetVelocity_005A87E2)         { ret_stdcall(DS_OK, 20); }
HOOK(IDirectSound_SetI3DL2Listener_005A8817)    { ret_stdcall(DS_OK, 12); }

HOOK(IDirectSound_CommitDeferredSettings_005A7E6C)
{
    listener_push();
    ret_stdcall(DS_OK, 4);
}

/* ---- buffers --------------------------------------------------------------- */

/* HRESULT CreateSoundBuffer(pDS, const DSBUFFERDESC *, LPDIRECTSOUNDBUFFER *, LPUNKNOWN)
 * DSBUFFERDESC { dwSize, dwFlags, dwBufferBytes, lpwfxFormat, lpMixBins, dwInputMixBin }
 * The game always asks for 0 bytes and attaches its sample with SetBufferData. */
HOOK(IDirectSound_CreateSoundBuffer_005A8B4D)
{
    uint32_t desc = arg(1), pp = arg(2), va = 0, hr = DSERR_INVALIDPARAM;
    int v;
    if (pp) MEM32(pp) = 0;
    if (desc && pp) {
        v = btvs_audio_voice_new(0);
        if (v < 0) {
            hr = DSERR_OUTOFMEMORY;
        } else if (!set_format(v, MEM32(desc + 12)) || (va = slot_new(MAGIC_BUFFER, v)) == 0) {
            btvs_audio_voice_free(v);
        } else {
            MEM32(pp) = va;
            hr = DS_OK;
        }
    }
    ret_stdcall(hr, 16);
}

HOOK(IDirectSoundBuffer_Release_005A6D2E)
{
    ret_stdcall(slot_release(arg(0), MAGIC_BUFFER), 4);
}

/* HRESULT SetBufferData(pBuffer, LPVOID pvData, DWORD dwBytes): the sample
 * stays in game memory and is decoded from there as it plays. (NULL, 0)
 * detaches it; the game does that before it frees a sample. */
HOOK(IDirectSoundBuffer_SetBufferData_005A8837)
{
    int v = voice_of(arg(0), MAGIC_BUFFER);
    uint32_t data = arg(1), bytes = arg(2);
    if (v >= 0) {
        if (!data || !bytes) {
            btvs_audio_voice_data(v, NULL, 0);
        } else {
            /* Dialogue lines are RIFF files with lip-sync chunks ("wrds",
             * "phon") right after the sample data; effects have none. That
             * is what tells speech from effects for the volume sliders. */
            /* (CSample keeps the whole file and points into it; its chunk
             * walk does not pad odd sizes.) */
            const uint8_t *end = (const uint8_t *)XBOX_PTR(data + bytes);
            int speech = !memcmp(end, "wrds", 4) || !memcmp(end, "phon", 4);
            btvs_audio_voice_category(v, speech ? BTVS_CAT_SPEECH : BTVS_CAT_EFFECTS);
            btvs_audio_voice_data(v, (const void *)XBOX_PTR(data), bytes);
        }
    }
    ret_stdcall(DS_OK, 12);
}

/* HRESULT Play(pBuffer, dwReserved1, dwReserved2, dwFlags) */
HOOK(IDirectSoundBuffer_Play_005A77B3)
{
    int v = voice_of(arg(0), MAGIC_BUFFER);
    if (v >= 0) btvs_audio_voice_play(v, (arg(3) & DSBPLAY_LOOPING) != 0);
    ret_stdcall(DS_OK, 16);
}

HOOK(IDirectSoundBuffer_Stop_005A77D7)
{
    int v = voice_of(arg(0), MAGIC_BUFFER);
    if (v >= 0) btvs_audio_voice_stop(v);
    ret_stdcall(DS_OK, 4);
}

HOOK(IDirectSoundBuffer_GetStatus_005A77EF)
{
    int v = voice_of(arg(0), MAGIC_BUFFER);
    uint32_t p = arg(1);
    if (p) MEM32(p) = (v >= 0 && btvs_audio_voice_playing(v)) ? DSBSTATUS_PLAYING : 0;
    ret_stdcall(DS_OK, 8);
}

/* HRESULT GetCurrentPosition(pBuffer, LPDWORD pdwPlay, LPDWORD pdwWrite), in bytes */
HOOK(IDirectSoundBuffer_GetCurrentPosition_005A780B)
{
    int v = voice_of(arg(0), MAGIC_BUFFER);
    uint32_t play = arg(1), write = arg(2), pos = v >= 0 ? btvs_audio_voice_cursor(v) : 0;
    if (play) MEM32(play) = pos;
    if (write) MEM32(write) = pos;
    ret_stdcall(DS_OK, 12);
}

HOOK(IDirectSoundBuffer_SetVolume_005A777B)
{
    int v = voice_of(arg(0), MAGIC_BUFFER);
    if (v >= 0) btvs_audio_voice_volume(v, (int32_t)arg(1));
    ret_stdcall(DS_OK, 8);
}

HOOK(IDirectSoundBuffer_SetHeadroom_005A7797)
{
    int v = voice_of(arg(0), MAGIC_BUFFER);
    if (v >= 0) btvs_audio_voice_headroom(v, (int32_t)arg(1));
    ret_stdcall(DS_OK, 8);
}

HOOK(IDirectSoundBuffer_SetFrequency_005A7E84)
{
    int v = voice_of(arg(0), MAGIC_BUFFER);
    if (v >= 0) btvs_audio_voice_frequency(v, (int)arg(1));
    ret_stdcall(DS_OK, 8);
}

/* HRESULT SetAllParameters(pBuffer, const DS3DBUFFER *, DWORD dwApply)
 * DS3DBUFFER (XDK 4432, 0x4C bytes): +4 vPosition, +16 vVelocity, +28/+32 cone
 * angles, +36 vConeOrientation, +48 lConeOutsideVolume, +52 flMinDistance,
 * +56 flMaxDistance, +60 dwMode, +64 distance / +68 rolloff / +72 doppler.
 * The game sets min = max = 1e6 (no distance roll-off: it works out the
 * attenuation itself and sends it with SetVolume), so only the direction is
 * left for us. Cones, velocity and the per-buffer factors are not modelled. */
HOOK(IDirectSoundBuffer_SetAllParameters_005A7EA0)
{
    int v = voice_of(arg(0), MAGIC_BUFFER);
    uint32_t p = arg(1);
    if (v >= 0 && p) {
        float pos[3];
        pos[0] = memf(p + 4); pos[1] = memf(p + 8); pos[2] = memf(p + 12);
        btvs_audio_voice_3d(v, (int)MEM32(p + 60), pos, memf(p + 52), memf(p + 56));
    }
    ret_stdcall(DS_OK, 12);
}

/* Per-source reverb sends (occlusion, obstruction) are not modelled. */
HOOK(IDirectSoundBuffer_SetI3DL2Source_005A7EC0) { ret_stdcall(DS_OK, 12); }

/* ---- streams ---------------------------------------------------------------
 * Used for the WMA music (the game decodes 8 KB packets with the WMA decoder
 * and keeps up to 6 queued) and by the WMV movie player. */

/* HRESULT DirectSoundCreateStream(const DSSTREAMDESC *, LPDIRECTSOUNDSTREAM *)
 * DSSTREAMDESC { dwFlags, dwMaxAttachedPackets, lpwfxFormat, lpfnCallback,
 *                lpvContext, lpMixBins } */
HOOK(DirectSoundCreateStream_005A8D4E)
{
    uint32_t desc = arg(0), pp = arg(1), va = 0, hr = DSERR_INVALIDPARAM;
    int v;
    btvs_audio_start(0);
    if (pp) MEM32(pp) = 0;
    if (desc && pp) {
        v = btvs_audio_voice_new(1);
        if (v < 0) {
            hr = DSERR_OUTOFMEMORY;
        } else if (!set_format(v, MEM32(desc + 8)) || (va = slot_new(MAGIC_STREAM, v)) == 0) {
            btvs_audio_voice_free(v);
        } else {
            s_stream_max[(va - s_pool) / SLOT_SIZE] = MEM32(desc + 4) ? MEM32(desc + 4) : 1;
            if (MEM32(desc + 12))
                dlog("stream %08X wants a packet callback (%08X): not supported", va, MEM32(desc + 12));
            MEM32(pp) = va;
            hr = DS_OK;
            if (s_trace < 0) { const char *e = getenv("BTVS_AUDIO_LOG"); s_trace = e && *e && *e != '0'; }
            if (s_trace) {
                uint32_t w = MEM32(desc + 8);
                dlog("stream %08X: tag %u, %u Hz, %u ch, %u bits, %u packets max, flags %X", va,
                     MEM16(w), MEM32(w + 4), MEM16(w + 2), MEM16(w + 14), MEM32(desc + 4), MEM32(desc));
                memset(&s_tr[(va - s_pool) / SLOT_SIZE], 0, sizeof s_tr[0]);
            }
        }
    }
    ret_stdcall(hr, 8);
}

HOOK(DirectSound_CDirectSoundStream_AddRef_005A7387)
{
    uint32_t va = arg(0);
    ret_stdcall(voice_of(va, MAGIC_STREAM) >= 0 ? slot_addref(va) : 0, 4);
}

HOOK(DirectSound_CDirectSoundStream_Release_005A73CE)
{
    ret_stdcall(slot_release(arg(0), MAGIC_STREAM), 4);
}

/* HRESULT GetInfo(this, XMEDIAINFO *): { dwFlags, dwInputSize, dwOutputSize, dwMaxLookahead } */
HOOK(DirectSound_CDirectSoundStream_GetInfo_005A741C)
{
    uint32_t p = arg(1);
    if (p) {
        MEM32(p) = 0x1 | 0x4;        /* fixed sample size, input completes asynchronously */
        MEM32(p + 4) = 4;
        MEM32(p + 8) = 0;
        MEM32(p + 12) = 0;
    }
    ret_stdcall(DS_OK, 8);
}

HOOK(DirectSound_CDirectSoundStream_GetStatus_005A751D)
{
    uint32_t va = arg(0), p = arg(1), st = 0;
    int v = voice_of(va, MAGIC_STREAM);
    if (v >= 0) {
        int q = btvs_audio_stream_queued(v);
        if ((uint32_t)q < s_stream_max[(va - s_pool) / SLOT_SIZE]) st |= DSSTREAMSTATUS_READY;
        if (btvs_audio_voice_playing(v)) st |= DSSTREAMSTATUS_PLAYING;
        else if (q) st |= DSSTREAMSTATUS_PAUSED;
        else st |= DSSTREAMSTATUS_STARVED;
    }
    if (p) MEM32(p) = st;
    ret_stdcall(DS_OK, 8);
}

/* HRESULT Process(this, const XMEDIAPACKET *pInput, const XMEDIAPACKET *pOutput)
 * XMEDIAPACKET { pvBuffer, dwMaxSize, pdwCompletedSize, pdwStatus,
 *                hCompletionEvent / pContext, prtTimestamp }
 * The data is copied, so the game may reuse the buffer once the status it
 * polls leaves "pending"; that happens when the mixer has played the packet. */
HOOK(DirectSound_CDirectSoundStream_Process_005A756E)
{
    int v = voice_of(arg(0), MAGIC_STREAM);
    uint32_t in = arg(1), hr = DS_OK;
    if (v >= 0 && in) {
        uint32_t data = MEM32(in), size = MEM32(in + 4), done = MEM32(in + 8), status = MEM32(in + 12);
        if (MEM32(in + 16) && !s_warned_event) {
            s_warned_event = 1;
            dlog("a stream packet carries a completion event/context (%08X): not signalled", MEM32(in + 16));
        }
        if (!data ||
            !btvs_audio_stream_submit(v, (const void *)XBOX_PTR(data), size,
                                      status ? (volatile uint32_t *)XBOX_PTR(status) : NULL,
                                      done ? (volatile uint32_t *)XBOX_PTR(done) : NULL)) {
            if (status) MEM32(status) = 0x80004005u;
            hr = 0x80004005u;
            if (s_trace > 0) dlog("stream %08X: packet of %u bytes refused (%d queued)", arg(0), size,
                                  btvs_audio_stream_queued(v));
        } else if (s_trace > 0) {
            uint32_t i = (arg(0) - s_pool) / SLOT_SIZE;
            uint64_t now = btvs_audio_clock();
            s_tr[i].packets++; s_tr[i].bytes += size;
            if (now - s_tr[i].at >= BTVS_AUDIO_RATE) {
                uint32_t st = btvs_audio_stream_starves(v);
                dlog("stream %08X at %.1f s: %u packets, %u bytes in; %d queued; ran dry %u times; %llu late frames dropped so far",
                     arg(0), (double)now / BTVS_AUDIO_RATE, s_tr[i].packets, s_tr[i].bytes,
                     btvs_audio_stream_queued(v), st - s_tr[i].starves,
                     (unsigned long long)btvs_audio_stream_skipped(v));
                s_tr[i].packets = s_tr[i].bytes = 0; s_tr[i].starves = st; s_tr[i].at = now;
            }
        }
    }
    ret_stdcall(hr, 12);
}

HOOK(DirectSound_CDirectSoundStream_Discontinuity_005A7483) { ret_stdcall(DS_OK, 4); }

HOOK(DirectSound_CDirectSoundStream_Flush_005A74D0)
{
    int v = voice_of(arg(0), MAGIC_STREAM);
    if (v >= 0 && s_trace > 0) dlog("stream %08X: flush (%d queued)", arg(0), btvs_audio_stream_queued(v));
    if (v >= 0) btvs_audio_stream_flush(v);
    ret_stdcall(DS_OK, 4);
}

HOOK(IDirectSoundStream_SetVolume_005A782B)
{
    int v = voice_of(arg(0), MAGIC_STREAM);
    if (v >= 0) btvs_audio_voice_volume(v, (int32_t)arg(1));
    ret_stdcall(DS_OK, 8);
}

/* HRESULT Pause(pStream, DWORD dwPause): 0 resume, 1 pause */
HOOK(IDirectSoundStream_Pause_005A7830)
{
    int v = voice_of(arg(0), MAGIC_STREAM);
    if (v >= 0 && s_trace > 0) dlog("stream %08X: pause %u", arg(0), arg(1));
    if (v >= 0) btvs_audio_stream_pause(v, arg(1) != 0);
    ret_stdcall(DS_OK, 8);
}
