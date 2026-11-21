/* Crash and hang reports.
 *
 *   crashes\crash_<yyyymmdd-hhmmss>.txt + .dmp   beside the exe, and in the log
 *   crashes\hang_<yyyymmdd-hhmmss>.txt
 *
 * A crash report names the guest function (the game's own name from its
 * linker map: every translated function is a host symbol called Name_<VA>),
 * the guest registers, the recent indirect calls, the return addresses on the
 * guest stack, the host stack, the last trail events and the memory figures;
 * the .dmp is a minidump for a debugger.
 *
 * The report is written by a helper thread while the faulting thread waits:
 * the faulting thread only copies its guest registers (they are thread-local).
 * So a stack overflow, or a fault inside the C library, still gets a report.
 *
 * Guest names need the build's .pdb beside the exe (dev builds have it) or a
 * btvs_symbols.txt (port/tools/report_symbols.py). Without either the report
 * carries addresses, and port/tools/report_open.py names them afterwards.
 *
 * Test aids: BTVS_TEST_CRASH=1 (at start, on the game thread) or =<seconds>;
 * BTVS_HANG_SECS=<n> (frames stopped for n seconds: default 20, 0 = off),
 * BTVS_BOOT_HANG_SECS=<n> (no first frame after n seconds: default 90).
 */
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xbox/xboxrecomp.h>
#include "btvs_log.h"

#pragma comment(lib, "dbghelp.lib")

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
extern RECOMP_TLS uint32_t g_ebx, g_esi, g_edi, g_ebp;
extern ptrdiff_t g_xbox_mem_offset;
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count;
void xbox_WatchdogReportTo(void *out, const char *why);
long xbox_HangMonitorFrames(void);
unsigned long xbox_HangMonitorIdleMs(void);

/* ── Guest symbols ──────────────────────────────────────────────────────── */

typedef struct { uint32_t va; uint32_t name; } sym_t;     /* name: offset into s_names */
static sym_t *s_syms;
static char *s_names;
static size_t s_nsyms, s_names_len, s_names_cap, s_syms_cap;
static volatile LONG s_sym_state;                         /* 0 no, 1 loading, 2 ready */
static CRITICAL_SECTION s_dbghelp;                        /* dbghelp is single-threaded */
static volatile DWORD s_report_thread;                    /* the helper's id: its own faults are not crashes */

static void sym_add(uint32_t va, const char *name, size_t len)
{
    if (s_nsyms >= s_syms_cap || s_names_len + len + 1 > s_names_cap)
        return;
    s_syms[s_nsyms].va = va;
    s_syms[s_nsyms].name = (uint32_t)s_names_len;
    memcpy(s_names + s_names_len, name, len);
    s_names[s_names_len + len] = 0;
    s_names_len += len + 1;
    s_nsyms++;
}

static int is_hex8(const char *p)
{
    int i;
    for (i = 0; i < 8; i++)
        if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'A' && p[i] <= 'F') || (p[i] >= 'a' && p[i] <= 'f')))
            return 0;
    return 1;
}

/* A host symbol "TString_SetLength_00011020" -> guest 0x00011020 "TString_SetLength". */
static BOOL CALLBACK enum_cb(PSYMBOL_INFO si, ULONG size, PVOID ctx)
{
    size_t len = si->NameLen ? si->NameLen : strlen(si->Name);
    (void)size; (void)ctx;
    while (len && si->Name[len - 1] == 0)
        len--;
    if (len > 9 && si->Name[len - 9] == '_' && is_hex8(si->Name + len - 8)) {
        uint32_t va = (uint32_t)strtoul(si->Name + len - 8, NULL, 16);
        if (va >= 0x10000u && va < 0x1000000u && strncmp(si->Name, "sub_", 4) != 0)
            sym_add(va, si->Name, len - 9);
        else if (va >= 0x10000u && va < 0x1000000u)
            sym_add(va, si->Name, len);
    }
    return TRUE;
}

static int sym_cmp(const void *a, const void *b)
{
    uint32_t x = ((const sym_t *)a)->va, y = ((const sym_t *)b)->va;
    return x < y ? -1 : x > y;
}

static DWORD WINAPI sym_thread(LPVOID arg)
{
    char path[MAX_PATH], line[512];
    FILE *f;
    double t0 = btvs_uptime();
    const char *from = "btvs_symbols.txt";
    (void)arg;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    s_syms_cap = 70000;
    s_names_cap = 6u << 20;
    s_syms = (sym_t *)VirtualAlloc(NULL, s_syms_cap * sizeof(sym_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    s_names = (char *)VirtualAlloc(NULL, s_names_cap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!s_syms || !s_names)
        return 0;

    sprintf_s(path, MAX_PATH, "%s\\btvs_symbols.txt", btvs_exe_dir());
    if (fopen_s(&f, path, "r") == 0 && f) {
        while (fgets(line, sizeof line, f)) {            /* "0001A2B0 Name" */
            size_t len = strcspn(line, "\r\n");
            if (len > 9 && is_hex8(line) && line[8] == ' ')
                sym_add((uint32_t)strtoul(line, NULL, 16), line + 9, len - 9);
        }
        fclose(f);
    }
    if (!s_nsyms) {
        from = "the .pdb";
        EnterCriticalSection(&s_dbghelp);
        SymEnumSymbols(GetCurrentProcess(), (ULONG64)(uintptr_t)GetModuleHandleA(NULL), "*_????????", enum_cb, NULL);
        LeaveCriticalSection(&s_dbghelp);
    }
    qsort(s_syms, s_nsyms, sizeof(sym_t), sym_cmp);
    if (s_nsyms) {
        InterlockedExchange(&s_sym_state, 2);
        btvs_log("crash", "guest function names: %u from %s (%.1f s)", (unsigned)s_nsyms, from, btvs_uptime() - t0);
    } else {
        InterlockedExchange(&s_sym_state, 0);
        btvs_log("crash", "no guest function names (no .pdb or btvs_symbols.txt beside the exe): "
                          "reports carry addresses; port/tools/report_open.py names them");
    }
    return 0;
}

int btvs_sym_ready(void) { return s_sym_state == 2; }

int btvs_sym_guest(uint32_t va, char *out, size_t n)
{
    size_t lo = 0, hi = s_nsyms;
    if (n)
        out[0] = 0;
    if (s_sym_state != 2 || !s_nsyms || va < s_syms[0].va)
        return 0;
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (s_syms[mid].va <= va)
            lo = mid;
        else
            hi = mid;
    }
    if (va - s_syms[lo].va > 0x20000u)
        return 0;
    if (va == s_syms[lo].va)
        _snprintf_s(out, n, _TRUNCATE, "%s", s_names + s_syms[lo].name);
    else
        _snprintf_s(out, n, _TRUNCATE, "%s+0x%X", s_names + s_syms[lo].name, va - s_syms[lo].va);
    return 1;
}

/* ── The report text ────────────────────────────────────────────────────── */

typedef struct {
    char *p;
    size_t n, cap;
} text_t;

static void put(text_t *t, const char *fmt, ...)
{
    va_list ap;
    int m;
    if (t->n + 1 >= t->cap)
        return;
    va_start(ap, fmt);
    m = _vsnprintf_s(t->p + t->n, t->cap - t->n, _TRUNCATE, fmt, ap);
    va_end(ap);
    t->n += m < 0 ? strlen(t->p + t->n) : (size_t)m;
}

static int guest_read32(uint32_t va, uint32_t *out)
{
    SIZE_T got = 0;
    if (!g_xbox_mem_offset)
        return 0;
    return ReadProcessMemory(GetCurrentProcess(), (const void *)((uintptr_t)g_xbox_mem_offset + va), out, 4, &got)
           && got == 4;
}

static int is_guest_code(uint32_t va) { return va > g_xbox_code_lo && va < g_xbox_code_hi; }

static void put_guest_addr(text_t *t, const char *lead, uint32_t va)
{
    char name[200];
    if (btvs_sym_guest(va, name, sizeof name))
        put(t, "%s0x%08X  %s\n", lead, va, name);
    else
        put(t, "%s0x%08X\n", lead, va);
}

static void host_name(DWORD64 addr, char *out, size_t n)
{
    char buf[sizeof(SYMBOL_INFO) + 300];
    SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
    IMAGEHLP_LINE64 ln;
    IMAGEHLP_MODULE64 mod;
    DWORD64 disp = 0;
    DWORD ldisp = 0;
    size_t len;

    memset(buf, 0, sizeof buf);
    si->SizeOfStruct = sizeof(SYMBOL_INFO);
    si->MaxNameLen = 299;
    memset(&mod, 0, sizeof mod);
    mod.SizeOfStruct = sizeof mod;
    out[0] = 0;
    if (SymGetModuleInfo64(GetCurrentProcess(), addr, &mod))
        _snprintf_s(out, n, _TRUNCATE, "%s!", mod.ModuleName);
    len = strlen(out);
    if (SymFromAddr(GetCurrentProcess(), addr, &disp, si))
        _snprintf_s(out + len, n - len, _TRUNCATE, "%s+0x%llX", si->Name, (unsigned long long)disp);
    else
        _snprintf_s(out + len, n - len, _TRUNCATE, "0x%llX", (unsigned long long)addr);
    len = strlen(out);
    memset(&ln, 0, sizeof ln);
    ln.SizeOfStruct = sizeof ln;
    if (SymGetLineFromAddr64(GetCurrentProcess(), addr, &ldisp, &ln) && ln.FileName) {
        const char *base = strrchr(ln.FileName, '\\');
        _snprintf_s(out + len, n - len, _TRUNCATE, "  (%s:%lu)", base ? base + 1 : ln.FileName,
                    (unsigned long)ln.LineNumber);
    }
}

static void put_host_stack(text_t *t, HANDLE thread, const CONTEXT *start)
{
    CONTEXT c = *start;
    STACKFRAME64 sf;
    int i;

    memset(&sf, 0, sizeof sf);
    sf.AddrPC.Offset = c.Rip;
    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = c.Rbp;
    sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = c.Rsp;
    sf.AddrStack.Mode = AddrModeFlat;
    for (i = 0; i < 48; i++) {
        char name[420];
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), thread, &sf, &c, NULL,
                         SymFunctionTableAccess64, SymGetModuleBase64, NULL) || !sf.AddrPC.Offset)
            break;
        host_name(sf.AddrPC.Offset, name, sizeof name);
        put(t, "  #%-2d %s\n", i, name);
    }
}

static const char *code_name(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case EXCEPTION_BREAKPOINT: return "breakpoint";
    case 0xE06D7363: return "C++ exception";
    case 0xC0000409: return "stack buffer overrun / fast fail";
    default: return "exception";
    }
}

static void stamp(char *out, size_t n)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    _snprintf_s(out, n, _TRUNCATE, "%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                t.wSecond);
}

static void crash_dir(char *out)
{
    sprintf_s(out, MAX_PATH, "%s\\crashes", btvs_exe_dir());
    CreateDirectoryA(out, NULL);
}

static void put_common_tail(text_t *t)
{
    static char tail[12000];
    static char memtext[1500];
    struct btvs_mem m;
    struct btvs_where w;

    btvs_trail_where(&w);
    put(t, "\nWhere in the game: level \"%s\"%s%s, last anchor %s \"%s\" at %.1f s (frame %ld)\n", w.level,
        w.checkpoint[0] ? ", checkpoint " : "", w.checkpoint, w.anchor_event, w.anchor_name, w.anchor_time,
        w.anchor_frame);
    if (w.have_pos)
        put(t, "Player: position %.2f %.2f %.2f, health %.0f\n", w.pos[0], w.pos[1], w.pos[2], w.health);
    btvs_trail_tail(tail, sizeof tail, 40);
    put(t, "\nLast trail events:\n%s", tail);
    btvs_mem_stats_now(&m);
    btvs_mem_text(&m, memtext, sizeof memtext);
    put(t, "\nMemory:\n%s", memtext);
}

/* ── Crashes ────────────────────────────────────────────────────────────── */

typedef struct {
    EXCEPTION_POINTERS *ep;
    HANDLE thread;
    DWORD tid;
    int unhandled;
    uint32_t regs[8];
    uint32_t icall[16];
    uint64_t icalls;
    char path[MAX_PATH];
} crash_t;

static volatile LONG s_crash_reports;
static void *s_reported_addr[4];
static char s_crash_text[96 * 1024];
static volatile LONG s_crash_busy;

static DWORD WINAPI crash_writer(LPVOID arg)
{
    crash_t *c = (crash_t *)arg;
    EXCEPTION_RECORD *er = c->ep->ExceptionRecord;
    CONTEXT *cx = c->ep->ContextRecord;
    text_t t = { s_crash_text, 0, sizeof s_crash_text };
    char when[32], dir[MAX_PATH], name[420], dmp[MAX_PATH];
    uint32_t fault_va = 0;
    int i, shown, have_fault = 0;
    HANDLE h;
    DWORD w;

    s_report_thread = GetCurrentThreadId();
    for (i = 0; i < 300 && s_sym_state == 1; i++)      /* the names are still loading: worth the wait */
        Sleep(100);
    stamp(when, sizeof when);
    crash_dir(dir);
    sprintf_s(c->path, MAX_PATH, "%s\\crash_%s.txt", dir, when);
    sprintf_s(dmp, MAX_PATH, "%s\\crash_%s.dmp", dir, when);

    EnterCriticalSection(&s_dbghelp);
    host_name(cx->Rip, name, sizeof name);
    put(&t, "[CRASH] %s (0x%08lX) at %s\n", code_name(er->ExceptionCode), (unsigned long)er->ExceptionCode, name);
    put(&t, "==== Buffy the Vampire Slayer (Xbox, 2002) - Windows port: crash report ====\n");
    put(&t, "Version: %s\nWhen: %s, %.1f s after start, frame %ld, thread %lu%s\n", btvs_version_text(), when,
        btvs_uptime(), xbox_HangMonitorFrames(), (unsigned long)c->tid,
        c->unhandled ? "" : "  (first chance: the game may have carried on)");
    if ((er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || er->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
            && er->NumberParameters >= 2) {
        ULONG_PTR kind = er->ExceptionInformation[0], addr = er->ExceptionInformation[1];
        ULONG_PTR rel = addr - (ULONG_PTR)g_xbox_mem_offset;
        put(&t, "Fault: %s address 0x%llX", kind == 1 ? "writing" : kind == 8 ? "executing" : "reading",
            (unsigned long long)addr);
        if (g_xbox_mem_offset && rel <= 0xFFFFFFFFull) {
            fault_va = (uint32_t)rel;
            have_fault = 1;
            put(&t, " = guest address 0x%08X%s", fault_va, fault_va < 0x10000 ? " (a null pointer)" : "");
        }
        put(&t, "\n");
    }
    put(&t, "Host: RIP=0x%llX  %s\n", (unsigned long long)cx->Rip, name);

    put(&t, "\nGuest registers (this thread):\n"
            "  eax=%08X ecx=%08X edx=%08X ebx=%08X\n  esi=%08X edi=%08X esp=%08X ebp=%08X\n",
        c->regs[0], c->regs[1], c->regs[2], c->regs[3], c->regs[4], c->regs[5], c->regs[6], c->regs[7]);

    put(&t, "\nRecent indirect calls (newest first; %llu so far):\n", (unsigned long long)c->icalls);
    for (i = 0; i < 16; i++)
        if (c->icall[i])
            put_guest_addr(&t, "  ", c->icall[i]);

    put(&t, "\nGuest stack: return addresses found from esp up (innermost first; a scan,\n"
            "so calls that already returned can linger in it):\n");
    for (i = 0, shown = 0; i < 512 && shown < 32 && c->regs[6]; i++) {
        uint32_t v;
        char lead[32];
        if (!guest_read32(c->regs[6] + (uint32_t)i * 4, &v))
            break;
        if (is_guest_code(v)) {
            sprintf_s(lead, sizeof lead, "  [esp+%04X] ", i * 4);
            put_guest_addr(&t, lead, v);
            shown++;
        }
    }

    put(&t, "\nHost stack (translated game functions are Name_<guest address>):\n");
    put_host_stack(&t, c->thread, cx);
    put(&t, "\nHost registers:\n  rax=%016llX rbx=%016llX rcx=%016llX rdx=%016llX\n"
            "  rsi=%016llX rdi=%016llX rsp=%016llX rbp=%016llX\n"
            "  r8 =%016llX r9 =%016llX r10=%016llX r11=%016llX\n"
            "  r12=%016llX r13=%016llX r14=%016llX r15=%016llX\n",
        cx->Rax, cx->Rbx, cx->Rcx, cx->Rdx, cx->Rsi, cx->Rdi, cx->Rsp, cx->Rbp, cx->R8, cx->R9, cx->R10, cx->R11,
        cx->R12, cx->R13, cx->R14, cx->R15);
    LeaveCriticalSection(&s_dbghelp);
    (void)have_fault;

    put_common_tail(&t);

    /* The minidump: threads, stacks, modules. Opens in Visual Studio / WinDbg
     * with the build's .pdb. */
    h = CreateFileA(dmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        BOOL ok;
        mei.ThreadId = c->tid;
        mei.ExceptionPointers = c->ep;
        mei.ClientPointers = FALSE;
        EnterCriticalSection(&s_dbghelp);
        ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h,
                               (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules
                                               | MiniDumpWithIndirectlyReferencedMemory),
                               &mei, NULL, NULL);
        LeaveCriticalSection(&s_dbghelp);
        CloseHandle(h);
        put(&t, "\nMinidump: %s%s\n", dmp, ok ? "" : "  (could not be written)");
        if (!ok)
            DeleteFileA(dmp);
    }
    put(&t, "Report: %s\n==== end of crash report ====\n", c->path);

    h = CreateFileA(c->path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, t.p, (DWORD)t.n, &w, NULL);
        CloseHandle(h);
    }
    btvs_log_block(t.p);
    s_report_thread = 0;
    return 0;
}

void btvs_crash_report(EXCEPTION_POINTERS *ep, int unhandled)
{
    static crash_t c;
    HANDLE th;
    void *addr = ep->ExceptionRecord->ExceptionAddress;
    int i;
    LONG n;

    if (GetCurrentThreadId() == s_report_thread)
        return;                                   /* the helper tripped over something: not the game's crash */
    for (i = 0; i < 4; i++)
        if (s_reported_addr[i] == addr) {
            if (!unhandled)
                return;                           /* the same fault again */
            btvs_log("crash", "the fault at %p (reported above) was not handled: the game stops", addr);
            return;
        }
    /* A first-chance fault that something handles must not flood the log:
     * four full reports a run, then a line each. */
    n = InterlockedIncrement(&s_crash_reports);
    if (n > 4 || InterlockedCompareExchange(&s_crash_busy, 1, 0) != 0) {
        btvs_log("crash", "%s 0x%08lX at %p (no report: %s)", unhandled ? "unhandled" : "first-chance",
                 (unsigned long)ep->ExceptionRecord->ExceptionCode, addr,
                 n > 4 ? "four written already" : "another is being written");
        return;
    }
    s_reported_addr[n - 1] = addr;

    memset(&c, 0, sizeof c);
    c.ep = ep;
    c.unhandled = unhandled;
    c.tid = GetCurrentThreadId();
    c.regs[0] = g_eax; c.regs[1] = g_ecx; c.regs[2] = g_edx; c.regs[3] = g_ebx;
    c.regs[4] = g_esi; c.regs[5] = g_edi; c.regs[6] = g_esp; c.regs[7] = g_ebp;
    for (i = 0; i < 16; i++)
        c.icall[i] = g_icall_trace[(g_icall_trace_idx - 1 - (uint32_t)i) & 15];
    c.icalls = g_icall_count;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &c.thread, 0, FALSE,
                    DUPLICATE_SAME_ACCESS);
    th = CreateThread(NULL, 256 * 1024, crash_writer, &c, 0, NULL);
    if (th) {
        WaitForSingleObject(th, 30000);
        CloseHandle(th);
    }
    if (c.thread)
        CloseHandle(c.thread);
    InterlockedExchange(&s_crash_busy, 0);
}

static LONG WINAPI unhandled_filter(EXCEPTION_POINTERS *ep)
{
    char msg[600];
    btvs_crash_report(ep, 1);
    btvs_log("crash", "the game stops (exception 0x%08lX)", (unsigned long)ep->ExceptionRecord->ExceptionCode);
    if (!btvs_is_test_run()) {
        sprintf_s(msg, sizeof msg,
                  "The game has stopped because of an error.\n\nA crash report was saved in\n%s\\crashes\n\n"
                  "Start the game again and use Report a Problem (or send that folder) so it can be fixed.",
                  btvs_exe_dir());
        MessageBoxA(NULL, msg, "Buffy the Vampire Slayer", MB_ICONERROR | MB_OK | MB_SETFOREGROUND);
    }
    TerminateProcess(GetCurrentProcess(), ep->ExceptionRecord->ExceptionCode);
    return EXCEPTION_EXECUTE_HANDLER;
}

/* ── Hangs ──────────────────────────────────────────────────────────────── */

static void hang_report(const char *why)
{
    static char body[20000];
    char dir[MAX_PATH], path[MAX_PATH], when[32];
    text_t t = { body, 0, sizeof body };
    FILE *f;

    stamp(when, sizeof when);
    crash_dir(dir);
    sprintf_s(path, MAX_PATH, "%s\\hang_%s.txt", dir, when);
    if (fopen_s(&f, path, "w") != 0 || !f)
        return;
    fprintf(f, "==== Buffy the Vampire Slayer (Xbox, 2002) - Windows port: hang report ====\n"
               "Version: %s\nWhen: %s, %.1f s after start\nWhy: %s\n\n"
               "Where the game thread is (guest stack, recent indirect calls):\n",
            btvs_version_text(), when, btvs_uptime(), why);
    fflush(f);
    xbox_WatchdogReportTo(f, why);
    put_common_tail(&t);
    fputs(body, f);
    fprintf(f, "==== end of hang report ====\n");
    fclose(f);
    btvs_log("crash", "HANG: %s. Report: %s", why, path);
    btvs_trail("hang", "why=%s", why);
}

/* Graphics' frames-per-second figure; 0 when its files are not in the build. */
float btvs_fps(void);
float btvs_fps_none(void) { return 0.0f; }
#pragma comment(linker, "/alternatename:btvs_fps=btvs_fps_none")

static volatile LONG s_runs;            /* restarts of the game inside the process */

void btvs_crash_reboot(void)
{
    int i;
    InterlockedExchange(&s_crash_reports, 0);       /* (the count and the "already reported here" list are per run) */
    for (i = 0; i < 4; i++)
        s_reported_addr[i] = NULL;
    InterlockedIncrement(&s_runs);
}

static DWORD WINAPI hang_thread(LPVOID arg)
{
    const char *e = getenv("BTVS_HANG_SECS"), *b = getenv("BTVS_BOOT_HANG_SECS");
    unsigned secs = e && *e ? (unsigned)atoi(e) : 20u, boot = b && *b ? (unsigned)atoi(b) : 90u;
    unsigned still = 0, reports = 0, t = 0;
    long last = -1, run_seen = 0;
    int reported = 0, boot_reported = 0, starting = 0;
    char why[120];
    (void)arg;

    for (;;) {
        long now;
        Sleep(1000);
        t++;
        now = xbox_HangMonitorFrames();
        /* The game restarted itself inside the process (a level load): a
         * stretch without frames that is a start, not a hang. It gets the
         * start's allowance, counted from the restart. */
        if (s_runs != run_seen) {
            run_seen = s_runs;
            starting = 1;
            t = 0;
            still = 0;
            reported = boot_reported = 0;
            reports = 0;
            last = now;
            continue;
        }
        if (starting) {
            if (now == last) {
                if (boot && t >= boot && !boot_reported) {
                    boot_reported = 1;
                    sprintf_s(why, sizeof why, "no frame shown %u s after the game restarted itself", t);
                    hang_report(why);
                }
                continue;
            }
            starting = 0;
        }
        /* A movie (the attract movie after a while in the main menu, a
         * cutscene file) is played inside one game frame: pictures still
         * reach the screen, so that is not a hang. */
        if (now != 0 && now == last && btvs_fps() >= 1.0f) {
            still = 0;
            continue;
        }
        if (now == 0) {
            if (boot && t >= boot && !boot_reported) {
                boot_reported = 1;
                sprintf_s(why, sizeof why, "no frame shown %u s after start", t);
                hang_report(why);
            }
            continue;
        }
        if (now != last) {
            if (reported)
                btvs_log("crash", "the game thread is moving again (frame %ld)", now);
            last = now;
            still = 0;
            reported = 0;
            continue;
        }
        if (secs && ++still >= secs && !reported && reports < 3) {
            reported = 1;
            reports++;
            sprintf_s(why, sizeof why, "no new frame for %u s (stuck at frame %ld)", still, now);
            hang_report(why);
        }
    }
}

/* ── Start ──────────────────────────────────────────────────────────────── */

static DWORD WINAPI test_crash_thread(LPVOID arg)
{
    Sleep((DWORD)(uintptr_t)arg * 1000);
    btvs_log("crash", "BTVS_TEST_CRASH: crashing on purpose (a helper thread)");
    *(volatile int *)(uintptr_t)0x10 = 1;
    return 0;
}

void btvs_crash_init(void)
{
    static int done;
    HANDLE h;
    if (done)
        return;
    done = 1;
    InitializeCriticalSection(&s_dbghelp);
    SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SetUnhandledExceptionFilter(unhandled_filter);
    InterlockedExchange(&s_sym_state, 1);
    h = CreateThread(NULL, 0, sym_thread, NULL, 0, NULL);
    if (h)
        CloseHandle(h);
    h = CreateThread(NULL, 0, hang_thread, NULL, 0, NULL);
    if (h)
        CloseHandle(h);
    {
        const char *e = getenv("BTVS_TEST_CRASH");
        int secs = e ? atoi(e) : 0;
        if (secs > 1) {
            h = CreateThread(NULL, 0, test_crash_thread, (LPVOID)(uintptr_t)secs, 0, NULL);
            if (h)
                CloseHandle(h);
        }
    }
}

/* BTVS_TEST_CRASH=1: called by main on the game thread once the guest is set
 * up, so the report has guest registers and a guest stack to show. */
void btvs_crash_test_now(void)
{
    const char *e = getenv("BTVS_TEST_CRASH");
    int i;
    if (!e || atoi(e) != 1)
        return;
    btvs_log("crash", "BTVS_TEST_CRASH=1: crashing on purpose (the game thread; the two guest return "
                      "addresses on the stack are put there by the test)");
    if (g_xbox_mem_offset && g_esp > 0x1000) {       /* as two nested guest calls would leave it */
        g_esp -= 4; *(uint32_t *)((uintptr_t)g_xbox_mem_offset + g_esp) = g_xbox_code_lo + 0x30;
        g_esp -= 4; *(uint32_t *)((uintptr_t)g_xbox_mem_offset + g_esp) = g_xbox_code_lo + 0x1000;
        g_eax = 0xB0FF1E5u;
    }
    *(volatile uint32_t *)((uintptr_t)g_xbox_mem_offset + 0xBAD00010u) = 1;     /* guest memory nothing maps */
    *(volatile int *)(uintptr_t)0x10 = 1;
}
