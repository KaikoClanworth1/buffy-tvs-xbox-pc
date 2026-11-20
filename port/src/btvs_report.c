/* Report a Problem.
 *
 * btvs_report_create(text) writes, beside the exe,
 *
 *   reports\<yyyymmdd-hhmmss>\        and  reports\<yyyymmdd-hhmmss>.zip
 *     report.txt        the player's words, version, system, where in the game,
 *                       the last trail events, memory, where the game thread is
 *     report.json       the same as data (the shape an upload endpoint takes:
 *                       docs/logs.md). "sent": false - nothing is sent anywhere
 *     replay.txt        what BTVS_REPLAY needs: the anchor to go to
 *     screenshot.png    the game picture (from before the menu opened)
 *     btvs_log.txt, btvs_log_previous.txt
 *     btvs_trail.txt, btvs_trail_previous.txt
 *     input.rec         the input recording since the last anchor
 *     settings.ini      btvs_settings.ini
 *     save\             the Xbox save drives (XboxData) as they are now
 *     crashes\          the newest crash / hang reports of this install
 *     memory.txt
 *
 * The in-game page (Menu area) calls btvs_report_prepare() when the pause
 * menu opens and btvs_report_create() when the player confirms. The hotkey
 * (F12, or a click of the left stick; btvs_settings.ini [report] hotkey=
 * 0 off, 1 both (default), 2 F12 only) is watched by a thread of its own, so
 * a report can be made while the game is frozen.
 *
 * Test aids: BTVS_TEST_REPORT_AT=<seconds> makes a report then (with
 * BTVS_TEST_REPORT_TEXT as the player's words). A test run never reads the
 * real keyboard or pads and never beeps.
 */
#include <windows.h>
#include <xinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btvs_log.h"

void xbox_WatchdogReportTo(void *out, const char *why);
long xbox_HangMonitorFrames(void);
unsigned long xbox_HangMonitorIdleMs(void);

/* Until Graphics defines btvs_screenshot: no picture, and the report says so. */
int btvs_screenshot_none(const char *path) { (void)path; return 0; }
int btvs_screenshot_count_none(void) { return 0; }
#pragma comment(linker, "/alternatename:btvs_screenshot=btvs_screenshot_none")
#pragma comment(linker, "/alternatename:btvs_screenshot_count=btvs_screenshot_count_none")

static CRITICAL_SECTION s_cs;
static INIT_ONCE s_once = INIT_ONCE_STATIC_INIT;
static char s_last[MAX_PATH];
static char s_pending[MAX_PATH];
static DWORD s_pending_tick;

static BOOL CALLBACK init_once(PINIT_ONCE o, PVOID p, PVOID *c)
{
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&s_cs);
    return TRUE;
}

/* Graphics writes the picture at the next frame it shows: ask, then wait for
 * it (2 s at most: a frozen game shows no next frame, and the report must
 * still be made). */
static int file_exists(const char *path);
static int take_screenshot(const char *path)
{
    int before = btvs_screenshot_count(), i;
    DeleteFileA(path);
    if (!btvs_screenshot(path))
        return 0;
    for (i = 0; i < 100; i++) {
        if (btvs_screenshot_count() != before && file_exists(path))
            return 1;
        Sleep(20);
    }
    return file_exists(path);
}

/* ── Files ──────────────────────────────────────────────────────────────── */

static int file_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int copy_into(const char *dir, const char *src, const char *name)
{
    char dst[MAX_PATH];
    sprintf_s(dst, MAX_PATH, "%s\\%s", dir, name);
    return file_exists(src) && CopyFileA(src, dst, FALSE);
}

static void make_dirs(const char *path)
{
    char tmp[MAX_PATH];
    char *p;
    strcpy_s(tmp, MAX_PATH, path);
    for (p = tmp + 3; *p; p++)
        if (*p == '\\' || *p == '/') {
            char c = *p;
            *p = 0;
            CreateDirectoryA(tmp, NULL);
            *p = c;
        }
    CreateDirectoryA(tmp, NULL);
}

int btvs_copy_tree(const char *from, const char *to, unsigned long long max_bytes, unsigned long long *copied)
{
    char pat[MAX_PATH], a[MAX_PATH], b[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int n = 0;

    sprintf_s(pat, MAX_PATH, "%s\\*", from);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    make_dirs(to);
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
            continue;
        if (_snprintf_s(a, MAX_PATH, _TRUNCATE, "%s\\%s", from, fd.cFileName) < 0
                || _snprintf_s(b, MAX_PATH, _TRUNCATE, "%s\\%s", to, fd.cFileName) < 0)
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            n += btvs_copy_tree(a, b, max_bytes, copied);
        } else {
            unsigned long long size = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            if (copied && *copied + size > max_bytes)
                continue;
            if (CopyFileA(a, b, FALSE)) {
                n++;
                if (copied)
                    *copied += size;
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

/* ── Zip (stored, no compression: nothing to link, and any tool opens it) ── */

static uint32_t s_crc_table[256];

static uint32_t crc32_update(uint32_t crc, const unsigned char *p, size_t n)
{
    size_t i;
    if (!s_crc_table[1]) {
        uint32_t c, k, j;
        for (k = 0; k < 256; k++) {
            for (c = k, j = 0; j < 8; j++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            s_crc_table[k] = c;
        }
    }
    crc = ~crc;
    for (i = 0; i < n; i++)
        crc = s_crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

typedef struct {
    FILE *f;
    char *central;
    size_t central_len, central_cap;
    unsigned count;
    unsigned short dos_time, dos_date;
} zip_t;

static void w16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void w32(unsigned char *p, uint32_t v) { w16(p, v & 0xFFFF); w16(p + 2, v >> 16); }
static unsigned r16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static uint32_t r32(const unsigned char *p) { return r16(p) | ((uint32_t)r16(p + 2) << 16); }

static void zip_add(zip_t *z, const char *path, const char *name)
{
    static unsigned char buf[65536];
    unsigned char h[30], c[46];
    FILE *in;
    uint32_t crc = 0, size = 0;
    long long at = _ftelli64(z->f), end;
    size_t nlen = strlen(name), n;

    if (fopen_s(&in, path, "rb") != 0 || !in)
        return;
    memset(h, 0, sizeof h);
    w32(h, 0x04034B50u);
    w16(h + 4, 20);
    w16(h + 10, z->dos_time);
    w16(h + 12, z->dos_date);
    w16(h + 26, (unsigned)nlen);
    fwrite(h, 1, 30, z->f);
    fwrite(name, 1, nlen, z->f);
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        crc = crc32_update(crc, buf, n);
        size += (uint32_t)n;
        fwrite(buf, 1, n, z->f);
    }
    fclose(in);
    end = _ftelli64(z->f);
    w32(h + 14, crc);
    w32(h + 18, size);
    w32(h + 22, size);
    _fseeki64(z->f, at, SEEK_SET);
    fwrite(h, 1, 30, z->f);
    _fseeki64(z->f, end, SEEK_SET);

    if (z->central_len + 46 + nlen > z->central_cap)
        return;
    memset(c, 0, sizeof c);
    w32(c, 0x02014B50u);
    w16(c + 4, 20);
    w16(c + 6, 20);
    memcpy(c + 12, h + 10, 16);          /* time, date, crc, sizes */
    w16(c + 28, (unsigned)nlen);
    w32(c + 42, (uint32_t)at);
    memcpy(z->central + z->central_len, c, 46);
    memcpy(z->central + z->central_len + 46, name, nlen);
    z->central_len += 46 + nlen;
    z->count++;
}

static void zip_walk(zip_t *z, const char *dir, const char *rel)
{
    char pat[MAX_PATH], a[MAX_PATH], r[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    sprintf_s(pat, MAX_PATH, "%s\\*", dir);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
            continue;
        if (_snprintf_s(a, MAX_PATH, _TRUNCATE, "%s\\%s", dir, fd.cFileName) < 0
                || _snprintf_s(r, MAX_PATH, _TRUNCATE, "%s%s%s", rel, rel[0] ? "/" : "", fd.cFileName) < 0)
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            zip_walk(z, a, r);
        else
            zip_add(z, a, r);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

int btvs_zip_folder(const char *folder, const char *zip_path)
{
    zip_t z;
    unsigned char e[22];
    SYSTEMTIME t;
    long long at;

    memset(&z, 0, sizeof z);
    if (fopen_s(&z.f, zip_path, "wb") != 0 || !z.f)
        return 0;
    z.central_cap = 256 * 1024;
    z.central = (char *)malloc(z.central_cap);
    if (!z.central) {
        fclose(z.f);
        return 0;
    }
    GetLocalTime(&t);
    z.dos_time = (unsigned short)((t.wHour << 11) | (t.wMinute << 5) | (t.wSecond / 2));
    z.dos_date = (unsigned short)(((t.wYear - 1980) << 9) | (t.wMonth << 5) | t.wDay);
    zip_walk(&z, folder, "");
    at = _ftelli64(z.f);
    fwrite(z.central, 1, z.central_len, z.f);
    memset(e, 0, sizeof e);
    w32(e, 0x06054B50u);
    w16(e + 8, z.count);
    w16(e + 10, z.count);
    w32(e + 12, (uint32_t)z.central_len);
    w32(e + 16, (uint32_t)at);
    fwrite(e, 1, 22, z.f);
    fclose(z.f);
    free(z.central);
    return (int)z.count;
}

/* Our own zips back into a folder (stored entries only; a zip another tool
 * re-packed with compression is refused: unpack it and give the folder). */
int btvs_unzip(const char *zip_path, const char *folder)
{
    static unsigned char buf[65536];
    unsigned char h[30];
    char name[MAX_PATH], out[MAX_PATH];
    FILE *f, *o;
    int n = 0;

    if (fopen_s(&f, zip_path, "rb") != 0 || !f)
        return -1;
    make_dirs(folder);
    while (fread(h, 1, 30, f) == 30 && r32(h) == 0x04034B50u) {
        unsigned method = r16(h + 8), flags = r16(h + 6), nlen = r16(h + 26), xlen = r16(h + 28);
        uint32_t size = r32(h + 22), left;
        char *p;
        if (method != 0 || (flags & 8) || nlen == 0 || nlen >= MAX_PATH - 1) {
            fclose(f);
            return -2;
        }
        if (fread(name, 1, nlen, f) != nlen)
            break;
        name[nlen] = 0;
        _fseeki64(f, xlen, SEEK_CUR);
        for (p = name; *p; p++)
            if (*p == '/')
                *p = '\\';
        /* never outside the folder */
        if (strstr(name, "..") || name[0] == '\\' || strchr(name, ':')
                || _snprintf_s(out, MAX_PATH, _TRUNCATE, "%s\\%s", folder, name) < 0) {
            _fseeki64(f, size, SEEK_CUR);
            continue;
        }
        p = strrchr(out, '\\');
        if (p) {
            *p = 0;
            make_dirs(out);
            *p = '\\';
        }
        if (name[nlen - 1] == '\\' || fopen_s(&o, out, "wb") != 0 || !o) {
            _fseeki64(f, size, SEEK_CUR);
            continue;
        }
        for (left = size; left;) {
            size_t want = left < sizeof buf ? left : sizeof buf, got = fread(buf, 1, want, f);
            if (!got)
                break;
            fwrite(buf, 1, got, o);
            left -= (uint32_t)got;
        }
        fclose(o);
        n++;
    }
    fclose(f);
    return n;
}

/* ── The report ─────────────────────────────────────────────────────────── */

static void json_str(FILE *f, const char *s, size_t max)
{
    size_t i;
    fputc('"', f);
    for (i = 0; s && s[i] && i < max; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\')
            fprintf(f, "\\%c", c);
        else if (c == '\n')
            fputs("\\n", f);
        else if (c == '\r')
            ;
        else if (c == '\t')
            fputs("\\t", f);
        else if (c < 0x20)
            fprintf(f, "\\u%04x", c);
        else
            fputc(c, f);
    }
    fputc('"', f);
}

static void first_line(const char *s, char *out, size_t n)
{
    size_t i = 0;
    while (s && (*s == ' ' || *s == '\n' || *s == '\r'))
        s++;
    for (; s && s[i] && s[i] != '\n' && s[i] != '\r' && i + 1 < n; i++)
        out[i] = s[i];
    out[i] = 0;
}

/* The newest `keep` files matching crashes\<prefix>* into <dir>\crashes. */
static int copy_newest(const char *dir, const char *pattern, int keep)
{
    char pat[MAX_PATH], src[MAX_PATH], dstdir[MAX_PATH], names[8][MAX_PATH];
    FILETIME times[8];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int n = 0, i, j;

    if (keep > 8)
        keep = 8;
    sprintf_s(pat, MAX_PATH, "%s\\crashes\\%s", btvs_exe_dir(), pattern);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        for (i = 0; i < n && CompareFileTime(&fd.ftLastWriteTime, &times[i]) <= 0; i++) ;
        if (i >= keep)
            continue;
        if (n < keep)
            n++;
        for (j = n - 1; j > i; j--) {
            strcpy_s(names[j], MAX_PATH, names[j - 1]);
            times[j] = times[j - 1];
        }
        strcpy_s(names[i], MAX_PATH, fd.cFileName);
        times[i] = fd.ftLastWriteTime;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    sprintf_s(dstdir, MAX_PATH, "%s\\crashes", dir);
    if (n)
        CreateDirectoryA(dstdir, NULL);
    for (i = 0; i < n; i++) {
        sprintf_s(src, MAX_PATH, "%s\\crashes\\%s", btvs_exe_dir(), names[i]);
        copy_into(dstdir, src, names[i]);
    }
    return n;
}

static long long file_size(const char *path)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &d))
        return -1;
    return ((long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

int btvs_report_make(const char *player_text, const char *trigger)
{
    static char sys[2048], memtext[1500], tail[16000];
    char root[MAX_PATH], dir[MAX_PATH], zip[MAX_PATH], path[MAX_PATH], src[MAX_PATH], when[32], problem[201];
    struct btvs_where w;
    struct btvs_mem m;
    SYSTEMTIME t;
    FILE *f;
    unsigned long long save_bytes = 0;
    int have_shot = 0, have_input, have_settings, save_files, crashes, i, files;
    long long input_bytes;
    double t0 = btvs_uptime();

    InitOnceExecuteOnce(&s_once, init_once, NULL, NULL);
    if (!player_text)
        player_text = "";
    EnterCriticalSection(&s_cs);

    GetLocalTime(&t);
    sprintf_s(when, sizeof when, "%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
              t.wSecond);
    sprintf_s(root, MAX_PATH, "%s\\reports", btvs_exe_dir());
    CreateDirectoryA(root, NULL);
    sprintf_s(dir, MAX_PATH, "%s\\%s", root, when);
    for (i = 2; !CreateDirectoryA(dir, NULL) && i < 10; i++)
        sprintf_s(dir, MAX_PATH, "%s\\%s-%d", root, when, i);
    sprintf_s(zip, MAX_PATH, "%s.zip", dir);

    /* The picture first: the one from before the menu opened if there is one
     * (10 minutes at most), else what is on screen now. */
    sprintf_s(path, MAX_PATH, "%s\\screenshot.png", dir);
    if (s_pending[0] && GetTickCount() - s_pending_tick < 600000 && MoveFileExA(s_pending, path, MOVEFILE_REPLACE_EXISTING))
        have_shot = 1;
    else
        have_shot = take_screenshot(path);
    s_pending[0] = 0;

    btvs_trail_where(&w);
    btvs_trail("report", "folder=%s trigger=%s", when, trigger);
    btvs_log("report", "making a report (%s): %s", trigger, dir);

    /* The recording, complete up to this moment. */
    btvs_input_record_flush();
    sprintf_s(src, MAX_PATH, "%s\\" BTVS_INPUT_FILE, btvs_exe_dir());
    have_input = copy_into(dir, src, "input.rec");
    sprintf_s(path, MAX_PATH, "%s\\input.rec", dir);
    input_bytes = have_input ? file_size(path) : 0;

    btvs_settings_save();
    have_settings = copy_into(dir, btvs_settings_path(), "settings.ini");
    sprintf_s(path, MAX_PATH, "%s\\save", dir);
    save_files = btvs_copy_tree(btvs_save_dir(), path, 32ull << 20, &save_bytes);
    crashes = copy_newest(dir, "crash_*.txt", 3) + copy_newest(dir, "hang_*.txt", 2) + copy_newest(dir, "crash_*.dmp", 1);

    btvs_system_text(sys, sizeof sys);
    btvs_mem_stats_now(&m);
    btvs_mem_text(&m, memtext, sizeof memtext);
    btvs_trail_tail(tail, sizeof tail, 60);
    first_line(player_text, problem, sizeof problem);

    sprintf_s(path, MAX_PATH, "%s\\memory.txt", dir);
    if (fopen_s(&f, path, "w") == 0 && f) {
        fputs(memtext, f);
        fclose(f);
    }

    /* replay.txt: what BTVS_REPLAY reads (key=value). */
    sprintf_s(path, MAX_PATH, "%s\\replay.txt", dir);
    if (fopen_s(&f, path, "w") == 0 && f) {
        fprintf(f, "format=1\nversion=%s\nanchor=%s\nanchor_name=%s\nlevel=%s\ncheckpoint=%s\n"
                   "anchor_time=%.3f\nanchor_frame=%ld\nreport_time=%.3f\nreport_frame=%ld\n"
                   "input=%s\nsettings=%s\nsave=%s\n",
                BTVS_VERSION, w.anchor_event, w.anchor_name, w.level, w.checkpoint, w.anchor_time, w.anchor_frame,
                t0, w.frame, have_input ? "input.rec" : "", have_settings ? "settings.ini" : "",
                save_files ? "save" : "");
        if (w.have_pos)
            fprintf(f, "pos=%.3f %.3f %.3f\nhealth=%.1f\n", w.pos[0], w.pos[1], w.pos[2], w.health);
        fclose(f);
    }

    sprintf_s(path, MAX_PATH, "%s\\report.txt", dir);
    if (fopen_s(&f, path, "w") == 0 && f) {
        fprintf(f, "Buffy the Vampire Slayer (Xbox, 2002) - Windows port: problem report\n");
        fprintf(f, "Made %04u-%02u-%02u %02u:%02u:%02u (%s), %.0f s into the game\n\n", t.wYear, t.wMonth, t.wDay,
                t.wHour, t.wMinute, t.wSecond, trigger, t0);
        fprintf(f, "== What the player wrote\n%s\n\n", player_text[0] ? player_text : "(nothing)");
        fprintf(f, "== Version\n%s\n\n", btvs_version_text());
        fprintf(f, "== Where in the game\n");
        fprintf(f, "level: %s\ncheckpoint: %s\nmenu screen: %s\n", w.level[0] ? w.level : "(not in a level yet)",
                w.checkpoint[0] ? w.checkpoint : "-", w.menu[0] ? w.menu : "-");
        if (w.have_pos)
            fprintf(f, "player: position %.2f %.2f %.2f, health %.0f\n", w.pos[0], w.pos[1], w.pos[2], w.health);
        fprintf(f, "frames shown: %ld, the last one %lu ms ago%s\n", w.frame, xbox_HangMonitorIdleMs(),
                xbox_HangMonitorIdleMs() > 3000 ? "   <-- the game thread is not moving" : "");
        fprintf(f, "replay starts at: %s \"%s\" (%.1f s, frame %ld); %.1f s of play since then; input recording: %s\n\n",
                w.anchor_event, w.anchor_name, w.anchor_time, w.anchor_frame, t0 - w.anchor_time,
                have_input ? "yes" : "NO (not recorded)");
        fprintf(f, "== The last things that happened (the trail; all of it: btvs_trail.txt)\n%s\n", tail);
        fprintf(f, "== System\n%s\n", sys);
        fprintf(f, "== Memory\n%s\n", memtext);
        fprintf(f, "== In this report\nscreenshot: %s\ninput recording: %s\nsettings: %s\nsave: %d files, %llu KB\n"
                   "crash / hang reports: %d\n\n",
                have_shot ? "yes" : "no (the game could not give one)", have_input ? "yes" : "no",
                have_settings ? "yes" : "no (defaults: no btvs_settings.ini)", save_files, save_bytes / 1024,
                crashes);
        fprintf(f, "== Where the game thread is\n");
        fflush(f);
        xbox_WatchdogReportTo(f, "problem report");
        fprintf(f, "\n== To go back there\npython port/tools/report_open.py \"%s\"\n", zip);
        fclose(f);
    }

    /* report.json: SvR2011's upload fields (format, app, version, platform,
     * os, problem, what, before, contact, signature) plus this game's own. */
    sprintf_s(path, MAX_PATH, "%s\\report.json", dir);
    if (fopen_s(&f, path, "w") == 0 && f) {
        char os[160];
        first_line(sys, os, sizeof os);
        fprintf(f, "{\n  \"format\": 1,\n  \"app\": \"game\",\n  \"game\": \"btvs-xbox-pc\",\n  \"version\": ");
        json_str(f, BTVS_VERSION, 64);
        fprintf(f, ",\n  \"build\": ");
        json_str(f, btvs_version_text(), 200);
        fprintf(f, ",\n  \"platform\": \"windows\",\n  \"os\": ");
        json_str(f, os + (strncmp(os, "Windows: ", 9) ? 0 : 9), 200);
        fprintf(f, ",\n  \"date\": \"%04u-%02u-%02uT%02u:%02u:%02u\",\n  \"trigger\": ", t.wYear, t.wMonth, t.wDay,
                t.wHour, t.wMinute, t.wSecond);
        json_str(f, trigger, 40);
        fprintf(f, ",\n  \"problem\": ");
        json_str(f, problem, 200);
        fprintf(f, ",\n  \"what\": ");
        json_str(f, player_text, 4000);
        fprintf(f, ",\n  \"before\": ");
        json_str(f, tail, 4000);
        fprintf(f, ",\n  \"contact\": \"\",\n  \"signature\": ");
        {
            char sig[240];
            sprintf_s(sig, sizeof sig, "%s|%s|%s", w.level, w.checkpoint, problem);
            sprintf_s(sig, sizeof sig, "%08x", crc32_update(0, (const unsigned char *)sig, strlen(sig)));
            json_str(f, sig, 16);
        }
        fprintf(f, ",\n  \"sent\": false,\n  \"where\": {\"level\": ");
        json_str(f, w.level, 96);
        fprintf(f, ", \"checkpoint\": ");
        json_str(f, w.checkpoint, 96);
        fprintf(f, ", \"menu\": ");
        json_str(f, w.menu, 96);
        fprintf(f, ", \"frame\": %ld, \"seconds\": %.3f", w.frame, t0);
        if (w.have_pos)
            fprintf(f, ", \"pos\": [%.3f, %.3f, %.3f], \"health\": %.1f", w.pos[0], w.pos[1], w.pos[2], w.health);
        fprintf(f, "},\n  \"replay\": {\"anchor\": ");
        json_str(f, w.anchor_event, 16);
        fprintf(f, ", \"anchor_name\": ");
        json_str(f, w.anchor_name, 96);
        fprintf(f, ", \"anchor_seconds\": %.3f, \"anchor_frame\": %ld, \"input\": %s, \"input_bytes\": %lld, "
                   "\"settings\": %s, \"save_files\": %d},\n",
                w.anchor_time, w.anchor_frame, have_input ? "true" : "false", input_bytes,
                have_settings ? "true" : "false", save_files);
        fprintf(f, "  \"memory\": {\"ram_total\": %llu, \"ram_available\": %llu, \"working_set\": %llu, "
                   "\"private_working_set\": %llu, \"commit\": %llu, \"gpu\": ",
                m.ram_total, m.ram_available, m.proc_working_set, m.proc_private_working_set, m.proc_commit);
        json_str(f, m.gpu_name, 128);
        fprintf(f, ", \"vram_total\": %llu, \"vram_used_system\": %llu, \"vram_used\": %llu, \"vram_budget\": %llu, "
                   "\"game_heap_valid\": %s, \"game_heap_used\": %llu, \"game_heap_allocs\": %u},\n",
                m.vram_total, m.vram_used_system, m.vram_used, m.vram_budget, m.game_heap_valid ? "true" : "false",
                m.game_heap_used, m.game_heap_allocs);
        fprintf(f, "  \"system\": ");
        json_str(f, sys, 2000);
        fprintf(f, ",\n  \"files\": {\"screenshot\": %s, \"crashes\": %d}\n}\n", have_shot ? "true" : "false", crashes);
        fclose(f);
    }

    /* The logs last, so they include the lines above. */
    sprintf_s(src, MAX_PATH, "%s\\" BTVS_LOG_FILE, btvs_exe_dir());
    copy_into(dir, src, BTVS_LOG_FILE);
    sprintf_s(src, MAX_PATH, "%s\\" BTVS_LOG_PREVIOUS, btvs_exe_dir());
    copy_into(dir, src, BTVS_LOG_PREVIOUS);
    sprintf_s(src, MAX_PATH, "%s\\" BTVS_TRAIL_FILE, btvs_exe_dir());
    copy_into(dir, src, BTVS_TRAIL_FILE);
    sprintf_s(src, MAX_PATH, "%s\\" BTVS_TRAIL_PREVIOUS, btvs_exe_dir());
    copy_into(dir, src, BTVS_TRAIL_PREVIOUS);

    files = btvs_zip_folder(dir, zip);
    strcpy_s(s_last, MAX_PATH, dir);
    LeaveCriticalSection(&s_cs);
    btvs_log("report", "report saved: %s (+ .zip, %d files, %.2f s; screenshot %s, input %s, save %d files)", dir, files,
             btvs_uptime() - t0, have_shot ? "yes" : "no", have_input ? "yes" : "no", save_files);
    return files > 0;
}

int btvs_report_create(const char *player_text) { return btvs_report_make(player_text, "menu"); }

const char *btvs_report_last_path(void) { return s_last; }

void btvs_report_prepare(void)
{
    char path[MAX_PATH];
    InitOnceExecuteOnce(&s_once, init_once, NULL, NULL);
    sprintf_s(path, MAX_PATH, "%s\\reports", btvs_exe_dir());
    CreateDirectoryA(path, NULL);
    strcat_s(path, MAX_PATH, "\\pending.png");
    EnterCriticalSection(&s_cs);
    if (take_screenshot(path)) {
        strcpy_s(s_pending, MAX_PATH, path);
        s_pending_tick = GetTickCount();
    }
    LeaveCriticalSection(&s_cs);
}

/* ── The hotkey ─────────────────────────────────────────────────────────── */

static int ours_in_front(void)
{
    HWND w = GetForegroundWindow();
    DWORD pid = 0;
    if (!w)
        return 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

static DWORD WINAPI hotkey_thread(LPVOID arg)
{
    int was_down = 1;            /* a key held as the game starts is not a press */
    DWORD last = 0, mode_tick = 0;
    int mode = 1;
    (void)arg;
    for (;;) {
        int down = 0;
        DWORD now = GetTickCount(), i;
        Sleep(30);
        if (now - mode_tick > 2000) {       /* the Menu area can switch it while the game runs */
            mode_tick = now;
            mode = btvs_setting_int("report", "hotkey", 1);
        }
        if (!mode || !ours_in_front()) {
            was_down = 0;
            continue;
        }
        if (GetAsyncKeyState(VK_F12) & 0x8000)
            down = 1;
        if (mode == 1)
            for (i = 0; i < 4 && !down; i++) {
                XINPUT_STATE st;
                if (XInputGetState(i, &st) == ERROR_SUCCESS && (st.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB))
                    down = 2;
            }
        if (down && !was_down && now - last > 3000) {
            last = now;
            if (btvs_report_make("", down == 1 ? "F12" : "left stick click")) {
                if (!getenv("BTVS_MUTE"))
                    MessageBeep(MB_ICONASTERISK);
            }
        }
        was_down = down;
    }
}

static DWORD WINAPI test_thread(LPVOID arg)
{
    const char *text = getenv("BTVS_TEST_REPORT_TEXT");
    Sleep((DWORD)(uintptr_t)arg * 1000);
    btvs_report_prepare();
    btvs_report_make(text ? text : "Test report (BTVS_TEST_REPORT_AT).\nSecond line.", "test");
    return 0;
}

void btvs_report_init(void)
{
    const char *e = getenv("BTVS_TEST_REPORT_AT");
    HANDLE h;
    InitOnceExecuteOnce(&s_once, init_once, NULL, NULL);
    if (!btvs_is_test_run()) {              /* a test run never reads the real keyboard or pads */
        h = CreateThread(NULL, 0, hotkey_thread, NULL, 0, NULL);
        if (h)
            CloseHandle(h);
    }
    if (e && atoi(e) > 0) {
        h = CreateThread(NULL, 0, test_thread, (LPVOID)(uintptr_t)atoi(e), 0, NULL);
        if (h)
            CloseHandle(h);
    }
}
