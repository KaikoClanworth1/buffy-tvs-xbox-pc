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
| `report`, `hang`, `replay_end` | Logs | |

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
   `<exe>\replay\save_run` with the report's `save\` copied in (the player's own saves and settings are never
   touched; the game's cache drive in there is kept between replays);
2. if the report has `launch.bin`, `RECOMP_LAUNCH_DATA_FILE` is pointed at it: the game boots straight into
   the level, exactly as it did when it restarted itself (no menus);
3. at `boot`, `btvs_input_playback(<report>\input.rec)` starts; when it ends the pads are the player's again
   and the log says how long it took against the original.

### How exact is it

- **Where it starts is exact**: same level, same session, by the game's own launch data.
- **The input stream is exact**: Controls plays it by pad-read count, so loading time does not shift it.
- **What the game does with it is not guaranteed**: the game steps by measured frame time and nothing pins its
  random numbers, so a walk should land close and a fight will not repeat. Measured so far: one replay of a
  report made 52 s into the first level ended at the same position to within 0.2 units - but the player had
  not moved by input in that recording (the level's opening cutscene was running), so this proves the start,
  not the drift. A replay of recorded walking has **not** been compared yet.
- **Time**: the level's own loading dominates: 47 s, 60 s and 58 s in three replays of that report on four
  CPU cores at below-normal priority (the load alone was 33 s and 46 s).
- **Saves are not restored** today (Known gaps), so a report made from a loaded game replays only on a PC
  that has the same save.

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

State on 21 Nov 2026. The game boots to its menus and loads the first level. All runs are hidden test runs
(`port/tools/run.sh`), driven by `BTVS_PAD_SCRIPT`.

Verified:

- **Trail in the menus**: `boot`, `game_start`, one `menu` line per screen as it is asked for (10 title, 1 main,
  4 options, 8 display, 5 load game, 12 start game, 13 difficulty, 9 pause), Menu's own pages, frame numbers.
- **Trail into a level**: the restart (`boot ... run=2 launch=root:Z:\ ... session:Buffy_10 sessionload:1`),
  `level name=m00_spanishmission_1`, `level_loaded`, `cutscene state=start/stop`, `pos`/`health` every few
  seconds, `menu screen=9 name=pause`. Log and trail continue across the restart.
- **Report at a menu** (`BTVS_TEST_REPORT_AT=25`, in Options): folder + zip of 11 files in 2.4 s: screenshot
  (shows the Options screen), log, trail, `settings.ini`, `input.rec` (the script's Start, Down, Down, A),
  memory, `report.json` (parses), `replay.txt`; zip passes `zipfile.testzip()`.
- **Replay of it**: a fresh run was on the same Options screen 11.7 s after start (screenshot and frame dump
  show the same screen and highlighted item).
- **Report mid-level** (`BTVS_TEST_REPORT_AT=52 BTVS_TEST_REPORT_RUN=1`, 52 s into the level's run, after
  Load Game): 13 files incl. `launch.bin`, level name, position and health in `replay.txt`.
- **Replay of it**: a fresh run booted straight into the level (no menus), played the recording and handed
  the pads back 47 s after start (60 s and 58 s in two more, the PC busier), at position 4232.0, -240.0, 296.0
  against the report's 4232.2, -240.0, 296.2. See "How exact is it" for what this does not prove.
- **Hang report**, twice, both real: the main menu's attract movie (frames stopped for 20 s inside
  `TGO_AttractMode_Play` -> `Movie_Play`; a playing movie is no longer counted as a hang, that fix is not
  re-tested), and a boot stuck on the game's own disk-error screen (`TLogo_DiskErrorCheck` ->
  `G_ErrorToUser`). Both name the game functions on the stack.
- **`report_open.py`** on a menu report zip: summary, trail, memory, game-thread stack, replay commands. Also run on the level report (level, position, replay commands).

Verified earlier (before the game booted; not re-run since):

- **Log** header and previous-run file. The player-run path (no stderr: toolkit output redirected into the
  log) was **not** run, since a visible run is not allowed in tests.
- **Forced crash** (`BTVS_TEST_CRASH=1`): report + `.dmp`, guest address, guest registers, planted guest return
  addresses named from the `.pdb`, host stack with file:line. The `.dmp` was not opened in a debugger.
- **Memory against Windows** (PowerShell, same second): working set 125.7 vs 125.4 MB; commit 135.5 vs
  135.4 MB; RAM total equal; video memory in use by all programs 7,182 vs 7,246 MB. Task Manager itself was not
  opened. The game's own video memory figure (24 MB in the menus) has not been compared with anything.

Not verified:

- **replay of recorded movement in a level** (so: how far a replayed walk drifts). The pad script starts again
  in every run of the game and its parts wait for marks that a level run never reaches, so my scripted runs
  could not move the player after the cutscene; asked of Controls in AREAS.md.
- **death, autosave, level completed, level-to-level restart, quit to menu**: hooks in place, never fired.
- **Report a Problem chosen in the game's menu** (Menu's item calling `btvs_report_create`), and the
  **hotkey thread** (off in test runs by design).
- **a save in a report / restored by a replay**: see Known gaps. The `HardDisk` copy code has not run on a real save.
- menu ids other than those listed are numbers in the trail (15 resurrect is from the disassembly, unseen).

Known gaps:

- **Saves in reports: the folders travel, a real saved game has not.** Since `09e1647` the console's hard
  disk (saved games in `UDATA`, settings in `TDATA`) is `<save folder>\HardDisk`; reports copy
  `HardDisk\UDATA` and `HardDisk\TDATA` and a replay puts them back. Seen: a menu report carried the 4 files
  the game had written there (`vars.cfg`, the title's save-list images). Not seen: a saved game in a report,
  or one restored by a replay. The level report above was made from "Load Game" on `buffy 10.ses`, an old
  test save that exists only in the shared `game_files\UDATA` (the game still finds saves there), so that
  report holds no save and replays only on a PC that has that file. Reported in AREAS.md.
- Controls writes the recording to disk once a second: a report can miss the last second of input.
- Frames are counted at the game loop's frame call: a load that keeps the loop away for more than 20 s
  *after* the first frame would be reported as a hang (the first level's 30 s load is before the first frame,
  where the limit is 90 s).
- The game heap "peak" is the toolkit heap's high-water mark (32 MB at boot): rough.
- No privacy pass on logs (user name in paths) before any future upload.
- `btvs_symbols.txt` for builds without a `.pdb` is generated by `report_symbols.py` but nothing runs it.
- DXGI lists this PC's one RTX 4080 SUPER three times (three LUIDs); the header prints all three.
