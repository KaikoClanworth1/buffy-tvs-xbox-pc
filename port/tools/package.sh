#!/usr/bin/env bash
# A release folder and zip with NO game data: the player installs the game
# from their own Xbox disc image with the launcher.
#   bash port/tools/package.sh [--allow-missing-game]
#     -> dist/Buffy the Vampire Slayer PC/  and  dist/Buffy-the-Vampire-Slayer-PC-v<version>.zip
# The version is BTVS_VERSION in port/CMakeLists.txt.
# After zipping, the zip is checked: it may hold only the files named below,
# and nothing with a disc file's name, type or content. A failed check deletes
# the zip.
#   --allow-missing-game   package without buffy_tvs.exe (to test this script
#                          before the game links); the result is not a release
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
[ "${1:-}" = "--allow-missing-game" ] && ALLOW_MISSING=1

pick() { if [ -f "$BIN/$1" ]; then echo "$BIN/$1"; elif [ -f "$MAINBIN/$1" ]; then echo "$MAINBIN/$1"; fi; }

rm -rf "$OUT"
mkdir -p "$OUT"
L="$(pick "Buffy Launcher.exe")"
[ -n "$L" ] || { echo "Buffy Launcher.exe is not built (bash port/tools/build.sh)"; exit 1; }
cp -f "$L" "$OUT/"
G="$(pick "buffy_tvs.exe")"
if [ -n "$G" ]; then
    cp -f "$G" "$OUT/"
    [ -f "${G%.exe}.pdb" ] && cp -f "${G%.exe}.pdb" "$OUT/" || echo "warning: no buffy_tvs.pdb (crash reports will not name functions)"
elif [ "$ALLOW_MISSING" = 1 ]; then
    echo "WARNING: buffy_tvs.exe is not built; this package is NOT a release"
else
    echo "buffy_tvs.exe is not built (bash port/tools/build.sh); --allow-missing-game to test packaging without it"; exit 1
fi
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
powershell -NoProfile -Command "Compress-Archive -Path '$NAME' -DestinationPath '$ZIP'"

# The check: only our own files, nothing from the disc.
if ! python - "$ZIP" "$NAME" "$BTVS_MAIN/game_files" <<'EOF'
import hashlib, os, sys, zipfile
zip_path, top, game = sys.argv[1:4]
allowed = {"buffy launcher.exe", "buffy_tvs.exe", "buffy_tvs.pdb", "read me.txt",
           "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll"}
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
z = zipfile.ZipFile(zip_path)
for i in z.infolist():
    n = i.filename.replace("\\", "/")
    if n.endswith("/"):
        continue
    parts = n.split("/")
    base = parts[-1].lower()
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
for b in bad:
    print("GAME MATERIAL IN THE PACKAGE: " + b)
print("checked %d files in %s: %s" % (len([i for i in z.infolist() if not i.filename.endswith('/')]),
                                      zip_path, "FAILED" if bad else "no game data"))
sys.exit(1 if bad else 0)
EOF
then
    rm -f "$ZIP"
    echo "the zip was deleted"; exit 1
fi
echo "packaged v$VER: dist/$NAME ($(du -sh "$NAME" | cut -f1)), dist/$ZIP ($(du -h "$ZIP" | cut -f1))"
