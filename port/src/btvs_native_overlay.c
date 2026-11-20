/**
 * Overlay: text and rectangles drawn by the port over the game's picture
 * (btvs.h: btvs_overlay_rect, btvs_overlay_text, btvs_overlay_text_width).
 * For Menu's and Logs' own pages and messages.
 *
 * Immediate mode: what is to be on screen is asked for again every frame
 * (from any thread, at any time before the frame is shown); a frame nobody
 * drew on costs nothing. Coordinates are the game's own 640x480 screen,
 * whatever size the picture is drawn at; the overlay is part of the frame,
 * so it is in screenshots and frame dumps too.
 *
 * The items are painted with GDI into a canvas of the frame's shape (at most
 * 1080 lines: it is stretched over a larger frame), premultiplied, and laid
 * over the frame by the GPU just before it is shown. The canvas is painted
 * again only when the items differ from the last frame's.
 */
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gpu.h"
#include "btvs.h"
#include "btvs_native.h"

void nv2a_gpu_overlay_blit(GpuTexture *target, GpuTexture *tex);   /* nv2a_overlay.inc */

#define OV_ITEMS 512
#define OV_TEXT  160

typedef struct {
    int kind;                          /* 0 rectangle, 1 text */
    float x, y, w, h;                  /* 640x480 units; text: h is the letter height */
    uint32_t argb;
    char text[OV_TEXT];
} OvItem;

static CRITICAL_SECTION s_cs;
static volatile LONG s_cs_ready;
static OvItem s_items[OV_ITEMS];
static int s_count;
static uint64_t s_painted_hash;
static int s_painted_any;

static HDC s_dc;
static HBITMAP s_bmp;
static uint32_t *s_scratch;            /* GDI's surface: text in white on black */
static uint32_t *s_canvas;             /* premultiplied BGRA */
static int s_cw, s_ch;
static GpuTexture *s_tex;

static void lock(void)
{
    if (InterlockedCompareExchange(&s_cs_ready, 1, 0) == 0) {
        InitializeCriticalSection(&s_cs);
        InterlockedExchange(&s_cs_ready, 2);
    }
    while (s_cs_ready != 2)
        Sleep(0);
    EnterCriticalSection(&s_cs);
}

void btvs_overlay_rect(float x, float y, float w, float h, uint32_t argb)
{
    lock();
    if (s_count < OV_ITEMS) {
        OvItem *it = &s_items[s_count++];
        memset(it, 0, sizeof *it);
        it->x = x; it->y = y; it->w = w; it->h = h; it->argb = argb;
    }
    LeaveCriticalSection(&s_cs);
}

void btvs_overlay_text(float x, float y, float height, uint32_t argb, const char *text)
{
    if (!text || !*text)
        return;
    lock();
    if (s_count < OV_ITEMS) {
        OvItem *it = &s_items[s_count++];
        memset(it, 0, sizeof *it);
        it->kind = 1;
        it->x = x; it->y = y; it->h = height; it->argb = argb;
        strncpy_s(it->text, sizeof it->text, text, _TRUNCATE);
    }
    LeaveCriticalSection(&s_cs);
}

static HFONT font_for(int px)
{
    static HFONT f[8];
    static int fpx[8];
    int i;
    if (px < 6) px = 6;
    for (i = 0; i < 8 && f[i]; i++)
        if (fpx[i] == px)
            return f[i];
    if (i == 8) {                                  /* (the sizes changed: start over) */
        for (i = 0; i < 8; i++) { DeleteObject(f[i]); f[i] = NULL; }
        i = 0;
    }
    fpx[i] = px;
    f[i] = CreateFontA(-px, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                       CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    return f[i];
}

/* The width `text` takes at that letter height, in 640x480 units. */
float btvs_overlay_text_width(float height, const char *text)
{
    static HDC dc;
    SIZE sz = { 0, 0 };
    float w = 0.0f;
    int px = (int)(height * 2.25f + 0.5f);          /* measured at 1080 lines */
    if (!text || !*text)
        return 0.0f;
    lock();
    if (!dc)
        dc = CreateCompatibleDC(NULL);
    if (dc) {
        SelectObject(dc, font_for(px));
        if (GetTextExtentPoint32A(dc, text, (int)strlen(text), &sz))
            w = (float)sz.cx / 2.25f;
    }
    LeaveCriticalSection(&s_cs);
    return w;
}

static int canvas(int w, int h)
{
    BITMAPINFO bi;
    if (s_dc && s_cw == w && s_ch == h)
        return 1;
    if (s_bmp) { DeleteObject(s_bmp); s_bmp = NULL; }
    free(s_canvas); s_canvas = NULL;
    if (s_tex) { gpu_texture_release(s_tex); s_tex = NULL; }
    if (!s_dc && !(s_dc = CreateCompatibleDC(NULL)))
        return 0;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    s_bmp = CreateDIBSection(s_dc, &bi, DIB_RGB_COLORS, (void **)&s_scratch, NULL, 0);
    s_canvas = (uint32_t *)malloc((size_t)w * h * 4);
    if (!s_bmp || !s_canvas)
        return 0;
    SelectObject(s_dc, s_bmp);
    SetBkMode(s_dc, TRANSPARENT);
    SetTextColor(s_dc, RGB(255, 255, 255));
    s_cw = w; s_ch = h;
    s_painted_any = 0;
    return 1;
}

/* `cov` (0..255) of colour argb over the canvas pixel, premultiplied. */
static void over(uint32_t *px, uint32_t argb, uint32_t cov)
{
    uint32_t a = ((argb >> 24) & 255) * cov / 255, ia = 255 - a;
    uint32_t r = ((argb >> 16) & 255) * a / 255, g = ((argb >> 8) & 255) * a / 255, b = (argb & 255) * a / 255;
    uint32_t d = *px;
    uint32_t da = (d >> 24) * ia / 255 + a, dr = ((d >> 16) & 255) * ia / 255 + r;
    uint32_t dg = ((d >> 8) & 255) * ia / 255 + g, db = (d & 255) * ia / 255 + b;
    *px = (da << 24) | (dr << 16) | (dg << 8) | db;
}

static void paint(const OvItem *items, int n)
{
    float s = (float)s_ch / 480.0f;
    int i, x, y;
    memset(s_canvas, 0, (size_t)s_cw * s_ch * 4);
    for (i = 0; i < n; i++) {
        const OvItem *it = &items[i];
        if (it->kind == 0) {
            int x0 = (int)(it->x * s + 0.5f), y0 = (int)(it->y * s + 0.5f);
            int x1 = (int)((it->x + it->w) * s + 0.5f), y1 = (int)((it->y + it->h) * s + 0.5f);
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > s_cw) x1 = s_cw;
            if (y1 > s_ch) y1 = s_ch;
            for (y = y0; y < y1; y++)
                for (x = x0; x < x1; x++)
                    over(&s_canvas[y * s_cw + x], it->argb, 255);
        } else {
            RECT r;
            SIZE sz = { 0, 0 };
            int px = (int)(it->h * s + 0.5f), len = (int)strlen(it->text);
            SelectObject(s_dc, font_for(px));
            GetTextExtentPoint32A(s_dc, it->text, len, &sz);
            r.left = (LONG)(it->x * s + 0.5f);
            r.top = (LONG)(it->y * s + 0.5f);
            r.right = r.left + sz.cx + 2;
            r.bottom = r.top + sz.cy + 2;
            if (r.left < 0) r.left = 0;
            if (r.top < 0) r.top = 0;
            if (r.right > s_cw) r.right = s_cw;
            if (r.bottom > s_ch) r.bottom = s_ch;
            if (r.right <= r.left || r.bottom <= r.top)
                continue;
            for (y = r.top; y < r.bottom; y++)
                memset(&s_scratch[y * s_cw + r.left], 0, (size_t)(r.right - r.left) * 4);
            TextOutA(s_dc, (int)(it->x * s + 0.5f), (int)(it->y * s + 0.5f), it->text, len);
            GdiFlush();
            for (y = r.top; y < r.bottom; y++)
                for (x = r.left; x < r.right; x++) {
                    uint32_t p = s_scratch[y * s_cw + x], c = (p >> 8) & 255;   /* white on black: any channel is the coverage */
                    if (c)
                        over(&s_canvas[y * s_cw + x], it->argb, c);
                }
        }
    }
}

/* Once a frame, game thread, before the frame is shown and dumped: this
 * frame's items over `frame` (pw x ph pixels), and the list emptied. */
void btvs_overlay_frame(GpuTexture *frame, uint32_t pw, uint32_t ph)
{
    static OvItem items[OV_ITEMS];
    uint64_t h = 1469598103934665603ull;
    int n, i, cw, ch;
    lock();
    n = s_count;
    if (n)
        memcpy(items, s_items, (size_t)n * sizeof items[0]);
    s_count = 0;
    LeaveCriticalSection(&s_cs);
    if (!n || !frame || !pw || !ph)
        return;
    ch = ph > 1080 ? 1080 : (int)ph;
    cw = (int)((uint64_t)pw * ch / ph);
    if (!canvas(cw, ch))
        return;
    for (i = 0; i < (int)((size_t)n * sizeof items[0]); i++)
        h = (h ^ ((const uint8_t *)items)[i]) * 1099511628211ull;
    if (!s_painted_any || h != s_painted_hash || !s_tex) {
        paint(items, n);
        if (!s_tex) {
            GpuTextureDesc td;
            memset(&td, 0, sizeof td);
            td.width = (uint32_t)cw; td.height = (uint32_t)ch; td.mips = 1;
            td.format = GPU_FMT_BGRA8;
            td.flags = GPU_TEX_SAMPLED;
            if (!(s_tex = gpu_texture_create(&td, NULL)))
                return;
        }
        gpu_texture_update(s_tex, 0, 0, s_canvas, (uint32_t)cw * 4);
        s_painted_hash = h;
        s_painted_any = 1;
    }
    nv2a_gpu_overlay_blit(frame, s_tex);
}

/* BTVS_OVERLAY_TEST=1: a panel with text every frame, to see the overlay in
 * a frame dump without Menu or Logs. */
void btvs_overlay_test(void)
{
    static int on = -1;
    char line[96];
    if (on < 0)
        on = getenv("BTVS_OVERLAY_TEST") != NULL;
    if (!on)
        return;
    btvs_overlay_rect(20, 20, 300, 78, 0xC0101420u);
    btvs_overlay_rect(20, 20, 300, 2, 0xFFB090FFu);
    btvs_overlay_text(30, 28, 18, 0xFFFFFFFFu, "Overlay test");
    wsprintfA(line, "%d fps  -  text and rectangles over the frame", (int)(btvs_fps() + 0.5f));
    btvs_overlay_text(30, 54, 12, 0xFFC8C8E8u, line);
    btvs_overlay_text(30, 72, 10, 0xFF80E0A0u, "ABCDEFGHIJKLM nopqrstuvwxyz 0123456789");
}
