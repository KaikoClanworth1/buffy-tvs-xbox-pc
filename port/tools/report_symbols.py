"""Write btvs_symbols.txt: the game's function names for crash reports of a
build shipped without its .pdb ("XXXXXXXX Name" per line, sorted).

    python port/tools/report_symbols.py [out file]     # default: port/build/Release/btvs_symbols.txt

The names come from the disc's linker map (through the disassembly's
functions.json), so the file is game material: generated locally, never
committed, and whether a release carries it is the packager's decision.
Without it (and without a .pdb) crash reports carry addresses only and
report_open.py names them.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)
m = re.match(r"(.*)[\\/]\.claude[\\/]worktrees[\\/][^\\/]+$", os.path.dirname(PORT))
MAIN = os.environ.get("BTVS_MAIN") or (m.group(1) if m else os.path.dirname(PORT))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(PORT, "build", "Release", "btvs_symbols.txt")
funcs = json.load(open(os.path.join(MAIN, "xboxrecomp", "tools", "disasm", "output", "functions.json")))
rows = sorted((int(x["start"], 16), x["name"]) for x in funcs)
with open(out, "w") as f:
    for va, name in rows:
        f.write("%08X %s\n" % (va, name))
print("%d names -> %s" % (len(rows), out))
