#!/usr/bin/env bash
# Refresh the playable folder:  bash port/tools/deploy.sh
#   GAME/buffy_tvs.exe (+ .pdb, for readable crash reports)
#   GAME/Buffy Launcher.exe, GAME/Read Me.txt
#   GAME/Default.xbe, pak/, media/, ...   the disc's files, hard-linked from
#                                         game_files/ (same drive: no copy)
# GAME/ is in the main checkout (ignored by git); BTVS_DEPLOY_DIR puts it
# elsewhere. Run after every rebuild. GAME/SaveData and the settings file are
# never touched, and nothing is ever linked into a save folder.
set -euo pipefail
source "$(dirname "$0")/env.sh"
SRC="$BTVS_MAIN/game_files"
DST="${BTVS_DEPLOY_DIR:-$BTVS_MAIN/GAME}"
BIN="$PORT/build/Release"
MAINBIN="$BTVS_MAIN/port/build/Release"

[ -f "$SRC/Default.xbe" ] || { echo "no game_files/Default.xbe in $BTVS_MAIN (unpack your disc first)"; exit 1; }
mkdir -p "$DST" "$DST/SaveData"

# The newest build of each program: this checkout's, else the main checkout's.
pick() { if [ -f "$BIN/$1" ]; then echo "$BIN/$1"; elif [ -f "$MAINBIN/$1" ]; then echo "$MAINBIN/$1"; fi; }
missing=0
for f in "buffy_tvs.exe" "buffy_tvs.pdb" "SDL3.dll" "Buffy Launcher.exe"; do
    p="$(pick "$f")"
    if [ -n "$p" ]; then
        cp -f "$p" "$DST/"
    else
        echo "not built yet: $f"
        [ "$f" = "buffy_tvs.pdb" ] || [ "$f" = "SDL3.dll" ] || missing=1
    fi
done
cp -f "$PORT/launcher/Read Me.txt" "$DST/Read Me.txt"

# Disc data: link once (only missing files), so reruns are instant.
cd "$SRC"
n=0
while IFS= read -r f; do
    t="$DST/${f#./}"
    if [ ! -e "$t" ]; then
        mkdir -p "$(dirname "$t")"
        ln "$f" "$t" 2>/dev/null || cp "$f" "$t"
        n=$((n + 1))
    fi
done < <(find . -type f ! -path './SaveData/*' ! -path './XboxData/*' ! -path './UDATA/*' ! -path './TDATA/*' \
              ! -name '*.log' ! -name 'btvs_*')
echo "deployed to $DST ($n game files linked)"
[ "$missing" = 0 ] || { echo "the folder is not playable yet: see 'not built yet' above"; exit 2; }
