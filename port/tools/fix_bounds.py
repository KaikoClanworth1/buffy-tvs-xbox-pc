"""Extend map-named functions to the next map symbol.

tools.disasm ends a function at the last byte it could reach by direct flow, so
code reached only through the function's own jump table (MSVC's memmove/memcpy
tails, some switch arms) falls outside it and the lifter turns those jumps into
failing indirect calls. The linker map is authoritative about where functions
start, so a map function owns everything up to the next function start -- unless
the disassembler found another entry inside that range (SEH funclets, CRT
alternate entry points), in which case it is left alone.

    python tools/fix_bounds.py <functions.json> <map_cnames.json>
"""
import json
import sys


def main():
    fpath, npath = sys.argv[1:3]
    funcs = json.load(open(fpath))
    names = json.load(open(npath))
    funcs.sort(key=lambda x: int(x["start"], 16))
    changed = 0
    for i in range(len(funcs) - 1):
        cur, nxt = funcs[i], funcs[i + 1]
        if "0x%08X" % int(cur["start"], 16) not in names:
            continue
        if cur["section"] != nxt["section"]:
            continue
        end, nstart = int(cur["end"], 16), int(nxt["start"], 16)
        if nstart > end:
            cur["end"] = "0x%08X" % nstart
            cur["size"] = nstart - int(cur["start"], 16)
            changed += 1
    # The disassembler follows at most 8192 instructions of one function and
    # then starts an unnamed function ("tail_jump_alias") in the middle of it.
    # The lifter gives that one the rest of the code, and the named function's
    # jump table can no longer reach its own cases: every particle template
    # past the 451st in TGBuffyInterface::ParticleTemplateSetup (10168
    # instructions, a 535-way switch) became a failed indirect call. Such an
    # alias inside a map function that long is dropped again.
    named = [(int(f["start"], 16), int(f["end"], 16)) for f in funcs
             if "0x%08X" % int(f["start"], 16) in names and f.get("num_instructions", 0) >= 8192]
    kept = []
    for f in funcs:
        s = int(f["start"], 16)
        if (f.get("detection_method") == "tail_jump_alias" and "0x%08X" % s not in names
                and any(a < s < b for a, b in named)):
            print("dropped %s: the tail of a function longer than the disassembler follows" % f["name"])
            continue
        kept.append(f)
    funcs = kept
    json.dump(funcs, open(fpath, "w"), indent=1)
    print("extended %d functions" % changed)


if __name__ == "__main__":
    main()
