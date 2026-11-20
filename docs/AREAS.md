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

## Requests

(Add a line here when you need something from another area.)
