# Graphics and boot

State of the Graphics area: what is in place, what was checked and how, and what is not done.
Updated as the work goes; the newest facts are at the top of each section.

## Status

Checked with muted, hidden test runs (CPUs 8-15, below-normal priority) and the frame dumps in the main
checkout's `port/runs/shots/` (21 Nov, 01:00).

| Item | State |
|---|---|
| Boot to the game's main loop | works: logos, intro movie, title, menus, new game, first level (see "Boot log") |
| Game's Direct3D 8 drawn natively (no NV2A emulation) | movies, title screen, menus (with the 3D Buffy model), the first level (`m00_spanishmission_1`): world with light maps, characters, cutscene captions, HUD, tips. Seen at 640x480, 1920x1080 and 2880x2160 |
| Frame rate | 60 a second in the menus and in the first level once it has loaded, at 640x480 and at 2880x2160; about 20-30 while a level loads (the game streams from its loader thread), and a dip to 30-50 for some seconds at one point of the opening cutscene (not looked into) |
| Resolutions to 4K, internal rendering at that size | work: `[display] width/height`; 1920x1080 and 3840x2160 (a 2880x2160 picture at 4:3) dumped and looked at |
| Widescreen | a really wider view in the levels, 4:3 in the middle for HUD, menus and movies (see "Widescreen"); seen at 1920x1080 and 3840x2160 |
| Window: windowed / borderless / fullscreen, Alt+Enter, F11 | from the runtime, settings wired; **never run with a visible window** (test runs must stay hidden) |
| VSync, frame limit | limiter works (steady 60.0 with 16.67 ms frames); VSync **not checked** (needs a visible window) |
| Frame dumps (`BTVS_SHOT_AT`, `BTVS_SHOT_EVERY`) | work (PNG; a 4K dump is 18 MB and costs a visible hitch) |
| Logo and intro movies (WMV) | play through the game's own decoder; sound not checked (Audio's) |
| `btvs_screenshot`, `btvs_gpu_info`, `btvs_fps`, `btvs_quit`, `btvs_on_quit` | in `btvs.h` |
| Overlay text / rectangles for Menu and Logs | `btvs_overlay_rect`, `btvs_overlay_text`, `btvs_overlay_text_width` (`btvs_native_overlay.c`); seen in a dump with `BTVS_OVERLAY_TEST=1` |
| The game restarting itself | works: the exe the player starts is a supervisor that runs the game as a child and starts it again with the launch data (`btvs_boot_restart.c`) |

Known faults and things not done:

- The highlighted menu item has a dark copy behind it, offset down. It may be the game's own drop shadow;
  not compared with a console picture.
- Fixed-function draws (the level geometry) get no lighting and no texture transforms: right for light-mapped
  walls, unknown elsewhere.
- `CopyRects` (the game's screen captures: `Buffer_DrawImage`, `Buffer_Capture`) is not drawn.
- Seen so far: the first level's opening (four minutes of scripted, blind stick input: no crash), and, started
  directly with `BTVS_MAP`, the first minute of `m01_highschool_1` (corridor) and `m03_cemetery_1` (a fight
  with skeletons, blue fire, the enemy's health bar). No level has been played through, and no level-to-level
  change has been made by playing.
- The levels are dark at the game's default brightness: that is how the game draws them (see "Brightness").
  Whether the default should be brighter on a PC monitor is the owner's call, by eye.
- A texture stage with `D3DTOP_BUMPENVMAP` (characters and props: the faint sheen from a shared 64x64
  environment texture, scaled by a texture factor of 12/255) passes the colour on correctly but does not
  shift the next stage's coordinates.
- Loading is slow (about 25 s for the first level from the menu in a test run).
- 2D text and HUD are drawn with nearest-pixel sampling: blocky at 4K.

## Widescreen

`[display] widescreen`: 0 the 4:3 picture with bars, 1 a wide view, 2 the 4:3 picture stretched.

With 1 and a frame wider than 4:3:

- **In a level the view is really wider.** The game's `View_SetFOV` gets a wider horizontal angle (60 -> 75.2
  degrees at 16:9: the tangent grows by 16:9 over 4:3). The game works out its vertical angle for a 4:3
  screen, so it grows too; the vertex shaders scale y back by the same factor. Result: the same picture
  height as on the console, more at the sides, nothing stretched.
- **HUD, tips, captions, movies** are drawn for a 640x480 screen and stay 4:3 in the middle.
- **2D that covers the whole width of the game's screen** (cutscene bars, fades, flashes, the dim behind the
  pause menu) covers the whole frame (`wide_full_width`).
- **In the menus** (no level loaded) everything stays 4:3 with black sides.
- **Objects that are part of the HUD** -- the weapon beside the ammunition count, the items of the two
  inventory screens -- are 3D, drawn by `TGORender_CustomViewport::Render` in a small viewport with a lens
  of their own (20 degrees). `View_SetFOV` knows that caller by its return address: for it the angle is not
  widened and the 3D goes where the 2D HUD goes (`btvs_native_ui_view`). Until 21 Nov they were widened and
  cut like the level's view: squeezed, off their places and, away from the middle, off the screen ("in
  widescreen the items don't appear in the UI"). Two more things were wrong with it: `View_SetViewport`
  calls `View_SetFOV` again with the angle already stored, which was widened a second time; and a program
  drawn in a small viewport had its 4:3 squeeze applied about the viewport's own middle while the viewport
  stayed at its place in the wide frame (now the viewport itself is moved and narrowed,
  `nv2a_native.inc`). Checked at 1920x1080 against the 4:3 picture: the HUD weapon and the inventory are
  the same, pixel for place.
- Not right yet: the inventory's and pause menu's purple backdrop is drawn as part of the level's 3D, wide,
  and its mesh ends before the frame's corners (dark corners at the far left and right).

Checked by frame dumps at 1920x1080 (menu, cutscene, gameplay) and 3840x2160 (menu). Not handled: the
game's own culling still thinks 4:3 x a wider angle, which is a superset, so nothing should pop at the
sides, but that has only been looked at in the first level's opening.

## Brightness

**What the owner sees dark is the game's own picture at its default setting; the game has a brightness
slider, and it works now.**

The game: Options -> Display has a brightness slider. It is the console variable `gamma` in `TGameOptions`
(+0x118; default 1.0, the slider's middle; the default settings file inside `install.dat` says `gamma=1.00000`
too). `TGame::Render` hands it to the renderer every frame, `TIF_Render_Implement::State_SetGamma` makes a
ramp (level i becomes 255 x (i/255)^gamma, gamma kept to 0.5..3) and calls `D3DDevice_SetGammaRamp`. On
the console the video encoder applied that ramp to the finished picture.

The port ignored the ramp, so the slider did nothing. Now `D3DDevice_SetGammaRamp` keeps the ramp and every
finished frame goes through it into a target of its own (`gamma_frame` in `btvs_native.c`,
`nv2a_gpu_gamma_ramp` in the runtime); frame dumps, the port's own text and the window take that picture.
At 1.0 the game sets no ramp and nothing is done. `btvs_brightness()` / `btvs_brightness_set(0..100)` in
`btvs.h` read and set the game's slider for Menu; the game saves it with its own settings.

Why the levels are dark at 1.0, checked on 21 Nov in `m01_highschool_1` (the first corridor) and
`m03_cemetery_1`, each by a test switch and a frame dump:

| Suspect | Found |
|---|---|
| Gamma ramp | the game calls `State_SetGamma(1.0, 1.0)` once and sets no ramp: nothing to apply at the default |
| Texture-stage ops (modulate 2x/4x) | the level is drawn in two passes: the light map (palettised 256x256 pages, `MODULATE` texture x factor FFFFFFFF), then the texture multiplied onto it (blend DESTCOLOR, ZERO; depth EQUAL). Plain 1x. Characters: four stages (bump, environment x diffuse, x factor, `MULTIPLYADD` diffuse x texture): no 2x either |
| Light maps drawn wrong | no: each page is drawn with its own palette (palette and texels dumped with `BTVS_P8_DUMP` and looked at: a night-time school, moonlight through windows, small lamps; most pages average 9 % of full brightness). `BTVS_WORLD_PASS=1` shows the light maps alone, `=2` the textures alone (fully bright) |
| Texture coordinate set | the second pass names set 1 through stage 0 (`D3DTSS_TEXCOORDINDEX`), which the fixed-function shader ignored. Fixed; in these two levels both sets are equal in that pass, so the picture did not change |
| Stencil shadows | every frame has a full-screen black quad at 34 % (the shadow pass): it is stencil-tested and only darkens shadows (same picture with and without it, `BTVS_NO_SHADOW_QUAD` while testing) |
| Fog | off in both levels |
| Mip levels | the light-map pages have one level; smaller ones are made from it (same picture with `BUFFY_TEX_LEVEL0=1`) |
| Missing draws | every draw call the game links is hooked; none is skipped in these frames |

So the dark corridor is the level's light maps times its textures, as the game asks. Pictures of the
original on the web look brighter than the default here; they cannot be matched frame for frame from a
hidden test run, and a player's slider position is not known. With the game's slider at 70 (gamma 0.6) the
corridor looks like those pictures.

## Slow frames

### Frame statistics (always on)

`btvs_frame.c` measures every frame. Once a minute, and when the game ends through its Exit, `btvs_log.txt`
gets three kinds of line (`[gfx] frames, ...`):

- how many frames, their average and longest, and how they were spread against the frame limit's step
  (on time, to 1.25x, 1.5x, 2x, 3x, 6x, longer);
- **the game thread's own work** per frame: its CPU time, not the clock. It does not grow while the thread
  waits or another program has the CPU, so it says what a frame costs whatever else the PC is doing. Also
  how many CPUs the whole program used;
- the slow frames (over 1.25 steps) by their largest part, and the five longest with all parts: `game`
  (the game's code and its drawing), `showing` (Present: with vsync the wait for the monitor),
  `textures` and `shaders` made in that frame, `limiter overslept`, `port` (the port's own work at the
  Swap: its text, frame dumps, window messages, input).

`BTVS_FRAME_LOG=<file>` writes every frame as a CSV line; `BTVS_FRAME_REPORT=<seconds>` changes the
minute; `BTVS_HITCH_LOG=<ms>` adds what the game thread was doing at that moment of every longer frame.

### "Game stutters a bit" (the 11:00 build of 21 Nov)

Measured in `m03_cemetery_1`, a fight with scripted input, 1920x1080 widescreen, about 80 s of play, hidden
test run on a busy PC (`port/runs/shots/1500_frame_times_before_after.png`):

| | Before (11:00 build) | After |
|---|---|---|
| Frames a second | 21.1 | 55.5 (58.8 with the frame recording off) |
| Frame time: median / 95 % / 99 % | 45.6 / 66.8 / 90.4 ms | 16.7 / 19.9 / 22.1 ms |
| The game thread's own work a frame: median / 99 % | 44.7 / 75.0 ms | 13.1 / 19.2 ms |
| Frames within 20 ms | 2 % | 95 % |
| Frames over 100 ms | 10 | 6 (3 with the frame recording off): all of them a log write, below |
| CPUs the program used | 1.9 | 0.9 to 1.1 |

What was wrong, in the order of what it cost:

1. **A test switch asked the environment for every texture of every draw** (`BUFFY_TEX_LEVEL0`, added to
   the texture code with the brightness work of that morning: mine). `getenv` scans the whole environment;
   several thousand times a frame it was 62 % of the game thread (profile: `strchr <- gpu_texture`). Now
   asked once. The other switches in the draw path were already asked once; `BTVS_FOV_LOG` was not and is.
2. **A thread spinning in the kernel's wait call**: XAPI's multimedia timer thread waits on an event that is
   an object in the game's memory; `KeWaitForMultipleObjects` passed its address to Windows as a handle, the
   wait failed at once and the thread looped two million times a second (Audio found this; their fix is
   in). A whole CPU.
3. **Two threads polling with `Sleep(0)`**: the game's disk thread when it has nothing to read (all through
   play) and whoever waits for a block from it. A thread that has only yielded for 2 ms now sleeps a
   millisecond at a time until it does something else (`xbox_KeDelayExecutionThread`).
4. **The limiter.** It slept *after* the frame was shown, so a frame reached the screen as soon as it was
   made and its place in time moved with the work in it; after a late frame the next ones were cut short to
   make the time up (the game moves by each frame's own time: a judder); and `Sleep` runs up to 15 ms over
   when Windows does not grant the 1 ms timer (a hidden or covered window). Now: wait first, then show; a
   late frame is followed by full steps; the wait is a high-resolution waitable timer.
5. **Log lines the game waits for.** stderr is not buffered: every line is a write, flushed, on the thread
   that printed it. With the disk busy a single write took 0.4 to 1.1 s, with the game thread inside
   `WriteFile`. The runtime's lines for every path opened, every open's result, every read and its
   two-second call summary are now only printed with `RECOMP_FILE_LOG=1` (`run.sh` sets it for test runs,
   which therefore still have these stalls; `RECOMP_FILE_LOG=0` for timing). A repeated warning from
   `NtFreeVirtualMemory` is given eight times at most.
6. An animated texture (the blue fire) had its smaller levels built again every frame it changed in, 2 to
   3 ms each: a texture that keeps changing gets none.

Left, each seen as a frame of 0.1 to 1.1 s with the game thread in `WriteFile` (requests in `AREAS.md`):
the frame recording's flush every frame (`write_frame`, Logs), the input recording's flush
(`record_poll`, Controls), the trail's position line to stderr every few seconds (Logs), and the runtime's
`CreateDirectoryW` on a path being opened (`xbox_translate_path`: mine, not done).

`RECOMP_SPIN_OLD=1` brings points 2, 3 and 5 back for comparison.

What none of this shows: a run at normal priority with a visible window and vsync on the owner's 240 Hz
display. The statistics in `btvs_log.txt` after the owner's next play will.

### Earlier (20 Nov)


`BTVS_HITCH_LOG=<ms>` logs every frame longer than that and what the game thread was doing at that moment
(a watcher takes its call stack).

A 150 s run into the first level (21 Nov, hidden, below-normal priority, CPUs 8-15, **while every CPU of the
PC was at 100 %** from other sessions' builds and test games) had 187 frames over 30 ms in the 103 s after
loading, 7.8 s in all, the worst 189 ms. By cause:

| Share | Cause | What was done |
|---|---|---|
| most | the game thread was not running: stopped inside the frame limiter's sleep, or in the middle of ordinary game code, for 30-190 ms | a test run is below-normal priority on CPUs other programs are using; a player's run is not. The limiter itself no longer bursts after a slow frame (it used to run frames with no pause until it had caught up 100 ms) |
| 8 frames, 50-75 ms | a texture's first use: decoded from the game's format when first drawn | not fixed (could be decoded when the game loads it) |
| 8 frames | the log itself (`fprintf` to the test run's stderr) | test runs only |
| 46 s -> 9 s of loading | all the game's threads on one CPU: the loader and the game took turns | the game's threads use all CPUs again (its lock is atomic now) |
| over 1 s, at every pause | Logs' `btvs_report_prepare()` on the game thread (waits for a screenshot) | Logs' |

What this does not show: how the game runs on an idle PC at normal priority. That needs a run the owner
makes (see the checklist).

## Frame limit and VSync

`[display] fps_limit` (0: none) and `vsync`. The game steps its world by measured time
(`TGame::ExecFrame(float dt)`), so the limit does not change its speed: the opening cutscene took the same
46-47 s at limits 60, 120, 144 and none (47.2, 46.2, 46.2, 45.9 s), and four seconds of scripted running
ended at the same place.

Measured in the first level, 30 s after the cutscene, four hidden runs on 21 Nov **with the PC's CPUs all
at 100 %**:

| Limit | Median frames a second | Best second | Average frame | Worst frame |
|---|---|---|---|---|
| 60 | 45.4 | 51.3 | 25.5 ms | 1068 ms |
| 120 | 43.3 | 110.0 | 30.9 ms | 967 ms |
| 144 | 50.0 | 56.5 | 20.0 ms | 82 ms |
| none | 50.8 | 161.7 | 18.0 ms | 71 ms |

So the limits above 60 work (110 with the limit at 120, 162 with none) and the game can render well over
60 in this level, but under that load no setting held its rate: the medians are what the loaded PC allowed,
not what the limit asked. An earlier run on a less loaded PC held 60.0 with 16.67 ms frames for minutes.
Steady 120/144 has to be confirmed on an idle PC (checklist, point 8). VSync has not been run at all.

## By hand: what only a visible run can show

Test runs are hidden and muted, so none of this has been seen. Each line is one thing to try and what
should happen.

1. Start the game (borderless is the default). The picture fills the screen; the taskbar is covered; Alt+Tab
   away and back works, with no mode switch.
2. **Alt+Enter** and **F11**: borderless/fullscreen <-> windowed, each way, several times. The window comes
   back at its old place and size; the picture is not stretched or cropped; the mouse cursor is visible over
   the window in a menu.
3. Windowed: drag the window, resize it by an edge, maximise it. The picture follows the window
   (4:3 with bars, or wide, by the widescreen setting).
4. Options -> Graphics -> fullscreen (mode 2) at the monitor's own resolution and at a lower one. The monitor
   switches mode; Alt+Tab gives the desktop back in its own mode and the game minimises; coming back
   restores the mode.
5. Two monitors: move the window to the other one, then Alt+Enter. Fullscreen goes to the monitor the
   window is on.
6. Close the window with its X and with Alt+F4: the game ends at once and `btvs_settings.ini` keeps the
   settings.
7. VSync on and the limit at 0: no tearing; the frame rate is the monitor's. VSync off: tearing may show.
8. Limits 120, 144 and 0 on a monitor that fast: `[display] show_fps` (or `BTVS_FPS_LOG=1` in a console)
   should say so in a level, and the game should not run faster (walk across a room with a stopwatch).
9. Widescreen 1 on a 16:9 monitor: menus 4:3 with black sides, levels wide, cutscene bars across the whole
   screen, HUD in the middle part.
10. A level at normal priority on an idle PC with `BTVS_HITCH_LOG=30`: how many `[HITCH]` lines in two
    minutes, and what they name.

## How it draws

```
game ─► The Collective's render layer (TIF_Render_*) ─► Xbox Direct3D 8, XDK 4432 (translated, runs as is)
                                                           │   keeps the device state; writes a pushbuffer nobody reads
                                                           └─ draw / clear / shader-constant calls hooked (btvs_native.c)
                                                                 └─► backend (xboxrecomp/src/kernel/nv2a_native.inc) ─► GPU layer ─► Direct3D 11
```

- The D3D library runs in its own no-GPU mode (`D3D__NullHardware`); `btvs_native_gpuwait.c` answers
  the few places that would still wait for a GPU (kick-off, fences, write-combine flush, vertical blank, IsBusy).
- At each draw the hook reads the library's state at that moment: `D3D__RenderState`, `D3D__TextureState`,
  the bound textures and palettes, the streams, the vertex shader object and its program. Vertex programs
  and their constants are kept from `LoadVertexShader` / `SetVertexShaderConstant` (the library only writes
  them to the pushbuffer).
- The backend turns Xbox vertex programs and the texture-stage / register-combiner setup into HLSL,
  decodes textures from guest memory (swizzled, DXT, palettised) into a cache, and draws.
- The frame is drawn into a texture at the output size (the game's 640x480 scaled), shown by the runtime's
  window code (`nv2a_pb_d3d11.inc`: swap chain, modes, Alt+Enter / F11).

What the game uses of Direct3D (from who calls what in the translated code):

| Call | Callers | For |
|---|---|---|
| `DrawIndexedVertices` | `TIF_Render_Implement::Render` (both overloads) | all geometry |
| `DrawIndexedVerticesUP` | `TIF_Render_Implement::Render` | geometry from memory |
| `DrawVerticesUP` | `BlitImage` | images, movie frames |
| `SetVertexShader` | `CD3DVertexShaderState::SetVertexShader`, `BlitImage` | FVF and programs |
| `SetVertexShaderConstant` | `CD3DMatrixState::UpdateRenderRegisters` | matrices for the programs |
| `SetTransform` | `CD3DMatrixState::SetProjectionMatrix / SetViewMatrix`, `CD3DMaterial::SetTextureTransform` | fixed-function path |
| `SetRenderTarget`, `SetPixelShader` | only D3D's own Swap | no render-to-texture, no pixel shaders: texture stages only |
| `CopyRects` | `Buffer_DrawImage`, `Buffer_Capture` | not drawn yet |
| `Swap` | `TIF_Render_Implement::Buffer_Swap` | once a frame |

## XDK 4432: where things are

Guest addresses (`final.map` address − 0x3EF460). Read off the library's own code; the function each
was read from is named so it can be checked with `python port/tools/xdis.py <name>`.

| What | Where | Seen in |
|---|---|---|
| `D3D::g_pDevice` | 0x005A29A0 | map |
| `D3D__NullHardware` | 0x0059F730 | map; `CDevice::KickOff` |
| `D3D__DirtyFlags` | 0x0059F738 | map |
| `D3D__TextureState` [4][32] | 0x0059F740 | map; `LazySetTextureState` (same order as later XDKs) |
| `D3D__RenderState` | 0x0059F940 | map |
| streams, 16 × {stride, offset, VB} | 0x0059FBA0 | `SetStreamSource`, `CDevice::SetStateVB` |
| attribute → shader slot, [2][16] bytes | 0x007729E8 | `CDevice::SetStateVB` |
| constant pool for c60..c62 | 0x0059E720 | `SetShaderConstantMode` |
| simple-state encode table | 0x00617058 | value 0x40354 at index 57 |
| device +0x08 flags (0x2000 no GPU), +0x1C index base, +0x30 time, +0x34 → GPU time | | `KickOff`, `BlockOnTime`, `DrawIndexedVertices` |
| device +0x370 pixel shader, +0x380 vertex shader, +0x388 program start | | `SetPixelShader`, `SetVertexShader`, `SelectVertexShader` |
| device +0x9D0 viewport, +0xA78 textures[4], +0xA88 palettes[4] | | `GetViewportOffsetAndScale`, `SetTexture`, `SetPalette` |
| device +0x2070 render target, +0x2074 depth, +0x2264/+0x2268 depth clip range | | `Clear`, `CommonSetViewport` |
| vertex shader object: +4 flags (2 screen-space, 0x10 program), +0xC program DWORDs, +0x14 slots × 16 {stream, offset, format}, +0x114 program block | | `SetVertexShader`, `LoadVertexShader`, `SetStateVB` |

**Render-state numbers differ from later XDKs.** The "simple" block is 57..82 here (57..91 from 4627 on),
so everything after it sits lower: `FOGENABLE` 83 (later 92), `PSTEXTUREMODES` 118 (136), `FOGCOLOR` 120,
`ZENABLE` 125, `STENCILENABLE` 126, `STENCILFAIL` 127, `FRONTFACE` 128, `CULLMODE` 129, `TEXTUREFACTOR` 130.
Worked out from which slot each `SetRenderState_*` function writes. The backend takes the two block starts
from the port (`nv2a_native_rs_layout(83, 118)`).

## Hooks

All in `port/src/manual_functions.json` as `wrap:` (the original stays callable as `<name>_orig`).
A change to that file needs a regen and a rebuild of the translated code, in the main checkout only.

## Test switches

| Variable | Effect |
|---|---|
| `BTVS_NO_WINDOW=1` | window made hidden, never shown or activated; 640x480 frames unless a size is given (`run.sh` sets it) |
| `BTVS_SHOT_AT=5,10,20` | `shot_<seconds>.png` of the frame at those times, into `BTVS_SHOT_DIR` (default `runs`) |
| `BTVS_SHOT_EVERY=<seconds>` | a frame dump that often |
| `BTVS_DISPLAY_WIDTH/HEIGHT/MODE/VSYNC/FPS_LIMIT/WIDESCREEN` | the `[display]` settings for one run |
| `BTVS_UNCAPPED=1` | no frame limiter |
| `BTVS_FPS_LOG=1` | frames per second, average and worst frame time, every second |
| `BTVS_FRAME_LOG=<file>`, `BTVS_FRAME_REPORT=<seconds>` | every frame's time and parts as CSV; how often the log gets the frame statistics (60) |
| `RECOMP_FILE_LOG=1` / `0` | the runtime's line for every file opened and read, and its kernel-call summary (`run.sh`: 1) |
| `RECOMP_SPIN_OLD=1`, `RECOMP_DELAY_LOG=1` | the spinning waits as they were (comparison); who sleeps for no time |
| `BTVS_FOV_LOG=1` | every `View_SetFOV` with its caller |
| `BTVS_NATIVE_LOG=1` / `=f<frame>` | draw counts every 2 s / that frame draw by draw |
| `BTVS_GAME_LOG=1` | first calls of the hooked game-layer functions |
| `BTVS_D3D_DRAWS=1` | also run the library's own draw code (fills the pushbuffer only) |
| `BTVS_GAME_GAMMA=<0.5..2>` | the game's brightness setting for the run (its `gamma`; 1 the default, below 1 brighter) |
| `BTVS_NO_GAMMA=1` | the game's gamma ramp not applied |
| `BTVS_WORLD_PASS=1` / `2` | the level with its light maps alone / its textures alone |
| `BTVS_P8_DUMP=<folder>` | with `BTVS_NATIVE_LOG=f<frame>` or `t<seconds>`: each palettised texture drawn in that frame, palette + indices |
| `BUFFY_TEX_LEVEL0=1`, `BUFFY_NATIVE_NO_TEXCOORDINDEX=1` | runtime: only the top mip level of every texture; texture coordinate sets by stage number |

## Boot log

Each thing that stopped the game on the way to its main loop, and the fix. Newest last.
All found and checked with muted, hidden test runs (`bash port/tools/run.sh boot <seconds> ...`).

1. **`memmove` copied nothing at its tail.** MSVC's hand-written `memmove` finishes through jump tables
   indexed from the table's end; the lifter did not see a switch (`[ICALL] Failed to resolve VA 0x005804A4`).
   Replaced by the host's `memmove` (`btvs_boot.c`).
2. **The runtime's fixed areas lay inside the game.** Its fake TLS, kernel data and stack sat at
   0x700000..0x780000, which here is the game's `.rdata`/`.data` (the image runs to 0x920D40). Moved above
   the image (`xbox_memory_layout.h`: kernel data 0x930000, TLS 0x950000, stack 0x980000, 6 MB).
3. **"Exit to dashboard" at start.** XAPI checks the hard disk's partitions; with `RECOMP_LAZY_DIRS` (my own
   default, copied from Chaos Bleeds) the partition images were never made. Default removed.
4. **"There's a problem with the disc you're using."** The game installs itself on a first start
   (`xinstall.obj`: while the logos play, a thread copies files to the cache drive Z: and unpacks
   `install.dat`: the `.cfg` files, the attract movie; 105 MB, not the whole disc). Two faults:
   creating a file that exists answered `STATUS_UNSUCCESSFUL` where the console says "name collision" (the
   one error the installer accepts), and a "format" of Z: left an interrupted install's files in place.
   Fixed in `kernel_file.c`; the files in `<save folder>\Cache` are removed when the game asks to install
   (`btvs_native_game.c`). `BTVS_NO_DISC_INSTALL=1` skips the install (Z: then reads from the game folder;
   the game runs without its `.cfg` files).
5. **Stack overflow in D3D when the game made its second device** (one for the logos, one for itself).
   D3D's start-up acknowledges the GPU's interrupt registers by writing ones; in plain memory they then
   read "pending" for ever and `ServiceGrInterrupt` / `HalGrLoadChannelContext` called each other until
   the stack was gone. The four interrupt registers are zeroed around every device start
   (`btvs_native_gpuwait.c`).
6. **The C runtime zeroed 35 s in; `sprintf("%f")` never returned.** The chain, backwards: the jump table of
   `_output` was zero ← `TSessionManager`'s constructor cleared 131 KB of game code ← `new` gave it an address
   inside the code ← the heap's free list held that address ← `TConsole::LoadVariables` had freed it: with
   no `vars.cfg` (a first start) `File_Read` sets no buffer and the caller frees whatever the stack held.
   Now `File_Read` leaves a null buffer when it fails (`btvs_boot_hooks.c`, after the second regen); until
   then an empty 24-byte `vars.cfg` is made before the game looks.
7. **The first level load never finished: the main thread waited for the loader thread to end.**
   `GetExitCodeThread` reads the thread object (`ObReferenceObjectByHandle`), which was a null pointer.
   The runtime now hands out an object with the two fields XAPI reads; for anything but a thread the
   object is the handle, which is what its `Ke*` waits expect (XAPI's multimedia-timer thread was
   spinning at time-critical priority on a wait that failed at once). `kernel_bridge.c`.
8. **Both threads waiting for the game's own lock.** `CLocker` is a counter taken in separate steps; it
   only works on one CPU. The game's threads now share one CPU (`BTVS_GAME_CPUS`, default one CPU of the
   process's; the renderer's and the audio's threads are not the game's and use the others), and the lock
   itself is taken in one step (`btvs_boot_hooks.c`, after the second regen).
9. **Hang on the first frame in `TManager_Object::QueryGroup`: a compiler bug.** Visual Studio 2019
   (cl 19.29.30159) compiles the translated loop `eax <<= 1; esi++; if (!(edx & eax)) again` to
   `bt eax, 1` on every round, at /O1 and /O2. With `/d2SSAOptimizer-` the code is right; the translated
   library is built with it (`port/CMakeLists.txt`). Found by dumping the compiled function's bytes
   (`BTVS_BREAK_CODE`) and reproduced on the one function alone.
10. **Particle templates past the 451st missing** (`[ICALL] Failed to resolve VA 0x00282E9A` and others).
    `TGBuffyInterface::ParticleTemplateSetup` is one 535-way switch, 10168 instructions; the disassembler
    follows 8192 and then starts an unnamed function in the middle, so the switch could not reach its last
    cases. `port/tools/fix_bounds.py` drops that split.

11. **Text and menu quads all drawn as the frame's last quad.** The backend caches vertex data by address
    and looked at it once a frame; `Draw...UP` data is scratch memory reused for every quad. Such draws are
    now looked at every time (`NativeDraw.scratch`).
12. **Music never arrived.** The game copies a track to `Z:` as `x.wma_` and renames it; the runtime had no
    rename (`NtSetInformationFile` class 10). Added (`kernel_file.c`).

13. **Everything after a skipped particle template failed to load; a new game faded to black.** The real
    cause of item 10: the lifter read at most 256 entries of a jump table, the switch has 535. The game reads
    its pak strictly in order, so one skipped template put every later request out of step: the mission
    list was "not found", no map sequencer was made, and the difficulty menu had nothing to launch.
    `lifter.py` now reads up to 4096 entries (third regen).
14. **The game exits to load a level.** It launches its own image again with a command line in the launch
    data (`root:Z:\ ... controller:0 ...`). The exe is now a supervisor: the game runs as a child, the
    runtime saves the launch data page and ends the child with a special exit code, the supervisor starts
    the next child with the page (`btvs_boot_restart.c`, `kernel_bridge.c`). `BTVS_EXIT_AFTER` counts for
    the whole session; the children die with the supervisor (job object).
15. **"Only 10 saved games allowed" after a few test runs.** The hard disk's partition fell into the game
    folder, so every test run left a saved game in `game_files\UDATA`. The hard disk is now
    `<save folder>\HardDisk` (`xbox_path_set_hdd_dir`), with the disk images kept for the bare devices
    (XAPI reads the disk's configuration sectors at start).
16. **The level's world was black.** Its geometry is drawn with the fixed pipeline (position + two texture
    coordinates), which the backend did not have. A vertex shader for it now takes world * view * projection
    (from the game's `SetTransform`) in constant slots 188..191 (`btvs_native.c: ff_begin`).

Reached so far: logos and intro movie, title, menus, a new game, the first level's opening cutscene and
the first steps of play under scripted input.

Debugging tools that found these (all in `btvs_boot.c`, off unless asked for):

| Variable | Effect |
|---|---|
| `BTVS_WATCH=<hex guest address>` | hardware write breakpoint on that DWORD, every thread: each write logged with the host call stack |
| `BTVS_BREAK=<function>[,<function>[,<function>]]` | log each entry of those translated functions with the guest registers; `BTVS_BREAK_QUIET=1` keeps the last 96 and prints them when the last-named one is reached |
| `BTVS_BREAK_HEAP=<hex size>`, `BTVS_BREAK_BADFREE=1` | with `RtlAllocateHeap` / `RtlFreeHeap` as the first function: the game heap at an allocation of that size; a free of something not in the heap |
| `BTVS_BREAK_CODE=<bytes>` | the compiled machine code of the named functions (to disassemble) |
| `BTVS_STACK_AT=<seconds>[,...]` | every thread's call stack at those times |
| `BTVS_SKIP_MOVIES=1` / `=2` | the start-up movies (logos and intro, 100 s) / every movie are not played |
| `BTVS_STACK_GAME=1`, `BTVS_STACK_RUN=<n>` | with `BTVS_STACK_AT`: only the thread that shows the frames; only in the game's n-th restart. 150 samples of the game thread make a usable profile |
| `BTVS_PAD_SCRIPT_<n>` | the pad script for the game's n-th restart (a script is for one run of the game; the level is restart 1) |
| `BTVS_NO_FIXED_FUNCTION=1` | the level geometry is not drawn |
| `BTVS_MAP=<map>` (`BTVS_MAP_OPTIONS`) | the game starts straight in that map (`m03_cemetery_1`, `hub01`, ...: the names of `pak/res_*.pak`), through the launch data it would write itself; more of its own options can be added (`cheats:1`) |
| `BTVS_HITCH_LOG=<ms>` | every frame longer than that, with what the game thread was doing |
| `BTVS_OVERLAY_TEST=1` | a sample panel through the overlay API |
| `BTVS_NO_SUPERVISOR=1` | the game runs in the process that was started; a restart ends it |
| `BTVS_GAME_CPUS=all` / `=<mask>` | the game's own threads on all CPUs / on those (default: one) |
| `RECOMP_KERNEL_LOG_BUDGET=<n>` | the runtime's kernel-call log beyond its first calls |

## Not verified / not done

Everything in the status table marked so. Nothing here has been seen on screen yet.
