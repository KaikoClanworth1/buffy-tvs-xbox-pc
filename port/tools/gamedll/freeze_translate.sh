#!/usr/bin/env bash
# translate.py and the recompiler's Python tools frozen into one exe for the
# player's PC, which has no Python:
#   bash port/tools/gamedll/freeze_translate.sh      -> port/build/gamedll/btvs_translate.exe
# Needs PyInstaller in port/build/gamedll/venv (once:
#   python -m venv --system-site-packages port/build/gamedll/venv
#   port/build/gamedll/venv/Scripts/python -m pip install pyinstaller )
# and capstone in the Python that venv was made from. Runs on CPUs 12-15 at
# low priority. The exe holds the port's and the recompiler's tools only: no
# game material.
set -euo pipefail
source "$(dirname "$0")/../env.sh"
HERE="$(cd "$(dirname "$0")" && pwd -W)"
TK="$(cd "$PORT/../xboxrecomp" && pwd -W)"
W="$(mkdir -p "$PORT/build/gamedll" && cd "$PORT/build/gamedll" && pwd -W)"
PY="$W/venv/Scripts/python.exe"
[ -x "$PY" ] || { echo "no PyInstaller venv in $W/venv (see the top of this script)"; exit 1; }
export MSYS2_ARG_CONV_EXCL='*'
cd "$W"
rm -rf freeze && mkdir freeze
# PYTHONPATH: PyInstaller gathers the "tools" package before it reads --paths
PYTHONPATH="$TK" bash "$PORT/tools/limit.sh" "$PY" -m PyInstaller --noconfirm --clean --onefile --console --name btvs_translate \
    --distpath "$W/freeze/dist" --workpath "$W/freeze/work" --specpath "$W/freeze" \
    --paths "$TK" --collect-submodules tools --hidden-import capstone --collect-all capstone \
    --add-data "$TK/tools;tools" \
    --add-data "$TK/templates/runtime/recomp_types.h;templates/runtime" \
    --add-data "$HERE/../map_seeds.py;port_tools" --add-data "$HERE/../fix_bounds.py;port_tools" \
    "$HERE/translate.py" > "$W/freeze.log" 2>&1 || { tail -30 "$W/freeze.log"; exit 1; }
cp -f "$W/freeze/dist/btvs_translate.exe" "$W/btvs_translate.exe"
ls -la "$W/btvs_translate.exe"
