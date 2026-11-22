# Buffy the Vampire Slayer (Xbox, 2002) — Windows port

A static recompilation of the 2002 Xbox game (The Collective, "Slayer" engine; title ID 0x45410012,
XDK 4432) into a native Windows program. **No emulation**: the game's x86 code is translated to C by
`xboxrecomp/` and the Xbox libraries it links (D3D8, DSOUND, XAPI input) are replaced by host code.

This repository is **private** and holds **no game material**: no disc image, XBE, map file, game
data or translated game code. Those are generated locally from the owner's disc. (One exception, the
owner's on 22 Nov: pictures of the port running, for the README, in `docs/media/`.)

## Goals (the owner's list)

1. Graphics: the game's Direct3D 8 calls drawn natively (D3D11 through `xboxrecomp/src/gpu`), no NV2A emulation
2. Audio: XAudio2 output (DSOUND buffers/streams, WMA music, `.rpe` dialogue)
3. Controls: XInput and SDL controllers, keyboard, rebinding
4. 4K and other resolutions, widescreen
5. 60+ FPS
6. No emulation
7. Fullscreen / borderless windowed
8. Report a Problem (in-game, like SvR2011's; must record *exactly what the player did* so a
   developer can get back to the same spot very quickly)
9. In-game menus in the game's own style: Exit, Options → Graphics, etc. All settings are set in-game
10. Easy install: choose the xiso, choose where to install, play
11. Memory view: system RAM and VRAM (total / used / by this game), and the game's own heaps

## Layout

| Path | What |
|---|---|
| `Iso/` | the owner's disc image (ignored) |
| `game_files/` | the unpacked disc: `Default.xbe`, `final.map`, `pak/`, `media/`, `interfc/`, `dialogue/`, `Sound/` (ignored) |
| `xboxrecomp/` | the recompiler and Xbox runtime (vendored from the Chaos Bleeds port, MIT) |
| `port/src/` | the PC side. One file per area, prefix `btvs_` |
| `port/src/recomp/gen/` | the translated game code (generated, ignored) |
| `port/src/manual_functions.json` | game functions replaced by host code (name them here, implement in `port/src`, then regen) |
| `port/tools/` | regen, build, run, symbolize, disassemble |
| `port/runs/` | test-run output (ignored) |
| `docs/` | notes per area |

`final.map` is the linker map from the disc: every function and global in the game has its real
name (41,601 functions). Use it — `python port/tools/sym.py <addr>`, `python port/tools/xdis.py <name>`.

## References on this PC (read, don't modify)

- `D:\Xbox Games Ports\Buffy The Vampire Slayer Chaos Bleeds` — the owner's finished port of the
  2003 sequel, built on the same toolkit. A **different engine** (Eurocom's, not The Collective's),
  so no game address carries over, but its PC layer is the model for this one:
  `port/src/buffy_native.c` (D3D8 → native renderer), `buffy_dsound.c`, `buffy_input.c`,
  `buffy_menu.c` (PC options inside the game's own menus), `buffy_settings.c`, `buffy_debug.c`
  (bug reports), `buffy_frame.c` (frame pacing), `buffy_movie.c`, `port/launcher/` (installer).
  Its XDK is newer, so the Xbox library functions sit at other addresses and may differ in detail.
- `D:\Xbox Games Ports\WWE Smackdown Vs Raw 2011` — the model for **Report a Problem**:
  `port/docs/PROBLEM_REPORTS.md`, `docs/REPORTS_API.md`, `port/src/report_page.cpp`,
  `port/launcher/report*.c`, `docs/MATCH_RECORDINGS.md` (input recording and replay).

## Rules — these are the owner's, follow them exactly

**Builds and test runs must never disturb the owner's PC** (they game on it while this runs):

- **Builds**: CPUs 12–15 only, low priority. Always `bash port/tools/build.sh` (never bare `cmake --build`).
- **Test games**: CPUs 8–15 only, below-normal priority, **muted**, window **hidden or behind every
  other window, never activated, never taking focus**. Always `bash port/tools/run.sh <name> [seconds]`,
  which sets `BTVS_MUTE=1`, `BTVS_NO_WINDOW=1` and `BTVS_AFFINITY=FF00`. Every piece of port code
  that opens a window, plays sound or sets affinity must honour those variables.
- Never leave a test game running: every run has a timeout. Stop processes by PID only.
- At most 2 test games at once across all sessions.
- Look at the picture with frame dumps the game writes itself (`BTVS_SHOT_AT`, to `port/runs/`), not
  by showing the window. Drive it with scripted input (`BTVS_PAD_SCRIPT`), not real input.
- Test runs use `game_files/` as the game folder and never touch a player's saves.

**Git**: the remote is private; keep it private. Never commit game material (see `.gitignore`).
Commit small and often, on `main` unless working in a worktree. Commit messages say what changed for
the player or the developer, not how the session went.

**Parallel sessions** (Graphics, Audio, Controls, Menu, Extras, Logs): each owns its files
(`docs/AREAS.md`). Don't edit another area's files; if you need something from one, add it to
`docs/AREAS.md` under "Requests" and work around it. Shared files (`main.c`, `CMakeLists.txt`,
`manual_functions.json`, `btvs.h`) take small, additive edits only — pull/rebase before touching them.

**Style**: C11, MSVC. Environment variables and settings are prefixed `BTVS_`. Everything the port
does goes to the log (`btvs_log.txt` beside the exe) through the Logs area's API once it exists.

## Build and run

```bash
bash port/tools/regen.sh      # Default.xbe + final.map -> port/src/recomp/gen (minutes; only after manual_functions.json changes)
bash port/tools/build.sh      # configure + build Release, limited CPUs
bash port/tools/run.sh boot 30   # muted, hidden, 30 s; output in port/runs/boot.out / .err
```

Needs Visual Studio Build Tools (MSVC x64), CMake ≥ 3.20, Git Bash, Python 3.12 with `capstone`.
