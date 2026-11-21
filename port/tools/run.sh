#!/usr/bin/env bash
# Run the dev build against the unpacked disc, muted and out of sight:
#   bash port/tools/run.sh <name> [seconds] [ENV=VAL ...]
# Writes runs/<name>.out and runs/<name>.err, then prints ICALL problems and a symbolized crash.
# The game stops itself after [seconds] (BTVS_EXIT_AFTER); the timeout is only a backstop,
# and a leftover game is stopped by its own PID, never by name.
source "$(dirname "$0")/env.sh"
NAME="${1:-run}"; SECS="${2:-60}"; shift 2 2>/dev/null
cd "$PORT"
mkdir -p runs
export BTVS_GAME_DIR="$(cd "$BTVS_MAIN/game_files" && pwd -W | tr '/' '\134')"
export RECOMP_WATCHDOG_SECS="${RECOMP_WATCHDOG_SECS:-600}"
for kv in "$@"; do export "$kv"; done
# Every run gets its own empty save folder (runs/<name>.save), so runs never share
# or pile up saved games (the game allows ten). BTVS_SAVE_DIR=<folder> keeps one instead.
OWN_SAVE=""
if [ -z "${BTVS_SAVE_DIR:-}" ] && [ -n "$NAME" ]; then
    OWN_SAVE="$PORT/runs/$NAME.save"
    rm -rf "$OWN_SAVE"; mkdir -p "$OWN_SAVE"
    export BTVS_SAVE_DIR="$(cd "$OWN_SAVE" && pwd -W | tr '/' '\')"
fi
export BTVS_NO_WINDOW=1          # hidden, never focused
export BTVS_MUTE=1               # test runs are silent
export BTVS_AFFINITY="${BTVS_AFFINITY:-FF00}"   # CPUs 8-15, below-normal priority
export BTVS_EXIT_AFTER="$SECS"
# At most 2 test games at once across every session: take one of two slots.
SLOT=""
for i in $(seq 1 180); do
    for n in 1 2; do
        L="$BTVS_MAIN/port/.game.slot$n"
        [ -n "$(find "$L" -maxdepth 0 -mmin +30 2>/dev/null)" ] && rmdir "$L" 2>/dev/null
        if mkdir "$L" 2>/dev/null; then SLOT="$L"; break 2; fi
    done
    sleep 5
done
[ -n "$SLOT" ] || { echo "no free test-game slot"; exit 1; }
trap 'rmdir "$SLOT" 2>/dev/null' EXIT
build/Release/buffy_tvs.exe > "runs/$NAME.out" 2> "runs/$NAME.err" &
PID=$!
( sleep $((SECS + 15)); kill -9 $PID 2>/dev/null ) &
KILLER=$!
wait $PID; echo "exit=$?"
kill $KILLER 2>/dev/null
# (the game's copy of its disc files and the disk images are not worth keeping: saves and settings stay)
[ -n "$OWN_SAVE" ] && rm -rf "$OWN_SAVE/Cache" "$OWN_SAVE"/Partition*.img
grep -E "^\[ICALL\]|\[CRASH\]|WATCHDOG|Cannot open" "runs/$NAME.err" | head -25
python tools/sym.py "runs/$NAME.err" | head -16
