"""Shaking objects in a BTVS_OBJLOG file: how often, and how far, an object steps back against its last step,
and whether that goes with the frames that hold no fixed (60 Hz) game tick.

    python port/tools/objlog_shake.py <file> [<file> ...] [--min 0.5] [--objects]

A "shake" is two steps in a row that point against each other (the second undoes at least half of the
first), both of --min units or more (default 0.5: what shows on screen; 0 counts every wobble).
Per class of object: shakes, shakes per second of game time, the mean and the largest step undone, and
the share of them whose first step was made in a frame without a fixed tick. --objects lists single objects.
An object that moves smoothly has none. BTVS_MAIN names the main checkout (final.map) from a worktree.
"""
import collections
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
                out.setdefault(int(m.group(3), 16) - 0x3EF460, m.group(1))
    except OSError:
        pass
    return out


def one(path, vt, least, per_object):
    ticks, seconds, frames, tickless = {}, 0.0, 0, 0
    prev = None
    last = {}        # object -> (frame, position, step vector of that frame or None)
    cls = collections.defaultdict(lambda: [0, 0.0, 0.0, 0, set()])
    objs = collections.defaultdict(lambda: [0, 0.0, 0.0, 0, ''])
    for line in open(path):
        if not line.endswith('\n'):
            continue
        p = line.rstrip().split(',')
        if p[0] == 'T' and len(p) >= 6:
            f = int(p[1])
            dt = float(p[2])
            if dt < 0.2:
                seconds += dt
            frames += 1
            if len(p) >= 7:
                c = int(p[6])
                ticks[f] = (c - prev) if prev is not None and c >= prev else None
                prev = c
                tickless += ticks[f] == 0
        elif p[0] == 'N' and len(p) >= 8:
            last[p[2]] = (int(p[1]), (float(p[4]), float(p[5]), float(p[6])), None)
        elif p[0].isdigit() and len(p) >= 10:
            f, o = int(p[0]), p[2]
            cur = (float(p[4]), float(p[5]), float(p[6]))
            was = last.get(o)
            d = None
            if was:
                d = (cur[0] - was[1][0], cur[1] - was[1][1], cur[2] - was[1][2])
                if was[2] is not None and was[0] == f - 1:
                    a, b = math.hypot(*was[2]), math.hypot(*d)
                    dot = sum(x * y for x, y in zip(was[2], d))
                    # the part of the last step that this one takes back
                    undone = -dot / a if a > 0 else 0.0
                    if a >= least and b >= least and a < 2000 and undone >= 0.5 * a:
                        name = vt.get(int(p[3], 16), p[3])
                        for rec in (cls[name], objs[o]):
                            rec[0] += 1
                            rec[1] += min(a, undone)
                            rec[2] = max(rec[2], min(a, undone))
                            rec[3] += ticks.get(f - 1) == 0
                        cls[name][4].add(o)
                        objs[o][4] = name
            last[o] = (f, cur, d)
    known = [f for f in ticks if ticks[f] is not None]
    print('%s: %d frames in %.1f s of game time (%.0f a second)%s' % (
        os.path.basename(path), frames, seconds, frames / seconds if seconds else 0,
        '; without a fixed tick: %.0f %%' % (100.0 * tickless / len(known)) if known else ''))
    total = sum(v[0] for v in cls.values())
    print('  shakes of %.2g units or more: %d (%.1f a second)' % (least, total, total / seconds if seconds else 0))
    for name, v in sorted(cls.items(), key=lambda kv: -kv[1][1]):
        print('    %-26s %6d  %6.1f/s  mean %7.2f  largest %8.2f  in %2d objects  after a frame without a tick: %3.0f %%' % (
            name[:26], v[0], v[0] / seconds if seconds else 0, v[1] / v[0], v[2], len(v[4]), 100.0 * v[3] / v[0]))
    if per_object:
        for o, v in sorted(objs.items(), key=lambda kv: -kv[1][1])[:25]:
            print('      %s %-24s %6d  mean %7.2f  largest %8.2f' % (o, v[4][:24], v[0], v[1] / v[0], v[2]))


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return
    least, per_object, files = 0.5, False, []
    i = 0
    while i < len(args):
        if args[i] == '--min':
            least = float(args[i + 1])
            i += 2
        elif args[i] == '--objects':
            per_object = True
            i += 1
        else:
            files.append(args[i])
            i += 1
    vt = vtables()
    for f in files:
        one(f, vt, least, per_object)


main()
