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
