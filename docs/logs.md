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
| `boot`, `game_start`, `exit` | main | `boot` is an anchor |
| `level name=<map>` | wrapper on `TManager_Buffy::Map_Loaded`, else the watcher | anchor |
| `level_left`, `level_done` | watcher / `Map_Completed` wrapper | |
| `menu screen=<id>` | wrapper on `GUI::TManager::SetMenu`, else the watcher (`s_menuID`) | ids, not names yet |
| `pos x= y= z= health=` | watcher, every 5 s (`BTVS_TRAIL_POS_SECS`) when changed | |
| `death`, `cutscene state=start/stop`, `autosave`, `save` | wrappers | |
| `setting section.key=value` | Menu (asked for in AREAS.md) | |
| `report`, `hang`, `replay_goto`, `replay_end` | Logs | |

**Anchors** (`boot`, `level`, `load`; `checkpoint` is reserved, the game has none: `ICheckpoint::Save` is an empty
function in the retail build, dying reloads the map) are where a replay can start. At each anchor Logs calls
Controls' `btvs_input_record_begin()`, so `btvs_input.rec` always holds "the input since the last anchor".

How the game is read (addresses in `btvs_trail.c`, section GAME HOOKS; found in `final.map` + disassembly):

- **wrappers on virtual functions**, through `recomp_lookup_manual` -> `btvs_trail_lookup()` (no regen needed):
  they run on the game thread at the exact moment. `TGame::ExecFrame` gives the once-a-frame tick that drives the
  replay. Each logs `game hook live: <name>` the first time it runs.
- **a watcher thread** reading the game's globals 10 times a second: level name
  (`[[0x7D0090]+0x40]+0x40`), menu id (`0x7F0408`), player position/health (`[[0x7D0090]+0xF0]` +0x240 / +0x90).
  It is the fall-back if a wrapper never fires (a function called directly, not through a vtable), and then the
  anchor is up to 0.1 s late.

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
| `save\` | the Xbox save drives (`XboxData`), 32 MB at most |
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

1. before anything reads them: `BTVS_SETTINGS_INI` -> the report's `settings.ini`, `BTVS_SAVE_DIR` -> a fresh copy of
   the report's `save\` in `<exe>\replay\save_run` (the player's own saves and settings are never touched);
2. for a `level` anchor, once the front end is up (a menu id is set, 2 s to settle), the game is sent there the
   way its own menus do it: `TGO::Map_QueueChange(name, 0)`. A `boot` anchor needs no jump;
3. when the trail reports the anchor, `btvs_input_playback(<report>\input.rec)` starts; when it ends the pads
   are the player's again and the log says how long it took against the original.

### How exact is it

Honestly: **not proven yet, and not exact by construction.**

- The jump puts the game in the right map with the report's save loaded by the game's own `Map_Loaded` ->
  `LoadGame`. Whether a queued map change from the front end sets up everything a normal "continue" does
  (session, difficulty, inventory) is not known until it is run.
- The input replay is frame-indexed. The game steps by measured frame time (`TGame::ExecFrame(float dt)`), so
  the same inputs on different frame timing drift: seconds of walking usually land close, a fight will not. To
  make it exact the replay must also feed the recorded `dt` per frame (asked of Controls: store the frame time;
  Logs can then force it in the `ExecFrame` wrapper) and the game's random seed must be pinned at the anchor.
  Neither is done.
- So today's promise is: right level, right save, right settings, the player's inputs re-applied
  approximately, plus the position trail (`pos` every 5 s) and screenshot to walk the last metres by hand.

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

State on 20 Nov 2026. The game does not boot yet (it faults about a second in, inside the translated Xbox
library start-up: Graphics' bring-up), so everything that needs a running level is **unverified**.

Verified in hidden test runs (`port/tools/run.sh`, build `d06ba57c3a` + this work):

- **Log**: header (version, commit, Windows 11 25H2, CPU, RAM, GPU + driver), previous run kept, lines on stderr
  in test runs. The player-run path (no stderr: toolkit output redirected into the log) was **not** run, since a
  visible run is not allowed in tests.
- **Forced crash** (`BTVS_TEST_CRASH=1`): `crashes\crash_*.txt` + a 166 KB `.dmp`; the fault shown as guest
  address `0xBAD00010`; guest registers; the two planted guest return addresses named from the 42,786 names
  loaded from the `.pdb` (`cvcolor_setstring+0x1C0`, `TString_SetLength+0x10`); host stack with file:line; trail
  tail; memory. The process then ends through the unhandled-exception filter. The `.dmp` was not opened in a
  debugger.
- **A real crash**: the game's own boot fault was caught and reported the same way
  (`buffy_tvs!SetLastError_00577605+0x90 (recomp_0039.c:29391)`), which is the evidence that game functions
  are named in real faults.
- **Report** (`BTVS_TEST_REPORT_AT=2`): folder + zip of 14 files in about 2.5 s; `report.json` parses; the zip
  passes Python's `zipfile.testzip()`; save copied (2 files); the newest crash reports included.
- **`report_open.py`** on that zip: summary, trail, symbolized crash, replay commands.
- **`BTVS_REPLAY=<zip>`**: unpacked, the throw-away save copy used as the save folder, the `boot` anchor
  reached, Controls' player started on the report's `input.rec` (it held 0 records: the game never read a pad).
- **Memory against Windows** (PowerShell `Get-Process` / `Get-Counter` / `Win32_OperatingSystem`, same second):
  working set 125.7 vs 125.4 MB; commit 135.5 vs 135.4 MB; RAM total 65,442 MB both; free 31,509 vs 32,007 MB;
  video memory in use by all programs 7,182 vs 7,246 MB (the counter Task Manager graphs); total 16,047 MB.
  `vram_used` (this game) read 0: no device existed. Task Manager itself was not opened (no windows in tests).
- **Game heap figures**: 1.01 MB in 14 blocks at the time of the report (the toolkit's heap). "peak" is that
  heap's high-water mark, which is its bump pointer (32 MB at boot: it starts with a large reservation), so
  treat peak as rough.

Not verified (needs a game that boots, or another area):

- every **game hook**: none of the wrappers (`ExecFrame`, `Map_Loaded`, `SetMenu`, `DieAsPlayer`, cutscenes,
  autosave) has fired, the watcher has never seen a level name, a menu id or the player. The addresses come
  from the map and disassembly only. Look for `game hook live:` lines in the log on the first run that reaches
  the menu.
- **the level jump** (`TGO::Map_QueueChange` called from the frame tick): never run. Unknowns: whether the
  front end accepts a queued map without a session being created first, and whether level changes go through
  `ShellRelaunch` (then the jump must pass launch parameters instead).
- **replay of real input**, and so the task's goal "a report made mid-level that `BTVS_REPLAY` takes back to
  the same place in under a minute": **not reached**.
- **hang report**: the code path ran only as far as starting its thread (the game dies before 20 s).
- **screenshot** in a report: Graphics' files are not in the build until its regen; reports say "no".
- **hotkey thread** (F12 / left stick): disabled in test runs by design, so never exercised.
- **settings in a report / replay**: no `btvs_settings.ini` existed in the test build folder.

Known gaps:

- Controls' recorder has no "write it now": a report closes the recording, takes the file and starts a new
  one, so a second report before the next anchor carries a partial recording (`input_complete=0`, which the
  replay refuses to play). Asked for `btvs_input_record_flush()` in AREAS.md.
- Menu ids in the trail are numbers.
- No privacy pass on logs (user name in paths) before any future upload.
- `btvs_symbols.txt` for builds without a `.pdb` is generated by `report_symbols.py` but nothing runs it.
- DXGI lists this PC's one RTX 4080 SUPER three times (three LUIDs); the header prints all three.

