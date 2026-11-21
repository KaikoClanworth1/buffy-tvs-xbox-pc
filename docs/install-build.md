# Building the game on the player's PC (Extras) — prototype, 21 Nov 2026

The owner's requirement: a release ships no translated game code. After the player picks their disc
image the launcher unpacks it, translates `Default.xbe` to C, compiles it, prepares the shaders, and
then Play works. **None of that is in the launcher yet.** This page records what a prototype showed and
what is still open. The prebuilt `GAME\` from `deploy.sh` is unchanged and stays the owner's test path.

## What the finished ports do

Neither Chaos Bleeds nor SvR 2011 compiles anything on the player's PC: both releases ship the game exe
with the translated code inside, and a prebuilt shader cache. There is no model to copy for this. The
one reusable piece is SvR's plan for downloads (`docs/FIRST_TIME_SETUP_PLAN.md` there): pinned version,
URL, size and SHA-256, licence shown, the player agrees first.

## The shape that was prototyped

- `buffy_tvs.exe`: the port layer and the Xbox runtime, **no game code** (3.1 MB instead of 70 MB).
  Built by the developer with MSVC as today, from the same objects, with no source change.
- `btvs_game.dll`: the translated code only, compiled with **clang** and linked with **lld-link** on the
  player's PC. Both are Apache-2.0 with the LLVM exception and may be shipped.
- The two refer to each other through ordinary DLL imports:
  - the port layer needs 81 game functions from the DLL (the hooks' `_orig` functions and a few more);
  - the game code needs about 110 functions from the exe (the 90 hooks in `manual_functions.json`, the
    runtime's lookup and log functions) and 6 globals (memory offset, code range, call trace).
- **Registers.** The translated code keeps the x86 registers in thread-local variables that the port
  layer also uses, and Windows cannot import thread-local variables from another module. The DLL
  therefore reads the exe's thread-local block directly (slot 0 of `gs:[58h]`) at the offsets the exe's
  link map gives (`port/tools/gamedll/patch_header.py` writes them as macros). The block's address comes
  from a small function of the DLL marked `const`, so the compiler asks once per function. (Reading
  `gs:[58h]` in place made it read again at every use of a register: a third more work per frame.)
  The DLL checks at load that the exe's thread-local index is 0 and ends the process otherwise. The
  offsets belong to one exact exe, so they ship with it.
- **Memory offset.** The DLL keeps its own copy of the offset the translated code adds to every
  address, taken when the port layer calls `recomp_dispatch_init` (after the memory is mapped, before
  any game code runs). No change to the port layer was needed.
- **No Microsoft header or library.** The compile uses `-nostdlibinc`: the compiler's own headers and
  five small ones of the port's (`port/tools/gamedll/include`: `math.h`, `string.h`, `stdlib.h`,
  `setjmp.h`, `malloc.h`, declarations only). The DLL imports `memcpy`, `memset`, `calloc` and the
  maths functions from `ucrtbase.dll` (part of Windows 10/11) through an import library made on the
  spot from a list of names (`llvm-dlltool`), and has its own entry point.

## Measured (this PC, CPUs 12-15 at low priority)

| | |
|---|---|
| Translated code | 45 files, 177 MB, 4.73 million lines |
| Compile, clang 23.1 `-O2`, 4 at a time | 179 s |
| Link of `btvs_game.dll` | a few seconds; 48 MB |
| `proto_build.sh` from start to finish (symbol pass, exe link, compile, DLL link) | about 6 minutes |
| The same code with MSVC | about 60 minutes on 4 cores |

## Speed: the clang DLL against the MSVC build

`port/tools/gamedll/speed_run.sh` and `speed_report.py`. Each run: hidden, muted, CPUs 8-15, game data
from `game_files/`, an empty save folder, Controls' scripts into the first level and the walk there,
100 s. Runs alternate between the builds; the PC was in use. The figure is the game thread's own work
per frame (processor cycles, so waiting for the frame limit or for another program does not count),
median over the frames from 30 s to 75 s of the level's run, then the median of four runs.

| Build | Work per frame | Each run |
|---|---|---|
| MSVC (the normal build, same port-layer objects) | 11.5 ms | 9.0, 12.2, 12.6, 10.9 |
| clang `-O2` DLL | 10.4 ms | 10.1, 10.7, 8.3, 10.8 |
| clang `-O2` DLL **before** the registers fix (earlier round) | 16.1 ms | 16.4, 15.4, 17.5, 15.8 |
| MSVC in that earlier round | 11.7 ms | 12.1, 12.0, 10.5, 11.5 |

The clang DLL is not slower than the MSVC build; the runs spread by about 2 ms on a busy PC, so "about
the same, possibly a little faster" is all these numbers say. Every run reached the level and ended
with exit code 0, and all held the 60 frames a second limit (median frame 16.7 ms).

## Not done, not known

- **The translate step on the player's PC**: the Python tools of `regen.sh` frozen into one exe; how long
  it takes; that its output is identical on another PC.
- **Toolchain size and delivery**: `clang.exe` is 110 MB and `lld-link.exe` 75 MB here, unpacked.
  In the release zip, or fetched by the launcher with a pinned SHA-256.
- **The launcher side**: progress, the install log, resuming a stopped build, the disc's SHA-256 check
  (the exe and its offsets fit exactly one `Default.xbe`), calling `buffy_tvs.exe --prepare-shaders`.
- **Thread-local index 0** holds for the process exe in practice; the DLL refuses to load otherwise, but
  only this PC was tried.
- The symbol pass (step 1 of `proto_build.sh`) and the exe link are the developer's; a release would
  ship their results (the two name lists, the exe, the register offsets) and run only steps 5 and 6.
