#!/usr/bin/env bash
# Run a command on a few CPUs at low priority, so builds and test runs never
# get in the way of whatever else the PC is doing (the owner games on 0-7).
#   bash port/tools/limit.sh <command> [args...]          builds: CPUs 12-15, low
#   BTVS_AFFINITY=FF00 bash port/tools/limit.sh ...        CPUs 8-15
# This shell limits itself, then becomes the command: children inherit the
# limits and the command's exit code comes back unchanged.
MASK="${BTVS_AFFINITY:-F000}"
WINPID="$(cat /proc/$$/winpid)"
powershell -NoProfile -Command "\$p=Get-Process -Id $WINPID; \$p.ProcessorAffinity=0x$MASK; \$p.PriorityClass='Idle'" || { echo "limit.sh: could not set CPU limits"; exit 1; }
exec "$@"
