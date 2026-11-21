/* Button pictures: the game's 2002 Xbox button art replaced by the port's own.
 * (Controls area.) See docs/controls.md, "Button pictures".
 *
 * The game draws its button pictures from textures: one 64x64 texture per
 * menu button (lit and dim), a 128x256 page of twenty small pictures that the
 * tips and the journal use as letters, and the controller on the Controls
 * page (four textures side by side). Nothing of the game's data is changed:
 * before the first frame the port draws its own pictures, four times the
 * size, into PNG files and gives the folder to the runtime's texture pack
 * (xboxrecomp nv2a_texpack.inc), which swaps a texture for a file named after
 * a hash of the texture's contents.
 *
 * Everything is drawn here, from shapes and the PC's own font: there is no
 * picture file in the repository and nothing of anyone else's art.
 *
 * The pictures show the device in use: Xbox letters, PlayStation shapes, or
 * the keys and mouse buttons the player has bound.
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "recomp_types.h"
#include "btvs_input_priv.h"
#include "btvs_settings.h"

double btvs_frame_seconds(void);
void nv2a_gpu_set_texture_pack(const WCHAR *root, int dump, int load, int prefetch);

#define GLYPH_VERSION 13            /* a change in the drawing: files are made again */
#define SCALE 4                    /* the port's pictures are this many times the game's */

enum { SET_KEYS, SET_XBOX, SET_PS, SET_ORIGINAL };
static int s_set = SET_XBOX;

/* ── a canvas of straight-alpha RGBA floats ──────────────────────────────── */

typedef struct { int w, h; float *p; } cv;

static int cv_make(cv *c, int w, int h)
{
    c->w = w; c->h = h;
    c->p = (float *)calloc((size_t)w * h * 4, sizeof(float));
    return c->p != NULL;
}

static void px_over(cv *c, int x, int y, const float rgb[3], float a)
{
    float *d = c->p + 4 * ((size_t)y * c->w + x);
    float da = d[3], oa = a + da * (1.0f - a);
    int k;
    if (a <= 0.0f || oa <= 0.0f)
        return;
    for (k = 0; k < 3; k++)
        d[k] = (rgb[k] * a + d[k] * da * (1.0f - a)) / oa;
    d[3] = oa;
}

/* ── shapes, as signed distances (pixels; negative inside) ───────────────── */

enum { SH_CIRCLE, SH_RBOX, SH_SEG, SH_TRI, SH_CROSS, SH_ARC, SH_BODY };
typedef struct { int kind; float a[8]; } shape;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static float sd_rbox(float x, float y, float cx, float cy, float hx, float hy, float rad)
{
    float qx = fabsf(x - cx) - hx + rad, qy = fabsf(y - cy) - hy + rad;
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, in = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - rad;
}

static float sd_seg(float x, float y, float ax, float ay, float bx, float by)
{
    float px = x - ax, py = y - ay, dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy;
    float t = l > 0 ? clampf((px * dx + py * dy) / l, 0, 1) : 0;
    px -= dx * t; py -= dy * t;
    return sqrtf(px * px + py * py);
}

static float smin(float a, float b, float k)
{
    float h = k - fabsf(a - b);
    if (h < 0) h = 0;
    h /= k;
    return (a < b ? a : b) - h * h * k * 0.25f;
}

static float sdf(const shape *s, float x, float y)
{
    const float *a = s->a;
    switch (s->kind) {
    case SH_CIRCLE:
        return sqrtf((x - a[0]) * (x - a[0]) + (y - a[1]) * (y - a[1])) - a[2];
    case SH_RBOX:
        return sd_rbox(x, y, a[0], a[1], a[2], a[3], a[4]);
    case SH_SEG:
        return sd_seg(x, y, a[0], a[1], a[2], a[3]) - a[4];
    case SH_TRI: {
        float d = -1e9f, area = (a[2] - a[0]) * (a[5] - a[1]) - (a[4] - a[0]) * (a[3] - a[1]);
        float sgn = area < 0 ? -1.0f : 1.0f;
        int i;
        for (i = 0; i < 3; i++) {
            float x0 = a[i * 2], y0 = a[i * 2 + 1], x1 = a[(i * 2 + 2) % 6], y1 = a[(i * 2 + 3) % 6];
            float ex = x1 - x0, ey = y1 - y0, l = sqrtf(ex * ex + ey * ey);
            float e = l > 0 ? sgn * ((y - y0) * ex - (x - x0) * ey) / -l : 0;
            if (e > d) d = e;
        }
        return d;
    }
    case SH_CROSS: {                        /* cx, cy, half length, half width, corner radius */
        float h = sd_rbox(x, y, a[0], a[1], a[2], a[3], a[4]), v = sd_rbox(x, y, a[0], a[1], a[3], a[2], a[4]);
        return h < v ? h : v;
    }
    case SH_ARC: {                          /* cx, cy, radius, from, to (radians, clockwise on screen), half width */
        float ang = atan2f(y - a[1], x - a[0]), d;
        while (ang < a[3]) ang += 6.2831853f;
        if (ang <= a[4])
            d = fabsf(sqrtf((x - a[0]) * (x - a[0]) + (y - a[1]) * (y - a[1])) - a[2]);
        else {
            float d0 = sd_seg(x, y, a[0] + cosf(a[3]) * a[2], a[1] + sinf(a[3]) * a[2], a[0] + cosf(a[3]) * a[2], a[1] + sinf(a[3]) * a[2]);
            float d1 = sd_seg(x, y, a[0] + cosf(a[4]) * a[2], a[1] + sinf(a[4]) * a[2], a[0] + cosf(a[4]) * a[2], a[1] + sinf(a[4]) * a[2]);
            d = d0 < d1 ? d0 : d1;
        }
        return d - a[5];
    }
    case SH_BODY: {                         /* a controller seen from above: a[0] is the scale, a[1], a[2] its origin */
        float k = a[0], px = (x - a[1]) / k, py = (y - a[2]) / k, d;
        d = sd_rbox(px, py, 158, 118, 92, 50, 40);
        d = smin(d, sd_seg(px, py, 80, 122, 58, 208) - 33, 22);
        d = smin(d, sd_seg(px, py, 236, 122, 258, 208) - 33, 22);
        return d * k;
    }
    }
    return 1e9f;
}

static void shape_bounds(const shape *s, float b[4])
{
    const float *a = s->a;
    int i;
    switch (s->kind) {
    case SH_CIRCLE: b[0] = a[0] - a[2]; b[1] = a[1] - a[2]; b[2] = a[0] + a[2]; b[3] = a[1] + a[2]; break;
    case SH_RBOX:   b[0] = a[0] - a[2]; b[1] = a[1] - a[3]; b[2] = a[0] + a[2]; b[3] = a[1] + a[3]; break;
    case SH_CROSS:  b[0] = a[0] - a[2]; b[1] = a[1] - a[2]; b[2] = a[0] + a[2]; b[3] = a[1] + a[2]; break;
    case SH_ARC:    b[0] = a[0] - a[2] - a[5]; b[1] = a[1] - a[2] - a[5]; b[2] = a[0] + a[2] + a[5]; b[3] = a[1] + a[2] + a[5]; break;
    case SH_SEG:
        b[0] = (a[0] < a[2] ? a[0] : a[2]) - a[4]; b[1] = (a[1] < a[3] ? a[1] : a[3]) - a[4];
        b[2] = (a[0] > a[2] ? a[0] : a[2]) + a[4]; b[3] = (a[1] > a[3] ? a[1] : a[3]) + a[4];
        break;
    case SH_TRI:
        b[0] = b[2] = a[0]; b[1] = b[3] = a[1];
        for (i = 1; i < 3; i++) {
            if (a[i * 2] < b[0]) b[0] = a[i * 2];
            if (a[i * 2] > b[2]) b[2] = a[i * 2];
            if (a[i * 2 + 1] < b[1]) b[1] = a[i * 2 + 1];
            if (a[i * 2 + 1] > b[3]) b[3] = a[i * 2 + 1];
        }
        break;
    default:                                /* the body */
        b[0] = a[1] + 20 * a[0]; b[1] = a[2] + 60 * a[0]; b[2] = a[1] + 296 * a[0]; b[3] = a[2] + 246 * a[0];
        break;
    }
}

enum { P_FILL, P_STROKE, P_GLOW };
static float s_clip[4] = { -1e9f, -1e9f, 1e9f, 1e9f };

/* FILL: the inside. STROKE: a line `width` wide along the edge. GLOW: the
 * inside and a falloff `width` pixels wide around it. Colour runs from `top`
 * to `bot` down the shape. */
static void paint(cv *c, const shape *s, int mode, float width, const float top[3], const float bot[3], float alpha)
{
    float b[4], m = mode == P_GLOW ? width * 3.0f : mode == P_STROKE ? width * 0.5f + 1.0f : 1.0f;
    int x0, y0, x1, y1, x, y, k;
    shape_bounds(s, b);
    x0 = (int)floorf((b[0] > s_clip[0] - m ? b[0] : s_clip[0] - m) - m); if (x0 < 0) x0 = 0;
    y0 = (int)floorf((b[1] > s_clip[1] - m ? b[1] : s_clip[1] - m) - m); if (y0 < 0) y0 = 0;
    x1 = (int)ceilf((b[2] < s_clip[2] + m ? b[2] : s_clip[2] + m) + m); if (x1 > c->w) x1 = c->w;
    y1 = (int)ceilf((b[3] < s_clip[3] + m ? b[3] : s_clip[3] + m) + m); if (y1 > c->h) y1 = c->h;
    for (y = y0; y < y1; y++) {
        float fy = (float)y + 0.5f, t = b[3] > b[1] ? clampf((fy - b[1]) / (b[3] - b[1]), 0, 1) : 0, col[3];
        if (fy < s_clip[1] || fy > s_clip[3])
            continue;
        for (k = 0; k < 3; k++)
            col[k] = top[k] + (bot[k] - top[k]) * t;
        for (x = x0; x < x1; x++) {
            float fx = (float)x + 0.5f, d, cov;
            if (fx < s_clip[0] || fx > s_clip[2])
                continue;
            d = sdf(s, fx, fy);
            if (mode == P_FILL)        cov = clampf(0.5f - d, 0, 1);
            else if (mode == P_STROKE) cov = clampf(0.5f - (fabsf(d) - width * 0.5f), 0, 1);
            else                       cov = d <= 0 ? 1.0f : expf(-(d * d) / (width * width));
            if (cov > 0.003f)
                px_over(c, x, y, col, cov * alpha);
        }
    }
}

static shape sh(int kind, float a0, float a1, float a2, float a3, float a4, float a5)
{
    shape s;
    memset(&s, 0, sizeof s);
    s.kind = kind;
    s.a[0] = a0; s.a[1] = a1; s.a[2] = a2; s.a[3] = a3; s.a[4] = a4; s.a[5] = a5;
    return s;
}

static void line(cv *c, float ax, float ay, float bx, float by, float w, const float col[3], float alpha)
{
    shape s = sh(SH_SEG, ax, ay, bx, by, w * 0.5f, 0);
    paint(c, &s, P_FILL, 0, col, col, alpha);
}

/* ── text, through GDI (drawn three times the size and scaled down) ──────── */

static uint8_t *text_mask(const WCHAR *s, float em, int weight, int *ow, int *oh)
{
    enum { SS = 3 };
    int n = lstrlenW(s), fh = (int)(em * SS + 0.5f), W, H, x, y, x0, y0, x1, y1, mw, mh;
    BITMAPINFO bi;
    void *bits = NULL;
    HDC dc;
    HBITMAP bm;
    HFONT f;
    HGDIOBJ ob, of;
    uint8_t *m = NULL;
    const uint32_t *px;
    *ow = *oh = 0;
    if (fh < 4) fh = 4;
    W = fh * (n + 2); H = fh * 2 + 8;
    dc = CreateCompatibleDC(NULL);
    if (!dc)
        return NULL;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bm || !bits) {
        DeleteDC(dc);
        return NULL;
    }
    ob = SelectObject(dc, bm);
    memset(bits, 0, (size_t)W * H * 4);
    f = CreateFontW(-fh, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                    ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    TextOutW(dc, fh / 2, fh / 4, s, n);
    GdiFlush();
    px = (const uint32_t *)bits;
    x0 = W; y0 = H; x1 = y1 = -1;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            if (px[(size_t)y * W + x] & 0x00FF00u) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    if (x1 >= x0) {
        mw = (x1 - x0 + SS) / SS; mh = (y1 - y0 + SS) / SS;
        m = (uint8_t *)calloc((size_t)mw * mh, 1);
        if (m) {
            for (y = 0; y < mh; y++)
                for (x = 0; x < mw; x++) {
                    int sx, sy, sum = 0;
                    for (sy = 0; sy < SS; sy++)
                        for (sx = 0; sx < SS; sx++) {
                            int px_x = x0 + x * SS + sx, px_y = y0 + y * SS + sy;
                            if (px_x <= x1 && px_y <= y1)
                                sum += (px[(size_t)px_y * W + px_x] >> 8) & 0xFF;
                        }
                    m[(size_t)y * mw + x] = (uint8_t)(sum / (SS * SS));
                }
            *ow = mw; *oh = mh;
        }
    }
    SelectObject(dc, of); SelectObject(dc, ob);
    DeleteObject(f); DeleteObject(bm); DeleteDC(dc);
    return m;
}

/* `cap` is the height of a capital letter in pixels; the text is centred on
 * (cx, cy) by what it inks and made smaller to fit in `maxw`. */
static void text(cv *c, const WCHAR *s, float cx, float cy, float cap, int weight, const float col[3], float alpha, float maxw)
{
    int w, h, x, y, ox, oy, pass;
    float em = cap / 0.70f;
    uint8_t *m = NULL;
    for (pass = 0; pass < 2; pass++) {
        m = text_mask(s, em, weight, &w, &h);
        if (!m)
            return;
        if (pass == 0 && maxw > 0 && (float)w > maxw) {
            em *= maxw / (float)w;
            free(m);
            m = NULL;
            continue;
        }
        break;
    }
    if (!m)
        return;
    ox = (int)floorf(cx - (float)w * 0.5f + 0.5f); oy = (int)floorf(cy - (float)h * 0.5f + 0.5f);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            if (m[(size_t)y * w + x] && ox + x >= 0 && ox + x < c->w && oy + y >= 0 && oy + y < c->h)
                px_over(c, ox + x, oy + y, col, alpha * (float)m[(size_t)y * w + x] / 255.0f);
    free(m);
}

/* ── what to draw for a button ───────────────────────────────────────────── */

enum {
    K_NONE, K_FACE, K_BUMPER, K_TRIGGER, K_STICK, K_DPAD, K_MENU, K_VIEW, K_TEXTBTN,
    K_KEY, K_KEYARROW, K_KEYS4, K_MOUSE, K_ARROW, K_ROTATE, K_PLUS
};
typedef struct {
    int kind, n;                    /* n: which face button, stick, direction, mouse button */
    WCHAR label[4][8];
} gsrc;

static const float INK[3] = { 0.93f, 0.89f, 1.00f }, MUTED[3] = { 0.66f, 0.58f, 0.88f };
static const float D0[3] = { 0.20f, 0.13f, 0.34f }, D1[3] = { 0.08f, 0.05f, 0.16f }, DARK[3] = { 0.09f, 0.05f, 0.17f };
static const float LILAC[3] = { 0.72f, 0.58f, 1.00f };
static const float FACE_XBOX[4][3] = { { 0.45f, 0.88f, 0.40f }, { 0.98f, 0.38f, 0.38f }, { 0.38f, 0.64f, 1.00f }, { 1.00f, 0.80f, 0.26f } };
static const float FACE_PS[4][3] = { { 0.52f, 0.66f, 1.00f }, { 1.00f, 0.44f, 0.47f }, { 0.93f, 0.58f, 0.88f }, { 0.36f, 0.88f, 0.74f } };

static int action_index(const char *name)
{
    int i, n = btvs_input_action_count();
    for (i = 0; i < n; i++)
        if (!strcmp(btvs_input_action_name(i), name))
            return i;
    return -1;
}

static void set_label(gsrc *g, int i, const char *s)
{
    MultiByteToWideChar(CP_ACP, 0, s, -1, g->label[i], 8);
    g->label[i][7] = 0;
}

static void pad_source(const char *name, gsrc *g)
{
    static const char *const face = "ABXY";
    const int ps = s_set == SET_PS;
    memset(g, 0, sizeof *g);
    if (strlen(name) == 1 && strchr(face, name[0])) {
        g->kind = K_FACE; g->n = (int)(strchr(face, name[0]) - face);
        set_label(g, 0, name);
    } else if (!strcmp(name, "LB") || !strcmp(name, "RB")) {
        g->kind = K_BUMPER; g->n = name[0] == 'R';
        set_label(g, 0, ps ? (g->n ? "R1" : "L1") : name);
    } else if (!strcmp(name, "LT") || !strcmp(name, "RT")) {
        g->kind = K_TRIGGER; g->n = name[0] == 'R';
        set_label(g, 0, ps ? (g->n ? "R2" : "L2") : name);
    } else if (!strcmp(name, "L3") || !strcmp(name, "R3") || !strncmp(name, "LS-", 3) || !strncmp(name, "RS-", 3)) {
        g->kind = K_STICK; g->n = name[0] == 'R';
        set_label(g, 0, g->n ? "R" : "L");
    } else if (!strncmp(name, "Dpad", 4)) {
        g->kind = K_DPAD;
        g->n = !strcmp(name, "DpadUp") ? 0 : !strcmp(name, "DpadDown") ? 1 : !strcmp(name, "DpadLeft") ? 2 : 3;
    } else if (!strcmp(name, "Start")) {
        g->kind = K_MENU;
    } else if (!strcmp(name, "Back")) {
        g->kind = K_VIEW;
    } else if (!strncmp(name, "Paddle", 6)) {
        char t[4] = { 'P', name[6], 0, 0 };
        g->kind = K_TEXTBTN; set_label(g, 0, t);
    } else if (name[0] && strcmp(name, "None")) {
        g->kind = K_TEXTBTN; set_label(g, 0, !strcmp(name, "Touchpad") ? "Pad" : !strcmp(name, "Guide") ? "Home" : name);
    }
}

static void key_source(const char *name, gsrc *g)
{
    static const struct { const char *name, *label; } k_short[] = {
        { "Backspace", "Bksp" }, { "Escape", "Esc" }, { "LShift", "Shift" }, { "RShift", "Shift" },
        { "LCtrl", "Ctrl" }, { "RCtrl", "Ctrl" }, { "LAlt", "Alt" }, { "RAlt", "Alt" }, { "CapsLock", "Caps" },
        { "PageUp", "PgUp" }, { "PageDown", "PgDn" }, { "Delete", "Del" }, { "Insert", "Ins" },
        { "Comma", "," }, { "Mouse4", "M4" }, { "Mouse5", "M5" },
    };
    int i;
    memset(g, 0, sizeof *g);
    if (!name[0])
        return;
    if (!strcmp(name, "MouseLeft"))        { g->kind = K_MOUSE; g->n = 0; }
    else if (!strcmp(name, "MouseRight"))  { g->kind = K_MOUSE; g->n = 1; }
    else if (!strcmp(name, "MouseMiddle")) { g->kind = K_MOUSE; g->n = 2; }
    else if (!strcmp(name, "WheelUp"))     { g->kind = K_MOUSE; g->n = 3; }
    else if (!strcmp(name, "WheelDown"))   { g->kind = K_MOUSE; g->n = 4; }
    else if (!strcmp(name, "Up"))          { g->kind = K_KEYARROW; g->n = 0; }
    else if (!strcmp(name, "Down"))        { g->kind = K_KEYARROW; g->n = 1; }
    else if (!strcmp(name, "Left"))        { g->kind = K_KEYARROW; g->n = 2; }
    else if (!strcmp(name, "Right"))       { g->kind = K_KEYARROW; g->n = 3; }
    else {
        g->kind = K_KEY;
        set_label(g, 0, name);
        for (i = 0; i < (int)(sizeof k_short / sizeof k_short[0]); i++)
            if (!strcmp(name, k_short[i].name))
                set_label(g, 0, k_short[i].label);
    }
}

static const char *key_of(const char *action, char *buf, int size)
{
    int a = action_index(action);
    buf[0] = 0;
    if (a >= 0) {
        btvs_input_binding_text(a, 0, buf, size);
        if (!buf[0])
            btvs_input_binding_text(a, 1, buf, size);
    }
    return buf;
}

/* The picture for one of the Xbox pad's inputs (an action of the input layer). */
static void action_source(const char *action, gsrc *g)
{
    char buf[48];
    int a = action_index(action);
    memset(g, 0, sizeof *g);
    if (a < 0)
        return;
    if (s_set == SET_KEYS) {
        key_source(key_of(action, buf, sizeof buf), g);
    } else {
        btvs_input_binding_text(a, 2, buf, sizeof buf);          /* "Pad LB" */
        pad_source(strlen(buf) > 4 ? buf + 4 : "", g);
    }
}

static void keys4_source(const char *const act[4], gsrc *g);

/* A whole stick: the pad's stick, or the four keys (the mouse, for the camera). */
static void stick_source(int right, gsrc *g)
{
    static const char *const k_dir[2][4] = {
        { "MoveForward", "MoveLeft", "MoveBack", "MoveRight" }, { "LookUp", "LookLeft", "LookDown", "LookRight" } };
    char buf[48];
    int i, a;
    memset(g, 0, sizeof *g);
    if (s_set != SET_KEYS) {
        a = action_index(k_dir[right][0]);
        btvs_input_binding_text(a, 2, buf, sizeof buf);
        pad_source(strlen(buf) > 4 ? buf + 4 : "", g);
        if (g->kind != K_STICK) {                               /* (bound to buttons: show the stick it stands for) */
            memset(g, 0, sizeof *g);
            g->kind = K_STICK; g->n = right; set_label(g, 0, right ? "R" : "L");
        }
        return;
    }
    if (right && btvs_setting_int("controls", "mouse_look", 1)) {
        g->kind = K_MOUSE; g->n = 5;                            /* the mouse itself */
        return;
    }
    keys4_source(k_dir[right], g);
}

/* Four keys as one picture: up / left down right. */
static void keys4_source(const char *const act[4], gsrc *g)
{
    char buf[48];
    int i;
    memset(g, 0, sizeof *g);
    g->kind = K_KEYS4;
    for (i = 0; i < 4; i++) {
        gsrc k;
        key_source(key_of(act[i], buf, sizeof buf), &k);
        if (k.kind == K_KEY && lstrlenW(k.label[0]) <= 2)
            lstrcpyW(g->label[i], k.label[0]);
        else if (k.kind == K_KEYARROW)
            lstrcpyW(g->label[i], k.n == 0 ? L"\x2191" : k.n == 1 ? L"\x2193" : k.n == 2 ? L"\x2190" : L"\x2192");
        else
            g->label[i][0] = 0;
    }
}

/* ── drawing a button ────────────────────────────────────────────────────── */

/* The menus have two textures a button: the usual one (which the game itself
 * fades when the button does nothing on a page) and one it flashes when the
 * button is pressed. INLINE: small, inside a line of text. */
enum { ST_NORMAL, ST_PRESSED, ST_INLINE };

static void mixc(float out[3], const float a[3], const float b[3], float t)
{
    int k;
    for (k = 0; k < 3; k++)
        out[k] = a[k] + (b[k] - a[k]) * t;
}

static void arrow(cv *c, float cx, float cy, float len, float ang, float w, const float col[3], float alpha)
{
    float dx = cosf(ang), dy = sinf(ang), tx = cx + dx * len * 0.5f, ty = cy + dy * len * 0.5f, h = len * 0.42f;
    line(c, cx - dx * len * 0.5f, cy - dy * len * 0.5f, tx, ty, w, col, alpha);
    line(c, tx, ty, tx + cosf(ang + 2.45f) * h, ty + sinf(ang + 2.45f) * h, w, col, alpha);
    line(c, tx, ty, tx + cosf(ang - 2.45f) * h, ty + sinf(ang - 2.45f) * h, w, col, alpha);
}

/* A dark plate with a light edge: the base of everything that is not a face
 * button. Pressed, it is a light plate (and what is on it goes dark). */
static void plate(cv *c, const shape *s, float d, int st)
{
    static const float WHITE[3] = { 1, 1, 1 };
    if (st == ST_PRESSED) {
        paint(c, s, P_GLOW, d * 0.12f, LILAC, LILAC, 0.50f);
        paint(c, s, P_FILL, 0, INK, LILAC, 1.0f);
        paint(c, s, P_STROKE, d * 0.065f, WHITE, INK, 1.0f);
    } else {
        paint(c, s, P_FILL, 0, D0, D1, 0.96f);
        paint(c, s, P_STROKE, d * (st == ST_INLINE ? 0.085f : 0.065f), WHITE, INK, 1.0f);
    }
}

static void key_cap(cv *c, float cx, float cy, float w, float h, float d, int st, const WCHAR *label, float cap)
{
    shape s = sh(SH_RBOX, cx, cy, w * 0.5f, h * 0.5f, d * 0.15f, 0);
    plate(c, &s, d, st);
    if (label && label[0])
        text(c, label, cx, cy, cap, 700, st == ST_PRESSED ? DARK : INK, 1.0f, w - d * 0.20f);
}

/* A mouse, d high, with the parts in `lit` light. */
enum { M_LEFT = 1, M_RIGHT = 2, M_WHEEL = 4, M_UP = 8, M_DOWN = 16, M_MOVED = 32 };

static void mouse_pic(cv *c, float cx, float cy, float d, int st, int lit)
{
    const float *ink = st == ST_PRESSED ? DARK : INK;
    float hw = d * 0.30f, hh = d * 0.46f, split = cy - hh * 0.18f;
    float keep[4];
    shape s = sh(SH_RBOX, cx, cy, hw, hh, hw * 0.95f, 0), wheel;
    int half;
    plate(c, &s, d, st);
    memcpy(keep, s_clip, sizeof keep);
    for (half = 0; half < 2; half++) {          /* a lit half above the split */
        if (!(lit & (half ? M_RIGHT : M_LEFT)))
            continue;
        s_clip[1] = cy - hh; s_clip[3] = split;
        s_clip[0] = half ? cx : cx - hw; s_clip[2] = half ? cx + hw : cx;
        paint(c, &s, P_FILL, 0, st == ST_PRESSED ? D0 : INK, st == ST_PRESSED ? D1 : LILAC, 1.0f);
        memcpy(s_clip, keep, sizeof keep);
        paint(c, &s, P_STROKE, d * 0.065f, ink, ink, 1.0f);
    }
    line(c, cx - hw, split, cx + hw, split, d * 0.05f, ink, 1.0f);
    line(c, cx, cy - hh, cx, split, d * 0.05f, (lit & (M_LEFT | M_RIGHT)) == (M_LEFT | M_RIGHT) && st != ST_PRESSED ? DARK : ink, 1.0f);
    wheel = sh(SH_RBOX, cx, cy - hh * 0.56f, d * 0.065f, d * 0.13f, d * 0.065f, 0);
    if (lit & M_WHEEL) {
        paint(c, &wheel, P_FILL, 0, st == ST_PRESSED ? D0 : INK, st == ST_PRESSED ? D1 : LILAC, 1.0f);
        paint(c, &wheel, P_STROKE, d * 0.045f, st == ST_PRESSED ? INK : DARK, st == ST_PRESSED ? INK : DARK, 1.0f);
    } else {
        paint(c, &wheel, P_FILL, 0, st == ST_PRESSED ? INK : D0, st == ST_PRESSED ? LILAC : D1, 1.0f);
        paint(c, &wheel, P_STROKE, d * 0.045f, ink, ink, 1.0f);
    }
    if ((lit & (M_UP | M_DOWN)) == (M_UP | M_DOWN)) {   /* the wheel, turned */
        arrow(c, cx, cy + hh * 0.22f, d * 0.17f, -1.5707963f, d * 0.055f, ink, 1.0f);
        arrow(c, cx, cy + hh * 0.62f, d * 0.17f, 1.5707963f, d * 0.055f, ink, 1.0f);
    } else if (lit & (M_UP | M_DOWN)) {
        arrow(c, cx, cy + hh * 0.42f, d * 0.26f, lit & M_UP ? -1.5707963f : 1.5707963f, d * 0.07f, ink, 1.0f);
    }
    if (lit & M_MOVED) {
        arrow(c, cx - hw - d * 0.12f, cy, d * 0.10f, 3.1415927f, d * 0.055f, ink, 1.0f);
        arrow(c, cx + hw + d * 0.12f, cy, d * 0.10f, 0, d * 0.055f, ink, 1.0f);
    }
}

/* (cx, cy): the middle; d: the size (a face button's diameter), in pixels. */
static void draw_glyph(cv *c, float cx, float cy, float d, const gsrc *g, int st)
{
    const float *ink = st == ST_PRESSED ? DARK : INK;
    float ia = 1.0f, r = d * 0.5f;
    shape s;
    switch (g->kind) {
    case K_FACE: {
        const float *acc = s_set == SET_PS ? FACE_PS[g->n] : FACE_XBOX[g->n];
        float sym[3], lo[3], w = d * 0.085f;
        static const float WHITE[3] = { 1, 1, 1 }, BLACK[3] = { 0, 0, 0 };
        s = sh(SH_CIRCLE, cx, cy, r * 0.93f, 0, 0, 0);
        if (st != ST_NORMAL) {                  /* small, or pressed: a filled disc */
            if (st == ST_PRESSED)
                paint(c, &s, P_GLOW, d * 0.16f, acc, acc, 0.60f);
            mixc(sym, acc, WHITE, 0.18f); mixc(lo, acc, BLACK, 0.22f);
            paint(c, &s, P_FILL, 0, sym, lo, 1.0f);
            memcpy(sym, DARK, sizeof sym);
            w = d * 0.105f;
        } else {
            paint(c, &s, P_FILL, 0, D0, D1, 0.95f);
            mixc(lo, acc, BLACK, 0.15f);
            paint(c, &s, P_STROKE, d * 0.085f, acc, lo, 1.0f);
            mixc(sym, acc, WHITE, 0.30f);
        }
        if (s_set != SET_PS) {
            text(c, g->label[0], cx, cy, d * (st == ST_NORMAL ? 0.44f : 0.50f), 700, sym, ia, 0);
        } else if (g->n == 0) {                 /* cross */
            float k = d * 0.19f;
            line(c, cx - k, cy - k, cx + k, cy + k, w, sym, ia);
            line(c, cx + k, cy - k, cx - k, cy + k, w, sym, ia);
        } else if (g->n == 1) {                 /* circle */
            s = sh(SH_CIRCLE, cx, cy, d * 0.23f, 0, 0, 0);
            paint(c, &s, P_STROKE, w, sym, sym, ia);
        } else if (g->n == 2) {                 /* square */
            s = sh(SH_RBOX, cx, cy, d * 0.20f, d * 0.20f, d * 0.02f, 0);
            paint(c, &s, P_STROKE, w, sym, sym, ia);
        } else {                                /* triangle */
            float k = d * 0.25f, y0 = cy - k * 1.05f, y1 = cy + k * 0.68f;
            line(c, cx, y0, cx + k, y1, w, sym, ia);
            line(c, cx + k, y1, cx - k, y1, w, sym, ia);
            line(c, cx - k, y1, cx, y0, w, sym, ia);
        }
        break;
    }
    case K_BUMPER:
        s = sh(SH_RBOX, cx, cy, d * 0.49f, d * 0.31f, d * 0.17f, 0);
        plate(c, &s, d, st);
        text(c, g->label[0], cx, cy, d * 0.33f, 700, ink, ia, d * 0.76f);
        break;
    case K_TRIGGER:
        s = sh(SH_RBOX, cx, cy, d * 0.38f, d * 0.47f, d * 0.24f, 0);
        plate(c, &s, d, st);
        text(c, g->label[0], cx, cy + d * 0.02f, d * 0.36f, 700, ink, ia, d * 0.60f);
        break;
    case K_TEXTBTN:
        s = sh(SH_CIRCLE, cx, cy, r * 0.93f, 0, 0, 0);
        plate(c, &s, d, st);
        text(c, g->label[0], cx, cy, d * 0.30f, 700, ink, ia, d * 0.66f);
        break;
    case K_STICK: {
        float top[3];
        static const float WHITE[3] = { 1, 1, 1 };
        s = sh(SH_CIRCLE, cx, cy, r * 0.90f, 0, 0, 0);
        plate(c, &s, d, st);
        s = sh(SH_CIRCLE, cx, cy, r * 0.62f, 0, 0, 0);
        mixc(top, INK, WHITE, 0.4f);
        if (st == ST_PRESSED) {
            paint(c, &s, P_FILL, 0, D0, D1, 1.0f);
            text(c, g->label[0], cx, cy, d * 0.34f, 800, INK, 1.0f, 0);
        } else {
            paint(c, &s, P_FILL, 0, top, LILAC, 1.0f);
            text(c, g->label[0], cx, cy, d * 0.34f, 800, DARK, 1.0f, 0);
        }
        break;
    }
    case K_DPAD: {
        static const float dir[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
        float k = d * 0.33f, t = d * 0.13f, px = dir[g->n][0], py = dir[g->n][1];
        shape tri;
        s = sh(SH_CROSS, cx, cy, d * 0.46f, d * 0.16f, d * 0.05f, 0);
        plate(c, &s, d, st);
        tri = sh(SH_TRI, cx + px * (k + t * 0.5f), cy + py * (k + t * 0.5f),
                 cx + px * (k - t) - py * t, cy + py * (k - t) + px * t, 0, 0);
        tri.a[4] = cx + px * (k - t) + py * t; tri.a[5] = cy + py * (k - t) - px * t;
        paint(c, &tri, P_FILL, 0, ink, ink, ia);
        break;
    }
    case K_MENU: {
        float k;
        s = sh(SH_CIRCLE, cx, cy, r * 0.93f, 0, 0, 0);
        plate(c, &s, d, st);
        for (k = -1; k <= 1; k++)
            line(c, cx - d * 0.20f, cy + k * d * 0.14f, cx + d * 0.20f, cy + k * d * 0.14f, d * 0.075f, ink, ia);
        break;
    }
    case K_VIEW: {
        shape b2;
        s = sh(SH_CIRCLE, cx, cy, r * 0.93f, 0, 0, 0);
        plate(c, &s, d, st);
        b2 = sh(SH_RBOX, cx - d * 0.06f, cy - d * 0.06f, d * 0.16f, d * 0.12f, d * 0.03f, 0);
        paint(c, &b2, P_STROKE, d * 0.06f, ink, ink, ia);
        b2 = sh(SH_RBOX, cx + d * 0.07f, cy + d * 0.07f, d * 0.16f, d * 0.12f, d * 0.03f, 0);
        paint(c, &b2, P_FILL, 0, st == ST_PRESSED ? INK : D0, st == ST_PRESSED ? LILAC : D1, 1.0f);
        paint(c, &b2, P_STROKE, d * 0.06f, ink, ink, ia);
        break;
    }
    case K_KEY: {
        int n = lstrlenW(g->label[0]);
        float w = n <= 1 ? d * 0.90f : d * 0.98f;
        key_cap(c, cx, cy, w, d * 0.90f, d, st, g->label[0], n <= 1 ? d * 0.42f : n <= 3 ? d * 0.30f : d * 0.26f);
        break;
    }
    case K_KEYARROW: {
        static const float ang[4] = { -1.5707963f, 1.5707963f, 3.1415927f, 0 };
        key_cap(c, cx, cy, d * 0.90f, d * 0.90f, d, st, NULL, 0);
        arrow(c, cx, cy, d * 0.42f, ang[g->n], d * 0.085f, ink, ia);
        break;
    }
    case K_KEYS4: {                             /* up / left down right */
        float k = d * 0.31f, cap = d * 0.17f, gap = d * 0.335f;
        if (!g->label[0][0] && !g->label[1][0] && !g->label[2][0] && !g->label[3][0]) {
            key_cap(c, cx, cy, d * 0.90f, d * 0.90f, d, st, NULL, 0);
            break;
        }
        key_cap(c, cx, cy - gap * 0.5f, k, k, d * 0.62f, st, g->label[0], cap);
        key_cap(c, cx - gap, cy + gap * 0.5f, k, k, d * 0.62f, st, g->label[1], cap);
        key_cap(c, cx, cy + gap * 0.5f, k, k, d * 0.62f, st, g->label[2], cap);
        key_cap(c, cx + gap, cy + gap * 0.5f, k, k, d * 0.62f, st, g->label[3], cap);
        break;
    }
    case K_MOUSE: {
        static const int k_mask[6] = { M_LEFT, M_RIGHT, M_WHEEL, M_WHEEL | M_UP, M_WHEEL | M_DOWN, M_MOVED };
        mouse_pic(c, cx, cy, d, st, k_mask[g->n]);
        break;
    }
    case K_ARROW: {
        static const float SHADOW[3] = { 0.10f, 0.05f, 0.20f };
        float ang = (float)g->n * 0.7853982f;   /* n: eighths of a turn clockwise from "right" */
        arrow(c, cx, cy + d * 0.03f, d * 0.72f, ang, d * 0.17f, SHADOW, 0.55f);
        arrow(c, cx, cy, d * 0.72f, ang, d * 0.115f, LILAC, 1.0f);
        break;
    }
    case K_ROTATE: {
        float rr = d * 0.30f, a0 = -0.9f, a1 = 4.0f, ex = cx + cosf(a1) * rr, ey = cy + sinf(a1) * rr, t = a1 + 1.5707963f;
        s = sh(SH_ARC, cx, cy, rr, a0, a1, d * 0.058f);
        paint(c, &s, P_FILL, 0, LILAC, LILAC, 1.0f);
        line(c, ex, ey, ex + cosf(t + 2.5f) * d * 0.24f, ey + sinf(t + 2.5f) * d * 0.24f, d * 0.115f, LILAC, 1.0f);
        line(c, ex, ey, ex + cosf(t - 2.5f) * d * 0.24f, ey + sinf(t - 2.5f) * d * 0.24f, d * 0.115f, LILAC, 1.0f);
        break;
    }
    case K_PLUS:
        line(c, cx - d * 0.30f, cy, cx + d * 0.30f, cy, d * 0.13f, LILAC, 1.0f);
        line(c, cx, cy - d * 0.30f, cx, cy + d * 0.30f, d * 0.13f, LILAC, 1.0f);
        break;
    default:                                    /* nothing bound */
        s = sh(SH_CIRCLE, cx, cy, r * 0.80f, 0, 0, 0);
        paint(c, &s, P_STROKE, d * 0.05f, MUTED, MUTED, 0.45f);
        break;
    }
}

/* ── PNG out (32-bit, stored) ────────────────────────────────────────────── */

static uint32_t crc32_of(const uint8_t *p, size_t n, uint32_t crc)
{
    static uint32_t t[256];
    size_t i;
    if (!t[1]) {
        uint32_t c, k, j;
        for (k = 0; k < 256; k++) {
            for (c = k, j = 0; j < 8; j++)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[k] = c;
        }
    }
    crc = ~crc;
    for (i = 0; i < n; i++)
        crc = t[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static void png_chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n)
{
    uint8_t h[8];
    uint32_t crc;
    be32(h, n); memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    crc = crc32_of(h + 4, 4, 0);
    crc = crc32_of(d, n, crc);
    be32(h, crc);
    fwrite(h, 1, 4, f);
}

/* The part (x0, y0, w, h) of the canvas as a PNG. Pixels nothing was drawn on
 * take a neutral colour, so that scaling does not bring in black edges. */
static int cv_write(const cv *c, int x0, int y0, int w, int h, const WCHAR *path)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    size_t row = 1 + (size_t)w * 4, raw = row * h, blocks = (raw + 65534) / 65535, zn = 2 + raw + blocks * 5 + 4, o = 0, i;
    uint8_t *z = (uint8_t *)malloc(zn), *img = (uint8_t *)malloc(raw), ihdr[13];
    uint32_t a = 1, b = 0;
    FILE *f = NULL;
    int x, y, k;
    if (!z || !img || _wfopen_s(&f, path, L"wb") || !f) {
        free(z); free(img);
        return 0;
    }
    for (y = 0; y < h; y++) {
        uint8_t *d = img + row * y;
        *d++ = 0;
        for (x = 0; x < w; x++) {
            const float *s = c->p + 4 * ((size_t)(y0 + y) * c->w + x0 + x);
            for (k = 0; k < 3; k++)
                *d++ = (uint8_t)(clampf(s[3] > 0 ? s[k] : MUTED[k], 0, 1) * 255.0f + 0.5f);
            *d++ = (uint8_t)(clampf(s[3], 0, 1) * 255.0f + 0.5f);
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
    be32(z + o, (b << 16) | a);
    o += 4;
    fwrite(sig, 1, 8, f);
    be32(ihdr, (uint32_t)w); be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", z, (uint32_t)o);
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(z); free(img);
    return 1;
}

/* ── the game's textures and what goes in their place ────────────────────── */

static WCHAR s_load[MAX_PATH];             /* <pack>\load */
static int s_written;

static void write_tex(const cv *c, int x0, int y0, int w, int h, const char *what, const char *hash)
{
    WCHAR path[MAX_PATH];
    swprintf_s(path, MAX_PATH, L"%s\\%S_%S.png", s_load, what, hash);
    s_written += cv_write(c, x0, y0, w, h, path);
}

/* The menus' buttons: a 64x64 texture each, the button's middle at (cx, cy). */
static const struct { const char *hash, *action; int st; float cx, cy; } k_menu[] = {
    { "2c2cffdf2d9f3df0", "A", ST_NORMAL, 28.5f, 35.5f },            { "900e2cb5737cf8c5", "A", ST_PRESSED, 28.5f, 35.5f },
    { "cb8e1b149a273b48", "B", ST_NORMAL, 31.5f, 39.5f },            { "8a9435603a2b138c", "B", ST_PRESSED, 31.5f, 39.5f },
    { "c9fe979a030c19f9", "X", ST_NORMAL, 34.5f, 24.5f },            { "54e1f70799c147a0", "X", ST_PRESSED, 34.5f, 24.5f },
    { "50a1335737741307", "Y", ST_NORMAL, 37.5f, 28.5f },            { "3df72ee3ccd324e3", "Y", ST_PRESSED, 37.5f, 28.5f },
    { "32a88c6376b54a2f", "LeftTrigger", ST_NORMAL, 35.0f, 30.0f },  { "d556da5f7aa60e0e", "LeftTrigger", ST_PRESSED, 35.0f, 30.0f },
    { "a820a77e9ee26382", "RightTrigger", ST_NORMAL, 29.0f, 30.0f }, { "b92832f6e90749ba", "RightTrigger", ST_PRESSED, 29.0f, 30.0f },
};

static void build_menu(void)
{
    int i;
    for (i = 0; i < (int)(sizeof k_menu / sizeof k_menu[0]); i++) {
        cv c;
        gsrc g;
        if (!cv_make(&c, 64 * SCALE, 64 * SCALE))
            return;
        action_source(k_menu[i].action, &g);
        draw_glyph(&c, k_menu[i].cx * SCALE, k_menu[i].cy * SCALE, 25.0f * SCALE, &g, k_menu[i].st);
        write_tex(&c, 0, 0, c.w, c.h, "menu", k_menu[i].hash);
        free(c.p);
    }
}

/* The page of small pictures the tips and the journal write as letters a..t
 * (128x256; each picture's box as the game's font has it). */
static const struct { int x0, y0, x1, y1; } k_page[20] = {
    { 1, 1, 21, 24 },   { 25, 1, 45, 24 },   { 49, 1, 69, 24 },   { 73, 1, 93, 24 },      /* A B X Y */
    { 0, 33, 20, 56 },  { 22, 33, 42, 56 },  { 44, 33, 66, 56 },  { 70, 33, 89, 56 },     /* L R, right, down-right */
    { 0, 66, 21, 88 },  { 23, 66, 42, 88 },  { 46, 66, 68, 88 },  { 70, 66, 89, 88 },     /* down, down-left, left, up-left */
    { 0, 99, 21, 123 }, { 24, 99, 43, 123 }, { 45, 99, 67, 123 }, { 69, 99, 92, 123 },    /* up, up-right, left stick, turn */
    { 0, 132, 22, 154 }, { 24, 132, 45, 154 }, { 48, 132, 66, 154 }, { 70, 132, 88, 154 }, /* right stick, plus, White, Black */
};

static void page_source(int i, gsrc *g)
{
    static const char *const act[20] = { "A", "B", "X", "Y", "LeftTrigger", "RightTrigger", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "White", "Black" };
    static const int turn[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };       /* letters g..n: right, then clockwise */
    memset(g, 0, sizeof *g);
    if (act[i])                action_source(act[i], g);
    else if (i >= 6 && i <= 13) { g->kind = K_ARROW; g->n = turn[i - 6]; }
    else if (i == 14)          stick_source(0, g);
    else if (i == 15)          g->kind = K_ROTATE;
    else if (i == 16)          stick_source(1, g);
    else                       g->kind = K_PLUS;
}

static void build_page(void)
{
    cv c;
    int i;
    if (!cv_make(&c, 128 * SCALE, 256 * SCALE))
        return;
    for (i = 0; i < 20; i++) {
        gsrc g;
        float w = (float)(k_page[i].x1 - k_page[i].x0), h = (float)(k_page[i].y1 - k_page[i].y0);
        page_source(i, &g);
        draw_glyph(&c, (float)(k_page[i].x0 + k_page[i].x1) * 0.5f * SCALE, (float)(k_page[i].y0 + k_page[i].y1) * 0.5f * SCALE,
                   (w < h ? w : h) * 0.85f * SCALE, &g, ST_INLINE);     /* (room around it: the next word follows at once) */
    }
    write_tex(&c, 0, 0, c.w, c.h, "tips", "da8653b18cdf8d7a");
    free(c.p);
}

/* The Controls page's picture, 384x320 over four textures. The game writes
 * its labels beside the ends of the lines, so those stay where they were. */
#define PX(v) ((float)(v) * SCALE)

static void callout(cv *c, const float *pts, int n, int tip)
{
    shape s;
    int i;
    for (i = 0; i + 1 < n; i++)
        line(c, PX(pts[i * 2]), PX(pts[i * 2 + 1]), PX(pts[i * 2 + 2]), PX(pts[i * 2 + 3]), PX(1.15f), LILAC, 0.95f);
    s = sh(SH_CIRCLE, PX(pts[0]), PX(pts[1]), PX(3.2f), 0, 0, 0);               /* the label's end: a small ring */
    paint(c, &s, P_GLOW, PX(2.0f), LILAC, LILAC, 0.35f);
    paint(c, &s, P_FILL, 0, D0, D1, 1.0f);
    paint(c, &s, P_STROKE, PX(1.3f), INK, LILAC, 1.0f);
    if (tip) {                                                                  /* the part's end: a dot */
        s = sh(SH_CIRCLE, PX(pts[n * 2 - 2]), PX(pts[n * 2 - 1]), PX(1.9f), 0, 0, 0);
        paint(c, &s, P_FILL, 0, INK, INK, 1.0f);
    }
}

/* The same page with keyboard and mouse: a keyboard and a mouse with every
 * key and button that does something light, and beside each of the game's
 * labels the keys the action is on now (two at most). */
static char s_lit[64][24];
static int s_nlit;

static int lit(const char *name)
{
    int i;
    for (i = 0; i < s_nlit; i++)
        if (!strcmp(s_lit[i], name))
            return 1;
    return 0;
}

/* One key of the keyboard: (x, y) its top left, w its width in keys, u a key's size. */
static void board_key(cv *c, float x, float y, float w, float u, const char *name)
{
    static const float WHITE[3] = { 1, 1, 1 };
    static const float ang[4] = { -1.5707963f, 1.5707963f, 3.1415927f, 0 };
    float cx = PX(x + w * u * 0.5f), cy = PX(y + u * 0.5f), d = PX(u);
    shape s = sh(SH_RBOX, cx, cy, PX(w * u * 0.5f - u * 0.07f), PX(u * 0.43f), PX(u * 0.17f), 0);
    gsrc k;
    if (!name[0] || !lit(name)) {
        paint(c, &s, P_FILL, 0, D0, D1, 0.92f);
        paint(c, &s, P_STROKE, d * 0.05f, MUTED, MUTED, 0.50f);
        return;
    }
    paint(c, &s, P_GLOW, d * 0.30f, LILAC, LILAC, 0.55f);
    paint(c, &s, P_FILL, 0, INK, LILAC, 1.0f);
    paint(c, &s, P_STROKE, d * 0.07f, WHITE, INK, 1.0f);
    key_source(name, &k);
    if (k.kind == K_KEYARROW)
        arrow(c, cx, cy, d * 0.42f, ang[k.n], d * 0.09f, DARK, 1.0f);
    else
        text(c, k.label[0], cx, cy, lstrlenW(k.label[0]) <= 1 ? d * 0.46f : d * 0.30f, 700, DARK, 1.0f, PX(w * u) - d * 0.26f);
}

static void build_keys_page(void)
{
    static const struct { const char *name; float w; } k_rows[5][15] = {
        { { "`", 1 }, { "1", 1 }, { "2", 1 }, { "3", 1 }, { "4", 1 }, { "5", 1 }, { "6", 1 }, { "7", 1 }, { "8", 1 }, { "9", 1 },
          { "0", 1 }, { "-", 1 }, { "=", 1 }, { "Backspace", 2 } },
        { { "Tab", 1.5f }, { "Q", 1 }, { "W", 1 }, { "E", 1 }, { "R", 1 }, { "T", 1 }, { "Y", 1 }, { "U", 1 }, { "I", 1 }, { "O", 1 },
          { "P", 1 }, { "[", 1 }, { "]", 1 }, { "\\", 1.5f } },
        { { "CapsLock", 1.75f }, { "A", 1 }, { "S", 1 }, { "D", 1 }, { "F", 1 }, { "G", 1 }, { "H", 1 }, { "J", 1 }, { "K", 1 },
          { "L", 1 }, { ";", 1 }, { "'", 1 }, { "Enter", 2.25f } },
        { { "LShift", 2.25f }, { "Z", 1 }, { "X", 1 }, { "C", 1 }, { "V", 1 }, { "B", 1 }, { "N", 1 }, { "M", 1 }, { "Comma", 1 },
          { ".", 1 }, { "/", 1 }, { "RShift", 2.75f } },
        { { "LCtrl", 1.25f }, { "", 1.25f }, { "LAlt", 1.25f }, { "Space", 6.25f }, { "RAlt", 1.25f }, { "", 1.25f }, { "", 1.25f },
          { "RCtrl", 1.25f } },
    };
    static const struct { const char *name; float x; } k_top[] = {
        { "Escape", 0 }, { "F1", 2 }, { "F2", 3 }, { "F3", 4 }, { "F4", 5 }, { "F5", 6.5f }, { "F6", 7.5f }, { "F7", 8.5f },
        { "F8", 9.5f }, { "F9", 11 }, { "F10", 12 }, { "F11", 13 }, { "F12", 14 },
    };
    static const struct { const char *name; float x, row; } k_nav[] = {
        { "Insert", 0, 0 }, { "Home", 1, 0 }, { "PageUp", 2, 0 }, { "Delete", 0, 1 }, { "End", 1, 1 }, { "PageDown", 2, 1 },
        { "Up", 1, 3 }, { "Left", 0, 4 }, { "Down", 1, 4 }, { "Right", 2, 4 },
    };
    /* the game's labels with one action each: the label's ring, the first key from it, the step to the second */
    static const struct { const char *action; float x, y, dx, dy, step; int most; } k_one[] = {
        { "LeftTrigger", 90, 18, 0, 18, 20, 2 },  { "RightTrigger", 228, 18, 0, 18, -20, 2 },
        { "White", 272, 28, -16, 2, -20, 1 },     { "Black", 294, 50, -16, 2, -20, 2 },
        { "Y", 297, 90, -16, 0, -20, 2 },         { "B", 318, 112, -16, 0, -20, 2 },
        { "X", 297, 133, -16, 0, -20, 2 },        { "A", 318, 154, -16, 0, -20, 2 },
        { "Back", 141, 279, 0, -18, -20, 2 },     { "Start", 175, 279, 0, -18, 20, 2 },
    };
    static const char *const k_move[4] = { "MoveForward", "MoveLeft", "MoveBack", "MoveRight" };
    static const char *const k_weapon[4] = { "DpadUp", "DpadLeft", "DpadDown", "DpadRight" };
    static const char *const k_camera[4] = { "LookUp", "LookLeft", "LookDown", "LookRight" };
    static const float BODY0[3] = { 0.17f, 0.12f, 0.30f }, BODY1[3] = { 0.07f, 0.04f, 0.14f };
    const float U = 11.0f, X0 = 37, Y0 = 85, NAVX = X0 + 15.4f * U, ROW0 = Y0 + 1.35f * U;
    const float MX = 221, MY = 205, MD = 60;
    const int mouse_look = btvs_setting_int("controls", "mouse_look", 1) != 0;
    char buf[48];
    cv c;
    shape s;
    gsrc g;
    int i, k, a, n = btvs_input_action_count(), mask = 0;
    float x, stub[6];
    if (!cv_make(&c, 384 * SCALE, 320 * SCALE))
        return;

    s_nlit = 0;
    for (a = 0; a < n; a++) {
        if (strstr(btvs_input_action_name(a), "StickClick"))    /* (not on this page) */
            continue;
        for (k = 0; k < 2; k++) {
            btvs_input_binding_text(a, k, buf, sizeof buf);
            if (buf[0] && s_nlit < 64)
                strcpy_s(s_lit[s_nlit++], sizeof s_lit[0], buf);
        }
    }
    if (lit("MouseLeft"))   mask |= M_LEFT;
    if (lit("MouseRight"))  mask |= M_RIGHT;
    if (lit("MouseMiddle")) mask |= M_WHEEL;
    if (lit("WheelUp"))     mask |= M_WHEEL | M_UP;
    if (lit("WheelDown"))   mask |= M_WHEEL | M_DOWN;

    /* the keyboard */
    s = sh(SH_RBOX, PX(X0 + 9.2f * U), PX(Y0 + 3.175f * U), PX(9.2f * U + 5), PX(3.175f * U + 5), PX(6), 0);
    paint(&c, &s, P_GLOW, PX(7), LILAC, LILAC, 0.38f);
    paint(&c, &s, P_FILL, 0, BODY0, BODY1, 0.985f);
    paint(&c, &s, P_STROKE, PX(1.5f), INK, LILAC, 0.95f);
    for (i = 0; i < (int)(sizeof k_top / sizeof k_top[0]); i++)
        board_key(&c, X0 + k_top[i].x * U, Y0, 1, U, k_top[i].name);
    for (i = 0; i < 5; i++)
        for (k = 0, x = X0; k < 15 && k_rows[i][k].w > 0; x += k_rows[i][k].w * U, k++)
            board_key(&c, x, ROW0 + (float)i * U, k_rows[i][k].w, U, k_rows[i][k].name);
    for (i = 0; i < (int)(sizeof k_nav / sizeof k_nav[0]); i++)
        board_key(&c, NAVX + k_nav[i].x * U, ROW0 + k_nav[i].row * U, 1, U, k_nav[i].name);

    /* the mouse */
    s = sh(SH_RBOX, PX(MX), PX(MY), PX(MD * 0.30f), PX(MD * 0.46f), PX(MD * 0.285f), 0);
    paint(&c, &s, P_GLOW, PX(7), LILAC, LILAC, 0.38f);
    mouse_pic(&c, PX(MX), PX(MY), PX(MD), ST_INLINE, mask);

    /* the labels */
    for (i = 0; i < (int)(sizeof k_one / sizeof k_one[0]); i++) {
        int shown = 0;
        a = action_index(k_one[i].action);
        stub[0] = k_one[i].x; stub[1] = k_one[i].y;
        stub[2] = k_one[i].x + k_one[i].dx * 0.5f; stub[3] = k_one[i].y + k_one[i].dy * 0.5f;
        callout(&c, stub, 2, 0);
        for (k = 0; k < 2 && shown < k_one[i].most; k++) {
            btvs_input_binding_text(a, k, buf, sizeof buf);
            if (!buf[0])
                continue;
            key_source(buf, &g);
            draw_glyph(&c, PX(k_one[i].x + k_one[i].dx + (float)shown * k_one[i].step), PX(k_one[i].y + k_one[i].dy), PX(19), &g, ST_INLINE);
            shown++;
        }
        if (!shown) {                           /* nothing bound */
            memset(&g, 0, sizeof g);
            draw_glyph(&c, PX(k_one[i].x + k_one[i].dx), PX(k_one[i].y + k_one[i].dy), PX(19), &g, ST_INLINE);
        }
    }
    stub[0] = 26; stub[1] = 53; stub[2] = 33; stub[3] = 53;
    callout(&c, stub, 2, 0);
    keys4_source(k_move, &g);
    draw_glyph(&c, PX(55), PX(52), PX(42), &g, ST_INLINE);
    stub[0] = 30; stub[1] = 212; stub[2] = 37; stub[3] = 212;
    callout(&c, stub, 2, 0);
    keys4_source(k_weapon, &g);
    draw_glyph(&c, PX(59), PX(211), PX(42), &g, ST_INLINE);
    stub[0] = 297; stub[1] = 212;
    if (mouse_look) {                           /* the camera is the mouse itself */
        stub[2] = 264; stub[3] = 212; stub[4] = MX + MD * 0.16f; stub[5] = MY + MD * 0.16f;
        callout(&c, stub, 3, 1);
    } else {
        stub[2] = 290; stub[3] = 212;
        callout(&c, stub, 2, 0);
        keys4_source(k_camera, &g);
        draw_glyph(&c, PX(268), PX(211), PX(42), &g, ST_INLINE);
    }

    write_tex(&c, 0, 0, 256 * SCALE, 256 * SCALE, "pad", "128e9d296c5ed8db");
    write_tex(&c, 256 * SCALE, 0, 128 * SCALE, 256 * SCALE, "pad", "678e86aa4ca925a0");
    write_tex(&c, 0, 256 * SCALE, 256 * SCALE, 64 * SCALE, "pad", "f26871cd3ad49a55");
    free(c.p);
}

static void build_controller(void)
{
    static const float move[] = { 26, 53, 62.5f, 94.5f }, weapon[] = { 30, 212, 62, 212, 88, 184 };
    static const float look[] = { 90, 18, 90, 30 }, target[] = { 228, 18, 228, 30 };
    static const float inventory[] = { 272, 28, 266, 29 }, ammo[] = { 294, 50, 288, 51 };
    static const float use[] = { 297, 90, 262, 90, 239, 92 }, jump[] = { 318, 112, 258, 112 };
    static const float punch[] = { 297, 133, 262, 131, 224, 114.5f }, kick[] = { 318, 154, 258, 154, 238, 136 };
    static const float camera[] = { 297, 212, 262, 212, 226, 184 }, back[] = { 141, 279, 141, 137 }, pause[] = { 175, 279, 175, 137 };
    static const float BODY0[3] = { 0.17f, 0.12f, 0.30f }, BODY1[3] = { 0.07f, 0.04f, 0.14f };
    static const struct { float x, y; int n; } face[4] = { { 232, 129, 0 }, { 249, 112, 1 }, { 215, 112, 2 }, { 232, 95, 3 } };
    cv c;
    shape s;
    gsrc g;
    int i, keep = s_set;
    if (s_set == SET_KEYS) {
        build_keys_page();
        return;
    }
    if (!cv_make(&c, 384 * SCALE, 320 * SCALE))
        return;

    /* triggers and shoulder buttons, behind the body */
    for (i = 0; i < 2; i++) {
        pad_source(i ? "RT" : "LT", &g);
        draw_glyph(&c, PX(i ? 228.0f : 90.0f), PX(42), PX(24), &g, ST_INLINE);
        pad_source(i ? "RB" : "LB", &g);
        draw_glyph(&c, PX(i ? 232.0f : 86.0f), PX(64), PX(30), &g, ST_INLINE);
    }
    s = sh(SH_BODY, (float)SCALE, 0, 0, 0, 0, 0);
    paint(&c, &s, P_GLOW, PX(7), LILAC, LILAC, 0.38f);
    paint(&c, &s, P_FILL, 0, BODY0, BODY1, 0.985f);
    paint(&c, &s, P_STROKE, PX(1.5f), INK, LILAC, 0.95f);

    /* sticks, d-pad, the middle buttons, the face buttons */
    memset(&g, 0, sizeof g);
    g.kind = K_STICK; g.n = 0; g.label[0][0] = L'L';
    draw_glyph(&c, PX(74), PX(106), PX(36), &g, ST_INLINE);
    g.n = 1; g.label[0][0] = L'R';
    draw_glyph(&c, PX(214), PX(172), PX(36), &g, ST_INLINE);
    s = sh(SH_CROSS, PX(100), PX(170), PX(17.5f), PX(6.2f), PX(2.0f), 0);
    paint(&c, &s, P_FILL, 0, D0, D1, 1.0f);
    paint(&c, &s, P_STROKE, PX(1.6f), INK, LILAC, 1.0f);
    s = sh(SH_CIRCLE, PX(160), PX(98), PX(8), 0, 0, 0);
    paint(&c, &s, P_STROKE, PX(1.3f), MUTED, MUTED, 0.7f);
    pad_source("Back", &g);  draw_glyph(&c, PX(141), PX(128), PX(17), &g, ST_INLINE);
    pad_source("Start", &g); draw_glyph(&c, PX(175), PX(128), PX(17), &g, ST_INLINE);
    for (i = 0; i < 4; i++) {
        memset(&g, 0, sizeof g);
        g.kind = K_FACE; g.n = face[i].n;
        g.label[0][0] = L"ABXY"[face[i].n];
        draw_glyph(&c, PX(face[i].x), PX(face[i].y), PX(17), &g, ST_NORMAL);
    }

    /* from each label to its part; the two shoulder buttons are named beside their labels */
    /* From each label to its part. A label whose button the player has put
     * somewhere else gets that button's picture beside it instead of a line
     * (as the two shoulder buttons always do: their labels are on the far side). */
    {
        static const struct { const char *action, *usual; const float *pts; int n, tip; float gx, gy; } k_label[] = {
            { "MoveForward", "LS-Up", move, 2, 1, 16, 0 },   { "DpadUp", "DpadUp", weapon, 3, 1, 16, 0 },
            { "LeftTrigger", "LT", look, 2, 0, 16, 2 },      { "RightTrigger", "RT", target, 2, 0, -16, 2 },
            { "Y", "Y", use, 3, 0, -15, 0 },                 { "B", "B", jump, 2, 0, -15, 0 },
            { "X", "X", punch, 3, 0, -15, 0 },               { "A", "A", kick, 3, 0, -15, 0 },
            { "LookUp", "RS-Up", camera, 3, 1, -15, 0 },     { "Back", "Back", back, 2, 0, 0, -15 },
            { "Start", "Start", pause, 2, 0, 0, -15 },       { "White", "", inventory, 2, 0, -16, 2 },
            { "Black", "", ammo, 2, 0, -17, 2 },
        };
        char buf[48];
        for (i = 0; i < (int)(sizeof k_label / sizeof k_label[0]); i++) {
            const float *p = k_label[i].pts;
            btvs_input_binding_text(action_index(k_label[i].action), 2, buf, sizeof buf);
            if (strlen(buf) > 4 && !strcmp(buf + 4, k_label[i].usual)) {
                callout(&c, p, k_label[i].n, k_label[i].tip);
            } else {
                float stub[4];
                stub[0] = p[0]; stub[1] = p[1];
                stub[2] = p[0] + k_label[i].gx * 0.5f; stub[3] = p[1] + k_label[i].gy * 0.5f;
                callout(&c, stub, 2, 0);
                pad_source(strlen(buf) > 4 ? buf + 4 : "", &g);
                draw_glyph(&c, PX(p[0] + k_label[i].gx), PX(p[1] + k_label[i].gy), PX(21), &g, ST_INLINE);
            }
        }
    }
    s_set = keep;

    write_tex(&c, 0, 0, 256 * SCALE, 256 * SCALE, "pad", "128e9d296c5ed8db");
    write_tex(&c, 256 * SCALE, 0, 128 * SCALE, 256 * SCALE, "pad", "678e86aa4ca925a0");
    write_tex(&c, 0, 256 * SCALE, 256 * SCALE, 64 * SCALE, "pad", "f26871cd3ad49a55");
    free(c.p);
}

/* ── start-up ────────────────────────────────────────────────────────────── */

/* What the files on disk were drawn from: the drawing's version, the set and
 * every binding that shows. Unchanged: nothing is drawn again. */
static void stamp_text(char *out, size_t size)
{
    char buf[48];
    int a, n = btvs_input_action_count(), slot;
    size_t o = (size_t)sprintf_s(out, size, "v%d set %d mouse %d\n", GLYPH_VERSION, s_set, btvs_setting_int("controls", "mouse_look", 1));
    for (a = 0; a < n; a++)
        for (slot = 0; slot < BTVS_BIND_SLOTS; slot++) {
            btvs_input_binding_text(a, slot, buf, sizeof buf);
            if (o + strlen(buf) + 2 < size)
                o += (size_t)sprintf_s(out + o, size - o, "%s;", buf);
        }
}

static void remove_old(void)
{
    WCHAR pat[MAX_PATH], p[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    swprintf_s(pat, MAX_PATH, L"%s\\*.png", s_load);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        swprintf_s(p, MAX_PATH, L"%s\\%s", s_load, fd.cFileName);
        DeleteFileW(p);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static int choose_set(void)
{
    const char *s = btvs_setting_str("controls", "button_pictures", "auto");
    if (!_stricmp(s, "original") || !_stricmp(s, "off"))  return SET_ORIGINAL;
    if (!_stricmp(s, "xbox"))                              return SET_XBOX;
    if (!_stricmp(s, "playstation") || !_stricmp(s, "ps")) return SET_PS;
    if (!_stricmp(s, "keyboard") || !_stricmp(s, "keys"))  return SET_KEYS;
    switch (btvs_input__glyph_device()) {
    case 1:  return SET_XBOX;
    case 2:  return SET_PS;
    default: return SET_KEYS;
    }
}

/* Before the game's first frame. */
void btvs_input__glyphs_boot(void)
{
    static const char *const k_names[] = { "keyboard and mouse", "Xbox", "PlayStation", "the game's own" };
    WCHAR root[MAX_PATH], stamp_path[MAX_PATH];
    char want[1024], have[1024];
    const char *e = getenv("BTVS_GLYPH_DIR");
    FILE *f = NULL;
    size_t n = 0;
    double t0 = btvs_frame_seconds();

    s_set = choose_set();
    if (s_set == SET_ORIGINAL) {
        if (getenv("BTVS_GLYPH_DUMP") && e) {                 /* testing: the game's own textures written out */
            MultiByteToWideChar(CP_ACP, 0, e, -1, root, MAX_PATH);
            nv2a_gpu_set_texture_pack(root, 1, 0, 0);
        }
        IN_LOG("button pictures: the game's own");
        return;
    }
    if (e && *e) {
        MultiByteToWideChar(CP_ACP, 0, e, -1, root, MAX_PATH);
    } else {
        WCHAR *slash;
        GetModuleFileNameW(NULL, root, MAX_PATH);
        slash = wcsrchr(root, L'\\');
        if (slash) *slash = 0;
        wcscat_s(root, MAX_PATH, L"\\button_pictures");
    }
    CreateDirectoryW(root, NULL);
    swprintf_s(s_load, MAX_PATH, L"%s\\load", root);
    CreateDirectoryW(s_load, NULL);
    swprintf_s(stamp_path, MAX_PATH, L"%s\\made_from.txt", s_load);

    stamp_text(want, sizeof want);
    have[0] = 0;
    if (!_wfopen_s(&f, stamp_path, L"rb") && f) {
        n = fread(have, 1, sizeof have - 1, f);
        have[n] = 0;
        fclose(f);
    }
    if (strcmp(want, have)) {
        remove_old();
        s_written = 0;
        build_menu();
        build_page();
        build_controller();
        if (s_written == (int)(sizeof k_menu / sizeof k_menu[0]) + 4 && !_wfopen_s(&f, stamp_path, L"wb") && f) {
            fwrite(want, 1, strlen(want), f);
            fclose(f);
        }
        IN_LOG("button pictures: %s set, %d pictures drawn in %.2f s (%S)", k_names[s_set], s_written, btvs_frame_seconds() - t0, s_load);
    } else {
        IN_LOG("button pictures: %s set (%S)", k_names[s_set], s_load);
    }
    nv2a_gpu_set_texture_pack(root, getenv("BTVS_GLYPH_DUMP") != NULL, 1, 1);
}

/* ── testing ─────────────────────────────────────────────────────────────── */

static int readable(uint32_t va, uint32_t len)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (va < 0x10000u || !VirtualQuery((void *)XBOX_PTR(va), &mbi, sizeof mbi))
        return 0;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return 0;
    return (uintptr_t)XBOX_PTR(va) + len <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

/* BTVS_GLYPH_RAMDUMP=<seconds>,<file>: the game's memory (its first 64 MB,
 * unreadable pages as zeros) written once, that long after the process
 * started. With BTVS_NATIVE_LOG=t<seconds> it shows which textures a frame
 * drew. */
static void ram_dump(void)
{
    static int state;
    static double at;
    static char path[MAX_PATH];
    static const uint8_t zero[0x1000];
    FILE *f;
    uint32_t va;
    if (state == 0) {
        const char *e = getenv("BTVS_GLYPH_RAMDUMP"), *c = e ? strchr(e, ',') : NULL;
        state = 2;
        if (c) {
            at = atof(e);
            strcpy_s(path, sizeof path, c + 1);
            state = 1;
        }
    }
    if (state != 1 || btvs_frame_seconds() < at)
        return;
    state = 2;
    if (fopen_s(&f, path, "wb") || !f)
        return;
    for (va = 0; va < 0x04000000u; va += 0x1000u)
        fwrite(readable(va, 0x1000u) ? (const void *)XBOX_PTR(va) : (const void *)zero, 1, 0x1000u, f);
    fclose(f);
    IN_LOG("glyphs: memory written to %s at %.2f s", path, btvs_frame_seconds());
}

/* ── the tips' sentences, with keyboard and mouse ────────────────────────── */

/* The tips are sentences from a text table in the level's files, held in
 * memory as strings ("Pull the \l trigger to reset": \l is where a picture
 * goes). With keyboard and mouse the pictures are keys, and "trigger",
 * "thumbstick" and "button" no longer fit, so the sentences are reworded in
 * memory, never longer than they were (the rest is filled with spaces at the
 * end). The game's files are not touched. */
#define G_TIP_WINDOW   0x007F18B8u      /* the tip window (a script object) */
#define TIP_QUERY_SLOT 0x2C8u           /* its QueryTip(name) in the vtable */
#define TIP_TABLE      0x288u           /* its table of sentences, made at the first tip */

/* '?' in a rule is the picture's letter. */
static const struct { const char *from, *to, *to_mouse; } k_rule[] = {
    { "Pull and hold the \\? trigger", "Hold \\?", NULL },   { "pull and hold the \\? trigger", "hold \\?", NULL },
    { "Pull and hold \\? trigger", "Hold \\?", NULL },       { "pull and hold \\? trigger", "hold \\?", NULL },
    { "Pull the \\? trigger", "Press \\?", NULL },           { "pull the \\? trigger", "press \\?", NULL },
    { "ress the \\? button", "ress \\?", NULL },
    { "Move the \\? thumbstick", "Use \\?", "Move the \\?" }, { "move the \\? thumbstick", "use \\?", "move the \\?" },
    { "Move \\? thumbstick", "Use \\?", "Move the \\?" },     { "move \\? thumbstick", "use \\?", "move the \\?" },
};

/* 1 when the sentence was changed. */
int btvs_input__glyphs_fit_text(char *s, int mouse_camera)
{
    char out[512];
    size_t len = strlen(s), i = 0, o = 0;
    int r, changed = 0;
    if (len >= sizeof out)
        return 0;
    while (i < len) {
        for (r = 0; r < (int)(sizeof k_rule / sizeof k_rule[0]); r++) {
            const char *f = k_rule[r].from, *t;
            size_t k = 0;
            char letter = 0;
            while (f[k] && i + k < len && (f[k] == s[i + k] || (f[k] == '?' && k && f[k - 1] == '\\'))) {
                if (f[k] == '?')
                    letter = s[i + k];
                k++;
            }
            if (f[k])
                continue;
            t = k_rule[r].to_mouse && mouse_camera && letter == ']' ? k_rule[r].to_mouse : k_rule[r].to;
            for (; *t; t++)
                out[o++] = *t == '?' ? letter : *t;
            i += k;
            changed = 1;
            break;
        }
        if (r == (int)(sizeof k_rule / sizeof k_rule[0]))
            out[o++] = s[i++];
    }
    if (!changed || o > len)
        return 0;
    memset(out + o, ' ', len - o);
    memcpy(s, out, len);
    return 1;
}

static uint32_t s_fit_text[32];             /* the reworded sentences, for tips_show */
static char s_fit_name[32][48];
static int s_nfit;

static void tips_fit(void)
{
    static int done, delay = -1;
    uint32_t tw, table, b, node, n = 0, seen = 0;
    int guard;
    if (done || s_set != SET_KEYS)
        return;
    if (!readable(G_TIP_WINDOW, 4) || (tw = MEM32(G_TIP_WINDOW)) == 0 || !readable(tw, 0x2A0u)
            || !readable(MEM32(tw), TIP_QUERY_SLOT + 4u) || MEM32(MEM32(tw) + TIP_QUERY_SLOT) != 0x002377B0u)
        return;
    /* The game makes the table when a level first asks for a tip. (Making it
     * sooner from here does not work: the level's tips file is not found.) */
    table = MEM32(tw + TIP_TABLE);
    if (!table || !readable(table, 0xA8u))
        return;
    if (delay < 0) {                            /* BTVS_GLYPH_TIP_DELAY=<reads>: testing, the rewording that much later */
        const char *e = getenv("BTVS_GLYPH_TIP_DELAY");
        delay = e ? atoi(e) : 0;
    }
    if (delay-- > 0)
        return;
    done = 1;
    if (getenv("BTVS_GLYPH_NO_TIP_TEXT"))
        return;
    for (b = 0; b < 37u; b++)                   /* 37 chains of { sentence, name, next } */
        for (node = MEM32(table + 8u + b * 4u), guard = 0; node && readable(node, 12) && guard < 500; node = MEM32(node + 8u), guard++) {
            uint32_t str = MEM32(node), text;
            if (!str || !readable(str, 8) || (text = MEM32(str)) == 0 || !readable(text, 1))
                continue;
            seen++;
            if (memchr((const char *)XBOX_PTR(text), 0, 500)
                    && btvs_input__glyphs_fit_text((char *)XBOX_PTR(text), btvs_setting_int("controls", "mouse_look", 1) != 0)) {
                uint32_t key = MEM32(node + 4u);
                n++;
                if (s_nfit < 32) {
                    s_fit_text[s_nfit] = text;
                    s_fit_name[s_nfit][0] = 0;
                    if (key && readable(key, 48) && memchr((const char *)XBOX_PTR(key), 0, 48))
                        strcpy_s(s_fit_name[s_nfit], sizeof s_fit_name[0], (const char *)XBOX_PTR(key));
                    s_nfit++;
                }
            }
        }
    IN_LOG("button pictures: %u of the game's %u tips reworded for keyboard and mouse", n, seen);
    for (b = 0; b < (uint32_t)s_nfit; b++)
        IN_LOG("button pictures: tip %s: %s", s_fit_name[b], (const char *)XBOX_PTR(s_fit_text[b]));
}

/* BTVS_GLYPH_SHOW_TIPS=<first>,<every>[,<type>]: testing. The reworded tips
 * put on screen one after the other through the game's own ShowTip, the
 * first <first> seconds after the process started, the next every <every>
 * seconds, so that frames can be taken of each. */
static void tips_show(void)
{
    static int state, type, idx;
    static double next, every;
    union { float f; uint32_t u; } secs;
    uint32_t tw, saved;
    recomp_func_t fn;
    if (state == 0) {
        const char *e = getenv("BTVS_GLYPH_SHOW_TIPS");
        state = 2;
        if (e && sscanf_s(e, "%lf,%lf,%d", &next, &every, &type) >= 2)
            state = 1;
    }
    if (state != 1 || idx >= s_nfit || btvs_frame_seconds() < next)
        return;
    next += every;
    if (!readable(G_TIP_WINDOW, 4) || (tw = MEM32(G_TIP_WINDOW)) == 0 || (fn = recomp_lookup(0x00236EF0u)) == NULL)
        return;
    IN_LOG("glyphs: tip %d (%s) shown at %.2f s as type %d (the window's was %d)", idx, s_fit_name[idx], btvs_frame_seconds(),
           type, (int)MEM32(tw + 0x270u));
    secs.f = (float)every - 0.4f;
    saved = g_esp;                              /* ShowTip(text, owner, type, seconds), a thiscall */
    PUSH32(g_esp, secs.u);
    PUSH32(g_esp, (uint32_t)type);
    PUSH32(g_esp, 0);
    PUSH32(g_esp, s_fit_text[idx]);
    PUSH32(g_esp, 0);
    g_ecx = tw;
    fn();
    g_esp = saved;
    idx++;
}

/* Once per read of player 1's pad, game thread. */
void btvs_input__glyphs_poll(void)
{
    ram_dump();
    tips_fit();
    tips_show();
}
