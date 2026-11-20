"""Open a problem report: what happened, where, and how to get back there.

    python port/tools/report_open.py <report folder or .zip>
    python port/tools/report_open.py            # the newest report of the dev build

Prints the player's words, the build, where in the game, the last trail
events, any crash in the report with the game's function names (from the
disassembly's functions.json, so it works for reports from builds without
symbols), and the exact commands that replay it.
"""
import bisect
import glob
import json
import os
import re
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)


def main_checkout():
    env = os.environ.get("BTVS_MAIN")
    if env:
        return env
    # a worktree lives in <main>/.claude/worktrees/<name>
    p = os.path.dirname(PORT)
    m = re.match(r"(.*)[\\/]\.claude[\\/]worktrees[\\/][^\\/]+$", p)
    return m.group(1) if m else p


def load_syms():
    path = os.path.join(main_checkout(), "xboxrecomp", "tools", "disasm", "output", "functions.json")
    if not os.path.isfile(path):
        return None
    try:
        f = sorted((int(x["start"], 16), int(x["end"], 16), x["name"]) for x in json.load(open(path)))
    except Exception as e:       # being rewritten by a regen right now
        print("(no function names: %s can't be read: %s)" % (path, e))
        return None
    return f, [a for a, _, _ in f]


def sym(syms, va):
    if not syms:
        return None
    f, starts = syms
    i = bisect.bisect_right(starts, va) - 1
    if i < 0:
        return None
    a, e, n = f[i]
    if va >= e + 0x40:
        return None
    return "%s+0x%X" % (n, va - a) if va != a else n


def symbolize(text, syms):
    """Append a name to every line that carries a guest code address without one."""
    out = []
    for line in text.splitlines():
        m = re.search(r"\b0x(00[0-9A-Fa-f]{6})\b(\s+\S+)?", line)
        if m and syms and not (m.group(2) or "").strip():
            n = sym(syms, int(m.group(1), 16))
            if n and 0x11000 <= int(m.group(1), 16) < 0x616D80:
                line += "  " + n
        out.append(line)
    return "\n".join(out)


def read(folder, name, limit=None):
    p = os.path.join(folder, name)
    if not os.path.isfile(p):
        return ""
    t = open(p, errors="replace").read()
    return t if limit is None else t[-limit:]


def section(report_txt, title):
    m = re.search(r"^== %s[^\n]*\n(.*?)(?=^== |\Z)" % re.escape(title), report_txt, re.S | re.M)
    return m.group(1).strip() if m else ""


def newest_report():
    c = glob.glob(os.path.join(PORT, "build", "Release", "reports", "*.zip"))
    return max(c, key=os.path.getmtime) if c else None


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else newest_report()
    if not src or not os.path.exists(src):
        print(__doc__)
        print("no report found" if not src else "not found: %s" % src)
        return 1
    src = os.path.abspath(src)
    folder = src
    if os.path.isfile(src):
        folder = os.path.join(tempfile.gettempdir(), "btvs_report_" + os.path.splitext(os.path.basename(src))[0])
        with zipfile.ZipFile(src) as z:
            for n in z.namelist():           # never outside the folder
                if not (n.startswith("/") or ".." in n or ":" in n):
                    z.extract(n, folder)
    syms = load_syms()
    rt = read(folder, "report.txt")
    try:
        rj = json.load(open(os.path.join(folder, "report.json"), errors="replace"))
    except Exception:
        rj = {}
    replay = dict(l.split("=", 1) for l in read(folder, "replay.txt").splitlines() if "=" in l)

    print("REPORT  %s" % src)
    print("  made  %s (%s)   build %s" % (rj.get("date", "?"), rj.get("trigger", "?"), rj.get("build", "?")))
    print("  os    %s" % rj.get("os", "?"))
    print()
    print("WHAT THE PLAYER WROTE")
    print("  " + (rj.get("what") or section(rt, "What the player wrote") or "(nothing)").replace("\n", "\n  "))
    print()
    print("WHERE")
    print("  " + section(rt, "Where in the game").replace("\n", "\n  "))
    print()
    trail = read(folder, "btvs_trail.txt").splitlines()
    print("LAST TRAIL EVENTS (%d in all)" % len(trail))
    for l in trail[-25:]:
        print("  " + l)
    print()
    mem = read(folder, "memory.txt").strip()
    if mem:
        print("MEMORY")
        print("  " + mem.replace("\n", "\n  "))
        print()
    crashes = sorted(glob.glob(os.path.join(folder, "crashes", "*.txt")), key=os.path.getmtime)
    if crashes:
        c = crashes[-1]
        print("NEWEST CRASH / HANG IN THE REPORT  (%s; %d in all)" % (os.path.basename(c), len(crashes)))
        body = symbolize(open(c, errors="replace").read(), syms)
        cut = body.find("\nHost registers:")
        print("  " + (body[:cut] if cut > 0 else body)[:6000].replace("\n", "\n  "))
        print()
    where_thread = section(rt, "Where the game thread is")
    if where_thread and not crashes:
        print("WHERE THE GAME THREAD WAS")
        print("  " + symbolize(where_thread, syms)[:3000].replace("\n", "\n  "))
        print()
    have = [n for n in ("screenshot.png", "input.rec", "settings.ini", "btvs_log.txt") if os.path.isfile(os.path.join(folder, n))]
    save_n = sum(len(f) for _, _, f in os.walk(os.path.join(folder, "save")))
    print("IN THE REPORT  %s; save: %d files" % (", ".join(have) or "-", save_n))
    if "input.rec" not in have:
        print("  (no input recording: the replay stops at the anchor and the pads are yours)")
    print()
    print("TO GO BACK THERE")
    print("  anchor: %s \"%s\", then %.1f s of recorded input" % (
        replay.get("anchor", "?"), replay.get("anchor_name", ""),
        float(replay.get("report_time", 0) or 0) - float(replay.get("anchor_time", 0) or 0)))
    secs = int(float(replay.get("report_time", 0) or 0) - float(replay.get("anchor_time", 0) or 0)) + 60
    print("  hidden test run (muted, no window; frame dumps with BTVS_SHOT_AT):")
    print('    bash port/tools/run.sh replay %d "BTVS_REPLAY=%s"' % (secs, src))
    print("  on screen (PowerShell):")
    print('    $env:BTVS_REPLAY="%s"; & "%s"' % (src, os.path.join(PORT, "build", "Release", "buffy_tvs.exe")))
    print("  unpacked at: %s" % folder)
    return 0


if __name__ == "__main__":
    sys.exit(main())
