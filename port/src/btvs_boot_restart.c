/**
 * The game restarts itself, so something has to start it again.
 *
 * To load a level the game calls XLaunchNewImage on its own image with a
 * command line in the launch data ("root:Z:\ ... controller:..."): the
 * console reboots into the same game with all its memory free, and the new
 * run reads the launch data and goes to the level. Menu -> level, level ->
 * level and level -> menu all go this way.
 *
 * Here the program started by the player (or by run.sh) is only a
 * supervisor: it starts the game as a child process (the same exe, with
 * BTVS_CHILD=1) and waits. When the game asks to be launched again, the
 * runtime writes the launch data page to a file and the child ends with
 * XBOX_EXIT_RELAUNCH; the supervisor then starts the next child with that
 * file. Any other exit code ends the supervisor with the same code.
 *
 * So a restart really is a new process (nothing of the old run is left, as
 * on the console), and whoever started the supervisor waits on one process
 * for the whole session. The children are in a job that dies with the
 * supervisor: stopping the supervisor by PID stops the game.
 *
 * Test runs: BTVS_EXIT_AFTER counts for the whole session (each child gets
 * what is left of it); everything else in the environment is inherited
 * (BTVS_NO_WINDOW, BTVS_MUTE, BTVS_AFFINITY ...). BTVS_NO_SUPERVISOR=1: the
 * game runs in the process that was started, and a restart just ends it.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define XBOX_EXIT_RELAUNCH 0x42544C52u

/* ── the session's window ────────────────────────────────────────────────
 * Every run of the game is a process with a window of its own, so at each
 * restart -- menu to level, level to level, back to the menu -- the player
 * saw the window close, the desktop behind it, and a new window that did
 * not always get the keyboard. The supervisor lives for the whole session,
 * so it has a window for the whole session:
 *
 * - "Buffy the Vampire Slayer" in the taskbar and Alt+Tab, with the exe's
 *   icon: one entry from start to quit. Black, with "Loading..." in it.
 * - Each game process makes its window *owned* by it (BTVS_SESSION_WINDOW in
 *   its environment; nv2a_gpu_set_owner_window): always in front of it, no
 *   taskbar entry of its own, hidden and shown with it. The game process
 *   tells the session window its handle (SW_MSG_GAME).
 * - While a game window is up the session window sits behind it, a little
 *   smaller, so it never shows. When the game is about to end for a restart
 *   it says so (SW_MSG_COVER) and the session window takes the game
 *   window's place exactly: the game window goes, black stays -- no desktop
 *   -- and Windows gives the keyboard to the owner, which is this process,
 *   so the next game process (started by it) may take it.
 * - Activating the session window (taskbar, Alt+Tab) activates the game
 *   window; closing it closes the game window, or ends the session when
 *   there is none; minimising the game window minimises the session window
 *   (the runtime does that), which hides the game window with it.
 *
 * Window modes, Alt+Enter, resizing and all input stay where they were: in
 * the game process's own window.
 *
 * BTVS_NO_WINDOW (test runs): the session window is made and never shown,
 * as the game's is. BTVS_NO_SESSION_WINDOW=1: none (the game windows are
 * ordinary top-level windows again). */
#define SW_MSG_GAME   (WM_APP + 1)      /* wParam: the game process's window */
#define SW_MSG_COVER  (WM_APP + 2)      /* the game window is about to go: cover its place */

static struct {
    HWND hwnd, game;
    int hidden, closing;
} s_sw;

/* What can be seen of a window (without the invisible frame Windows 10 and
 * 11 put round a sizable one). */
static int visible_rect(HWND w, RECT *r)
{
    HMODULE dwm = GetModuleHandleA("dwmapi.dll");
    typedef HRESULT (WINAPI *get_attr)(HWND, DWORD, void *, DWORD);
    get_attr f;
    if (!dwm)
        dwm = LoadLibraryA("dwmapi.dll");
    f = dwm ? (get_attr)(void *)GetProcAddress(dwm, "DwmGetWindowAttribute") : NULL;
    if (f && f(w, 9 /* DWMWA_EXTENDED_FRAME_BOUNDS */, r, sizeof *r) == 0)
        return 1;
    return GetWindowRect(w, r);
}

/* Behind the game window (smaller by `inset` on every side), or -- inset 0 --
 * exactly where it is. Never activates, never shown in a hidden run. */
static void sw_place(int inset)
{
    RECT r;
    if (!s_sw.hwnd || !s_sw.game || !IsWindow(s_sw.game) || IsIconic(s_sw.game) || IsIconic(s_sw.hwnd))
        return;
    if (!visible_rect(s_sw.game, &r) || r.right - r.left < 2 * inset + 16 || r.bottom - r.top < 2 * inset + 16)
        return;
    SetWindowPos(s_sw.hwnd, s_sw.game, r.left + inset, r.top + inset, r.right - r.left - 2 * inset,
                 r.bottom - r.top - 2 * inset, SWP_NOACTIVATE | (s_sw.hidden ? 0 : SWP_SHOWWINDOW));
}

static LRESULT CALLBACK sw_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case SW_MSG_GAME:
        s_sw.game = (HWND)w;
        fprintf(stderr, "[boot] window: the game window %p is up, owned by the session window %p\n", (void *)s_sw.game, (void *)h);
        fflush(stderr);
        sw_place(24);
        if (!s_sw.hidden && GetForegroundWindow() == h)
            SetForegroundWindow(s_sw.game);
        return 0;
    case SW_MSG_COVER:
        sw_place(0);
        if (!s_sw.hidden)
            UpdateWindow(h);
        return 0;
    case WM_TIMER:
        if (s_sw.game && !IsWindow(s_sw.game))
            s_sw.game = NULL;                    /* (its process ended: the place it had is kept) */
        sw_place(24);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) != WA_INACTIVE && !s_sw.hidden && s_sw.game && IsWindow(s_sw.game) && IsWindowVisible(s_sw.game))
            SetForegroundWindow(s_sw.game);
        return 0;
    case WM_SYSCOMMAND:
        if ((w & 0xFFF0) == SC_RESTORE || (w & 0xFFF0) == SC_MINIMIZE)
            break;
        if ((w & 0xFFF0) != SC_CLOSE)
            return 0;                            /* (it is not for moving or sizing: the game window is) */
        break;
    case WM_CLOSE:
        if (s_sw.game && IsWindow(s_sw.game))
            PostMessageA(s_sw.game, WM_CLOSE, 0, 0);     /* the game quits in its own order; the session ends with it */
        else
            s_sw.closing = 1;
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT r;
        HDC dc = BeginPaint(h, &ps);
        GetClientRect(h, &r);
        FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(150, 120, 200));
        DrawTextA(dc, "Loading...", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcA(h, m, w, l);
}

static void sw_make(void)
{
    WNDCLASSEXA wc;
    char v[32];
    const char *nw = getenv("BTVS_NO_WINDOW");
    if (getenv("BTVS_NO_SESSION_WINDOW"))
        return;
    s_sw.hidden = nw && *nw && *nw != '0';
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = sw_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "BuffySessionWindow";
    wc.hIcon = (HICON)LoadImageA(wc.hInstance, MAKEINTRESOURCEA(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                 GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageA(wc.hInstance, MAKEINTRESOURCEA(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    RegisterClassExA(&wc);
    /* made hidden and small: it is shown, in the game window's place, once there is a game window */
    s_sw.hwnd = CreateWindowExA(0, wc.lpszClassName, "Buffy the Vampire Slayer", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
                                0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);
    if (!s_sw.hwnd) {
        fprintf(stderr, "[boot] window: no session window (error %lu): each run of the game has its own\n", GetLastError());
        return;
    }
    SetTimer(s_sw.hwnd, 1, 50, NULL);
    sprintf_s(v, sizeof v, "%llX", (unsigned long long)(uintptr_t)s_sw.hwnd);
    SetEnvironmentVariableA("BTVS_SESSION_WINDOW", v);
    fprintf(stderr, "[boot] window: session window %p made by the supervisor, process %lu%s\n", (void *)s_sw.hwnd,
            GetCurrentProcessId(), s_sw.hidden ? " (hidden: test run)" : "");
    fflush(stderr);
}

/* Wait for the game process, with the session window's messages handled.
 * 0: it ended; 1: the session window was closed with no game window to pass
 * that on to. */
static int sw_wait(HANDLE process)
{
    for (;;) {
        MSG msg;
        DWORD r = MsgWaitForMultipleObjects(1, &process, FALSE, INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0 || r == WAIT_FAILED)
            return 0;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (s_sw.closing)
            return 1;
    }
}

/* `buffy_tvs.exe --prepare-shaders` or BTVS_PREPARE_SHADERS=1: make the
 * shaders and end (btvs_native.c, shaders_prepare), for an installer. */
int btvs_prepare_shaders_asked(void)
{
    static int asked = -1;
    if (asked < 0) {
        const char *e = getenv("BTVS_PREPARE_SHADERS");
        asked = (e && *e && *e != '0') || strstr(GetCommandLineA(), "--prepare-shaders") != NULL;
    }
    return asked;
}

/* 1: this process was the supervisor and the session is over (*exit_code is
 * the game's). 0: this process is the game; carry on. */
int btvs_supervise(int *exit_code)
{
    char exe[MAX_PATH], dir[MAX_PATH], launch[MAX_PATH], next[MAX_PATH], *sl;
    const char *ea = getenv("BTVS_EXIT_AFTER");
    double limit = ea ? atof(ea) : 0.0;
    DWORD t0 = GetTickCount(), code = 0;
    HANDLE job;
    int runs = 0, have_launch = 0;

    if (btvs_prepare_shaders_asked()) {
        /* no game is started: this process does the work itself, out of sight and silent */
        _putenv_s("BTVS_NO_WINDOW", "1");
        _putenv_s("BTVS_MUTE", "1");
        return 0;
    }
    if (getenv("BTVS_CHILD") || getenv("BTVS_NO_SUPERVISOR"))
        return 0;
    if (!GetModuleFileNameA(NULL, exe, sizeof exe))
        return 0;
    strcpy_s(dir, sizeof dir, exe);
    if ((sl = strrchr(dir, '\\')) != NULL)
        *sl = 0;
    sprintf_s(launch, sizeof launch, "%s\\btvs_launch_%lu.bin", dir, GetCurrentProcessId());
    sprintf_s(next, sizeof next, "%s\\btvs_launch_%lu.next", dir, GetCurrentProcessId());

    job = CreateJobObjectA(NULL, NULL);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
        memset(&li, 0, sizeof li);
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    }
    {   /* the supervisor itself keeps to the test run's CPUs and priority too */
        const char *aff = getenv("BTVS_AFFINITY");
        DWORD_PTR mask = aff ? (DWORD_PTR)strtoull(aff, NULL, 16) : 0;
        if (mask && SetProcessAffinityMask(GetCurrentProcess(), mask))
            SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    }
    {   /* BTVS_MAP=<map> (testing): the game starts as if it had launched
         * itself for that map -- the launch data it would have written, with
         * its own "map:" option -- instead of at the logos and menus.
         * BTVS_MAP_OPTIONS adds more of the game's options ("cheats:1"). */
        const char *map = getenv("BTVS_MAP"), *more = getenv("BTVS_MAP_OPTIONS");
        if (map && *map) {
            static unsigned char page[0x1000];
            FILE *f = NULL;
            memset(page, 0, sizeof page);
            *(unsigned *)(page + 4) = 0x45410012u;                       /* type 0: launched by a title; its id */
            strcpy_s((char *)page + 8, 520, "\\Device\\CdRom0;default.xbe");
            sprintf_s((char *)page + 0x400, 0xC00, "root:Z:\\ rpecopy:d:\\ controller:0 memoryunit:0 pack:2 map:%s%s%s",
                      map, more && *more ? " " : "", more && *more ? more : "");
            if (!fopen_s(&f, launch, "wb") && f) {
                fwrite(page, 1, sizeof page, f);
                fclose(f);
                have_launch = 1;
            }
        }
    }
    SetEnvironmentVariableA("BTVS_CHILD", "1");
    SetEnvironmentVariableA("RECOMP_RELAUNCH_FILE", next);
    sw_make();                                   /* the session's window (above) */

    for (;;) {
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        char left[32];
        double used = (GetTickCount() - t0) / 1000.0;
        if (limit > 0.0) {
            if (limit - used < 1.0)
                break;                                     /* the test run's time is up */
            sprintf_s(left, sizeof left, "%d", (int)(limit - used));
            SetEnvironmentVariableA("BTVS_EXIT_AFTER", left);
        }
        SetEnvironmentVariableA("RECOMP_LAUNCH_DATA_FILE", have_launch ? launch : NULL);
        if (runs) {
            /* scripted input: a script is for one run of the game. The n-th
             * restart plays BTVS_PAD_SCRIPT_<n> (1 = the first restart), or
             * nothing. */
            char name[40];
            sprintf_s(name, sizeof name, "BTVS_PAD_SCRIPT_%d", runs);
            SetEnvironmentVariableA("BTVS_PAD_SCRIPT", getenv(name));
        }
        SetEnvironmentVariableA("BTVS_RESTARTS", runs ? (sprintf_s(left, sizeof left, "%d", runs), left) : NULL);
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        if (!CreateProcessA(exe, GetCommandLineA(), NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
            fprintf(stderr, "[boot] cannot start the game process (error %lu)\n", GetLastError());
            code = 1;
            break;
        }
        if (job)
            AssignProcessToJobObject(job, pi.hProcess);
        AllowSetForegroundWindow(pi.dwProcessId);    /* its window takes the keyboard from the session window */
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        if (sw_wait(pi.hProcess)) {
            /* the session window was closed while no game window was up (between two runs, or before the first) */
            fprintf(stderr, "[boot] window: the session window was closed: the game is stopped\n");
            TerminateProcess(pi.hProcess, 0);
            WaitForSingleObject(pi.hProcess, 5000);
            CloseHandle(pi.hProcess);
            code = 0;
            break;
        }
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        runs++;
        if (code != XBOX_EXIT_RELAUNCH)
            break;
        /* the launch data the game left is the next run's */
        DeleteFileA(launch);
        if (!MoveFileA(next, launch)) {
            fprintf(stderr, "[boot] the game asked to be started again but left no launch data\n");
            code = 1;
            break;
        }
        have_launch = 1;
        fprintf(stderr, "[boot] the game restarts itself (run %d)\n", runs + 1);
        fflush(stderr);
    }
    DeleteFileA(launch);
    DeleteFileA(next);
    if (s_sw.hwnd) {
        fprintf(stderr, "[boot] window: session window %p closed after %d run(s) of the game\n", (void *)s_sw.hwnd, runs);
        DestroyWindow(s_sw.hwnd);
    }
    if (job)
        CloseHandle(job);
    *exit_code = (int)code;
    return 1;
}

/* ── The game restarting inside the process ──────────────────────────────
 *
 * The other way to give the game the reboot it asks for: not a new process,
 * but this one put back to where the game's entry point is about to be
 * called -- its other threads ended, its memory and the runtime's tables as
 * at the first start, the launch data kept (xboxrecomp: kernel_bridge.c,
 * g_xbox_reboot_inplace; xbox_memory_layout.c, xbox_RebootSnapshot). The
 * window, the Direct3D device, the sound device, the pads and every helper
 * thread of the port live on: to the player a level load is the game's own
 * loading screen in the same window, nothing else.
 *
 * Each area of the port keeps things about the game that a new process used
 * to forget for it. It forgets them here, in a function of its own, called on
 * the game thread after the game's other threads have ended and before its
 * memory is put back (so guest memory may still be read, and nothing of the
 * game runs):
 *
 *     void btvs_audio_reboot(void);     sounds stopped, DirectSound objects forgotten
 *     void btvs_input_reboot(void);     rumble off, per-run input state
 *     void btvs_menu_reboot(void);      cached game menu objects, one-time text patches
 *     void btvs_logs_reboot(void);      trail / recorder / replay state; the "boot" anchor
 *
 * An area that has none yet gets an empty one (the /alternatename lines).
 * BTVS_RESTARTS and RECOMP_LAUNCH_DATA_FILE are already the new run's when
 * they are called.
 *
 * BTVS_REBOOT=process: the old way (a new process per run, started by the
 * supervisor above). BTVS_REBOOT=inplace: this way.
 */
#include <setjmp.h>

jmp_buf g_btvs_reboot_jmp;                  /* WinMain's, set just before the entry point is called */
static int s_reboots;
static LARGE_INTEGER s_reboot_asked;

void btvs_reboot_none(void) { }
#pragma comment(linker, "/alternatename:btvs_audio_reboot=btvs_reboot_none")
#pragma comment(linker, "/alternatename:btvs_input_reboot=btvs_reboot_none")
#pragma comment(linker, "/alternatename:btvs_menu_reboot=btvs_reboot_none")
#pragma comment(linker, "/alternatename:btvs_logs_reboot=btvs_reboot_none")
void btvs_audio_reboot(void);
void btvs_input_reboot(void);
void btvs_menu_reboot(void);
void btvs_logs_reboot(void);

void btvs_log(const char *area, const char *fmt, ...);
void btvs_native_reboot(void);              /* btvs_native.c */
void xbox_RebootReset(const void *launch_page);
void xbox_RebootSnapshot(void);
extern void (*g_xbox_reboot_inplace)(const void *launch_page);

static double reboot_ms_since(LARGE_INTEGER t0)
{
    LARGE_INTEGER f, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
}

/* How many restarts this process has made of the game, and how long ago the
 * last one was asked for (ms; for "level load took ..." lines). */
int btvs_reboot_count(void) { return s_reboots; }
double btvs_reboot_ms(void) { return s_reboots ? reboot_ms_since(s_reboot_asked) : 0.0; }

static void reboot_inplace(const void *launch_page)
{
    char n[16];
    const char *file = getenv("RECOMP_RELAUNCH_FILE");
    const char *base = getenv("BTVS_RESTARTS");
    LARGE_INTEGER t0;
    double areas_ms, reset_ms;

    QueryPerformanceCounter(&t0);
    s_reboot_asked = t0;
    s_reboots++;
    /* The run's number and launch data, where the other areas look for them. */
    sprintf_s(n, sizeof n, "%d", (base ? atoi(base) : 0) + 1);
    _putenv_s("BTVS_RESTARTS", n);
    if (file && *file)
        _putenv_s("RECOMP_LAUNCH_DATA_FILE", file);
    btvs_log("boot", "the game restarts itself (run %s of the session, \"%.80s\"): in this process %lu, same window",
             n, (const char *)launch_page + 0x400, GetCurrentProcessId());

    btvs_audio_reboot();
    btvs_input_reboot();
    btvs_menu_reboot();
    btvs_logs_reboot();
    btvs_native_reboot();
    areas_ms = reboot_ms_since(t0);

    xbox_RebootReset(launch_page);
    reset_ms = reboot_ms_since(t0) - areas_ms;
    btvs_log("boot", "restart %d ready in %.0f ms (the port's areas %.0f ms, memory and runtime %.0f ms); starting the game",
             s_reboots, areas_ms + reset_ms, areas_ms, reset_ms);
    longjmp(g_btvs_reboot_jmp, 1);
}

/* WinMain, when everything is ready and the entry point has not run.
 * Inside the process once every area has its function (above): until then an
 * area would carry what it knew of the last run into the next. */
void btvs_reboot_arm(void)
{
    const char *e = getenv("BTVS_REBOOT");
    char missing[64] = "";
    if ((void (*)(void))btvs_audio_reboot == btvs_reboot_none) strcat_s(missing, sizeof missing, " audio");
    if ((void (*)(void))btvs_input_reboot == btvs_reboot_none) strcat_s(missing, sizeof missing, " input");
    if ((void (*)(void))btvs_menu_reboot == btvs_reboot_none)  strcat_s(missing, sizeof missing, " menu");
    if ((void (*)(void))btvs_logs_reboot == btvs_reboot_none)  strcat_s(missing, sizeof missing, " logs");
    if (e && !_stricmp(e, "process")) {
        btvs_log("boot", "restarts: a new process each time (BTVS_REBOOT=process)");
        return;
    }
    if (missing[0] && !(e && !_stricmp(e, "inplace"))) {
        btvs_log("boot", "restarts: a new process each time -- no btvs_<area>_reboot() yet for:%s (BTVS_REBOOT=inplace to try without)", missing);
        return;
    }
    xbox_RebootSnapshot();
    g_xbox_reboot_inplace = reboot_inplace;
    btvs_log("boot", "restarts: inside this process, same window%s%s", missing[0] ? "; areas with nothing to forget yet:" : "", missing);
}

/* (testing) BTVS_TEST_RELAUNCH=<seconds>:<map>[,<seconds>:<map>...]: at those
 * times after the start the game is restarted for that map, as if it had
 * asked for it itself -- a level change without playing to a level's end,
 * and at a moment the game did not choose. Called once a frame from the
 * game's Swap. */
void btvs_reboot_test_frame(double seconds)
{
    static const char *next;
    static int asked;
    void xbox_RebootNow(const void *launch_page);
    if (!asked) {
        asked = 1;
        next = getenv("BTVS_TEST_RELAUNCH");
    }
    if (!next || !*next)
        return;
    if (!g_xbox_reboot_inplace) {
        /* A new process per run (the comparison): run n of the session takes
         * the list's entry n-1, 25 s after its own start. */
        static int skipped;
        const char *r = getenv("BTVS_RESTARTS");
        int k = (r ? atoi(r) : 0) - 1;
        if (!skipped) {
            skipped = 1;
            while (k-- > 0 && next) {
                next = strchr(next, ',');
                if (next) next++;
            }
            if (k >= 0 || !r || !atoi(r))
                next = NULL;
        }
        if (!next || seconds < 25.0)
            return;
    } else if (seconds < atof(next)) {
        return;
    }
    {
        static unsigned char page[0x1000];
        char map[64];
        const char *c = strchr(next, ':'), *end;
        size_t n;
        if (!c) { next = NULL; return; }
        end = strchr(c, ',');
        n = end ? (size_t)(end - c - 1) : strlen(c + 1);
        if (n >= sizeof map) n = sizeof map - 1;
        memcpy(map, c + 1, n);
        map[n] = 0;
        next = end ? end + 1 : NULL;
        memset(page, 0, sizeof page);
        *(unsigned *)(page + 4) = 0x45410012u;
        strcpy_s((char *)page + 8, 520, "\\Device\\CdRom0;default.xbe");
        {
            /* The line the game itself writes for a change of level (its
             * format string is in the XBE), so the next run goes straight to
             * the map: no logos, no title. */
            static char prev[64] = "none";
            sprintf_s((char *)page + 0x400, 0xC00,
                      "root:Z:\\ rpecopy:d:\\ controller:0 memoryunit:0 pack:2 session:Buffy_1 sessionload:0 "
                      "preservebackbuffer:1 preservelogfile:1 spawn:0 map:%s title:0 previousmap:%s cheats:0 "
                      "resurrected:0 e3attract:0", map, prev);
            strcpy_s(prev, sizeof prev, map);
        }
        btvs_log("boot", "BTVS_TEST_RELAUNCH: asking for %s at %.1f s", map, seconds);
        if (g_xbox_reboot_inplace) {
            xbox_RebootNow(page);
        } else {
            const char *file = getenv("RECOMP_RELAUNCH_FILE");
            FILE *f = NULL;
            if (file && !fopen_s(&f, file, "wb") && f) {
                fwrite(page, 1, sizeof page, f);
                fclose(f);
                ExitProcess(XBOX_EXIT_RELAUNCH);
            }
        }
    }
}
