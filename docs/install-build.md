# Building the game on the player's PC (Extras), 21 Nov 2026

The owner's requirement: a release ships no translated game code. After the player picks their disc
image the launcher unpacks it, translates `Default.xbe` to C, compiles it, prepares the shaders, and
then Play works.

**Where it stands:** the launcher does all of that, and it was run hidden on this PC from the disc image
to a game that reaches the first level (below). **One piece is missing for a real release:** the
translate program as an exe (it is Python; freezing it needs PyInstaller, a download the owner has to
allow), and LLVM's licence text has to be put in the kit. `package.sh` refuses to make a release without
either. The prebuilt `GAME\` from `deploy.sh` is unchanged and stays the owner's test path.

## How a release is laid out and what the launcher does

```
Buffy Launcher.exe
buffy_tvs.exe  (+ .pdb)       the port's own code, NO game code (3 MB); loads btvs_game.dll
SDL3.dll, vcruntime140*.dll, msvcp140.dll, Read Me.txt
build\                        the kit, used once at install (193 MB unpacked)
    kit.ini                   names this exact kit (exe hash + compiler version)
    btvs_translate.exe        Default.xbe + final.map -> C      (tools/gamedll/translate.py, frozen)
    llvm\bin\clang.exe, lld-link.exe;  llvm\lib\clang\23\include;  llvm\LICENSE.TXT
    include\                  the port's five small C headers
    game_dll.c, btvs_game_tls.h, game_link.def, manual_functions.json
    buffy_tvs_exe.lib, ucrt_min.lib, k32_min.lib      import libraries made from lists of names
```

Install (`port/launcher/btvs_build.inc`, called from the launcher's install after the disc is copied
and its `Default.xbe` checked against the release's SHA-256):

1. **Translate** (`build_work\gen` in the game folder): progress per step from the program's
   `STEP n/9` lines. About 5 minutes on one core here.
2. **Compile** every file with clang `-O2`, as many at once as the PC has processors (and 1.5 GB of
   memory each), biggest first, below normal priority. An object is written under another name and
   renamed when whole, so a file is either done or not.
3. **Link** `btvs_game.dll` (+ `.pdb`) into the game folder.
4. **Shaders**: `buffy_tvs.exe --prepare-shaders` (Graphics). Not fatal if it fails: the game makes
   them at its start.
5. `btvs_game.ok` records the kit's Id; the work folder is removed.

- **Progress**: the launcher's own bar and line: the disc copy has the first 40 %, translating 10 %,
  compiling 46 % (by size of the files done), then linking and shaders.
- **Install log**: `install_log.txt` in the game folder: every command line, every step, each file
  compiled, a failing compiler's first lines, the shader step's output.
- **Resume**: a stopped or failed install is carried on by installing again: copied disc files,
  a finished translation and compiled files are kept. A work folder from another kit is thrown away.
- **Messages** name the step and the file, say where the details are, and say that installing again
  carries on. Missing kit files say "the release is incomplete, unpack it again".
- **Play** refuses with a plain message while the game's code is not built (or was built by an older
  release), instead of Windows' "DLL not found" box. A release's exe is told from the developer's
  prebuilt one by its import of `btvs_game.dll`.
- **Space**: about 0.9 GB free is asked for in the game folder while it builds; about 50 MB stays.
- Tests on this PC: `BTVS_BUILD_AFFINITY=F000` (idle priority), `BTVS_BUILD_JOBS=4`,
  `BTVS_TRANSLATE_CMD` (the Python program instead of the exe), `BTVS_BUILD_STOP_AFTER=<n>`.

### Checked on this PC (hidden, compilers on CPUs 12-15 at idle priority, 4 at a time, build lock held)

- `"Buffy Launcher.exe" --install <the disc image> D:\_btvs_install_test`, stopped on purpose after 6
  compiled files: exit 1, "Cancelled while building. Install again to carry on: what is built is kept."
  Translate took 5 min 5 s of that run.
- The same command again: translate skipped ("already done"), the 40 files left compiled in 2 min 16 s,
  link, shaders (13 of 13, exit 0), "installed", exit 0, 140 s in all. Work folder gone;
  `btvs_game.dll` 48 MB.
- The installed game through its launcher's Play path, hidden and muted: main menu at 2.7 s,
  `m00_spanishmission_1` loaded 9.4 s into the second process, exit code 0.
- **Not checked**: the launcher's window during a build (only the console form ran); a PC without Visual
  Studio or LLVM installed; a failing compile; all processors at once (4 were used here; a guess for an
  8-core PC is 3 to 4 minutes of compiling plus the 5 of translating).

## The translate step

`port/tools/gamedll/translate.py` runs `regen.sh`'s steps in one process with every in-between file
under its work folder. Its output was compared with `port/src/recomp/gen` (made by `regen.sh`): **all 48
files identical**, and identical again with another `PYTHONHASHSEED`. It is single-threaded: 5 min 14 s
on one of CPUs 12-15.

## Toolchain delivery: in the zip

Recommended and built that way. `clang.exe` (110 MB) and `lld-link.exe` (75 MB) need nothing but
Windows' own DLLs, and LLVM's licence (Apache-2.0 with the LLVM exception) allows shipping them with
its text. Fetching instead would mean LLVM's whole Windows installer (several hundred MB, to be taken
apart on the player's PC) since the two programs are not offered alone, plus a consent step and a
failure mode with no network. In the zip the cost is the download: the whole release zip is 78 MB (219 MB unpacked), and
nothing can go wrong at install for lack of a network.

## `package.sh`

Makes the exe without game code and the kit (`proto_build.sh` with `BTVS_GAMEDLL_KIT`), zips, and checks
the zip: only the port's files and the kit; nothing with a disc file's name, type or content; **no
translated game code in any form**: an exe over 16 MB or one that does not import `btvs_game.dll`, any
`recomp_*` file, any `.c` but the kit's `game_dll.c`, any object file, `btvs_game.dll`. The check was tried on a zip holding today's 66 MB exe, a `recomp_0001.c`, an object file, a
`btvs_game.dll` and a stray `.c`: each was named and refused. It also refuses
when the translate program or LLVM's licence is missing, unless `--allow-incomplete`, which marks the
package "NOT A RELEASE".

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

- **`btvs_translate.exe`**: freezing `translate.py` (PyInstaller) and proving the frozen program's output
  identical too. Waits for the owner's yes to the download.
- **LLVM's licence text** in the kit: LLVM's Windows installer does not leave a copy on disk; it has to
  be fetched once from llvm.org (`port/build/gamedll/LLVM-LICENSE.TXT`).
- **Crash reports** name game functions from `buffy_tvs.pdb`; with the code in the DLL they are in
  `btvs_game.pdb` (asked of Logs in `docs/AREAS.md`).
- **Thread-local index 0** holds for the process exe in practice; the DLL refuses to load otherwise, but
  only this PC was tried.
- The launcher's window was not looked at during a build, and no second PC has run any of this.
