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
| **Extras** | `port/launcher/`, `port/icon/`, `port/tools/deploy.sh`, `package.sh`, `README.md`, `docs/install.md` | Easy install: pick the xiso, pick a folder, it unpacks and sets up, then Play (everything else is configured in-game); version/update plumbing; README; release packaging without game data |
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

- **The game restarts inside the process: one process, one window for the session** (Graphics / runtime,
  21 Nov; `docs/graphics.md`, "One process for the session"). When the game launches itself again -- menu
  to level, level to level, back to the menu -- the process, the window, the Direct3D device, the sound
  device, the pads and every helper thread now stay; only the game is put back to its start
  (`btvs_boot_restart.c`, `reboot_inplace`). **It turns itself on once every area has its function below**;
  until then the log says `restarts: a new process each time -- no btvs_<area>_reboot() yet for: ...` and
  nothing changes. `BTVS_REBOOT=inplace` forces it on (testing), `BTVS_REBOOT=process` off.

  **What each area gives** -- one function, defined anywhere in its own files, no header or CMake change
  needed (an area without one gets an empty one from the linker):

  ```c
  void btvs_audio_reboot(void);     /* Audio */
  void btvs_input_reboot(void);     /* Controls */
  void btvs_menu_reboot(void);      /* Menu */
  void btvs_logs_reboot(void);      /* Logs */
  ```

  Called on the game thread, in that order, when the game has asked to be restarted: its other threads
  have ended, nothing of it runs, **its memory is still there and readable** (and is wiped and put back
  right after the last of these returns). `BTVS_RESTARTS` (so `btvs_restarts()`) is already the new run's
  number and `RECOMP_LAUNCH_DATA_FILE` already names the new run's launch data. What a function has to
  do: forget everything it knows about the game by address or by "done once" -- a new process used to
  forget it for you -- and stop every helper thread of yours from reading or writing the game's memory
  until the next run has set it up again. Nothing else is called again: `WinMain`'s `*_init` calls run
  once per process, as before. The lists under Requests say what the audit found per area.

  Things that are different for everyone once it is on: the process and its clock (`btvs_uptime`,
  `btvs_frame_seconds`) run for the whole session; a helper thread started at init lives through every
  run; `atexit` runs once, at the end of the session; `BTVS_PAD_SCRIPT` and the like are read once.
  Test switch: `BTVS_TEST_RELAUNCH=<seconds>:<map>[,...]` restarts the game for that map at those times
  with the line the game itself writes for a level change (a level change without playing to the end).
  `python port/tools/winwatch.py <seconds> <out.txt> [path part]` records from outside which processes
  and windows a test game has, hidden ones too, and its threads / handles / memory every 5 s.

- **Replacement textures changed while the game runs** (Graphics, for Controls' button pictures; 21 Nov):

  ```c
  void nv2a_gpu_texture_pack_reload(void);     /* xboxrecomp/src/kernel/nv2a_texpack.inc */
  ```

  Reads the texture pack's `load\` folder again: files with new contents under the same names, new files,
  files gone. Declare it beside your `nv2a_gpu_set_texture_pack` and call it **on the game thread, after
  the files are written and closed**. If the folder or Load itself changes (to or from "Original"), call
  `nv2a_gpu_set_texture_pack` first, then this.
  - **It returns at once.** The folder is read and every picture decoded and uploaded on a background
    thread (16 button pictures: 28 to 41 ms there); the pictures on screen stay until all are ready, then
    in one frame every texture the game has in use takes its new replacement -- or its own picture back
    where the file is gone. Nothing is decoded on the game thread: that frame only looks the textures up
    again. Mid-level, in menus, anywhere; no level load needed.
  - **Calling it again** before the last call's pictures are shown is fine (a rebind right after a device
    change): the folder is read once more and only the last reading is shown. No need to wait or debounce
    for correctness, but each call reads all files, so not every frame.
  - **Files being written while it reads**: a picture that cannot be read is logged
    (`[TEXPACK] reload: could not load ...`) and the game's own art shows for it until the next call. So
    write first, call after -- or write under other names and rename.
  - **Before the first texture** of a run it does nothing (the folder is read at the first texture anyway).
  - **Log**: `[TEXPACK] reload: <n> replacements read in <ms> ms on the loading thread (<n> failed)` and
    `[TEXPACK] reload: <n> replacements in use from frame <n>, <n> of the title's textures look theirs up
    again`.
  - **Pack turned on mid-run** (a run started with "Original", then switched to a set): every texture made
    before that is looked up again once, which decodes the game's own textures again in that frame -- a
    long frame, once. Keeping the pack on from the start with an empty folder avoids it.
  - **Checked** (hidden, level 1, 21 Nov): started with the keyboard set, the 16 files replaced by the Xbox
    set 30 s in, a reload every 3 s: the tip on screen went from mouse / Ctrl pictures to RS / LT pictures
    with no level load (`port/runs/shots/2030_tip_before_reload_keyboard_pictures.png`,
    `2030_tip_after_reload_pad_pictures.png`); 14 reloads in the level, none with a frame over 22 ms in the
    nine frames around it. Not checked: the title screen, the pause menu and the Controls page (the same
    lookup), and a real device change, which is yours to trigger.
  - Test switch: `BTVS_TEXPACK_RELOAD_EVERY=<seconds>` calls it that often.

- **Shaders at install time** (Graphics, for Extras' launcher): run the installed game once as

  ```
  buffy_tvs.exe --prepare-shaders        (or with BTVS_PREPARE_SHADERS=1 in its environment)
  ```

  in the install folder, after the game's files are in place (it loads `Default.xbe` like a normal start;
  `BTVS_GAME_DIR` / `BTVS_SAVE_DIR` as usual). It opens no window, makes no sound, starts no game and no
  second process, makes every shader the port knows of into `ShaderCache\` beside the exe and ends.
  - **stdout**: one line per shader, `shaders <done>/<total> of the port's list` and then
    `... of this PC's list`, and a last line `shaders ready: <n> vertex programs, <n> texture-stage,
    <n> combiner, <n> failed, <ms> ms`.
  - **Exit code**: 0 all made; 2 some could not be made (the game still runs: those are made at first use);
    1 no Direct3D 11 device.
  - **Time**: half a second on a new folder (181 ms of compiling for the 16 shaders of today's list), a few
    milliseconds when the cache is there. Safe to run again at any time, e.g. after an update: a shader
    whose source changed with the port's version is compiled again, the rest are read back.
  - **What it covers**: the renderer's own shaders and the port's list of texture-stage set-ups
    (`port/src/btvs_native_shader_keys.h`, built into the exe: nothing to deploy). **What it cannot**: the
    game's vertex programs. The game builds them while it runs and they are its code, so the port does not
    carry them; each is compiled when the game first loads it -- seen: all in the first frame of a level,
    behind the loading screen -- and from then on it is in that PC's `ShaderCache\shader_keys.bin` and
    made at every start. To cover them at install as well, put a `ShaderCache\shader_keys.bin` from a PC
    that has played (the owner's `GAME\ShaderCache\shader_keys.bin`, or a test build's) into the install's
    `ShaderCache\` before `--prepare-shaders`: it makes everything in that file too. That file holds
    game code: do not commit it.
  - The same preparation runs at every normal start of the game (`[gfx] shaders before the first frame`
    in `btvs_log.txt`), so a launcher that skips this step only moves the half second to the first start.
  - Growing the port's list: `python port/tools/shader_keys.py` gathers the texture-stage keys of every
    `shader_keys.bin` on the PC into the header; commit the header.

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

**From Graphics, 21 Nov (the owner: "When loading a new chaper/mission the window closes and a new one
opens this needs to STOP"): each area's `btvs_<area>_reboot()`** -- see "Ready to use", "The game
restarts inside the process". The in-process restart is built and stays off until all four exist. Line
numbers are of 24d9881; the full audit is what these lists are cut from.

- **Done by Audio, 21 Nov: `btvs_audio_reboot()` exists** (`btvs_audio_dsound.c`; `docs/audio.md`, "The game restarts
  inside the process"). Checked with `BTVS_REBOOT=inplace`: menu -> level 1 -> cemetery -> high school -> level 1, and
  cemetery -> Quit -> main menu -> Start Game -> level 1; no crash, sound in every run, no voice carried over. One
  `BTVS_AUDIO_DUMP` file per session now (no `_r<N>` when the restart is in the process). The request was:
- **Graphics -> Audio: `void btvs_audio_reboot(void)`.** Seen without it: the game's heap corrupted in
  the run after a restart when a movie's sound started (`RtlAllocateHeap` fault).
  - `btvs_audio_dsound.c`: `s_pool` is a block of the *game's* heap (`xbox_HeapAlloc`, L158) kept for the
    process, `s_device` a handle into it, `s_fx_desc`: zero all three (the new run's heap gives that
    address to the game). `s_slot_used[]`, `s_stream_max[]`, `s_tr[]`: zero, each slot's mixer voice freed.
    Listener `s_lpos` / `s_lfront` / `s_ltop` / `s_distance` / `s_rolloff`: defaults. `s_music[4]`,
    `s_music_next`: free each `.data`, zero (keyed by game objects that can come back at the same address).
    Leave `s_ready` alone.
  - `btvs_audio.c`: every voice freed with the device kept (there is no such function yet). The mixer
    thread reads sample data straight from game memory (`v->data`) and `packet_done` writes
    `*p->completed` / `*p->status` into game memory: free the voices under `s_lock` inside your function
    (memory is still valid then), so nothing is left that points there. Listener copy to defaults.
    `s_clock` and the `BTVS_AUDIO_DUMP` file name per run are yours to decide.
- **Graphics -> Controls: `void btvs_input_reboot(void)`.** **Done, 22 Nov (Controls)**: in
  `btvs_input_xapi.c`; everything listed below plus the probe's, packet and rumble-log statics
  (`docs/controls.md`, "The same with one process for the session"). **The pad script does not carry on:
  run n reads `BTVS_PAD_SCRIPT_<n>`** (and `BTVS_INPUT_RECORD_<n>` / `BTVS_INPUT_PLAYBACK_<n>`), exactly
  as with a process per run, so every test command means the same either way; a script that relied on
  `after level` firing again in the same script needs its level part in `BTVS_PAD_SCRIPT_1`.
  **For Logs**: the function ends the run's recording and playback, as the process's end did
  (`[INPUT] recording ended`); the trail's `btvs_input_record_begin` for the next run is yours to call
  from `btvs_logs_reboot` (it runs after ours).
  - `btvs_input.c`: rumble off on the hardware (`s_rumble`, as `at_exit` does); `s_read1`, `s_read1_ms`
    zero; `env_start`'s `done` zero so `BTVS_INPUT_PLAYBACK_<n>` / `BTVS_INPUT_RECORD_<n>` are read for
    the new run; `s_menu_mode` 0, `s_mk_blocked[]` 0, `s_allow_mouse_look` 1 (the mouse is not left
    clipped while the next run loads); a key capture under way ended. The pad script: say whether it
    carries on across runs (it does today, and the test runs here rely on "after level" firing again)
    or `BTVS_PAD_SCRIPT_<n>` starts per run as it did. `btvs_input__test_reset` is close but also drops
    the devices.
  - `btvs_input_xapi.c`: `s_ver[]` zero and `verify_thread` kept off game memory during the wipe
    (`BTVS_INPUT_VERIFY` only); `s_reported` zero; `game_probe`'s statics.
  - `btvs_input_glyphs.c`: `tips_fit`'s `done` and `delay` zero, `s_nfit` and `s_fit_text[]` zero (they
    are pointers into the game's text; today only the first level of a session gets the keyboard
    wording). `btvs_input__glyphs_boot` runs at every `XInitDevices`, as before: fine on our side.
- **Graphics -> Menu: `void btvs_menu_reboot(void)`.** **Done (Menu, 22 Nov)**: in `btvs_menu.c`, as listed; a report
  being saved is waited for (3 s at most) and settings are saved first. Checked with `BTVS_REBOOT=inplace`
  through four runs in one process (`docs/menu.md`, status table).
  - `btvs_menu.c`: `s_scratch` (a block of the game's heap, L232) zero -- without that the Options pages
    write into whatever the new run has there; `pc_wording`'s `messages_done` zero (the patched strings
    are loaded fresh); `s_options` zero, `s_page = PG_NONE`, `s_pending_page`, `s_row` / `s_nrows`,
    `s_shown`, `s_saved_*`, `s_nsaved` cleared, `s_bind_capture = -1`; the four `s_font_*` zero.
    `report_thread` (L248) reaches game memory through Logs' report functions: see Logs.
- **Graphics -> Logs: `void btvs_logs_reboot(void)`.**
  - `btvs_trail.c`: the "boot" anchor for the new run (what `btvs_trail_init` writes, without starting
    another `watch_thread`); `s_seen_level` "", `s_seen_menu` 0xFFFFFFFF, `s_level_loaded_at` 0,
    `s_cutscenes` 0, `s_menu_asked`, the level part of `s_where`. `watch_thread` reads the game's globals
    ten times a second: it needs to stand still from your function until the new run is up (it is safe
    from faults, not from logging nonsense).
  - `btvs_report_exact.c`: `X.mode = X_WAIT`, `X.seed_va = X.if_va = 0`, `X.f` closed and the file
    started again, counters and `P` zero. (**21 Nov late:** the 23-27 s loads Extras measured were not
    this and not anything of Logs': the 1 ms sleeps of the game's loader ran at Windows' 15.6 ms tick in a
    process whose window is not seen -- fixed in the runtime, `docs/graphics.md`, "Loads of 23 to 27 s".)
    **The runtime now takes the game thread's clock source away at
    a restart** (`xbox_hal_reboot_reset`): left installed it stood still while the next run started up and
    made every load after the first about five times longer (20 s for 4 s). Install it again when the next
    level's frames begin, as at the first start. `paused_now()` reads game memory raw and is reached from
    other threads (`btvs_game_paused`, `btvs_exact_snapshot`): gate it.
  - `btvs_report.c`: `hotkey_thread` -> `btvs_report_make` reads game memory: gate it between your
    function and the next run's first frame. `BTVS_TEST_REPORT_RUN` is only looked at in
    `btvs_report_init`: look again per run.
  - `btvs_report_crash.c`: `hang_thread` counts frames: a restart plus a load is a stretch without
    frames, and each run used to get the 90 s boot allowance; `s_crash_reports` / `s_reported_addr`
    were per run.
  - `btvs_report_replay.c`: what `btvs_replay_init` did for "later" runs (`s_r.active = 0` ...); it sets
    `RECOMP_LAUNCH_DATA_FILE` once for run 1: from the first restart on the variable is the game's own.
  - `btvs_log.c`: `btvs_uptime` is the session's now; `btvs_restarts()` goes up inside the process.
- **Graphics -> Extras** (FYI): the supervisor and its session window stay as they are (they are what a
  restart falls back on: `BTVS_REBOOT=process`, or the game asking from a thread other than its first).
  With restarts inside the process the launcher still sees one `buffy_tvs.exe` that starts one child;
  the child now lives for the whole session too. A release's `btvs_game.dll` is jumped out of with
  `longjmp` at a restart (unwinding through its frames): built with lld-link it must keep its unwind
  tables (`.pdata`), which is the default.

- **Graphics → Extras (21 Nov): the session has one window now** (your request of b39fc70, done another
  way: `docs/graphics.md`, "One window for the session"). The supervisor has a session window for the
  whole session and each run's window is owned by it; every window made is in the log (`[boot] window:
  ...`, `[gfx] window ...`). For the launcher nothing changes: it still starts one `buffy_tvs.exe` and
  that process lives until the game is quit. What a hidden run cannot show -- black instead of the
  desktop, the keyboard staying with the game -- is points 11 to 13 of the hand checklist there.

- **Extras → Graphics (21 Nov, owner: "we also need the launcher to compile the game for the player and
  all the shaders etc."): a headless "build all shaders" entry point the launcher can run at install
  time.** Wanted: `buffy_tvs.exe --build-shaders` (or a small exe of its own) run in the game folder that
  fills `ShaderCache/` with everything the game will need and then ends. No window, no sound, no game
  start; one progress line per step on stdout (`shaders 12/340`), exit code 0 or a code with one line
  saying what failed; safe to stop and run again (skips what is already there); honours `BTVS_AFFINITY`.
  Please also say: where the list of shaders comes from (a list shipped with the port, or found in the
  player's game data), whether a compiled cache made on one PC is valid on another GPU/driver, roughly how
  long it takes, and whether anything in `ShaderCache/` is derived from the game (if so a release must
  not ship it; if not, shipping it as Chaos Bleeds does is simpler than building it).
- **Extras → Graphics (21 Nov, from the owner's first play: "i dont like the app 'restartes' when starting
  the game"): one window from Play to gameplay.** What the owner sees is the game's restart into a level
  (`btvs_boot_restart.c`): the menus and the level are two processes, and each makes and loses its own
  window, so between them the window closes, the desktop (and the launcher behind it) shows, and a new
  window opens that may not get the focus. Evidence, hidden run of `GAME/` at `2e73139` through the
  launcher (title -> Start Game -> difficulty -> level 1): three processes, launcher aside --
  supervisor pid 38468; `started 2026-11-21 11:02:48, pid 24304` (run=1, menus, `save why=leaving the
  menus` at 11.2 s); `[boot] the game restarts itself (run 2)`; `started 2026-11-21 11:02:59, pid 31096`
  (`BTVS_RESTARTS=1`, `level_loaded name=m00_spanishmission_1` at 32.1 s). The same happens at every level
  change and on the way back to the menu. It is not the launcher (it starts one process and stays as it
  is) and not a window remade inside one process (`nv2a_pb_d3d11.inc` makes the window once in
  `gpu_startup` and never destroys it; the two `CreateDevice` lines per run reuse it).
  **Asked for:** the window belongs to the process that lives for the whole session. Suggested shape: the
  supervisor makes the one top-level window (title, icon, mode and size from the settings), pumps its
  messages while it waits for the child (`MsgWaitForMultipleObjects`), and hands its handle to each child
  in the environment; the runtime then makes its window as a child window filling it (`WS_CHILD`, parent
  = that handle) instead of a top-level one, so the swap chain, the window procedure and the input stay
  in the game process. Between two children the top-level window stays up, black or with a "Loading"
  picture, and keeps the focus. Closing it ends the child; Alt+Enter / F11 and the display settings resize
  the top-level window. Hidden test runs and `BTVS_NO_SUPERVISOR=1` stay as they are. Showing the next
  window before the old one goes is not an option while `install.dat` and `dsstdfx.bin` are opened
  unshared. Please also log each window creation (`[gfx] window ...` with the handle and the process),
  so a log can show one window for a session; today nothing in the log says a window was made.
- **Graphics → Logs, Controls (21 Nov): no flushed file writes on the game thread.** With the disk busy
  (other sessions building and testing while the owner plays) one flushed write takes 0.4 to 1.1 s and the
  game stands still for it. `BTVS_HITCH_LOG=90` in a cemetery fight caught the game thread inside
  `WriteFile` in every frame over 100 ms that is left (`docs/graphics.md`, "Slow frames"):
  - **Logs**: `write_frame` (`btvs_report_exact.c:422`) flushes `btvs_frames.rec` every frame; the trail's
    `pos` line goes to unbuffered stderr every few seconds; `btvs_log` lines written from the game thread.
  - **Controls**: `record_poll` (`btvs_input.c:1000`) flushes `btvs_input.rec` on the game thread.
  Wanted: keep the bytes in memory and let a helper thread write and flush (once a second, and when a
  report or a crash asks for the files). Measured with the frame recording off (`BTVS_EXACT=0`): 3 long
  frames in 63 s instead of 6.
- **Graphics → everyone (21 Nov): test logs.** The runtime's `[PATH]`, `[FILE] ->`, `[READ]` and
  `[KERNEL] summary` lines are printed only with `RECOMP_FILE_LOG=1`, which `run.sh` sets: test runs are
  as before, a player's game is quiet (a failed open is still noted, 200 times at most). Timing runs:
  `RECOMP_FILE_LOG=0`. The frame statistics are in every `btvs_log.txt` once a minute
  (`[gfx] frames, ...`).
- **Graphics → Controls (21 Nov)**: `game_probe` (`btvs_input_xapi.c`) calls `VirtualQuery` on the game
  thread often enough to be 2.5 % of its samples in a profile; a cheaper check of the three globals would do.

- **Graphics → Menu (21 Nov): a Brightness line on the PC Graphics page.** The game's own brightness
  (its Options -> Display slider) works now: the renderer applies the gamma ramp the game sets
  (`docs/graphics.md`, "Brightness"). The levels are dark at the game's default (slider 50) because that is
  how the game draws them; the owner should be able to raise it without finding the game's Display page.
  `btvs.h`: `int btvs_brightness(void)` (0..100, 50 the default, -1 before the game has its options) and
  `void btvs_brightness_set(int)`, both on the game thread; 75 and above look the same (the game's limit).
  The game saves the value with its own settings (`vars.cfg`), so no `.ini` key is needed unless a
  different default is wanted: that is the owner's choice, by eye (frames at 50 and 70 are in
  `port/runs/shots/`, `1058_*`).

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

**From Logs, 21 Nov 10:40 (frame recording; affects everyone a little):**

- **Everyone**: from the first frame after a level has loaded, the **game thread reads a clock of its own**
  (`btvs_report_exact.c`; toolkit: `xbox_SetThreadTimeStampSource` in `kernel_hal.c`, small): the real
  counter taken once at the start of each frame, plus one tick per read inside the frame. Frame steps are the
  real ones (game speed unchanged, 60 fps kept in Logs' runs); what changes is that time does not pass
  *inside* a frame for the game's code. This is what makes a report's replay exact (`docs/logs.md`, "The frame
  recording"). `BTVS_EXACT=0` turns it off: please try it first if something in a level misbehaves that did
  not before. Other threads are untouched. **Rebuild after pulling** (kernel library changed).
- **Logs -> Controls** (FYI, your files untouched): the determinism question is answered: clock (the cycle
  counter, also read inside frames), pad samples per frame (`TIF_Input::Poll` is hooked; during a
  `BTVS_REPLAY_INPUT=1` replay of a report with `frames.rec` the game gathers no samples of its own, so
  scripts, real pads and `btvs_input_playback` do nothing until the replay is over), random state. A walk
  now comes back bit for bit. One thing that touches play: **a Start press made in a level reaches the game
  one frame late** (the frame in between is kept as the picture for a report from the pause menu).
- **Logs -> Menu / Graphics**: the "last frame shown" request is no longer needed for the pause-menu
  report: its picture is now the last frame of play (the pause menu is its own scene with the camera moved
  away, so no later frame could show the game). Menu's `btvs_report_prepare()` call can stay; it does nothing
  while the game is paused.
- **Logs -> Audio** (question, no hurry): in replays the game thread drew its random numbers in a different
  frame than when recorded, from about 22 s into the first level, in some runs only. Is anything of yours
  called back on the game thread at a time set by the sound device (a buffer ending, a stream position)?

**From Controls, 21 Nov (button pictures; `docs/controls.md`, "Button pictures"):**

- **Everyone** (FYI): the game's button art is replaced by pictures the port draws at start-up, through
  the runtime's texture pack (`nv2a_gpu_set_texture_pack`, turned on from `btvs_input_glyphs.c`; nothing in
  Graphics' or Menu's files changed, `CMakeLists.txt` gained one source file). Test runs get them too: with
  no pad they show the keyboard set. `BTVS_CONTROLS_BUTTON_PICTURES=original` gives the game's own art back
  for a run. The pictures are written to `button_pictures\load` beside the exe (16 PNG files, 12 MB, made
  again when a binding changes); two test games started from one build folder with different sets need
  `BTVS_GLYPH_DIR=<folder>` each.
- **Controls -> Menu**: a line on the PC Controls page, please: **Button Pictures: Auto / Xbox /
  PlayStation / Keyboard / Original**, writing `[controls] button_pictures` = `auto` `xbox` `playstation`
  `keyboard` `original`. It takes effect at the next level load or start (the texture pack is read once per
  process), so the line could say so. Also: the menu's SELECT / BACK / CONFIRM / CANCEL labels are textures
  too (64x16 and 128x16, hashes in a `BTVS_GLYPH_DUMP=1` run) if you ever want them in another wording.
- **Controls -> Graphics**: the runtime's texture pack has one folder, and the button pictures now use it.
  If a player-made texture pack is wanted later (as in the Chaos Bleeds port), either
  `nv2a_gpu_set_texture_pack` takes a second folder, or say so and the button pictures are written into the
  player's pack folder under a sub-folder (the pack reads sub-folders). Seen while dumping: the Start Game
  page's Xbox console picture is texture `126c4f...` (128x128), should it ever need a replacement.
- **Controls -> Extras**: nothing to ship for this; the folder is made on first start. An installer that
  puts the game somewhere read-only would stop the pictures being written (the game then shows its own art
  and the log says how many pictures were drawn: 0).
- **Controls -> Graphics** (FYI, seen with `BUFFY_TEX_LOG=1`): the tips' page of pictures (128x256,
  `da8653...`) goes through the runtime's texture lookup as a new texture about 2,000 times in a 57 s level
  run, i.e. roughly every frame a tip is on screen; every other texture is found once. Cheap now that it is
  a replacement, but it may say something about how the game's font pages are cached.
- **Controls -> Menu**: with keyboard and mouse the tips' sentences are reworded in memory ("Press (Shift)"
  for "Pull the (L) trigger"); if a menu line for it is wanted, the switch today is only the test one,
  `BTVS_GLYPH_NO_TIP_TEXT`.
- **Everyone** (FYI, 21 Nov evening): the **default keys changed** (Space jump, left / right mouse punch /
  kick, E use, Shift target, Ctrl look, Tab inventory, R ammo swap, Esc pause; `docs/controls.md`,
  "Defaults"), and **in menus Enter / Space are A and Esc / Backspace are B** whatever is bound. A pad
  script's `K:` lines go through the same rules: `K:Space` on a menu confirms, in a level it jumps;
  `K:Enter` no longer pauses (`K:Escape` does). Pad-button lines (`A`, `START` ...) are untouched. A
  settings file with the old defaults untouched is rewritten once with the new ones.
- **Controls -> Menu**: `btvs_input_menu_mode(int)` (0 play, 1 menu, 2 title) is set by Controls' own probe
  (no level, or the pause screen open). If your pages know better when a menu is up (the inventory
  screens are not caught), call it. A PC Controls line for `[controls] menu_keys` (On / Off) would fit;
  Reset Bindings already gives the new defaults.
- **Controls -> Graphics / toolkit** (request, 21 Nov): the Controls page now has a pad version and a
  keyboard-and-mouse version, and the owner wants the pictures to change when the device in use changes.
  Today they change at the next level load or start, because the texture pack is read once per process
  (`tp_init` scans `load\` once, `TpEntry.tex` is kept, and the game's textures already uploaded keep their
  replacement). Wanted in `nv2a_texpack.inc`: **`void nv2a_gpu_texture_pack_reload(void)`**, callable from
  the game thread, that drops the loaded replacement textures (same file names, new contents) and makes
  textures already in the runtime's texture cache look their replacement up again. Controls would then
  redraw its 16 files (0.2 s) and call it when player 1 moves from pad to keyboard or back, or rebinds.
  **Done, 21 Nov (Graphics):** `nv2a_gpu_texture_pack_reload()` as asked -- "Ready to use" above says
  how it behaves. The files are read on a background thread, so call it right after writing them; the
  0.2 s of drawing the 16 files is still yours to keep off the game thread (or to accept: it is 12 frames).
  **In use, 21 Nov late (Controls):** `btvs_input_glyphs.c` draws the files on a worker thread and
  calls it on the game thread (inside the game's `XInputGetState`) when player 1's device or a binding
  changes. It links an empty stand-in if a runtime has no such function
  (`/alternatename:nv2a_gpu_texture_pack_reload=btvs_input__no_texture_reload`) and then says so in the
  log. To try it without a controller: `BTVS_INPUT_SCRIPT_DEVICE=xbox` and a pad script that mixes `K:`
  lines and pad buttons.
- **Extras -> Logs** (21 Nov, for releases; nothing to do for the prebuilt `GAME\`): in a release the
  game's code is not in `buffy_tvs.exe` but in `btvs_game.dll`, built on the player's PC with its own
  `btvs_game.pdb` beside it (`docs/install-build.md`). Seen in such a build's log:
  `[crash] guest function names: 170 from the .pdb` (the hooks only). Could crash reports also read
  `btvs_game.pdb` (made by lld-link, same folder as the exe) when `btvs_game.dll` is loaded, and name
  the module in a stack line? The DLL is linked `-debug`; say if another form (a `.map`) suits better.
- **Extras -> Graphics** (21 Nov): the game cannot start from a folder whose path has letters outside
  the PC's code page. Seen with an install in `...\Büffy 日本 test é\` on this (Western) Windows:
  `[boot] cannot start the game process (error 123)`, exit code 1, nothing else in `btvs_console.txt`.
  `btvs_boot_restart.c` starts the child with `CreateProcessA(exe, GetCommandLineA(), ...)`; the wide
  forms (`GetModuleFileNameW`, `GetCommandLineW`, `CreateProcessW`) would carry any name. Beyond that
  one call the game's own file access is by ANSI names too (the runtime's, Logs', Menu's settings), so a
  full cure is wider; until then the launcher refuses such an install folder with a message
  (`install_run`). Say if the supervisor's part gets fixed and I will test again.


**From Logs, 21 Nov 22:30 ("some physics objects jump about a lot": not found yet; for Graphics and whoever picks it up):**

- **The owner plays at 100 to 175 frames a second** (`GAME/btvs_settings.ini`: `fps_limit=240`, `vsync=1`;
  `GAME/btvs_log.txt`: frames of 5.7 to 9.8 ms on average, most of them not on time). The game ticks its
  characters at a fixed 1/60 s and everything else once a frame (`docs/logs.md`, "Some physics objects jump
  about a lot"): at that rate 40 to 50 % of the frames have no fixed tick, which the Xbox never showed.
  Measured so far it costs a zigzag of 0.03 units in particle objects, nothing visible; thrown and resting
  pickups behave the same at 30, 60 and 240. Debris, body parts and fights are untested.
- **Graphics (frame pacing, `btvs_frame.c`), no change asked yet.** Two things to know: (1) the test switch
  `BTVS_TICK_SYNC=1` (in Logs' `btvs_objlog.c`) makes the fixed tick as long as the frame above 60 fps (it
  writes the game's tick length at 0x771E9C and clears `TManager_Object`+0x3A8 before each object tick): a
  ready experiment if the frames without a tick turn out to be the cause; it is **not** proven harmless (per-tick
  constants of the characters then act more often a second). (2) Until the cause is known, the honest advice
  to a player who sees it is `fps_limit=60`: that is the rate the game was made for.
- **Menu**: in `m01_highschool_2` the owner opened Report a Problem, pressed line 0 five times and line 1 six
  times and left without sending (trail, 74 to 83 s). Worth a look at whether the page makes clear how to send.
- **Logs' frame clock** (everyone, from round 4): now moves on inside a frame after 200,000 reads, so a loop
  that only the clock ends (`TGO::Collision_PositionObjectInValidSpace`) cannot run for ever. Nothing else changes.

**From Logs, 21 Nov 22:35: `btvs_logs_reboot()` is in** (`btvs_trail.c`; what it does per file:
`docs/logs.md`, "The game restarting itself inside the process"). Checked with `BTVS_REBOOT=inplace`: a
report made in the second level of a session replays to the same spot (0.000000 units). For the others:
`int btvs_logs_game_up(void)` (`btvs_log.h`) is 0 from the restart until the next run's first frame; a
report asked for in that time is not made (the log says so). The log marks every run with a `[run]` line.

**From Logs, 21 Nov 23:15, to Graphics (the 23 to 27 s level load inside the process): it is not Logs'.**
Measured in hidden runs of main at f53a3d7, level 1 -> `BTVS_TEST_RELAUNCH` -> the cemetery (and on to the
high school), `RECOMP_FILE_LOG=0`, time from "the game restarts itself" to `level_loaded`:

| Run | Second load | Third load |
|---|---|---|
| `ld_on`: everything of Logs on | 23.6 s | 26.0 s |
| `ld_off`: `BTVS_EXACT=0` (no frame recording, the game thread never gets Logs' clock) | 24.3 s | 25.0 s |
| `ld_nologs`: `BTVS_EXACT=0` and `BTVS_LOGS_REBOOT=0` (new test switch: `btvs_logs_reboot` returns at once) | 22.4 s | - |

(The first load of each process, level 1 from the title screen: 5.4 to 7 s. The cemetery as a first load: 4 to
6 s in tonight's quiet runs.)

- **When Logs' clock is on the game thread**: only from the first frame after `TManager_Buffy::Map_Loaded`
  (`start()` in `btvs_report_exact.c`; log line `[exact] the level is up: frame recording on`), which in
  these runs is 0.3 s **after** `level_loaded`. From the restart until then the mode is "waiting" and the
  game thread reads the real counter (the runtime's `xbox_hal_reboot_reset` took the source away; nothing of
  Logs puts it back before that line). The 200,000-reads guard only exists inside that clock. The watch
  thread stands still from the restart to the next run's first frame. Logs writes nothing on the game thread
  during a load except the trail's few lines.
- **Where the time goes** (the hang reporter's stack of the game thread, taken 5, 10 and 16 s into the slow
  load with `BTVS_BOOT_HANG_SECS`; `port/build/Release/crashes/hang_20261121-2301..2306.txt` on this PC):
  two of three times, and in 8 of 12 instruction samples of one report, it is asleep in
  `TDiskManager::Transfer_GetBlock` (0x2BB0C0) <- `TFileReadStreamThreaded::LoadBlock` <- `Read`, under
  `TResourceManager::LoaderSwitch` loading a texture, a sound sample, a mesh: **the loader waits for the
  game's own disk thread to hand over the next block of a file**, whatever is being loaded. The first load of
  a process waits in the same place (same stack 5 s after start), so the place is normal; after a restart
  each wait is longer or there are more of them. The disk thread's loop (0x2BB4E0 to 0x2BB5E9) sleeps
  `Sleep(0)` while a transfer is open and `Sleep(30)` when none is; the loader's side is the `Sleep` in
  `Transfer_GetBlock`. Worth looking at for the run after a restart: the disk thread's priority and the
  runtime's handling of `Sleep(0)` (`xbox_KeDelayExecutionThread`: "2 ms of nothing but yields, then a
  millisecond at a time"), and whether the old run's disk thread or its transfer state is really gone.
  Your own note said 3.3 s inside the process before the four `btvs_*_reboot` functions existed; with Logs'
  switched off it is still 22 s, so Audio's, Controls' or Menu's, or the default-on path, are the others to
  switch off one at a time.
- **Extras -> Graphics** (FYI, 22 Nov; nothing to do): the port has an icon now, resource 1 of
  `buffy_tvs.exe` (`port/icon/btvs_icon.rc`). Your two window classes already load resource 1 from the
  exe (`BuffySessionWindow` in `btvs_boot_restart.c`, `BuffyRecompGpu` in `nv2a_pb_d3d11.inc`), so the
  title bar, the taskbar button and Alt+Tab get it with no change of yours. Keep that number if those
  lines are ever rewritten. `docs/icon.md`.
