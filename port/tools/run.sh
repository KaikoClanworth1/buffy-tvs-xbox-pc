#!/usr/bin/env bash
# Run the dev build against the unpacked disc, muted and out of sight:
#   bash port/tools/run.sh <name> [seconds] [ENV=VAL ...]
# Writes runs/<name>.out and runs/<name>.err, then prints ICALL problems and a symbolized crash.
# The game stops itself after [seconds] (BTVS_EXIT_AFTER); the timeout is only a backstop,
# and a leftover game is stopped by its own PID, never by name.
PORT="$(cd "$(dirname "$0")/.." && pwd)"
NAME="${1:-run}"; SECS="${2:-60}"; shift 2 2>/dev/null
cd "$PORT"
mkdir -p runs
export BTVS_GAME_DIR="$(cd "$PORT/../game_files" && pwd -W | tr '/' '\134')"
export RECOMP_WATCHDOG_SECS="${RECOMP_WATCHDOG_SECS:-600}"
for kv in "$@"; do export "$kv"; done
export BTVS_NO_WINDOW=1          # hidden, never focused
export BTVS_MUTE=1               # test runs are silent
export BTVS_AFFINITY="${BTVS_AFFINITY:-FF00}"   # CPUs 8-15, below-normal priority
export BTVS_EXIT_AFTER="$SECS"
build/Release/buffy_tvs.exe > "runs/$NAME.out" 2> "runs/$NAME.err" &
PID=$!
( sleep $((SECS + 15)); kill -9 $PID 2>/dev/null ) &
KILLER=$!
wait $PID; echo "exit=$?"
kill $KILLER 2>/dev/null
grep -E "^\[ICALL\]|\[CRASH\]|WATCHDOG|Cannot open" "runs/$NAME.err" | head -25
python tools/sym.py "runs/$NAME.err" | head -16
