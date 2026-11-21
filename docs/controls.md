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
port's and are provisional. What the buttons do in this game, as seen in the first level: B jumps,
A and X strike, Start pauses (and skips a cutscene), Black and White open the inventory screens
(B closes them), the right stick turns the camera and the left trigger resets it (the game's own
tip). So jump is on Backspace today, which wants changing once the owner has played.

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
| `button_pictures` | `auto` | `auto`, `xbox`, `playstation`, `keyboard`, `original`: see "Button pictures" |
| `last_device` | | written by the port: `controller` or `keyboard`, whichever player 1 used last (for `auto`) |
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
- **keys**: `K:<key name>` (`K:Space`, `K:W`, `K:MouseLeft`; the names of the bindings) holds a
  keyboard key or mouse button. It goes through the bindings table like a real key press, for the
  keyboard's player, and needs no focus: this is how the keyboard path is tested in hidden runs.
- **mouse**: `M:<dx>,<dy>` moves the mouse by that many counts at every pad read while the line
  lasts (right and down positive; `M:25,0` is the right stick fully right at the default
  sensitivity). It is the camera under the same rules as a real mouse: only in play, not while
  paused, and through `mouse_look`, `sensitivity` and the invert options.
- **for**: how long it is held (same unit as *when*; frame lines default to 9 frames). A press
  whose whole time fell into a stall of the game (it stops reading the pad during a long hitch) is
  not lost: it starts at the first read after the stall and lasts as long as written.
- **every**: repeat with this period until the next part starts.
- **after `<name>`** starts a new part that begins when some code calls
  `btvs_input_script_mark("<name>")`; the earlier part stops then. Loading time therefore does not
  matter. Parts run in the order written (a mark only starts the next part), so the same name can
  be waited for again: `after menu1` … `after menu12` … `after menu1`. Marks the input layer places itself (see "Where the game is"): `title`, `menu` (main
  menu), `menu<id>` for any menu, `level` (the player exists: the level has loaded), `pause` and
  `resume` (the pause screen or the inventory opens / closes). A mark reached while a recording
  is playing is kept; the part it starts begins its clock when the recording ends.
- `BTVS_INPUT_LOG=1` logs every change of the pad the game was given (`[INPUT] t … p1 pad btn …`).
- `btvs_input_script_done()` is 1 when everything has played.

Without the game: `port/build/Release/btvs_input_test.exe script <file|text> [seconds]`.

**A script is for one run of the game.** The game loads a level by starting itself again
(`docs/graphics.md`), so a test run that goes from the menus into a level is two runs:
`BTVS_PAD_SCRIPT` plays in the first, `BTVS_PAD_SCRIPT_1` in the level after the first restart,
`BTVS_PAD_SCRIPT_2` in the next, and so on (`btvs_boot_restart.c`). `BTVS_INPUT_RECORD_<n>` and
`BTVS_INPUT_PLAYBACK_<n>` work the same way. From boot into the first level and around it:

```bash
D="$(pwd -W)/port/src/btvs_input_scripts"
rm -rf port/build/Release/XboxData/HardDisk/UDATA     # the test build's own saves (ten and the game refuses a new one)
bash port/tools/run.sh level 150 BTVS_SKIP_MOVIES=2 "BTVS_SETTINGS_INI=<a scratch file>" \
     "BTVS_PAD_SCRIPT=$D/boot_to_level.txt" "BTVS_PAD_SCRIPT_1=$D/level_controls.txt" \
     BTVS_INPUT_LOG=1 BTVS_INPUT_VERIFY=1 BTVS_INPUT_POS_LOG=6
```

In the level's run nothing reads the pad until the level has loaded (20 to 45 s in a test run), so
times in a level script count from `after level`. The first level opens with a cutscene; Start
skips it (`8.0 START` after the mark is late enough).

### Where the game is

Ten times a second, from player 1's pad read, `btvs_input_xapi.c` looks at three of the game's
globals: the menu id (`0x7F0408`; 1 main, 9 pause, 10 title, 12 start game, 13 difficulty), the
player object (`[[0x7D0090] + 0xF0]`, non-zero while a level runs) and the pause screen's "open"
byte (`[[0x7F08F0] + 0x274]`, `IPauseScreen::SetOpen`). Changes are logged
(`[INPUT] game: menu id 1`, `game: level running`, `game: pause screen open`), named for scripts
(`title`, `menu`, `menu<id>`, `level`, `pause`, `resume`), and mouse look is allowed only while a
level runs and the pause screen is closed. All three are confirmed in the running game. The menu
id is the last menu *asked for*, not the one on screen: it is still 10 (title) when a level
starts and stays 9 after the pause menu has closed, which is why "paused" is read from the pause
screen instead. `BTVS_INPUT_NO_PROBE=1` turns the probe off.

`BTVS_INPUT_POS_LOG=<n>` logs the player's position and health every n reads of player 1's pad
(`[INPUT] pos r1830 x 4268.44 y -240.00 z 329.23 health 1000`; y is up). The read count makes two
runs comparable line for line.

### Checking a run: `BTVS_INPUT_VERIFY=1`

With this set, the port compares every pad it hands the game with what the game then stored in its
own input table (the 22 values `TInputDeviceController::Poll` makes of a read: sticks after its
dead zone, the eight analog buttons, the eight digital ones; layout in `btvs_input_xapi.c`). One
log line per pad value:

```
[INPUT] verify p1 ok: btn 0000 an 0 0 0 0 0 0 128 40 L 0 0 R 0 0 -> LT=0.502 RT=0.157
[INPUT] verify p1 MISMATCH: ...        <- the game stored something else
```

Ready-made scripts in `port/src/btvs_input_scripts/`:

| Script | What |
|---|---|
| `all_inputs.txt` | every button, trigger and stick direction once, part values, values under the game's thresholds |
| `all_keys.txt` | every default key and mouse button once (`K:` lines) |
| `menu_path.txt` | title → main menu → New Game → back → Load → back → Credits → back, waiting on the menu marks |
| `boot_to_level.txt` | title → New Game → name → difficulty: the game then restarts into the first level |
| `level_controls.txt` | in the level (`BTVS_PAD_SCRIPT_1`): skip the cutscene, each stick direction, camera, face buttons, part-way triggers, mouse, a keyboard key, inventory, pause |
| `level_walk.txt` | in the level: a short walk with camera turns, for recording and playing back |

```bash
bash port/tools/run.sh inputs 50 BTVS_INPUT_LOG=1 BTVS_INPUT_VERIFY=1 BTVS_SKIP_MOVIES=2 \
     "BTVS_PAD_SCRIPT=$(pwd -W)/port/src/btvs_input_scripts/all_inputs.txt" "BTVS_SETTINGS_INI=<a scratch file>"
grep "verify" port/runs/inputs.err
```

Run test games one after the other when both read the disc's sound files: two at once can stop
at start with a sharing violation on `Sound\dsstdfx.bin` (seen once; not a Controls fault).

## Recording and playback (for Logs' trail)

```c
btvs_input_record_begin(path);   // every pad the game reads from now on
btvs_input_record_end();
btvs_input_record_flush();       // what was recorded so far is on disk (the file stays open)
btvs_input_playback(path);       // the game gets the recording instead of the player until it ends
btvs_input_playback_active();  btvs_input_playback_stop();
```

Also `BTVS_INPUT_RECORD=<path>` / `BTVS_INPUT_PLAYBACK=<path>` (from the first read of the game's
first run; `BTVS_INPUT_RECORD_<n>` / `BTVS_INPUT_PLAYBACK_<n>` for the run after the n-th restart,
so `_1` is the first level), and `btvs_input_test dump <path>` prints a recording as text.

Format (little-endian): `"BTVSINP1"`, `u32` version 1, `u32` reserved, then records.

| Record | Bytes |
|---|---|
| state | tag `port \| connected<<2`; varint *reads* of this port since its previous record; varint *frames* since the previous record of any port; varint *ms* likewise; `u16` mask (bit 0 buttons, 1–8 analog A B X Y Black White LT RT, 9–12 LX LY RX RY); then only the masked fields |
| end | tag `0xFF`; 4 varints (reads of each port since its last record); varint frames; varint ms |
| first read | tag `0xFE`; varint ms since the recording began: when the game read a pad for the first time in this run. Written once, and only when the recording began by then (a run recorded from its start) |

A record is written only when a port's pad changes, so a resting pad costs nothing and a minute of
play is a few kilobytes. The file is flushed within a second of each record; one without an end
record (a crash, or a test run's timed exit) plays to its last complete record.

**The playback clock.** A recording made from the start of a run (it has the "first read" record)
and played from the start of a run goes by **time from the game's first pad read**: the game
starts reading the pad when it is ready to run (in a level's run: when the level has loaded), so
that moment is the same in both runs however long the load took, and from there a pad change
recorded at 14.016 s is given at 14.016 s. Anything else (a recording begun or played in mid-game)
goes by **read count**: the n-th read of a port returns what the n-th returned when recording.
`BTVS_INPUT_REPLAY_CLOCK=poll`, `frame` or `time` forces read count, frame number or plain time
since playback began. During playback the player's devices are ignored and the recorded
connect/disconnect of ports 2-4 is reproduced. The log says which clock a playback uses.

Why not read count everywhere: the game reads the pad from a timer whose rate is not steady (58 to
68 reads a second across test runs, none at all during a hitch), so the same read number is a
different moment in another run.

**How deterministic is it?** Measured in the first level (21 Nov, hidden test runs; `level_walk.txt`
recorded with `BTVS_INPUT_RECORD_1`, then two more runs playing it with `BTVS_INPUT_PLAYBACK_1`,
`BTVS_INPUT_POS_LOG=30` in all three):

- **The input is reproduced.** By the default clock each of the 18 pad changes of the 28 s recording
  reached the game within 0.01 to 0.09 s of its recorded time (most within 0.03 s). By read count
  the same recording ran up to 0.53 s early.
- **The player's position is not reproduced exactly.** After the first two legs (3.5 s of walking,
  about 430 units) the three runs stood within 10 units of each other. The next leg, after a
  0.7 s camera turn, passes a ramp: in the recorded run she walked past it, in both replays she
  walked onto it, and at the end of the walk the replays stood 130 units from where the recorded
  run ended (she is about 60 units tall) and 7 units from each other. So: a few units of drift
  per leg, which an obstacle can turn into a different path.
- The cause is the game, not the input: it steps by measured frame time and the frame rate
  differed between the runs (38 to 56 frames a second during the same camera turn). Nothing
  in the port pins the game's clock or its random numbers.

For Logs' replay this means what `docs/logs.md` already does: play the input to make the same
things happen in roughly the same place, then put the player on the report's exact spot. A replay
that lands exactly would need the game's own clock recorded and played back frame by frame
(the runtime's time functions, not Controls); see `docs/AREAS.md`.

In the menus: a 29 s recording of `menu_path.txt` from boot, played back in a second run, gave the
same 29 pad changes and the same menus in the same order (10, 1, 12, 1, 5, 1, 11, 1, 11, 1).

## Button pictures

The game's 2002 button art (the Y / X / B / A medallions beside SELECT and BACK, the small pictures
inside tips, the black Xbox controller on the Controls page) is replaced by pictures the port draws
itself, for the device the player is on:

| Set | When | Looks |
|---|---|---|
| Xbox | an XInput pad (or any pad that is not a PlayStation one) is player 1 | A B X Y in their colours, `LB RB LT RT`, sticks `L` `R` |
| PlayStation | player 1's pad is named DualSense / DualShock / PS4 / PS5 / Sony | cross, circle, square, triangle; `L1 R1 L2 R2` |
| Keyboard and mouse | no pad, or the keyboard or mouse was the last thing used | the keys and mouse buttons bound, by name: `Space`, `E`, a mouse with the button lit |

`[controls] button_pictures` chooses: `auto` (default), `xbox`, `playstation`, `keyboard`, or
`original` for the game's own art untouched. The pictures follow the **bindings**: rebind jump to
another key or pad button and the pictures show that one.

**Nothing of anyone else's is in them and there is no picture file in the repository.** Every
picture is drawn by `port/src/btvs_input_glyphs.c` from circles, rounded boxes and lines, with
letters from the PC's own Segoe UI font. No downloaded set, no console maker's art, no licence to
keep. The game's data is not touched either.

### How

The game draws all of it from textures (found with the runtime's texture dump, below):

| What | The game's textures | The port's |
|---|---|---|
| Menu buttons A B X Y, and L R on the inventory screens | twelve, 64x64: one as usual and one flashed when pressed, each | 256x256 each |
| The pictures in tips and the journal | one 128x256 page of twenty; the text writes them as the letters `a`..`t` of a font (`IJournal::TranslateGlyphs`): A B X Y, L R, eight arrows, the two sticks, "turn", "+", White, Black | 512x1024 |
| The controller on the Controls page | 384x320 over three textures (256x256, 128x256, 256x64) | 1536x1280, cut the same way |

Before the game's first frame (`btvs_input__glyphs_boot`, from the `XInitDevices` replacement) the
port draws its pictures four times the game's size (sharp up to 4K, where a menu pixel is 4.5
screen pixels), writes them as PNG files into `button_pictures\load` beside the exe, and gives that
folder to the runtime's **texture pack** (`xboxrecomp/src/kernel/nv2a_texpack.inc`,
`nv2a_gpu_set_texture_pack`). The texture pack swaps a texture for the file whose name holds a hash
of the texture's contents; the game's own coordinates then land on the bigger picture. Drawing takes
about 0.2 s and is done again only when something it depends on changed (the set, a binding, the
drawing itself: `load\made_from.txt`).

The menus fade a button that does nothing on a page themselves, so there is one look for a button
and one for the moment it is pressed. Labels (SELECT, BACK, the Controls page's "Jump", "Kick" ...)
are the game's text and stay.

**The Controls page has two versions**, chosen with the set (so by the device in use):

- *A pad* (Xbox and PlayStation sets). Its lines end where the game writes the labels. Black and
  White are the bumpers on a PC pad: the two labels have the bumper's picture beside them instead
  of a line across the whole controller. A rebound pad button shows the same way: a label whose
  button the player has put somewhere else (`pad_<Action>`) loses its line and gets the picture of
  the button it is on now.
- *Keyboard and mouse* (keyboard set; `build_keys_page`). A keyboard and a mouse in the place of the
  pad, with every key and mouse button that does something light and named, and beside each of the
  game's labels the keys the action is on now: both of them when it has two ("Punch": `E` and the
  left mouse button), four as a cluster for Move, Weapon Select and (without mouse look) Camera.
  With mouse look on, Camera's line goes to the mouse. It reads the bindings (`key_<Action>`,
  `mouse_look`), so a rebound key shows beside its label and lights up on the keyboard.
  Keys that are not on the drawn keyboard (the number pad, `Mouse4` / `Mouse5`) still show beside
  the label. Inventory shows its first key only (no room for two beside Target's).

So the page, the menu buttons and the tips all show what is bound.

### The tips' sentences with keyboard and mouse

A tip is a sentence from a text table in the level's files with marks where pictures go, written
for a pad: "Pull the (L) trigger", "Move the (R) thumbstick", "press the (Y) button". With the
keyboard set the pictures are keys, so the sentences are reworded too: "Press (Shift)", "Move the
(mouse)", "Use (WASD)", "press (Q)". Pads keep the game's wording.

This is done on the strings in memory, never in the game's files: when the game has read its table
(`TTipTable`, made at a level's first tip, reached through the tip window at `0x7F18B8`), each
sentence is passed through a few rules (`k_rule` in `btvs_input_glyphs.c`: "Pull [and hold] the ?
trigger", "press the ? button", "Move [the] ? thumbstick"). A sentence never gets longer; what it
loses is filled with spaces at its end. 11 of the first level's 56 tips change (the log names each
with its new sentence): `MoveCamera`, `Grapple`, `FA_PushUse`, `PickupStake`, `ReaperWing`,
`StalkMove`, `Stalk`, `PickupKey`, `ZoomCrossbow`, `Throwing`, `AimCrossbow`.
`BTVS_GLYPH_NO_TIP_TEXT=1` leaves them alone.

Left as they are: "D-pad" and "pad" in the weapon-selection tips, "Start button on other
controllers", and the journal's own pages (another text, not looked at).

**The set is chosen once per start of the game's process**: the texture pack is read once. The game
starts a new process for every level it loads, so the pictures follow a change of device, or a
rebinding, from the next level load (or the next start).

### Test switches

| | |
|---|---|
| `BTVS_CONTROLS_BUTTON_PICTURES=xbox` | (the settings override) the set for this run |
| `BTVS_GLYPH_DIR=<folder>` | the folder instead of `button_pictures` beside the exe (two runs with different sets must not share one) |
| `BTVS_GLYPH_DUMP=1` | the runtime also writes every texture the game draws to `<folder>\dump` as PNG, named with the hash: how the textures above were found, and how to find more |
| `BTVS_GLYPH_NO_TIP_TEXT=1` | keyboard set: the tips keep the game's wording |
| `BTVS_GLYPH_TIP_DELAY=<reads>` | the rewording that many pad reads late (how the limit below was found) |
| `BTVS_GLYPH_SHOW_TIPS=<first>,<every>,3` | keyboard set, in a level: the reworded tips put on screen one after the other through the game's own `ITipWindow::ShowTip`, the first at `<first>` seconds, the next every `<every>` seconds (the last number is the tip's kind; under 3 the game's own tip wins). For frames of sentences a scripted run cannot walk to |
| `BTVS_GLYPH_RAMDUMP=<seconds>,<file>` | the game's first 64 MB of memory to a file at that time |

To replace another texture: dump, find it, add its hash and what to draw to `k_menu` (or a new
table) in `btvs_input_glyphs.c`, raise `GLYPH_VERSION`.

### What was looked at, and what was not

In hidden runs (21 Nov), frames dumped by the game, the game's own art against the port's
(`port/runs/shots/btn_*` in the main checkout):

- 640x480 and 1920x1080 (a 1440x1080 picture): the main menu, Options, the Controls page, the first
  level's camera tip, an inventory screen, the pause menu, in all three sets.
- 3840x2160 (a 2880x2160 picture), Xbox set: main menu, Controls page, the tip. Edges and letters
  are clean at that size.
- All 16 textures were swapped, the six pressed looks included (the log names each hash; the run
  pressed A, B, X, Y and both triggers on an inventory screen). In frames the pressed look was
  caught for A only (a filled, glowing disc that fades back in about a third of a second); the
  others flash between two dumped frames. `btn_pressed_pictures_all_sets.png` is the pictures
  themselves.
- Rebinding: with `pad_B = Y`, `pad_Y = B`, `pad_LeftTrigger = LB`, `pad_Black = LT` the Controls
  page shows those buttons beside "Jump", "Use", "Look" and "Ammo Swap" and the menu shows Y where
  B was; with `key_A = Enter`, `key_B = Tab` the keyboard set shows `Enter` and `Tab`.
- The keyboard wording of the camera tip, at both sizes.
- All eleven reworded tips on screen at 1080p, keyboard set (`btn_kbm_tip_<name>_1080p.png`, and
  `btn_kbm_tips_reworded_all_1080p.png` for the eleven in one picture). The camera tip came up by
  itself; the other ten were put up with `BTVS_GLYPH_SHOW_TIPS` (the game's own tip window and
  text, but not at the place in the level where the game shows them). Each reads as a sentence.
  Two things seen: where a sentence ends with a picture, the full stop after it sits under the
  picture's edge ("... hold (F) and press (Q)"), and the four-key picture for "use (WASD) to aim"
  is small in a line of text at 1080p (it has one letter's room).
- The keyboard-and-mouse Controls page at 640x480 and 1080p with the default keys
  (`btn_kbm_controls_page_*`; `..._compare_1080p.png` is the pad page it replaces beside it), and
  with `key_A = Enter`, `key_Start = P`, `key_X = MouseLeft`, `key_LeftTrigger = Tab`,
  `mouse_look = 0`: the new keys are beside "Kick", "Pause", "Punch" and "Look", lit on the
  keyboard, and Camera has its four keys instead of the line to the mouse.

Not seen: the journal's pages; a tip with A / B / X / Y, an arrow, "+" or White / Black in it (same
page of pictures as the two that were seen; whether each sits well in its line was not looked at);
the reworded tips at 640x480 other than the camera one; the keyboard Controls page with the game's
Configuration 2 or 3 chosen; `auto` with a real pad, since no run has one (so the change from one
Controls page to the other was seen only by choosing the set); rebinding through the menu rather
than the settings. One limit found: a tip that is already on screen when its sentence is reworded keeps the old wording until it
is shown again (in every run the rewording came first). Not replaced: the inventory's 3-D button
pieces (`H_Inv_Button ...` in the game's HUD), if they are ever shown; no run met them.

## Verified / not verified

**In the running game** (hidden, muted test runs through `port/tools/run.sh`; no window, no focus,
no hardware; menus 20 Nov 2026, the first level 21 Nov):

- The XAPI replacements: the game calls `XInitDevices`, `XGetDevices`, `XInputOpen`,
  `XGetDeviceChanges` and `XInputGetState`, gets a pad on port 1 and runs on; the stack handling is
  right (thousands of reads per run, no crash).
- **Every pad input through the script path** (`all_inputs.txt` with `BTVS_INPUT_VERIFY=1`): A, B,
  X, Y, Black, White, both triggers (full and part values), Start, Back, both stick clicks, the
  four d-pad directions and a diagonal, both sticks in all four directions and at part deflection.
  Each was stored by the game with the expected value; values under the game's own thresholds
  (analog 30, stick dead zone) were stored as nothing. 29 of 29, no mismatch.
- **The keyboard and mouse-button mapping** (`all_keys.txt`): each of the 29 default keys held by the
  script went through the bindings table and the same per-read function, and the game stored the
  input it is bound to. The key *reading* itself (`GetAsyncKeyState`) is not part of this.
- **Bindings and options from the settings file**: with `key_A=K`, `key_LookDown=Num5`, `invert_y=1`
  in the file, Space no longer gave A, K gave A, Num5 looked up (inverted), I looked down.
- **Menus driven by a script**: `menu_path.txt` went title → main → New Game → back → Load → back →
  Credits → back, d-pad and left stick both moving the selection, waiting on the menu marks.
- **Recording then playback** of that run: same pad changes, same menus, same order (above).
- The menu id of the game-state probe (above).
- **A script from boot into the first level** (`boot_to_level.txt`, then the level's own script in
  the run after the game restarts itself).
- **Every kind of control in the level** (`level_controls.txt`, `BTVS_INPUT_VERIFY=1`: 54 pad values,
  no mismatch; effects read from the player's position and frame dumps):
  - Start skips the opening cutscene;
  - left stick: forward, back, left and right each move her the right way (forward is -z at the
    start, left is +x);
  - right stick: a second held left turns the camera (frame dumps), and forward then moves her
    the other way along z;
  - B jumps (66 units up and down in a second), A and X strike and step her forward, Y does
    nothing where she stood; all four are named as the game's own tips name them;
  - triggers at part values reach the game as part values (LT 128 as 0.502, RT 64 as 0.251);
  - White and Black each open an inventory screen, B closes it;
  - Start opens the pause menu and Start closes it (`game: pause screen open` / `closed`);
  - **the mouse as the right stick**: a scripted mouse movement of 25 counts a read gave the game a
    full right stick and turned the camera (forward on the keyboard, `K:W`, then moved her back along
    -z); the same movement while paused gave the game nothing.
- **The player object and the pause screen** of the game-state probe: `level` fires when the level
  has loaded, `pause` / `resume` when the pause menu opens and closes.
- **Rumble, as far as a hidden run goes**: the game calls `XInputSetState` (the replacement is
  reached and returns what the game expects): once, with both motors at 0, when a level starts.
  Nothing in the first level's opening made it ask for more: 90 s of running, jumping, climbing and
  striking the air through the tutorial ledges. No fight was reached (see "Waiting").
- **Recording a walk and playing it back** in the level: see "How deterministic is it?".

Two faults these runs found, both fixed: the menu id stays 9 after the pause menu closes, so mouse
look stayed off for the rest of the level after the first pause; and a script press whose whole
time fell into a hitch of the game was never given.

The game opens port 1 twice at start (`XInputOpen port 1 -> open` is logged twice); both get the
same handle and it does no harm.

**Self-test** (`btvs_input_test.exe`, made-up clock and controller):

- controller → Xbox pad mapping (analog triggers, Black/White on LB/RB, digital buttons, stick
  dead-zone mapping onto the game's);
- devices ignored without focus while port 1 stays connected;
- script parsing and timing: seconds and frames, `+`, `for`, `every`, `after` marks, player 2, keys;
- recording → playback gives identical pads read for read even when the clock stumbles; recording
  size (changes only);
- capturing a controller binding, the resting pad during capture, save / reload / reset;
- scripted mouse movement as the right stick, and not while the game says the mouse is not the
  camera; a mark reached during playback; playback by time from the first read across a longer
  load and a stall;
- **rumble routing**: a rumble for the player whose controller it is goes to that controller with
  the values the game gave; one for a player without a controller goes nowhere; it stops when the
  window loses focus and comes back with it. (In the game each send is logged:
  `[INPUT] rumble -> Xbox controller 1 (XInput, player 1): left N right N`.)

Also run by hand on the dev PC: `btvs_input_test live 3` loads `xinput1_4.dll`, finds and starts
`SDL3.dll` (every function the backend uses resolves). No controller was plugged in.

**Waiting** (not verified, and why):

- **Rumble in a fight.** No fight has been reached by script: the first level opens with a
  tutorial of ledges, and scripted runs got through it but not to an enemy. When the game asks
  for rumble the log shows `[INPUT] rumble p1 left N right N` (the game's call) and, with a
  controller and a focused window, `rumble -> ...` (what was sent to it).
- **Y, and the triggers' effect in play**: they reach the game with the right values; nothing was
  there for them to act on.
- The resurrect screen (dying) and whether the pause screen's flag covers it; the inventory screens
  do not set that flag, so the mouse stays captured in them.
- Cutscenes: the mouse stays captured during them.

## The owner's checklist

Everything above ran without a window, a controller or a keyboard. These need the game on screen
(start it with the launcher's **Play**) and take about ten minutes. For anything that goes wrong:
pause, **Report a Problem**, one line saying which step.

1. **Xbox pad** (plug it in before starting). In the first level: the left stick walks, the right
   stick turns the camera both ways and up and down, B jumps, A and X strike, both triggers do
   something, Start pauses, the bumpers (LB / RB) open the inventory screens, the d-pad moves
   through menus.
2. **Rumble on the Xbox pad.** Get hit by the first enemy, or hit one: the pad should shake, and
   stop shaking when the hit is over. Then Options -> PC Controls -> Rumble off: no shaking.
   Alt+Tab away in mid-shake: it stops.
3. **DualSense through SDL.** Unplug the Xbox pad, plug in the DualSense (USB first, then
   Bluetooth if you use it), start the game. Same checks as 1 and 2 (Cross is A, Circle is B,
   L1 / R1 are the bumpers, Options is Start). Key Bindings should name the pad's buttons.
4. **Hot-plug.** In a level, unplug the pad: the game should not crash, and the keyboard should
   still work. Plug it back in: it works again within about two seconds, without a restart.
   Then plug in a second pad: the first keeps player 1.
5. **Keyboard and mouse.** W A S D walk, the mouse turns the camera (too fast or too slow? note
   which; Options -> PC Controls -> Sensitivity changes it), Space is A, E or left click is X,
   Q or right click is Y, Backspace is B (jump), Enter is Start, Z and C are the bumpers. The
   cursor is hidden while playing, and free in the pause menu and after Alt+Tab.
6. **Rebinding a key.** Options -> PC Controls -> Key Bindings: choose "B", press A, press the key
   you want for jump. Back in the level that key jumps. Quit the game, start it again: it still
   does. Then Reset Bindings: Backspace jumps again.

7. **Button pictures.** With the Xbox pad plugged in, start the game: the menus show coloured
   A B X Y discs (not the old purple medallions), Options -> Controls shows a modern controller,
   and the first level's tip shows a stick and `LT`. Start again with the DualSense instead:
   cross / circle / square / triangle and `L2`. Start again with no pad: key names (`Space`,
   `Bksp`, `E`, `Q`), and Options -> Controls shows a keyboard and a mouse with your keys lit
   instead of the controller. Then with a pad plugged in, play a moment on the keyboard and load a level:
   the pictures should be the keys now, and the first tip should read "Move the (mouse) to move
   the camera around. Press (Shift) to reset." Rebind a pad button or a key in Key Bindings, load
   a level: the menus, the tips and the Controls page show the new one. Say which pictures look
   wrong, too small or too big.

Not expected to be right yet: the default keys are the Chaos Bleeds port's and were chosen before
this game could be played (jump on Backspace is the obvious one to change).

## Not done

- Keyboard defaults that suit this game (see the end of the checklist).
- The mouse is still captured during cutscenes and in the inventory screens.
- Button pictures, and with them the Controls page (pad or keyboard and mouse), change set only at
  a level load or a start, not the moment another device is touched: the runtime's texture pack is
  read once per process. Asked of Graphics in `docs/AREAS.md` (a way to read it again).
- With keyboard and mouse the weapon-selection tips still say "D-pad", and the journal's text is
  not reworded.
- A menu line for `button_pictures` (asked of Menu in `docs/AREAS.md`).
- `[INPUT]` lines go to stderr (which a player's run redirects into the log); calling Logs'
  `btvs_log` directly would be tidier.
