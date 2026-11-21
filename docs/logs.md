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
| `btvs_frames.rec` | the frame recording since the level came up (below: "The frame recording") |
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
4. `BTVS_REPLAY_INPUT=1` instead gives the game the report's **frame recording** (`frames.rec`) back, frame
   for frame, from the moment the level is up: the same things happen again, the same every time (as long
   as the player had played). For a fault that needs the player's actions to happen again. A report without
   one (made before 21 Nov, or with `BTVS_EXACT=0`) plays `input.rec` by time and then places the player. A
   report made in the menus has no spot and always replays its input by time;
5. 45 frames after arriving the replay writes `replay_arrived.png` beside the report's `screenshot.png`, to
   compare.

What a report stores for this (`replay.txt`): `launch`, `level`, `player_matrix` (the player's world matrix,
12 floats: rotation and position, read at the moment of the report), `camera_matrix` (stored, not used),
`player_health`, `input`, `save`, `settings`, `frames` and `exact_frames` (the frame recording and how many of
its frames to give back), `exact_pos` (where the player stood after the last of them).

### The frame recording (`btvs_report_exact.c`)

Why a replayed walk used to end somewhere else (3 to 130 units off, measured by Logs and by Controls):

| What differed between two runs | Where in the game | What is done now |
|---|---|---|
| **The clock.** The game times itself with the CPU's cycle counter (`TTimerManager::GetTimeStamp` is a bare `rdtsc`): the frame's step (`TGameManagerImplementation::Frame`), and inside a frame how long a button was held (`TCommandManager::UpdateCommandState`), how long collision may look for a free place (`TGO::Collision_PositionObjectInValidSpace`), the object and particle ticks | every read gave another value in another run | from the first frame after the level has loaded the game thread reads a clock of its own: the real counter taken **once at the start of each frame**, plus one tick per read. The step of every frame is recorded and given back |
| **The pad.** A 16 ms timer thread samples the pads, and `TIF_Input::Update` adds one sample per millisecond of the frame: how many samples a frame gets, and which of them a press lands in, was never the same | `TIF_Input::Poll`, `TInputDeviceController::Poll` | the samples the game used in each frame are recorded (only what changed) and given back in the same frames; the timer's own sampling is held off during a replay |
| **Random numbers.** `rand()`, per thread, never seeded | wherever the level starts drawing | the state when the level comes up is recorded and set (it was the same in every run anyway); the state after **every frame** is recorded and put back when it differs (see below) |
| **Load time.** How long the level takes to load | before the level is up | nothing of the recording depends on it: it starts at the first frame after `Map_Loaded` |

The toolkit gained one small thing for this: a thread can have its own cycle counter
(`xbox_SetThreadTimeStampSource`, `kernel_hal.c`); every other thread still reads the real one.

**This is on in normal play too** (the recording must be made the same way it is played back): inside one
frame the game's clock now moves by one tick per read instead of by real time. The frame step is the real
one, so the game's speed is unchanged. `BTVS_EXACT=0` turns all of it off. `BTVS_EXACT_CHECK=1` records the
player's position every frame instead of every 8th. Size: 13.6 bytes a frame in the test (27.8 KB for 2,050
frames of walking, about 3 MB an hour).

**The picture before the pause menu.** While the game is paused its camera is not in the level
(`IPauseScreen::SetOpen` moves it to the menu's own scene), so a picture taken with the pause menu up can
only show the menu. A Start press made in play is therefore held back for one frame in the pad samples; that
frame, still play, is kept, and a report made while the game stays paused gets it as `screenshot.png`. The
pause opens one frame (1/60 s) later than pressed. The frames given back by a replay of such a report end
with the last frame before the pause.

### How exact is it

Straight to the spot (the default):

- **Level, session, saved game**: the game's own launch data and the report's copy of the save. Exact.
- **Position, facing, health**: exact (set from the report's numbers through the game's own function).
- **Camera**: the game's "behind the player" reset, not the report's camera.
- **Everything else in the level** (enemies, doors, pickups, script progress): as the saved game and a fresh
  load of the level leave them, **not** as they were at the moment of the report.
- **Time**: 2 s after the level is playable (32.9 s from start in the quickest run of 21 Nov, 02:00).

With the frame recording (`BTVS_REPLAY_INPUT=1`), measured 21 Nov 10:30 in hidden test runs:

- **The player's path is reproduced bit for bit.** One report (36 s in the level: the opening cutscene
  skipped, then the walk of `level_walk.txt`, 2,050 frames) replayed three times: at each of 257 position
  checks the player was 0.000000 units from the recorded place, and all three ended at
  4213.267090 -240.000000 187.313004, the position the recording ended at. A second report (walk, pause,
  report sent from the pause menu; 1,261 frames) replayed once: the same, and its `replay_arrived.png` is the
  same picture as the report's `screenshot.png`, camera and on-screen tip included
  (`logs_r4_pause_report.png`, `logs_r4_pause_replay.png`). No placing was needed in any of them.
- **Random numbers are NOT reproduced by themselves.** In two of the three replays the random state after a
  frame differed from the recorded one from frame 1116 on (22 s in), in 143 of the 2,050 frames; in the
  third and in the pause report's replay never; in an earlier build's replays from frame 190 or 1101.
  Something that is not on the game thread's clock decides in which frame some effect draws its numbers (not
  found: sound and streaming are the suspects). The replay puts the recorded state back after every such
  frame, so a difference lasts one frame; the player's path was not touched by it in these runs.
- **So what is and is not reproducible:** walking, jumping, the camera, the pause, cutscenes skipped by the
  player: yes, exactly, every time. Anything decided by a random number drawn in a frame where the count
  differed (which enemy move, which effect) may differ for that frame: **a fight has not been tried and is
  not promised.** Sound-driven events (a line of speech ending) are on the real clock, not the recorded one.
- **Time**: as long as the player had played since the level came up, plus the level's loading (the three
  replays: on the spot 43.6 to 46.4 s after start, for 36 s of play).
- **Limits.** The recording starts when a level is up and covers that run of the game: a level reached from
  another level starts a new one. It is not made in the menus. A report made during a replay has none.

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

**Round of 21 Nov, 10:30** (frame recording, pause picture; hidden test runs, pictures
`port/runs/shots/logs_r4_*.png` in the main checkout):

- verified: the numbers under "How exact is it" (one walk report replayed three times, one pause-menu report
  replayed once, all on the recorded place at every check and at the end); the pause-menu report's picture is
  the game (`logs_r4_pause_report.png`); `report_open.py` on it; reports made by a replay's own play are not
  made (`exact_frames` stops before the pause, so the replay never reaches Send);
- fixed on the way: reports never carried the running session file (`Cache\*.ses`): the path was written
  with a lost backslash in `btvs_report.c` and `btvs_report_replay.c`;
- not verified: a fight or anything else with enemies; a recording longer than 36 s; a level reached from
  another level; `BTVS_EXACT=0`; a player's real pad (only scripted pads); what a frame's clock moving one
  tick per read does to the few places that give collision or ticks a time budget (no fault seen in these
  runs, 60 fps kept).

State on 21 Nov 2026, 02:00 (the round before). All runs are hidden test runs (`port/tools/run.sh`, each with its own empty save
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

- A report made by the hotkey or a test trigger **while paused, after a pause that began before the level was
  up or with `Start` held from a menu**, still shows the pause menu (no kept picture). Not seen in practice.
- Random numbers per frame are put back, not reproduced (see "How exact is it").
- Level state other than the player is not restored (see "How exact is it").
- Controls writes the recording to disk once a second: a report can miss the last second of input.
- A load that keeps the game loop away for more than 20 s after the first frame would be reported as a hang.
- Two test games at once: until every session's build has the shared-read fix (`kernel_file.c`, this
  round), a boot can still end on the game's disk-error screen when another game holds
  `Sound\dsstdfx.bin`.
- The game heap "peak" is rough. No privacy pass on logs before any future upload. `btvs_symbols.txt` is
  generated by `report_symbols.py` but nothing runs it. DXGI lists this PC's one GPU three times.
