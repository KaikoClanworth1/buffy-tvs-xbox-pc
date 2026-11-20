# Graphics and boot

State of the Graphics area: what is in place, what was checked and how, and what is not done.
Updated as the work goes; the newest facts are at the top of each section.

## Status

| Item | State |
|---|---|
| Boot to the game's main loop | **not run yet**: see "Boot log" |
| Game's Direct3D 8 drawn natively (no NV2A emulation) | code written, **not run yet** |
| Window: windowed / borderless / fullscreen, Alt+Enter, F11 | from the runtime; settings wired; **not checked** |
| Resolutions to 4K, internal rendering at that size | wired (`[display] width/height`); **not checked** |
| Widescreen | picture shape wired (4:3 bars, 16:9, stretch); a wider *view* needs the game's projection changed: not done |
| VSync, frame limit | wired (`[display] vsync`, `fps_limit`); **not checked** |
| Frame dumps (`BTVS_SHOT_AT`) | written; **not checked** |
| Logo movies (WMV) | not started (hooks on the game's `CWMVPlayer` are in place, passing through) |
| `btvs_screenshot`, `btvs_gpu_info`, `btvs_fps` | in `btvs.h` |
| Overlay text / rectangles for Menu and Logs | not done |

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

(none run yet: the first build of the translated code was still being made)

## Not verified / not done

Everything in the status table marked so. Nothing here has been seen on screen yet.
