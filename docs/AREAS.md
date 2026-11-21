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
- **Menu → Controls / Audio**: PC Controls and PC Audio write `[controls] sensitivity invert_y rumble`
  and `[audio] master music effects speech`; Key Bindings uses your `btvs_input_*` binding calls (seen
  working: list, slot choice). Rebinding by capture was not driven in a test.
- **Graphics, 20 Nov 22:32: the second library is ready** (main checkout, 7166290: built with
  `/d2SSAOptimizer-`, hooks for the game's lock and `File_Read`). Rebase on main and rebuild. The game now
  boots to the title screen and its menus (frame dumps in the main checkout's `port/runs/shots/`). For test
  runs: `BTVS_SKIP_MOVIES=1` skips the logos and intro (100 s); a first run installs 105 MB into
  `<exe>/XboxData/Cache`; the game's threads share one CPU (`BTVS_GAME_CPUS`). `btvs_quit()` /
  `btvs_on_quit()` are in `btvs.h`; the window gives Menu its `WM_CHAR` and Controls the wheel, the cursor
  capture and `btvs_input_frame()`.
  Open (Graphics is on it): after the difficulty menu a new game fades to black and stays there. Cause
  found so far: the game has no map list. `TGame::Map_Set` asks the resource manager for the text resource
  `GameInfo\MissionList` (it is in `pak/res_null.pak`), gets nothing, and so never makes its
  `TMapSequencer`; the difficulty menu then has no map to launch (`GUI::TMenuDifficulty::OnUpdate`).
  Also open: the hard disk's `TDATA`/`UDATA` still land in `game_files/` (the runtime's
  `xbox_path_set_hdd_dir` is not used yet).
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

- **Extras → Graphics** (`main.c`): saves must live in `SaveData\` beside the exe (the owner's
  rule; install, deploy and the Read Me all say so). `resolve_game_paths` currently uses
  `XboxData\`. Please switch it, or tell Extras the real name so the docs follow.
- **Extras → Graphics / Audio**: does anything on the disc need converting once at install time
  (`interfc/*.wmv` logos, `Sound/Music/*.wma`, `dialogue/*.rpe`)? The installer currently copies
  the disc as it is and converts nothing, assuming Media Foundation / the port's own decoders
  read the originals. If a conversion is needed, say which files and to what, and Extras adds an
  install step (see `docs/install.md`).
- **Extras → Logs**: the launcher's "Reports folder" opens `Reports\` beside the exe, and its
  messages point players at `btvs_log.txt` beside the exe. Tell Extras if reports go elsewhere.
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
