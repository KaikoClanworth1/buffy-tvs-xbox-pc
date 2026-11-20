#!/usr/bin/env bash
# Configure (first time) and build Release, on CPUs 12-15 at low priority.
#   bash port/tools/build.sh [target]
# One build at a time across every session and worktree (a lock in the main
# checkout): a second caller waits its turn.
# In a git worktree the translated game code is not rebuilt: the main
# checkout's port/build/Release/btvs_recomp.lib is linked.
set -euo pipefail
source "$(dirname "$0")/env.sh"
cd "$PORT"
LOCK="$BTVS_MAIN/port/.build.lock"
for i in $(seq 1 360); do
    mkdir "$LOCK" 2>/dev/null && break
    # a lock older than 90 minutes is from a build that died
    [ -n "$(find "$LOCK" -maxdepth 0 -mmin +90 2>/dev/null)" ] && rmdir "$LOCK" 2>/dev/null && continue
    [ "$i" = 1 ] && echo "waiting for another build to finish..."
    sleep 10
done
trap 'rmdir "$LOCK" 2>/dev/null' EXIT

IN_WORKTREE=0
[ "$(cd "$BTVS_MAIN" && pwd)" != "$(cd "$PORT/.." && pwd)" ] && IN_WORKTREE=1
if [ "$IN_WORKTREE" = 1 ] && [ ! -f "$BTVS_MAIN/port/build/Release/btvs_recomp.lib" ]; then
    echo "The main checkout has not built btvs_recomp.lib yet (its first build takes about an hour)."
    echo "Try again later; do not build the translated code in a worktree."
    exit 2
fi
if [ ! -f build/CMakeCache.txt ]; then
    CFG=(-S . -B build -G "Visual Studio 16 2019" -A x64)   # MSVC 14.29, as the Chaos Bleeds port
    [ "$(cd "$BTVS_MAIN" && pwd)" != "$(cd "$PORT/.." && pwd)" ] && CFG+=("-DBTVS_MAIN=$(cd "$BTVS_MAIN" && pwd -W)")
    # a worktree has no copy of the toolkit's submodules: use the main checkout's toolkit
    [ ! -f "$PORT/../xboxrecomp/third_party/glslang/CMakeLists.txt" ] && [ -f "$BTVS_MAIN/xboxrecomp/third_party/glslang/CMakeLists.txt" ]         && CFG+=("-DXBOXRECOMP_DIR=$(cd "$BTVS_MAIN/xboxrecomp" && pwd -W)")
    cmake "${CFG[@]}" > build_configure.log 2>&1 || { tail -30 build_configure.log; exit 1; }
fi
ARGS=(--build build --config Release)
[ -n "${1:-}" ] && ARGS+=(--target "$1")
ARGS+=(-- -m:1 -nologo -v:m)
export CL="${CL:-} -MP4"
if bash tools/limit.sh cmake "${ARGS[@]}" > build_log.txt 2>&1; then
    echo "build ok"
else
    echo "build FAILED (port/build_log.txt)"; grep -E "error|fatal" build_log.txt | head -40; exit 1
fi
