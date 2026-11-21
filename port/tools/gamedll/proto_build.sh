#!/usr/bin/env bash
# PROTOTYPE (Extras): the translated game code as its own DLL, built with clang
# and lld-link and no Microsoft library; the port layer as an exe with no game
# code in it. See docs/install-build.md. Not part of the normal build.
#
#   bash port/tools/gamedll/proto_build.sh <work folder>
#
# Needs an up-to-date normal build (bash port/tools/build.sh) for the port
# layer's objects, and LLVM in C:\Program Files\LLVM. Everything it makes goes
# to <work folder>; <work folder>/run then holds buffy_tvs.exe + btvs_game.dll.
# Builds on CPUs 12-15 at low priority (tools/limit.sh), like build.sh.
set -euo pipefail
source "$(dirname "$0")/../env.sh"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$(mkdir -p "$1" && cd "$1" && pwd -W)"
LLVM="${BTVS_LLVM:-C:/Program Files/LLVM/bin}"
GEN="$BTVS_MAIN/port/src/recomp/gen"
[ -f "$GEN/recomp_funcs.h" ] || GEN="$PORT/src/recomp/gen"
VCB="$(ls -d "C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools/VC/Tools/MSVC/"*/ | tail -1)"
KIT="$(ls -d "C:/Program Files (x86)/Windows Kits/10/Lib/"*/ | head -1)"
OPT="${BTVS_GAMEDLL_OPT:--O1}"
export MSYS2_ARG_CONV_EXCL='*'
cd "$PORT"
mkdir -p "$OUT/gen" "$OUT/obj0" "$OUT/obj" "$OUT/run"
NM="$LLVM/llvm-nm.exe"

echo "1/6 what each side needs from the other"
cp -u "$GEN"/* "$OUT/gen/"
# a first compile, with the header as generated, only to read the symbols
if [ ! -f "$OUT/need_from_host.txt" ]; then
    ls "$OUT/gen"/*.c | bash tools/limit.sh xargs -P 4 -I{} sh -c \
        '"'"$LLVM"'/clang.exe" -c -O0 -w -I "'"$OUT"'/gen" -I src -I ../xboxrecomp/include "{}" -o "'"$OUT"'/obj0/$(basename "{}" .c).obj"'
    "$NM" --defined-only --extern-only "$OUT"/obj0/*.obj | awk 'NF==3 && $2=="T"{print $3}' | sort -u > "$OUT/gen_def.txt"
    "$NM" --undefined-only "$OUT"/obj0/*.obj | awk 'NF==2{print $2}' | sort -u > "$OUT/gen_und.txt"
    comm -23 "$OUT/gen_und.txt" "$OUT/gen_def.txt" > "$OUT/need_from_host.txt"
fi
HOSTLIBS=$(find build/xboxrecomp -path "*Release*" -name "*.lib" | grep -iv "glslang\|spirv")
"$NM" --undefined-only build/buffy_tvs.dir/Release/*.obj $HOSTLIBS 2>/dev/null | awk 'NF==2{print $2}' | sort -u > "$OUT/host_und.txt"
"$NM" --defined-only --extern-only build/buffy_tvs.dir/Release/*.obj $HOSTLIBS 2>/dev/null | awk 'NF==3{print $3}' | sort -u > "$OUT/host_def.txt"
comm -12 "$OUT/host_und.txt" "$OUT/gen_def.txt" > "$OUT/host_needs_gen.txt"
echo "    the port layer needs $(wc -l < "$OUT/host_needs_gen.txt") game functions; the game code needs $(wc -l < "$OUT/need_from_host.txt") names"

echo "2/6 import library for the DLL (from a list of names; the DLL need not exist)"
{ echo "LIBRARY btvs_game.dll"; echo EXPORTS; sed 's/^/    /' "$OUT/host_needs_gen.txt"; } > "$OUT/game.def"
"$LLVM/llvm-dlltool.exe" -m i386:x86-64 -d "$OUT/game.def" -l "$OUT/btvs_game.lib" -D btvs_game.dll

echo "3/6 the exe without game code (MSVC's linker, as on the developer's PC)"
CRT=" __dyn_tls_on_demand_init __tls_guard _dsign _fltused _tls_index atan2 calloc ceil cos exp2 floor fmod ldexp log2 memcpy memset memmove nearbyint sin sqrt sqrtf tan trunc truncf "
DATA=" g_xbox_mem_offset g_xbox_code_lo g_xbox_code_hi g_icall_count g_icall_trace g_icall_trace_idx "
{   echo EXPORTS
    while read -r n; do
        case "$CRT" in *" $n "*) continue;; esac
        case "$DATA" in *" $n "*) echo "    $n DATA"; continue;; esac
        case "$n" in g_*) continue;; esac                     # the registers: thread-local, read directly (step 4)
        if ! grep -qx "$n" "$OUT/host_def.txt" && grep -qx "btvs_ds_$n" "$OUT/host_def.txt"; then
            echo "    $n=btvs_ds_$n"                           # Audio's hooks get their game names by /alternatename
        else
            echo "    $n"
        fi
    done < "$OUT/need_from_host.txt"
} > "$OUT/exe.def"
LIBS=$(iconv -f UTF-16LE -t UTF-8 build/buffy_tvs.dir/Release/buffy_tvs.tlog/link.command.1.tlog | tr -d '\r' \
    | grep -o '"[^"]*\.LIB"\|[^ "]*\.LIB' | grep -iv "BTVS_RECOMP\|BUFFY_TVS.LIB" | tr -d '"' | tr '\134' '/' | sort -u | tr '\n' '|')
IFS='|' read -ra LA <<< "$LIBS"
( cd build && LIB="$VCB/lib/x64;$KIT/um/x64;$KIT/ucrt/x64" bash ../tools/limit.sh "$VCB/bin/Hostx64/x64/link.exe" \
    /OUT:"$OUT/run/buffy_tvs.exe" /NOLOGO /SUBSYSTEM:WINDOWS /DEBUG /PDB:"$OUT/run/buffy_tvs.pdb" /MAP:"$OUT/run/buffy_tvs.map" \
    /DEF:"$OUT/exe.def" /IMPLIB:"$OUT/buffy_tvs_exe.lib" /OPT:REF /OPT:ICF /INCREMENTAL:NO \
    buffy_tvs.dir/Release/*.obj "$OUT/btvs_game.lib" "${LA[@]}" )

echo "4/6 the header: registers read from the exe's thread-local block, the exe's globals imported"
python "$HERE/patch_header.py" "$OUT/run/buffy_tvs.map" "$OUT/gen"

echo "5/6 compile the translated code ($OPT, 4 at a time)"
t0=$(date +%s)
ls "$OUT/gen"/*.c | bash tools/limit.sh xargs -P 4 -I{} sh -c \
    '"'"$LLVM"'/clang.exe" -c '"$OPT"' -w -I "'"$OUT"'/gen" -I src -I ../xboxrecomp/include "{}" -o "'"$OUT"'/obj/$(basename "{}" .c).obj"'
"$LLVM/clang.exe" -c -O1 "$HERE/game_dll.c" -o "$OUT/obj/game_dll.obj"
echo "    $(( $(date +%s) - t0 )) s"

echo "6/6 link btvs_game.dll (lld-link, import libraries made here, no Microsoft library)"
printf 'LIBRARY ucrtbase.dll\nEXPORTS\n' > "$OUT/ucrt.def"
for n in _dsign atan2 calloc ceil cos exp2 floor fmod ldexp log2 memcpy memset memmove nearbyint sin sqrt sqrtf tan trunc truncf; do echo "    $n" >> "$OUT/ucrt.def"; done
printf 'LIBRARY KERNEL32.dll\nEXPORTS\n    GetModuleHandleW\n    ExitProcess\n' > "$OUT/k32.def"
"$LLVM/llvm-dlltool.exe" -m i386:x86-64 -d "$OUT/ucrt.def" -l "$OUT/ucrt_min.lib"
"$LLVM/llvm-dlltool.exe" -m i386:x86-64 -d "$OUT/k32.def" -l "$OUT/k32_min.lib"
bash tools/limit.sh "$LLVM/lld-link.exe" -dll -nodefaultlib -entry:DllMain -nologo -out:"$OUT/run/btvs_game.dll" \
    -def:"$OUT/game.def" -implib:"$OUT/btvs_game_real.lib" -debug -pdb:"$OUT/run/btvs_game.pdb" \
    "$OUT"/obj/*.obj "$OUT/buffy_tvs_exe.lib" "$OUT/ucrt_min.lib" "$OUT/k32_min.lib"
ls -la "$OUT/run/buffy_tvs.exe" "$OUT/run/btvs_game.dll"
