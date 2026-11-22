"""List the icon a program carries, without showing anything:

    python port/icon/check_icon.py <program.exe> [<program2.exe> ...] [--ico port/icon/btvs.ico]

Reads the program as data (it is not run), finds its icon group resource 1 and prints every
image in it. With --ico (default: btvs.ico beside this file) each image is also compared with
the same size in that .ico, byte for byte. Exit code 0 only when every program has all nine
sizes and they match.
"""
import ctypes
import os
import struct
import sys
from ctypes import wintypes as wt

HERE = os.path.dirname(os.path.abspath(__file__))
WANT = (16, 20, 24, 32, 40, 48, 64, 128, 256)
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
k32.LoadLibraryExW.restype = wt.HMODULE
k32.LoadLibraryExW.argtypes = (wt.LPCWSTR, wt.HANDLE, wt.DWORD)
k32.FindResourceW.restype = wt.HRSRC
k32.FindResourceW.argtypes = (wt.HMODULE, ctypes.c_void_p, ctypes.c_void_p)
k32.LoadResource.restype = wt.HGLOBAL
k32.LoadResource.argtypes = (wt.HMODULE, wt.HRSRC)
k32.LockResource.restype = ctypes.c_void_p
k32.LockResource.argtypes = (wt.HGLOBAL,)
k32.SizeofResource.restype = wt.DWORD
k32.SizeofResource.argtypes = (wt.HMODULE, wt.HRSRC)
k32.FreeLibrary.argtypes = (wt.HMODULE,)
RT_ICON, RT_GROUP_ICON = 3, 14


def res(mod, kind, ident):
    h = k32.FindResourceW(mod, ident, kind)
    if not h:
        return None
    return ctypes.string_at(k32.LockResource(k32.LoadResource(mod, h)), k32.SizeofResource(mod, h))


def ico_images(path):
    d = open(path, 'rb').read()
    out = {}
    for i in range(struct.unpack_from('<H', d, 4)[0]):
        w, _, _, _, _, bpp, n, off = struct.unpack_from('<BBBBHHII', d, 6 + 16 * i)
        out[w or 256] = d[off:off + n]
    return out


def check(exe, ico):
    mod = k32.LoadLibraryExW(exe, None, 0x2 | 0x20)     # as data and as an image resource: nothing runs
    if not mod:
        print('%s: cannot open (error %d)' % (exe, ctypes.get_last_error()))
        return False
    try:
        g = res(mod, RT_GROUP_ICON, 1)
        if g is None:
            print('%s: no icon group 1' % exe)
            return False
        n = struct.unpack_from('<H', g, 4)[0]
        print('%s: icon group 1, %d images' % (exe, n))
        ok, seen = True, []
        for i in range(n):
            w, h, _, _, planes, bpp, size, ident = struct.unpack_from('<BBBBHHIH', g, 6 + 14 * i)
            w = w or 256
            data = res(mod, RT_ICON, ident)
            kind = 'PNG' if data and data[:4] == b'\x89PNG' else 'bitmap'
            same = ''
            if ico is not None:
                same = ', same as the .ico' if data == ico.get(w) else ', NOT the .ico\'s'
                ok = ok and data == ico.get(w)
            print('  %3d x %-3d %2d-bit %-6s %7d bytes%s' % (w, h or 256, bpp, kind, len(data or b''), same))
            seen.append(w)
        missing = [s for s in WANT if s not in seen]
        if missing:
            print('  missing sizes: %s' % missing)
        return ok and not missing
    finally:
        k32.FreeLibrary(mod)


def main():
    a = sys.argv[1:]
    ico_path = os.path.join(HERE, 'btvs.ico')
    if '--ico' in a:
        i = a.index('--ico')
        ico_path = a[i + 1]
        del a[i:i + 2]
    if not a:
        sys.stderr.write(__doc__)
        return 2
    ico = ico_images(ico_path)
    good = [check(os.path.abspath(e), ico) for e in a]
    return 0 if all(good) else 1


if __name__ == '__main__':
    sys.exit(main())
