/**
 * The native renderer, game side (docs/graphics.md).
 *
 * The game draws through the Xbox Direct3D 8 library it was linked with
 * (XDK 4432), translated with the rest of the code. Nothing emulates the
 * Xbox GPU: the library runs in its own no-GPU mode (btvs_native_gpuwait.c)
 * and keeps doing what it does well -- resources, Lock/Unlock, the device
 * state. Its draw calls are hooked here; each one reads the state the library
 * holds at that moment (render states, texture stages, bound textures,
 * streams, the vertex shader) and hands the draw to the backend
 * (xboxrecomp/src/kernel/nv2a_native.inc), which draws it through the GPU
 * layer (Direct3D 11).
 *
 * Debug switches (environment): BTVS_NATIVE_LOG=1 a summary every 2 s,
 * BTVS_NATIVE_LOG=f<frame> that frame draw by draw, BTVS_D3D_DRAWS=1 also
 * runs the library's own draws (they only fill the pushbuffer).
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "recomp_types.h"
#include "gpu.h"
#include "btvs.h"
#include "btvs_native.h"

/* the backend (nv2a_native.inc, nv2a_pb_d3d11.inc) */
typedef struct { uint32_t va, stride, fmt; } NativeAttr;
typedef struct { uint32_t res, palette, data, format; GpuTexture *srv; } NativeTex;
typedef struct {
    uint32_t prim, count, index_va;
    NativeAttr attr[16];
    int passthrough;
    uint32_t vp_start;
    NativeTex tex[4];
    int pixel_shader;
    const uint32_t *rs, *tss;
    float clip_min, clip_max;
    int scratch;                       /* Draw...UP: the game reuses the memory within a frame */
} NativeDraw;
int  nv2a_native_draw(const NativeDraw *d, GpuTexture *rt, GpuTexture *ds,
                      uint32_t pw, uint32_t ph, uint32_t sw, uint32_t sh);
void nv2a_native_vp_load(uint32_t slot, const uint32_t *code, uint32_t dwords);
void nv2a_native_const(uint32_t slot, const float *v, uint32_t vectors);
void nv2a_native_const_get(uint32_t slot, float out[4]);
void nv2a_native_stats(unsigned long long out[4]);
uint32_t nv2a_native_resolve(uint32_t phys);
void nv2a_native_rs_layout(int first_deferred, int first_complex);
void nv2a_native_prewarm(void);
void nv2a_native_interp_setup(int on, GpuTexture *main);
void nv2a_native_interp_frame_end(void);
void nv2a_native_interp_clear(GpuTexture *rt, unsigned flags, const float c[4], float z, uint8_t stencil);
int  nv2a_gpu_native_begin(void);
void nv2a_gpu_native_scale(float *sx, float *sy);
void nv2a_gpu_native_present(GpuTexture *tex, uint32_t w, uint32_t h, uint32_t pw, uint32_t ph);
uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);

#define GUEST_W 640
#define GUEST_H 480

static int s_up;                             /* the device is up */
static int s_log = -1;
static long s_detail_frame = -1;
static double s_detail_time;
static unsigned long s_frame;

enum { PATH_INDEXED, PATH_UP, PATH_INDEXED_UP, PATH_CLEAR, PATH_RT, PATH_COUNT };
static const char *k_path[PATH_COUNT] = { "indexed", "up", "indexed-up", "clear", "rendertarget" };
static unsigned s_count[PATH_COUNT], s_verts, s_ff_skipped, s_no_target;

static int log_on(void)
{
    if (s_log < 0) {
        const char *e = getenv("BTVS_NATIVE_LOG");
        s_log = e ? 1 : 0;
        if (e && *e == 'f')
            s_detail_frame = atol(e + 1);
        if (e && *e == 't')                      /* t<seconds>: the first frame that long after this process started */
            s_detail_time = atof(e + 1);
    }
    return s_log;
}

/* ── targets ─────────────────────────────────────────────────────────── */

static struct { GpuTexture *tex, *z; uint32_t pw, ph; } s_fb;   /* the frame, at the output scale */

static int make_targets(uint32_t pw, uint32_t ph, GpuTexture **tex, GpuTexture **z)
{
    GpuTextureDesc td;
    memset(&td, 0, sizeof td);
    td.width = pw; td.height = ph; td.mips = 1;
    td.format = GPU_FMT_BGRA8;
    td.flags = GPU_TEX_TARGET | GPU_TEX_SAMPLED;
    if (!(*tex = gpu_texture_create(&td, NULL)))
        return 0;
    td.format = GPU_FMT_D24S8;
    td.flags = GPU_TEX_DEPTH;
    *z = gpu_texture_create(&td, NULL);
    return 1;
}

static int fb_ready(void)
{
    float sx, sy;
    uint32_t pw, ph;
    if (!s_up)
        return 0;
    nv2a_gpu_native_scale(&sx, &sy);
    pw = (uint32_t)(GUEST_W * sx + 0.5f);
    ph = (uint32_t)(GUEST_H * sy + 0.5f);
    if (s_fb.tex && s_fb.pw == pw && s_fb.ph == ph)
        return 1;
    if (s_fb.tex) gpu_texture_release(s_fb.tex);
    if (s_fb.z) gpu_texture_release(s_fb.z);
    memset(&s_fb, 0, sizeof s_fb);
    if (!make_targets(pw, ph, &s_fb.tex, &s_fb.z))
        return 0;
    s_fb.pw = pw;
    s_fb.ph = ph;
    {
        float black[4] = { 0, 0, 0, 1 };
        gpu_clear_target(s_fb.tex, black);
    }
    btvs_log("gfx", "frame %ux%u", pw, ph);
    return 1;
}

/* The frame as it is shown: the game's frame, or -- when the game has set a
 * gamma ramp that changes something -- that frame through the ramp, in a
 * target of its own (the game's frame is left as drawn: it is not always
 * drawn again before the next Swap). Port text, frame dumps and the window
 * all take the shown frame. */
static uint8_t s_ramp[768];
static int s_ramp_on;
static GpuTexture *s_shown;
static uint32_t s_shown_w, s_shown_h;
static int s_shown_ok;

int nv2a_gpu_gamma_ramp(GpuTexture *target, GpuTexture *src_tex, const uint8_t *ramp);

static void gamma_frame(void)
{
    static int off = -1;
    s_shown_ok = 0;
    if (off < 0)
        off = getenv("BTVS_NO_GAMMA") != NULL;       /* (testing: the game's ramp ignored, as before) */
    if (!s_ramp_on || off || !s_fb.tex)
        return;
    if (!s_shown || s_shown_w != s_fb.pw || s_shown_h != s_fb.ph) {
        GpuTexture *z = NULL;
        if (s_shown)
            gpu_texture_release(s_shown);
        s_shown = NULL;
        if (!make_targets(s_fb.pw, s_fb.ph, &s_shown, &z))
            return;
        if (z)
            gpu_texture_release(z);
        s_shown_w = s_fb.pw; s_shown_h = s_fb.ph;
    }
    s_shown_ok = nv2a_gpu_gamma_ramp(s_shown, s_fb.tex, s_ramp);
}

static GpuTexture *shown(void)
{
    return s_shown_ok && s_shown ? s_shown : s_fb.tex;
}

void btvs_native_frame_size(uint32_t *w, uint32_t *h)
{
    *w = s_fb.pw;
    *h = s_fb.ph;
}

GpuTexture *btvs_native_frame_texture(void)
{
    return shown();
}

/* Render-to-texture: a GPU target per guest surface the game renders into
 * (keyed by the surface's memory), sampled instead of that memory when a
 * texture over the same memory is bound. */
typedef struct {
    uint32_t data, w, h, pw, ph;
    GpuTexture *tex, *z;
    DWORD last_use;
} NativeRT;
#define NATIVE_RTS 32
static NativeRT s_rts[NATIVE_RTS];
static uint32_t s_backbuffer;                /* the surface object shown at Swap */

static void surface_size(uint32_t surf, uint32_t *w, uint32_t *h)
{
    uint32_t size = MEM32(surf + 0x10), fmt = MEM32(surf + 0xC);
    if (size) {
        *w = (size & 0xFFF) + 1;
        *h = ((size >> 12) & 0xFFF) + 1;
    } else {
        *w = 1u << ((fmt >> 20) & 0xF);
        *h = 1u << ((fmt >> 24) & 0xF);
    }
}

static NativeRT *rt_for(uint32_t surf)
{
    uint32_t data = MEM32(surf + 4), w, h;
    float sx, sy;
    int i, lru = 0;
    NativeRT *r;
    if (!data)
        return NULL;
    surface_size(surf, &w, &h);
    for (i = 0; i < NATIVE_RTS; i++) {
        if (s_rts[i].tex && s_rts[i].data == data && s_rts[i].w == w && s_rts[i].h == h) {
            s_rts[i].last_use = GetTickCount();
            return &s_rts[i];
        }
        if (!s_rts[i].tex) { lru = i; break; }
        if (s_rts[i].last_use < s_rts[lru].last_use) lru = i;
    }
    r = &s_rts[lru];
    if (r->tex) gpu_texture_release(r->tex);
    if (r->z) gpu_texture_release(r->z);
    memset(r, 0, sizeof *r);
    nv2a_gpu_native_scale(&sx, &sy);
    r->pw = (uint32_t)(w * sx + 0.5f);
    r->ph = (uint32_t)(h * sy + 0.5f);
    if (!make_targets(r->pw, r->ph, &r->tex, &r->z))
        return NULL;
    r->data = data; r->w = w; r->h = h;
    r->last_use = GetTickCount();
    {
        float black[4] = { 0, 0, 0, 0 };
        gpu_clear_target(r->tex, black);
        if (r->z)
            gpu_clear_depth(r->z, GPU_CLEAR_DEPTH | GPU_CLEAR_STENCIL, 1.0f, 0);
    }
    btvs_log("gfx", "render target %08X %ux%u (drawn at %ux%u)", data, w, h, r->pw, r->ph);
    return r;
}

static GpuTexture *rt_texture(uint32_t data)
{
    int i;
    for (i = 0; i < NATIVE_RTS; i++)
        if (s_rts[i].tex && s_rts[i].data == data)
            return s_rts[i].tex;
    return NULL;
}

typedef struct { GpuTexture *rt, *ds; uint32_t pw, ph, sw, sh; } NativeTarget;

static int target_now(NativeTarget *t)
{
    uint32_t dev = MEM32(D3D_PDEVICE), rt = dev ? MEM32(dev + DEV_RT) : 0;
    if (!fb_ready())
        return 0;
    if (!s_backbuffer)
        s_backbuffer = rt;                       /* the first target a device has is its back buffer */
    if (rt && rt != s_backbuffer) {
        NativeRT *r = rt_for(rt);
        if (!r)
            return 0;
        t->rt = r->tex; t->ds = r->z; t->pw = r->pw; t->ph = r->ph; t->sw = r->w; t->sh = r->h;
        return 1;
    }
    t->rt = s_fb.tex; t->ds = s_fb.z; t->pw = s_fb.pw; t->ph = s_fb.ph; t->sw = GUEST_W; t->sh = GUEST_H;
    return 1;
}

/* ── the draw ────────────────────────────────────────────────────────── */

/* The vertex shader's attributes, as the library would program them
 * (CDevice::SetStateVB / SetStateUP). */
static void native_attrs(NativeDraw *d, uint32_t dev, uint32_t up_data, uint32_t up_stride)
{
    uint32_t vs = MEM32(dev + DEV_VSHADER), flags = vs ? MEM32(vs + 4) : 0, base = MEM32(dev + DEV_INDEX_BASE);
    int a;
    memset(d->attr, 0, sizeof d->attr);
    if (!vs)
        return;
    for (a = 0; a < 16; a++) {
        uint32_t slot = MEM8(D3D_ATTR_REMAP + (flags & 0x10) + a), e = vs + slot * 16;
        uint32_t s = MEM32(e + 0x14), off = MEM32(e + 0x18), fmt = MEM32(e + 0x1C);
        if ((fmt & 0xFF) == 2 || !(fmt & 0xF0))
            continue;
        if (up_data) {
            d->attr[a].va = up_data + off;
            d->attr[a].stride = up_stride;
        } else {
            uint32_t st = D3D_STREAMS + s * 12, vb = s < 16 ? MEM32(st + 8) : 0;
            if (!vb)
                continue;
            d->attr[a].stride = MEM32(st);
            d->attr[a].va = nv2a_native_resolve(MEM32(vb + 4)) + MEM32(st + 4) + off + d->attr[a].stride * base;
        }
        d->attr[a].fmt = fmt & 0xFF;
    }
}

/* The viewport transform the library folds into c58 (scale) and c59
 * (offset): it only writes them into the pushbuffer, so they are asked of
 * its own D3DDevice_GetViewportOffsetAndScale(pOffset, pScale). */
void D3DDevice_GetViewportOffsetAndScale_005914C0(void);
static int s_vp_dirty = 1;

static void native_viewport_consts(void)
{
    static uint32_t buf;
    uint32_t esp0 = g_esp, eax = g_eax, ecx = g_ecx, edx = g_edx;
    if (!s_vp_dirty)
        return;
    if (!buf && !(buf = xbox_HeapAlloc(32, 16)))
        return;
    s_vp_dirty = 0;
    g_esp -= 4; MEM32(g_esp) = buf + 16;               /* pScale */
    g_esp -= 4; MEM32(g_esp) = buf;                    /* pOffset */
    g_esp -= 4; MEM32(g_esp) = 0;                      /* return address */
    D3DDevice_GetViewportOffsetAndScale_005914C0();
    g_esp = esp0;
    g_eax = eax; g_ecx = ecx; g_edx = edx;
    nv2a_native_const(58, (const float *)XBOX_PTR(buf + 16), 1);
    nv2a_native_const(59, (const float *)XBOX_PTR(buf), 1);
}

static void log_draw(int path, const NativeDraw *d, uint32_t dev, uint32_t up_stride)
{
    uint32_t vs = MEM32(dev + DEV_VSHADER);
    const uint32_t *r = d->rs;
    int i;
    fprintf(stderr, "[NATIVE] f%lu %s prim %u n %u vs %08X flags %X start %u ps %d", s_frame, k_path[path], d->prim,
            d->count, vs, vs ? MEM32(vs + 4) : 0, d->vp_start, d->pixel_shader);
    if (up_stride)
        fprintf(stderr, " stride %u", up_stride);
    for (i = 0; i < 4; i++)
        if (d->tex[i].res) {
            uint32_t t = d->tex[i].res, w, h;
            surface_size(t, &w, &h);
            fprintf(stderr, " t%d=%08X(fmt %02X %ux%u word %08X data %08X pal %08X)", i, t, (MEM32(t + 0xC) >> 8) & 0xFF, w, h, MEM32(t + 0xC), MEM32(t + 4), d->tex[i].palette);
        }
    fprintf(stderr, " A");
    for (i = 0; i < 16; i++)
        if (d->attr[i].fmt)
            fprintf(stderr, " %d:%X/%u", i, d->attr[i].fmt, d->attr[i].stride);
    fprintf(stderr, " | z %u zfunc %X zw %u blend %u %X %X atest %u ref %X cop0 %u aop0 %u\n", r[RS_ZENABLE], r[57], r[64],
            r[59], r[62], r[63], r[60], r[61], d->tss[12], d->tss[16]);
    if (d->tex[0].res && ((MEM32(d->tex[0].res + 0xC) >> 8) & 0xFF) == 0x0B && d->tex[0].palette) {
        /* a palettised texture: its palette (every 16th entry) and what its texels are */
        uint32_t pal = nv2a_native_resolve(MEM32(d->tex[0].palette + 4)), tx = nv2a_native_resolve(MEM32(d->tex[0].res + 4));
        uint32_t w, h, n, lo = 255, hi = 0, k;
        unsigned long long sum = 0, lum = 0;
        surface_size(d->tex[0].res, &w, &h);
        n = w * h;
        for (k = 0; k < n; k++) {
            uint32_t ix = MEM8(tx + k), c = MEM32(pal + ix * 4);
            if (ix < lo) lo = ix;
            if (ix > hi) hi = ix;
            sum += ix;
            lum += ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255);
        }
        if (getenv("BTVS_P8_DUMP")) {                /* <folder>: palette + indices of each, for a look outside */
            char fn[MAX_PATH];
            FILE *f = NULL;
            sprintf_s(fn, sizeof fn, "%s/p8_%08X_%ux%u.bin", getenv("BTVS_P8_DUMP"), MEM32(d->tex[0].res + 4), w, h);
            if (!fopen_s(&f, fn, "wb") && f) {
                fwrite(XBOX_PTR(pal), 1, 1024, f);
                fwrite(XBOX_PTR(tx), 1, n, f);
                fclose(f);
            }
        }
        fprintf(stderr, "[NATIVE]    palette %08X (word %08X):", pal, MEM32(d->tex[0].palette + 0xC));
        for (k = 0; k < 256; k += 16)
            fprintf(stderr, " %08X", MEM32(pal + k * 4));
        fprintf(stderr, " | index %u..%u mean %.1f, mean colour %.1f of 255\n", lo, hi, (double)sum / n, (double)lum / (3.0 * n));
    }
    for (i = 0; i < 4; i++) {                    /* the texture stages, raw (D3DTSS 9..28), to the first disabled one */
        const uint32_t *t = d->tss + i * 32;
        int k;
        fprintf(stderr, "[NATIVE]    stage %d:", i);
        for (k = 9; k <= 28; k++)
            if (k >= 22 && k <= 27) fprintf(stderr, " %.2f", *(const float *)&t[k]);
            else fprintf(stderr, " %X", t[k]);
        fprintf(stderr, "\n");
        if (t[12] <= 1)
            break;
    }
    fprintf(stderr, "[NATIVE]    stencil %u func %X ref %X mask %X write %X fail %X zfail %X pass %X, cull %X front %X, colour write %X\n",
            r[RS_FIRST_COMPLEX + 8], r[70], r[71], r[72], r[73], r[RS_FIRST_COMPLEX + 9], r[68], r[69],
            r[RS_FIRST_COMPLEX + 11], r[RS_FIRST_COMPLEX + 10], r[67]);
    fprintf(stderr, "[NATIVE]    texture modes %X, factor %08X, specular %X\n", r[RS_FIRST_COMPLEX], r[RS_FIRST_COMPLEX + 12], MEM32(0x59FAF4));
    {   /* the first vertices, attribute by attribute */
        int v;
        for (v = 0; v < 3 && (uint32_t)v < d->count; v++) {
            uint32_t idx = d->index_va ? MEM16(d->index_va + v * 2) : (uint32_t)v;
            fprintf(stderr, "[NATIVE]    v%u:", idx);
            for (i = 0; i < 16; i++)
                if (d->attr[i].fmt) {
                    uint32_t p = d->attr[i].va + idx * d->attr[i].stride;
                    int n = (d->attr[i].fmt >> 4) & 7, k;
                    fprintf(stderr, " a%d(", i);
                    if ((d->attr[i].fmt & 0xF) == 2)
                        for (k = 0; k < n; k++) fprintf(stderr, "%s%.3f", k ? " " : "", MEMF(p + k * 4));
                    else
                        fprintf(stderr, "%08X", MEM32(p));
                    fprintf(stderr, ")");
                }
            fprintf(stderr, "\n");
        }
    }
}

/* ── fixed-function transform ────────────────────────────────────────
 * The level's own geometry (position + two texture coordinates: texture and
 * light map) is drawn with no vertex program: Direct3D's fixed pipeline with
 * the view and projection the game gave SetTransform. The backend has a
 * vertex shader for that (NativeDraw.passthrough == 2) which takes its matrix
 * from constant slots 188..191; here the matrix is made -- world * view *
 * projection, then the viewport as a part of the target -- and put there for
 * the one draw (the game's own programs use those slots too: what was in
 * them is put back). Lighting is not done (the levels are lit by their light
 * maps); texture transforms are not applied.
 *
 * (A first version transformed the vertices here and handed the backend
 * screen-space positions: 300,000 vertices a frame, each array hashed and
 * uploaded again for every draw -- 25 frames a second in the first level.) */
static float s_tm[8][16];
static int s_tm_set[8];
static float s_ff_saved[4][4];

static void mat_mul(float *o, const float *a, const float *b)
{
    int r, c;
    float t[16];
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            t[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c] + a[r * 4 + 3] * b[12 + c];
    memcpy(o, t, sizeof t);
}

static int ff_begin(NativeDraw *d, uint32_t dev, uint32_t sw, uint32_t sh)
{
    static const float ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    float m[16], v[16], col[4][4];
    float vx = (float)MEM32(dev + DEV_VIEWPORT), vy = (float)MEM32(dev + DEV_VIEWPORT + 4);
    float vw = (float)MEM32(dev + DEV_VIEWPORT + 8), vh = (float)MEM32(dev + DEV_VIEWPORT + 12);
    float z0 = MEMF(dev + DEV_VIEWPORT + 16), z1 = MEMF(dev + DEV_VIEWPORT + 20);
    int k;
    if ((d->attr[0].fmt & 0xFF) != 0x32 || !d->attr[0].va || !s_tm_set[0] || !s_tm_set[1] || !sw || !sh)
        return 0;
    mat_mul(m, s_tm_set[6] ? s_tm[6] : ident, s_tm[0]);
    mat_mul(m, m, s_tm[1]);
    /* the viewport inside the target, as one more matrix (row vectors):
     * x' = x * vw/sw + w * ((2 vx + vw) / sw - 1),  y likewise (y up),
     * z' = z * (z1 - z0) + w * z0 */
    memcpy(v, ident, sizeof v);
    v[0] = vw / (float)sw;  v[12] = (2.0f * vx + vw) / (float)sw - 1.0f;
    v[5] = vh / (float)sh;  v[13] = 1.0f - (2.0f * vy + vh) / (float)sh;
    v[10] = z1 - z0;        v[14] = z0;
    {   /* (w' = w: the last column of v stays 0 0 0 1, the w terms above ride on row 3 of a matrix applied to (x y z w)) */
        float t[16];
        int r;
        for (r = 0; r < 4; r++) {
            float x = m[r * 4], y = m[r * 4 + 1], z = m[r * 4 + 2], w = m[r * 4 + 3];
            t[r * 4 + 0] = x * v[0] + w * v[12];
            t[r * 4 + 1] = y * v[5] + w * v[13];
            t[r * 4 + 2] = z * v[10] + w * v[14];
            t[r * 4 + 3] = w;
        }
        memcpy(m, t, sizeof m);
    }
    for (k = 0; k < 4; k++) {                    /* column k: what the shader dots the position with */
        col[k][0] = m[k]; col[k][1] = m[4 + k]; col[k][2] = m[8 + k]; col[k][3] = m[12 + k];
        nv2a_native_const_get(188 + (uint32_t)k, s_ff_saved[k]);
    }
    nv2a_native_const(188, &col[0][0], 4);
    d->passthrough = 2;
    return 1;
}

static void ff_end(void)
{
    nv2a_native_const(188, &s_ff_saved[0][0], 4);
}

static int ff_off(void);
static int wide_full_width(const NativeDraw *d, int path, uint32_t sw);
static void wide_frame(void);

static void native_draw(int path, uint32_t prim, uint32_t count, uint32_t index_va, uint32_t up_data, uint32_t up_stride)
{
    NativeDraw d;
    NativeTarget tg;
    uint32_t dev = MEM32(D3D_PDEVICE), vs, flags;
    int st, ff = 0, full;
    s_count[path]++;
    s_verts += count;
    if (!s_up || !dev)
        return;
    if (!target_now(&tg)) {
        s_no_target++;
        return;
    }
    vs = MEM32(dev + DEV_VSHADER);
    flags = vs ? MEM32(vs + 4) : 0;
    memset(&d, 0, sizeof d);
    d.prim = prim;
    d.count = count;
    d.index_va = index_va;
    d.passthrough = vs && (flags & 2);
    d.vp_start = MEM32(dev + DEV_VP_START);
    d.clip_min = MEMF(dev + DEV_CLIP_MIN);
    d.clip_max = MEMF(dev + DEV_CLIP_MAX);
    native_attrs(&d, dev, up_data, up_stride);
    d.scratch = up_data != 0;
    for (st = 0; st < 4; st++) {
        d.tex[st].res = MEM32(dev + DEV_TEXTURES + st * 4);
        d.tex[st].palette = MEM32(dev + DEV_PALETTES + st * 4);
        if (d.tex[st].res)
            d.tex[st].srv = rt_texture(MEM32(d.tex[st].res + 4));
    }
    d.pixel_shader = MEM32(dev + DEV_PSHADER) != 0;
    d.rs = (const uint32_t *)XBOX_PTR(D3D_RENDER_STATE);
    d.tss = (const uint32_t *)XBOX_PTR(D3D_TEXTURE_STATE);
    if (log_on() && (long)s_frame == s_detail_frame)
        log_draw(path, &d, dev, up_stride);
    if (vs && !(flags & 0x12)) {
        /* fixed-function transform: no program, not screen space */
        if (ff_off() || !ff_begin(&d, dev, tg.sw, tg.sh)) {
            s_ff_skipped++;
            return;
        }
        ff = 1;
    }
    {   /* (testing) BTVS_WORLD_PASS=1: the level without its light-map pass; 2: the light maps alone */
        static int pass = -1;
        static uint32_t rs2[256];
        if (pass < 0)
            pass = getenv("BTVS_WORLD_PASS") ? atoi(getenv("BTVS_WORLD_PASS")) : 0;
        if (pass && ff && d.rs[62] == 0x306) {
            if (pass == 1) { ff_end(); return; }
            memcpy(rs2, d.rs, sizeof rs2);
            rs2[59] = 0;                             /* no blending: the light map itself */
            d.rs = rs2;
        }
    }
    native_viewport_consts();
    full = wide_full_width(&d, path, tg.sw);
    nv2a_native_draw(&d, tg.rt, tg.ds, tg.pw, tg.ph, tg.sw, tg.sh);
    if (full)
        wide_frame();
    if (ff)
        ff_end();
}

static int ff_off(void)
{
    static int off = -1;
    if (off < 0)
        off = getenv("BTVS_NO_FIXED_FUNCTION") != NULL;
    return off;
}

static int d3d_draws(void)
{
    static int on = -1;
    if (on < 0)
        on = getenv("BTVS_D3D_DRAWS") != NULL;
    return on;
}

/* ── the hooked calls (stdcall unless said) ─────────────────────────── */

void D3DDevice_DrawIndexedVertices_00593FC0_orig(void);
void D3DDevice_DrawVerticesUP_00593D20_orig(void);
void D3DDevice_DrawIndexedVerticesUP_00593E80_orig(void);
void D3DDevice_Clear_00595E70_orig(void);
void D3DDevice_SetRenderTarget_00592AF0_orig(void);
void D3DDevice_LoadVertexShader_005948F0_orig(void);
void D3DDevice_SelectVertexShader_00594950_orig(void);
void D3DDevice_SetVertexShader_00594C20_orig(void);
void D3DDevice_SetVertexShaderConstant_00594D80_orig(void);
void D3DDevice_SetShaderConstantMode_005949D0_orig(void);
void D3DDevice_SetViewport_005933E0_orig(void);
void D3D_CommonSetViewport_00591FE0_orig(void);
void D3DDevice_SetTransform_005927E0_orig(void);
void D3DDevice_CopyRects_00593110_orig(void);
void D3DDevice_PersistDisplay_00593A80_orig(void);
void D3DDevice_SetGammaRamp_005926E0_orig(void);
void Direct3D_CreateDevice_005966E0_orig(void);

/* void DrawIndexedVertices(D3DPRIMITIVETYPE, UINT VertexCount, CONST PVOID pIndexData) */
void D3DDevice_DrawIndexedVertices_00593FC0(void)
{
    native_draw(PATH_INDEXED, MEM32(g_esp + 4), MEM32(g_esp + 8), MEM32(g_esp + 12), 0, 0);
    if (!d3d_draws()) {
        g_esp += 16;
        return;
    }
    D3DDevice_DrawIndexedVertices_00593FC0_orig();
}

/* void DrawVerticesUP(D3DPRIMITIVETYPE, UINT VertexCount, CONST void *pData, UINT Stride) */
void D3DDevice_DrawVerticesUP_00593D20(void)
{
    native_draw(PATH_UP, MEM32(g_esp + 4), MEM32(g_esp + 8), 0, MEM32(g_esp + 12), MEM32(g_esp + 16));
    if (!d3d_draws()) {
        g_esp += 20;
        return;
    }
    D3DDevice_DrawVerticesUP_00593D20_orig();
}

/* void DrawIndexedVerticesUP(D3DPRIMITIVETYPE, UINT VertexCount, CONST PVOID pIndexData,
 *                            CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride) */
void D3DDevice_DrawIndexedVerticesUP_00593E80(void)
{
    native_draw(PATH_INDEXED_UP, MEM32(g_esp + 4), MEM32(g_esp + 8), MEM32(g_esp + 12), MEM32(g_esp + 16),
                MEM32(g_esp + 20));
    if (!d3d_draws()) {
        g_esp += 24;
        return;
    }
    D3DDevice_DrawIndexedVerticesUP_00593E80_orig();
}

/* HRESULT Clear(Count, pRects, Flags, Color, Z, Stencil). Flags: 0xF0 the
 * colour channels, 1 depth, 2 stencil. */
void D3DDevice_Clear_00595E70(void)
{
    uint32_t nrects = MEM32(g_esp + 4), flags = MEM32(g_esp + 12), color = MEM32(g_esp + 16);
    float z = MEMF(g_esp + 20);
    uint8_t stencil = (uint8_t)MEM32(g_esp + 24);
    NativeTarget tg;
    s_count[PATH_CLEAR]++;
    if (log_on() && (long)s_frame == s_detail_frame)
        fprintf(stderr, "[NATIVE] f%lu clear flags %X colour %08X z %g rects %u\n", s_frame, flags, color, z, nrects);
    if (s_up && target_now(&tg)) {
        float c[4] = { ((color >> 16) & 255) / 255.0f, ((color >> 8) & 255) / 255.0f,
                       (color & 255) / 255.0f, (color >> 24) / 255.0f };
        if (flags & 0xF0)
            gpu_clear_target(tg.rt, c);
        if (flags & 0xF3)
            nv2a_native_interp_clear(tg.rt, flags, c, z, stencil);
        if ((flags & 3) && tg.ds)
            gpu_clear_depth(tg.ds, ((flags & 1) ? GPU_CLEAR_DEPTH : 0) | ((flags & 2) ? GPU_CLEAR_STENCIL : 0), z, stencil);
    }
    D3DDevice_Clear_00595E70_orig();
}

/* HRESULT SetRenderTarget(pRenderTarget, pNewZStencil) */
void D3DDevice_SetRenderTarget_00592AF0(void)
{
    uint32_t sf = MEM32(g_esp + 4), z = MEM32(g_esp + 8);
    s_count[PATH_RT]++;
    s_vp_dirty = 1;
    if (log_on() && sf) {
        static int shown;
        if (shown++ < 24)
            fprintf(stderr, "[NATIVE] SetRenderTarget %08X (data %08X fmt %08X size %08X) z %08X backbuffer %08X\n", sf,
                    MEM32(sf + 4), MEM32(sf + 0xC), MEM32(sf + 0x10), z, s_backbuffer);
    }
    D3DDevice_SetRenderTarget_00592AF0_orig();
}

/* void LoadVertexShader(DWORD Handle, DWORD Address): the program block at
 * (Handle - 1) + 0x114, [+0xC] DWORDs, into program memory at Address. */
void D3DDevice_LoadVertexShader_005948F0(void)
{
    uint32_t vs = MEM32(g_esp + 4) - 1, addr = MEM32(g_esp + 8);
    if (s_up && vs)
        nv2a_native_vp_load(addr, (const uint32_t *)XBOX_PTR(vs + 0x114), MEM32(vs + 0xC));
    D3DDevice_LoadVertexShader_005948F0_orig();
}

void D3DDevice_SelectVertexShader_00594950(void)
{
    s_vp_dirty = 1;
    D3DDevice_SelectVertexShader_00594950_orig();
}

void D3DDevice_SetVertexShader_00594C20(void)
{
    s_vp_dirty = 1;
    D3DDevice_SetVertexShader_00594C20_orig();
}

/* void SetVertexShaderConstant(INT Register, CONST void *pData, DWORD Count):
 * Register -96..95 is constant slot Register + 96. */
void D3DDevice_SetVertexShaderConstant_00594D80(void)
{
    uint32_t slot = MEM32(g_esp + 4) + 96, data = MEM32(g_esp + 8), n = MEM32(g_esp + 12);
    if (s_up && slot < 192 && data)
        nv2a_native_const(slot, (const float *)XBOX_PTR(data), n);
    D3DDevice_SetVertexShaderConstant_00594D80_orig();
}

/* void SetShaderConstantMode(Mode): in the 96-constant mode the library
 * loads its own constants into c60..c62 (pushbuffer only). */
void D3DDevice_SetShaderConstantMode_005949D0(void)
{
    uint32_t mode = MEM32(g_esp + 4);
    D3DDevice_SetShaderConstantMode_005949D0_orig();
    s_vp_dirty = 1;
    if (s_up && !(mode & ~0x10u))
        nv2a_native_const(60, (const float *)XBOX_PTR(D3D_CONST_POOL), 3);
}

void D3DDevice_SetViewport_005933E0(void)
{
    s_vp_dirty = 1;
    D3DDevice_SetViewport_005933E0_orig();
}

/* (ecx = device, edx = pushbuffer position; returns the position after its
 * writes.) Called by everything that changes c58/c59. */
void D3D_CommonSetViewport_00591FE0(void)
{
    s_vp_dirty = 1;
    D3D_CommonSetViewport_00591FE0_orig();
}

/* void SetTransform(D3DTRANSFORMSTATETYPE, CONST D3DMATRIX *): kept for the
 * fixed-function draws (ff_transform). Xbox numbering: 0 view, 1 projection,
 * 2..5 texture 0..3, 6 world. */
void D3DDevice_SetTransform_005927E0(void)
{
    uint32_t state = MEM32(g_esp + 4), pm = MEM32(g_esp + 8);
    if (state < 8 && pm) {
        memcpy(s_tm[state], (const void *)XBOX_PTR(pm), 64);
        s_tm_set[state] = 1;
    }
    D3DDevice_SetTransform_005927E0_orig();
}

void D3DDevice_CopyRects_00593110(void)
{
    static int shown;
    if (shown++ < 8)
        btvs_log("gfx", "CopyRects src %08X dst %08X (not drawn yet)", MEM32(g_esp + 4), MEM32(g_esp + 16));
    D3DDevice_CopyRects_00593110_orig();
}

void D3DDevice_PersistDisplay_00593A80(void)
{
    /* keeps the last frame on screen across a reboot into another title:
     * nothing to do on a PC */
    g_eax = 0;
    g_esp += 4;
}

/* void SetGammaRamp(DWORD Flags, CONST D3DGAMMARAMP *): 256 red, 256 green,
 * 256 blue bytes. The game's own brightness setting (Options -> Display; its
 * console variable "gamma", 1.0 = no change) arrives here, from
 * TIF_Render_Implement::State_SetGamma: level i becomes 255 * (i/255)^gamma.
 * The ramp is kept and applied to each finished frame (gamma_frame). */
void D3DDevice_SetGammaRamp_005926E0(void)
{
    uint32_t ramp = MEM32(g_esp + 8);
    if (ramp) {
        const uint8_t *r = (const uint8_t *)XBOX_PTR(ramp);
        int i, same = 1;
        memcpy(s_ramp, r, sizeof s_ramp);
        for (i = 0; i < 256 && same; i++)
            same = r[i] == i && r[256 + i] == i && r[512 + i] == i;
        s_ramp_on = !same;
        btvs_log("gfx", "gamma ramp%s: red %u %u %u %u %u (of 0 64 128 192 255), green[128] %u, blue[128] %u",
                 same ? " (no change)" : "", r[0], r[64], r[128], r[192], r[255], r[256 + 128], r[512 + 128]);
    }
    D3DDevice_SetGammaRamp_005926E0_orig();
}

/* HRESULT Direct3D_CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags,
 *                               D3DPRESENT_PARAMETERS *, D3DDevice **) */
void Direct3D_CreateDevice_005966E0(void)
{
    uint32_t pp = MEM32(g_esp + 20);
    if (pp)
        btvs_log("gfx", "CreateDevice: back buffer %ux%u format %X count %u, multisample %X, depth %X, flags %X, interval %X",
                 MEM32(pp), MEM32(pp + 4), MEM32(pp + 8), MEM32(pp + 12), MEM32(pp + 16), MEM32(pp + 36),
                 MEM32(pp + 40), MEM32(pp + 48));
    { extern void btvs_gpu_registers_idle(void); btvs_gpu_registers_idle();
      Direct3D_CreateDevice_005966E0_orig();
      btvs_gpu_registers_idle(); }
    btvs_log("gfx", "CreateDevice -> %08X, device %08X", g_eax, MEM32(D3D_PDEVICE));
    { extern volatile LONG g_btvs_watch_armed; static int made; if (++made >= 2) g_btvs_watch_armed = 1; }   /* (BTVS_WATCH_ARM, btvs_boot.c) */
    s_backbuffer = 0;
    s_vp_dirty = 1;
}

/* ── start and present ─────────────────────────────────────────────── */

void btvs_native_start(void)
{
    if (s_up)
        return;
    MEM32(D3D_NULL_HARDWARE) = 1;            /* the D3D library's own no-GPU mode (btvs_native_gpuwait.c) */
    { extern void btvs_boot_watch_start(void); btvs_boot_watch_start(); }
    nv2a_native_rs_layout(RS_FIRST_DEFERRED, RS_FIRST_COMPLEX);
    if (!(s_up = nv2a_gpu_native_begin())) {
        btvs_log("gfx", "no Direct3D 11 device");
        if (!btvs_window_hidden())
            MessageBoxA(NULL, "No graphics device came up.\n\nThe game needs a GPU with Direct3D 11. "
                        "Updating the graphics driver usually fixes this.", "Buffy the Vampire Slayer", MB_ICONERROR);
        ExitProcess(1);
    }
    btvs_log("gfx", "native renderer on %s", gpu_adapter_name());
    nv2a_native_prewarm();
}

/* ── widescreen ──────────────────────────────────────────────────────
 * [display] widescreen = 1 and a frame wider than 4:3. In a level the 3D
 * view is really wider: the game is given a wider horizontal field of view
 * (btvs_native_game.c: View_SetFOV), which with its own 4:3 arithmetic also
 * makes it taller, and the extra height is cut off again here (y scaled up
 * by the same factor). HUD, menus and movies are drawn for a 4:3 screen and
 * stay 4:3 in the middle. In the menus (no level loaded) everything stays
 * 4:3, bars at the sides. */
static float s_wide = 1.0f;            /* the frame's aspect over 4:3; 1: not wide */
static int s_fov_wide;                 /* the game's last SetFOV was widened */

void btvs_native_set_wide(float aspect_over_43)
{
    s_wide = aspect_over_43 > 1.001f ? aspect_over_43 : 1.0f;
}

/* A level is loaded (the map manager has a map name; "" in the menus). */
static int level_loaded(void)
{
    uint32_t game = MEM32(0x7D0090u), mgr, str;
    if (game < 0x10000u || game >= 0x04000000u) return 0;
    mgr = MEM32(game + 0x40);
    if (mgr < 0x10000u || mgr >= 0x04000000u || !MEM8(mgr + 0x3E)) return 0;
    str = MEM32(mgr + 0x40);
    if (str < 0x10000u || str >= 0x04000000u) return 0;
    return MEM8(str) >= 0x20 && MEM8(str) < 0x7F;
}

/* For View_SetFOV: the factor to widen the horizontal field of view by. */
float btvs_native_fov_factor(void)
{
    s_fov_wide = s_wide > 1.0f && level_loaded();
    return s_fov_wide ? s_wide : 1.0f;
}

/* Widescreen, in a level: screen-space drawing that covers the whole width
 * of the game's screen -- the cutscene bars, fades, flashes, the dim behind
 * the pause menu -- is for the whole frame, not for the 4:3 middle: it is
 * drawn at full width (1: the caller puts the frame's setting back after the
 * draw). Not movie frames (Draw...VerticesUP, btvs BlitImage), which are
 * pictures and keep their shape. */
static int wide_full_width(const NativeDraw *d, int path, uint32_t sw)
{
    void nv2a_native_view(float y3d, float x2d, float x3d);
    uint32_t i, n = d->count, fmt = d->attr[0].fmt & 0xFF;
    float lo = 1e9f, hi = -1e9f;
    if (s_wide <= 1.0f || !s_fov_wide || d->passthrough != 1 || path == PATH_UP || !sw
            || !d->attr[0].va || !d->attr[0].stride || (fmt != 0x42 && fmt != 0x32) || n < 3 || n > 64)
        return 0;
    for (i = 0; i < n; i++) {
        uint32_t ix = d->index_va ? MEM16(d->index_va + i * 2) : i;
        float x = MEMF(d->attr[0].va + ix * d->attr[0].stride);
        if (x < lo) lo = x;
        if (x > hi) hi = x;
    }
    if (lo > 1.0f || hi < (float)sw - 1.0f)
        return 0;
    nv2a_native_view(s_wide, 1.0f, 1.0f);
    return 1;
}

static void wide_frame(void)
{
    void nv2a_native_view(float y3d, float x2d, float x3d);
    if (s_wide <= 1.0f)
        nv2a_native_view(1.0f, 1.0f, 1.0f);
    else if (s_fov_wide && level_loaded())
        nv2a_native_view(s_wide, 1.0f / s_wide, 1.0f);
    else
        nv2a_native_view(1.0f, 1.0f / s_wide, 1.0f / s_wide);
}

void btvs_native_overlay(void)
{
    if (s_wide > 1.0f && !(s_fov_wide && level_loaded())) {
        /* everything is 4:3 in the middle (the menus): black at the sides,
         * where 3D drawn past the edges of the game's screen would show */
        float extra = (640.0f * s_wide - 640.0f) * 0.5f + 2.0f;
        btvs_overlay_rect(-extra, 0.0f, extra, 480.0f, 0xFF000000u);
        btvs_overlay_rect(640.0f, 0.0f, extra, 480.0f, 0xFF000000u);
    }
    wide_frame();                                /* (for the frame that starts after this one is shown) */
    void btvs_overlay_test(void);
    void btvs_overlay_frame(GpuTexture *frame, uint32_t pw, uint32_t ph);
    if (!s_up || !s_fb.tex)
        return;
    {   /* (testing) BTVS_GAME_GAMMA=<0.5..2>: the game's own brightness setting, as its
         * Options -> Display slider would leave it (TGameOptions + 0x118; below 1 is brighter) */
        static float g = -1.0f;
        uint32_t game = MEM32(0x7D0090), opt = game ? MEM32(game + 0x20) : 0;
        if (g < 0.0f)
            g = getenv("BTVS_GAME_GAMMA") ? (float)atof(getenv("BTVS_GAME_GAMMA")) : 0.0f;
        if (g > 0.0f && opt)
            MEMF(opt + 0x118) = g;
    }
    gamma_frame();
    btvs_overlay_test();
    btvs_overlay_frame(shown(), s_fb.pw, s_fb.ph);
}

void btvs_native_present(void)
{
    if (!s_up || !fb_ready())
        return;
    s_vp_dirty = 1;
    nv2a_gpu_native_present(shown(), GUEST_W, GUEST_H, s_fb.pw, s_fb.ph);
    nv2a_native_interp_frame_end();
    nv2a_native_interp_setup(0, s_fb.tex);
    if (log_on()) {
        static DWORD last;
        if (GetTickCount() - last >= 2000) {
            unsigned long long st[4];
            int i;
            last = GetTickCount();
            nv2a_native_stats(st);
            fprintf(stderr, "[NATIVE] frame %lu:", s_frame);
            for (i = 0; i < PATH_COUNT; i++)
                fprintf(stderr, " %s %u", k_path[i], s_count[i]);
            fprintf(stderr, ", %u vertices; fixed-function skipped %u, no target %u; in all: %llu drawn, "
                    "%llu no vertex shader, %llu too big, %llu no pixel shader\n", s_verts, s_ff_skipped, s_no_target,
                    st[0], st[1], st[2], st[3]);
        }
    }
    memset(s_count, 0, sizeof s_count);
    s_verts = s_ff_skipped = s_no_target = 0;
    s_frame++;
    if (s_detail_time > 0.0 && btvs_frame_seconds() >= s_detail_time) {
        s_detail_frame = (long)s_frame;
        s_detail_time = 0.0;
    }
}
