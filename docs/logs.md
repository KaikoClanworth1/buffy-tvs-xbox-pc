# Logs, crash reports, Report a Problem, the trail, memory

Owner: the Logs session. Code: `port/src/btvs_log.c/.h`, `btvs_trail.c`, `btvs_mem.c`, `btvs_report.c`,
`btvs_report_crash.c`, `btvs_report_replay.c`; tools `port/tools/report_open.py`, `report_symbols.py`.
What other areas call is in `port/src/btvs.h`.

**What was and wasn't verified is at the end. Read it before trusting any of this.**

## Files beside the exe

| | |
|---|---|
| `btvs_log.txt`, `btvs_log_previous.txt` | the log of this run and of the run before |
| `btvs_trail.txt`, `btvs_trail_previous.txt` | what the player did (below) |
| `btvs_input.rec` | Controls' input recording since the last anchor |
| `crashes\crash_<date-time>.txt` + `.dmp`, `crashes\hang_<date-time>.txt` | crash and hang reports |
| `reports\<date-time>\` and `reports\<date-time>.zip` | Report a Problem |
| `replay\` | `BTVS_REPLAY` working files (an unpacked report, a throw-away copy of its save) |

`BTVS_LOG_DIR=<folder>` moves all of them (two test games at once from one build would otherwise share them).

## The log

`btvs_log("area", "fmt", ...)`: one line, `[seconds] [area] text`, from any thread, written unbuffered. It works
before `main` has set anything up. The header: port version, git commit (looked up at every build:
`port/cmake/btvs_build_info.cmake`; `+changes` = uncommitted edits), build time, Windows version, CPU, RAM, every
GPU with driver version and memory, the `BTVS_*` / `RECOMP_*` environment and the settings file.

A test run (`run.sh`: stderr is a file) also gets every line on stderr, so `port/runs/<name>.err` still has
everything in order with the toolkit's own output. A player's run has no stderr: the toolkit's `fprintf(stderr)` /
`printf` are pointed into `btvs_log.txt`.

## Crash and hang reports

`main.c`'s exception handler calls `btvs_crash_report()`; an unhandled-exception filter catches everything else.
A report has: what and where (host symbol; translated game functions are `Name_<guest address>`, so that *is* the
game function), the faulting address as a guest address, the guest registers of the faulting thread, the last 16
indirect-call targets, the return addresses on the guest stack (a scan from esp: calls that already returned can
linger), the host stack (dbghelp, with file:line for port code), host registers, the last 40 trail events, the
memory figures, and a minidump (`.dmp`: opens in Visual Studio / WinDbg with the build's `.pdb`).

The report is written by a helper thread while the faulting thread waits, so a stack overflow or a fault inside
the C library still produces one. First-chance faults that something later handles would flood the log: four full
reports a run, then one line each; the same address is reported once.

Guest names at run time come from the build's `.pdb` (dev builds; loaded on a low-priority thread at start, about
42,000 names) or from `btvs_symbols.txt` beside the exe (`report_symbols.py`). With neither, reports carry
addresses and `report_open.py` names them from the disassembly.

Hangs: no new frame (`xbox_HangMonitorBeat`) for `BTVS_HANG_SECS` (20; 0 = off) writes `crashes\hang_*.txt`
once per stall, three a run at most: the toolkit's "where is the game thread" report (guest stack, indirect calls,
every thread), the trail and memory. `BTVS_BOOT_HANG_SECS` (90): no first frame at all. The game is left running.
A long level load without frames would produce one too: it is only a file.

Test aids: `BTVS_TEST_CRASH=1` crashes on the game thread just before the game starts (the test puts two guest
return addresses on the guest stack so the report has something to name); `BTVS_TEST_CRASH=<seconds>` crashes a
helper thread later.

## The trail

`btvs_trail("event", "key=value ...")`: `seconds f=<frames> event key=value...`. Always on, a few lines a minute.

| event | from | |
|---|---|---|
| `boot version= run=<n> [launch=<command line>]`, `game_start`, `exit` | main | `boot` is the anchor; `launch=` when the game started this run itself (a level) |
| `level name=<map>` | the watcher (seen while the level is still loading) | |
| `level_loaded name=<map>` | wrapper on `TManager_Buffy::Map_Loaded` | the level is playable |
| `level_left`, `level_done` | watcher / `Map_Completed` wrapper | |
| `menu screen=<id> name=<name>` | wrapper on `GUI::TBuffyManager::SetMenu`, else the watcher (`s_menuID`) | 1 main, 4 options, 5 load game, 8 display, 9 pause, 10 title, 12 start game, 13 difficulty; Menu's own pages log `screen=pc_audio` etc. |
| `pos x= y= z= health=` | watcher, every 5 s (`BTVS_TRAIL_POS_SECS`) when changed | |
| `death`, `cutscene state=start/stop`, `autosave`, `save` | wrappers | |
| `setting section.key=value` | Menu | |
| `report`, `hang`, `replay_placed x= y= z= drift=`, `replay_end` | Logs | |

**The game restarts itself to load a level** (`btvs_boot_restart.c`: a new process with the game's launch
data). The log and the trail carry on in the same files across such a restart (times start again from 0; the
`boot` line says `run=2`...), and only a fresh start of the program moves them to `*_previous.txt`.

**The anchor** is `boot`: the start of a process is the one place that can be reached again exactly, and
because every level load is a restart, every level begins with one. (The game has no checkpoints:
`ICheckpoint::Save` is an empty function in the retail build, dying reloads the map.) At the anchor Logs calls
Controls' `btvs_input_record_begin()`, so `btvs_input.rec` always holds "the input since this run of the game
began" - also while a replay is playing, so a report made during a replay can be replayed too.

How the game is read (addresses in `btvs_trail.c`, section GAME HOOKS; found in `final.map` + disassembly):

- **wrappers on virtual functions**, through `recomp_lookup_manual` -> `btvs_trail_lookup()` (no regen needed):
  they run on the game thread at the exact moment. `TGameManagerImplementation::Frame` gives the once-a-frame
  tick (frame numbers, the hang monitor's beat, the replay's end). Each logs `game hook live: <name>` the first
  time it runs. Seen live: `SetMenu`, `Frame`, `Map_Loaded`, `Map_Completed`, `Event_CutsceneStart`.
- **a watcher thread** reading the game's globals 10 times a second: level name
  (`[[0x7D0090]+0x40]+0x40`), menu id (`0x7F0408`), player position/health (`[[0x7D0090]+0xF0]` +0x240 / +0x90).

## Report a Problem

`btvs_report_create(text)` (Menu's page), the hotkey (F12 or a left-stick click; `btvs_settings.ini`
`[report] hotkey` 0 off / 1 both / 2 F12 only; read by its own thread, only while the game's window is in
front, so it works when the game is frozen), or `BTVS_TEST_REPORT_AT=<seconds>` in a test run
(`BTVS_TEST_REPORT_TEXT`). `btvs_report_prepare()` takes the screenshot when the pause menu opens.

`reports\<yyyymmdd-hhmmss>\` and the same as a `.zip` (stored, no compression: nothing to link):

| | |
|---|---|
| `report.txt` | the player's words, version, where in the game, last 60 trail events, system, memory, what's in the report, where the game thread is |
| `report.json` | the same as data (below) |
| `replay.txt` | `key=value`: the anchor, level, times, which of input / settings / save are present |
| `screenshot.png` | Graphics' `btvs_screenshot` (absent until Graphics provides it) |
| `btvs_log.txt`, `btvs_log_previous.txt`, `btvs_trail.txt`, `btvs_trail_previous.txt` | |
| `input.rec` | the recording since the last anchor (absent until Controls provides it) |
| `settings.ini` | `btvs_settings.ini` |
| `save\` | the player's saves only (`UserData`, `TitleData`, `HardDisk\UDATA`, `HardDisk\TDATA` of the save folder, and the session file `Cache\*.ses`; never the game's files on its cache drive or the disk images), 32 MB at most. No report has carried a save yet (see Known gaps) |
| `launch.bin` | the launch data this run of the game was started with (a level); the replay starts the game with it |
| `crashes\` | the 3 newest crash reports, 2 newest hang reports, the newest minidump |
| `memory.txt` | |

Nothing is sent anywhere. `report.json` has the fields SvR2011's `POST /api/reports` takes (`format`, `app`,
`version`, `platform`, `os`, `problem` (first line, 200), `what` (4000), `before` (the trail tail, 4000),
`contact`, `signature`) plus `game`, `build`, `date`, `trigger`, `sent: false`, `where`, `replay`, `memory`,
`system`, `files`: an uploader would post it as the `report` part and the zip as `zip`. There is no privacy
pass yet: the log has the Windows user name in paths. Do that before anything uploads.

## Getting back there: `BTVS_REPLAY` and `report_open.py`

```bash
python port/tools/report_open.py <report folder or .zip>     # no argument: the dev build's newest report
```

prints the player's words, build, where, the last trail events, memory, the newest crash with function names,
and the commands to replay it (hidden test run, or on screen).

`BTVS_REPLAY=<folder or zip>` (`btvs_report_replay.c`):

1. before anything reads them: `BTVS_SETTINGS_INI` -> the report's `settings.ini`, `BTVS_SAVE_DIR` ->
   `<exe>\replay\save_run` with the report's `save\` copied in, **the saved game included**
   (`HardDisk\UDATA`); the player's own saves and settings are never touched;
2. a report made in a level has `launch.bin` (what the game handed itself when it restarted to load the
   level): `RECOMP_LAUNCH_DATA_FILE` is pointed at it and the game boots straight into the level, loading the
   report's saved game, with no menus;
3. **straight to the spot** (the default for a level report): while the replay is on its way the game is told
   the player wants cutscenes skipped (`ICinematicsManager::DoesUserWantToSkip`), and half a second after the
   level is playable the player is put on the report's exact position and facing, with the report's health,
   by the game's own `ICharacter::Teleport` (which also puts the camera behind her). Time: the level's
   loading plus about 2 s, however long the player had played;
4. `BTVS_REPLAY_INPUT=1` instead plays the report's `input.rec` from the start of the level first (as long
   as the player had played), then places the player the same way: for a fault that needs the player's
   actions to happen again. A report made in the menus has no spot and always replays its input;
5. 45 frames after arriving the replay writes `replay_arrived.png` beside the report's `screenshot.png`, to
   compare.

What a report stores for this (`replay.txt`): `launch`, `level`, `player_matrix` (the player's world matrix,
12 floats: rotation and position, read at the moment of the report), `camera_matrix` (stored, not used),
`player_health`, `input`, `save`, `settings`.

### How exact is it

- **Level, session, saved game**: the game's own launch data and the report's copy of the save. Exact.
- **Position, facing, health**: exact (set from the report's numbers through the game's own function).
- **Camera**: the game's "behind the player" reset, not the report's camera. In the test the two pictures
  show the same view; a player who had turned the camera will see it reset.
- **Everything else in the level** (enemies, doors, pickups, script progress): as the saved game and a fresh
  load of the level leave them, **not** as they were at the moment of the report. `BTVS_REPLAY_INPUT=1` gets
  closer (the same inputs act on the level again) but the game steps by measured frame time and nothing pins
  its random numbers: after 28 s and 20 s of replayed walking the player stood 5.1, 6.5 and 3.4 units from
  where she had stood (three replays; she is about 60 units tall), which the placement then corrects.
  A fight will not repeat.
- **Time** (hidden test runs on four shared CPU cores at below-normal priority): 2 s after the level is
  playable; 32.9 s from start in the quickest run. The level's loading was 31 to 81 s that night depending on what else the PC was doing (other
  sessions' builds and test games); on an idle PC at normal priority it has not been measured.

## Memory

`btvs_mem_stats(&m)` (cheap: a snapshot refreshed once a second on its own thread while somebody asks) and
`btvs_mem_stats_now()` (reports, crashes).

| field | source | Task Manager |
|---|---|---|
| `ram_total`, `ram_available` | `GlobalMemoryStatusEx` | Performance > Memory |
| `proc_working_set` | `GetProcessMemoryInfo` | Details: "Working set (memory)" |
| `proc_private_working_set` | same (`PROCESS_MEMORY_COUNTERS_EX2`, Windows 10 22H2+; else 0) | Processes "Memory" / Details "Memory (private working set)" |
| `proc_commit` | `PrivateUsage` | Details: "Commit size" |
| `vram_total` | adapter `DedicatedVideoMemory` | Performance > GPU: Dedicated GPU memory (total) |
| `vram_used_system` | counter `GPU Adapter Memory(luid...)\Dedicated Usage` | the same graph (in use) |
| `vram_used`, `vram_budget`, `vram_shared_used` | `IDXGIAdapter3::QueryVideoMemoryInfo` | Details: "Dedicated GPU memory" |
| `game_heap_used`, `game_heap_allocs`, `game_heap_peak`, `guest_ram_total` | below | - |

The adapter is the one Graphics names (`btvs_gpu_info`, matched by name), else the first hardware adapter.

The game's own heaps: its per-category counters (`TConsoleDebugDisplayMemory::Report`, "Megs alloc") are compiled
out of the retail build (its `_Free` is a bare jump to the C library's `free`), so there is nothing of the game's
to read except `g_memoryPeakAllocation` (0x837FC4). The figures are the Xbox memory manager's instead: the
toolkit's heap that stands in for the kernel's (`xbox_HeapStats`, added to `xbox_memory_layout.c`): bytes and
blocks the game holds of the console's 64 MB, and the high-water mark.

## What was verified, and what wasn't

State on 21 Nov 2026, 02:00. All runs are hidden test runs (`port/tools/run.sh`, each with its own empty save
folder), driven by `BTVS_PAD_SCRIPT` / `BTVS_PAD_SCRIPT_1`. Pictures: main checkout, `port/runs/shots/logs_*.png`.

Verified:

- **Report mid-level after walking** (new game, 20 s of scripted walking and a camera turn,
  `BTVS_TEST_REPORT_AFTER_LEVEL=28`): 20 files; `buffy 1.ses` and its `SaveMeta.xbx` in `save\HardDisk\UDATA`,
  `launch.bin`, `player_matrix`, the picture of the game (`logs_spot_report.png`).
- **Replay of it, straight to the spot**: saved game restored and loaded by the game, opening cutscene
  skipped by the hook, player placed 1.9 s after the level was playable, 346 units from where the level
  starts; trail position afterwards equals the report's (4155.74, -227.86, -41.16). The replay's own picture
  (`logs_spot_replay_direct.png`) shows the same place and view as the report's.
- **Replay of it with `BTVS_REPLAY_INPUT=1`** (before the direct mode existed, twice): input replay ended 5.1
  and 6.5 units off, then placed; picture `logs_spot_replay.png` matches the report's.
- **The in-game path**: pause menu -> Report a Problem (Menu's page) -> Send Report, mid-level after walking:
  report made on Menu's helper thread (`trigger=menu`), with save, launch data and spot;
  `report_open.py` on it; replay with input: the replay walks, pauses, opens the report page, presses Send
  (no second report is made: "this is the replay arriving at the report it replays"), then the player is
  placed (3.4 units of drift). Its direct replay: on the spot 32.9 s after start (level playable at 31.3 s);
  pictures `logs_pause_report.png` (the report's: the menu, see Known gaps) and `logs_pause_report_replay_direct.png`.
- **Clean save folder per run** (`run.sh`): `runs/<name>.save`, emptied at the start; Start Game works again
  and names its session `Buffy_1`. The game's cache copy and the disk images are deleted from it after the run.
- **Trail** in menus and level, **hang report** (two real ones), **menu report and its replay**: as before.

Verified earlier, not re-run: log header and previous-run file; forced crash report and minidump with guest
names; memory figures against Windows (PowerShell; Task Manager itself not opened).

Not verified:

- the **hotkey** (F12 / left stick: off in test runs by design) and the player-run log path (no stderr).
- **death, autosave, level completed, level-to-level restart, quit to menu**: hooks in place, never fired.
- a replay of a report made **later in a level** (after doors, fights, pickups) or in another level; a
  report made while a cutscene plays; `Teleport` onto a spot that needs level state the fresh load lacks.
- a report **from a loaded game** (Load Game) with its save in the report: only new games were used.
- menu ids other than those listed are numbers in the trail.

Known gaps:

- **The picture in a pause-menu report is the menu**, not the game: the screenshot is the next frame shown.
  Logs asks for the game's picture when the game asks for its pause menu through `TBuffyManager::SetMenu`,
  but the pause menu does not come that way (the hook never fired for it). Needs Graphics' "last frame
  shown" (Menu has asked). The replay's `replay_arrived.png` shows the game.
- Level state other than the player is not restored (see "How exact is it").
- Controls writes the recording to disk once a second: a report can miss the last second of input.
- A load that keeps the game loop away for more than 20 s after the first frame would be reported as a hang.
- Two test games at once: until every session's build has the shared-read fix (`kernel_file.c`, this
  round), a boot can still end on the game's disk-error screen when another game holds
  `Sound\dsstdfx.bin`.
- The game heap "peak" is rough. No privacy pass on logs before any future upload. `btvs_symbols.txt` is
  generated by `report_symbols.py` but nothing runs it. DXGI lists this PC's one GPU three times.
