# Building from source

For developers. Players don't need any of this: the launcher in a release builds the game for them.

## You need

- Visual Studio Build Tools (MSVC x64), CMake 3.20 or newer, Git Bash, and Python 3.12 with `capstone`.
- Your own disc unpacked into `game_files/` (`Default.xbe`, `final.map`, `pak/`, `media/`, ...). Only the European (PAL) release is supported: see [install.md](install.md#releases).
- The submodules: `git submodule update --init --depth 1`.

## Steps

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

- `regen.sh` translates `game_files/Default.xbe` to C in `port/src/recomp/gen/`. It takes minutes, and the result is never committed.
- `build.sh` builds `buffy_tvs.exe` and `Buffy Launcher.exe` in `port/build/Release/`.
- `deploy.sh` makes a playable `GAME/` folder: the build plus the disc's files. Start `GAME/Buffy Launcher.exe` and press Play.
- `package.sh` makes the release zip in `dist/`, with no game data and no translated game code, and checks that nothing from the disc is in it. How a release builds the game on the player's PC: [install-build.md](install-build.md).

## More

- [CLAUDE.md](../CLAUDE.md): layout and working rules.
- [AREAS.md](AREAS.md): who owns which files.
- [install.md](install.md): the launcher, releases of the disc, deploying and packaging.
