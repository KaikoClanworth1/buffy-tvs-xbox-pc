"""Compare builds of the game from speed_run.sh's frame files.

  python speed_report.py <output folder> [from seconds] [to seconds]

Files are named <build>_<n>.csv. Only frames between the two times of the
level's run count (default 30 to 75: in the first level, after the opening
cutscene is skipped, through the scripted walk and standing still after it).
For each run: the median over its frames; for each build: the median of its
runs' medians.

thread_cpu_ms is the work the game's thread did in a frame, counted in
processor cycles: it does not grow while the thread waits for the frame limit
or is pushed aside by another program, so it is the figure to compare when the
PC is busy. "game" is clock time in the game's code and its drawing.
"""
import csv
import glob
import os
import statistics
import sys

folder = sys.argv[1]
t0 = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
t1 = float(sys.argv[3]) if len(sys.argv) > 3 else 75.0
COLS = ('thread_cpu_ms', 'game', 'frame_ms')
builds = {}
print('%-14s %7s %14s %10s %10s %12s' % ('run', 'frames', 'thread_cpu_ms', 'game ms', 'frame ms', 'cpu mean ms'))
for path in sorted(glob.glob(os.path.join(folder, '*_*.csv'))):
    name = os.path.splitext(os.path.basename(path))[0]
    rows = [r for r in csv.DictReader(open(path)) if r.get('thread_cpu_ms') and t0 <= float(r['seconds']) < t1]
    if len(rows) < 100:
        print('%-14s %7d  (too few frames in the window: left out)' % (name, len(rows)))
        continue
    med = {c: statistics.median(float(r[c]) for r in rows) for c in COLS}
    mean = statistics.fmean(float(r['thread_cpu_ms']) for r in rows)
    print('%-14s %7d %14.2f %10.2f %10.2f %12.2f' % (name, len(rows), med['thread_cpu_ms'], med['game'], med['frame_ms'], mean))
    builds.setdefault(name.rsplit('_', 1)[0], []).append((med, mean, len(rows)))
print()
print('%-14s %5s %14s %10s %10s %12s   %s' % ('build', 'runs', 'thread_cpu_ms', 'game ms', 'frame ms', 'cpu mean ms', 'thread_cpu_ms of each run'))
for b, runs in builds.items():
    print('%-14s %5d %14.2f %10.2f %10.2f %12.2f   %s' % (
        b, len(runs), *(statistics.median(m[c] for m, _, _ in runs) for c in COLS),
        statistics.median(mean for _, mean, _ in runs),
        ' '.join('%.2f' % m['thread_cpu_ms'] for m, _, _ in runs)))
