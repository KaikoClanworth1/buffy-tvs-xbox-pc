#!/usr/bin/env bash
# Full XBE -> C regeneration for Buffy the Vampire Slayer (Xbox, 2002).
# Run from anywhere: bash port/tools/regen.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PORT="$ROOT/port"
TK="$ROOT/xboxrecomp"
GF="$TK/game_files"
mkdir -p "$GF"
[ -f "$ROOT/game_files/Default.xbe" ] || { echo "No game_files/Default.xbe: unpack your disc image into game_files/ first"; exit 1; }
[ -f "$ROOT/game_files/final.map" ] || { echo "No game_files/final.map (it is on the game disc)"; exit 1; }
cp -f "$ROOT/game_files/Default.xbe" "$GF/default.xbe"
cd "$TK"

python -m tools.xbe_parser "$GF/default.xbe" --json "$GF/default_analysis.json" > /dev/null
python "$PORT/tools/map_seeds.py" "$ROOT/game_files/final.map" "$GF/default_analysis.json" "$GF"
python -m tools.disasm "$GF/default.xbe" --force --seed-functions "$GF/map_seeds.json" > /dev/null

# DOLBY is DSP microcode, not x86.
python - <<'PY'
import json
p = 'tools/disasm/output/functions.json'
f = [x for x in json.load(open(p)) if x['section'] != 'DOLBY']
json.dump(f, open(p, 'w'), indent=1)
PY

python tools/ghidra_naming/merge_names.py --names-json "$GF/map_cnames.json" --out "$GF/applied_names.json" --apply > /dev/null
python "$PORT/tools/fix_bounds.py" tools/disasm/output/functions.json "$GF/map_cnames.json"
python -m tools.func_id "$GF/default.xbe" > /dev/null
python -m tools.abi_analysis "$GF/default.xbe" > /dev/null

EXTRA=()
[ -f "$PORT/src/manual_functions.json" ] && EXTRA+=(--manual-functions "$PORT/src/manual_functions.json")
python -m tools.recomp "$GF/default.xbe" --all --split 1000 \
    --game-name "Buffy the Vampire Slayer" \
    --gen-dir "$PORT/src/recomp/gen" "${EXTRA[@]}" 2>&1 | grep -E "functions \(|unresolved|unimplemented"
