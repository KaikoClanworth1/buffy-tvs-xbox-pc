# Install, packaging, releases (Extras)

The player's flow: open **Buffy Launcher.exe** → choose the disc image → choose a folder →
**Install** → **Play**. Everything else is set in the game, so the launcher has nothing else:
Install, Play, "Reports folder", "Game folder", "Install again", and the version.

| File | What |
|---|---|
| `port/launcher/btvs_launcher.c` | the launcher (`buffy_tvs_launcher` target → `Buffy Launcher.exe`) |
| `port/launcher/btvs_release.h` | which `Default.xbe` this build is made from (title, region, version, size, SHA-256) |
| `port/launcher/Read Me.txt` | the player's read-me, shipped in the zip and copied into the game folder |
| `port/src/btvs_xbecheck.c` | the game's own check of the installed `Default.xbe` at start |
| `port/tools/deploy.sh` | a playable `GAME/` folder from the build + `game_files/` |
| `port/tools/package.sh` | the release zip, checked to hold no game data |

## What "installed" means

A game folder is:

```
<folder>\
  Default.xbe, DVD.txt, final.map, install.dat     the disc's root, as on the disc
  pak\  media\  interfc\  dialogue\  Sound\         the disc's folders, as on the disc
  buffy_tvs.exe, buffy_tvs.pdb                      the PC program (copied from beside the launcher)
  Buffy Launcher.exe, Read Me.txt                   so the folder stands on its own
  SDL3.dll, vcruntime140*.dll, msvcp140.dll         when they ship beside the launcher
  btvs_settings.ini                                 written by install only when missing (first-start defaults)
  XboxData\                                         made by the game: HardDisk\ (saved games, the game's own
                                                    settings), Cache\ (its 105 MB install); never touched by install
  btvs_log*.txt, btvs_trail*.txt, btvs_input.rec,   written by the game (Logs); never touched by install
  reports\, crashes\, replay\, ShaderCache\, xbox_kernel.log
  btvs_console.txt                                  the game's stdout and stderr, new at each Play (see "Play")
```

- **Saves**: the owner's rule was `SaveData\` beside the exe; the game as built keeps its hard disk in
  `XboxData\` beside the exe (`btvs_log.c`: `btvs_save_dir()`), so that is the name the launcher, deploy
  and the Read Me now use. Renaming it is one line in the Logs area; nothing in Extras depends on the name
  beyond `SAVE_DIR` in the launcher.
- **Default settings**: a missing `btvs_settings.ini` is written with `[display] mode=1 width=0 height=0
  vsync=1` (borderless at the desktop's size) and `[audio]` volumes at 100. Those are also what the game
  uses with no file at all; an existing file is the player's and is never rewritten.

- The launcher treats a folder as a game folder when it has `Default.xbe`, `pak\` and `media\`;
  Play is enabled when `buffy_tvs.exe` is there too.
- **Nothing is converted at install time.** The disc's files are copied as they are (152 files,
  2,007,716,654 bytes for the supported release). Whether the logo movies (`interfc\*.wmv`), music
  (`Sound\Music\*.wma`) or dialogue (`dialogue\*.rpe`) need a one-time conversion is asked of
  Graphics and Audio in `docs/AREAS.md`; if one is needed it becomes a step after the copy in
  `install_run()`.
- The disc's `final.map` (the linker map) is copied too: it is on the disc, and crash reports can
  use it for guest function names.
- Installing over an existing install rewrites the disc's files and the program, and leaves
  everything else alone (saves, settings, logs, reports).
- `launcher.ini` beside the launcher remembers `GameFolder` and `DiscImage`. A launcher sitting
  in a game folder needs no ini. With a game folder known, the launcher opens on Play.
- Choosing a folder that already has other things in it installs into a
  `Buffy the Vampire Slayer` subfolder of it.

### Safety

- The image is opened `GENERIC_READ` only; nothing writes to it.
- File names in the image that could escape the install folder (`..`, a slash, a colon) make the
  launcher refuse the whole image as damaged.
- Free space is checked before copying (what is missing or of a different size, plus 64 MB).
- A cancelled or failed copy deletes the file it was writing.

## Releases

The game's translated code is compiled into `buffy_tvs.exe`, so an exe belongs to exactly one
`Default.xbe`. This build:

| | |
|---|---|
| Title ID | `0x45410012` (EA-018), "Buffy: The Vampire Slayer" |
| Certificate region | `0x4` (rest of world: the European / PAL disc) |
| Certificate version | 2 |
| `Default.xbe` | 7,819,264 bytes |
| SHA-256 | `f91cd33c980f6b5514aa98925397338ee1de6e7b432dd393efd470c0f0702f40` |

Both the launcher (before installing, and it will not install another release) and the game (at
start, `btvs_xbe_check`) compare the whole file's SHA-256 and say which case it is:

- another game (Chaos Bleeds is named specifically),
- another release of this game (region and version are shown),
- the right release but a changed file (patched, trained, or a bad dump).

`BTVS_ANY_XBE=1` makes the game warn and carry on (development only). With `BTVS_NO_WINDOW` set
the message goes to stderr only, with no dialog.

**Other releases exist and are not supported.** The game was also released in North America
(region `0x1`); that disc's `Default.xbe` is, as far as anyone here knows, a different build with
its code at other addresses (it has not been seen; the hash check settles it either way).
Supporting it needs its own regen and its own exe from that `Default.xbe` (and its `final.map`,
if that disc carries one), plus a second set of values in `btvs_release.h` — the Chaos Bleeds
port does this with a second exe (`buffy_chaos_bleeds_usa.exe`) and the launcher picking between
them. Nothing of that is done here; only the owner's disc has been seen. To identify any image or
folder:

```bash
"port/build/Release/Buffy Launcher.exe" --identify "<image, game folder or .xbe>"
```

After a regen from a different `Default.xbe`, put the printed values in `btvs_release.h`.

## Play

The Play button (and `--play`) starts `<game folder>\buffy_tvs.exe` with the game folder as its working
directory and waits for it; a non-zero exit code is shown with where to look.

**The game is given a real stdout and stderr**: `btvs_console.txt` in the game folder, made new at each
start. This is a workaround for a fault found on 21 Nov: started with no standard handles at all, which is
how Explorer and a plain `CreateProcess` start a window program, the game ends itself about a second into
the boot with exit code `0xC0000409`, at the point where sound starts ("logo movies" is the last log line;
nothing the runtime prints reaches `btvs_log.txt` in such a run). Seen twice through the launcher before
the workaround, never in runs with stdout/stderr redirected. So **a double-click on `buffy_tvs.exe` itself
is expected to fail** until that is fixed in the game (asked of Logs / Audio in `docs/AREAS.md`).

## The launcher without a window

For tests and scripts. Nothing is shown and no focus is taken.

| Command | Does | Exit code |
|---|---|---|
| `--install <image> <folder> [--quiet]` | the same install as the button | 0 installed; 2 game files installed but no `buffy_tvs.exe` beside the launcher; 1 failed (reason on stderr) |
| `--play [game folder]` | what the Play button does (folder from `launcher.ini`, else the launcher's own), then waits; the game inherits the environment, so `BTVS_NO_WINDOW` / `BTVS_MUTE` reach it | the game's exit code; 1 if there is no installed game |
| `--identify <image \| folder \| xbe>` | which game and release | 0 supported, 1 not |
| `--list <image>` | `<bytes>\t<path>` for every file | 0 |
| `--capture <install\|busy\|error\|play> <out.bmp> [game folder]` | the window's picture, drawn off screen | 0 |
| `--version` | version and supported release | 0 |

## deploy.sh and package.sh

- `bash port/tools/deploy.sh` → `GAME/` in the main checkout (ignored by git; `BTVS_DEPLOY_DIR`
  overrides): the newest `buffy_tvs.exe`, `.pdb` and launcher (this checkout's build, else the
  main checkout's), `SDL3.dll`, the Read Me, a default `btvs_settings.ini` when there is none, and
  the disc's files **hard-linked** from `game_files/` (only missing ones, so reruns are instant).
  `install.dat` and `Sound\dsstdfx.bin` are **copied** instead: the game opens them unshared, and a
  linked copy is the same file as the one a test game from `game_files/` has open (seen: the copy
  itself failed while a test game ran). Save folders, settings and logs are never linked or touched.
  Exits 2 when a program is not built yet, 3 when one of the two copies could not be made because a
  running game held the file (run it again).
- `bash port/tools/package.sh` → `dist/Buffy the Vampire Slayer PC/` and
  `dist/Buffy-the-Vampire-Slayer-PC-v<version>.zip` (`BTVS_VERSION` in `port/CMakeLists.txt`):
  launcher, game exe, pdb, Read Me, and the VC++ runtime DLLs. The zip is then checked — only
  those file names, nothing with a disc file's name or type, nothing with the same content as a
  file in `game_files/` — and deleted if the check fails. `--allow-missing-game` packages without
  `buffy_tvs.exe` to test the script; that zip is not a release.

## Not done

- **Updates**: no update check or self-update yet (Chaos Bleeds' `launcher_update.inc` is the
  model, once there are releases to update from).
- **An icon** for `Buffy Launcher.exe` and `buffy_tvs.exe` in Explorer. The launcher's window
  and header use the game's own title image from the installed `Default.xbe`; no art is built in.
- **Other releases** of the disc (above).
- **Install-time conversions** (above): none known to be needed yet.

## What was checked on 21 Nov 2026 (the game boots: Play made real)

All with the window hidden, the sound muted and the test CPUs (`BTVS_NO_WINDOW=1 BTVS_MUTE=1
BTVS_AFFINITY=FF00 BTVS_EXIT_AFTER=<s>`, `BTVS_SKIP_MOVIES=1`), one test-game slot at a time, and
**no `BTVS_GAME_DIR`**: the game used only the folder its exe was in.

- **`deploy.sh` → `GAME/`** in the main checkout: launcher, `buffy_tvs.exe`, `.pdb`, `SDL3.dll`, Read Me,
  default `btvs_settings.ini`, 150 disc files hard-linked and 2 copied. The first try failed to copy
  `Sound\dsstdfx.bin` because another session's test game had it open unshared; the script now says so
  and exits 3, and a rerun after that game ended finished it.
- **The deployed exe started from `GAME/`** (first start, 90 s): it read `GAME\btvs_settings.ini`, logged
  `game folder: ...\GAME` and `save folder: ...\GAME\XboxData`, installed its cache to
  `GAME\XboxData\Cache` and reached the title screen in 8.5 s. Exit code 0.
- **A second start from `GAME/`** with a scripted Start button: title at 9 s, **main menu at 10 s**, then
  Start Game → difficulty → the game restarted itself for the first level and ran to the time limit.
  Exit code 0.
- **The launcher's Play path** (`"GAME\Buffy Launcher.exe" --play`, run from another working directory, no
  `launcher.ini`): it found the game folder as its own, started `GAME\buffy_tvs.exe` with `GAME` as the
  working directory (the game's log names that exe, settings file, game folder and save folder), waited
  and returned the game's exit code. The first two tries **ended after one second with `0xC0000409`**
  (see "Play"); with `btvs_console.txt` as the game's stdout/stderr it ran to the title screen (frame dump
  looked at: the title with "Press START") and exited 0.
- **The whole player path in a scratch folder**: `"GAME\Buffy Launcher.exe" --install <the real xiso>
  D:\_btvs_install_test\Buffy` → **exit code 0** in 44 s; all 152 disc files there at the right sizes
  (contents were compared by SHA-256 on 20 Nov), plus `buffy_tvs.exe`, `.pdb`, `SDL3.dll`, launcher, Read
  Me and the default `btvs_settings.ini`. Then `--play` from that folder: first-start cache install,
  title at 6.6 s, main menu at 7.8 s, Start Game, difficulty, restart into the first level, exit code 0.
- **Installing over an existing install**: with a hand-changed `btvs_settings.ini` and a marker file in
  `XboxData\HardDisk\UDATA`, a second `--install` into the same folder exited 0 and left both exactly as
  they were. With the settings file deleted, a third install wrote the default one again.
- **`package.sh`**: `dist/Buffy-the-Vampire-Slayer-PC-v0.0.1.zip`, 47 MB, 8 files (launcher, game exe,
  pdb, `SDL3.dll`, Read Me, three VC++ runtime DLLs); its check passed: no disc file name, type or content.

Not checked on 21 Nov, in addition to the list at the end:

- **Anything on a real screen or through real speakers.** The hidden runs log `display: 640x480,
  windowed`: with `BTVS_NO_WINDOW` the borderless-at-desktop-size default is not exercised at all. That
  the first visible start comes up borderless at the desktop's resolution with sound is what the settings
  say, not something seen.
- **The Play button itself** (the click, the "game is running" state, the message after a failed run):
  `--play` runs the same `start_game()` but not the window code around it.
- **A double-click on `buffy_tvs.exe`**: expected to fail (see "Play"); not tried, since it would show a
  window.
- The scratch folder `D:\_btvs_install_test` could not be deleted by the session (the tool refused the
  removal) and was left for the owner to delete.

## What was checked on 20 Nov 2026 (first round)

Checked, with the real image (`Iso/Buffy the Vampire Slayer.xiso.iso`, a bare xiso, 2,008,481,792
bytes), all without showing a window:

- `--install <image> D:\_btvs_install_test\Buffy`: all **152 files** installed; compared with
  `game_files/` (unpacked earlier by another tool) by path, size and **SHA-256: identical**,
  2,007,716,654 bytes. The launcher copied in. The image's size and
  modification time were unchanged afterwards. Exit code 2, as designed, because no
  `buffy_tvs.exe` existed beside the launcher yet. It took 6 min 38 s on a disk that other builds
  were using at the time. The scratch folder was deleted afterwards.
- `--identify` on the image and on `game_files/`: the supported release, with the values above.
- Refusals, each with exit code 1, a clear message and nothing created on disk: an image that is
  not there; a file that is not an Xbox image; the Chaos Bleeds disc image (named as the sequel).
- `--capture` of all four states (install, busy, wrong release, play): layout and text read
  correctly at 96 dpi; the header icon is decoded from `Default.xbe`'s `$$XTIMAGE`.
- The launcher builds with no warnings.
- `btvs_xbe_check` in the game exe (hidden, muted test runs): with `game_files/` the game goes on
  past the check; with a copy of `Default.xbe` that has one bit changed it prints "the right
  release ... changed or damaged" with both hashes and exits with code 1.
- `deploy.sh`: made `GAME/` in the main checkout with the main checkout's `buffy_tvs.exe`/`.pdb`,
  the launcher, the Read Me and all 152 disc files as hard links (link count 2); a
  second run linked 0. A test run's `UDATA\` in `game_files/` was correctly left out.
- `package.sh`: made `dist/Buffy-the-Vampire-Slayer-PC-v0.0.1.zip` (45 MB: launcher, game exe,
  pdb, Read Me, three runtime DLLs); its check passed ("7 files, no game data"). The check's
  failing side (a disc file slipped into the folder) was not exercised. **That zip is not a
  release**: the game in it does not boot yet.

Not checked:

- **The window with a real mouse and keyboard** (Browse dialogs, hover, Tab/Enter, Cancel during
  a copy, closing during a copy, a DPI change). Test runs must not take focus, so only the
  drawing and the install code under it were exercised.
- The "game closed with an error" message in the window. (Play starting the game, exit code 0 of
  `--install` and installing over an existing install were checked on 21 Nov, above.)
- Installing from an unpacked release zip (the three VC++ runtime DLLs copied along): the 21 Nov
  install ran from `GAME/`, which has no runtime DLLs beside the launcher.
- **A full disc image** (game partition at an offset, as redump images have): the offsets are the
  Chaos Bleeds launcher's, where they are proven; this game's only image here is a bare xiso.
- **Another release** of this game: the message was drawn from a made-up certificate
  (`--capture error`), not from a real North American disc.
- The `btvs_xbe_check` dialog itself (test runs suppress it; only the stderr text was seen).
- The not-enough-space and disk-full paths.
