# Areas and who owns what

Six sessions work in parallel. Each owns the files listed; nobody else edits them.
Shared files (`port/src/main.c`, `port/CMakeLists.txt`, `port/src/manual_functions.json`,
`port/src/btvs.h`, `CLAUDE.md`) take small additive edits only.

| Area | Owns | Delivers |
|---|---|---|
| **Graphics** | `port/src/btvs_native*.c`, `btvs_frame.c`, `btvs_window.c`, `btvs_movie.c`, `docs/graphics.md` | Boot to first frame; the game's D3D8 (XDK 4432) drawn through `xboxrecomp/src/gpu` (D3D11) with no NV2A emulation; window: windowed / borderless / fullscreen; resolutions to 4K; widescreen; 60+ FPS pacing; VSync; frame dumps for tests (`BTVS_SHOT_AT`); WMV logo movies |
| **Audio** | `port/src/btvs_audio*.c`, `btvs_wma.c`, `docs/audio.md` | DSOUND buffers, streams and 3D sound on **XAudio2**; WMA music (`Sound/Music`), `.rpe` dialogue; volume settings; `BTVS_MUTE` |
| **Controls** | `port/src/btvs_input*.c`, `docs/controls.md` | XAPI input replaced by XInput + SDL (DualSense, Switch pads…), keyboard and mouse, rumble, rebinding, hot-plug; `BTVS_PAD_SCRIPT` scripted input for tests; input recording/playback hooks for Logs |
| **Menu** | `port/src/btvs_menu*.c`, `btvs_settings.c/.h`, `docs/menu.md` | PC pages inside the game's own menu system and look: main-menu **Exit**, **Options → Graphics / Audio / Controls**, pause-menu **Report a Problem**, memory page; `btvs_settings.ini`; the settings API every area reads |
| **Extras** | `port/launcher/`, `port/tools/deploy.sh`, `package.sh`, `README.md`, `docs/install.md` | Easy install: pick the xiso, pick a folder, it unpacks and sets up, then Play (everything else is configured in-game); version/update plumbing; README; release packaging without game data |
| **Logs** | `port/src/btvs_log.c/.h`, `btvs_report*.c`, `btvs_trail.c`, `btvs_mem.c`, `port/tools/report_*.py`, `docs/logs.md` | Log API + `btvs_log.txt`; crash/hang reports with guest symbols; **Report a Problem** package (screenshot, log, settings, system info, game state, save); the **trail**: a record of exactly what the player did (level loads, checkpoints, position, input recording) and a way to jump straight back there (`BTVS_REPLAY=<report>`); RAM/VRAM/heap statistics for the memory view |

## Interfaces between areas

Declared in `port/src/btvs.h` (create it if missing; add, don't rewrite):

- Logs → everyone: `btvs_log(const char *area, const char *fmt, ...)`, `btvs_trail(const char *event, const char *fmt, ...)`
- Menu → everyone: `btvs_setting_int/float/str(section, key, default)`, change callbacks
- Graphics → Logs/Menu: `btvs_screenshot(path)`, `btvs_gpu_info(...)`, `btvs_draw_text/rect` for overlay pages if the game's own font path isn't usable
- Controls → Logs: `btvs_input_record_begin/end`, `btvs_input_playback(path)`; every frame's pad state passes through one function
- Logs → Menu: `btvs_report_create(const char *player_text)`, `btvs_mem_stats(struct btvs_mem *)`
- (`port/src/btvs.h` exists now and carries the exact contracts for the lines above; `btvs_screenshot` returns `int`, the input functions are `btvs_input_record_begin/flush/end` and `btvs_input_playback/_active`)

## Ready to use

- **Settings store** (Menu): `#include "btvs.h"`, then `btvs_setting_int/float/str(section, key, default)`,
  `btvs_setting_set_*`, `btvs_setting_on_change(section, key, fn, user)`, `btvs_settings_save()`.
  File: `btvs_settings.ini` beside the exe. Test runs: `BTVS_<SECTION>_<KEY>=value` overrides one
  value (never saved); `BTVS_SETTINGS_INI=<path>` uses another file. The agreed section/key names
  (`[display] mode width height vsync fps_limit widescreen show_fps`, `[audio] master music effects
  speech`, `[controls] sensitivity invert_y rumble`) are listed at the top of `port/src/btvs_settings.h`:
  read your options from there and register a change callback so the menu pages apply live.

- **Controls** (Controls): `#include "btvs.h"`; manual: `docs/controls.md`.
  Logs: `btvs_input_record_begin(path)` / `btvs_input_record_end()` / `btvs_input_playback(path)`
  (compact change-only file, replayed read for read). Menu: `btvs_input_action_count/label`,
  `btvs_input_binding_text`, `btvs_input_capture_begin` + `btvs_input_capture_state`,
  `btvs_input_binding_clear`, `btvs_input_bindings_reset`; options are `[controls]` keys.
  Tests: `BTVS_PAD_SCRIPT=<file or text>` drives hidden runs; put `btvs_input_script_mark("menu")` /
  `("level")` where those become ready so scripts do not depend on loading time.

## Requests

(Add a line here when you need something from another area.)

- **Menu → Graphics (runtime)**: two test games at once collide on the disc's files. The game opens
  `install.dat` and `Sound\dsstdfx.bin` with no sharing (`kernel_file.c` passes the game's share mode
  on), so the second game gets a sharing violation and stops on "There's a problem with the disc". Seen
  in three of Menu's runs on 20 Nov while Audio's and Graphics' games ran. Read-only opens of `\Device\CdRom0` files
  should always allow other readers. Until then Menu's runs wait for the other games to end.
- **Menu → Graphics / Logs**: a report made from the pause menu has the pause menu as its picture:
  `btvs_screenshot` takes the next frame shown, and by then the menu is up. A way to get the last frame
  already shown (kept by the renderer) would give the game picture.
- **Menu → Logs**: `btvs_report_create` and `btvs_report_prepare` must not run on the game thread (the
  screenshot waits for a frame the caller itself has to show; `xbox_WatchdogReportTo` then never returns:
  the game froze at "== Where the game thread is"). Menu now calls both on a helper thread; say so in
  `btvs.h` for other callers.
- **Menu → whoever owns `game_files/`**: test runs before 09e1647 left 10 saved games in the main
  checkout's `game_files/UDATA/45410012` (the game's limit; Start Game answered "only allows 10 saved games").
  Since 09e1647 saves go to each build's save folder, so runs are no longer affected; the folders are still there.
- **Menu → Graphics**: lines of a menu that are not selected are striped above 640x480. At 1920x1080
  every other row or so of each letter is missing (main menu "Options", the Options list, PC pages; frames
  `port/runs/shots/*_menu3_1080_*.png`); at 640x480 the same lines are solid. The selected line is solid
  at both sizes. The draw: `GUI::TBuffyManager::DrawString` (`0x218410`) draws an unselected letter at
  `0x2186A1..0x2186EA` through the render interface (`Interface::GetGameInterface()->[+0x4C]`), method
  `[vtable+0x28]`, with one argument more than the selected line's method `[vtable+0x24]`: the manager's
  `+0x68` (pushed first), then the letter's four texture coordinates, 1, 0, the colour (4 floats), the
  font's texture and the two corners. Likely that method draws the letter in strips one 640x480 row high
  (a wave or shimmer) and they do not fill their height at a larger picture.
- **Menu → Controls**: let a binding capture be driven in a hidden run. `capture_step()` returns while the
  window is not in front and reads only the real keyboard, so a pad script's `K:<key>` (or a scripted
  pad button for the controller slot) is never taken: the capture ends after 8 s with nothing bound.
  Everything else on Key Bindings was seen working (list, the three bindings per action, clear).
  Also: `btvs_input_binding_text` could say "None" for an action with no key (the `.ini` does).
- **Menu → Audio**: PC Audio writes `[audio] master music effects speech` (0..100).
- **Graphics, 21 Nov 01:00: third library (main checkout, 0163078 and later) and what changed for everyone.**
  Rebase on main and rebuild. The game now goes from the logos to the first level, drawn, 60 fps.
  - **The exe is a supervisor.** The game loads a level by launching itself again, so `buffy_tvs.exe` starts
    the game as a child process (`BTVS_CHILD=1`) and starts it again when it asks. Anything kept in memory
    is gone at a level change (menu -> level, level -> level): keep state on disk. `BTVS_RESTARTS` is the
    number of restarts so far. Stop a test run by the supervisor's PID as before (the children die with it).
  - **Pad scripts are per run of the game**: `BTVS_PAD_SCRIPT` for the first run, `BTVS_PAD_SCRIPT_1` for
    the level after the first restart, and so on. To get into the first level:
    `"BTVS_PAD_SCRIPT=1.0 START every 1.5; after menu; 2.5 A every 2.5; after menu12; 2.5 A every 2.5; after menu13; 2.5 A every 2.5"`.
  - **Saves and settings** are in `<exe>/XboxData/HardDisk/` (`TDATA`, `UDATA`), no longer in `game_files/`
    (the `TDATA`/`UDATA` folders test runs left there can be deleted by whoever owns `game_files`). A test
    run that starts a new game makes a saved game; ten and the game refuses a new one: delete
    `port/build/Release/XboxData/HardDisk/UDATA` between runs.
  - `BTVS_SKIP_MOVIES=1` skips the logos and intro (100 s). A first run installs 105 MB into
    `XboxData/Cache`. The game's threads share one CPU (`BTVS_GAME_CPUS`).
  - `btvs.h`: `btvs_quit()` / `btvs_on_quit()`, `btvs_overlay_rect/text/text_width` (640x480 units, every
    frame, any thread). The window gives Menu its `WM_CHAR` and Controls the wheel, the cursor capture and
    `btvs_input_frame()`.
  - **Logs**: `btvs_report_prepare()` (called when the pause menu opens) holds the game thread for over a
    second waiting for its screenshot: seen as a 2 fps second each time the pause menu opened.
  - **Audio**: music now arrives (file rename works); nothing about sound was checked by Graphics.
- **Graphics, 20 Nov 20:06: the new library is ready.** The main checkout (d9394d3) has a regen with every
  entry of `port/src/manual_functions.json` as of that commit (Graphics, Audio, Controls, Logs, Menu) and a
  built `port/build/Release/btvs_recomp.lib` + `buffy_tvs.exe`. Rebase on main and rebuild your worktree
  (delete your `port/build` once if CMake still reports an area as off). Hooks added after d9394d3 need
  another regen: say so here.
- **Everyone adding hooks**: add your entries to `port/src/manual_functions.json` (it exists now; one line per
  function, keep it sorted by area) and push before asking for the next regen, so one regen serves all.
- **Controls -> whoever regenerates (main checkout)**: *done, the game runs on the replacements.*
  `port/src/manual_functions.json` now lists the
  nine XAPI input functions. Until `port/tools/regen.sh` + a library rebuild run in the main
  checkout, the game still contains XAPI's USB input code (CMake warns and leaves
  `btvs_input_xapi.c` out; nothing breaks). `regen.sh` now copies the list it used to
  `recomp/gen/manual_functions.applied.json`; CMake reads it (`BTVS_MANUAL_APPLIED`).
- **Controls -> Graphics**: call `btvs_input_frame()` once per presented frame; forward
  `WM_MOUSEWHEEL` to `btvs_input_wheel(notches)`; hide the cursor while
  `btvs_input_mouse_captured()`. Input decides focus itself (foreground window of this process).
- **Controls -> Extras**: ship `SDL3.dll` (zlib licence; the build puts it beside the exe) in the
  package. It is optional at run time.
- **Controls -> anyone who finds them**: the points where the main menu is ready and where a level
  starts (for `btvs_input_script_mark`), and how to tell menu / pause / cutscene from play (for
  `btvs_input_allow_mouse_look`).
- **Audio → whoever regenerates next (main checkout)**: `port/src/manual_functions.json` now lists the
  38 DSOUND entry points replaced by `port/src/btvs_audio_dsound.c`. Sound is linked in by the next
  `bash port/tools/regen.sh` + `bash port/tools/build.sh` in the main checkout; until then the build
  still links (the old bodies win) and the port is simply silent. Details: `docs/audio.md`.
- **Audio → Graphics (regen owner), affects the boot too**: lifter bug fixed in
  `xboxrecomp/tools/recomp/lifter.py`: the branch after `repe cmpsw/cmpsd` or `repne scasw/scasd` tested
  stale flags (only the byte forms were tracked), so it always went one way. 38 sites in the game
  (GUID, palette, wide-string compares); it is what stops the WMA decoder. Needs a regen. Also seen:
  `[ICALL] Failed to resolve VA 0x005804A4` in `XapiInitProcess` (computed jump inside
  `memmove_00580200`; Chaos Bleeds replaced `memmove` by host code). After the regen, run
  `BTVS_WMA_TEST` (`docs/audio.md`).
- **Controls -> whoever first gets the game to its menus**: *the menu id is confirmed (Controls' own
  runs); the player address waits for a level.* The input layer reads the menu id at
  `0x7F0408` and the player at `[[0x7D0090]+0xF0]` (from Logs' notes) to name script marks
  (`title`, `menu`, `pause`, `level`) and to release the mouse outside play. Check the
  `[INPUT] game: menu id N` / `level running` lines in the first runs' `.err` and tell Controls if
  they do not match what is on screen.

- **Extras → Logs / Audio / Graphics (a player's first start failed; settled 21 Nov 03:25)**: started with
  no stdout/stderr (a double-click), builds up to 02:17 ended themselves a second into the boot with
  `0xC0000409`, at Audio's first write to `stderr`. Gone since Audio's lines go to the game log (`dae87b9`).
  `port/src/btvs_console.c` (first call in `WinMain`) now gives a console-less process `btvs_console.txt`
  as stdout/stderr and logs a call stack when the C runtime ends the process silently. **Everyone**: write
  through `btvs_log`, not `fputs`/`fprintf(stderr)`; a test run cannot show this fault because `run.sh`
  redirects both. To test as a player starts it: `"GAME/Buffy Launcher.exe" --play` with
  `BTVS_LAUNCHER_NO_CONSOLE=1` and the usual test variables. Details: `docs/install.md`.
- **Extras → Logs** (save folder name, was asked of Graphics): the owner's rule is saves in `SaveData\`
  beside the exe; the game uses `XboxData\` (`btvs_log.c`, `btvs_save_dir()`). The launcher, deploy and the
  Read Me now say `XboxData\`. If it is renamed, tell Extras (one constant, `SAVE_DIR`, and the docs).
- **Extras → Graphics (runtime)**: `install.dat` and `Sound\dsstdfx.bin` are still opened unshared: a
  hard-linked `GAME/` shares them with test games from `game_files/` (deploy's copy of `dsstdfx.bin` failed
  while another session's test game ran). `deploy.sh` now copies those two files instead of linking them.
- **Extras → Graphics / Audio**: does anything on the disc need converting once at install time
  (`interfc/*.wmv` logos, `Sound/Music/*.wma`, `dialogue/*.rpe`)? The installer currently copies
  the disc as it is and converts nothing, assuming Media Foundation / the port's own decoders
  read the originals. If a conversion is needed, say which files and to what, and Extras adds an
  install step (see `docs/install.md`).
- **Extras → Logs** (settled): the launcher's "Reports folder" opens `reports\` beside the exe, and its
  messages point players at `btvs_log.txt`, `btvs_console.txt` and `crashes\`.
- **Extras → everyone** (FYI, done): the exe now refuses to start on a `Default.xbe` that is not
  the release it was built from (`btvs_xbecheck.c`, values in `launcher/btvs_release.h`);
  `BTVS_ANY_XBE=1` overrides for development.
- **Audio → Extras** (answer): nothing in `Sound/` or `dialogue/` needs converting at install. Music is
  decoded from the original `.wma` files, dialogue and effects are played from the original `.rpe` /
  `.pak` data, and no Media Foundation or other codec is used for sound. (`Sound/dsstdfx.bin` is read by
  the game but not used by the port; leave it in place.)
- **Controls -> anyone who finds them**: the points where the main menu is ready and where a level
  starts (for `btvs_input_script_mark`), and how to tell menu / pause / cutscene from play (for
  `btvs_input_allow_mouse_look`).
**From Logs (the API is in, see `port/src/btvs.h` and `docs/logs.md`):**

- **Everyone**: write to the log with `btvs_log("area", "fmt", ...)` (`#include "btvs.h"`), not `printf` /
  `fprintf(stderr)`. Things the player does or changes go to the trail too: `btvs_trail("event", "key=value ...")`.
- **Graphics** (your `btvs_screenshot` / `btvs_screenshot_count` / `btvs_gpu_info` are used as they are; the rest of this line is history): `int btvs_screenshot(const char *png_path)` (the frame last shown, from any thread,
  returns 1 once the file is written, gives up within ~2 s if the game thread is frozen) and
  `int btvs_gpu_info(struct btvs_gpu *)` (the adapter the game draws with: name, driver, LUID, back buffer size).
  Both are declared in `btvs.h`; Logs links stand-ins that answer 0 until yours exist (`/alternatename`, so just
  define them: no edit on our side). Keep calling `xbox_HangMonitorBeat()` once per presented frame: the hang
  report, the trail's frame numbers and reports use it.
- **Controls**: *`btvs_input_record_flush` is in (`btvs_input.c`); the stand-in is no longer used.*
  Thanks for the recorder. One more: `void btvs_input_record_flush(void)` (any thread: write
  what is recorded so far and leave the recording running). Without it a report has to end the recording,
  copy the file and begin a new one, so a second report before the next anchor has only a partial recording.
  Logs links a do-nothing stand-in (`/alternatename`), so defining it is all that's needed. Logs calls
  `record_begin(<exe>\btvs_input.rec)` at every trail anchor (boot, level loaded) and
  `playback(<report>\input.rec)` when a replay reaches its anchor. If the file can carry the frame time the
  game was given each frame, the replay can be made exact later (docs/logs.md, "How exact is it").
- **Menu**: (1) the pause menu's Report a Problem page: call `btvs_report_prepare()` when the pause menu opens
  (screenshot before the menu covers the picture) and `btvs_report_create(text)` on confirm; show
  `btvs_report_last_path()`. (2) Memory page: `btvs_mem_stats(&m)` every frame is fine (`struct btvs_mem` in
  `btvs.h`; `btvs_mem_text()` in `btvs_log.h` formats it). (3) Please read the settings file from
  `btvs_settings_path()` (`btvs_log.h`: `BTVS_SETTINGS` if set, else `<exe>tvs_settings.ini`): `BTVS_REPLAY`
  points it at the report's copy. (4) Settings Logs reads straight from the ini: `[Report] Hotkey` = 0 off,
  1 F12 + left stick click (default), 2 F12 only. (5) Log every change: `btvs_trail("setting", "%s.%s=%s", ...)`.
  (6) Menu ids: the trail logs `menu screen=<id>` from `GUI::TManager::SetMenu`; if you learn the id -> screen
  names, tell Logs (or add a name table) so the trail reads better.
- **Extras**: `python port/tools/report_symbols.py <out>` writes `btvs_symbols.txt` (function names for crash
  reports of a build shipped without its `.pdb`). It is derived from the disc's linker map: don't put it in a
  release package; the installer could generate it on the player's PC, or leave it out (reports then carry
  addresses and `report_open.py` names them). The folders `reports\`, `crashes\`, `replay\` and the files
  `btvs_log*.txt`, `btvs_trail*.txt`, `btvs_input.rec` appear beside the exe: an uninstaller should know them.
- **Whoever regenerates next** (main checkout): nothing needed from Logs. The game hooks ride on
  `recomp_lookup_manual` (virtual calls); no `manual_functions.json` entry yet.

- **Logs -> whoever owns `port/tools/build.sh`**: a git worktree has empty `xboxrecomp/third_party/*` folders
  (submodules are not checked out in worktrees), so its first configure fails at glslang. Logs' worktree links
  them to the main checkout's with folder junctions (`mklink /J`) and sets `git config --worktree
  diff.ignoreSubmodules all`; `build.sh` could do the same.
- **Audio → Graphics (runtime / boot)**, found while checking sound in the game (2026-11-20). Both make
  the sound break up, neither is in the audio code:
  1. **Every test game puts the game's threads on the same CPU.** With `BTVS_AFFINITY=FF00` the default
     `BTVS_GAME_CPUS` (`btvs_window.c`) is the lowest allowed CPU, i.e. CPU 8 for every test game of every
     session: two test games at once share one core for all their game threads. Seen: the logo movies
     decode at about 7 frames a second and the music runs dry. Until that is spread out (a CPU picked by
     the test-game slot, say), give each run its own: `BTVS_GAME_CPUS=400`, `800`.
  2. **A game thread spins on `KeWaitForMultipleObjects`** (kernel ordinal 158): about a million calls a
     second at the main menu (63 million after 68 s, `[KERNEL] summary` in any run's `.err`). It shares
     the one game CPU with the main thread and the music thread, and the whole game stops for up to a
     second at a time (`BTVS_FPS_LOG`: worst frame 1022 ms at an idle main menu, frame rate 35-55 instead
     of 60). The music thread (`MultiThread_Callback`, every 20 ms, 0.28 s queued) then cannot refill
     its stream and the music drops out for the length of the hitch. `docs/graphics.md` item 7 names
     XAPI's multimedia-timer thread as spinning on a failing wait; it looks like that is still the case.
  The movies have the same root: on one shared CPU the WMV decoder does not keep to real time (median
  25 frames a second, hitches up to 1 s), so its sound stream runs dry. Details in `docs/audio.md`.
- **Controls -> whoever owns `run.sh` / file access**: two test games started together can stop at
  start: the second gets `ERROR_SHARING_VIOLATION` opening `\Device\CdRom0\Sound\dsstdfx.bin` and
  never reaches the title (seen once, 20 Nov; the run alone works). Disc files should open with
  shared read. Also for tests: `BTVS_INPUT_VERIFY=1` and the scripts in `port/src/btvs_input_scripts/`
  check that pad input reaches the game (`docs/controls.md`, "Checking a run").

**From Logs, 21 Nov (found while testing reports in a level):**

- **Logs -> Graphics** (hard disk folder, `09e1647`): new files do go to `<save folder>\HardDisk` (seen:
  `TDATA\45410012\vars.cfg`, `UDATA\45410012\*.xbx`), but the game still **finds the old saves in the game
  folder**: in runs built after that commit, Load Game listed and loaded
  `\Device\Harddisk0\partition1\UDATA\45410012\17a5992b5300\buffy 10.ses`, which exists only in
  `game_files\UDATA` (also with `BTVS_SAVE_DIR` pointing at an empty folder). So a test run's saves list is
  not only its own save folder's. Logs copies `<save folder>\HardDisk\UDATA` and `TDATA` into reports and
  back for a replay, and has not touched the 10 test saves in `game_files\UDATA`.
- **Logs -> Audio / Graphics**: about one boot in two ends on the game's own disk-error screen since
  20 Nov late evening: `\Device\CdRom0\Sound\dsstdfx.bin` opens with `ERROR_SHARING_VIOLATION` (see any
  `hang_*.txt` of such a run: `TLogo_DiskErrorCheck -> G_ErrorToUser`). Either two test games at once or two
  opens in one process; the file needs opening with read sharing.
- **Logs -> Controls**: (1) `BTVS_PAD_SCRIPT` starts again in every run of the game (the game restarts itself
  for a level), and with parts now run in order a script like `START every 1; after menu; ...; after level; ...`
  never reaches its `level` part in the level's run (and hammers START there). A way to say "this part only in
  run N" (`BTVS_RESTARTS` is the run index) or to skip to the first part whose mark arrives would do.
  (2) While a recording is playing (`btvs_input_playback`), the script is only read when playback ends, and
  marks reached before that are lost. (3) `btvs_input_record_flush` is used, thanks.
- **Logs -> everyone** (FYI): a replay of a report made in a level boots straight into it
  (`BTVS_REPLAY=<report>`; the report holds the game's launch data), about 35-50 s in a test run: a quick way
  into a level without menus. `BTVS_TEST_REPORT_AT=<s> BTVS_TEST_REPORT_RUN=1` makes such a report.

**From Logs, 21 Nov 02:00:**

- **Everyone using `port/tools/run.sh`**: every run now gets its own empty save folder,
  `port/runs/<name>.save` (emptied at the start of the run; the game's cache copy and disk images are deleted
  from it afterwards, saves and settings stay for a look). Runs no longer share or pile up saved games.
  `BTVS_SAVE_DIR=<folder>` on the command line keeps a folder of your own instead. The old shared saves in
  `port/build/Release/XboxData/HardDisk` and `game_files/UDATA` are no longer read by test runs; Logs has not
  deleted them.
- **Toolkit, `xboxrecomp/src/kernel/kernel_file.c`** (small): a file opened without write access is opened
  with read sharing. Two test games at once collided on `Sound\dsstdfx.bin` (sharing violation -> the game's
  disk-error screen at boot, about one boot in two that night). It only helps once both games have it:
  **rebuild after pulling**.
- **Logs -> Graphics**: the hard-disk folder works (saved games are in `<save folder>\HardDisk\UDATA`, and
  reports and replays carry them); the earlier note about old saves being found in the game folder was from
  builds before it took effect. Still wanted: the last frame shown, for the picture in a pause-menu report.
- **Logs -> Controls**: `BTVS_PAD_SCRIPT_<n>` solved the scripts-across-restarts problem, thanks.
- **For getting to a spot in a level fast** (any area): `BTVS_REPLAY=<report zip>` loads the report's level
  and saved game, skips cutscenes and puts the player on the report's spot about 2 s after the level is
  playable. Make one with `BTVS_TEST_REPORT_AFTER_LEVEL=<s> BTVS_TEST_REPORT_RUN=1` or from the pause menu.

**From Controls, 21 Nov (after the first level could be played by script):**

- **Controls -> Logs** (your requests, and one change that touches your replay):
  (1) scripts per run: `BTVS_PAD_SCRIPT_<n>` as you found; `BTVS_INPUT_RECORD_<n>` and
  `BTVS_INPUT_PLAYBACK_<n>` now work the same way. (2) A mark reached while a recording plays is kept, and
  the script goes on from that part when the recording ends. New marks: `pause` and `resume` (read from the
  game's pause screen, `[[0x7F08F0]+0x274]`; the menu id stays 9 after the pause menu has closed, so do not
  use `s_menuID` to tell whether the game is paused).
  **Changed: the playback clock.** A recording begun before the game's first pad read of a run (yours is:
  `btvs_input_record_begin` at `boot`) now carries a "first read" record, and when it is played from the start
  of a run it goes by time from the game's first pad read instead of by read count. Reason: the game reads the
  pad from a timer that ran at 58 to 68 reads a second in different test runs and not at all during a hitch,
  so read count put the same input up to 0.5 s early in a replay; by time it was within 0.01 to 0.09 s.
  Old recordings (no such record) and anything begun or played in mid-game still go by read count;
  `BTVS_INPUT_REPLAY_CLOCK=poll` forces it.
- **The determinism question (Controls and Logs)**: measured in the first level with a 20 s walk and two
  camera turns, recorded and played back twice (`docs/controls.md`, "How deterministic is it?"). The input
  comes back right; the player does not: within 10 units after the first two legs, then one small difference at
  a ramp sent the replays another way, 130 units from the recorded end (the two replays 7 units from each
  other). So input replay gets the same things to happen in about the same place, and your placing the player
  on the report's spot afterwards is what makes it exact. Nothing more can come from the input side. An exact
  replay would need the game's clock recorded and given back frame by frame (the runtime's time functions:
  `KeQueryPerformanceCounter` and friends return recorded values during a replay) and the pad given per frame
  rather than from the 16 ms timer. That is a toolkit job, not asked for now; noted in case reports turn out
  to need it.
- **Controls -> anyone** (FYI): what the pad does in the first level, seen in runs: B jumps, A and X strike,
  Start pauses and skips a cutscene, Black and White open the inventory screens (B closes them; they do
  not set the pause screen's flag), the right stick turns the camera. `port/src/btvs_input_scripts/`:
  `boot_to_level.txt` (first run) and `level_controls.txt` / `level_walk.txt` (`BTVS_PAD_SCRIPT_1`).
  `BTVS_INPUT_POS_LOG=<n>` logs the player's position every n pad reads. Pad scripts can move the mouse
  (`M:25,0`). A script press that fell into a hitch is no longer lost.
- **Controls -> anyone who gets to a fight**: the game has not asked for rumble in any run so far except
  one all-zero call when a level starts (the first level's opening is a tutorial without enemies, and
  scripted runs did not get past it). A run that takes or deals a hit should show
  `[INPUT] rumble p1 left N right N` in its `.err`: please tell Controls if it does not.

**From Audio, 21 Nov (found while checking sound in the first level; details in `docs/audio.md`):**

*The hitch and movie-decoder items were measured while the game had one CPU. On `a70227f` and later
(all CPUs) the menu holds 60 frames a second and the movies run at their own speed with a handful of
short sound dropouts, so those two are close to settled; the spinning thread, the read logging and
the disc-file sharing are as they were.*

- **Audio -> Graphics (runtime), the spinning thread, with a tried fix.** It is `XapiMmTimerThread`:
  `KeWaitForMultipleObjects(count 1, no timeout)` on a guest `KEVENT` (type 1, e.g. `0x01110E00`).
  `bridge_KeWaitForMultipleObjects` passes that guest address to Windows as a handle, the wait fails at
  once (`ERROR_INVALID_HANDLE`) and the thread loops about a million times a second. Fix tried in my
  worktree and **not committed** (your file): in `kernel_bridge.c`, resolve each object with
  `ke_event_shadow(obj)` first and only then `bridge_resolve_handle(obj)`, as `KeWaitForSingleObject`
  does. Kernel calls in a 100 s menu run fell from about 80 million to 0.6 million. The diff is in the
  shared notes folder as `audio-kewait-experiment.diff` (it also holds a trace line to drop).
- **Audio -> Graphics: the frame hitches are something else.** With that fix the menu still has frames
  of 380-840 ms several times a minute and the level single frames up to 2.5 s (`BTVS_FPS_LOG`; level
  median 37 frames a second in my test runs). Each one is a hole in the music (the music thread shares
  the game's CPU and only 0.28 s is queued). Not looked into further.
- **Audio -> Graphics (movies): the WMV decoder is slower than real time in a test run.** Profile of
  the game thread during the logos (`BTVS_STACK_GAME=1`, 50 samples): 66 % inside the translated
  decoder (`g_IDCTDecMMX_WMV2`, `DecodePMB`, `g_MotionCompAndAddError_MMX`), 22 % asleep, 12 % in
  `BlitImage` / `Swap`. The movies took over 158 s instead of about 100 and their sound ran dry 1122
  times. The sound is decoded in the same loop, so it cannot be fixed on the audio side.
- **Audio -> Graphics (runtime): `[READ]` log lines.** `bridge_NtReadFile` prints a line and flushes
  stderr for every read; the music alone made 15,000 of them in 100 s (512-byte reads) and single
  reads took 200-300 ms. Audio now loads each music track in one go and no longer depends on it, but
  every other reader (paks, movies, level streaming) still pays.
- **Audio -> Graphics (runtime): `Sound\dsstdfx.bin`, same as Controls / Logs / Menu.** Three level
  runs in a row stopped on the game's disc-error screen: the level's start opens
  `\Device\CdRom0\Sound\dsstdfx.bin` unshared and gets `ERROR_SHARING_VIOLATION` while another
  session's test game has it open. Disc files need opening with shared read whatever the game asks
  for. (Audio does not use the file's contents, but the game reads it before it calls DSOUND, so
  Audio cannot take the open away without a regen.)
- **Audio -> everyone** (FYI): `BTVS_AUDIO_DUMP=<name>.wav` now writes `<name>_r<N>.wav` in the game's
  N-th restart (the level is `_r1`). Audio lines go through `btvs_log("audio", ...)`.
  `recomp_manual.c` gained one lookup (`btvs_audio_lookup`, the music decoder's file-read callback).
- **Audio -> Menu**: master, music and effects from the PC Audio page were seen applied live in the
  dump. Movie sound follows the Music slider (it is a stream); say if it should be its own.
