"""Watch a test game from outside: its processes and the windows the desktop has for them.

    python port/tools/winwatch.py <seconds> <out.txt> [exe-path-part]

(<seconds> at most; it ends by itself 3 s after the last of the processes has gone.)

Every 0.25 s it asks Windows (not the game) for every buffy_tvs.exe process whose path
holds [exe-path-part] (default: any) and for every top-level window those processes own,
hidden ones too, and writes a line when something changes:

    process <pid> started / ended
    window <hwnd> of <pid> made / gone / shown / hidden   class, title, size

and every 5 s a line per process: threads, handles, GDI and USER objects, private bytes.
This is how a run with a hidden window proves "one process, one window": the lines are
about windows and processes the system has, not about what the game logs of itself.
It only reads; it never touches a window.
"""
import ctypes, sys, time
from ctypes import wintypes as wt

u32 = ctypes.WinDLL('user32', use_last_error=True)
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
psapi = ctypes.WinDLL('psapi', use_last_error=True)

TH32CS_SNAPPROCESS, TH32CS_SNAPTHREAD = 2, 4


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [('dwSize', wt.DWORD), ('cntUsage', wt.DWORD), ('th32ProcessID', wt.DWORD),
                ('th32DefaultHeapID', ctypes.c_void_p), ('th32ModuleID', wt.DWORD),
                ('cntThreads', wt.DWORD), ('th32ParentProcessID', wt.DWORD),
                ('pcPriClassBase', wt.LONG), ('dwFlags', wt.DWORD), ('szExeFile', wt.WCHAR * 260)]


class PMC(ctypes.Structure):
    _fields_ = [('cb', wt.DWORD), ('PageFaultCount', wt.DWORD), ('PeakWorkingSetSize', ctypes.c_size_t),
                ('WorkingSetSize', ctypes.c_size_t), ('QuotaPeakPagedPoolUsage', ctypes.c_size_t),
                ('QuotaPagedPoolUsage', ctypes.c_size_t), ('QuotaPeakNonPagedPoolUsage', ctypes.c_size_t),
                ('QuotaNonPagedPoolUsage', ctypes.c_size_t), ('PagefileUsage', ctypes.c_size_t),
                ('PeakPagefileUsage', ctypes.c_size_t), ('PrivateUsage', ctypes.c_size_t)]


k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
k32.OpenProcess.restype = wt.HANDLE
k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.CloseHandle.argtypes = [wt.HANDLE]
k32.Process32FirstW.argtypes = [wt.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
k32.Process32NextW.argtypes = [wt.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
k32.QueryFullProcessImageNameW.argtypes = [wt.HANDLE, wt.DWORD, wt.LPWSTR, ctypes.POINTER(wt.DWORD)]
k32.GetProcessHandleCount.argtypes = [wt.HANDLE, ctypes.POINTER(wt.DWORD)]
psapi.GetProcessMemoryInfo.argtypes = [wt.HANDLE, ctypes.POINTER(PMC), wt.DWORD]
u32.GetGuiResources.argtypes = [wt.HANDLE, wt.DWORD]
u32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
u32.IsWindowVisible.argtypes = [wt.HWND]
u32.GetClassNameW.argtypes = [wt.HWND, wt.LPWSTR, ctypes.c_int]
u32.GetWindowTextW.argtypes = [wt.HWND, wt.LPWSTR, ctypes.c_int]
u32.GetWindowRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
ENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
u32.EnumWindows.argtypes = [ENUMPROC, wt.LPARAM]


def processes(part):
    out = {}
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    pe = PROCESSENTRY32W()
    pe.dwSize = ctypes.sizeof(pe)
    ok = k32.Process32FirstW(snap, ctypes.byref(pe))
    while ok:
        if pe.szExeFile.lower() == 'buffy_tvs.exe':
            h = k32.OpenProcess(0x1000 | 0x0400, False, pe.th32ProcessID)   # limited query + query
            if not h:
                h = k32.OpenProcess(0x1000, False, pe.th32ProcessID)
            path = ''
            if h:
                buf = ctypes.create_unicode_buffer(1024)
                n = wt.DWORD(1024)
                if k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n)):
                    path = buf.value
            if not part or part.lower() in path.lower():
                info = {'threads': pe.cntThreads, 'parent': pe.th32ParentProcessID}
                if h:
                    hc = wt.DWORD(0)
                    k32.GetProcessHandleCount(h, ctypes.byref(hc))
                    m = PMC()
                    m.cb = ctypes.sizeof(m)
                    psapi.GetProcessMemoryInfo(h, ctypes.byref(m), m.cb)
                    info.update(handles=hc.value, gdi=u32.GetGuiResources(h, 0), user=u32.GetGuiResources(h, 1),
                                private=m.PrivateUsage)
                out[pe.th32ProcessID] = info
            if h:
                k32.CloseHandle(h)
        ok = k32.Process32NextW(snap, ctypes.byref(pe))
    k32.CloseHandle(snap)
    return out


def windows(pids):
    out = {}

    def cb(hwnd, _):
        pid = wt.DWORD(0)
        u32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value in pids:
            c = ctypes.create_unicode_buffer(128)
            t = ctypes.create_unicode_buffer(128)
            r = wt.RECT()
            u32.GetClassNameW(hwnd, c, 128)
            u32.GetWindowTextW(hwnd, t, 128)
            u32.GetWindowRect(hwnd, ctypes.byref(r))
            out[hwnd] = (pid.value, bool(u32.IsWindowVisible(hwnd)), c.value, t.value,
                         r.right - r.left, r.bottom - r.top)
        return True

    u32.EnumWindows(ENUMPROC(cb), 0)
    return out


def main():
    secs = float(sys.argv[1])
    out = open(sys.argv[2], 'w', encoding='utf-8')
    part = sys.argv[3] if len(sys.argv) > 3 else ''
    t0 = time.time()
    seen_p, seen_w = {}, {}
    last_stats = 0.0
    had = 0.0
    counts = {'process started': 0, 'process ended': 0, 'window made': 0, 'window gone': 0}

    def say(text):
        out.write('%8.2f  %s\n' % (time.time() - t0, text))
        out.flush()

    # helper classes the system or the runtime makes beside the game's window are named, not hidden
    while time.time() - t0 < secs:
        ps = processes(part)
        for pid in ps:
            if pid not in seen_p:
                say('process %d started (parent %d)' % (pid, ps[pid]['parent']))
                counts['process started'] += 1
        for pid in list(seen_p):
            if pid not in ps:
                say('process %d ended' % pid)
                counts['process ended'] += 1
        seen_p = ps
        if ps:
            had = time.time()
        elif had and time.time() - had > 3.0:
            break                       # the session is over
        ws = windows(set(ps))
        for h, w in ws.items():
            if h not in seen_w:
                say('window %08X of %d made: %s, class "%s", title "%s", %dx%d'
                    % (h, w[0], 'shown' if w[1] else 'hidden', w[2], w[3], w[4], w[5]))
                counts['window made'] += 1
            elif seen_w[h][1] != w[1]:
                say('window %08X of %d %s' % (h, w[0], 'shown' if w[1] else 'hidden'))
            elif seen_w[h][4:] != w[4:]:
                say('window %08X of %d resized to %dx%d' % (h, w[0], w[4], w[5]))
        for h, w in seen_w.items():
            if h not in ws:
                say('window %08X of %d gone (class "%s")' % (h, w[0], w[2]))
                counts['window gone'] += 1
        seen_w = ws
        if time.time() - last_stats >= 5.0 and ps:
            last_stats = time.time()
            for pid, i in sorted(ps.items()):
                if 'handles' in i:
                    say('  process %d: %d threads, %d handles, %d GDI, %d USER objects, %.0f MB private, %d windows'
                        % (pid, i['threads'], i['handles'], i['gdi'], i['user'], i['private'] / 1048576.0,
                           sum(1 for w in ws.values() if w[0] == pid)))
        time.sleep(0.25)
    say('total: ' + ', '.join('%d %s' % (v, k) for k, v in counts.items()))


if __name__ == '__main__':
    main()
