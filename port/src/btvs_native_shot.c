/**
 * Screenshots: the frame the renderer shows, as a PNG.
 *
 * btvs_screenshot(path) asks for the next frame shown to be written to
 * `path` (the Report a Problem package, a player's F12, test runs).
 * BTVS_SHOT_AT=<seconds[,seconds...]> writes shot_<seconds>.png into
 * BTVS_SHOT_DIR (default: the current folder's runs\) at those times after
 * start -- how test runs look at the picture, the window being hidden.
 * BTVS_SHOT_EVERY=<seconds> writes one that often.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gpu.h"
#include "btvs.h"
#include "btvs_native.h"

GpuTexture *btvs_native_frame_texture(void);

/* ── PNG (no compression library here: zlib "stored" blocks) ─────────── */

static uint32_t s_crc_tab[256];

static uint32_t crc_update(uint32_t c, const uint8_t *p, size_t n)
{
    size_t i;
    if (!s_crc_tab[1]) {
        uint32_t k, j;
        for (k = 0; k < 256; k++) {
            uint32_t v = k;
            for (j = 0; j < 8; j++)
                v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            s_crc_tab[k] = v;
        }
    }
    for (i = 0; i < n; i++)
        c = s_crc_tab[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n)
{
    uint8_t h[8], c[4];
    uint32_t crc;
    put_be32(h, n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (n)
        fwrite(data, 1, n, f);
    crc = crc_update(0xFFFFFFFFu, h + 4, 4);
    crc = crc_update(crc, data, n) ^ 0xFFFFFFFFu;
    put_be32(c, crc);
    fwrite(c, 1, 4, f);
}

/* `bgra`: rows of w pixels (B, G, R, A in memory), stride_px pixels apart.
 * Written as 24-bit RGB. 1 on success. */
int btvs_png_write(const char *path, const uint32_t *bgra, uint32_t w, uint32_t h, uint32_t stride_px)
{
    size_t row = 1 + (size_t)w * 3, raw = row * h, blocks = (raw + 65534) / 65535, zn = 2 + raw + blocks * 5 + 4, o = 0, i;
    uint8_t *z = (uint8_t *)malloc(zn), *img = (uint8_t *)malloc(raw), ihdr[13];
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    uint32_t x, y, a = 1, b = 0;
    FILE *f;
    if (!z || !img || !(f = fopen(path, "wb"))) {
        free(z); free(img);
        return 0;
    }
    for (y = 0; y < h; y++) {
        uint8_t *d = img + row * y;
        const uint32_t *s = bgra + (size_t)y * stride_px;
        *d++ = 0;
        for (x = 0; x < w; x++) {
            uint32_t p = s[x];
            *d++ = (uint8_t)(p >> 16); *d++ = (uint8_t)(p >> 8); *d++ = (uint8_t)p;
        }
    }
    z[o++] = 0x78; z[o++] = 0x01;
    for (i = 0; i < raw; i += 65535) {
        size_t n = raw - i < 65535 ? raw - i : 65535;
        z[o++] = (uint8_t)(i + n >= raw);
        z[o++] = (uint8_t)n; z[o++] = (uint8_t)(n >> 8);
        z[o++] = (uint8_t)~n; z[o++] = (uint8_t)(~n >> 8);
        memcpy(z + o, img + i, n);
        o += n;
    }
    for (i = 0; i < raw; i++) {                      /* adler-32 */
        a += img[i]; if (a >= 65521) a -= 65521;
        b += a; if (b >= 65521) b -= 65521;
    }
    put_be32(z + o, (b << 16) | a);
    o += 4;
    fwrite(sig, 1, 8, f);
    put_be32(ihdr, w); put_be32(ihdr + 4, h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)o);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(z); free(img);
    return 1;
}

/* ── requests ────────────────────────────────────────────────────────── */

static CRITICAL_SECTION s_lock;
static volatile LONG s_lock_ready;
static char s_want[MAX_PATH];
static volatile LONG s_done;                  /* shots written so far */

static void lock(void)
{
    if (InterlockedCompareExchange(&s_lock_ready, 1, 0) == 0) {
        InitializeCriticalSection(&s_lock);
        InterlockedExchange(&s_lock_ready, 2);
    }
    while (s_lock_ready != 2)
        Sleep(0);
    EnterCriticalSection(&s_lock);
}

int btvs_screenshot(const char *path)
{
    if (!path || !*path || strlen(path) >= MAX_PATH)
        return 0;
    lock();
    strcpy_s(s_want, sizeof s_want, path);
    LeaveCriticalSection(&s_lock);
    return 1;
}

int btvs_screenshot_count(void)
{
    return (int)s_done;
}

static int save_frame(const char *path)
{
    GpuTexture *tex = btvs_native_frame_texture(), *st;
    GpuTextureDesc td;
    uint32_t *px;
    int ok = 0;
    if (!tex)
        return 0;
    gpu_texture_desc(tex, &td);
    td.mips = 1;
    td.flags = GPU_TEX_READBACK;
    if (!(st = gpu_texture_create(&td, NULL)))
        return 0;
    gpu_texture_copy(st, tex);
    px = (uint32_t *)malloc((size_t)td.width * td.height * 4);
    if (px && gpu_texture_read(st, px, 0))
        ok = btvs_png_write(path, px, td.width, td.height, td.width);
    free(px);
    gpu_texture_release(st);
    btvs_log("gfx", "screenshot %s: %s (%ux%u)", path, ok ? "written" : "FAILED", td.width, td.height);
    if (ok)
        InterlockedIncrement(&s_done);
    return ok;
}

/* Once a frame, game thread, after the frame is complete (btvs_frame.c). */
void btvs_shot_frame(void)
{
    static double at[64];
    static int n = -1, next;
    static double every, every_next;
    char path[MAX_PATH];
    double now = btvs_frame_seconds();
    const char *dir;
    if (n < 0) {
        const char *e = getenv("BTVS_SHOT_AT");
        n = 0;
        while (e && *e && n < 64) {
            at[n++] = atof(e);
            e = strchr(e, ',');
            if (e) e++;
        }
        e = getenv("BTVS_SHOT_EVERY");
        every = e ? atof(e) : 0.0;
        every_next = every;
    }
    dir = getenv("BTVS_SHOT_DIR");
    if (!dir || !*dir)
        dir = "runs";
    while (next < n && now >= at[next]) {
        sprintf_s(path, sizeof path, "%s\\shot_%g.png", dir, at[next]);
        next++;
        if (next >= n || now < at[next])              /* (several due at once: the last one) */
            save_frame(path);
    }
    if (every > 0.0 && now >= every_next) {
        sprintf_s(path, sizeof path, "%s\\shot_%04d.png", dir, (int)(every_next + 0.5));
        every_next += every;
        save_frame(path);
    }
    if (s_want[0]) {
        lock();
        strcpy_s(path, sizeof path, s_want);
        s_want[0] = 0;
        LeaveCriticalSection(&s_lock);
        save_frame(path);
    }
}
