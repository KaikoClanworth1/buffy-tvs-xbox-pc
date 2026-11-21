/* Buffy the Vampire Slayer (Xbox, 2002) - launcher ("Buffy Launcher.exe")
 *
 * Two things only: Install and Play. Everything else is set in the game.
 *
 *   Install   choose your Xbox disc image, choose a folder; the launcher
 *             checks the image is this game (and the release this build was
 *             made from), copies the game out of it, and puts the PC program
 *             beside it. The image is opened read-only and never written.
 *   Play      starts buffy_tvs.exe in the game folder.
 *
 * The game folder is remembered in launcher.ini beside the launcher; a
 * launcher that sits in a game folder needs no ini at all.
 *
 * Without a window (tests, scripts; nothing is shown and no focus is taken):
 *   --install <image> <folder> [--quiet]   exit 0 installed, 2 game files
 *                                          installed but no buffy_tvs.exe
 *                                          beside the launcher, 1 failed
 *   --play [game folder]                   start the game as the Play button
 *                                          does and wait for it; exits with
 *                                          the game's exit code
 *   --identify <image | folder | xbe>      which game and release it is
 *   --list <image>                         "<bytes>\t<path>" per file
 *   --capture <install|busy|error|play> <out.bmp> [game folder]
 *                                          the window's picture, drawn off screen
 *   --version
 *
 * The window is drawn by hand (no child controls), so it can look like part
 * of the game rather than a grey dialog. No game art is built in: the icon in
 * the header is read from the installed Default.xbe.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <ole2.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <wchar.h>

#include "btvs_release.h"

#ifndef BTVS_VERSION
#define BTVS_VERSION "0.0.0"            /* from CMakeLists.txt */
#endif
#define WIDEN_(x)     L##x
#define WIDEN(x)      WIDEN_(x)
#define GAME_TITLE    L"Buffy the Vampire Slayer"
#define GAME_EXE      L"buffy_tvs.exe"
#define LAUNCHER_EXE  L"Buffy Launcher.exe"
#define SAVE_DIR      L"XboxData"       /* beside the exe: the game's hard disk (saved games in
                                         * HardDisk\, its cache in Cache\); install never touches it */
#define REPORTS_DIR   L"reports"        /* beside the exe (Logs area) */
#define SETTINGS_INI  L"btvs_settings.ini"
#define CONSOLE_FILE  L"btvs_console.txt"  /* the game's stdout and stderr when started by Play */
#define CHAOS_BLEEDS_TITLE_ID 0x56550005u

#define WM_APP_PROGRESS (WM_APP + 1)    /* wParam permille, lParam heap WCHAR* or 0 */
#define WM_APP_DONE     (WM_APP + 2)    /* wParam result (INSTALL_*), lParam heap WCHAR* */
#define WM_APP_GAMEEND  (WM_APP + 3)    /* wParam exit code */

enum { INSTALL_FAILED = 0, INSTALL_OK = 1, INSTALL_NO_PROGRAM = 2 };

static HINSTANCE s_inst;
static HWND      s_wnd;
static int       s_dpi = 96;
static int       s_quiet;               /* --quiet: no progress lines */
static WCHAR     s_launcher_dir[MAX_PATH], s_launcher_ini[MAX_PATH];
static WCHAR     s_game_dir[MAX_PATH];
static volatile LONG s_cancel;

/* ── small helpers ─────────────────────────────────────────────────────── */

static int S(int v) { return MulDiv(v, s_dpi, 96); }

static int file_exists(const WCHAR *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int dir_exists(const WCHAR *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void join(WCHAR *out, const WCHAR *a, const WCHAR *b)
{
    size_t n = wcslen(a);
    swprintf_s(out, MAX_PATH, (n && (a[n - 1] == L'\\' || a[n - 1] == L'/')) ? L"%s%s" : L"%s\\%s", a, b);
}

static int mkdirs(const WCHAR *path)
{
    WCHAR tmp[MAX_PATH];
    WCHAR *p;
    if (wcslen(path) < 3)
        return 0;
    wcscpy_s(tmp, MAX_PATH, path);
    for (p = tmp + 3; *p; p++) {
        if (*p == L'\\' || *p == L'/') {
            WCHAR c = *p;
            *p = 0;
            CreateDirectoryW(tmp, NULL);
            *p = c;
        }
    }
    return CreateDirectoryW(tmp, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static int dir_is_empty(const WCHAR *dir)
{
    WCHAR pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int empty = 1;
    join(pat, dir, L"*");
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 1;
    do {
        if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L".."))
            empty = 0;
    } while (empty && FindNextFileW(h, &fd));
    FindClose(h);
    return empty;
}

static WCHAR *wdup(const WCHAR *s)
{
    size_t n = wcslen(s) + 1;
    WCHAR *d = (WCHAR *)malloc(n * sizeof(WCHAR));
    if (d)
        wcscpy_s(d, n, s);
    return d;
}

/* Progress from the install: to the window, or (no window) to stdout. */
static void post_progress(int permille, const WCHAR *fmt, ...)
{
    WCHAR buf[512];
    va_list ap;
    buf[0] = 0;
    if (fmt) {
        va_start(ap, fmt);
        vswprintf_s(buf, 512, fmt, ap);
        va_end(ap);
    }
    if (s_wnd) {
        PostMessageW(s_wnd, WM_APP_PROGRESS, (WPARAM)permille, fmt ? (LPARAM)wdup(buf) : 0);
    } else if (!s_quiet && fmt) {
        wprintf(L"[%3d%%] %s\n", permille / 10, buf);
        fflush(stdout);
    }
}

/* The game's files are there: Default.xbe and its data folders. */
static int is_game_folder(const WCHAR *dir)
{
    WCHAR a[MAX_PATH], b[MAX_PATH], c[MAX_PATH];
    if (!dir[0])
        return 0;
    join(a, dir, L"Default.xbe");
    join(b, dir, L"pak");
    join(c, dir, L"media");
    return file_exists(a) && dir_exists(b) && dir_exists(c);
}

static int has_program(const WCHAR *dir)
{
    WCHAR a[MAX_PATH];
    join(a, dir, GAME_EXE);
    return file_exists(a);
}

/* ── launcher.ini ──────────────────────────────────────────────────────── */

static WCHAR s_image[MAX_PATH], s_target[MAX_PATH];

static void load_launcher_ini(void)
{
    GetPrivateProfileStringW(L"Launcher", L"GameFolder", L"", s_game_dir, MAX_PATH, s_launcher_ini);
    GetPrivateProfileStringW(L"Launcher", L"DiscImage", L"", s_image, MAX_PATH, s_launcher_ini);
    if (!is_game_folder(s_game_dir)) {
        s_game_dir[0] = 0;
        if (is_game_folder(s_launcher_dir))
            wcscpy_s(s_game_dir, MAX_PATH, s_launcher_dir);
    }
    if (s_image[0] && !file_exists(s_image))
        s_image[0] = 0;
}

static void save_launcher_ini(void)
{
    WritePrivateProfileStringW(L"Launcher", L"GameFolder", s_game_dir, s_launcher_ini);
    WritePrivateProfileStringW(L"Launcher", L"DiscImage", s_image, s_launcher_ini);
}

/* ── Default.xbe: which game, which release ────────────────────────────── */

typedef struct {
    uint32_t title_id, region, version, size;
    char     sha256[65];
} XbeInfo;

static int sha256_hex(const uint8_t *data, size_t n, char out[65])
{
    BCRYPT_ALG_HANDLE alg;
    uint8_t h[32];
    int i, ok;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0)
        return 0;
    ok = BCryptHash(alg, NULL, 0, (PUCHAR)data, (ULONG)n, h, 32) >= 0;
    BCryptCloseAlgorithmProvider(alg, 0);
    for (i = 0; ok && i < 32; i++)
        sprintf_s(out + i * 2, 3, "%02x", h[i]);
    return ok;
}

static int xbe_info(const uint8_t *x, size_t n, XbeInfo *info)
{
    uint32_t base, cert;
    memset(info, 0, sizeof *info);
    if (n < 0x180 || memcmp(x, "XBEH", 4))
        return 0;
    memcpy(&base, x + 0x104, 4);
    memcpy(&cert, x + 0x118, 4);
    cert -= base;
    if ((uint64_t)cert + 0xB0 > n)
        return 0;
    memcpy(&info->title_id, x + cert + 8, 4);
    memcpy(&info->region, x + cert + 0xA0, 4);
    memcpy(&info->version, x + cert + 0xAC, 4);
    info->size = (uint32_t)n;
    return sha256_hex(x, n, info->sha256);
}

/* 1 if this build runs that Default.xbe; otherwise 0 and why, for the player. */
static int release_supported(const XbeInfo *x, WCHAR *why, size_t cap)
{
    if (x->title_id == CHAOS_BLEEDS_TITLE_ID) {
        swprintf_s(why, cap, L"This disc is Buffy the Vampire Slayer: Chaos Bleeds, the 2003 sequel. "
                             L"This port is of the first game (2002).");
        return 0;
    }
    if (x->title_id != BTVS_TITLE_ID) {
        swprintf_s(why, cap, L"This disc is not " GAME_TITLE L" (its title ID is %08X; the game's is %08X).",
                   x->title_id, BTVS_TITLE_ID);
        return 0;
    }
    if (!strcmp(x->sha256, BTVS_XBE_SHA256))
        return 1;
    if (x->region == BTVS_XBE_REGION && x->version == BTVS_XBE_VERSION)
        swprintf_s(why, cap, L"This is the right release of the game (the " WIDEN(BTVS_RELEASE_NAME) L"), but its "
                             L"Default.xbe has been changed or damaged: it is not the file from the original disc. "
                             L"Use an unmodified image of your disc.");
    else
        swprintf_s(why, cap, L"This is another release of " GAME_TITLE L" (region %X, version %u). This build of "
                             L"the port was made from the " WIDEN(BTVS_RELEASE_NAME) L" and only runs that one: "
                             L"another release needs its own build.", x->region, x->version);
    return 0;
}

static uint8_t *read_whole_file(const WCHAR *path, size_t *n)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER sz;
    uint8_t *buf;
    DWORD got = 0;
    if (f == INVALID_HANDLE_VALUE)
        return NULL;
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (64 << 20)) {
        CloseHandle(f);
        return NULL;
    }
    buf = (uint8_t *)malloc((size_t)sz.QuadPart);
    if (buf && (!ReadFile(f, buf, (DWORD)sz.QuadPart, &got, NULL) || got != (DWORD)sz.QuadPart)) {
        free(buf);
        buf = NULL;
    }
    CloseHandle(f);
    *n = (size_t)sz.QuadPart;
    return buf;
}

/* ── the disc image: XDVDFS ────────────────────────────────────────────── */

typedef struct {
    WCHAR    path[MAX_PATH];     /* relative, backslashes */
    uint32_t sector, size;
} DiscFile;

typedef struct {
    HANDLE    f;
    uint64_t  base;              /* the game partition's offset in the image */
    DiscFile *files;
    int       count, cap;
    uint64_t  total;
} Disc;

static int disc_read(Disc *d, uint64_t off, void *buf, DWORD n)
{
    LARGE_INTEGER li;
    DWORD got = 0;
    li.QuadPart = (LONGLONG)(d->base + off);
    return SetFilePointerEx(d->f, li, NULL, FILE_BEGIN) && ReadFile(d->f, buf, n, &got, NULL) && got == n;
}

static int disc_add(Disc *d, const WCHAR *path, uint32_t sector, uint32_t size)
{
    if (d->count == d->cap) {
        int cap = d->cap ? d->cap * 2 : 256;
        DiscFile *n = (DiscFile *)realloc(d->files, sizeof(DiscFile) * (size_t)cap);
        if (!n)
            return 0;
        d->files = n;
        d->cap = cap;
    }
    wcscpy_s(d->files[d->count].path, MAX_PATH, path);
    d->files[d->count].sector = sector;
    d->files[d->count].size = size;
    d->count++;
    d->total += size;
    return 1;
}

static int disc_dir(Disc *d, uint32_t sector, uint32_t size, const WCHAR *prefix, int depth);

/* One node of a directory's binary tree (offset in dwords), in order. */
static int disc_node(Disc *d, const uint8_t *dir, uint32_t size, uint32_t off, const WCHAR *prefix,
                     int depth, int guard)
{
    uint16_t left, right;
    uint32_t sec, sz;
    uint8_t attr, nlen;
    WCHAR name[256], path[MAX_PATH];
    int i;

    if (guard > 4096 || (size_t)off * 4 + 14 > size)
        return 1;
    left = (uint16_t)(dir[off * 4] | dir[off * 4 + 1] << 8);
    right = (uint16_t)(dir[off * 4 + 2] | dir[off * 4 + 3] << 8);
    if (left == 0xFFFF && right == 0xFFFF)
        return 1;                                   /* padding */
    memcpy(&sec, dir + off * 4 + 4, 4);
    memcpy(&sz, dir + off * 4 + 8, 4);
    attr = dir[off * 4 + 12];
    nlen = dir[off * 4 + 13];
    if ((size_t)off * 4 + 14 + nlen > size)
        return 0;
    for (i = 0; i < nlen; i++)
        name[i] = (WCHAR)dir[off * 4 + 14 + i];
    name[nlen] = 0;
    /* a name that could leave the install folder: refuse the whole image */
    if (!nlen || wcschr(name, L'\\') || wcschr(name, L'/') || wcschr(name, L':')
            || !wcscmp(name, L"..") || !wcscmp(name, L"."))
        return 0;
    if (wcslen(prefix) + nlen + 2 >= MAX_PATH - 80)
        return 0;
    if (left && left != 0xFFFF && !disc_node(d, dir, size, left, prefix, depth, guard + 1))
        return 0;
    swprintf_s(path, MAX_PATH, prefix[0] ? L"%s\\%s" : L"%s%s", prefix, name);
    if (attr & 0x10) {
        if (sz && !disc_dir(d, sec, sz, path, depth + 1))
            return 0;
    } else if (!disc_add(d, path, sec, sz)) {
        return 0;
    }
    if (right && right != 0xFFFF && !disc_node(d, dir, size, right, prefix, depth, guard + 1))
        return 0;
    return 1;
}

static int disc_dir(Disc *d, uint32_t sector, uint32_t size, const WCHAR *prefix, int depth)
{
    uint8_t *buf;
    int ok;
    if (depth > 32 || size > (16u << 20))
        return 0;
    buf = (uint8_t *)malloc(size);
    if (!buf)
        return 0;
    ok = disc_read(d, (uint64_t)sector * 2048, buf, size) && disc_node(d, buf, size, 0, prefix, depth, 0);
    free(buf);
    return ok;
}

static void disc_close(Disc *d)
{
    if (d->f && d->f != INVALID_HANDLE_VALUE)
        CloseHandle(d->f);
    free(d->files);
    memset(d, 0, sizeof *d);
}

/* Open an image (read-only) and list its files; on failure *err says why.
 * The game partition starts at 0 in a bare xiso, and further in for a full
 * disc image (redump: video partition first) or a few older rip layouts. */
static int disc_open(Disc *d, const WCHAR *image, WCHAR *err, size_t errn)
{
    static const uint64_t bases[] = { 0, 0x18300000ull, 0xFD90000ull, 0x2080000ull };
    uint8_t vd[2048];
    int i;
    memset(d, 0, sizeof *d);
    d->f = CreateFileW(image, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (d->f == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
            swprintf_s(err, errn, L"The disc image is not there any more: %s", image);
        else
            swprintf_s(err, errn, L"The disc image could not be opened (error %lu).", e);
        return 0;
    }
    for (i = 0; i < 4; i++) {
        d->base = bases[i];
        if (disc_read(d, 32 * 2048, vd, sizeof vd) && !memcmp(vd, "MICROSOFT*XBOX*MEDIA", 20)
                && !memcmp(vd + 0x7EC, "MICROSOFT*XBOX*MEDIA", 20)) {
            uint32_t root, rsize;
            memcpy(&root, vd + 20, 4);
            memcpy(&rsize, vd + 24, 4);
            if (!disc_dir(d, root, rsize, L"", 0)) {
                swprintf_s(err, errn, L"The disc image is damaged: its file list could not be read.");
                return 0;
            }
            return 1;
        }
    }
    swprintf_s(err, errn, L"This file is not an Xbox disc image. Choose the .iso (xiso) made from your "
                          L"Xbox disc of " GAME_TITLE L".");
    return 0;
}

static DiscFile *disc_find(Disc *d, const WCHAR *path)
{
    int i;
    for (i = 0; i < d->count; i++)
        if (!_wcsicmp(d->files[i].path, path))
            return &d->files[i];
    return NULL;
}

/* Is the image this game, in the release this build runs? */
static int disc_check(Disc *d, XbeInfo *info, WCHAR *err, size_t errn)
{
    DiscFile *x = disc_find(d, L"Default.xbe");
    uint8_t *buf;
    int ok;
    memset(info, 0, sizeof *info);
    if (!x || x->size < 0x1000 || x->size > (64u << 20) || !(buf = (uint8_t *)malloc(x->size))) {
        swprintf_s(err, errn, L"The disc image has no readable Default.xbe: it is incomplete or not a game disc.");
        return 0;
    }
    ok = disc_read(d, (uint64_t)x->sector * 2048, buf, x->size) && xbe_info(buf, x->size, info);
    free(buf);
    if (!ok) {
        swprintf_s(err, errn, L"The disc image's Default.xbe could not be read: the image is incomplete or damaged.");
        return 0;
    }
    return release_supported(info, err, errn);
}

/* ── install ───────────────────────────────────────────────────────────── */

static WCHAR s_install_msg[1024];

static int copy_out(Disc *d, DiscFile *df, const WCHAR *target, uint8_t *buf, uint64_t *done, uint64_t total)
{
    WCHAR dst[MAX_PATH], dir[MAX_PATH], *slash;
    HANDLE out;
    uint32_t left = df->size;
    uint64_t off = (uint64_t)df->sector * 2048;
    join(dst, target, df->path);
    wcscpy_s(dir, MAX_PATH, dst);
    slash = wcsrchr(dir, L'\\');
    if (slash) {
        *slash = 0;
        mkdirs(dir);
    }
    out = CreateFileW(dst, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        swprintf_s(s_install_msg, 1024, e == ERROR_SHARING_VIOLATION
                   ? L"Could not write %s: it is in use. Close the game and try again."
                   : L"Could not create %s (error %lu).", dst, e);
        return 0;
    }
    while (left) {
        DWORD n = left > (4u << 20) ? (4u << 20) : left, wr = 0;
        if (s_cancel) {
            CloseHandle(out);
            DeleteFileW(dst);
            wcscpy_s(s_install_msg, 1024, L"Installation cancelled.");
            return 0;
        }
        if (!disc_read(d, off, buf, n)) {
            CloseHandle(out);
            DeleteFileW(dst);
            swprintf_s(s_install_msg, 1024, L"The disc image could not be read at %s. The image is incomplete.", df->path);
            return 0;
        }
        if (!WriteFile(out, buf, n, &wr, NULL) || wr != n) {
            DWORD e = GetLastError();
            CloseHandle(out);
            DeleteFileW(dst);
            swprintf_s(s_install_msg, 1024, L"Could not write %s (error %lu). Is the disk full?", dst, e);
            return 0;
        }
        off += n;
        left -= n;
        *done += n;
        post_progress((int)(*done * 980 / (total ? total : 1)), NULL);
    }
    CloseHandle(out);
    return 1;
}

/* The port's own files, from the launcher's folder to the install. */
static void copy_program(const WCHAR *target)
{
    /* The Visual C++ runtime may ship beside the exe (app-local), for PCs
     * without the redistributable installed. */
    static const WCHAR *files[] = { GAME_EXE, L"buffy_tvs.pdb", L"Read Me.txt", LAUNCHER_EXE,
                                    L"SDL3.dll", L"vcruntime140.dll", L"vcruntime140_1.dll", L"msvcp140.dll" };
    WCHAR src[MAX_PATH], dst[MAX_PATH], full_a[MAX_PATH], full_b[MAX_PATH];
    int i;
    GetFullPathNameW(target, MAX_PATH, full_a, NULL);
    GetFullPathNameW(s_launcher_dir, MAX_PATH, full_b, NULL);
    if (!_wcsicmp(full_a, full_b))
        return;
    for (i = 0; i < (int)_countof(files); i++) {
        join(src, s_launcher_dir, files[i]);
        join(dst, target, files[i]);
        if (file_exists(src))
            CopyFileW(src, dst, FALSE);
    }
}

/* A first install starts the game borderless at the desktop's resolution with
 * sound on. Those are the game's own defaults too; the file only makes them
 * visible. A settings file that is already there is the player's: left alone. */
static void default_settings(const WCHAR *target)
{
    static const char text[] =
        "; Buffy the Vampire Slayer - PC settings. The game's Options menu writes this file.\r\n"
        "[display]\r\n"
        "mode=1\r\n"          /* 0 windowed, 1 borderless, 2 exclusive fullscreen */
        "width=0\r\n"         /* 0 = the desktop's size */
        "height=0\r\n"
        "vsync=1\r\n"
        "[audio]\r\n"
        "master=100\r\n"
        "music=100\r\n"
        "effects=100\r\n"
        "speech=100\r\n";
    WCHAR path[MAX_PATH];
    HANDLE h;
    DWORD wr;
    join(path, target, SETTINGS_INI);
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    WriteFile(h, text, sizeof text - 1, &wr, NULL);
    CloseHandle(h);
}

/* Space the copy needs: a file already there at the right size is rewritten
 * in place and needs none. */
static uint64_t space_needed(Disc *d, const WCHAR *target)
{
    uint64_t need = 0;
    int i;
    for (i = 0; i < d->count; i++) {
        WCHAR dst[MAX_PATH];
        WIN32_FILE_ATTRIBUTE_DATA fa;
        join(dst, target, d->files[i].path);
        if (GetFileAttributesExW(dst, GetFileExInfoStandard, &fa) && !fa.nFileSizeHigh
                && fa.nFileSizeLow == d->files[i].size)
            continue;
        need += d->files[i].size;
    }
    return need;
}

static int install_run(const WCHAR *image, const WCHAR *target_in)
{
    Disc d;
    XbeInfo info;
    uint64_t done = 0, need;
    ULARGE_INTEGER freeb;
    uint8_t *buf;
    int i, ok = 1;
    WCHAR err[512], target[MAX_PATH];

    s_install_msg[0] = 0;
    if (!GetFullPathNameW(target_in, MAX_PATH, target, NULL) || wcslen(target) < 3 || wcslen(target) > MAX_PATH - 100) {
        wcscpy_s(s_install_msg, 1024, L"That install folder's path is not usable (too long, or not a folder path).");
        return INSTALL_FAILED;
    }
    post_progress(0, L"Reading the disc image...");
    if (!disc_open(&d, image, err, 512) || !disc_check(&d, &info, err, 512)) {
        wcscpy_s(s_install_msg, 1024, err);
        disc_close(&d);
        return INSTALL_FAILED;
    }
    if (!mkdirs(target)) {
        swprintf_s(s_install_msg, 1024, L"Could not create the folder %s (error %lu).", target, GetLastError());
        disc_close(&d);
        return INSTALL_FAILED;
    }
    need = space_needed(&d, target);
    if (GetDiskFreeSpaceExW(target, &freeb, NULL, NULL) && freeb.QuadPart < need + (64ull << 20)) {
        swprintf_s(s_install_msg, 1024, L"Not enough free space: the game needs %.1f GB there, and the drive has %.1f GB free.",
                   (need + (64ull << 20)) / 1073741824.0, freeb.QuadPart / 1073741824.0);
        disc_close(&d);
        return INSTALL_FAILED;
    }
    buf = (uint8_t *)malloc(4u << 20);
    if (!buf) {
        disc_close(&d);
        wcscpy_s(s_install_msg, 1024, L"Out of memory.");
        return INSTALL_FAILED;
    }
    for (i = 0; i < d.count && ok; i++) {
        post_progress((int)(done * 980 / (d.total ? d.total : 1)),
                      L"Copying the game from your disc image: %.2f of %.2f GB",
                      done / 1073741824.0, d.total / 1073741824.0);
        ok = copy_out(&d, &d.files[i], target, buf, &done, d.total);
    }
    free(buf);
    disc_close(&d);
    if (!ok)
        return INSTALL_FAILED;
    post_progress(985, L"Setting up the PC version...");
    copy_program(target);
    /* SAVE_DIR is the game's to make; an existing one holds the player's saves */
    default_settings(target);
    post_progress(1000, L"Installed.");
    if (!has_program(target)) {
        wcscpy_s(s_install_msg, 1024, L"The game's files are installed, but there is no " GAME_EXE L" beside the "
                                      L"launcher to put with them. Unpack the whole release and install again, "
                                      L"or copy " GAME_EXE L" into the game folder.");
        return INSTALL_NO_PROGRAM;
    }
    return INSTALL_OK;
}

typedef struct { WCHAR image[MAX_PATH], target[MAX_PATH]; } InstallJob;
static InstallJob s_job;

static DWORD WINAPI install_thread(LPVOID unused)
{
    int r;
    (void)unused;
    r = install_run(s_job.image, s_job.target);
    PostMessageW(s_wnd, WM_APP_DONE, (WPARAM)r, (LPARAM)wdup(s_install_msg));
    return 0;
}

/* ── the game's own icon: $$XTIMAGE in Default.xbe (128x128 DXT1 XPR0) ─── */

static uint32_t s_icon_px[128 * 128];   /* 0xAARRGGBB, top row first */
static int      s_have_icon;
static HICON    s_icon;

static void dxt1_block(const uint8_t *b, uint32_t out[16])
{
    uint16_t c0 = (uint16_t)(b[0] | b[1] << 8), c1 = (uint16_t)(b[2] | b[3] << 8);
    uint32_t pal[4], bits = (uint32_t)b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24;
    int r0 = (c0 >> 11) * 255 / 31, g0 = ((c0 >> 5) & 63) * 255 / 63, b0 = (c0 & 31) * 255 / 31;
    int r1 = (c1 >> 11) * 255 / 31, g1 = ((c1 >> 5) & 63) * 255 / 63, b1 = (c1 & 31) * 255 / 31;
    int i;
    pal[0] = 0xFF000000u | r0 << 16 | g0 << 8 | b0;
    pal[1] = 0xFF000000u | r1 << 16 | g1 << 8 | b1;
    if (c0 > c1) {
        pal[2] = 0xFF000000u | ((2 * r0 + r1) / 3) << 16 | ((2 * g0 + g1) / 3) << 8 | (2 * b0 + b1) / 3;
        pal[3] = 0xFF000000u | ((r0 + 2 * r1) / 3) << 16 | ((g0 + 2 * g1) / 3) << 8 | (b0 + 2 * b1) / 3;
    } else {
        pal[2] = 0xFF000000u | ((r0 + r1) / 2) << 16 | ((g0 + g1) / 2) << 8 | (b0 + b1) / 2;
        pal[3] = 0xFF000000u;
    }
    for (i = 0; i < 16; i++)
        out[i] = pal[(bits >> (i * 2)) & 3];
}

static int icon_pixels(const WCHAR *game_dir, uint32_t *px)
{
    WCHAR p[MAX_PATH];
    uint8_t *x;
    size_t n = 0;
    uint32_t base, nsec, sec, i;
    int found = 0;
    join(p, game_dir, L"Default.xbe");
    x = read_whole_file(p, &n);
    if (!x)
        return 0;
    if (n > 0x180 && !memcmp(x, "XBEH", 4)) {
        memcpy(&base, x + 0x104, 4);
        memcpy(&nsec, x + 0x11C, 4);
        memcpy(&sec, x + 0x120, 4);
        sec -= base;
        for (i = 0; i < nsec && i < 64 && (uint64_t)sec + (i + 1) * 56 <= n; i++) {
            uint32_t raw, rsz, name;
            const uint8_t *img;
            int bx, by, k;
            memcpy(&raw, x + sec + i * 56 + 12, 4);
            memcpy(&rsz, x + sec + i * 56 + 16, 4);
            memcpy(&name, x + sec + i * 56 + 20, 4);
            name -= base;
            if ((uint64_t)name + 10 > n || memcmp(x + name, "$$XTIMAGE", 10))
                continue;
            if ((uint64_t)raw + rsz > n || rsz < 0x800 + 8192)
                break;
            img = x + raw;
            if (memcmp(img, "XPR0", 4) || img[0x19] != 0x0C)     /* DXT1 */
                break;
            for (by = 0; by < 32; by++)
                for (bx = 0; bx < 32; bx++) {
                    uint32_t blk[16];
                    dxt1_block(img + 0x800 + (by * 32 + bx) * 8, blk);
                    for (k = 0; k < 16; k++)
                        px[(by * 4 + k / 4) * 128 + bx * 4 + k % 4] = blk[k];
                }
            found = 1;
            break;
        }
    }
    free(x);
    return found;
}

static void load_game_icon(void)
{
    BITMAPV5HEADER bh;
    HBITMAP color, mask;
    ICONINFO ii;
    HDC dc;
    void *bits = NULL;
    if (s_have_icon || !is_game_folder(s_game_dir) || !icon_pixels(s_game_dir, s_icon_px))
        return;
    s_have_icon = 1;
    memset(&bh, 0, sizeof bh);
    bh.bV5Size = sizeof bh;
    bh.bV5Width = 128;
    bh.bV5Height = -128;
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_BITFIELDS;
    bh.bV5RedMask = 0x00FF0000; bh.bV5GreenMask = 0x0000FF00; bh.bV5BlueMask = 0x000000FF;
    bh.bV5AlphaMask = 0xFF000000;
    dc = GetDC(NULL);
    color = CreateDIBSection(dc, (BITMAPINFO *)&bh, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!color)
        return;
    memcpy(bits, s_icon_px, sizeof s_icon_px);
    mask = CreateBitmap(128, 128, 1, 1, NULL);
    ii.fIcon = TRUE; ii.xHotspot = ii.yHotspot = 0; ii.hbmMask = mask; ii.hbmColor = color;
    s_icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    if (s_icon && s_wnd) {
        SendMessageW(s_wnd, WM_SETICON, ICON_BIG, (LPARAM)s_icon);
        SendMessageW(s_wnd, WM_SETICON, ICON_SMALL, (LPARAM)s_icon);
    }
}

/* ── the window ────────────────────────────────────────────────────────── */

#define WIN_W 680                       /* client size, at 96 dpi */
#define WIN_H 440
#define X0    36

#define C_BONE     RGB(0xEC, 0xE4, 0xD4)
#define C_DIM      RGB(0x95, 0x8A, 0x7C)
#define C_FAINT    RGB(0x5E, 0x52, 0x4C)
#define C_BLOOD    RGB(0xB0, 0x12, 0x1E)
#define C_BLOOD_HI RGB(0xD9, 0x22, 0x30)
#define C_BLOOD_LO RGB(0x4A, 0x10, 0x16)
#define C_PANEL    RGB(0x18, 0x10, 0x13)
#define C_EDGE     RGB(0x45, 0x30, 0x34)
#define C_ERROR    RGB(0xFF, 0x8A, 0x70)

enum { VIEW_INSTALL, VIEW_PLAY };
enum { B_NONE, B_IMAGE, B_TARGET, B_INSTALL, B_CANCEL, B_BACK, B_PLAY, B_REPORTS, B_FOLDER, B_REINSTALL };
enum { K_PRIMARY, K_SECOND, K_LINK };

typedef struct {
    int id, kind, x, y, w, h, enabled;      /* 96-dpi units */
    const WCHAR *text;
} Btn;

static Btn    s_btn[12];
static int    s_nbtn, s_hot, s_down, s_focus;
static int    s_view, s_installing, s_permille, s_image_ok, s_status_err, s_close_after;
static WCHAR  s_status[1024], s_image_note[256];
static HANDLE s_game_proc;
static HFONT  s_f_title, s_f_sub, s_f_ui, s_f_bold, s_f_label, s_f_big, s_f_play;

static int game_running(void)
{
    return s_game_proc && WaitForSingleObject(s_game_proc, 0) == WAIT_TIMEOUT;
}

static void set_status(int err, const WCHAR *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vswprintf_s(s_status, 1024, fmt, ap);
    va_end(ap);
    s_status_err = err;
    if (s_wnd)
        InvalidateRect(s_wnd, NULL, FALSE);
}

static void add_btn(int id, int kind, int x, int y, int w, int h, int enabled, const WCHAR *text)
{
    Btn *b = &s_btn[s_nbtn++];
    b->id = id; b->kind = kind; b->x = x; b->y = y; b->w = w; b->h = h; b->enabled = enabled; b->text = text;
}

static Btn *btn(int id)
{
    int i;
    for (i = 0; i < s_nbtn; i++)
        if (s_btn[i].id == id)
            return &s_btn[i];
    return NULL;
}

/* The buttons for what is on screen now. */
static void layout(void)
{
    Btn *f;
    s_nbtn = 0;
    if (s_view == VIEW_INSTALL) {
        if (s_installing) {
            add_btn(B_CANCEL, K_SECOND, WIN_W - X0 - 96, 290, 96, 34, !s_cancel, L"Cancel");
        } else {
            add_btn(B_IMAGE, K_SECOND, WIN_W - X0 - 96, 158, 96, 34, 1, L"Browse...");
            add_btn(B_TARGET, K_SECOND, WIN_W - X0 - 96, 228, 96, 34, 1, L"Browse...");
            add_btn(B_INSTALL, K_PRIMARY, X0, 284, 200, 46, s_image[0] && s_image_ok && s_target[0], L"INSTALL");
            if (is_game_folder(s_game_dir))
                add_btn(B_BACK, K_LINK, X0, WIN_H - 36, 80, 22, 1, L"< Back");
        }
    } else {
        add_btn(B_PLAY, K_PRIMARY, X0, 156, 280, 76, has_program(s_game_dir) && !game_running(), L"PLAY");
        add_btn(B_REPORTS, K_LINK, X0, WIN_H - 36, 108, 22, 1, L"Reports folder");
        add_btn(B_FOLDER, K_LINK, X0 + 124, WIN_H - 36, 92, 22, 1, L"Game folder");
        add_btn(B_REINSTALL, K_LINK, X0 + 232, WIN_H - 36, 96, 22, !game_running(), L"Install again");
    }
    f = btn(s_focus);
    if (!f || !f->enabled) {
        f = btn(s_view == VIEW_PLAY ? B_PLAY : (s_installing ? B_CANCEL : B_INSTALL));
        if (!f || !f->enabled)
            f = btn(s_view == VIEW_PLAY ? B_REPORTS : B_IMAGE);
        s_focus = f ? f->id : B_NONE;
    }
    if (s_wnd)
        InvalidateRect(s_wnd, NULL, FALSE);
}

static void fill(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    RECT r;
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    SetDCBrushColor(dc, c);
    FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}

static void edge(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    RECT r;
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    SetDCBrushColor(dc, c);
    FrameRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}

static void text(HDC dc, HFONT f, COLORREF c, int x, int y, int w, int h, UINT fmt, const WCHAR *s)
{
    RECT r;
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, fmt | DT_NOPREFIX);
}

static void draw_btn(HDC dc, const Btn *b)
{
    int x = S(b->x), y = S(b->y), w = S(b->w), h = S(b->h);
    int hot = b->enabled && s_hot == b->id, down = hot && s_down == b->id;
    if (b->kind == K_LINK) {
        HFONT f = s_f_ui;
        text(dc, f, !b->enabled ? C_FAINT : hot ? C_BONE : C_DIM, x, y, w, h, DT_LEFT | DT_VCENTER | DT_SINGLELINE, b->text);
        if (s_focus == b->id || hot)
            fill(dc, x, y + h - S(2), w - S(8), 1, hot ? C_BLOOD_HI : C_FAINT);
        return;
    }
    if (b->kind == K_PRIMARY) {
        fill(dc, x, y, w, h, !b->enabled ? C_BLOOD_LO : down ? C_BLOOD_LO : hot ? C_BLOOD_HI : C_BLOOD);
        edge(dc, x, y, w, h, !b->enabled ? C_EDGE : C_BLOOD_HI);
        SetTextCharacterExtra(dc, S(3));
        text(dc, b->id == B_PLAY ? s_f_play : s_f_big, b->enabled ? C_BONE : C_FAINT, x + S(3), y, w, h,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE, b->text);
        SetTextCharacterExtra(dc, 0);
    } else {
        fill(dc, x, y, w, h, down ? C_BLOOD_LO : C_PANEL);
        edge(dc, x, y, w, h, !b->enabled ? C_PANEL : hot ? C_BLOOD_HI : C_EDGE);
        text(dc, s_f_ui, b->enabled ? C_BONE : C_FAINT, x, y, w, h, DT_CENTER | DT_VCENTER | DT_SINGLELINE, b->text);
    }
    if (s_focus == b->id && b->enabled)
        edge(dc, x - S(3), y - S(3), w + S(6), h + S(6), C_DIM);
}

/* A path in its box, or a hint when nothing is chosen. */
static void draw_path(HDC dc, int y, const WCHAR *label, const WCHAR *path, const WCHAR *hint)
{
    int w = WIN_W - 2 * X0 - 96 - 10;
    SetTextCharacterExtra(dc, S(1));
    text(dc, s_f_label, C_DIM, S(X0), S(y - 22), S(400), S(18), DT_LEFT | DT_SINGLELINE, label);
    SetTextCharacterExtra(dc, 0);
    fill(dc, S(X0), S(y), S(w), S(34), C_PANEL);
    edge(dc, S(X0), S(y), S(w), S(34), C_EDGE);
    text(dc, s_f_ui, path[0] ? C_BONE : C_FAINT, S(X0 + 12), S(y), S(w - 24), S(34),
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS, path[0] ? path : hint);
}

static void paint(HDC dc)
{
    int W = S(WIN_W), H = S(WIN_H), y, i, tx = S(X0);
    WCHAR buf[MAX_PATH + 64];

    /* night to blood, top to bottom */
    for (y = 0; y < H; y++) {
        int t = y * 255 / H;
        fill(dc, 0, y, W, 1, RGB(0x08 + t * 0x1C / 255, 0x06 - t * 0x02 / 255, 0x0A - t * 0x03 / 255));
    }
    SetBkMode(dc, TRANSPARENT);

    /* header */
    if (s_have_icon) {
        BITMAPINFO bi;
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = 128;
        bi.bmiHeader.biHeight = -128;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, NULL);
        StretchDIBits(dc, S(X0), S(26), S(72), S(72), 0, 0, 128, 128, s_icon_px, &bi, DIB_RGB_COLORS, SRCCOPY);
        edge(dc, S(X0) - 1, S(26) - 1, S(72) + 2, S(72) + 2, C_EDGE);
        tx = S(X0 + 72 + 20);
    }
    SetTextCharacterExtra(dc, S(6));
    text(dc, s_f_title, C_BONE, tx, S(22), W, S(50), DT_LEFT | DT_SINGLELINE, L"BUFFY");
    SetTextCharacterExtra(dc, S(5));
    text(dc, s_f_sub, C_BLOOD_HI, tx + S(2), S(72), W, S(24), DT_LEFT | DT_SINGLELINE, L"THE VAMPIRE SLAYER");
    SetTextCharacterExtra(dc, 0);
    fill(dc, S(X0), S(114), W - 2 * S(X0), 1, C_BLOOD);
    fill(dc, S(X0), S(114) + 1, W - 2 * S(X0), 1, C_BLOOD_LO);

    if (s_view == VIEW_INSTALL) {
        draw_path(dc, 158, L"1    YOUR XBOX DISC IMAGE", s_image, L"Choose the .iso of your own Buffy the Vampire Slayer disc");
        draw_path(dc, 228, L"2    INSTALL TO", s_target, L"Choose a folder for the game (about 2 GB)");
        if (s_installing) {
            int bw = WIN_W - 2 * X0 - 96 - 10;
            fill(dc, S(X0), S(298), S(bw), S(18), C_PANEL);
            edge(dc, S(X0), S(298), S(bw), S(18), C_EDGE);
            fill(dc, S(X0) + 2, S(298) + 2, (S(bw) - 4) * s_permille / 1000, S(18) - 4, C_BLOOD);
        }
        text(dc, s_f_ui, s_status_err ? C_ERROR : C_DIM, S(X0), S(346), W - 2 * S(X0), S(52),
             DT_LEFT | DT_WORDBREAK, s_status[0] ? s_status : s_image_note);
    } else {
        text(dc, s_f_ui, s_status_err ? C_ERROR : C_BONE, S(X0), S(254), W - 2 * S(X0), S(56),
             DT_LEFT | DT_WORDBREAK, s_status);
        SetTextCharacterExtra(dc, S(1));
        text(dc, s_f_label, C_DIM, S(X0), S(326), S(300), S(18), DT_LEFT | DT_SINGLELINE, L"INSTALLED IN");
        SetTextCharacterExtra(dc, 0);
        text(dc, s_f_ui, C_DIM, S(X0), S(344), W - 2 * S(X0), S(20), DT_LEFT | DT_SINGLELINE | DT_PATH_ELLIPSIS, s_game_dir);
        text(dc, s_f_ui, C_FAINT, S(X0 + 300), S(170), S(WIN_W - X0 - X0 - 300), S(60), DT_LEFT | DT_WORDBREAK,
             L"Graphics, sound and controls are set in the game's own Options menu.");
    }
    for (i = 0; i < s_nbtn; i++)
        draw_btn(dc, &s_btn[i]);
    swprintf_s(buf, _countof(buf), L"PC port  v%s", WIDEN(BTVS_VERSION));
    text(dc, s_f_ui, C_FAINT, W - S(X0) - S(200), S(WIN_H - 36), S(200), S(22), DT_RIGHT | DT_VCENTER | DT_SINGLELINE, buf);
}

static HFONT mkfont(const WCHAR *face, int pt, int weight)
{
    return CreateFontW(-MulDiv(pt, s_dpi, 72), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
}

static void make_fonts(void)
{
    HFONT *all[] = { &s_f_title, &s_f_sub, &s_f_ui, &s_f_bold, &s_f_label, &s_f_big, &s_f_play };
    int i;
    for (i = 0; i < (int)_countof(all); i++)
        if (*all[i])
            DeleteObject(*all[i]);
    s_f_title = mkfont(L"Georgia", 32, FW_BOLD);
    s_f_sub = mkfont(L"Georgia", 11, FW_BOLD);
    s_f_ui = mkfont(L"Segoe UI", 10, FW_NORMAL);
    s_f_bold = mkfont(L"Segoe UI", 10, FW_SEMIBOLD);
    s_f_label = mkfont(L"Segoe UI", 8, FW_BOLD);
    s_f_big = mkfont(L"Georgia", 14, FW_BOLD);
    s_f_play = mkfont(L"Georgia", 24, FW_BOLD);
}

/* ── choosing the image and the folder ─────────────────────────────────── */

static void set_view(int v);

static void set_image(const WCHAR *path)
{
    Disc d;
    XbeInfo info;
    WCHAR err[512];
    wcscpy_s(s_image, MAX_PATH, path);
    s_image_ok = 0;
    s_image_note[0] = 0;
    if (!s_image[0]) {
        set_status(0, L"");
    } else if (disc_open(&d, s_image, err, 512) && disc_check(&d, &info, err, 512)) {
        s_image_ok = 1;
        swprintf_s(s_image_note, 256, L"That is " GAME_TITLE L", the " WIDEN(BTVS_RELEASE_NAME) L": %d files, %.2f GB.",
                   d.count, d.total / 1073741824.0);
        set_status(0, L"");
    } else {
        set_status(1, L"%s", err);
    }
    if (s_image[0])
        disc_close(&d);
    layout();
}

/* A folder with other things in it gets a subfolder for the game. */
static void set_target(const WCHAR *path)
{
    WCHAR full[MAX_PATH];
    if (!GetFullPathNameW(path, MAX_PATH, full, NULL))
        wcscpy_s(full, MAX_PATH, path);
    if (dir_exists(full) && !dir_is_empty(full) && !is_game_folder(full) && _wcsicmp(full, s_launcher_dir)
            && wcslen(full) + 32 < MAX_PATH)
        join(s_target, full, GAME_TITLE);
    else
        wcscpy_s(s_target, MAX_PATH, full);
    layout();
}

static int pick_folder(const WCHAR *title, WCHAR *out)
{
    IFileOpenDialog *d;
    IShellItem *it;
    PWSTR p = NULL;
    int ok = 0;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void **)&d)))
        return 0;
    d->lpVtbl->SetOptions(d, FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    d->lpVtbl->SetTitle(d, title);
    if (SUCCEEDED(d->lpVtbl->Show(d, s_wnd)) && SUCCEEDED(d->lpVtbl->GetResult(d, &it))) {
        if (SUCCEEDED(it->lpVtbl->GetDisplayName(it, SIGDN_FILESYSPATH, &p))) {
            wcscpy_s(out, MAX_PATH, p);
            CoTaskMemFree(p);
            ok = 1;
        }
        it->lpVtbl->Release(it);
    }
    d->lpVtbl->Release(d);
    return ok;
}

static int pick_image(WCHAR *out)
{
    OPENFILENAMEW of;
    WCHAR buf[MAX_PATH] = L"";
    memset(&of, 0, sizeof of);
    of.lStructSize = sizeof of;
    of.hwndOwner = s_wnd;
    of.lpstrFilter = L"Xbox disc images (*.iso, *.xiso)\0*.iso;*.xiso\0All files\0*.*\0";
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH;
    of.lpstrTitle = L"Choose your " GAME_TITLE L" disc image";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&of))
        return 0;
    wcscpy_s(out, MAX_PATH, buf);
    return 1;
}

/* ── actions ───────────────────────────────────────────────────────────── */

static void refresh_play(void)
{
    load_game_icon();
    if (!has_program(s_game_dir))
        set_status(1, L"The game's files are here, but " GAME_EXE L" is not. Copy it into the game folder, "
                      L"or unpack the whole release and choose Install again.");
    else if (game_running())
        set_status(0, L"The game is running.");
    else if (!s_status[0] || s_status_err)
        set_status(0, L"Ready.");
}

static void set_view(int v)
{
    s_view = v;
    s_focus = B_NONE;
    if (v == VIEW_PLAY) {
        refresh_play();
    } else {
        set_status(0, L"");
        if (!s_target[0])
            wcscpy_s(s_target, MAX_PATH, s_game_dir[0] ? s_game_dir : L"");
    }
    layout();
}

static void start_install(void)
{
    if (s_installing || !s_image[0] || !s_target[0])
        return;
    if (game_running()) {
        set_status(1, L"Close the game before installing.");
        return;
    }
    wcscpy_s(s_job.image, MAX_PATH, s_image);
    wcscpy_s(s_job.target, MAX_PATH, s_target);
    s_cancel = 0;
    s_installing = 1;
    s_permille = 0;
    set_status(0, L"Reading the disc image...");
    layout();
    CloseHandle(CreateThread(NULL, 0, install_thread, NULL, 0, NULL));
}

static DWORD WINAPI game_watch(LPVOID p)
{
    HANDLE h = (HANDLE)p;
    DWORD code = 0;
    WaitForSingleObject(h, INFINITE);
    GetExitCodeProcess(h, &code);
    CloseHandle(h);
    PostMessageW(s_wnd, WM_APP_GAMEEND, code, 0);
    return 0;
}

/* Start the game: buffy_tvs.exe of the game folder, with the game folder as
 * its working directory. Used by the Play button and by --play. */
static BOOL start_game(const WCHAR *dir, PROCESS_INFORMATION *pi)
{
    WCHAR exe[MAX_PATH], cmd[MAX_PATH + 4];
    STARTUPINFOW si;
    join(exe, dir, GAME_EXE);
    swprintf_s(cmd, MAX_PATH + 4, L"\"%s\"", exe);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    {
        /* The game gets a real stdout and stderr: btvs_console.txt in the game
         * folder, new at each start. Started with none at all (as a window
         * program is), the 21 Nov build ended itself a second into the boot
         * (exit code 0xC0000409, seen twice); with them it runs. The file
         * also keeps what the runtime prints, for problem reports. */
        SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
        WCHAR out[MAX_PATH];
        HANDLE hout, hin;
        BOOL ok;
        join(out, dir, CONSOLE_FILE);
        hout = CreateFileW(out, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hout == INVALID_HANDLE_VALUE)       /* a folder that cannot be written: nowhere, but still real handles */
            hout = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
        hin = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
        if (hout != INVALID_HANDLE_VALUE && hin != INVALID_HANDLE_VALUE
                && !_wgetenv(L"BTVS_LAUNCHER_NO_CONSOLE")) {    /* (testing: start it as Explorer would) */
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdInput = hin;
            si.hStdOutput = hout;
            si.hStdError = hout;
        }
        ok = CreateProcessW(exe, cmd, NULL, NULL, si.dwFlags ? TRUE : FALSE, 0, NULL, dir, &si, pi);
        if (hout != INVALID_HANDLE_VALUE) CloseHandle(hout);
        if (hin != INVALID_HANDLE_VALUE) CloseHandle(hin);
        return ok;
    }
}

static void play(void)
{
    PROCESS_INFORMATION pi;
    HANDLE dup;
    if (game_running())
        return;
    if (!start_game(s_game_dir, &pi)) {
        set_status(1, L"Windows could not start the game (error %lu).", GetLastError());
        return;
    }
    CloseHandle(pi.hThread);
    if (s_game_proc)
        CloseHandle(s_game_proc);
    s_game_proc = pi.hProcess;
    if (DuplicateHandle(GetCurrentProcess(), pi.hProcess, GetCurrentProcess(), &dup,
                        SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, 0))
        CloseHandle(CreateThread(NULL, 0, game_watch, dup, 0, NULL));
    set_status(0, L"The game is running.");
    layout();
}

static void open_folder(const WCHAR *sub)
{
    WCHAR p[MAX_PATH];
    if (sub) {
        join(p, s_game_dir, sub);
        CreateDirectoryW(p, NULL);
    } else {
        wcscpy_s(p, MAX_PATH, s_game_dir);
    }
    ShellExecuteW(s_wnd, L"open", p, NULL, NULL, SW_SHOWNORMAL);
}

static void action(int id)
{
    WCHAR p[MAX_PATH];
    Btn *b = btn(id);
    if (!b || !b->enabled)
        return;
    switch (id) {
    case B_IMAGE:
        if (pick_image(p))
            set_image(p);
        break;
    case B_TARGET:
        if (pick_folder(L"Choose where to install " GAME_TITLE, p))
            set_target(p);
        break;
    case B_INSTALL:   start_install(); break;
    case B_CANCEL:
        s_cancel = 1;
        set_status(0, L"Cancelling...");
        layout();
        break;
    case B_BACK:      set_view(VIEW_PLAY); break;
    case B_PLAY:      play(); break;
    case B_REPORTS:   open_folder(REPORTS_DIR); break;
    case B_FOLDER:    open_folder(NULL); break;
    case B_REINSTALL:
        wcscpy_s(s_target, MAX_PATH, s_game_dir);
        set_view(VIEW_INSTALL);
        break;
    }
}

static int hit(int px, int py)
{
    int i;
    for (i = 0; i < s_nbtn; i++) {
        const Btn *b = &s_btn[i];
        if (b->enabled && px >= S(b->x) && px < S(b->x + b->w) && py >= S(b->y) && py < S(b->y + b->h))
            return b->id;
    }
    return B_NONE;
}

static void focus_next(int dir)
{
    int i, at = 0, k;
    if (!s_nbtn)
        return;
    for (i = 0; i < s_nbtn; i++)
        if (s_btn[i].id == s_focus)
            at = i;
    for (k = 0; k < s_nbtn; k++) {
        at = (at + dir + s_nbtn) % s_nbtn;
        if (s_btn[at].enabled) {
            s_focus = s_btn[at].id;
            break;
        }
    }
    InvalidateRect(s_wnd, NULL, FALSE);
}

static LRESULT CALLBACK wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps), mdc = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, S(WIN_W), S(WIN_H)), old = (HBITMAP)SelectObject(mdc, bmp);
        paint(mdc);
        BitBlt(dc, 0, 0, S(WIN_W), S(WIN_H), mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, old);
        DeleteObject(bmp);
        DeleteDC(mdc);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int h = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        TRACKMOUSEEVENT t;
        if (h != s_hot) {
            s_hot = h;
            InvalidateRect(w, NULL, FALSE);
        }
        t.cbSize = sizeof t; t.dwFlags = TME_LEAVE; t.hwndTrack = w; t.dwHoverTime = 0;
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (s_hot) {
            s_hot = B_NONE;
            InvalidateRect(w, NULL, FALSE);
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && s_hot) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return 1;
        }
        break;
    case WM_LBUTTONDOWN:
        s_down = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (s_down) {
            s_focus = s_down;
            SetCapture(w);
        }
        InvalidateRect(w, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        int was = s_down, h = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        s_down = B_NONE;
        ReleaseCapture();
        InvalidateRect(w, NULL, FALSE);
        if (was && h == was)
            action(was);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_TAB || wp == VK_RIGHT || wp == VK_DOWN || wp == VK_LEFT || wp == VK_UP)
            focus_next((wp == VK_LEFT || wp == VK_UP || (wp == VK_TAB && GetKeyState(VK_SHIFT) < 0)) ? -1 : 1);
        else if (wp == VK_RETURN || wp == VK_SPACE)
            action(s_focus);
        else if (wp == VK_ESCAPE)
            PostMessageW(w, WM_CLOSE, 0, 0);
        return 0;
    case WM_APP_PROGRESS:
        s_permille = (int)wp;
        if (lp) {
            if (!s_cancel)
                set_status(0, L"%s", (WCHAR *)lp);
            free((void *)lp);
        }
        InvalidateRect(w, NULL, FALSE);
        return 0;
    case WM_APP_DONE: {
        WCHAR *msg = (WCHAR *)lp;
        s_installing = 0;
        if (s_close_after) {
            free(msg);
            DestroyWindow(w);
            return 0;
        }
        if (wp == INSTALL_FAILED) {
            set_status(1, L"%s", msg && msg[0] ? msg : L"The installation failed.");
            layout();
        } else {
            GetFullPathNameW(s_job.target, MAX_PATH, s_game_dir, NULL);
            save_launcher_ini();
            s_have_icon = 0;
            set_status(0, L"Installed. Press Play.");
            set_view(VIEW_PLAY);
        }
        free(msg);
        return 0;
    }
    case WM_APP_GAMEEND:
        if (wp)
            set_status(1, L"The game closed with an error (code %08X). What happened is in btvs_log.txt and "
                          L"btvs_console.txt in the game folder, and in its crashes folder if it wrote a report.", (unsigned)wp);
        else
            set_status(0, L"Ready.");
        layout();
        return 0;
    case WM_DPICHANGED: {
        const RECT *r = (const RECT *)lp;
        s_dpi = HIWORD(wp);
        make_fonts();
        SetWindowPos(w, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(w, NULL, FALSE);
        return 0;
    }
    case WM_CLOSE:
        if (s_installing) {
            if (MessageBoxW(w, L"The game is still being installed. Stop and close?", GAME_TITLE,
                            MB_YESNO | MB_ICONQUESTION) != IDYES)
                return 0;
            if (s_installing) {
                s_cancel = 1;
                s_close_after = 1;
                return 0;
            }
        }
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        save_launcher_ini();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

/* ── without a window ──────────────────────────────────────────────────── */

/* --capture: the window's picture as a 24-bit BMP, drawn off screen (the
 * window is never shown). */
static int capture(const WCHAR *file)
{
    HDC sdc = GetDC(NULL), mdc = CreateCompatibleDC(sdc);
    int w = S(WIN_W), h = S(WIN_H), stride = (w * 3 + 3) & ~3, ok = 0;
    HBITMAP bmp = CreateCompatibleBitmap(sdc, w, h), old = (HBITMAP)SelectObject(mdc, bmp);
    BITMAPINFOHEADER bi;
    BITMAPFILEHEADER fh;
    uint8_t *bits = (uint8_t *)malloc((size_t)stride * h);
    FILE *f = NULL;
    paint(mdc);
    SelectObject(mdc, old);
    memset(&bi, 0, sizeof bi);
    bi.biSize = sizeof bi; bi.biWidth = w; bi.biHeight = h; bi.biPlanes = 1; bi.biBitCount = 24;
    GetDIBits(mdc, bmp, 0, (UINT)h, bits, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    memset(&fh, 0, sizeof fh);
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof bi;
    fh.bfSize = fh.bfOffBits + (DWORD)(stride * h);
    if (!_wfopen_s(&f, file, L"wb") && f) {
        fwrite(&fh, sizeof fh, 1, f);
        fwrite(&bi, sizeof bi, 1, f);
        fwrite(bits, (size_t)stride * h, 1, f);
        fclose(f);
        ok = 1;
    }
    free(bits);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(NULL, sdc);
    return ok;
}

/* A console's output for the command-line modes when started from one. */
static void attach_console(void)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((h == NULL || h == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *f;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
}

static int identify(const WCHAR *what)
{
    XbeInfo info;
    WCHAR err[512] = L"", p[MAX_PATH];
    int ok = 0, known = 0;
    if (dir_exists(what) || (wcslen(what) > 4 && !_wcsicmp(what + wcslen(what) - 4, L".xbe"))) {
        size_t n = 0;
        uint8_t *x;
        if (dir_exists(what))
            join(p, what, L"Default.xbe");
        else
            wcscpy_s(p, MAX_PATH, what);
        x = read_whole_file(p, &n);
        known = x && xbe_info(x, n, &info);
        if (!known)
            swprintf_s(err, 512, L"No readable Default.xbe at %s.", p);
        else
            ok = release_supported(&info, err, 512);
        free(x);
    } else {
        Disc d;
        if (disc_open(&d, what, err, 512)) {
            ok = disc_check(&d, &info, err, 512);
            known = info.size != 0;
            wprintf(L"image: %d files, %llu bytes, game partition at 0x%llX\n", d.count,
                    (unsigned long long)d.total, (unsigned long long)d.base);
        }
        disc_close(&d);
    }
    if (known)
        wprintf(L"Default.xbe: title %08X, region %X, version %u, %u bytes\nsha256 %hs\n",
                info.title_id, info.region, info.version, info.size, info.sha256);
    wprintf(L"%s\n", ok ? L"supported: the " WIDEN(BTVS_RELEASE_NAME) : err);
    return ok ? 0 : 1;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show)
{
    WNDCLASSEXW wc;
    MSG msg;
    RECT r;
    BOOL dark = TRUE;
    int argc = 0;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc), *slash;
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    (void)prev; (void)cmdline;

    s_inst = inst;
    GetModuleFileNameW(NULL, s_launcher_dir, MAX_PATH);
    slash = wcsrchr(s_launcher_dir, L'\\');
    if (slash)
        *slash = 0;
    join(s_launcher_ini, s_launcher_dir, L"launcher.ini");

    if (argc >= 2 && !wcsncmp(argv[1], L"--", 2) && wcscmp(argv[1], L"--capture")) {
        attach_console();
        if (!wcscmp(argv[1], L"--version")) {
            wprintf(L"%s launcher %s (for the " WIDEN(BTVS_RELEASE_NAME) L")\n", GAME_TITLE, WIDEN(BTVS_VERSION));
            return 0;
        }
        if (argc >= 4 && !wcscmp(argv[1], L"--install")) {
            int res;
            s_quiet = argc >= 5 && !wcscmp(argv[4], L"--quiet");
            res = install_run(argv[2], argv[3]);
            if (res == INSTALL_OK)
                wprintf(L"installed\n");
            else
                fwprintf(stderr, L"%s: %s\n", res == INSTALL_NO_PROGRAM ? L"game files installed" : L"failed", s_install_msg);
            return res == INSTALL_OK ? 0 : res == INSTALL_NO_PROGRAM ? 2 : 1;
        }
        if (!wcscmp(argv[1], L"--play")) {
            /* What the Play button does, without the window: the game folder
             * is found the same way (launcher.ini, else the launcher's own
             * folder) unless one is given. The game inherits the environment,
             * so a test's BTVS_NO_WINDOW / BTVS_MUTE reach it. */
            PROCESS_INFORMATION pi;
            DWORD code = 0;
            if (argc >= 3)
                GetFullPathNameW(argv[2], MAX_PATH, s_game_dir, NULL);
            else
                load_launcher_ini();
            if (!is_game_folder(s_game_dir) || !has_program(s_game_dir)) {
                fwprintf(stderr, L"no installed game%s%s\r\n", s_game_dir[0] ? L" in " : L"", s_game_dir);
                return 1;
            }
            if (!start_game(s_game_dir, &pi)) {
                fwprintf(stderr, L"Windows could not start the game (error %lu)\r\n", GetLastError());
                return 1;
            }
            wprintf(L"started %s\\%s (process %lu) in %s\r\n", s_game_dir, GAME_EXE, pi.dwProcessId, s_game_dir);
            fflush(stdout);
            WaitForSingleObject(pi.hProcess, INFINITE);
            GetExitCodeProcess(pi.hProcess, &code);
            wprintf(L"the game ended with code %lu\r\n", code);
            return (int)code;
        }
        if (argc >= 3 && !wcscmp(argv[1], L"--identify"))
            return identify(argv[2]);
        if (argc >= 3 && !wcscmp(argv[1], L"--list")) {
            Disc d;
            WCHAR err[512];
            int i;
            if (!disc_open(&d, argv[2], err, 512)) {
                fwprintf(stderr, L"%s\n", err);
                disc_close(&d);
                return 1;
            }
            for (i = 0; i < d.count; i++)
                wprintf(L"%u\t%s\n", d.files[i].size, d.files[i].path);
            disc_close(&d);
            return 0;
        }
        fwprintf(stderr, L"usage: --install <image> <folder> [--quiet] | --play [game folder] | --identify <image|folder|xbe> | --list <image> "
                         L"| --capture <install|busy|error|play> <out.bmp> [game folder] | --version\n");
        return 1;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    load_launcher_ini();
    s_dpi = (int)GetDpiForSystem();
    make_fonts();

    if (argc >= 4 && !wcscmp(argv[1], L"--capture")) {
        if (argc >= 5)
            wcscpy_s(s_game_dir, MAX_PATH, argv[4]);
        else
            s_game_dir[0] = 0;
        load_game_icon();
        if (!wcscmp(argv[2], L"play")) {
            s_view = VIEW_PLAY;
            refresh_play();
        } else {
            s_view = VIEW_INSTALL;
            wcscpy_s(s_image, MAX_PATH, L"D:\\Discs\\Buffy the Vampire Slayer.iso");
            wcscpy_s(s_target, MAX_PATH, L"D:\\Games\\Buffy the Vampire Slayer");
            s_image_ok = 1;
            wcscpy_s(s_image_note, 256, L"That is " GAME_TITLE L", the " WIDEN(BTVS_RELEASE_NAME) L": 152 files, 1.87 GB.");
            if (!wcscmp(argv[2], L"busy")) {
                s_installing = 1;
                s_permille = 420;
                set_status(0, L"Copying the game from your disc image: 0.79 of 1.87 GB");
            } else if (!wcscmp(argv[2], L"error")) {
                XbeInfo other;
                memset(&other, 0, sizeof other);
                other.title_id = BTVS_TITLE_ID; other.region = 1; other.version = 1;
                s_image_ok = 0;
                release_supported(&other, s_status, 1024);
                s_status_err = 1;
            }
        }
        layout();
        return capture(argv[3]) ? 0 : 1;
    }

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"BtvsLauncher";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    r.left = 0; r.top = 0; r.right = S(WIN_W); r.bottom = S(WIN_H);
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, (UINT)s_dpi);
    s_wnd = CreateWindowExW(0, wc.lpszClassName, GAME_TITLE, style, CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    if (!s_wnd)
        return 1;
    DwmSetWindowAttribute(s_wnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);

    /* installed already: straight to Play */
    if (is_game_folder(s_game_dir)) {
        set_view(VIEW_PLAY);
    } else {
        if (s_image[0])
            set_image(s_image);
        set_view(VIEW_INSTALL);
    }
    ShowWindow(s_wnd, show);
    UpdateWindow(s_wnd);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
