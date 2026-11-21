"""Read a BTVS_OBJLOG file (port/src/btvs_objlog.c): which objects moved, how, and with which frame steps.

    python port/tools/objlog.py <file> [--obj 0x00ABCDEF] [--top 30] [--from FRAME] [--to FRAME]

Per object: its class (from final.map), ticks in which it moved, the way it went in all (path), how far it
ended from where it started (net), its longest step, how often the step turned back on the one before
(back), and the height it covered. An object that rests has no line; one that slides or falls has
path ~ net; one that jitters has a long path, a short net and many turns back.
--obj prints that object's lines. BTVS_MAIN names the main checkout (final.map) from a worktree.
"""
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.environ.get('BTVS_MAIN') or os.path.join(HERE, '..', '..')


def vtables():
    out = {}
    try:
        for line in open(os.path.join(MAIN, 'game_files', 'final.map'), errors='replace'):
            m = re.match(r'\s*[0-9a-f]{4}:[0-9a-f]{8}\s+\?\?_7(\w+?)@@6B(\w*)@\s+([0-9a-f]{8})', line)
            if m:
                out.setdefault(int(m.group(3), 16) - 0x3EF460, m.group(1) + (('/' + m.group(2).strip('@')) if m.group(2) else ''))
    except OSError:
        pass
    return out


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return
    path = args[0]
    opt = dict(zip(args[1::2], args[2::2]))
    only = int(opt['--obj'], 16) if '--obj' in opt else None
    top = int(opt.get('--top', 30))
    f0, f1 = int(opt.get('--from', 0)), int(opt.get('--to', 1 << 60))
    vt = vtables()
    objs, ticks, born = {}, [], {}
    for line in open(path):
        p = line.rstrip().split(',')
        if p[0] == 'frame' or len(p) < (6 if p[0] == 'T' else 8 if p[0] == 'N' else 10) or not line.endswith(chr(10)):
            continue                    # (the heading; a last line cut off when the game stopped)
        if p[0] == 'T':
            if f0 <= int(p[1]) <= f1:
                ticks.append((int(p[1]), float(p[2]), float(p[3]), int(p[4]), int(p[5])))
            continue
        if p[0] == 'N':
            born[int(p[2], 16)] = (int(p[1]), int(p[3], 16), float(p[4]), float(p[5]), float(p[6]))
            continue
        fr = int(p[0])
        if not f0 <= fr <= f1:
            continue
        o = int(p[2], 16)
        x, y, z, step, turn = (float(v) for v in p[4:9])
        if only is not None:
            if o == only:
                print(line.rstrip())
            continue
        s = objs.get(o)
        if s is None:
            b = born.get(o)
            s = objs[o] = dict(vt=int(p[3], 16), n=0, path=0.0, first=(b[2], b[3], b[4]) if b else (x, y, z), last=None,
                               mx=0.0, mxf=0, turn=0.0, back=0, prev=None, lo=y, hi=y, f0=fr, f1=fr, big=0)
        s['n'] += 1
        s['path'] += step
        s['turn'] += turn
        if step > s['mx']:
            s['mx'], s['mxf'] = step, fr
        if step > 5.0:
            s['big'] += 1
        if s['last'] is not None:
            d = (x - s['last'][0], y - s['last'][1], z - s['last'][2])
            if s['prev'] is not None and d[0] * s['prev'][0] + d[1] * s['prev'][1] + d[2] * s['prev'][2] < 0:
                s['back'] += 1
            s['prev'] = d
        s['last'] = (x, y, z)
        s['lo'], s['hi'] = min(s['lo'], y), max(s['hi'], y)
        s['f1'] = fr
    if only is not None:
        return
    if ticks:
        dts = sorted(t[1] for t in ticks)
        n = len(dts)
        print('ticks: %d (frames %d..%d)  dt min %.5f  median %.5f  95%% %.5f  max %.5f  mean %.5f' % (
            n, ticks[0][0], ticks[-1][0], dts[0], dts[n // 2], dts[int(n * .95)], dts[-1], sum(dts) / n))
        print('objects in the level: %d..%d, moving per tick: median %d, max %d' % (
            min(t[3] for t in ticks), max(t[3] for t in ticks), sorted(t[4] for t in ticks)[n // 2], max(t[4] for t in ticks)))
    rows = []
    for o, s in objs.items():
        net = math.dist(s['first'], s['last']) if s['last'] else 0.0
        rows.append((s['path'], o, s, net))
    rows.sort(reverse=True)
    print('%-10s %-34s %6s %10s %10s %9s %7s %5s %5s %9s  %s' % ('object', 'class', 'ticks', 'path', 'net', 'max step', '@frame',
                                                                  'back', '>5', 'height', 'last place'))
    for path_len, o, s, net in rows[:top]:
        print('%08X   %-34s %6d %10.2f %10.2f %9.3f %7d %5d %5d %9.2f  %.1f %.1f %.1f' % (
            o, vt.get(s['vt'], '%08X' % s['vt'])[:34], s['n'], path_len, net, s['mx'], s['mxf'], s['back'], s['big'],
            s['hi'] - s['lo'], s['last'][0], s['last'][1], s['last'][2]))


main()
