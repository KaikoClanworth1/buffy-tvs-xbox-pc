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

- **Menu → Graphics**: forward the window's `WM_CHAR` to `btvs_menu_char(wParam)` (text entry on
  the Report a Problem page). If Exit should shut the window/device down in order, offer a
  function; today the main menu's Exit calls `exit(0)`. `recomp_manual.c` now asks
  `btvs_menu_lookup()` first in `recomp_lookup_manual()`: keep that call if you rework the file.
- **Menu → Logs**: when `struct btvs_mem` / `btvs_mem_stats()` land in `btvs.h`, also
  `#define BTVS_HAVE_MEM_STATS 1` beside them: the Memory page then shows video memory and the
  game heap. `btvs_menu.c` already calls `btvs_report_prepare/create/last_path`, `btvs_log`, `btvs_trail`.
- **Menu → Controls**: `btvs_menu.c` calls `btvs_input_action_count/label`, `binding_text`,
  `capture_begin/state/cancel`, `binding_clear`, `bindings_reset` with the prototypes in your
  draft `btvs_input.h`; tell Menu if they change.
- **Menu → Audio**: PC Audio writes `[audio] master music effects speech` (0..100).
- **Graphics, 20 Nov ~16:35: regen + rebuild of the translated code in the main checkout** (first one with
  hooks: `port/src/manual_functions.json`, the Direct3D calls). It starts when the build lock frees and takes
  about 45 minutes; log in the main checkout's `port/runs/graphics_regen.log`. Until `port/build/Release/btvs_recomp.lib`
  exists there, a worktree build compiles the whole library itself (30 minutes under the lock): better to
  wait for it. Afterwards rebase on main before building: the generated code then calls functions that live
  in `port/src/btvs_native*.c`, `btvs_frame.c` and `btvs_window.c`.
- **Everyone adding hooks**: add your entries to `port/src/manual_functions.json` (it exists now; one line per
  function, keep it sorted by area) and push before asking for the next regen, so one regen serves all.
- **Controls -> whoever regenerates (main checkout)**: `port/src/manual_functions.json` now lists the
  nine XAPI input functions. Until `port/tools/regen.sh` + a library rebuild run in the main
  checkout, the game still contains XAPI's USB input code (CMake warns and leaves
  `btvs_input_xapi.c` out; nothing breaks). `regen.sh` now copies the list it used to
  `recomp/gen/manual_functions.applied.json`; CMake reads it (`BTVS_MANUAL_APPLIED`).
- **Controls -> Graphics**: call `btvs_input_frame()` once per presented frame; forward
  `WM_MOUSEWHEEL` to `btvs_input_wheel(notches)`; hide the cursor while
  `btvs_input_mouse_captured()`. Input decides focus itself (foreground window of this process).
- **Controls -> Extras**: ship `SDL3.dll` (zlib licence; the build puts it beside the exe) in the
  package. It is optional at run time.
- **Controls -> whoever first gets the game to its menus**: the input layer reads the menu id at
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
