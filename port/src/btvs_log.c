/* The log: btvs_log.txt beside the exe.
 *
 *   [   0.000] [log] ...header: version, commit, Windows, CPU, RAM, GPU, settings...
 *   [  12.345] [gfx] text
 *
 * - one line per call, from any thread, written straight to the file (no
 *   buffering: a crash loses nothing);
 * - the run before is kept as btvs_log_previous.txt;
 * - a test run (stderr is a file: port/tools/run.sh) also gets every line on
 *   stderr; a player's run has no stderr, so the toolkit's own stderr/stdout
 *   output is pointed into the log file as well.
 *
 * BTVS_LOG_DIR=<folder> puts the log (and the trail, reports, crashes)
 * somewhere else than beside the exe.
 */
#include <windows.h>
#include <intrin.h>
#include <io.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btvs_log.h"
#include "btvs_build_info.h"   /* BTVS_GIT_COMMIT (generated at build time) */

#ifndef BTVS_VERSION
#define BTVS_VERSION "dev"
#endif

static INIT_ONCE s_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION s_cs;
static HANDLE s_file = INVALID_HANDLE_VALUE;
static LARGE_INTEGER s_t0, s_freq;
static char s_dir[MAX_PATH], s_save_dir[MAX_PATH];
static int s_tee;          /* also write to stderr */
static int s_test_run;

static void write_all(const char *p, size_t n)
{
    DWORD w;
    if (s_file != INVALID_HANDLE_VALUE)
        WriteFile(s_file, p, (DWORD)n, &w, NULL);
    if (s_tee) {
        fwrite(p, 1, n, stderr);
        fflush(stderr);
    }
}

double btvs_uptime(void)
{
    LARGE_INTEGER now;
    if (!s_freq.QuadPart)
        return 0.0;
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - s_t0.QuadPart) / (double)s_freq.QuadPart;
}

static BOOL CALLBACK init_once(PINIT_ONCE once, PVOID param, PVOID *ctx);
const char *btvs_exe_dir(void) { InitOnceExecuteOnce(&s_once, init_once, NULL, NULL); return s_dir; }
/* Read again each time: BTVS_REPLAY points it at a copy of the report's save. */
const char *btvs_save_dir(void)
{
    const char *e = getenv("BTVS_SAVE_DIR");
    InitOnceExecuteOnce(&s_once, init_once, NULL, NULL);
    return e && *e ? e : s_save_dir;
}
int btvs_is_test_run(void) { InitOnceExecuteOnce(&s_once, init_once, NULL, NULL); return s_test_run; }

const char *btvs_version_text(void)
{
    return BTVS_VERSION " (commit " BTVS_GIT_COMMIT ", built " __DATE__ " " __TIME__ ")";
}

/* ── System description ─────────────────────────────────────────────────── */

static void reg_str(const char *key, const char *name, char *out, DWORD n)
{
    HKEY h;
    DWORD type = 0, size = n - 1;
    out[0] = 0;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key, 0, KEY_READ | KEY_WOW64_64KEY, &h) != ERROR_SUCCESS)
        return;
    if (RegQueryValueExA(h, name, NULL, &type, (BYTE *)out, &size) != ERROR_SUCCESS || type != REG_SZ)
        out[0] = 0;
    else
        out[size < n ? size : n - 1] = 0;
    RegCloseKey(h);
}

void btvs_system_text(char *buf, size_t n)
{
    static const char *cv = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    char product[128], display[64], build[32], cpu[64] = "";
    int regs[4], i;
    MEMORYSTATUSEX ms;
    SYSTEM_INFO si;
    DWORD_PTR proc_mask = 0, sys_mask = 0;
    size_t len;
    DWORD ubr = 0, sz = sizeof ubr;
    HKEY h;

    reg_str(cv, "ProductName", product, sizeof product);
    reg_str(cv, "DisplayVersion", display, sizeof display);
    reg_str(cv, "CurrentBuildNumber", build, sizeof build);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, cv, 0, KEY_READ | KEY_WOW64_64KEY, &h) == ERROR_SUCCESS) {
        RegQueryValueExA(h, "UBR", NULL, NULL, (BYTE *)&ubr, &sz);
        RegCloseKey(h);
    }
    /* Windows 11 still calls itself "Windows 10" in this key. */
    if (atoi(build) >= 22000 && !strncmp(product, "Windows 10", 10))
        product[9] = '1';

    __cpuid(regs, 0x80000000);
    if ((unsigned)regs[0] >= 0x80000004u)
        for (i = 0; i < 3; i++) {
            __cpuid(regs, 0x80000002 + i);
            memcpy(cpu + i * 16, regs, 16);
        }
    cpu[48] = 0;
    for (i = 0; cpu[i] == ' '; i++) ;
    GetSystemInfo(&si);
    GetProcessAffinityMask(GetCurrentProcess(), &proc_mask, &sys_mask);
    ms.dwLength = sizeof ms;
    GlobalMemoryStatusEx(&ms);

    _snprintf_s(buf, n, _TRUNCATE,
                "Windows: %s %s (build %s.%lu)\n"
                "CPU: %s, %lu logical processors (this game may use mask %llX)\n"
                "RAM: %.1f GB, %.1f GB free\n",
                product[0] ? product : "Windows", display, build, (unsigned long)ubr,
                cpu + i, (unsigned long)si.dwNumberOfProcessors, (unsigned long long)proc_mask,
                ms.ullTotalPhys / 1073741824.0, ms.ullAvailPhys / 1073741824.0);
    len = strlen(buf);
    btvs_gpu_text(buf + len, n - len);
}

static void log_header(void)
{
    char sys[2048], exe[MAX_PATH], line[512];
    SYSTEMTIME t;
    LPCH env, e;
    FILE *f;

    GetLocalTime(&t);
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    btvs_log("log", "Buffy the Vampire Slayer (Xbox, 2002) - Windows port %s", btvs_version_text());
    btvs_log("log", "started %04u-%02u-%02u %02u:%02u:%02u, pid %lu%s", t.wYear, t.wMonth, t.wDay, t.wHour,
             t.wMinute, t.wSecond, (unsigned long)GetCurrentProcessId(), s_test_run ? ", test run" : "");
    btvs_log("log", "exe: %s", exe);
    btvs_system_text(sys, sizeof sys);
    {
        char *p = sys, *nl;
        while (*p && (nl = strchr(p, '\n')) != NULL) {
            *nl = 0;
            btvs_log("log", "%s", p);
            p = nl + 1;
        }
    }
    /* What the run was started with. */
    env = GetEnvironmentStringsA();
    for (e = env; e && *e; e += strlen(e) + 1)
        if (!strncmp(e, "BTVS_", 5) || !strncmp(e, "RECOMP_", 7))
            btvs_log("log", "env %s", e);
    if (env)
        FreeEnvironmentStringsA(env);
    /* The settings as they are on disk now (the Menu area logs later changes). */
    if (fopen_s(&f, btvs_settings_path(), "r") == 0 && f) {
        btvs_log("log", "settings: %s", btvs_settings_path());
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (line[0])
                btvs_log("log", "  %s", line);
        }
        fclose(f);
    } else {
        btvs_log("log", "settings: no %s yet (defaults)", btvs_settings_path());
    }
}

static BOOL CALLBACK init_once(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    char path[MAX_PATH], prev[MAX_PATH], *slash;
    const char *e;
    HANDLE herr;
    (void)once; (void)param; (void)ctx;

    InitializeCriticalSection(&s_cs);
    QueryPerformanceFrequency(&s_freq);
    QueryPerformanceCounter(&s_t0);

    GetModuleFileNameA(NULL, s_dir, MAX_PATH);
    slash = strrchr(s_dir, '\\');
    if (slash)
        *slash = 0;
    sprintf_s(s_save_dir, MAX_PATH, "%s\\XboxData", s_dir);
    e = getenv("BTVS_LOG_DIR");
    if (e && *e) {
        strcpy_s(s_dir, MAX_PATH, e);
        CreateDirectoryA(s_dir, NULL);
    }
    e = getenv("BTVS_NO_WINDOW");
    s_test_run = e && *e && *e != '0';

    sprintf_s(path, MAX_PATH, "%s\\" BTVS_LOG_FILE, s_dir);
    sprintf_s(prev, MAX_PATH, "%s\\" BTVS_LOG_PREVIOUS, s_dir);
    MoveFileExA(path, prev, MOVEFILE_REPLACE_EXISTING);
    s_file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    /* stderr: a test run's is a file (keep it, and copy our lines to it); a
     * player's doesn't exist (a windowed program), so the toolkit's
     * fprintf(stderr) and printf are pointed at the log file. Both handles
     * append, so the lines interleave in order. */
    herr = GetStdHandle(STD_ERROR_HANDLE);
    if (herr && herr != INVALID_HANDLE_VALUE && GetFileType(herr) != FILE_TYPE_UNKNOWN && _fileno(stderr) >= 0) {
        s_tee = 1;
    } else {
        FILE *f;
        if (freopen_s(&f, path, "a", stderr) == 0)
            setvbuf(stderr, NULL, _IONBF, 0);
        if (freopen_s(&f, path, "a", stdout) == 0)
            setvbuf(stdout, NULL, _IONBF, 0);
    }
    return TRUE;
}

void btvs_log_init(void)
{
    static volatile LONG header;
    InitOnceExecuteOnce(&s_once, init_once, NULL, NULL);
    if (InterlockedCompareExchange(&header, 1, 0) == 0)
        log_header();
}

void btvs_logv(const char *area, const char *fmt, va_list ap)
{
    char line[2048];
    int n, m;

    InitOnceExecuteOnce(&s_once, init_once, NULL, NULL);
    n = _snprintf_s(line, sizeof line - 2, _TRUNCATE, "[%8.3f] [%s] ", btvs_uptime(), area ? area : "?");
    if (n < 0)
        n = 0;
    m = _vsnprintf_s(line + n, sizeof line - 2 - n, _TRUNCATE, fmt, ap);
    n = m < 0 ? (int)strlen(line) : n + m;
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        n--;
    line[n++] = '\n';
    EnterCriticalSection(&s_cs);
    write_all(line, (size_t)n);
    LeaveCriticalSection(&s_cs);
}

void btvs_log(const char *area, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    btvs_logv(area, fmt, ap);
    va_end(ap);
}

void btvs_log_block(const char *text)
{
    size_t n = strlen(text);
    InitOnceExecuteOnce(&s_once, init_once, NULL, NULL);
    {
        /* A crash report must come out even if the crashed thread died holding the lock. */
        int tries, locked = 0;
        for (tries = 0; tries < 50 && !(locked = TryEnterCriticalSection(&s_cs)); tries++)
            Sleep(10);
        write_all(text, n);
        if (n && text[n - 1] != '\n')
            write_all("\n", 1);
        if (locked)
            LeaveCriticalSection(&s_cs);
    }
}
