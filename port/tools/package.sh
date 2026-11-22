#!/usr/bin/env bash
# A release folder and zip with NO game data and NO translated game code: the
# player installs the game from their own Xbox disc image with the launcher,
# which also translates and builds the game's code on their PC
# (launcher/btvs_build.inc, docs/install-build.md).
#   bash port/tools/package.sh [--allow-incomplete]
#     -> dist/Buffy the Vampire Slayer PC/  and  dist/Buffy-the-Vampire-Slayer-PC-v<version>.zip
# The version is BTVS_VERSION in port/CMakeLists.txt.
#
# What goes in: the launcher; buffy_tvs.exe linked WITHOUT the game's code
# (tools/gamedll/proto_build.sh makes it from the normal build's objects; it
# loads btvs_game.dll, which only exists on the player's PC); and "build", the
# kit the launcher builds that DLL with: the translate program, clang and
# lld-link (LLVM, Apache-2.0 with the LLVM exception), a few headers and lists.
# Needs: an up-to-date normal build, LLVM in C:\Program Files\LLVM,
#   port/build/gamedll/btvs_translate.exe   (translate.py frozen; or BTVS_TRANSLATE_EXE=<file>)
#   port/build/gamedll/LLVM-LICENSE.TXT     (LLVM's licence text; or BTVS_LLVM_LICENSE=<file>)
#
# After zipping, the zip is checked: it may hold only the files named below
# and the kit, nothing with a disc file's name, type or content, and no
# translated game code in any form (the exe with the code inside, C files,
# objects, the DLL). A failed check deletes the zip.
#   --allow-incomplete   package although the translate program or LLVM's
#                        licence is missing (to test this script); the result
#                        is not a release and says so
set -euo pipefail
source "$(dirname "$0")/env.sh"
ROOT="$(cd "$PORT/.." && pwd)"
BIN="$PORT/build/Release"
MAINBIN="$BTVS_MAIN/port/build/Release"
NAME="Buffy the Vampire Slayer PC"
OUT="$ROOT/dist/$NAME"
VER="$(sed -n 's/^set(BTVS_VERSION "\(.*\)")/\1/p' "$PORT/CMakeLists.txt")"
[ -n "$VER" ] || { echo "no BTVS_VERSION in port/CMakeLists.txt"; exit 1; }
ZIP="Buffy-the-Vampire-Slayer-PC-v$VER.zip"
ALLOW_MISSING=0
[ "${1:-}" = "--allow-incomplete" ] && ALLOW_MISSING=1
WORK="$PORT/build/gamedll"
TREXE="${BTVS_TRANSLATE_EXE:-$WORK/btvs_translate.exe}"
LLVMLIC="${BTVS_LLVM_LICENSE:-$WORK/LLVM-LICENSE.TXT}"

pick() { if [ -f "$BIN/$1" ]; then echo "$BIN/$1"; elif [ -f "$MAINBIN/$1" ]; then echo "$MAINBIN/$1"; fi; }

rm -rf "$OUT"
mkdir -p "$OUT"
L="$(pick "Buffy Launcher.exe")"
[ -n "$L" ] || { echo "Buffy Launcher.exe is not built (bash port/tools/build.sh)"; exit 1; }
cp -f "$L" "$OUT/"
[ -f "$BIN/buffy_tvs.exe" ] || { echo "buffy_tvs.exe is not built here (bash port/tools/build.sh)"; exit 1; }
INCOMPLETE=0
for f in "$TREXE" "$LLVMLIC"; do
    [ -f "$f" ] && continue
    if [ "$ALLOW_MISSING" = 1 ]; then echo "WARNING: no $f; this package is NOT a release"; INCOMPLETE=1
    else echo "missing: $f (see the top of this script); --allow-incomplete to test packaging without it"; exit 1; fi
done
# The exe without game code and the build kit (the symbol pass of the first run takes a few minutes).
BTVS_GAMEDLL_KIT="$OUT" BTVS_TRANSLATE_EXE="$TREXE" bash "$PORT/tools/gamedll/proto_build.sh" "$WORK" > "$WORK.log" 2>&1 \
    || { tail -20 "$WORK.log"; echo "the build kit could not be made ($WORK.log)"; exit 1; }
tail -1 "$WORK.log"
[ -f "$LLVMLIC" ] && cp -f "$LLVMLIC" "$OUT/build/llvm/LICENSE.TXT"
[ "$INCOMPLETE" = 1 ] && echo "NOT A RELEASE: the translate program or LLVM's licence is missing" > "$OUT/NOT A RELEASE.txt"
S="$(pick "SDL3.dll")"   # controllers other than XInput (Controls area)
[ -n "$S" ] && cp -f "$S" "$OUT/" || echo "note: no SDL3.dll beside the build"
cp -f "$PORT/launcher/Read Me.txt" "$OUT/Read Me.txt"
# Visual C++ runtime, app-local, for PCs without the redistributable.
CRT="$(ls -d "/c/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools/VC/Redist/MSVC/"*/x64/Microsoft.VC142.CRT 2>/dev/null | tail -1 || true)"
if [ -n "$CRT" ]; then
    cp -f "$CRT/vcruntime140.dll" "$CRT/vcruntime140_1.dll" "$CRT/msvcp140.dll" "$OUT/"
else
    echo "warning: VC++ runtime DLLs not found; players will need the VC++ redistributable"
fi

cd "$ROOT/dist"
rm -f "$ZIP"
bash "$PORT/tools/limit.sh" powershell -NoProfile -Command "Compress-Archive -Path '$NAME' -DestinationPath '$ZIP'"

# The check: only our own files, nothing from the disc, no translated game code.
if ! python - "$ZIP" "$NAME" "$BTVS_MAIN/game_files" "$PORT/icon/btvs.ico" <<'EOF'
import hashlib, os, struct, sys, zipfile
zip_path, top, game = sys.argv[1:4]
allowed = {"buffy launcher.exe", "buffy_tvs.exe", "buffy_tvs.pdb", "read me.txt", "not a release.txt",
           "sdl3.dll", "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll"}
# the build kit: these files, the port's headers, and LLVM's two programs, headers and licence
kit_files = {"kit.ini", "btvs_translate.exe", "game_dll.c", "btvs_game_tls.h", "game_link.def", "manual_functions.json",
             "buffy_tvs_exe.lib", "ucrt_min.lib", "k32_min.lib",
             "include/math.h", "include/string.h", "include/stdlib.h", "include/setjmp.h", "include/malloc.h",
             "llvm/bin/clang.exe", "llvm/bin/lld-link.exe", "llvm/license.txt"}
EXE_MAX = 16 << 20          # the exe without the game's code is 3 MB; with it, 70 MB
disc_ext = {".xbe", ".iso", ".xiso", ".pak", ".wma", ".wmv", ".rpe", ".xpr", ".mtx", ".map", ".dat",
            ".bin", ".xbx", ".xmv", ".sav"}
disc_names, disc_sizes = set(), {}
if os.path.isdir(game):
    for d, _, fs in os.walk(game):
        for f in fs:
            disc_names.add(f.lower())
            disc_sizes.setdefault(os.path.getsize(os.path.join(d, f)), []).append(os.path.join(d, f))
else:
    print("note: no game_files/ here, so content was checked by name and type only")
bad = []
seen_licence = False
# the port's own drawn icon: every image of it must be in both programs as shipped. An exe whose icon
# was replaced on a PC with the game's real one (made from a disc at install) has none of them.
ico = open(sys.argv[4], "rb").read()
own_icons = []
for k in range(struct.unpack_from("<H", ico, 4)[0]):
    size, off = struct.unpack_from("<II", ico, 6 + 16 * k + 8)
    own_icons.append(ico[off:off + size])
z = zipfile.ZipFile(zip_path)
for i in z.infolist():
    n = i.filename.replace("\\", "/")
    if n.endswith("/"):
        continue
    parts = n.split("/")
    base = parts[-1].lower()
    low = n.lower()
    # translated game code, in any form, anywhere
    if base.startswith("recomp_") or base in ("btvs_game.dll", "btvs_game.pdb", "btvs_recomp.lib") \
            or base.endswith((".obj", ".o")) or (base.endswith(".c") and low != top.lower() + "/build/game_dll.c"):
        bad.append(n + ": translated game code (or an object or source file that could be)")
        continue
    if base.endswith((".ico", ".icon.tmp", ".xbx")):
        bad.append(n + ": an icon file (the game's real icon is made on the player's PC and never ships)")
        continue
    if base in ("buffy_tvs.exe", "buffy launcher.exe") and len(parts) == 2:
        data = z.read(i)
        if not all(b in data for b in own_icons):
            bad.append(n + ": its icon is not the port's own drawn one (port/icon/btvs.ico)")
            continue
    if base == "buffy_tvs.exe":
        data = z.read(i)
        if i.file_size > EXE_MAX or b"btvs_game.dll" not in data:
            bad.append(n + ": this exe has the game's code inside (%d MB, or it does not load btvs_game.dll)" % (i.file_size >> 20))
            continue
    if len(parts) > 2 and parts[0] == top and parts[1] == "build":
        rel = "/".join(parts[2:]).lower()
        if rel in kit_files:
            if rel == "llvm/license.txt":
                seen_licence = True
            continue
        if rel.startswith("llvm/lib/clang/") and "/include/" in rel and base.endswith((".h", ".modulemap", ".inc", ".def", ".td")):
            continue
        bad.append(n + ": not a file of the build kit")
        continue
    if len(parts) != 2 or parts[0] != top:
        bad.append(n + ": outside the release folder")
    elif base not in allowed:
        bad.append(n + ": not a file of the port")
    elif os.path.splitext(base)[1] in disc_ext or base in disc_names:
        bad.append(n + ": a disc file's name or type")
    elif i.file_size in disc_sizes:
        h = hashlib.sha256(z.read(i)).hexdigest()
        for p in disc_sizes[i.file_size]:
            if hashlib.sha256(open(p, "rb").read()).hexdigest() == h:
                bad.append(n + ": the same content as the disc's " + os.path.relpath(p, game))
names = {i.filename.replace("\\", "/").lower() for i in z.infolist()}
if top.lower() + "/not a release.txt" in names:
    print("NOTE: this package says it is not a release")
elif not seen_licence or top.lower() + "/build/btvs_translate.exe" not in names:
    bad.append("the build kit is incomplete (LLVM's licence or the translate program is missing)")
for b in bad:
    print("NOT ALLOWED IN THE PACKAGE: " + b)
print("checked %d files in %s: %s" % (len([i for i in z.infolist() if not i.filename.endswith('/')]),
                                      zip_path, "FAILED" if bad else "no game data, no translated game code"))
sys.exit(1 if bad else 0)
EOF
then
    rm -f "$ZIP"
    echo "the zip was deleted"; exit 1
fi
echo "packaged v$VER: dist/$NAME ($(du -sh "$NAME" | cut -f1)), dist/$ZIP ($(du -h "$ZIP" | cut -f1))"
