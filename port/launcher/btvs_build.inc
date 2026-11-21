/* Building the game's code on the player's PC (part of btvs_launcher.c).
 *
 * A release carries no translated game code. It carries buffy_tvs.exe (the
 * port's own code) and, in the folder "build" beside the launcher, what is
 * needed to make btvs_game.dll from the player's own Default.xbe:
 *
 *   build\kit.ini               [Kit] Id=<names this exact kit>
 *   build\btvs_translate.exe    Default.xbe + final.map -> C   (tools/gamedll/translate.py, frozen)
 *   build\llvm\bin\clang.exe, lld-link.exe; build\llvm\lib\clang\<n>\include   (LLVM, Apache-2.0)
 *   build\include\              the port's own few C headers
 *   build\game_dll.c, btvs_game_tls.h, game_link.def, manual_functions.json
 *   build\buffy_tvs_exe.lib, ucrt_min.lib, k32_min.lib        import libraries
 *
 * No "build" folder beside the launcher (the developer's prebuilt GAME
 * folder, whose exe has the game code inside): nothing here runs.
 *
 * Steps, each resumed where it stopped: translate -> compile every file (as
 * many at once as the PC has processors and memory for) -> link -> shaders.
 * Everything goes to install_log.txt in the game folder. The work folder
 * (build_work in the game folder, about 500 MB) is removed at the end.
 *
 * For tests on the developer's PC:
 *   BTVS_BUILD_AFFINITY=<hex mask>   the compilers' processors, at idle priority
 *   BTVS_BUILD_JOBS=<n>              how many compile at once
 *   BTVS_TRANSLATE_CMD=<command>     used instead of build\btvs_translate.exe
 *   BTVS_BUILD_STOP_AFTER=<n>        stop (as if cancelled) after n files compiled
 */

#define BUILD_DIR      L"build"
#define GAME_DLL       L"btvs_game.dll"
#define GAME_DLL_OK    L"btvs_game.ok"       /* holds the kit's Id the DLL was built with */
#define WORK_DIR       L"build_work"
#define INSTALL_LOG    L"install_log.txt"
#define BUILD_MAX_JOBS 32
#define BUILD_MAX_SRC  256

/* where each step sits in the bar, in permille (the disc copy has 0..B_P_COPY) */
enum { B_P_COPY = 400, B_P_TRANSLATE = 500, B_P_COMPILE = 960, B_P_LINK = 975, B_P_SHADERS = 1000 };

static FILE *s_blog;

static void blog(const WCHAR *fmt, ...)
{
    WCHAR w[2048];
    char u[4096];
    SYSTEMTIME t;
    va_list ap;
    if (!s_blog)
        return;
    va_start(ap, fmt);
    vswprintf_s(w, 2048, fmt, ap);
    va_end(ap);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, NULL, NULL);
    GetLocalTime(&t);
    fprintf(s_blog, "%04d-%02d-%02d %02d:%02d:%02d  %s\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, u);
    fflush(s_blog);
}

static void kit_path(WCHAR *out, const WCHAR *name)
{
    WCHAR k[MAX_PATH];
    join(k, s_launcher_dir, BUILD_DIR);
    join(out, k, name);
}

/* The Id of the build kit beside the launcher; 0 when there is none. */
static int kit_id(WCHAR *id, DWORD cap)
{
    WCHAR ini[MAX_PATH];
    kit_path(ini, L"kit.ini");
    id[0] = 0;
    if (!file_exists(ini))
        return 0;
    GetPrivateProfileStringW(L"Kit", L"Id", L"", id, cap, ini);
    return id[0] != 0;
}

static int read_small(const WCHAR *path, WCHAR *out, DWORD cap)
{
    FILE *f = NULL;
    char u[512];
    size_t n;
    out[0] = 0;
    if (_wfopen_s(&f, path, L"rb") || !f)
        return 0;
    n = fread(u, 1, sizeof u - 1, f);
    fclose(f);
    u[n] = 0;
    while (n && (u[n - 1] == '\n' || u[n - 1] == '\r' || u[n - 1] == ' '))
        u[--n] = 0;
    MultiByteToWideChar(CP_UTF8, 0, u, -1, out, (int)cap);
    return 1;
}

static int write_small(const WCHAR *path, const WCHAR *text)
{
    FILE *f = NULL;
    char u[512];
    if (_wfopen_s(&f, path, L"wb") || !f)
        return 0;
    WideCharToMultiByte(CP_UTF8, 0, text, -1, u, sizeof u, NULL, NULL);
    fputs(u, f);
    fclose(f);
    return 1;
}

/* Is this folder's buffy_tvs.exe the kind that loads the game's code from
 * btvs_game.dll (a release's), not the kind with the code inside (the
 * developer's prebuilt one)? It says so in its import table; the answer is
 * kept for as long as the file does not change. */
static int exe_needs_game_dll(const WCHAR *dir)
{
    static WCHAR last[MAX_PATH];
    static FILETIME last_time;
    static DWORD last_size;
    static int last_answer;
    static const char name[] = "btvs_game.dll";
    WCHAR p[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA fa;
    uint8_t *data;
    size_t n, i;
    join(p, dir, GAME_EXE);
    if (!GetFileAttributesExW(p, GetFileExInfoStandard, &fa))
        return 0;
    if (!wcscmp(p, last) && fa.nFileSizeLow == last_size && !CompareFileTime(&fa.ftLastWriteTime, &last_time))
        return last_answer;
    wcscpy_s(last, MAX_PATH, p);
    last_size = fa.nFileSizeLow;
    last_time = fa.ftLastWriteTime;
    last_answer = 0;
    data = read_whole_file(p, &n);
    if (!data)
        return 0;
    for (i = 0; i + sizeof name <= n; i++)
        if (data[i] == 'b' && !memcmp(data + i, name, sizeof name - 1)) {
            last_answer = 1;
            break;
        }
    free(data);
    return last_answer;
}

/* Does this install still need its game code built (or built again, for a
 * newer release beside this launcher)? */
static int game_code_missing(const WCHAR *dir)
{
    WCHAR id[128], have[128], p[MAX_PATH];
    if (!exe_needs_game_dll(dir))
        return 0;
    join(p, dir, GAME_DLL);
    if (!file_exists(p))
        return 1;
    if (!kit_id(id, 128))
        return 0;                       /* a launcher without the kit beside it: what is there is what there is */
    join(p, dir, GAME_DLL_OK);
    return !read_small(p, have, 128) || wcscmp(have, id) != 0;
}

static void remove_tree(const WCHAR *dir)
{
    WCHAR pat[MAX_PATH], p[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    join(pat, dir, L"*");
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
                continue;
            join(p, dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                remove_tree(p);
            } else {
                SetFileAttributesW(p, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(p);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

/* Start a program with no window, at low priority, its output going to
 * `out` (a file or pipe handle; may be NULL). The command line is logged. */
static HANDLE spawn(WCHAR *cmd, const WCHAR *cwd, HANDLE out)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    const char *aff = getenv("BTVS_BUILD_AFFINITY");
    DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED | (aff ? IDLE_PRIORITY_CLASS : BELOW_NORMAL_PRIORITY_CLASS);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (out) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = out;
        si.hStdError = out;
        SetHandleInformation(out, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    }
    if (!CreateProcessW(NULL, cmd, NULL, NULL, out != NULL, flags, NULL, cwd, &si, &pi)) {
        blog(L"could not start (error %lu): %s", GetLastError(), cmd);
        return NULL;
    }
    if (aff) {
        DWORD_PTR mask = (DWORD_PTR)strtoull(aff, NULL, 16);
        if (mask)
            SetProcessAffinityMask(pi.hProcess, mask);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

static HANDLE open_out(const WCHAR *path)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    return h == INVALID_HANDLE_VALUE ? NULL : h;
}

/* The first lines of a tool's output file, into the install log. */
static void blog_file(const WCHAR *path, int max_lines)
{
    FILE *f = NULL;
    char line[1024];
    WCHAR w[1024];
    if (_wfopen_s(&f, path, L"rb") || !f)
        return;
    while (max_lines-- > 0 && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        MultiByteToWideChar(CP_UTF8, 0, line, -1, w, 1024);
        blog(L"    %s", w);
    }
    fclose(f);
}

static int build_fail(const WCHAR *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vswprintf_s(s_install_msg, 1024, fmt, ap);
    va_end(ap);
    blog(L"FAILED: %s", s_install_msg);
    return 0;
}

/* Step 1: Default.xbe -> C. Progress comes as "STEP n/of text" lines. */
static int build_translate(const WCHAR *target, const WCHAR *work)
{
    WCHAR cmd[4096], exe[MAX_PATH], xbe[MAX_PATH], map[MAX_PATH], manual[MAX_PATH], tls[MAX_PATH], logp[MAX_PATH], ok[MAX_PATH];
    WCHAR over[2048];
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE rd = NULL, wr = NULL, proc;
    char buf[512], line[512];
    DWORD n, code = 1;
    size_t len = 0;

    join(ok, work, L"translated.ok");
    if (file_exists(ok)) {
        blog(L"translate: already done");
        return 1;
    }
    join(xbe, target, L"Default.xbe");
    join(map, target, L"final.map");
    kit_path(exe, L"btvs_translate.exe");
    kit_path(manual, L"manual_functions.json");
    kit_path(tls, L"btvs_game_tls.h");
    if (!file_exists(map))
        return build_fail(L"final.map is not in the game folder. It is on the game disc; install from the disc image again.");
    if (GetEnvironmentVariableW(L"BTVS_TRANSLATE_CMD", over, 2048) && over[0])
        swprintf_s(cmd, 4096, L"%s \"%s\" \"%s\" \"%s\" \"%s\" --dll \"%s\"", over, xbe, map, manual, work, tls);
    else if (!file_exists(exe))
        return build_fail(L"The release is incomplete: build\\btvs_translate.exe is missing beside the launcher. Unpack the whole release again.");
    else
        swprintf_s(cmd, 4096, L"\"%s\" \"%s\" \"%s\" \"%s\" \"%s\" --dll \"%s\"", exe, xbe, map, manual, work, tls);
    blog(L"translate: %s", cmd);
    if (!CreatePipe(&rd, &wr, &sa, 0))
        return build_fail(L"Could not start translating the game (no pipe, error %lu).", GetLastError());
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    proc = spawn(cmd, work, wr);
    CloseHandle(wr);
    if (!proc) {
        CloseHandle(rd);
        return build_fail(L"Could not start translating the game's code (see " INSTALL_LOG L").");
    }
    post_progress(B_P_COPY, L"Translating the game's code for this PC...");
    while (ReadFile(rd, buf, sizeof buf, &n, NULL) && n) {
        DWORD i;
        for (i = 0; i < n; i++) {
            if (buf[i] != '\n') {
                if (len < sizeof line - 1 && buf[i] != '\r')
                    line[len++] = buf[i];
                continue;
            }
            line[len] = 0;
            len = 0;
            {
                int a = 0, b = 0, used = 0;
                WCHAR w[512];
                MultiByteToWideChar(CP_UTF8, 0, line, -1, w, 512);
                blog(L"  %s", w);
                if (sscanf_s(line, "STEP %d/%d %n", &a, &b, &used) >= 2 && b > 0) {
                    MultiByteToWideChar(CP_UTF8, 0, line + used, -1, w, 512);
                    post_progress(B_P_COPY + (B_P_TRANSLATE - B_P_COPY) * (a - 1) / b,
                                  L"Translating the game's code (step %d of %d): %s", a, b, w);
                }
            }
        }
        if (s_cancel)
            TerminateProcess(proc, 1);
    }
    CloseHandle(rd);
    WaitForSingleObject(proc, INFINITE);
    GetExitCodeProcess(proc, &code);
    CloseHandle(proc);
    if (s_cancel)
        return build_fail(L"Cancelled while translating. Install again to carry on.");
    if (code) {
        join(logp, work, L"translate.log");
        blog(L"translate ended with code %lu; the end of its log is in %s", code, logp);
        return build_fail(L"Translating the game's code failed (code %lu). The details are in " INSTALL_LOG
                          L" and " WORK_DIR L"\\translate.log in the game folder.", code);
    }
    write_small(ok, L"ok");
    return 1;
}

typedef struct { WCHAR name[64]; uint64_t size; int state; /* 0 to do, 1 running, 2 done */ } BuildSrc;

static int src_bigger(const void *a, const void *b)
{
    uint64_t x = ((const BuildSrc *)a)->size, y = ((const BuildSrc *)b)->size;
    return x < y ? 1 : x > y ? -1 : wcscmp(((const BuildSrc *)a)->name, ((const BuildSrc *)b)->name);
}

static int build_jobs(void)
{
    SYSTEM_INFO si;
    MEMORYSTATUSEX ms;
    const char *e = getenv("BTVS_BUILD_JOBS");
    int n, by_mem;
    if (e && atoi(e) > 0)
        return atoi(e) > BUILD_MAX_JOBS ? BUILD_MAX_JOBS : atoi(e);
    GetSystemInfo(&si);
    n = (int)si.dwNumberOfProcessors;
    ms.dwLength = sizeof ms;
    GlobalMemoryStatusEx(&ms);
    by_mem = (int)(ms.ullTotalPhys / (1536ull << 20));   /* allow 1.5 GB a compiler */
    if (n > by_mem)
        n = by_mem;
    return n < 1 ? 1 : n > BUILD_MAX_JOBS ? BUILD_MAX_JOBS : n;
}

/* Step 2: every C file to an object file. A file whose object is there is
 * done (the object is written under another name and renamed when whole). */
static int build_compile(const WCHAR *work)
{
    static BuildSrc src[BUILD_MAX_SRC];
    WCHAR gen[MAX_PATH], obj[MAX_PATH], pat[MAX_PATH], clang[MAX_PATH], inc[MAX_PATH], gdll[MAX_PATH];
    WCHAR cmd[4096], a[MAX_PATH], b[MAX_PATH], c[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h, proc[BUILD_MAX_JOBS];
    int job_src[BUILD_MAX_JOBS];
    int n = 0, i, running = 0, jobs = build_jobs(), failed = -1, compiled = 0;
    int shown = -1;
    int stop_after = getenv("BTVS_BUILD_STOP_AFTER") ? atoi(getenv("BTVS_BUILD_STOP_AFTER")) : 0;
    uint64_t total = 0, done = 0;
    DWORD t0 = GetTickCount();

    join(gen, work, L"gen");
    join(obj, work, L"obj");
    CreateDirectoryW(obj, NULL);
    kit_path(clang, L"llvm\\bin\\clang.exe");
    kit_path(inc, L"include");
    kit_path(gdll, L"game_dll.c");
    if (!file_exists(clang))
        return build_fail(L"The release is incomplete: build\\llvm\\bin\\clang.exe is missing beside the launcher. Unpack the whole release again.");

    join(pat, gen, L"*.c");
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (n < BUILD_MAX_SRC - 1 && wcslen(fd.cFileName) < 60) {
                wcscpy_s(src[n].name, 64, fd.cFileName);
                src[n].size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                src[n].state = 0;
                n++;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (n < 10)
        return build_fail(L"The translated code is not there (%d files in " WORK_DIR L"\\gen).", n);
    qsort(src, (size_t)n, sizeof src[0], src_bigger);       /* the big files first: they decide the time */
    wcscpy_s(src[n].name, 64, L"game_dll.c");                /* the DLL's own few lines, from the kit */
    src[n].size = 1;
    src[n].state = 0;
    n++;
    for (i = 0; i < n; i++) {
        swprintf_s(a, MAX_PATH, L"%s\\%s.obj", obj, src[i].name);
        total += src[i].size;
        if (file_exists(a)) {
            src[i].state = 2;
            done += src[i].size;
        }
    }
    blog(L"compile: %d files, %.0f MB, %d at a time, %.0f MB already compiled", n, total / 1048576.0, jobs, done / 1048576.0);

    for (;;) {
        /* start what there is room for */
        while (running < jobs && failed < 0 && !s_cancel) {
            HANDLE out;
            for (i = 0; i < n && src[i].state; i++)
                ;
            if (i == n)
                break;
            swprintf_s(a, MAX_PATH, L"%s\\%s.obj.tmp", obj, src[i].name);
            swprintf_s(b, MAX_PATH, L"%s\\%s.txt", obj, src[i].name);
            if (!wcscmp(src[i].name, L"game_dll.c"))
                wcscpy_s(c, MAX_PATH, gdll);
            else
                join(c, gen, src[i].name);
            swprintf_s(cmd, 4096, L"\"%s\" -c -O2 -w -nostdlibinc -DBTVS_GAME_OWN_MEM_OFFSET -I \"%s\" -I \"%s\" \"%s\" -o \"%s\"",
                       clang, inc, gen, c, a);
            out = open_out(b);
            proc[running] = spawn(cmd, work, out);
            if (out)
                CloseHandle(out);
            if (!proc[running]) {
                failed = i;
                break;
            }
            src[i].state = 1;
            job_src[running++] = i;
        }
        if (!running)
            break;
        {
            DWORD w = WaitForMultipleObjects((DWORD)running, proc, FALSE, 500);
            if (s_cancel || failed >= 0) {
                for (i = 0; i < running; i++) {
                    TerminateProcess(proc[i], 1);
                    WaitForSingleObject(proc[i], 5000);
                    CloseHandle(proc[i]);
                }
                running = 0;
                break;
            }
            if (w < WAIT_OBJECT_0 + (DWORD)running) {
                int k = (int)(w - WAIT_OBJECT_0), s = job_src[k];
                DWORD code = 1;
                GetExitCodeProcess(proc[k], &code);
                CloseHandle(proc[k]);
                proc[k] = proc[running - 1];
                job_src[k] = job_src[running - 1];
                running--;
                swprintf_s(a, MAX_PATH, L"%s\\%s.obj.tmp", obj, src[s].name);
                swprintf_s(b, MAX_PATH, L"%s\\%s.obj", obj, src[s].name);
                if (code == 0 && MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING)) {
                    src[s].state = 2;
                    done += src[s].size;
                    compiled++;
                    blog(L"  compiled %s", src[s].name);
                    if (stop_after && compiled >= stop_after)
                        s_cancel = 1;
                } else {
                    blog(L"  %s: the compiler ended with code %lu", src[s].name, code);
                    swprintf_s(c, MAX_PATH, L"%s\\%s.txt", obj, src[s].name);
                    blog_file(c, 20);
                    DeleteFileW(a);
                    failed = s;
                }
            }
            {
                DWORD el = (GetTickCount() - t0) / 1000;
                int ndone = 0, q;
                for (q = 0; q < n; q++)
                    ndone += src[q].state == 2;
                if (s_wnd || ndone != shown)
                    post_progress(B_P_TRANSLATE + (int)((B_P_COMPILE - B_P_TRANSLATE) * done / (total ? total : 1)),
                              L"Building the game for this PC: %d of %d parts done, %lu:%02lu so far (%d at a time)",
                              ndone, n, el / 60, el % 60, jobs);
                shown = ndone;
            }
        }
    }
    if (failed >= 0)
        return build_fail(L"Building the game failed at %s. The compiler's message is in " INSTALL_LOG
                          L" in the game folder. Install again to retry: what is built is kept.", src[failed].name);
    if (s_cancel)
        return build_fail(L"Cancelled while building. Install again to carry on: what is built is kept.");
    for (i = 0; i < n; i++)
        if (src[i].state != 2)
            return build_fail(L"Building the game stopped early at %s.", src[i].name);
    blog(L"compile: done in %lu s", (GetTickCount() - t0) / 1000);
    return 1;
}

/* Step 3: the objects to btvs_game.dll in the game folder. */
static int build_link(const WCHAR *target, const WCHAR *work)
{
    WCHAR lld[MAX_PATH], def[MAX_PATH], l1[MAX_PATH], l2[MAX_PATH], l3[MAX_PATH], rsp[MAX_PATH], out[MAX_PATH], dll[MAX_PATH];
    WCHAR obj[MAX_PATH], pat[MAX_PATH], tmp[MAX_PATH], pdb[MAX_PATH], cmd[2048];
    WIN32_FIND_DATAW fd;
    HANDLE h, proc, o;
    FILE *f = NULL;
    DWORD code = 1;
    char u[1024];

    kit_path(lld, L"llvm\\bin\\lld-link.exe");
    kit_path(def, L"game_link.def");
    kit_path(l1, L"buffy_tvs_exe.lib");
    kit_path(l2, L"ucrt_min.lib");
    kit_path(l3, L"k32_min.lib");
    if (!file_exists(lld) || !file_exists(def) || !file_exists(l1) || !file_exists(l2) || !file_exists(l3))
        return build_fail(L"The release is incomplete: files are missing from the build folder beside the launcher. Unpack the whole release again.");
    join(obj, work, L"obj");
    join(rsp, work, L"link.rsp");
    join(out, work, L"link.txt");
    join(tmp, work, L"btvs_game.dll");
    join(pdb, target, L"btvs_game.pdb");
    join(dll, target, GAME_DLL);
    if (_wfopen_s(&f, rsp, L"wb") || !f)
        return build_fail(L"Could not write in the work folder (error %lu).", GetLastError());
#define RSP(fmt, w) (WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, NULL, NULL), fprintf(f, fmt "\n", u))
    fprintf(f, "-dll -nodefaultlib -entry:DllMain -nologo -debug\n");
    RSP("\"-out:%s\"", tmp);
    RSP("\"-pdb:%s\"", pdb);
    RSP("\"-def:%s\"", def);
    RSP("\"%s\"", l1);
    RSP("\"%s\"", l2);
    RSP("\"%s\"", l3);
    join(pat, obj, L"*.obj");
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            WCHAR p[MAX_PATH];
            join(p, obj, fd.cFileName);
            RSP("\"%s\"", p);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
#undef RSP
    fclose(f);
    swprintf_s(cmd, 2048, L"\"%s\" \"@%s\"", lld, rsp);
    blog(L"link: %s", cmd);
    post_progress(B_P_COMPILE, L"Putting the game together...");
    o = open_out(out);
    proc = spawn(cmd, work, o);
    if (o)
        CloseHandle(o);
    if (!proc)
        return build_fail(L"Could not start putting the game together (see " INSTALL_LOG L").");
    WaitForSingleObject(proc, INFINITE);
    GetExitCodeProcess(proc, &code);
    CloseHandle(proc);
    blog_file(out, 30);
    if (code)
        return build_fail(L"Putting the game together failed (code %lu). The details are in " INSTALL_LOG L" in the game folder.", code);
    if (!MoveFileExW(tmp, dll, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
        return build_fail(L"Could not put " GAME_DLL L" in the game folder (error %lu). Is the game running?", GetLastError());
    return 1;
}

/* Step 4: the shaders (Graphics' buffy_tvs.exe --prepare-shaders). Not
 * fatal: the game makes at its start whatever is missing. */
static void build_shaders(const WCHAR *target)
{
    WCHAR cmd[1024], exe[MAX_PATH], out[MAX_PATH];
    HANDLE proc, o;
    DWORD code = 1;
    join(exe, target, GAME_EXE);
    join(out, target, L"build_shaders.txt");
    swprintf_s(cmd, 1024, L"\"%s\" --prepare-shaders", exe);
    post_progress(B_P_LINK, L"Preparing the shaders for this PC's graphics card...");
    blog(L"shaders: %s", cmd);
    o = open_out(out);
    proc = spawn(cmd, target, o);
    if (o)
        CloseHandle(o);
    if (!proc)
        return;
    if (WaitForSingleObject(proc, 300000) == WAIT_TIMEOUT) {
        blog(L"shaders: no end after 5 minutes; stopped (the game makes them at its start)");
        TerminateProcess(proc, 1);
    }
    GetExitCodeProcess(proc, &code);
    CloseHandle(proc);
    blog_file(out, 400);
    DeleteFileW(out);
    blog(L"shaders: ended with code %lu%s", code,
         code == 0 ? L"" : code == 2 ? L" (some could not be made; the game makes them at first use)"
                                     : L" (the game makes them at its start)");
}

/* The whole build, after the game's files and the port's are in `target`.
 * 1 = the game is ready; 0 = not, with s_install_msg saying why. */
static int build_game(const WCHAR *target)
{
    WCHAR id[128], have[128], work[MAX_PATH], p[MAX_PATH], logp[MAX_PATH];
    ULARGE_INTEGER freeb;
    DWORD t0 = GetTickCount();
    int ok;

    if (!game_code_missing(target))
        return 1;
    if (!kit_id(id, 128)) {
        s_install_msg[0] = 0;
        return build_fail(L"The game has to be built for this PC, and this launcher has no \"build\" folder beside it to do "
                          L"that with. Start the launcher that is in the unpacked release, and install to this folder again.");
    }
    join(logp, target, INSTALL_LOG);
    if (_wfopen_s(&s_blog, logp, L"ab"))
        s_blog = NULL;
    blog(L"---- building the game's code, kit %s, launcher %s ----", id, WIDEN(BTVS_VERSION));
    join(work, target, WORK_DIR);
    join(p, work, L"kit.id");
    if (dir_exists(work) && (!read_small(p, have, 128) || wcscmp(have, id))) {
        blog(L"the work folder is from another kit (%s): started afresh", have);
        remove_tree(work);
    }
    CreateDirectoryW(work, NULL);
    write_small(p, id);
    if (GetDiskFreeSpaceExW(target, &freeb, NULL, NULL) && freeb.QuadPart < (900ull << 20)) {
        ok = build_fail(L"Not enough free space to build the game: about 0.9 GB is needed in the game folder while it builds "
                        L"(the drive has %.1f GB free). It is given back afterwards.", freeb.QuadPart / 1073741824.0);
    } else {
        ok = build_translate(target, work) && build_compile(work) && build_link(target, work);
    }
    if (ok) {
        join(p, target, GAME_DLL_OK);
        write_small(p, id);
        build_shaders(target);
        remove_tree(work);
        blog(L"done in %lu s", (GetTickCount() - t0) / 1000);
        post_progress(1000, L"Installed.");
    }
    if (s_blog)
        fclose(s_blog);
    s_blog = NULL;
    return ok;
}
