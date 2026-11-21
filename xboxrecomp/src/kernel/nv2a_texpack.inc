/*
 * Texture packs: dumping and replacing the game's textures, as Dolphin and
 * PCSX2 do. Included by nv2a_pb_d3d11.inc; gpu_texture() calls in here.
 *
 *   <root>\dump\   each texture the game uploads, once, as a PNG (Dump on)
 *   <root>\load\   PNG / DDS replacements, any size, subfolders allowed (Load on)
 *
 * A texture is named buffy_<w>x<h>_<hash>_<fmt>.png. The hash is 64 bits of
 * the texture's level-0 contents (and palette), format and size -- not its
 * address -- so it is the same every run. Any file in load\ whose name holds
 * that 16-digit hash replaces the texture, whatever else the name says.
 * Replacements get a full mip chain (box filter) and are kept for the rest of
 * the run; Prefetch loads them all on a background thread at start-up.
 * nv2a_gpu_texture_pack_reload() reads load\ again while the title runs.
 * PNGs are written on a background thread, so dumping does not stall a frame.
 *
 * Cube maps are left alone.
 */
#if defined(_WIN32)
#include <wincodec.h>
#else
typedef struct IWICImagingFactory IWICImagingFactory;   /* (no WIC: Android has its own codecs below, others none) */
#define IWICImagingFactory_Release(f) ((void)(f), 0u)
#endif
#if defined(_MSC_VER)
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#endif

/* WIC GUIDs, local so no GUID library is needed. */
static const GUID tp_CLSID_WICImagingFactory =
    { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
static const GUID tp_IID_IWICImagingFactory =
    { 0xec5ec8a9, 0xc395, 0x4314, { 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70 } };
static const GUID tp_GUID_ContainerFormatPng =
    { 0x1b7cfaf4, 0x713f, 0x473c, { 0xbb, 0xcd, 0x61, 0x37, 0x42, 0x5f, 0xae, 0xaf } };
static const GUID tp_GUID_WICPixelFormat32bppBGRA =
    { 0x6fddc324, 0x4e03, 0x4bfe, { 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0f } };

typedef struct {
    uint64_t hash;
    WCHAR   *path;
    GpuTexture *tex;
    int      failed;
} TpEntry;

typedef struct TpDump {
    struct TpDump *next;
    uint64_t hash;
    uint32_t w, h, fmt;
    uint32_t *px;
} TpDump;

static struct {
    WCHAR    root[MAX_PATH];
    int      dump, load, prefetch;
    int      inited;
    TpEntry *ent;                /* open addressing by hash; built at init and by a reload */
    uint32_t cap, n;
    int      lock_made;
    HANDLE   prefetch_thread;    /* start-up's, while a reload may have to wait for it */
    /* nv2a_gpu_texture_pack_reload: */
    WCHAR    r_root[MAX_PATH];   /* what the reload under way reads (under lock) */
    int      r_load;
    uint32_t r_gen;              /* counts the calls */
    int      r_running;          /* the reload thread is alive */
    TpEntry *next;               /* the table it made, textures loaded, waiting for the render thread */
    uint32_t next_cap, next_n;
    volatile LONG pending;       /* ... is there */
    double   r_ms;
    uint64_t *dumped;            /* hashes already in dump\ (render thread only) */
    uint32_t dcap, dn;
    CRITICAL_SECTION lock;       /* entry textures, the dump queue */
    TpDump  *qhead, *qtail;
    HANDLE   qevent;
    uint32_t replaced, dumped_run, loaded;
} s_tp;

/* Called by the game before the first frame. root: the textures_replacement
 * folder. Env: BUFFY_TEX_DUMP / BUFFY_TEX_LOAD / BUFFY_TEX_PREFETCH = 0|1 and
 * BUFFY_TEX_DIR override. */
void nv2a_gpu_set_texture_pack(const WCHAR *root, int dump, int load, int prefetch)
{
    const char *e;
    wcscpy_s(s_tp.root, MAX_PATH, root ? root : L"");
    if ((e = getenv("BUFFY_TEX_DIR")) != NULL)
        MultiByteToWideChar(CP_ACP, 0, e, -1, s_tp.root, MAX_PATH);
    s_tp.dump = (e = getenv("BUFFY_TEX_DUMP")) != NULL ? atoi(e) != 0 : dump;
    s_tp.load = (e = getenv("BUFFY_TEX_LOAD")) != NULL ? atoi(e) != 0 : load;
    s_tp.prefetch = (e = getenv("BUFFY_TEX_PREFETCH")) != NULL ? atoi(e) != 0 : prefetch;
    if (!s_tp.root[0])
        s_tp.dump = s_tp.load = 0;
}

static int tp_active(void) { return s_tp.dump || s_tp.load; }

/* ── hashing ── */

static uint64_t tp_mix(const uint8_t *p, size_t n, uint64_t h)
{
    const uint64_t P = 0x9E3779B97F4A7C15ull;
    while (n >= 8) {
        uint64_t v;
        memcpy(&v, p, 8);
        h ^= v * 0xC2B2AE3D27D4EB4Full;
        h = ((h << 31) | (h >> 33)) * P;
        p += 8; n -= 8;
    }
    while (n--) {
        h ^= *p++ * 0x165667B19E3779F9ull;
        h = ((h << 11) | (h >> 53)) * P;
    }
    return h;
}

static uint64_t tp_fmix(uint64_t h)
{
    h ^= h >> 33; h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h;
}

/* The texture's identity: format, size, level 0 (only the pixels of each row
 * of a linear texture, not its pitch padding) and palette. */
static uint64_t tp_texture_hash(const uint8_t *lvl0, uint32_t fmt, uint32_t block, uint32_t w, uint32_t h,
                                int linear, uint32_t pitch, const uint32_t *pal, uint32_t npal)
{
    uint64_t k = tp_fmix(((uint64_t)fmt << 48) ^ ((uint64_t)w << 24) ^ h ^ 0x42554646ull);
    if (linear) {
        uint32_t y, row = w * tx_bpp(fmt);
        for (y = 0; y < h; y++)
            k = tp_mix(lvl0 + (size_t)y * pitch, row, k);
    } else {
        k = tp_mix(lvl0, tx_level_bytes(fmt, block, w, h), k);
    }
    if (pal && npal)
        k = tp_mix((const uint8_t *)pal, npal * 4, k);
    return tp_fmix(k);
}

/* DDS replacements, read here on every system: Windows' own DDS codec takes
 * only the compressed ones, Android has none. */
/* One of a pixel's channels by its DDS bit mask, as 0..255 (no mask: 255). */
static uint32_t tp_channel(uint32_t v, uint32_t mask)
{
    uint32_t shift = 0;
    if (!mask)
        return 255u;
    while (!((mask >> shift) & 1))
        shift++;
    return (uint32_t)((uint64_t)((v & mask) >> shift) * 255 / (mask >> shift));
}

/* One BC1-3 4x4 block's colours (BGRA), BC1's alpha with them. */
static void tp_bc_color(const uint8_t *b, uint32_t out[16], int bc1)
{
    uint32_t c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8;
    uint32_t bits = (uint32_t)b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24;
    uint32_t r[4], g[4], bl[4], a[4];
    int i;
    r[0] = (c0 >> 11) * 255 / 31; g[0] = ((c0 >> 5) & 63) * 255 / 63; bl[0] = (c0 & 31) * 255 / 31;
    r[1] = (c1 >> 11) * 255 / 31; g[1] = ((c1 >> 5) & 63) * 255 / 63; bl[1] = (c1 & 31) * 255 / 31;
    a[0] = a[1] = a[2] = a[3] = 255;
    if (c0 > c1 || !bc1) {
        r[2] = (2 * r[0] + r[1]) / 3; g[2] = (2 * g[0] + g[1]) / 3; bl[2] = (2 * bl[0] + bl[1]) / 3;
        r[3] = (r[0] + 2 * r[1]) / 3; g[3] = (g[0] + 2 * g[1]) / 3; bl[3] = (bl[0] + 2 * bl[1]) / 3;
    } else {
        r[2] = (r[0] + r[1]) / 2; g[2] = (g[0] + g[1]) / 2; bl[2] = (bl[0] + bl[1]) / 2;
        r[3] = g[3] = bl[3] = 0; a[3] = 0;
    }
    for (i = 0; i < 16; i++) {
        uint32_t k = (bits >> (i * 2)) & 3;
        out[i] = a[k] << 24 | r[k] << 16 | g[k] << 8 | bl[k];
    }
}

/* A DDS file's top level as BGRA, or NULL: DXT1/2/3/4/5 or 32-bit RGB(A). */
static uint32_t *tp_read_dds(const uint8_t *d, size_t n, uint32_t *w, uint32_t *h)
{
    uint32_t iw, ih, pfflags, fourcc, bits, rm, gm, bm, am, x, y;
    uint32_t *px;
    const uint8_t *s = d + 128;
    if (n < 128 || memcmp(d, "DDS ", 4))
        return NULL;
    ih = d[12] | d[13] << 8 | d[14] << 16 | (uint32_t)d[15] << 24;
    iw = d[16] | d[17] << 8 | d[18] << 16 | (uint32_t)d[19] << 24;
    pfflags = d[80] | d[81] << 8 | d[82] << 16 | (uint32_t)d[83] << 24;
    fourcc = d[84] | d[85] << 8 | d[86] << 16 | (uint32_t)d[87] << 24;
    bits = d[88] | d[89] << 8;
    rm = d[92] | d[93] << 8 | d[94] << 16 | (uint32_t)d[95] << 24;
    gm = d[96] | d[97] << 8 | d[98] << 16 | (uint32_t)d[99] << 24;
    bm = d[100] | d[101] << 8 | d[102] << 16 | (uint32_t)d[103] << 24;
    am = d[104] | d[105] << 8 | d[106] << 16 | (uint32_t)d[107] << 24;
    if (!iw || !ih || iw > 16384 || ih > 16384 || !(px = (uint32_t *)malloc((size_t)iw * ih * 4)))
        return NULL;
    if (pfflags & 4) {                                  /* DDPF_FOURCC: BC */
        int kind = fourcc == 0x31545844u ? 1 : (fourcc == 0x32545844u || fourcc == 0x33545844u) ? 2
                 : (fourcc == 0x34545844u || fourcc == 0x35545844u) ? 3 : 0;
        uint32_t bb = kind == 1 ? 8 : 16, bw = (iw + 3) / 4, bh = (ih + 3) / 4;
        if (!kind || 128 + (size_t)bw * bh * bb > n) {
            free(px);
            return NULL;
        }
        for (y = 0; y < bh; y++)
            for (x = 0; x < bw; x++) {
                const uint8_t *b = s + ((size_t)y * bw + x) * bb;
                uint32_t c[16];
                int i;
                tp_bc_color(kind == 1 ? b : b + 8, c, kind == 1);
                if (kind == 2) {
                    for (i = 0; i < 16; i++)
                        c[i] = (c[i] & 0xFFFFFFu) | (uint32_t)(((b[i / 2] >> ((i & 1) * 4)) & 15) * 17) << 24;
                } else if (kind == 3) {
                    uint32_t a[8], k;
                    uint64_t ab = 0;
                    a[0] = b[0]; a[1] = b[1];
                    for (k = 0; k < 6; k++)
                        ab |= (uint64_t)b[2 + k] << (8 * k);
                    if (a[0] > a[1])
                        for (k = 1; k < 7; k++) a[k + 1] = ((7 - k) * a[0] + k * a[1]) / 7;
                    else {
                        for (k = 1; k < 5; k++) a[k + 1] = ((5 - k) * a[0] + k * a[1]) / 5;
                        a[6] = 0; a[7] = 255;
                    }
                    for (i = 0; i < 16; i++)
                        c[i] = (c[i] & 0xFFFFFFu) | a[(ab >> (3 * i)) & 7] << 24;
                }
                for (i = 0; i < 16; i++) {
                    uint32_t px_x = x * 4 + (i & 3), px_y = y * 4 + (i >> 2);
                    if (px_x < iw && px_y < ih)
                        px[(size_t)px_y * iw + px_x] = c[i];
                }
            }
    } else if ((pfflags & 0x40) && bits == 32 && 128 + (size_t)iw * ih * 4 <= n) {   /* DDPF_RGB */
        size_t i;
        for (i = 0; i < (size_t)iw * ih; i++) {
            uint32_t v = s[i * 4] | s[i * 4 + 1] << 8 | s[i * 4 + 2] << 16 | (uint32_t)s[i * 4 + 3] << 24;
            px[i] = tp_channel(v, am) << 24 | tp_channel(v, rm) << 16 | tp_channel(v, gm) << 8 | tp_channel(v, bm);
        }
        if (!(pfflags & 1) || !am)                      /* (no alpha: opaque) */
            for (i = 0; i < (size_t)iw * ih; i++)
                px[i] |= 0xFF000000u;
    } else {
        free(px);
        return NULL;
    }
    *w = iw;
    *h = ih;
    return px;
}

/* A .dds file (open, read to its end) as BGRA, or NULL. */
static uint32_t *tp_read_dds_file(FILE *in, uint32_t *w, uint32_t *h)
{
    uint32_t *px = NULL;
    uint8_t *d;
    long n;
    fseek(in, 0, SEEK_END);
    n = ftell(in);
    fseek(in, 0, SEEK_SET);
    if (n > 0 && n < (256L << 20) && (d = (uint8_t *)malloc((size_t)n)) != NULL) {
        if (fread(d, 1, (size_t)n, in) == (size_t)n)
            px = tp_read_dds(d, (size_t)n, w, h);
        free(d);
    }
    return px;
}

#if defined(_WIN32)
/* ── WIC ── */

static IWICImagingFactory *tp_wic(void)
{
    IWICImagingFactory *f = NULL;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);     /* S_FALSE / RPC_E_CHANGED_MODE are fine */
    if (FAILED(CoCreateInstance(&tp_CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                &tp_IID_IWICImagingFactory, (void **)&f)))
        return NULL;
    return f;
}

static int tp_write_png(IWICImagingFactory *f, const WCHAR *path, const uint32_t *px, uint32_t w, uint32_t h)
{
    IWICStream *s = NULL;
    IWICBitmapEncoder *e = NULL;
    IWICBitmapFrameEncode *fr = NULL;
    IPropertyBag2 *pb = NULL;
    WICPixelFormatGUID pf = tp_GUID_WICPixelFormat32bppBGRA;
    int ok = 0;
    if (FAILED(IWICImagingFactory_CreateStream(f, &s)) ||
        FAILED(IWICStream_InitializeFromFilename(s, path, GENERIC_WRITE)) ||
        FAILED(IWICImagingFactory_CreateEncoder(f, &tp_GUID_ContainerFormatPng, NULL, &e)) ||
        FAILED(IWICBitmapEncoder_Initialize(e, (IStream *)s, WICBitmapEncoderNoCache)) ||
        FAILED(IWICBitmapEncoder_CreateNewFrame(e, &fr, &pb)) ||
        FAILED(IWICBitmapFrameEncode_Initialize(fr, pb)) ||
        FAILED(IWICBitmapFrameEncode_SetSize(fr, w, h)) ||
        FAILED(IWICBitmapFrameEncode_SetPixelFormat(fr, &pf)))
        goto done;
    if (memcmp(&pf, &tp_GUID_WICPixelFormat32bppBGRA, sizeof pf))
        goto done;
    if (FAILED(IWICBitmapFrameEncode_WritePixels(fr, h, w * 4, w * 4 * h, (BYTE *)px)) ||
        FAILED(IWICBitmapFrameEncode_Commit(fr)) ||
        FAILED(IWICBitmapEncoder_Commit(e)))
        goto done;
    ok = 1;
done:
    if (pb) IPropertyBag2_Release(pb);
    if (fr) IWICBitmapFrameEncode_Release(fr);
    if (e) IWICBitmapEncoder_Release(e);
    if (s) IWICStream_Release(s);
    if (!ok)
        DeleteFileW(path);
    return ok;
}

/* An image file as 32-bit BGRA (malloc'd), or NULL. */
static uint32_t *tp_read_image(IWICImagingFactory *f, const WCHAR *path, uint32_t *w, uint32_t *h)
{
    IWICBitmapDecoder *d = NULL;
    IWICBitmapFrameDecode *fr = NULL;
    IWICFormatConverter *cv = NULL;
    uint32_t *px = NULL;
    UINT iw = 0, ih = 0;
    if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(f, path, NULL, GENERIC_READ,
                                                            WICDecodeMetadataCacheOnDemand, &d)) ||
        FAILED(IWICBitmapDecoder_GetFrame(d, 0, &fr)) ||
        FAILED(IWICImagingFactory_CreateFormatConverter(f, &cv)) ||
        FAILED(IWICFormatConverter_Initialize(cv, (IWICBitmapSource *)fr, &tp_GUID_WICPixelFormat32bppBGRA,
                                              WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom)) ||
        FAILED(IWICFormatConverter_GetSize(cv, &iw, &ih)) ||
        !iw || !ih || iw > 16384 || ih > 16384)
        goto done;
    px = (uint32_t *)malloc((size_t)iw * ih * 4);
    if (px && FAILED(IWICFormatConverter_CopyPixels(cv, NULL, iw * 4, iw * 4 * ih, (BYTE *)px))) {
        free(px);
        px = NULL;
    }
    *w = iw; *h = ih;
done:
    if (cv) IWICFormatConverter_Release(cv);
    if (fr) IWICBitmapFrameDecode_Release(fr);
    if (d) IWICBitmapDecoder_Release(d);
    if (!px) {
        /* a DDS WIC does not take (uncompressed): read here */
        const WCHAR *ext = wcsrchr(path, L'.');
        FILE *in = NULL;
        if (ext && !_wcsicmp(ext, L".dds") && !_wfopen_s(&in, path, L"rb") && in) {
            px = tp_read_dds_file(in, w, h);
            fclose(in);
        }
    }
    return px;
}

#elif defined(BUFFY_ANDROID)
/* Android: the NDK's image codecs (libjnigraphics) -- AImageDecoder reads
 * PNG (and JPEG / WebP), AndroidBitmap_compress writes PNG -- and DDS here
 * (BC1-3 and uncompressed 32-bit, level 0), which Android has no codec for.
 * No factory to make: tp_wic gives a token the callers only test. */
#include <android/bitmap.h>
#include <android/data_space.h>
#include <android/imagedecoder.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

void android_path_from_wide(const wchar_t *in, char *out, size_t n);   /* port/android/src/android_win32.c */

static IWICImagingFactory *tp_wic(void) { return (IWICImagingFactory *)(uintptr_t)1; }

/* RGBA <-> BGRA (the module's pixels are BGRA, as WIC's 32bppBGRA) */
static void tp_swap_rb(uint32_t *px, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        uint32_t v = px[i];
        px[i] = (v & 0xFF00FF00u) | ((v & 0xFFu) << 16) | ((v >> 16) & 0xFFu);
    }
}

static bool tp_png_out(void *user, const void *data, size_t size)
{
    return fwrite(data, 1, size, (FILE *)user) == size;
}

static int tp_write_png(IWICImagingFactory *f, const WCHAR *path, const uint32_t *px, uint32_t w, uint32_t h)
{
    char p[1024];
    AndroidBitmapInfo info;
    uint32_t *rgba;
    FILE *out;
    int ok;
    (void)f;
    android_path_from_wide(path, p, sizeof p);
    if (!(rgba = (uint32_t *)malloc((size_t)w * h * 4)))
        return 0;
    memcpy(rgba, px, (size_t)w * h * 4);
    tp_swap_rb(rgba, (size_t)w * h);
    if (!(out = fopen(p, "wb"))) {
        free(rgba);
        return 0;
    }
    memset(&info, 0, sizeof info);
    info.width = w;
    info.height = h;
    info.stride = w * 4;
    info.format = ANDROID_BITMAP_FORMAT_RGBA_8888;
    info.flags = ANDROID_BITMAP_FLAGS_ALPHA_UNPREMUL;
    ok = AndroidBitmap_compress(&info, ADATASPACE_SRGB, rgba, ANDROID_BITMAP_COMPRESS_FORMAT_PNG, 100, out,
                                tp_png_out) == ANDROID_BITMAP_RESULT_SUCCESS;
    fclose(out);
    free(rgba);
    if (!ok)
        unlink(p);
    return ok;
}

static uint32_t *tp_read_image(IWICImagingFactory *f, const WCHAR *path, uint32_t *w, uint32_t *h)
{
    char p[1024];
    const char *ext;
    AImageDecoder *dec = NULL;
    const AImageDecoderHeaderInfo *hi;
    uint32_t *px = NULL;
    int fd;
    (void)f;
    android_path_from_wide(path, p, sizeof p);
    ext = strrchr(p, '.');
    if (ext && !strcasecmp(ext, ".dds")) {
        FILE *in = fopen(p, "rb");
        if (!in) {
            fprintf(stderr, "  [TEXPACK] %s: can't open (errno %d)\n", p, errno);
            return NULL;
        }
        px = tp_read_dds_file(in, w, h);
        fclose(in);
        if (!px)
            fprintf(stderr, "  [TEXPACK] %s: not a DDS read here (BC1-3 or 32-bit only)\n", p);
        return px;
    }
    if ((fd = open(p, O_RDONLY)) < 0) {
        fprintf(stderr, "  [TEXPACK] %s: can't open (errno %d)\n", p, errno);
        return NULL;
    }
    {
        int r = AImageDecoder_createFromFd(fd, &dec);
        if (r != ANDROID_IMAGE_DECODER_SUCCESS)
            fprintf(stderr, "  [TEXPACK] %s: not an image Android reads (%d)\n", p, r);
    }
    if (dec) {
        int32_t iw, ih;
        size_t stride;
        hi = AImageDecoder_getHeaderInfo(dec);
        iw = AImageDecoderHeaderInfo_getWidth(hi);
        ih = AImageDecoderHeaderInfo_getHeight(hi);
        AImageDecoder_setAndroidBitmapFormat(dec, ANDROID_BITMAP_FORMAT_RGBA_8888);
        AImageDecoder_setUnpremultipliedRequired(dec, true);
        stride = AImageDecoder_getMinimumStride(dec);
        if (iw > 0 && ih > 0 && iw <= 16384 && ih <= 16384 && stride == (size_t)iw * 4
                && (px = (uint32_t *)malloc(stride * ih)) != NULL) {
            if (AImageDecoder_decodeImage(dec, px, stride, stride * ih) == ANDROID_IMAGE_DECODER_SUCCESS) {
                tp_swap_rb(px, (size_t)iw * ih);
                *w = (uint32_t)iw;
                *h = (uint32_t)ih;
            } else {
                free(px);
                px = NULL;
            }
        }
        AImageDecoder_delete(dec);
    }
    close(fd);
    return px;
}

#else
static IWICImagingFactory *tp_wic(void) { return NULL; }
static int tp_write_png(IWICImagingFactory *f, const WCHAR *path, const uint32_t *px, uint32_t w, uint32_t h)
{ (void)f; (void)path; (void)px; (void)w; (void)h; return 0; }
static uint32_t *tp_read_image(IWICImagingFactory *f, const WCHAR *path, uint32_t *w, uint32_t *h)
{ (void)f; (void)path; (void)w; (void)h; return NULL; }
#endif

/* ── replacements ── */

/* A file name's 16-hex-digit token, or 0. */
static uint64_t tp_name_hash(const WCHAR *name)
{
    const WCHAR *p = name;
    while (*p) {
        const WCHAR *q = p;
        uint64_t v = 0;
        int n = 0;
        while (iswxdigit(*q) && n < 17) {
            v = (v << 4) | (uint64_t)(iswdigit(*q) ? *q - L'0' : (towlower(*q) - L'a' + 10));
            q++; n++;
        }
        if (n == 16 && (*q == L'_' || *q == L'.' || *q == 0) && (p == name || p[-1] == L'_'))
            return v;
        p = q > p ? q : p + 1;
    }
    return 0;
}

/* A table of replacements: the one in use (s_tp.ent) or the one a reload is making. */
typedef struct { TpEntry *ent; uint32_t cap, n; } TpTable;

static TpEntry *tp_find_in(TpEntry *ent, uint32_t cap, uint64_t hash)
{
    uint32_t i;
    if (!cap)
        return NULL;
    for (i = (uint32_t)hash & (cap - 1); ent[i].path; i = (i + 1) & (cap - 1))
        if (ent[i].hash == hash)
            return &ent[i];
    return NULL;
}

static TpEntry *tp_find(uint64_t hash)
{
    return tp_find_in(s_tp.ent, s_tp.cap, hash);
}

static void tp_add(TpTable *tb, uint64_t hash, const WCHAR *path)
{
    uint32_t i;
    if (tb->n * 2 >= tb->cap) {
        TpEntry *old = tb->ent;
        uint32_t oc = tb->cap, k;
        tb->cap = oc ? oc * 2 : 1024;
        tb->ent = (TpEntry *)calloc(tb->cap, sizeof(TpEntry));
        tb->n = 0;
        for (k = 0; k < oc; k++)
            if (old[k].path) {
                for (i = (uint32_t)old[k].hash & (tb->cap - 1); tb->ent[i].path; i = (i + 1) & (tb->cap - 1)) ;
                tb->ent[i] = old[k];
                tb->n++;
            }
        free(old);
    }
    if (tp_find_in(tb->ent, tb->cap, hash))
        return;                                /* first one found wins */
    for (i = (uint32_t)hash & (tb->cap - 1); tb->ent[i].path; i = (i + 1) & (tb->cap - 1)) ;
    tb->ent[i].hash = hash;
    tb->ent[i].path = _wcsdup(path);
    tb->n++;
}

static void tp_scan(TpTable *tb, const WCHAR *dir, int depth)
{
    WCHAR pat[MAX_PATH], p[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    swprintf_s(pat, MAX_PATH, L"%s\\*", dir);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        const WCHAR *ext;
        uint64_t hash;
        if (fd.cFileName[0] == L'.')
            continue;
        swprintf_s(p, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth < 8)
                tp_scan(tb, p, depth + 1);
            continue;
        }
        ext = wcsrchr(fd.cFileName, L'.');
        if (!ext || (_wcsicmp(ext, L".png") && _wcsicmp(ext, L".dds")))
            continue;
        if ((hash = tp_name_hash(fd.cFileName)) != 0)
            tp_add(tb, hash, p);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* BGRA image -> immutable texture with a full box-filtered mip chain. */
static int tp_make_texture(uint32_t *px, uint32_t w, uint32_t h, GpuTexture **tex)
{
    GpuTextureDesc td;
    GpuSubresource sub[15];
    uint32_t *lv[15];
    uint32_t levels = 1, l, lw = w, lh = h;
    int ok = 0;
    while ((lw > 1 || lh > 1) && levels < 15) {
        lw = lw > 1 ? lw / 2 : 1;
        lh = lh > 1 ? lh / 2 : 1;
        levels++;
    }
    lv[0] = px;
    lw = w; lh = h;
    for (l = 1; l < levels; l++) {
        uint32_t nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1, x, y;
        const uint32_t *s = lv[l - 1];
        uint32_t *d = (uint32_t *)malloc((size_t)nw * nh * 4);
        if (!d) { levels = l; break; }
        for (y = 0; y < nh; y++)
            for (x = 0; x < nw; x++) {
                uint32_t x0 = x * 2 < lw ? x * 2 : lw - 1, x1 = x * 2 + 1 < lw ? x * 2 + 1 : x0;
                uint32_t y0 = y * 2 < lh ? y * 2 : lh - 1, y1 = y * 2 + 1 < lh ? y * 2 + 1 : y0;
                uint32_t a = s[y0 * lw + x0], b = s[y0 * lw + x1], c = s[y1 * lw + x0], e = s[y1 * lw + x1];
                uint32_t out = 0, sh;
                for (sh = 0; sh < 32; sh += 8)
                    out |= ((((a >> sh) & 255) + ((b >> sh) & 255) + ((c >> sh) & 255) + ((e >> sh) & 255) + 2) / 4) << sh;
                d[y * nw + x] = out;
            }
        lv[l] = d;
        lw = nw; lh = nh;
    }
    memset(&td, 0, sizeof td);
    td.width = w; td.height = h; td.mips = levels;
    td.format = GPU_FMT_BGRA8;
    td.flags = GPU_TEX_SAMPLED;
    lw = w;
    for (l = 0; l < levels; l++) {
        sub[l].data = lv[l];
        sub[l].pitch = lw * 4;
        lw = lw > 1 ? lw / 2 : 1;
    }
    ok = (*tex = gpu_texture_create(&td, sub)) != NULL;
    for (l = 1; l < levels; l++)
        free(lv[l]);
    return ok;
}

/* Decode and upload one entry (any thread; the device is free-threaded). */
static void tp_load_entry(IWICImagingFactory *f, TpEntry *e)
{
    uint32_t w = 0, h = 0, *px;
    GpuTexture *tex = NULL;
    int ok = 0;
    px = f ? tp_read_image(f, e->path, &w, &h) : NULL;
    if (px) {
        ok = tp_make_texture(px, w, h, &tex);
        free(px);
    }
    EnterCriticalSection(&s_tp.lock);
    if (e->tex || e->failed) {
        if (tex) gpu_texture_release(tex);
    } else if (ok) {
        e->tex = tex;
        s_tp.loaded++;
    } else {
        e->failed = 1;
        fwprintf(stderr, L"  [TEXPACK] could not load %s\n", e->path);
    }
    LeaveCriticalSection(&s_tp.lock);
}

static DWORD WINAPI tp_prefetch_thread(LPVOID arg)
{
    IWICImagingFactory *f = tp_wic();
    uint32_t i;
    DWORD t0 = GetTickCount();
    (void)arg;
    for (i = 0; i < s_tp.cap; i++)
        if (s_tp.ent[i].path && !s_tp.ent[i].tex && !s_tp.ent[i].failed)
            tp_load_entry(f, &s_tp.ent[i]);
    fprintf(stderr, "  [TEXPACK] prefetched %u replacements in %lu ms\n", s_tp.loaded, GetTickCount() - t0);
    if (f) IWICImagingFactory_Release(f);
    return 0;
}

/* ── dumping ── */

static int tp_dumped(uint64_t hash, int add)
{
    uint32_t i;
    if (s_tp.dcap) {
        for (i = (uint32_t)hash & (s_tp.dcap - 1); s_tp.dumped[i]; i = (i + 1) & (s_tp.dcap - 1))
            if (s_tp.dumped[i] == hash)
                return 1;
    }
    if (!add)
        return 0;
    if ((s_tp.dn + 1) * 2 >= s_tp.dcap) {
        uint64_t *old = s_tp.dumped;
        uint32_t oc = s_tp.dcap, k;
        s_tp.dcap = oc ? oc * 2 : 4096;
        s_tp.dumped = (uint64_t *)calloc(s_tp.dcap, sizeof(uint64_t));
        for (k = 0; k < oc; k++)
            if (old[k]) {
                for (i = (uint32_t)old[k] & (s_tp.dcap - 1); s_tp.dumped[i]; i = (i + 1) & (s_tp.dcap - 1)) ;
                s_tp.dumped[i] = old[k];
            }
        free(old);
    }
    for (i = (uint32_t)hash & (s_tp.dcap - 1); s_tp.dumped[i]; i = (i + 1) & (s_tp.dcap - 1)) ;
    s_tp.dumped[i] = hash;
    s_tp.dn++;
    return 0;
}

static void tp_scan_dumped(const WCHAR *dir)
{
    WCHAR pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    swprintf_s(pat, MAX_PATH, L"%s\\*.png", dir);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        uint64_t hash = tp_name_hash(fd.cFileName);
        if (hash)
            tp_dumped(hash, 1);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static DWORD WINAPI tp_dump_thread(LPVOID arg)
{
    IWICImagingFactory *f = tp_wic();
    (void)arg;
    for (;;) {
        TpDump *d;
        WaitForSingleObject(s_tp.qevent, INFINITE);
        for (;;) {
            WCHAR path[MAX_PATH];
            EnterCriticalSection(&s_tp.lock);
            d = s_tp.qhead;
            if (d) {
                s_tp.qhead = d->next;
                if (!s_tp.qhead) s_tp.qtail = NULL;
            }
            LeaveCriticalSection(&s_tp.lock);
            if (!d)
                break;
            swprintf_s(path, MAX_PATH, L"%s\\dump\\buffy_%ux%u_%016llx_%02X.png", s_tp.root, d->w, d->h,
                       (unsigned long long)d->hash, d->fmt);
            if (f && tp_write_png(f, path, d->px, d->w, d->h))
                InterlockedIncrement((volatile LONG *)&s_tp.dumped_run);
            free(d->px);
            free(d);
        }
    }
}

/* Queue level 0 (BGRA, w x h) for dumping, once per hash. */
static void tp_dump(uint64_t hash, uint32_t fmt, uint32_t w, uint32_t h, const uint32_t *px)
{
    TpDump *d;
    if (tp_dumped(hash, 1))
        return;
    d = (TpDump *)malloc(sizeof *d);
    if (!d)
        return;
    d->px = (uint32_t *)malloc((size_t)w * h * 4);
    if (!d->px) { free(d); return; }
    memcpy(d->px, px, (size_t)w * h * 4);
    d->next = NULL; d->hash = hash; d->w = w; d->h = h; d->fmt = fmt;
    EnterCriticalSection(&s_tp.lock);
    if (s_tp.qtail) s_tp.qtail->next = d; else s_tp.qhead = d;
    s_tp.qtail = d;
    LeaveCriticalSection(&s_tp.lock);
    SetEvent(s_tp.qevent);
}

/* ── start-up, lookup ── */

static void tp_lock_make(void)
{
    if (!s_tp.lock_made) {
        InitializeCriticalSection(&s_tp.lock);
        s_tp.lock_made = 1;
    }
}

static void tp_init(void)
{
    WCHAR d[MAX_PATH];
    s_tp.inited = 1;
    if (!tp_active())
        return;
    tp_lock_make();
    CreateDirectoryW(s_tp.root, NULL);
    if (s_tp.load) {
        TpTable tb = { NULL, 0, 0 };
        swprintf_s(d, MAX_PATH, L"%s\\load", s_tp.root);
        CreateDirectoryW(d, NULL);
        tp_scan(&tb, d, 0);
        s_tp.ent = tb.ent; s_tp.cap = tb.cap; s_tp.n = tb.n;
        fwprintf(stderr, L"  [TEXPACK] %u replacement textures in %s%s\n", s_tp.n, d,
                 s_tp.prefetch && s_tp.n ? L" (prefetching)" : L"");
        if (s_tp.prefetch && s_tp.n)
            s_tp.prefetch_thread = CreateThread(NULL, 0, tp_prefetch_thread, NULL, 0, NULL);
    }
    if (s_tp.dump) {
        swprintf_s(d, MAX_PATH, L"%s\\dump", s_tp.root);
        CreateDirectoryW(d, NULL);
        tp_scan_dumped(d);
        s_tp.qevent = CreateEventW(NULL, FALSE, FALSE, NULL);
        CloseHandle(CreateThread(NULL, 0, tp_dump_thread, NULL, 0, NULL));
        fwprintf(stderr, L"  [TEXPACK] dumping new textures to %s (%u already there)\n", d, s_tp.dn);
    }
}

/* The replacement for `hash`, AddRef'd into *tex, or 0. */
static int tp_replacement(uint64_t hash, GpuTexture **tex)
{
    static IWICImagingFactory *f;
    TpEntry *e;
    int ok = 0;
    if (!s_tp.load || !s_tp.n || !(e = tp_find(hash)))
        return 0;
    if (!e->tex && !e->failed) {
        if (!f) f = tp_wic();
        tp_load_entry(f, e);
    }
    EnterCriticalSection(&s_tp.lock);
    if (e->tex) {
        gpu_texture_addref(e->tex);
        *tex = e->tex;
        ok = 1;
    }
    LeaveCriticalSection(&s_tp.lock);
    if (!ok)
        return 0;
    if (s_tp.replaced++ < 20 || getenv("BUFFY_TEX_LOG"))
        fwprintf(stderr, L"  [TEXPACK] replaced %016llx\n", (unsigned long long)hash);
    return 1;
}

/* ── reading load\ again while the title runs ── */

static void tp_free_table(TpEntry *ent, uint32_t cap)
{
    uint32_t i;
    for (i = 0; i < cap; i++) {
        if (ent[i].tex) gpu_texture_release(ent[i].tex);
        free(ent[i].path);
    }
    free(ent);
}

/* Makes a whole new table beside the one in use: the folder read, every
 * picture decoded and uploaded. Nothing the render thread uses is touched
 * until it takes the table over (tp_apply), so no frame waits for a file. */
static DWORD WINAPI tp_reload_thread(LPVOID arg)
{
    IWICImagingFactory *f = tp_wic();
    (void)arg;
    if (s_tp.prefetch_thread)                       /* start-up's loading still under way: let it end */
        WaitForSingleObject(s_tp.prefetch_thread, INFINITE);
    for (;;) {
        TpTable tb = { NULL, 0, 0 };
        WCHAR d[MAX_PATH];
        uint32_t gen, i, made = 0, failed = 0;
        int load, done;
        LARGE_INTEGER t0, t1, fq;
        QueryPerformanceFrequency(&fq);
        QueryPerformanceCounter(&t0);
        EnterCriticalSection(&s_tp.lock);
        gen = s_tp.r_gen;
        load = s_tp.r_load;
        swprintf_s(d, MAX_PATH, L"%s\\load", s_tp.r_root);
        LeaveCriticalSection(&s_tp.lock);
        if (load)
            tp_scan(&tb, d, 0);
        for (i = 0; i < tb.cap; i++) {
            TpEntry *e = &tb.ent[i];
            uint32_t w = 0, h = 0, *px;
            if (!e->path)
                continue;
            px = f ? tp_read_image(f, e->path, &w, &h) : NULL;
            if (px && tp_make_texture(px, w, h, &e->tex))
                made++;
            else {
                e->tex = NULL;
                e->failed = 1;
                failed++;
                fwprintf(stderr, L"  [TEXPACK] reload: could not load %s\n", e->path);
            }
            free(px);
        }
        QueryPerformanceCounter(&t1);
        EnterCriticalSection(&s_tp.lock);
        done = gen == s_tp.r_gen;
        if (done) {
            if (s_tp.next || s_tp.pending)          /* an earlier one the render thread never took */
                tp_free_table(s_tp.next, s_tp.next_cap);
            s_tp.next = tb.ent; s_tp.next_cap = tb.cap; s_tp.next_n = tb.n;
            s_tp.r_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)fq.QuadPart;
            s_tp.r_running = 0;
            InterlockedExchange(&s_tp.pending, 1);
        }
        LeaveCriticalSection(&s_tp.lock);
        if (done) {
            fprintf(stderr, "  [TEXPACK] reload: %u replacements read in %.0f ms on the loading thread (%u failed)\n",
                    made, s_tp.r_ms, failed);
            break;
        }
        tp_free_table(tb.ent, tb.cap);              /* asked again meanwhile: the files may have changed under us */
    }
    if (f) IWICImagingFactory_Release(f);
    return 0;
}

/* Read the pack's load\ folder again: files with new contents, new files,
 * files gone. Call it on the thread that draws (the title's), after the files
 * are written -- and after nv2a_gpu_set_texture_pack if the folder or Load
 * changed. It returns at once: the pictures are read and uploaded on a
 * background thread, the old ones stay on screen meanwhile, and the frame
 * after the last one is ready every texture the title has in use takes its new
 * replacement (or its own picture back). Calling it again before that is fine:
 * the folder is read once more and only the last reading is shown. */
void nv2a_gpu_texture_pack_reload(void)
{
    int start;
    if (!s_tp.inited)
        return;                                     /* no texture yet: the first one reads the folder */
    tp_lock_make();
    EnterCriticalSection(&s_tp.lock);
    wcscpy_s(s_tp.r_root, MAX_PATH, s_tp.root);
    s_tp.r_load = s_tp.load;
    s_tp.r_gen++;
    start = !s_tp.r_running;
    s_tp.r_running = 1;
    LeaveCriticalSection(&s_tp.lock);
    if (start) {
        HANDLE th = CreateThread(NULL, 0, tp_reload_thread, NULL, 0, NULL);
        if (th) {
            SetThreadPriority(th, THREAD_PRIORITY_BELOW_NORMAL);
            CloseHandle(th);
        } else {
            s_tp.r_running = 0;
        }
    }
}

/* Render thread, when a reload's table is ready (s_tp.pending): take it over
 * and make the textures it changes look their replacement up again at their
 * next use. `forget(hash, known, replaced)` is called per cached texture and
 * says whether to drop it. */
static int tp_apply_begin(void)
{
    TpEntry *old;
    uint32_t oc;
    if (!s_tp.lock_made)
        return 0;
    EnterCriticalSection(&s_tp.lock);
    if (!s_tp.pending) {
        LeaveCriticalSection(&s_tp.lock);
        return 0;
    }
    old = s_tp.ent; oc = s_tp.cap;
    s_tp.ent = s_tp.next; s_tp.cap = s_tp.next_cap; s_tp.n = s_tp.next_n;
    s_tp.next = NULL; s_tp.next_cap = s_tp.next_n = 0;
    InterlockedExchange(&s_tp.pending, 0);
    LeaveCriticalSection(&s_tp.lock);
    tp_free_table(old, oc);                         /* (textures still drawn with hold their own reference) */
    return 1;
}

/* Does a cached texture have to be looked up again after a reload? */
static int tp_stale(uint64_t hash, int known, int replaced)
{
    TpEntry *e;
    if (replaced)
        return 1;                                   /* its picture may be another one now, or gone */
    if (!known)
        return tp_active();                         /* made while no pack was on: it may have one now */
    return s_tp.load && (e = tp_find(hash)) != NULL && e->tex != NULL;
}
