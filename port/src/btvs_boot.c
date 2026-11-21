/**
 * Boot bring-up: game functions replaced by host code because the translated
 * version cannot work as it is. Each is listed in manual_functions.json;
 * docs/graphics.md ("Boot log") says what failed without it.
 *
 * Guest calling convention: on entry [esp] is the guest return address and
 * the arguments follow it; returning pops the return address (and, for
 * stdcall, the arguments).
 */
#include <stdint.h>
#include <string.h>

#include "recomp_types.h"

/* void *memmove(void *dst, const void *src, size_t n) -- 0x00580200, cdecl.
 * (memcpy is the same function in this CRT.) MSVC's hand-written version
 * finishes its copies through jump tables indexed from the *end* of the
 * table (`jmp [ecx*4 + TrailUpVec]` and friends), which the lifter does not
 * see as a switch: at run time it became an indirect jump into the middle of
 * the function, "[ICALL] Failed to resolve VA 0x005804A4", and the copy's
 * tail bytes were never written. */
void memmove_00580200(void)
{
    uint32_t dst = MEM32(g_esp + 4), src = MEM32(g_esp + 8), n = MEM32(g_esp + 12);
    if (n)
        memmove((void *)XBOX_PTR(dst), (const void *)XBOX_PTR(src), n);
    g_eax = dst;
    g_esp += 4;
}

/* BTVS_WATCH=<hex guest address>: a hardware write breakpoint on that DWORD,
 * on every thread of the game; each write is logged with the host call stack
 * (translated functions carry the game's names). To find what overwrites
 * something. */
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "dbghelp.lib")

static uint32_t s_watch;
volatile LONG g_btvs_watch_armed = 1;   /* BTVS_WATCH_ARM=1: only after the game's own device is made */
static LONG s_watch_max = 40, s_watch_depth = 24;   /* BTVS_WATCH_MAX, BTVS_WATCH_DEPTH */
static void *s_watch_host;
static DWORD s_watch_tid;
static volatile LONG s_watch_hits;

/* The host call stack of this thread, to the log (translated functions carry
 * the game's names): for a debugging message that needs to say who called. */
void btvs_boot_host_stack(const char *tag)
{
    static volatile LONG sym;
    void *fr[32];
    USHORT i, c;
    char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
    if (InterlockedCompareExchange(&sym, 1, 0) == 0) {
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
        SymInitialize(GetCurrentProcess(), NULL, TRUE);
    }
    c = CaptureStackBackTrace(1, 32, fr, NULL);
    for (i = 0; i < c; i++) {
        DWORD64 d = 0;
        memset(buf, 0, sizeof buf);
        si->SizeOfStruct = sizeof(SYMBOL_INFO);
        si->MaxNameLen = 255;
        if (SymFromAddr(GetCurrentProcess(), (DWORD64)(uintptr_t)fr[i], &d, si))
            fprintf(stderr, "[%s]   %s+0x%llX\n", tag, si->Name, (unsigned long long)d);
    }
    fflush(stderr);
}

static void *s_break_host[3];           /* BTVS_BREAK=<host function>[,<function>[,<function>]]: log each entry */
static char s_break_name[3][96];
static volatile LONG s_break_hits;
static int s_break_quiet;

static LONG CALLBACK watch_veh(PEXCEPTION_POINTERS ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP && (ep->ContextRecord->Dr6 & 0xE)) {
        int which = (ep->ContextRecord->Dr6 & 2) ? 0 : (ep->ContextRecord->Dr6 & 4) ? 1 : 2;
        ep->ContextRecord->Dr6 = 0;
        ep->ContextRecord->EFlags |= 0x10000;        /* resume past the breakpoint */
        if (s_break_quiet) {                         /* BTVS_BREAK_QUIET=1: remember the last entries, print them when the last-named function is reached */
            static struct { int which; DWORD tid; uint32_t r[8], s[4]; } ring[96];
            static LONG rn;
            int last = s_break_host[2] ? 2 : s_break_host[1] ? 1 : 0;
            LONG k = InterlockedIncrement(&rn) - 1;
            int j = (int)(k % 96);
            ring[j].which = which; ring[j].tid = GetCurrentThreadId();
            ring[j].r[0] = g_eax; ring[j].r[1] = g_ecx; ring[j].r[2] = g_edx; ring[j].r[3] = g_ebx;
            ring[j].r[4] = g_esi; ring[j].r[5] = g_edi; ring[j].r[6] = g_esp; ring[j].r[7] = g_ebp;
            ring[j].s[0] = MEM32(g_esp); ring[j].s[1] = MEM32(g_esp + 4); ring[j].s[2] = MEM32(g_esp + 8); ring[j].s[3] = MEM32(g_esp + 12);
            if (getenv("BTVS_BREAK_BADFREE") && !strncmp(s_break_name[which], "RtlFreeHeap", 11)
                    && MEM32(g_esp + 12) && (MEM32(g_esp + 12) < 0x00F80000u || MEM32(g_esp + 12) >= 0x04000000u)) {
                /* a free of something that is not in the game's heap */
                static LONG shown;
                if (InterlockedIncrement(&shown) <= 6) {
                    fprintf(stderr, "[BADFREE] RtlFreeHeap(%08X, %X, %08X) from %08X; eax=%08X ecx=%08X edx=%08X ebx=%08X esi=%08X edi=%08X esp=%08X ebp=%08X\n",
                            MEM32(g_esp + 4), MEM32(g_esp + 8), MEM32(g_esp + 12), MEM32(g_esp),
                            g_eax, g_ecx, g_edx, g_ebx, g_esi, g_edi, g_esp, g_ebp);
                    btvs_boot_host_stack("BADFREE");
                }
            }
            if (which == 0 && getenv("BTVS_BREAK_HEAP") && MEM32(g_esp + 12) == (uint32_t)strtoul(getenv("BTVS_BREAK_HEAP"), NULL, 16)) {
                /* the game heap (XAPI's, at the address in [esp+4]) as an allocation of that size starts */
                uint32_t hp = MEM32(g_esp + 4), q2, sg;
                fprintf(stderr, "[HEAPDUMP] RtlAllocateHeap(%08X, %X, %X)\n", hp, MEM32(g_esp + 8), MEM32(g_esp + 12));
                for (q2 = 0; q2 < 0x1A0; q2 += 32)
                    fprintf(stderr, "[HEAPDUMP] %08X: %08X %08X %08X %08X %08X %08X %08X %08X\n", hp + q2,
                            MEM32(hp + q2), MEM32(hp + q2 + 4), MEM32(hp + q2 + 8), MEM32(hp + q2 + 12),
                            MEM32(hp + q2 + 16), MEM32(hp + q2 + 20), MEM32(hp + q2 + 24), MEM32(hp + q2 + 28));
                for (sg = 0; sg < 8; sg++) {
                    uint32_t sp = MEM32(hp + 0x60 + sg * 4);
                    if (sp >= 0x10000u && sp < 0x04000000u)
                        for (q2 = 0; q2 < 0x40; q2 += 32)
                            fprintf(stderr, "[HEAPDUMP] seg%u %08X: %08X %08X %08X %08X %08X %08X %08X %08X\n", sg, sp + q2,
                                    MEM32(sp + q2), MEM32(sp + q2 + 4), MEM32(sp + q2 + 8), MEM32(sp + q2 + 12),
                                    MEM32(sp + q2 + 16), MEM32(sp + q2 + 20), MEM32(sp + q2 + 24), MEM32(sp + q2 + 28));
                }
                {   /* the big-block free list: each entry's size */
                    uint32_t head = hp + 0x180, e = MEM32(head), cnt = 0;
                    while (e != head && e >= 0x10000u && e < 0x04000000u && cnt < 40) {
                        fprintf(stderr, "[HEAPDUMP] free[0] entry %08X: header %08X %08X links %08X %08X\n", e,
                                MEM32(e - 8), MEM32(e - 4), MEM32(e), MEM32(e + 4));
                        e = MEM32(e);
                        cnt++;
                    }
                    fprintf(stderr, "[HEAPDUMP] free[0] ends at %08X after %u entries (head %08X)\n", e, cnt, head);
                }
                fflush(stderr);
            }
            if (which == last && g_btvs_watch_armed && InterlockedIncrement(&s_break_hits) <= 3) {
                LONG a;
                for (a = (k >= 95 ? k - 95 : 0); a <= k; a++) {
                    int q = (int)(a % 96);
                    fprintf(stderr, "[BREAK] %ld %s t%lu eax=%08X ecx=%08X edx=%08X ebx=%08X esi=%08X edi=%08X esp=%08X ebp=%08X [esp]=%08X %08X %08X %08X\n",
                            a, s_break_name[ring[q].which], ring[q].tid, ring[q].r[0], ring[q].r[1], ring[q].r[2], ring[q].r[3],
                            ring[q].r[4], ring[q].r[5], ring[q].r[6], ring[q].r[7], ring[q].s[0], ring[q].s[1], ring[q].s[2], ring[q].s[3]);
                }
                btvs_boot_host_stack("BREAK");
                if (getenv("BTVS_BREAK_DUMP")) {         /* <hex address>,<DWORDs>: guest memory at that moment */
                    uint32_t at = (uint32_t)strtoul(getenv("BTVS_BREAK_DUMP"), NULL, 16), nn = 64, q2;
                    const char *cm = strchr(getenv("BTVS_BREAK_DUMP"), ',');
                    if (cm) nn = (uint32_t)atoi(cm + 1);
                    for (q2 = 0; q2 < nn; q2 += 8)
                        fprintf(stderr, "[DUMP] %08X: %08X %08X %08X %08X %08X %08X %08X %08X\n", at + q2 * 4,
                                MEM32(at + q2 * 4), MEM32(at + q2 * 4 + 4), MEM32(at + q2 * 4 + 8), MEM32(at + q2 * 4 + 12),
                                MEM32(at + q2 * 4 + 16), MEM32(at + q2 * 4 + 20), MEM32(at + q2 * 4 + 24), MEM32(at + q2 * 4 + 28));
                    fflush(stderr);
                }
            }
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (g_btvs_watch_armed && InterlockedIncrement(&s_break_hits) <= s_watch_max) {
            fprintf(stderr, "[BREAK] #%ld %s guest eax=%08X ecx=%08X edx=%08X ebx=%08X esi=%08X edi=%08X esp=%08X ebp=%08X [esp]=%08X %08X %08X\n",
                    s_break_hits, s_break_name[which], g_eax, g_ecx, g_edx, g_ebx, g_esi, g_edi, g_esp, g_ebp,
                    MEM32(g_esp), MEM32(g_esp + 4), MEM32(g_esp + 8));
            if (getenv("BTVS_BREAK_ARG")) {          /* what the first argument points at, and what its DWORDs point at, as text */
                uint32_t a1 = MEM32(g_esp + 4), q4, q5;
                if (a1 >= 0x10000u && a1 < 0x04000000u) {
                    fprintf(stderr, "[BREAK]   arg1 +10 \"");      /* (a TFileName keeps its text there) */
                    for (q5 = 0; q5 < 96; q5++) {
                        unsigned char ch = MEM8(a1 + 10 + q5);
                        if (ch < 32 || ch > 126) break;
                        fputc(ch, stderr);
                    }
                    fprintf(stderr, "\" ->");
                    for (q4 = 0; q4 < 6; q4++) {
                        uint32_t v = MEM32(a1 + q4 * 4);
                        fprintf(stderr, " %08X", v);
                        if (v >= 0x10000u && v < 0x04000000u) {
                            fprintf(stderr, "(\"");
                            for (q5 = 0; q5 < 48; q5++) {
                                unsigned char ch = MEM8(v + q5);
                                if (ch < 32 || ch > 126) break;
                                fputc(ch, stderr);
                            }
                            fprintf(stderr, "\")");
                        }
                    }
                    fprintf(stderr, "\n");
                }
            }
            if (s_watch_depth > 0)
                btvs_boot_host_stack("BREAK");
            fflush(stderr);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP && (ep->ContextRecord->Dr6 & 1)) {
        LONG n;
        ep->ContextRecord->Dr6 = 0;
        if (!g_btvs_watch_armed)
            return EXCEPTION_CONTINUE_EXECUTION;
        n = InterlockedIncrement(&s_watch_hits);
        if (n <= s_watch_max) {
            void *fr[24];
            USHORT i, c = CaptureStackBackTrace(0, (ULONG)s_watch_depth, fr, NULL);
            char buf[sizeof(SYMBOL_INFO) + 256];
            SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
            fprintf(stderr, "[WATCH] write #%ld: %08X now %08X, thread %lu, rip %p\n", n, s_watch,
                    *(uint32_t *)s_watch_host, GetCurrentThreadId(), (void *)ep->ContextRecord->Rip);
            fprintf(stderr, "[WATCH]   guest eax=%08X ecx=%08X edx=%08X ebx=%08X esi=%08X edi=%08X esp=%08X ebp=%08X\n",
                    g_eax, g_ecx, g_edx, g_ebx, g_esi, g_edi, g_esp, g_ebp);
            for (i = 0; i < c; i++) {
                DWORD64 d = 0;
                memset(buf, 0, sizeof buf);
                si->SizeOfStruct = sizeof(SYMBOL_INFO);
                si->MaxNameLen = 255;
                if (SymFromAddr(GetCurrentProcess(), (DWORD64)(uintptr_t)fr[i], &d, si))
                    fprintf(stderr, "[WATCH]   %s+0x%llX\n", si->Name, (unsigned long long)d);
            }
            fflush(stderr);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI watch_thread(LPVOID arg)
{
    (void)arg;
    s_watch_tid = GetCurrentThreadId();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    if (s_watch_host)
        fprintf(stderr, "[WATCH] %08X = %08X at start\n", s_watch, *(uint32_t *)s_watch_host);
    if (getenv("BTVS_BREAK")) {
        char list[300], *tok, *ctx = NULL;
        int k = 0;
        strncpy_s(list, sizeof list, getenv("BTVS_BREAK"), _TRUNCATE);
        for (tok = strtok_s(list, ",", &ctx); tok && k < 3; tok = strtok_s(NULL, ",", &ctx), k++) {
            char buf[sizeof(SYMBOL_INFO) + 256];
            SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
            memset(buf, 0, sizeof buf);
            si->SizeOfStruct = sizeof(SYMBOL_INFO);
            si->MaxNameLen = 255;
            if (SymFromName(GetCurrentProcess(), tok, si))
                s_break_host[k] = (void *)(uintptr_t)si->Address;
            strncpy_s(s_break_name[k], sizeof s_break_name[k], tok, _TRUNCATE);
            fprintf(stderr, "[BREAK] %s at %p\n", tok, s_break_host[k]);
            if (s_break_host[k] && getenv("BTVS_BREAK_CODE")) {      /* its machine code, to disassemble */
                const unsigned char *pc = (const unsigned char *)s_break_host[k];
                int q3, nb = atoi(getenv("BTVS_BREAK_CODE"));
                fprintf(stderr, "[CODE] %s %p ", tok, s_break_host[k]);
                for (q3 = 0; q3 < nb; q3++)
                    fprintf(stderr, "%02X", pc[q3]);
                fprintf(stderr, "\n");
            }
        }
    }
    for (;;) {                                   /* new threads get the breakpoint too */
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 te;
        te.dwSize = sizeof te;
        if (snap != INVALID_HANDLE_VALUE && Thread32First(snap, &te)) {
            do {
                HANDLE th;
                CONTEXT c;
                if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == s_watch_tid)
                    continue;
                th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (!th)
                    continue;
                memset(&c, 0, sizeof c);
                c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (SuspendThread(th) != (DWORD)-1) {
                    if (GetThreadContext(th, &c) && (c.Dr0 != (DWORD64)(uintptr_t)s_watch_host
                                                     || c.Dr1 != (DWORD64)(uintptr_t)s_break_host[0])) {
                        c.Dr0 = (DWORD64)(uintptr_t)s_watch_host;
                        c.Dr1 = (DWORD64)(uintptr_t)s_break_host[0];
                        c.Dr2 = (DWORD64)(uintptr_t)s_break_host[1];
                        c.Dr3 = (DWORD64)(uintptr_t)s_break_host[2];
                        c.Dr7 &= ~0xFFFF00FFull;
                        if (s_watch_host) c.Dr7 |= 0x000D0001ull;           /* L0, write, 4 bytes */
                        if (s_break_host[0]) c.Dr7 |= 0x00000004ull;        /* L1..L3, execute */
                        if (s_break_host[1]) c.Dr7 |= 0x00000010ull;
                        if (s_break_host[2]) c.Dr7 |= 0x00000040ull;
                        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                        SetThreadContext(th, &c);
                    }
                    ResumeThread(th);
                }
                CloseHandle(th);
            } while (Thread32Next(snap, &te));
        }
        if (snap != INVALID_HANDLE_VALUE)
            CloseHandle(snap);
        {   /* a change no breakpoint saw (the kernel wrote it, or the page was replaced) */
            static uint32_t last;
            static int have;
            uint32_t v = s_watch_host ? *(volatile uint32_t *)s_watch_host : 0;
            if (have && v != last) {
                uint32_t pg, a2;
                fprintf(stderr, "[WATCH] %08X: %08X -> %08X seen by polling\n", s_watch, last, v);
                for (pg = (s_watch & ~0xFFFu) - 0x4000u; pg <= (s_watch & ~0xFFFu) + 0x4000u; pg += 0x1000u) {
                    int nz = 0;
                    for (a2 = pg; a2 < pg + 0x1000u; a2 += 4)
                        nz += MEM32(a2) != 0;
                    fprintf(stderr, "[WATCH]   page %08X: %d non-zero DWORDs\n", pg, nz);
                }
                for (a2 = s_watch - 0x40; a2 < s_watch + 0x40; a2 += 16)
                    fprintf(stderr, "[WATCH]   %08X: %08X %08X %08X %08X\n", a2, MEM32(a2), MEM32(a2 + 4), MEM32(a2 + 8), MEM32(a2 + 12));
                fflush(stderr);
            }
            last = v;
            have = 1;
        }
        Sleep(5);
    }
}

/* BTVS_STACK_AT=<seconds>[,<seconds>...]: at those times after start, where
 * every thread of the game is (host call stacks; translated functions carry
 * the game's names). To see what a start-up that shows nothing is waiting for. */
volatile DWORD g_btvs_game_tid;         /* the thread that shows the frames (btvs_frame.c) */

static DWORD WINAPI stack_at_thread(LPVOID arg)
{
    int only_game = getenv("BTVS_STACK_GAME") != NULL;
    const char *e = getenv("BTVS_STACK_AT");
    DWORD t0 = GetTickCount(), me = GetCurrentThreadId();
    (void)arg;
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    while (e && *e) {
        double at = atof(e);
        HANDLE snap;
        THREADENTRY32 te;
        while ((GetTickCount() - t0) / 1000.0 < at)
            Sleep(50);
        snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        te.dwSize = sizeof te;
        if (snap != INVALID_HANDLE_VALUE && Thread32First(snap, &te)) {
            do {
                HANDLE th;
                CONTEXT c;
                if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == me)
                    continue;
                if (only_game && te.th32ThreadID != g_btvs_game_tid)
                    continue;                              /* BTVS_STACK_GAME=1: only the thread that shows the frames */
                th = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                if (!th)
                    continue;
                {
                    /* The frames are collected while the thread is stopped,
                     * with nothing that takes a lock (a stopped thread may
                     * hold the heap's or the symbol library's); the names
                     * are looked up after it runs again. */
                    DWORD64 pcs[40];
                    int npc = 0, q;
                    if (SuspendThread(th) != (DWORD)-1) {
                        memset(&c, 0, sizeof c);
                        c.ContextFlags = CONTEXT_FULL;
                        if (GetThreadContext(th, &c)) {
                            while (npc < 40 && c.Rip) {
                                DWORD64 base = 0, est = 0;
                                PVOID hd = NULL;
                                PRUNTIME_FUNCTION fn;
                                pcs[npc++] = c.Rip;
                                fn = RtlLookupFunctionEntry(c.Rip, &base, NULL);
                                if (fn)
                                    RtlVirtualUnwind(0, base, c.Rip, fn, &c, &hd, &est, NULL);
                                else {
                                    if (IsBadReadPtr((const void *)c.Rsp, 8))
                                        break;
                                    c.Rip = *(const DWORD64 *)c.Rsp;       /* a leaf: the return address is on top */
                                    c.Rsp += 8;
                                }
                            }
                        }
                        ResumeThread(th);
                    }
                    if (npc) {
                        fprintf(stderr, "[STACK] at %.2f s, thread %lu:\n", at, te.th32ThreadID);
                        for (q = 0; q < npc; q++) {
                            char buf[sizeof(SYMBOL_INFO) + 256];
                            SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
                            DWORD64 d = 0;
                            memset(buf, 0, sizeof buf);
                            si->SizeOfStruct = sizeof(SYMBOL_INFO);
                            si->MaxNameLen = 255;
                            if (SymFromAddr(GetCurrentProcess(), pcs[q], &d, si))
                                fprintf(stderr, "[STACK]   %s+0x%llX\n", si->Name, (unsigned long long)d);
                        }
                    }
                }
                CloseHandle(th);
            } while (Thread32Next(snap, &te));
        }
        if (snap != INVALID_HANDLE_VALUE)
            CloseHandle(snap);
        fflush(stderr);
        e = strchr(e, ',');
        if (e) e++;
    }
    return 0;
}

/* BTVS_HITCH_LOG=<ms> (default 30): what the game thread is doing whenever a
 * frame takes longer than that. A watcher looks every 2 ms; the first time a
 * frame is over the limit it stops the game thread for a moment, takes its
 * call stack (nothing that locks while it is stopped) and logs it with the
 * frame's age; btvs_frame.c logs the frame's whole length when it ends. */
volatile LONGLONG g_btvs_frame_start;   /* QueryPerformanceCounter at the start of the frame being made */
volatile LONG g_btvs_frame_no;

static DWORD WINAPI hitch_thread(LPVOID arg)
{
    double limit = atof(getenv("BTVS_HITCH_LOG"));
    LARGE_INTEGER f, q;
    LONG done = -1;
    (void)arg;
    if (limit <= 0.0)
        limit = 30.0;
    QueryPerformanceFrequency(&f);
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    for (;;) {
        LONG no = g_btvs_frame_no;
        LONGLONG st = g_btvs_frame_start;
        double age;
        Sleep(2);
        if (!st || !g_btvs_game_tid || no == done || no != g_btvs_frame_no)
            continue;
        QueryPerformanceCounter(&q);
        age = (double)(q.QuadPart - st) * 1000.0 / (double)f.QuadPart;
        if (age < limit)
            continue;
        done = no;
        {
            HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, g_btvs_game_tid);
            DWORD64 pcs[48];
            int npc = 0, k, shown = 0;
            CONTEXT c;
            if (!th)
                continue;
            if (SuspendThread(th) != (DWORD)-1) {
                memset(&c, 0, sizeof c);
                c.ContextFlags = CONTEXT_FULL;
                if (GetThreadContext(th, &c)) {
                    while (npc < 48 && c.Rip) {
                        DWORD64 base = 0, est = 0;
                        PVOID hd = NULL;
                        PRUNTIME_FUNCTION fn;
                        pcs[npc++] = c.Rip;
                        fn = RtlLookupFunctionEntry(c.Rip, &base, NULL);
                        if (fn)
                            RtlVirtualUnwind(0, base, c.Rip, fn, &c, &hd, &est, NULL);
                        else {
                            if (IsBadReadPtr((const void *)c.Rsp, 8))
                                break;
                            c.Rip = *(const DWORD64 *)c.Rsp;
                            c.Rsp += 8;
                        }
                    }
                }
                ResumeThread(th);
            }
            CloseHandle(th);
            fprintf(stderr, "[HITCH] frame %ld at %.0f ms:", no, age);
            for (k = 0; k < npc && shown < 12; k++) {
                char buf[sizeof(SYMBOL_INFO) + 256];
                SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
                DWORD64 d = 0;
                memset(buf, 0, sizeof buf);
                si->SizeOfStruct = sizeof(SYMBOL_INFO);
                si->MaxNameLen = 255;
                if (SymFromAddr(GetCurrentProcess(), pcs[k], &d, si)) {
                    fprintf(stderr, " %s%s", shown ? "< " : "", si->Name);
                    shown++;
                }
            }
            fprintf(stderr, "\n");
            fflush(stderr);
        }
    }
}

void btvs_boot_watch_start(void)
{
    if (getenv("BTVS_HITCH_LOG"))
        CloseHandle(CreateThread(NULL, 0, hitch_thread, NULL, 0, NULL));
    const char *e = getenv("BTVS_WATCH");
    if ((e && *e && (s_watch = (uint32_t)strtoul(e, NULL, 16)) != 0) || getenv("BTVS_BREAK")) {
        s_watch_host = s_watch ? (void *)XBOX_PTR(s_watch) : NULL;
        if (getenv("BTVS_WATCH_ARM")) g_btvs_watch_armed = 0;
        if (getenv("BTVS_BREAK_QUIET")) s_break_quiet = 1;
        if (getenv("BTVS_WATCH_MAX")) s_watch_max = atoi(getenv("BTVS_WATCH_MAX"));
        if (getenv("BTVS_WATCH_DEPTH")) s_watch_depth = atoi(getenv("BTVS_WATCH_DEPTH"));
        AddVectoredExceptionHandler(1, watch_veh);
        CloseHandle(CreateThread(NULL, 0, watch_thread, NULL, 0, NULL));
    }
    /* (BTVS_STACK_RUN=<n>: only in the game's n-th restart, counted from 1;
     * the times count from each start of the game) */
    if (getenv("BTVS_STACK_AT") && (!getenv("BTVS_STACK_RUN")
            || (getenv("BTVS_RESTARTS") && !strcmp(getenv("BTVS_RESTARTS"), getenv("BTVS_STACK_RUN")))))
        CloseHandle(CreateThread(NULL, 0, stack_at_thread, NULL, 0, NULL));
}
