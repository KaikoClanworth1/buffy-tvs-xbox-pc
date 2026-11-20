#!/usr/bin/env bash
# Configure (first time) and build, on CPUs 12-15 at low priority.
#   bash port/tools/build.sh [target]
set -euo pipefail
PORT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PORT"
[ -f build/CMakeCache.txt ] || cmake -S . -B build -A x64 > build_configure.log 2>&1 || { tail -30 build_configure.log; exit 1; }
ARGS=(--build build --config Release -- -m:4 -nologo -v:m)
[ -n "${1:-}" ] && ARGS=(--build build --config Release --target "$1" -- -m:4 -nologo -v:m)
export CL="${CL:-} /MP4"
bash tools/limit.sh cmake "${ARGS[@]}" > build_log.txt 2>&1 && echo "build ok" || { echo "build FAILED"; grep -E "error|fatal" build_log.txt | head -40; exit 1; }
