# Buffy the Vampire Slayer (Xbox, 2002) for Windows

A native Windows port of the 2002 Xbox game by The Collective, made by **static recompilation**:
the game's own x86 code is translated to C and compiled for Windows, and the Xbox libraries it
used (Direct3D 8, DirectSound, controller input) are replaced by PC code. There is **no emulator**
inside.

**This repository contains no game material.** No disc image, no game files, no game code. You need
your own Xbox disc of the game, as an image file; the launcher installs the game from it.

> **Status: in development. The game does not start yet.** The launcher and the installer work;
> the game itself is being brought up. The table below says what is real today.

## What works today

| | State |
|---|---|
| Launcher: choose the disc image, choose a folder, install, Play | **Works.** Install is tested against the real disc image (see [docs/install.md](docs/install.md)) |
| Checks that the image is this game and the supported release | **Works** |
| The game's code translated and compiled for Windows | Builds; **does not boot yet** |
| Graphics (native Direct3D 11, any resolution up to 4K, widescreen, 60+ FPS, fullscreen / borderless) | Planned |
| Sound (XAudio2: effects, music, dialogue) | Planned |
| Controllers (XInput and others), keyboard, rebinding | Planned |
| Options in the game's own menus (Graphics, Audio, Controls, Exit) | Planned |
| Report a Problem (in-game, records what you did so the spot can be reached again) | Planned |
| Memory view (RAM, VRAM, the game's heaps) | Planned |

## What you need

- Windows 10 or 11, 64-bit
- Your own **Xbox** disc of *Buffy the Vampire Slayer* (2002) as an image file (`.iso` / xiso).
  Both a bare xiso and a full disc image work.
- The **European (PAL) release, version 2**. This build is made from that release and runs only
  that one; the launcher tells you if your image is another release (or another game). See
  [docs/install.md](docs/install.md#releases).
- About 2 GB of disk space

## Install and play

1. Unpack the release zip anywhere.
2. Start **Buffy Launcher.exe**.
3. **Browse** to your disc image. The launcher says whether it is the right game.
4. **Browse** to where the game should go, and press **Install**. Your image is only read, never
   changed.
5. Press **Play**.

Next time the launcher opens straight on Play. Your saves live in `SaveData\` in the game folder;
installing again or updating never touches them.

## Settings and controls

Everything is meant to be set **inside the game**, in its own Options menu (resolution, window
mode, sound volumes, controls and rebinding). The launcher has no settings on purpose. None of the
in-game pages exist yet; this section will list the controls once they do.

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
  `game_files/`.
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
