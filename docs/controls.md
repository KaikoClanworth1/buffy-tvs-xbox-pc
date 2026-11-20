# Controls

The game's Xbox pads, answered by the PC: Xbox controllers (XInput), DualSense / DualShock 4 /
Switch Pro and other pads (SDL3), keyboard and mouse. No USB or controller hardware is emulated.

## How it fits together

| File | What |
|---|---|
| `port/src/btvs_input.h` | the API (other areas include `btvs.h`) |
| `port/src/btvs_input.c` | the one per-read function, bindings, ports, rumble, pad script, recording, playback, rebinding |
| `port/src/btvs_input_xapi.c` | the game's XAPI functions (`XGetDevices`, `XInputGetState`…) replaced; they only call `btvs_input.c` |
| `port/src/btvs_input_xinput.c` | XInput backend (DLL loaded at run time) |
| `port/src/btvs_input_sdl.c` | SDL3 backend (`SDL3.dll` loaded at run time, on its own thread) |
| `port/src/btvs_input_kbm.c` | keyboard and mouse |
| `port/src/btvs_input_test.c` | `btvs_input_test.exe`: the layer without the game |

**Every pad state the game reads comes from `btvs_input_poll(port, &pad)`.** In order it: plays a
recording if one is playing (and looks at nothing else); otherwise reads the PC controller on that
port and the keyboard/mouse, *only while the game's window is the foreground window*; maps them
through the bindings; lays the test script on top (no focus needed); writes the result to the
recording if one is being made.

### What the game does with a pad (from `i_xinput.obj`)

- `TIF_Input_Implement::Init` calls `XGetDevices` once; `HandleDeviceChanges` calls
  `XGetDeviceChanges` every update and makes or deletes a `TInputDeviceController` per port
  (`XInputOpen` in its constructor). So plugging pads in and out mid-game is the game's own path.
- `TInputDeviceController::Poll` calls `XInputGetState` from a **16 ms multimedia timer**
  (`InputTimerProc`), not once per frame, and buffers the reads. It ignores the return value.
- Sticks get the game's own **radial dead zone of 13107 (40 %)** and are rescaled past it; both
  right-stick axes are negated; analog buttons count above 30; triggers and face buttons are
  analog (0–255 → 0–1).
- Rumble: `SetMotorSpeed` writes two 0–65535 words into an `XINPUT_FEEDBACK` and calls
  `XInputSetState`, treating `ERROR_IO_PENDING` as success.

Because of that dead zone, sticks leave the port already past it: the PC stick's own dead zone
(`stick_deadzone`, default 15 %) maps onto the game's 40 %, so the whole travel of a modern stick
is used and the smallest push that registers already moves Buffy.

### Replaced XAPI functions (`port/src/manual_functions.json`)

| Address | Function | Answer |
|---|---|---|
| `0x006106B6` | `XInitDevices` | nothing to start |
| `0x006106BB` | `XGetDevices` | gamepads: bit per port with a pad; every other device type: none |
| `0x006106DD` | `XGetDeviceChanges` | insertions / removals since the last ask |
| `0x0061058D` | `XInputOpen` | a handle when the port has a pad |
| `0x006105E3` | `XInputClose` | stops that pad's rumble |
| `0x006105EF` | `XInputGetState` | `btvs_input_poll` |
| `0x0061065E` | `XInputSetState` | `btvs_input_rumble`, completed at once |
| `0x00610BFF` | `XMountMUA` | no memory units (saves are on the hard disk) |
| `0x00610D31` | `XUnmountMU` | — |

`XInputGetCapabilities` and `XInputPoll` are not linked into this game. Gamepad type table:
`0x0060F7F4`.

These only take effect after `port/tools/regen.sh` has run with the list (in the main checkout);
CMake compiles `btvs_input_xapi.c` in only when the generated code was made with it
(`recomp/gen/manual_functions.applied.json`, written by `regen.sh`).

## Players and devices

- Player 1 (port 1) always has a pad: the keyboard is always there.
- Each controller takes the lowest free port when it connects and keeps it until it is unplugged.
  Up to four. Hot-plug works both ways.
- `backend = auto` (default): Xbox-type pads through XInput, everything else through SDL (SDL skips
  pads it identifies as Xbox 360 / One so nothing is counted twice). `xinput`, `sdl` (SDL for all
  pads) and `none` are the other choices.
- `SDL3.dll` beside the exe is optional. Without it the log says so and XInput + keyboard still
  work. The build downloads the official SDL 3.4.20 release (pinned SHA-256) into
  `port/build/sdl3` and copies the DLL beside the exe; `-DBTVS_FETCH_SDL=OFF` skips that.
- When the window loses focus: devices are not read, the mouse is released, rumble stops (and
  resumes with focus). With `BTVS_NO_WINDOW=1` the game never has focus, so no device DLL is even
  loaded: a hidden test run cannot see or shake the owner's controller.

## Defaults

Controller (any pad, by position; Black / White are the bumpers):

| Xbox pad | PC pad | Keyboard / mouse |
|---|---|---|
| A | A / Cross | Space |
| B | B / Circle | Backspace, middle mouse |
| X | X / Square | E, left mouse |
| Y | Y / Triangle | Q, right mouse |
| Black | LB / L1 | Z |
| White | RB / R1 | C |
| Left trigger | LT / L2 (analog) | Left Shift |
| Right trigger | RT / R2 (analog) | F, Right Shift |
| Start | Start / Options | Enter |
| Back | Back / Share | Escape |
| D-pad | D-pad | arrow keys (wheel down / up = left / right) |
| Left stick | left stick | W A S D |
| Right stick | right stick | I J K L, and the mouse |
| Stick clicks | L3 / R3 | Left Ctrl / V |

Mouse buttons act only while the mouse is the camera. The keyboard defaults are the Chaos Bleeds
port's and are provisional until the game can be played.

## Settings (`[controls]` in `btvs_settings.ini`)

| Key | Default | |
|---|---|---|
| `backend` | `auto` | `auto`, `xinput`, `sdl`, `none` |
| `rumble` | 1 | 0 off |
| `rumble_strength` | 100 | 0–100 |
| `stick_deadzone` | 15 | % of the PC stick's travel ignored |
| `trigger_deadzone` | 12 | % |
| `invert_y`, `invert_x` | 0 | camera (right stick and mouse) |
| `mouse_look` | 1 | the mouse turns the camera |
| `sensitivity` | 5 | mouse, 1–10 |
| `keyboard_player` | 1 | which player the keyboard and mouse are; 0 = off |
| `key_<Action>` | see above | up to two keys: `key_A = Space, MouseMiddle`; `None` = unbound |
| `pad_<Action>` | see above | one controller input: `pad_Black = LB` |

Actions: `A B X Y Black White LeftTrigger RightTrigger Start Back DpadUp DpadDown DpadLeft DpadRight
MoveForward MoveBack MoveLeft MoveRight LookUp LookDown LookLeft LookRight LeftStickClick
RightStickClick`.
Key names: letters, digits, `Space Enter Escape Backspace Tab LShift RShift LCtrl RCtrl LAlt RAlt Up
Down Left Right Insert Delete Home End PageUp PageDown CapsLock F1`–`F12 Num0`–`Num9 MouseLeft
MouseRight MouseMiddle Mouse4 Mouse5 WheelUp WheelDown` and punctuation (`Comma` for `,`).
Controller inputs: `A B X Y LB RB LT RT Start Back L3 R3 DpadUp DpadDown DpadLeft DpadRight LS-Up
LS-Down LS-Left LS-Right RS-Up RS-Down RS-Left RS-Right Guide Misc Paddle1`–`Paddle4 Touchpad None`.

Changes made through the settings API apply at the next read (a change callback is registered).
As with every setting, `BTVS_CONTROLS_<KEY>=value` overrides one for a test run.

## For the Menu area: the Controls page

```c
#include "btvs.h"
int n = btvs_input_action_count();
btvs_input_action_label(a);                       // "Black button"
btvs_input_binding_text(a, slot, buf, sizeof buf); // slot 0, 1 = keys, 2 = controller; "" = empty
btvs_input_capture_begin(a, slot);                // then each frame:
switch (btvs_input_capture_state()) { case 0: /* "press a key..." */ case 1: /* bound, saved */ case -1: /* Escape or 8 s */ }
btvs_input_binding_clear(a, slot);
btvs_input_bindings_reset();                      // defaults, saved
btvs_input_device_name(port);                     // "DualSense Wireless Controller", "" = none
```

While a binding is being captured, and until the chosen key or button is released, the game sees a
resting pad, so the press does nothing in the menu underneath. Capture first waits for everything
to be released (the button that opened the prompt does not bind itself).

## For Graphics

- Call `btvs_input_frame()` once per presented frame (the frame clock of scripts and recordings;
  until then one read of port 1 counts as a frame, i.e. 62.5 per second).
- Forward `WM_MOUSEWHEEL` as `btvs_input_wheel(GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA)`.
- Hide the cursor (`WM_SETCURSOR`) while `btvs_input_mouse_captured()`.

`btvs_input_allow_mouse_look(0/1)` turns mouse capture on and off; the input layer drives it itself
from the game's state (see "Where the game is").

## Test runs: `BTVS_PAD_SCRIPT`

`BTVS_PAD_SCRIPT=<file>` — or the script itself, with `;` between lines — plays scripted input. It
needs no window and no focus, and works with `BTVS_NO_WINDOW=1`:

```bash
bash port/tools/run.sh menu 60 "BTVS_PAD_SCRIPT=0.5 START every 1.0; after menu; 1.0 A" BTVS_INPUT_LOG=1
```

One event per line: `<when> <inputs...> [for <how long>] [every <period>]`

```
# comment
0.5 START every 1.0        # tap Start at 0.5 s and then every second...
after menu                 # ...until btvs_input_script_mark("menu"); times now count from there
hold 0.2                   # taps are 0.2 s from here on (default 0.15)
1.0 DOWN                   # 1 s after the mark
+0.5 A                     # 0.5 s after the previous line
+0.5 LS=0.5,-1 LT=128 for 1.0   # left stick half right, full back; left trigger half; held 1 s
3.0 p2 X for 0.5           # player 2 (gives port 2 a pad for the whole run)
f500 RSL for f10           # frame 500 after the mark, for 10 frames
port 2                     # following lines are player 2
```

- **when**: seconds (`1.5`, `1.5s`) or frames (`f90`), counted from the game's first pad read, or
  from the last `after` mark. `+` = after the previous line's start.
- **inputs**: `A B X Y BLACK WHITE LT RT` (analog; `LT=128` sets a value 0–255, otherwise 255),
  `START BACK UP DOWN LEFT RIGHT L3 R3`, `LSU LSD LSL LSR RSU RSD RSL RSR` (stick fully one way),
  `LS=x,y RS=x,y` (−1…1, up and right positive), `NONE`. Stick values are raw: they must exceed the
  game's 40 % dead zone to move anything.
- **for**: how long it is held (same unit as *when*; frame lines default to 9 frames).
- **every**: repeat with this period until the next part starts.
- **after `<name>`** starts a new part that begins when some code calls
  `btvs_input_script_mark("<name>")`; the earlier part stops then. Loading time therefore does not
  matter. Marks the input layer places itself (see "Where the game is"): `title`, `menu` (main
  menu), `pause`, `menu<id>` for any menu, `level`.
- `BTVS_INPUT_LOG=1` logs every change of the pad the game was given (`[INPUT] t … p1 pad btn …`).
- `btvs_input_script_done()` is 1 when everything has played.

Without the game: `port/build/Release/btvs_input_test.exe script <file|text> [seconds]`.

### Where the game is

Ten times a second, from player 1's pad read, `btvs_input_xapi.c` looks at two of the game's
globals: the current menu id (`0x7F0408`; 1 main, 9 pause, 10 title, 15 resurrect) and the player
object (`[[0x7D0090] + 0xF0]`, non-zero while a level runs). Changes are logged
(`[INPUT] game: menu id 1`), named for scripts (`title`, `menu`, `pause`, `menu<id>`, `level`), and
mouse look is allowed only while a level runs and neither the pause nor the resurrect menu is up.
The addresses come from the Logs area's reading of `final.map` and are **not confirmed at run time**;
the log lines of the first runs will show. `BTVS_INPUT_NO_PROBE=1` turns the probe off.

## Recording and playback (for Logs' trail)

```c
btvs_input_record_begin(path);   // every pad the game reads from now on
btvs_input_record_end();
btvs_input_playback(path);       // the game gets the recording instead of the player until it ends
btvs_input_playback_active();  btvs_input_playback_stop();
```

Also `BTVS_INPUT_RECORD=<path>` / `BTVS_INPUT_PLAYBACK=<path>` (from the first read), and
`btvs_input_test dump <path>` prints a recording as text.

Format (little-endian): `"BTVSINP1"`, `u32` version 1, `u32` reserved, then records.

| Record | Bytes |
|---|---|
| state | tag `port \| connected<<2`; varint *reads* of this port since its previous record; varint *frames* since the previous record of any port; varint *ms* likewise; `u16` mask (bit 0 buttons, 1–8 analog A B X Y Black White LT RT, 9–12 LX LY RX RY); then only the masked fields |
| end | tag `0xFF`; 4 varints (reads of each port since its last record); varint frames; varint ms |

A record is written only when a port's pad changes, so a resting pad costs nothing and a minute of
play is a few kilobytes. The file is flushed every second; one without an end record (a crash) plays
to its last complete record.

Playback is keyed by **read count** by default: the n-th `XInputGetState` of a port returns what
the n-th returned when recording. That is exact with respect to the game's input sampling.
`BTVS_INPUT_REPLAY_CLOCK=frame` or `time` keys by frame number or milliseconds instead. During
playback the player's devices are ignored and the recorded connect/disconnect of ports 2–4 is
reproduced.

**How deterministic is it?** The input stream is reproduced exactly. Whether the *game* ends up in
the same spot also depends on the game: it samples pads on a 16 ms timer and consumes the buffered
samples per frame, so frame timing (and anything random) can shift how samples fall into frames. A
recording that starts at a level load or checkpoint and is replayed at the same frame rate is the
best case. This has not been measured on the running game yet.

## Verified / not verified

Verified (`btvs_input_test.exe` self-test, made-up clock and controller, no hardware):

- controller → Xbox pad mapping (analog triggers, Black/White on LB/RB, digital buttons, stick
  dead-zone mapping onto the game's);
- devices ignored without focus while port 1 stays connected;
- script parsing and timing: seconds and frames, `+`, `for`, `every`, `after` marks, player 2;
- recording → playback gives identical pads read for read even when the clock stumbles; recording
  size (changes only);
- capturing a controller binding, the resting pad during capture, save / reload / reset.

Also run by hand on the dev PC: `btvs_input_test script "<text>" 5` plays a script in real time with
`BTVS_NO_WINDOW=1` (no device is opened); `btvs_input_test live 3` loads `xinput1_4.dll`, finds and
starts `SDL3.dll` (every function the backend uses resolves). No controller was plugged in at the
time, so no real pad state has been seen.

Not verified:

- **In the game**: nothing yet. The game does not boot to its main loop (see `docs/graphics.md`), and
  the XAPI replacements need a regen. Stack layouts and return values in `btvs_input_xapi.c` were
  written from the disassembly of the callers, not run.
- Real hardware: an Xbox pad, a DualSense through SDL, rumble on either, hot-plug, keyboard and mouse
  in the window. The SDL functions are declared by hand from the SDL3 ABI.
- Mouse-look speed (the factor is a guess until the camera can be seen turning).
- Keyboard rebinding capture (needs a focused window and key presses).

## Not done

- Confirming the game-state probe's addresses (marks and mouse-look gating depend on them).
- A pad script that goes from boot through the main menu into a level (needs the game running).
- Cutscenes: the mouse is still captured during them (only menus and "no level" release it).
- The Menu area's Controls page (the functions it needs are above).
