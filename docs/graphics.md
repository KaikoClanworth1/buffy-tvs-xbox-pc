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
| Widescreen | the picture's shape only: 4:3 with bars, or stretched to 16:9. A wider *view* needs the game's projection changed: not done |
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
- Only the first level's opening minutes have been seen. Nothing beyond, no fights, no other level.
- Loading is slow (about 25 s for the first level from the menu in a test run).
- 2D text and HUD are drawn with nearest-pixel sampling: blocky at 4K.

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
| `BTVS_NATIVE_LOG=1` / `=f<frame>` | draw counts every 2 s / that frame draw by draw |
| `BTVS_GAME_LOG=1` | first calls of the hooked game-layer functions |
| `BTVS_D3D_DRAWS=1` | also run the library's own draw code (fills the pushbuffer only) |

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
| `BTVS_OVERLAY_TEST=1` | a sample panel through the overlay API |
| `BTVS_NO_SUPERVISOR=1` | the game runs in the process that was started; a restart ends it |
| `BTVS_GAME_CPUS=all` / `=<mask>` | the game's own threads on all CPUs / on those (default: one) |
| `RECOMP_KERNEL_LOG_BUDGET=<n>` | the runtime's kernel-call log beyond its first calls |

## Not verified / not done

Everything in the status table marked so. Nothing here has been seen on screen yet.
