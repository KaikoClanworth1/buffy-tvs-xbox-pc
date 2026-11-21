/* btvs_console - a window program's missing stdout/stderr, and the C
 * runtime's silent ways to end the process (Extras area).
 *
 * btvs_console_init() is the first thing WinMain does.
 *
 * 1. The C runtime ends the process with exit code 0xC0000409 and no message
 *    when a library function is handed something invalid, and when abort()
 *    is called. Neither passes through the crash handler (no exception is
 *    raised), so a run that ended that way left nothing behind. Both now
 *    write what happened and the call stack to the log first.
 *
 * 2. See btvs_console_init for the standard handles.
 */
#include <windows.h>
#include <dbghelp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <io.h>

#include "btvs.h"

static void log_stack(const char *what)
{
    void *frames[48];
    USHORT i, n = CaptureStackBackTrace(1, 48, frames, NULL);
    HANDLE proc = GetCurrentProcess();
    btvs_log("crash", "%s; the process ends with code 0xC0000409. Call stack:", what);
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    SymInitialize(proc, NULL, TRUE);           /* a second call fails harmlessly */
    for (i = 0; i < n; i++) {
        char buf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
        DWORD64 disp = 0;
        IMAGEHLP_LINE64 line;
        DWORD ldisp = 0;
        memset(buf, 0, sizeof buf);
        si->SizeOfStruct = sizeof(SYMBOL_INFO);
        si->MaxNameLen = 255;
        memset(&line, 0, sizeof line);
        line.SizeOfStruct = sizeof line;
        if (SymFromAddr(proc, (DWORD64)(uintptr_t)frames[i], &disp, si)) {
            if (SymGetLineFromAddr64(proc, (DWORD64)(uintptr_t)frames[i], &ldisp, &line))
                btvs_log("crash", "  %p %s+0x%llX (%s:%lu)", frames[i], si->Name, (unsigned long long)disp,
                         line.FileName, line.LineNumber);
            else
                btvs_log("crash", "  %p %s+0x%llX", frames[i], si->Name, (unsigned long long)disp);
        } else {
            btvs_log("crash", "  %p", frames[i]);
        }
    }
}

static void on_invalid_parameter(const wchar_t *expr, const wchar_t *func, const wchar_t *file,
                                 unsigned int line, uintptr_t reserved)
{
    (void)expr; (void)func; (void)file; (void)line; (void)reserved;   /* all NULL in a release build */
    log_stack("the C runtime was given an invalid argument");
    TerminateProcess(GetCurrentProcess(), 0xC0000409u);
}

static void on_abort(int sig)
{
    (void)sig;
    log_stack("abort() was called");
    TerminateProcess(GetCurrentProcess(), 0xC0000409u);
}

static int handle_usable(DWORD which)
{
    HANDLE h = GetStdHandle(which);
    return h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN;
}

/* A program started by a double-click has no stdout or stderr at all. The
 * runtime under this port prints to both all the time, so they have to be
 * somewhere: when there is none, both are pointed at btvs_console.txt beside
 * the exe (new at each start of the game; a level restart appends). When the
 * starter gave real ones (the launcher, a test run's files), they are kept.
 *
 * Why it matters is in docs/install.md, "Starting buffy_tvs.exe directly". */
void btvs_console_init(void)
{
    _set_invalid_parameter_handler(on_invalid_parameter);
    signal(SIGABRT, on_abort);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    if (!handle_usable(STD_ERROR_HANDLE) || !handle_usable(STD_OUTPUT_HANDLE) || _fileno(stderr) < 0) {
        char path[MAX_PATH], *slash;
        const char *dir = getenv("BTVS_LOG_DIR");
        const char *restarts = getenv("BTVS_RESTARTS");
        FILE *f = NULL;
        if (getenv("BTVS_NO_CONSOLE_FILE"))             /* testing: leave the process as it was started */
            return;
        if (dir && *dir) {
            sprintf_s(path, sizeof path, "%s\\btvs_console.txt", dir);
        } else {
            GetModuleFileNameA(NULL, path, MAX_PATH);
            slash = strrchr(path, '\\');
            if (slash) slash[1] = 0; else path[0] = 0;
            strcat_s(path, sizeof path, "btvs_console.txt");
        }
        /* The supervisor (the process the player starts) makes the file new;
         * the game processes it starts append to it. */
        if (freopen_s(&f, path, (getenv("BTVS_CHILD") || (restarts && *restarts)) ? "a" : "w", stderr) != 0 || !f)
            freopen_s(&f, "NUL", "w", stderr);          /* a folder that cannot be written */
        if (_fileno(stderr) >= 0) {
            HANDLE h = (HANDLE)_get_osfhandle(_fileno(stderr));
            setvbuf(stderr, NULL, _IONBF, 0);
            SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
            SetStdHandle(STD_ERROR_HANDLE, h);
            SetStdHandle(STD_OUTPUT_HANDLE, h);
            /* stdout: a second append stream on the same file (both unbuffered, so lines keep their order) */
            if (freopen_s(&f, path, "a", stdout) == 0 && f)
                setvbuf(stdout, NULL, _IONBF, 0);
        }
    }
}
