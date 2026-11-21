# Buffy the Vampire Slayer (Xbox, 2002) for Windows

A native Windows port of the 2002 Xbox game by The Collective, made by **static recompilation**:
the game's own x86 code is translated to C and compiled for Windows, and the Xbox libraries it
used (Direct3D 8, DirectSound, controller input) are replaced by PC code. There is **no emulator**
inside.

**This repository contains no game material.** No disc image, no game files, no game code. You need
your own Xbox disc of the game, as an image file; the launcher installs the game from it.

> **Status: in development (21 Nov 2026).** The game starts, plays its logos and intro, shows its
> menus and loads the first level. So far all of that has been checked only by automated runs
> with the window hidden and the sound off: **nobody has played it yet** with a visible window,
> speakers and a real controller. Expect faults.

## What works today

"Checked hidden" means: seen in automated test runs (window hidden, sound muted, scripted
controller) through the game's own frame dumps, sound dumps and logs. Each area's notes in
`docs/` list exactly what was and was not checked.

| | State |
|---|---|
| Launcher: choose the disc image, choose a folder, install, Play | **Works** in windowless test runs: install checked against the real disc image, and Play starts the game in its folder ([docs/install.md](docs/install.md)). The window itself has not been used by hand |
| Checks that the image is this game and the supported release | **Works** |
| Boot: logos, intro movie, title, menus, new game, first level | Checked hidden ([docs/graphics.md](docs/graphics.md)) |
| Graphics: the game's Direct3D 8 drawn natively through Direct3D 11, no emulation | Checked hidden in the menus and the first level, at 640x480, 1080p and 4K sizes; 60 FPS once a level has loaded. Later levels not looked at |
| Borderless / fullscreen / windowed, VSync | Wired to the settings; **never seen on a real screen** |
| Widescreen | Picture shape only (4:3 with bars, or stretched). A wider view is planned |
| Sound (XAudio2: music, effects, movie sound) | Checked from sound dumps of muted runs; **never listened to**. Breaks up when the game hitches ([docs/audio.md](docs/audio.md)) |
| Controllers (XInput, others through SDL3), keyboard and mouse, rebinding | Checked hidden with scripted input; **no real controller, key or mouse has been tried** ([docs/controls.md](docs/controls.md)) |
| Options in the game's own menus: Graphics, PC Audio, PC Controls, Memory, Exit | Checked hidden ([docs/menu.md](docs/menu.md)) |
| Report a Problem (Options and pause menu; F12) | Checked hidden: saves a report with picture, log, what you did, settings, input recording ([docs/logs.md](docs/logs.md)) |
| Memory view (RAM, VRAM, the game's heap) | Checked hidden |
| Starting `buffy_tvs.exe` by itself (not through the launcher) | Works (checked hidden); it ended itself a second after starting in builds before 21 Nov 03:25 ([docs/install.md](docs/install.md)) |
| Update check, icons for the exes in Explorer | Planned |

## What you need

- Windows 10 or 11, 64-bit
- Your own **Xbox** disc of *Buffy the Vampire Slayer* (2002) as an image file (`.iso` / xiso).
  Both a bare xiso and a full disc image work.
- The **European (PAL) release, version 2**. This build is made from that release and runs only
  that one; the launcher tells you if your image is another release (or another game). See
  [docs/install.md](docs/install.md#releases).
- About 2.2 GB of disk space

## Install and play

1. Unpack the release zip anywhere.
2. Start **Buffy Launcher.exe**.
3. **Browse** to your disc image. The launcher says whether it is the right game.
4. **Browse** to where the game should go, and press **Install**. Your image is only read, never
   changed.
5. Press **Play**.

Next time the launcher opens straight on Play. Your saved games live in `XboxData\HardDisk\` in
the game folder; installing again or updating never touches `XboxData\` or your settings.

## Settings and controls

Everything is set **inside the game**, in its own Options menu: Graphics (display mode,
resolution, VSync, picture shape, FPS limit), PC Audio (volumes), PC Controls (sensitivity,
invert, rumble, key bindings), Memory, Report a Problem. The launcher has no settings on purpose.
The game first starts borderless at the desktop's resolution with sound on
(`btvs_settings.ini` in the game folder holds the settings).

Default controls (provisional; rebind in Options → PC Controls):

| Xbox pad | PC pad | Keyboard / mouse |
|---|---|---|
| A | A / Cross | Space |
| B | B / Circle | Backspace, middle mouse |
| X | X / Square | E, left mouse |
| Y | Y / Triangle | Q, right mouse |
| Black / White | LB / RB | Z / C |
| Triggers | LT / RT | Left Shift / F, Right Shift |
| Start / Back | Start / Back | Enter / Escape |
| D-pad | D-pad | arrow keys |
| Left stick | left stick | W A S D |
| Right stick | right stick | I J K L, and the mouse |

Alt+Enter or F11 switches fullscreen; F12 (or a click of the left stick) makes a problem report.
To leave the game: main menu → Exit.

## Building from source

You need Visual Studio Build Tools 2019 (MSVC x64), CMake 3.20 or newer, Git Bash, Python 3.12
with `capstone`, and your own disc unpacked into `game_files/` (`Default.xbe`, `final.map`,
`pak/`, `media/`, ...).

```bash
bash port/tools/regen.sh
```

```bash
bash port/tools/build.sh
```

```bash
bash port/tools/deploy.sh
```

```bash
bash port/tools/package.sh
```

- `regen.sh` translates `game_files/Default.xbe` to C in `port/src/recomp/gen/` (takes minutes;
  the result is never committed).
- `build.sh` builds `buffy_tvs.exe` and `Buffy Launcher.exe` in `port/build/Release/`.
- `deploy.sh` makes a playable `GAME/` folder: the build plus the disc's files, hard-linked from
  `game_files/`, and a default `btvs_settings.ini` if there is none. Start
  `GAME/Buffy Launcher.exe` and press Play.
- `package.sh` makes the release zip in `dist/` (launcher, game exe, pdb, Read Me) and checks that
  nothing from the disc is in it.

The game's translated code is compiled into `buffy_tvs.exe`, so the exe belongs to one release of
the disc. It checks the installed `Default.xbe` at start and refuses another release with a clear
message. A build for another release: [docs/install.md](docs/install.md#releases).

More: [CLAUDE.md](CLAUDE.md) (layout and working rules), [docs/AREAS.md](docs/AREAS.md),
[docs/install.md](docs/install.md).

## Legal

This is an unofficial fan project, not affiliated with or endorsed by the game's developers,
publishers or rights holders. *Buffy the Vampire Slayer* and everything in the game belong to
their owners.

The repository and the release zip contain only the port's own code. They include **no** disc
image, game data, or original game executable; the game cannot be played without your own
legally obtained copy. The recompiler toolkit in `xboxrecomp/` is MIT-licensed.
