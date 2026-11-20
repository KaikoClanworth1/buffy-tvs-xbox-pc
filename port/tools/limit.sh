#!/usr/bin/env bash
# Run a command on a few CPUs at low priority, so builds and test runs never
# get in the way of whatever else the PC is doing (the owner games on 0-7).
#   bash port/tools/limit.sh <command> [args...]          builds: CPUs 12-15, low
#   BTVS_AFFINITY=FF00 bash port/tools/limit.sh ...        test games: CPUs 8-15
MASK="${BTVS_AFFINITY:-F000}"
exec cmd //c start "" //b //wait //low //affinity "$MASK" "$@"
