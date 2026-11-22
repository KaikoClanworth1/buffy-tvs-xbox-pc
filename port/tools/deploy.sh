#!/usr/bin/env bash
# Refresh the playable folder:  bash port/tools/deploy.sh
#   GAME/buffy_tvs.exe (+ .pdb, for readable crash reports)
#   GAME/Buffy Launcher.exe, GAME/Read Me.txt
#   GAME/Default.xbe, pak/, media/, ...   the disc's files, hard-linked from
#                                         game_files/ (same drive: no copy)
# GAME/ is in the main checkout (ignored by git); BTVS_DEPLOY_DIR puts it
# elsewhere. Run after every rebuild. GAME/XboxData (the game's hard disk:
# saved games, its cache) and an existing btvs_settings.ini are never touched,
# and nothing is ever linked into a save folder. A missing settings file is
# written with the first-start defaults (borderless at the desktop's size,
# sound on).
# Two disc files are copied, not linked: the game opens them unshared, so a
# linked copy would collide with a test game running from game_files/.
set -euo pipefail
source "$(dirname "$0")/env.sh"
SRC="$BTVS_MAIN/game_files"
DST="${BTVS_DEPLOY_DIR:-$BTVS_MAIN/GAME}"
BIN="$PORT/build/Release"
MAINBIN="$BTVS_MAIN/port/build/Release"

[ -f "$SRC/Default.xbe" ] || { echo "no game_files/Default.xbe in $BTVS_MAIN (unpack your disc first)"; exit 1; }
mkdir -p "$DST"
rmdir "$DST/SaveData" 2>/dev/null || true      # (older deploys made it; the game's folder is XboxData)

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
# The game's real icon, from this disc: btvs_icon.ico and the copied exe's icon (as an install does).
if [ "$missing" = 0 ] && [ -f "$DST/Default.xbe" ]; then
    ( cd "$DST" && "./Buffy Launcher.exe" --make-icon . ) || echo "the exe keeps the port's drawn icon"
fi
if [ ! -e "$DST/btvs_settings.ini" ]; then
    printf '%s\r\n' "; Buffy the Vampire Slayer - PC settings. The game's Options menu writes this file." \
        "[display]" "mode=1" "width=0" "height=0" "vsync=1" \
        "[audio]" "master=100" "music=100" "effects=100" "speech=100" > "$DST/btvs_settings.ini"
    echo "wrote the default btvs_settings.ini"
fi

# Disc data: link once (only missing files), so reruns are instant.
cd "$SRC"
n=0; busy=0
while IFS= read -r f; do
    t="$DST/${f#./}"
    case "$f" in
        ./install.dat|./Sound/dsstdfx.bin)
            if [ ! -e "$t" ] || [ "$t" -ef "$f" ]; then
                mkdir -p "$(dirname "$t")"
                if cp "$f" "$t.new" 2>/dev/null; then
                    mv -f "$t.new" "$t"; n=$((n + 1))
                else        # a running test game holds it open: try again when it has ended
                    rm -f "$t.new"; echo "could not copy ${f#./} (in use by a running game?): run deploy.sh again"; busy=1
                fi
            fi
            continue ;;
    esac
    if [ ! -e "$t" ]; then
        mkdir -p "$(dirname "$t")"
        ln "$f" "$t" 2>/dev/null || cp "$f" "$t"
        n=$((n + 1))
    fi
done < <(find . -type f ! -path './SaveData/*' ! -path './XboxData/*' ! -path './UDATA/*' ! -path './TDATA/*' \
              ! -name '*.log' ! -name 'btvs_*')
echo "deployed to $DST ($n game files linked or copied)"
[ "$busy" = 0 ] || exit 3
[ "$missing" = 0 ] || { echo "the folder is not playable yet: see 'not built yet' above"; exit 2; }
