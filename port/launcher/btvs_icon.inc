/* The game's real icon, made on the player's PC from the player's own disc.
 * Part of btvs_launcher.c (included after btvs_build.inc).
 *
 * The disc's Default.xbe carries the picture the Xbox dashboard showed for the
 * game ($$XTIMAGE, 128 x 128). Nothing of it is in a release: at install the
 * launcher reads it from the game folder, makes btvs_icon.ico there (256 down
 * to 16 px) and writes the same images into the installed buffy_tvs.exe as its
 * icon resource 1 -- the resource Explorer shows and both of the game's window
 * classes load, so the title bar, the taskbar and Alt+Tab follow. The exe as
 * shipped has the port's own drawn icon (port/icon), which is what stays if
 * any of this fails: nothing here can fail an install.
 *
 * The exe is changed in a copy beside it, which then takes its place (a virus
 * scanner may hold a just-written file: move_patiently). Only the resource
 * section changes: the code, the thread-local data the game DLL reads and the
 * debug record that ties the exe to its .pdb are the same bytes at the same
 * addresses (docs/icon.md has the check). */

#define ICON_FILE L"btvs_icon.ico"
static const int k_icon_sizes[] = { 16, 20, 24, 32, 40, 48, 64, 128, 256 };
#define ICON_COUNT ((int)_countof(k_icon_sizes))

/* ── resampling: 0xAARRGGBB, top row first ───────────────────────────────── */

/* Smaller: every new pixel is the average of the area it covers (exact for
 * 128 -> 64, 32, 16; fractional weights at the edges for 48, 40, 24, 20). */
static void icon_shrink(const uint32_t *src, int sn, uint32_t *dst, int dn)
{
    double k = (double)sn / dn;
    int x, y, sx, sy, c;
    for (y = 0; y < dn; y++)
        for (x = 0; x < dn; x++) {
            double y0 = y * k, y1 = (y + 1) * k, x0 = x * k, x1 = (x + 1) * k, sum[4] = { 0, 0, 0, 0 }, wsum = 0;
            for (sy = (int)y0; sy < sn && sy < y1; sy++) {
                double wy = (sy + 1 < y1 ? sy + 1 : y1) - (sy > y0 ? sy : y0);
                for (sx = (int)x0; sx < sn && sx < x1; sx++) {
                    double w = wy * ((sx + 1 < x1 ? sx + 1 : x1) - (sx > x0 ? sx : x0));
                    uint32_t p = src[sy * sn + sx];
                    for (c = 0; c < 4; c++)
                        sum[c] += w * ((p >> (c * 8)) & 255);
                    wsum += w;
                }
            }
            dst[y * dn + x] = 0;
            for (c = 0; c < 4; c++)
                dst[y * dn + x] |= (uint32_t)(sum[c] / wsum + 0.5) << (c * 8);
        }
}

static double icon_cubic(double t)      /* Catmull-Rom */
{
    t = t < 0 ? -t : t;
    if (t < 1) return 1.5 * t * t * t - 2.5 * t * t + 1;
    if (t < 2) return -0.5 * t * t * t + 2.5 * t * t - 4 * t + 2;
    return 0;
}

/* Larger (128 -> 256): bicubic. It cannot add detail the disc's picture does
 * not have; it keeps the edges from turning into blocks. */
static void icon_grow(const uint32_t *src, int sn, uint32_t *dst, int dn)
{
    double k = (double)sn / dn;
    int x, y, i, j, c;
    for (y = 0; y < dn; y++)
        for (x = 0; x < dn; x++) {
            double fy = (y + 0.5) * k - 0.5, fx = (x + 0.5) * k - 0.5, sum[4] = { 0, 0, 0, 0 };
            int iy = (int)floor(fy), ix = (int)floor(fx);
            for (j = -1; j <= 2; j++)
                for (i = -1; i <= 2; i++) {
                    int sy = iy + j, sx = ix + i;
                    double w = icon_cubic(fy - sy) * icon_cubic(fx - sx);
                    uint32_t p;
                    sy = sy < 0 ? 0 : sy >= sn ? sn - 1 : sy;
                    sx = sx < 0 ? 0 : sx >= sn ? sn - 1 : sx;
                    p = src[sy * sn + sx];
                    for (c = 0; c < 4; c++)
                        sum[c] += w * ((p >> (c * 8)) & 255);
                }
            dst[y * dn + x] = 0;
            for (c = 0; c < 4; c++) {
                double v = sum[c] < 0 ? 0 : sum[c] > 255 ? 255 : sum[c];
                dst[y * dn + x] |= (uint32_t)(v + 0.5) << (c * 8);
            }
        }
}

/* ── the images as an .ico stores them ───────────────────────────────────── */

static uint32_t icon_crc(uint32_t crc, const uint8_t *p, size_t n)
{
    static uint32_t tab[256];
    size_t i;
    int k;
    if (!tab[1])
        for (i = 0; i < 256; i++) {
            uint32_t c = (uint32_t)i;
            for (k = 0; k < 8; k++)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            tab[i] = c;
        }
    crc = ~crc;
    for (i = 0; i < n; i++)
        crc = tab[(crc ^ p[i]) & 255] ^ (crc >> 8);
    return ~crc;
}

static uint8_t *icon_chunk(uint8_t *o, const char *type, const uint8_t *data, uint32_t n)
{
    uint32_t crc;
    o[0] = (uint8_t)(n >> 24); o[1] = (uint8_t)(n >> 16); o[2] = (uint8_t)(n >> 8); o[3] = (uint8_t)n;
    memcpy(o + 4, type, 4);
    if (n)
        memcpy(o + 8, data, n);
    crc = icon_crc(0, o + 4, n + 4);
    o += 8 + n;
    o[0] = (uint8_t)(crc >> 24); o[1] = (uint8_t)(crc >> 16); o[2] = (uint8_t)(crc >> 8); o[3] = (uint8_t)crc;
    return o + 4;
}

/* A PNG with its pixels stored, not packed (the 256 px image of an .ico is a
 * PNG by Windows' convention; packing would need a compressor here). */
static uint8_t *icon_png(const uint32_t *px, int n, uint32_t *size)
{
    uint32_t raw_n = (uint32_t)n * (1 + n * 4), z_n = 2 + raw_n + 5 * (raw_n / 65535 + 1) + 4, a = 1, b = 0, i, left;
    uint8_t *raw = malloc(raw_n), *z = malloc(z_n), *out = malloc(8 + 25 + 12 + z_n + 12), *o, *zp, ihdr[13];
    const uint8_t *rp;
    int x, y;
    if (!raw || !z || !out) {
        free(raw); free(z); free(out);
        return NULL;
    }
    for (y = 0; y < n; y++) {
        raw[y * (1 + n * 4)] = 0;
        for (x = 0; x < n; x++) {
            uint32_t p = px[y * n + x];
            uint8_t *q = raw + y * (1 + n * 4) + 1 + x * 4;
            q[0] = (uint8_t)(p >> 16); q[1] = (uint8_t)(p >> 8); q[2] = (uint8_t)p; q[3] = (uint8_t)(p >> 24);
        }
    }
    zp = z;
    *zp++ = 0x78; *zp++ = 0x01;
    for (rp = raw, left = raw_n; left; ) {
        uint32_t take = left > 65535 ? 65535 : left;
        *zp++ = left == take;
        *zp++ = (uint8_t)take; *zp++ = (uint8_t)(take >> 8);
        *zp++ = (uint8_t)~take; *zp++ = (uint8_t)(~take >> 8);
        memcpy(zp, rp, take);
        zp += take; rp += take; left -= take;
    }
    for (i = 0; i < raw_n; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    *zp++ = (uint8_t)(b >> 8); *zp++ = (uint8_t)b; *zp++ = (uint8_t)(a >> 8); *zp++ = (uint8_t)a;
    memcpy(out, "\x89PNG\r\n\x1a\n", 8);
    memset(ihdr, 0, sizeof ihdr);
    ihdr[2] = (uint8_t)(n >> 8); ihdr[3] = (uint8_t)n; ihdr[6] = (uint8_t)(n >> 8); ihdr[7] = (uint8_t)n;
    ihdr[8] = 8; ihdr[9] = 6;
    o = icon_chunk(out + 8, "IHDR", ihdr, 13);
    o = icon_chunk(o, "IDAT", z, (uint32_t)(zp - z));
    o = icon_chunk(o, "IEND", NULL, 0);
    *size = (uint32_t)(o - out);
    free(raw);
    free(z);
    return out;
}

/* A 32-bit bitmap as icons hold it: header, pixels bottom row first, an empty mask. */
static uint8_t *icon_dib(const uint32_t *px, int n, uint32_t *size)
{
    uint32_t mask_row = (uint32_t)((n + 31) / 32) * 4, total = 40 + (uint32_t)n * n * 4 + mask_row * n;
    uint8_t *out = calloc(1, total);
    BITMAPINFOHEADER bh;
    int y;
    if (!out)
        return NULL;
    memset(&bh, 0, sizeof bh);
    bh.biSize = 40; bh.biWidth = n; bh.biHeight = n * 2; bh.biPlanes = 1; bh.biBitCount = 32;
    bh.biSizeImage = total - 40;
    memcpy(out, &bh, 40);
    for (y = 0; y < n; y++)
        memcpy(out + 40 + (size_t)y * n * 4, px + (size_t)(n - 1 - y) * n, (size_t)n * 4);
    *size = total;
    return out;
}

typedef struct { uint8_t *data[ICON_COUNT]; uint32_t size[ICON_COUNT]; } IconSet;

static void icon_set_free(IconSet *s)
{
    int i;
    for (i = 0; i < ICON_COUNT; i++)
        free(s->data[i]);
    memset(s, 0, sizeof *s);
}

static int icon_set_make(const uint32_t *px128, IconSet *s)
{
    uint32_t *tmp = malloc(256 * 256 * 4);
    int i, ok = tmp != NULL;
    memset(s, 0, sizeof *s);
    for (i = 0; ok && i < ICON_COUNT; i++) {
        int n = k_icon_sizes[i];
        if (n == 128)
            memcpy(tmp, px128, 128 * 128 * 4);
        else if (n < 128)
            icon_shrink(px128, 128, tmp, n);
        else
            icon_grow(px128, 128, tmp, n);
        s->data[i] = n >= 256 ? icon_png(tmp, n, &s->size[i]) : icon_dib(tmp, n, &s->size[i]);
        ok = s->data[i] != NULL;
    }
    free(tmp);
    if (!ok)
        icon_set_free(s);
    return ok;
}

#pragma pack(push, 1)
typedef struct { uint8_t w, h, colors, zero; uint16_t planes, bpp; uint32_t size; } IconEntry;   /* then: offset (file) or id (resource) */
#pragma pack(pop)

static int icon_write_file(const WCHAR *path, const IconSet *s)
{
    WCHAR tmp[MAX_PATH];
    FILE *f = NULL;
    uint16_t head[3] = { 0, 1, ICON_COUNT };
    uint32_t off = 6 + 16 * ICON_COUNT;
    int i, ok;
    swprintf_s(tmp, MAX_PATH, L"%s.tmp", path);
    if (_wfopen_s(&f, tmp, L"wb") || !f)
        return 0;
    fwrite(head, 1, 6, f);
    for (i = 0; i < ICON_COUNT; i++) {
        IconEntry e = { (uint8_t)k_icon_sizes[i], (uint8_t)k_icon_sizes[i], 0, 0, 1, 32, s->size[i] };
        fwrite(&e, 1, 12, f);
        fwrite(&off, 1, 4, f);
        off += s->size[i];
    }
    for (i = 0; i < ICON_COUNT; i++)
        fwrite(s->data[i], 1, s->size[i], f);
    ok = !ferror(f);
    ok = !fclose(f) && ok;
    if (ok)
        ok = move_patiently(tmp, path, MOVEFILE_REPLACE_EXISTING);
    if (!ok)
        DeleteFileW(tmp);
    return ok;
}

/* ── into the installed exe ──────────────────────────────────────────────── */

typedef struct { WORD ids[64], langs[64]; int n; WORD group_lang; int have_group; } IconOld;

static BOOL CALLBACK icon_old_lang(HMODULE m, LPCWSTR type, LPCWSTR name, WORD lang, LONG_PTR p)
{
    IconOld *o = (IconOld *)p;
    (void)m;
    if (type == RT_GROUP_ICON) {
        if (IS_INTRESOURCE(name) && (WORD)(ULONG_PTR)name == 1) {
            o->group_lang = lang;
            o->have_group = 1;
        }
    } else if (IS_INTRESOURCE(name) && o->n < 64) {
        o->ids[o->n] = (WORD)(ULONG_PTR)name;
        o->langs[o->n++] = lang;
    }
    return TRUE;
}

static BOOL CALLBACK icon_old_name(HMODULE m, LPCWSTR type, LPWSTR name, LONG_PTR p)
{
    EnumResourceLanguagesW(m, type, name, icon_old_lang, p);
    return TRUE;
}

/* 1 = the exe now has these images as icon group 1; 0 = it is as it was. */
static int icon_into_exe(const WCHAR *exe, const IconSet *s)
{
    WCHAR tmp[MAX_PATH];
    IconOld old;
    HMODULE m;
    HANDLE h;
    uint8_t group[6 + 14 * ICON_COUNT];
    WORD lang;
    int i, ok = 1;

    swprintf_s(tmp, MAX_PATH, L"%s.icon.tmp", exe);
    if (!CopyFileW(exe, tmp, FALSE)) {
        blog(L"icon: could not copy %s (error %lu)", exe, GetLastError());
        return 0;
    }
    /* what the exe has now: the drawn icon's images, or an earlier install's */
    memset(&old, 0, sizeof old);
    m = LoadLibraryExW(tmp, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (m) {
        EnumResourceNamesW(m, RT_ICON, icon_old_name, (LONG_PTR)&old);
        EnumResourceLanguagesW(m, RT_GROUP_ICON, MAKEINTRESOURCEW(1), icon_old_lang, (LONG_PTR)&old);
        FreeLibrary(m);
    }
    lang = old.have_group ? old.group_lang : MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);

    h = BeginUpdateResourceW(tmp, FALSE);
    if (!h) {
        blog(L"icon: the exe's resources could not be opened (error %lu)", GetLastError());
        DeleteFileW(tmp);
        return 0;
    }
    for (i = 0; ok && i < old.n; i++)
        ok = UpdateResourceW(h, RT_ICON, MAKEINTRESOURCEW(old.ids[i]), old.langs[i], NULL, 0);
    memset(group, 0, sizeof group);
    group[2] = 1; group[4] = ICON_COUNT;
    for (i = 0; ok && i < ICON_COUNT; i++) {
        IconEntry e = { (uint8_t)k_icon_sizes[i], (uint8_t)k_icon_sizes[i], 0, 0, 1, 32, s->size[i] };
        WORD id = (WORD)(i + 1);
        memcpy(group + 6 + 14 * i, &e, 12);
        memcpy(group + 6 + 14 * i + 12, &id, 2);
        ok = UpdateResourceW(h, RT_ICON, MAKEINTRESOURCEW(id), lang, s->data[i], s->size[i]);
    }
    if (ok)
        ok = UpdateResourceW(h, RT_GROUP_ICON, MAKEINTRESOURCEW(1), lang, group, sizeof group);
    if (!ok)
        blog(L"icon: a resource could not be written (error %lu)", GetLastError());
    if (!EndUpdateResourceW(h, !ok) && ok) {
        blog(L"icon: the exe's resources could not be saved (error %lu)", GetLastError());
        ok = 0;
    }
    if (ok)
        ok = move_patiently(tmp, exe, MOVEFILE_REPLACE_EXISTING);     /* fails while the game runs: the exe stays as it was */
    if (!ok)
        DeleteFileW(tmp);
    return ok;
}

/* At install, and "Buffy Launcher.exe" --make-icon <game folder>: the icon
 * file and the exe's icon, from the game folder's own Default.xbe.
 * Returns 0 nothing done, 1 the .ico only, 2 the .ico and the exe. */
static int disc_icon(const WCHAR *target)
{
    static uint32_t px[128 * 128];
    WCHAR ico[MAX_PATH], exe[MAX_PATH];
    IconSet s;
    int done = 0, own_log = 0;
    if (!s_blog) {                      /* the install log, when the build did not have it open */
        join(ico, target, INSTALL_LOG);
        own_log = !_wfopen_s(&s_blog, ico, L"ab") && s_blog;
        if (!own_log)
            s_blog = NULL;
    }
    if (!icon_pixels(target, px)) {
        blog(L"icon: no title image in this disc's Default.xbe; the port's own icon stays");
        goto out;
    }
    if (!icon_set_make(px, &s))
        goto out;
    join(ico, target, ICON_FILE);
    join(exe, target, GAME_EXE);
    if (icon_write_file(ico, &s)) {
        done = 1;
        blog(L"icon: %s made from the disc's title image", ICON_FILE);
        if (file_exists(exe) && icon_into_exe(exe, &s)) {
            done = 2;
            blog(L"icon: written into %s", GAME_EXE);
            SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, exe, NULL);     /* Explorer: look at this file's icon again */
        }
    } else
        blog(L"icon: could not write %s", ico);
    icon_set_free(&s);
out:
    if (own_log) {
        fclose(s_blog);
        s_blog = NULL;
    }
    return done;
}
