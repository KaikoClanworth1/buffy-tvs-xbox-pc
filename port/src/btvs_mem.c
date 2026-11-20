/* Memory statistics: the PC's RAM, this game's share of it, the graphics
 * card's memory (total / in use by everything / in use by this game) and the
 * game's own heaps. For the Menu area's memory page and for every report.
 *
 * Where the figures come from, so they can be checked against Task Manager:
 *   ram_*                     GlobalMemoryStatusEx (Performance > Memory: total, "Available")
 *   proc_working_set          Details tab, column "Working set (memory)"
 *   proc_private_working_set  Details tab "Memory (private working set)" = Processes tab "Memory"
 *   proc_commit               Details tab "Commit size"
 *   vram_total                the adapter's DedicatedVideoMemory (Performance > GPU: "Dedicated GPU memory" total)
 *   vram_used_system          counter "GPU Adapter Memory \ Dedicated Usage" (the same graph's "in use")
 *   vram_used / vram_budget   IDXGIAdapter3::QueryVideoMemoryInfo, local segment (Details tab "Dedicated GPU memory")
 *
 * btvs_mem_stats() only copies a snapshot; a thread refreshes it once a second
 * while somebody is asking (the Windows counters take milliseconds to read).
 */
#define COBJMACROS
#include <windows.h>
#include <psapi.h>
#include <pdh.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <string.h>

#include "btvs_log.h"

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

/* Until Graphics defines btvs_gpu_info: "don't know", and the default adapter is used. */
int btvs_gpu_info_none(struct btvs_gpu *out) { (void)out; return 0; }
#pragma comment(linker, "/alternatename:btvs_gpu_info=btvs_gpu_info_none")

/* The game's own heap figures: btvs_trail.c reads them from the game (it owns
 * the game hooks). 0 = not known yet. */
int btvs_game_heap_stats(uint64_t *used, uint64_t *peak, uint32_t *allocs);
uint64_t btvs_guest_ram_bytes(void);

/* PROCESS_MEMORY_COUNTERS_EX2 (Windows 10 22H2 / 11): older SDKs lack it. */
typedef struct {
    DWORD cb;
    DWORD PageFaultCount;
    SIZE_T PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage,
        QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage, PrivateUsage,
        PrivateWorkingSetSize;
    ULONG64 SharedCommitUsage;
} btvs_pmc_ex2;

static CRITICAL_SECTION s_cs;
static INIT_ONCE s_once = INIT_ONCE_STATIC_INIT;
static struct btvs_mem s_cache;
static volatile LONG s_thread_running;
static volatile DWORD s_last_ask;
static IDXGIAdapter3 *s_adapter3;
static DXGI_ADAPTER_DESC1 s_desc;
static int s_adapter_tried;
static PDH_HQUERY s_pdh;
static PDH_HCOUNTER s_pdh_dedicated;
static int s_pdh_tried;

static BOOL CALLBACK init_once(PINIT_ONCE o, PVOID p, PVOID *c)
{
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&s_cs);
    return TRUE;
}

static void driver_text(IDXGIAdapter1 *a, char *out, size_t n)
{
    LARGE_INTEGER v;
    out[0] = 0;
    if (SUCCEEDED(IDXGIAdapter1_CheckInterfaceSupport(a, &IID_IDXGIDevice, &v)))
        _snprintf_s(out, n, _TRUNCATE, "%u.%u.%u.%u", HIWORD(v.HighPart), LOWORD(v.HighPart), HIWORD(v.LowPart),
                    LOWORD(v.LowPart));
}

/* Every adapter, one line each (the log header and reports). */
void btvs_gpu_text(char *buf, size_t n)
{
    IDXGIFactory1 *f = NULL;
    IDXGIAdapter1 *a = NULL;
    UINT i;
    size_t len = 0;

    if (n)
        buf[0] = 0;
    if (FAILED(CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&f)) || !f) {
        _snprintf_s(buf, n, _TRUNCATE, "GPU: (DXGI not available)\n");
        return;
    }
    for (i = 0; IDXGIFactory1_EnumAdapters1(f, i, &a) == S_OK; i++) {
        DXGI_ADAPTER_DESC1 d;
        char name[128], drv[64];
        if (SUCCEEDED(IDXGIAdapter1_GetDesc1(a, &d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && len < n) {
            WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof name, NULL, NULL);
            driver_text(a, drv, sizeof drv);
            _snprintf_s(buf + len, n - len, _TRUNCATE,
                        "GPU %u: %s, driver %s, %llu MB video memory, %llu MB shared (%04X:%04X)\n", i, name,
                        drv[0] ? drv : "?", (unsigned long long)(d.DedicatedVideoMemory >> 20),
                        (unsigned long long)(d.SharedSystemMemory >> 20), d.VendorId, d.DeviceId);
            len += strlen(buf + len);
        }
        IDXGIAdapter1_Release(a);
    }
    IDXGIFactory1_Release(f);
}

/* The adapter the game draws with (Graphics says which; else the first). */
static void find_adapter(void)
{
    IDXGIFactory1 *f = NULL;
    IDXGIAdapter1 *a = NULL, *pick = NULL;
    struct btvs_gpu g;
    int have = 0;
    UINT i;

    memset(&g, 0, sizeof g);
    have = btvs_gpu_info(&g);
    if (!have && s_adapter_tried)
        return;                 /* keep the default until Graphics knows better */
    if (have && s_adapter3 && s_desc.AdapterLuid.LowPart == g.luid_low && s_desc.AdapterLuid.HighPart == g.luid_high)
        return;
    s_adapter_tried = 1;
    if (FAILED(CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&f)) || !f)
        return;
    for (i = 0; IDXGIFactory1_EnumAdapters1(f, i, &a) == S_OK; i++) {
        DXGI_ADAPTER_DESC1 d;
        if (SUCCEEDED(IDXGIAdapter1_GetDesc1(a, &d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            int match = have && d.AdapterLuid.LowPart == g.luid_low && d.AdapterLuid.HighPart == g.luid_high;
            if (match || !pick) {
                if (pick)
                    IDXGIAdapter1_Release(pick);
                pick = a;
                IDXGIAdapter1_AddRef(pick);
                s_desc = d;
                if (match) {
                    IDXGIAdapter1_Release(a);
                    break;
                }
            }
        }
        IDXGIAdapter1_Release(a);
    }
    if (pick) {
        if (s_adapter3) {
            IDXGIAdapter3_Release(s_adapter3);
            s_adapter3 = NULL;
        }
        IDXGIAdapter1_QueryInterface(pick, &IID_IDXGIAdapter3, (void **)&s_adapter3);
        IDXGIAdapter1_Release(pick);
    }
    IDXGIFactory1_Release(f);
}

/* Video memory in use by every program, from Windows' GPU counters. */
static uint64_t system_vram_used(void)
{
    static PDH_FMT_COUNTERVALUE_ITEM_A items[64];
    DWORD size = sizeof items, count = 0, i;
    char want[64];
    uint64_t sum = 0;

    if (!s_pdh_tried) {
        s_pdh_tried = 1;
        if (PdhOpenQueryA(NULL, 0, &s_pdh) != ERROR_SUCCESS)
            s_pdh = NULL;
        else if (PdhAddEnglishCounterA(s_pdh, "\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &s_pdh_dedicated)
                 != ERROR_SUCCESS) {
            PdhCloseQuery(s_pdh);
            s_pdh = NULL;
        }
    }
    if (!s_pdh || PdhCollectQueryData(s_pdh) != ERROR_SUCCESS)
        return 0;
    if (PdhGetFormattedCounterArrayA(s_pdh_dedicated, PDH_FMT_LARGE, &size, &count, items) != ERROR_SUCCESS)
        return 0;
    /* Instances are named luid_0x<high>_0x<low>_phys_<n>. */
    sprintf_s(want, sizeof want, "luid_0x%08lX_0x%08lX", (unsigned long)s_desc.AdapterLuid.HighPart,
              (unsigned long)s_desc.AdapterLuid.LowPart);
    for (i = 0; i < count; i++)
        if (items[i].szName && !_strnicmp(items[i].szName, want, strlen(want)) && items[i].FmtValue.CStatus == 0)
            sum += (uint64_t)items[i].FmtValue.largeValue;
    return sum;
}

static void sample(struct btvs_mem *m, int with_counters)
{
    MEMORYSTATUSEX ms;
    btvs_pmc_ex2 pmc;

    memset(m, 0, sizeof *m);
    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) {
        m->ram_total = ms.ullTotalPhys;
        m->ram_available = ms.ullAvailPhys;
    }
    memset(&pmc, 0, sizeof pmc);
    pmc.cb = sizeof pmc;
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof pmc)) {
        memset(&pmc, 0, sizeof pmc);       /* older Windows: no private working set */
        pmc.cb = sizeof(PROCESS_MEMORY_COUNTERS_EX);
        K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pmc, pmc.cb);
    }
    m->proc_working_set = pmc.WorkingSetSize;
    m->proc_peak_working_set = pmc.PeakWorkingSetSize;
    m->proc_commit = pmc.PrivateUsage;
    m->proc_private_working_set = pmc.PrivateWorkingSetSize;

    EnterCriticalSection(&s_cs);
    find_adapter();
    WideCharToMultiByte(CP_UTF8, 0, s_desc.Description, -1, m->gpu_name, sizeof m->gpu_name, NULL, NULL);
    m->vram_total = s_desc.DedicatedVideoMemory;
    if (s_adapter3) {
        DXGI_QUERY_VIDEO_MEMORY_INFO vi;
        if (SUCCEEDED(IDXGIAdapter3_QueryVideoMemoryInfo(s_adapter3, 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vi))) {
            m->vram_used = vi.CurrentUsage;
            m->vram_budget = vi.Budget;
        }
        if (SUCCEEDED(IDXGIAdapter3_QueryVideoMemoryInfo(s_adapter3, 0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &vi)))
            m->vram_shared_used = vi.CurrentUsage;
    }
    if (with_counters)
        m->vram_used_system = system_vram_used();
    else
        m->vram_used_system = s_cache.vram_used_system;
    LeaveCriticalSection(&s_cs);

    m->guest_ram_total = btvs_guest_ram_bytes();
    m->game_heap_valid = btvs_game_heap_stats(&m->game_heap_used, &m->game_heap_peak, &m->game_heap_allocs);
}

static DWORD WINAPI sampler(LPVOID arg)
{
    (void)arg;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    while (GetTickCount() - s_last_ask < 15000) {
        struct btvs_mem m;
        sample(&m, 1);
        EnterCriticalSection(&s_cs);
        s_cache = m;
        LeaveCriticalSection(&s_cs);
        Sleep(1000);
    }
    InterlockedExchange(&s_thread_running, 0);
    return 0;
}

void btvs_mem_stats(struct btvs_mem *out)
{
    InitOnceExecuteOnce(&s_once, init_once, NULL, NULL);
    s_last_ask = GetTickCount();
    if (InterlockedCompareExchange(&s_thread_running, 1, 0) == 0) {
        HANDLE h;
        struct btvs_mem m;
        sample(&m, 0);                     /* the first answer now, without the slow counters */
        EnterCriticalSection(&s_cs);
        s_cache = m;
        LeaveCriticalSection(&s_cs);
        h = CreateThread(NULL, 0, sampler, NULL, 0, NULL);
        if (h)
            CloseHandle(h);
        else
            InterlockedExchange(&s_thread_running, 0);
    }
    EnterCriticalSection(&s_cs);
    *out = s_cache;
    LeaveCriticalSection(&s_cs);
}

void btvs_mem_stats_now(struct btvs_mem *out)
{
    InitOnceExecuteOnce(&s_once, init_once, NULL, NULL);
    sample(out, 1);
}

void btvs_mem_text(const struct btvs_mem *m, char *buf, size_t n)
{
#define MB(x) ((double)(x) / 1048576.0)
    int len = _snprintf_s(buf, n, _TRUNCATE,
                "PC memory:    %.0f MB total, %.0f MB in use, %.0f MB free\n"
                "This game:    %.1f MB working set (%.1f MB private), %.1f MB committed, peak %.1f MB\n"
                "Video memory: %s: %.0f MB total, %.0f MB in use by all programs, %.1f MB by this game"
                " (budget %.0f MB; shared %.1f MB)\n",
                MB(m->ram_total), MB(m->ram_total - m->ram_available), MB(m->ram_available),
                MB(m->proc_working_set), MB(m->proc_private_working_set), MB(m->proc_commit),
                MB(m->proc_peak_working_set), m->gpu_name[0] ? m->gpu_name : "?", MB(m->vram_total),
                MB(m->vram_used_system), MB(m->vram_used), MB(m->vram_budget), MB(m->vram_shared_used));
    if (len < 0)
        return;
    if (m->game_heap_valid)
        _snprintf_s(buf + len, n - len, _TRUNCATE,
                    "Game heaps:   %.2f MB allocated in %u blocks (peak %.2f MB) of the Xbox's %.0f MB\n",
                    MB(m->game_heap_used), m->game_heap_allocs, MB(m->game_heap_peak), MB(m->guest_ram_total));
    else
        _snprintf_s(buf + len, n - len, _TRUNCATE, "Game heaps:   not known yet (Xbox memory: %.0f MB)\n",
                    MB(m->guest_ram_total));
#undef MB
}
