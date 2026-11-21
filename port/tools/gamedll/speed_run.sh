#!/usr/bin/env bash
# One timed run for comparing two builds of the game (Extras, install-time build):
#   bash port/tools/gamedll/speed_run.sh <name> <folder with buffy_tvs.exe> <output folder> [seconds]
# The same for every build: game data from game_files/, an empty save folder,
# hidden, muted, CPUs 8-15, the scripted way into the first level and the
# scripted walk there (Controls' boot_to_level.txt and level_walk.txt), the
# runtime's file log off. Every frame of the level's run goes to
# <output folder>/<name>.csv (BTVS_FRAME_LOG); speed_report.py reads those.
# Takes one of the two test-game slots, like run.sh.
source "$(dirname "$0")/../env.sh"
NAME="$1"; EXEDIR="$(cd "$2" && pwd)"; OUT="$(mkdir -p "$3" && cd "$3" && pwd)"; SECS="${4:-90}"
W() { (cd "$1" && pwd -W | tr '/' '\134'); }
rm -rf "$OUT/$NAME.save" "$OUT/$NAME.logs"; mkdir -p "$OUT/$NAME.save" "$OUT/$NAME.logs"
export BTVS_GAME_DIR="$(W "$BTVS_MAIN/game_files")"
export BTVS_SAVE_DIR="$(W "$OUT/$NAME.save")" BTVS_LOG_DIR="$(W "$OUT/$NAME.logs")"
export BTVS_SETTINGS_INI="$(W "$OUT/$NAME.logs")\\settings.ini"
export BTVS_NO_WINDOW=1 BTVS_MUTE=1 BTVS_AFFINITY=FF00 BTVS_EXIT_AFTER="$SECS"
export BTVS_SKIP_MOVIES=1 RECOMP_FILE_LOG=0 RECOMP_WATCHDOG_SECS=600
export BTVS_PAD_SCRIPT="$(W "$PORT/src/btvs_input_scripts")\\boot_to_level.txt"
export BTVS_PAD_SCRIPT_1="$(W "$PORT/src/btvs_input_scripts")\\level_walk.txt"
export BTVS_FRAME_LOG="$(W "$OUT")\\$NAME.csv" BTVS_INPUT_POS_LOG=60
SLOT=""
for i in $(seq 1 360); do
    for n in 1 2; do
        L="$BTVS_MAIN/port/.game.slot$n"
        [ -n "$(find "$L" -maxdepth 0 -mmin +30 2>/dev/null)" ] && rmdir "$L" 2>/dev/null
        if mkdir "$L" 2>/dev/null; then SLOT="$L"; break 2; fi
    done
    sleep 5
done
[ -n "$SLOT" ] || { echo "no free test-game slot"; exit 1; }
trap 'rmdir "$SLOT" 2>/dev/null' EXIT
cd "$EXEDIR" || exit 1
./buffy_tvs.exe > "$OUT/$NAME.out" 2> "$OUT/$NAME.err" &
PID=$!
( sleep $((SECS + 20)); kill -9 $PID 2>/dev/null ) &
KILLER=$!
wait $PID; echo "$NAME exit=$?"
kill $KILLER 2>/dev/null
rm -rf "$OUT/$NAME.save"
