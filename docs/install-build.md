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
  therefore reads the exe's thread-local block directly (`gs:[58h]`, slot 0) at the offsets the exe's
  link map gives (`port/tools/gamedll/patch_header.py` writes them as macros). The DLL checks at load
  that the exe's thread-local index is 0 and ends the process otherwise. The offsets belong to one
  exact exe, so they ship with it.
- **No Microsoft library or header is needed for the link.** The DLL imports `memcpy`, `memset`,
  `calloc` and the maths functions from `ucrtbase.dll` (part of Windows 10/11) through an import library
  made on the spot from a list of names (`llvm-dlltool`), and has its own entry point.

## Measured (this PC, CPUs 12-15 at low priority)

| | |
|---|---|
| Translated code | 45 files, 177 MB, 4.73 million lines |
| Compile, clang 23.1 `-O1`, 4 at a time | 227 s and 411 s in two passes (the PC was busier in the second) |
| Compile of one 3.3 MB file, one core | `-O0` 7 s, `-O1` 21 s, `-O2` 26 s |
| Link of `btvs_game.dll` | 1 s; 95 MB with nothing stripped |
| The same code with MSVC | about 60 minutes on 4 cores |

**Hidden run of the prototype** (exe + DLL, muted, CPUs 8-15, scripted Start, 120 s, game data from
`game_files/`): title 4.8 s, main menu 6.0 s, Start Game, difficulty, restart, `m00_spanishmission_1`
loaded at 24.2 s of the second process, positions logged in play, exit code 0.

**Speed, one uncontrolled comparison:** standing at the same spot in the first level the prototype
logged about 25 frames a second against about 36 for the MSVC build's run earlier the same day. Another
test game was running during the prototype's run and the two used different game folders, so this is
not a measurement; it is a reason to make one. Suspects if it holds: `-O1` against MSVC's `/O2`, and
the memory offset now being read through an import on every access.

## Not done, not known

- **Headers.** The compile still used this PC's MSVC and Windows SDK headers (`math.h`, `string.h`,
  `stdlib.h`, `setjmp.h`), which cannot be shipped. The code uses only a handful of declarations from
  them, so small headers of the port's own should do; untried.
- **A controlled speed comparison**, `-O2`, and keeping the memory offset inside the DLL.
- **The translate step on the player's PC**: the Python tools of `regen.sh` frozen into one exe; how long
  it takes; that its output is identical on another PC.
- **Toolchain size and delivery**: `clang.exe` is 110 MB and `lld-link.exe` 75 MB here, unpacked.
  In the release zip, or fetched by the launcher with a pinned SHA-256.
- **The launcher side**: progress, the install log, resuming a stopped build, the disc's SHA-256 check
  (the exe and its offsets fit exactly one `Default.xbe`).
- **Thread-local index 0** holds for the process exe in practice; the DLL refuses to load otherwise, but
  only this PC was tried.
- **Shaders**: asked of Graphics (`docs/AREAS.md`).
- `port/tools/gamedll/proto_build.sh` gathers the steps that were run by hand; **the script itself has
  not been run from start to finish.**
