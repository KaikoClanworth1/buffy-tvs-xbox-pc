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
  vcruntime140*.dll, msvcp140.dll                   when they ship beside the launcher
  SaveData\                                         the player's saves: created if missing, never touched
  btvs_settings.ini, btvs_log.txt, Reports\         written by the game later, never touched by install
```

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

## The launcher without a window

For tests and scripts. Nothing is shown and no focus is taken.

| Command | Does | Exit code |
|---|---|---|
| `--install <image> <folder> [--quiet]` | the same install as the button | 0 installed; 2 game files installed but no `buffy_tvs.exe` beside the launcher; 1 failed (reason on stderr) |
| `--identify <image \| folder \| xbe>` | which game and release | 0 supported, 1 not |
| `--list <image>` | `<bytes>\t<path>` for every file | 0 |
| `--capture <install\|busy\|error\|play> <out.bmp> [game folder]` | the window's picture, drawn off screen | 0 |
| `--version` | version and supported release | 0 |

## deploy.sh and package.sh

- `bash port/tools/deploy.sh` → `GAME/` in the main checkout (ignored by git; `BTVS_DEPLOY_DIR`
  overrides): the newest `buffy_tvs.exe`, `.pdb` and launcher (this checkout's build, else the
  main checkout's), the Read Me, and the disc's files **hard-linked** from `game_files/` (only
  missing ones, so reruns are instant). Save folders and logs are never linked or touched.
  Exits 2 when a program is not built yet.
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

## What was checked (20 Nov 2026)

Checked, with the real image (`Iso/Buffy the Vampire Slayer.xiso.iso`, a bare xiso, 2,008,481,792
bytes), all without showing a window:

- `--install <image> D:\_btvs_install_test\Buffy`: all **152 files** installed; compared with
  `game_files/` (unpacked earlier by another tool) by path, size and **SHA-256: identical**,
  2,007,716,654 bytes. `SaveData\` created, the launcher copied in. The image's size and
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
  the launcher, the Read Me, `SaveData\` and all 152 disc files as hard links (link count 2); a
  second run linked 0. A test run's `UDATA\` in `game_files/` was correctly left out.
- `package.sh`: made `dist/Buffy-the-Vampire-Slayer-PC-v0.0.1.zip` (45 MB: launcher, game exe,
  pdb, Read Me, three runtime DLLs); its check passed ("7 files, no game data"). The check's
  failing side (a disc file slipped into the folder) was not exercised. **That zip is not a
  release**: the game in it does not boot yet.

Not checked:

- **The window with a real mouse and keyboard** (Browse dialogs, hover, Tab/Enter, Cancel during
  a copy, closing during a copy, a DPI change). Test runs must not take focus, so only the
  drawing and the install code under it were exercised.
- **Play** actually starting the game, and the "game closed with an error" message: there is no
  working `buffy_tvs.exe` yet.
- **Exit code 0** of `--install` (needs a `buffy_tvs.exe` beside the launcher) and the copy of
  the program, Read Me and runtime DLLs from a real release folder.
- **Installing over an existing install** (saves and settings left alone). By construction
  nothing outside the disc's file list and the program files is written or deleted, but it was
  not run.
- **A full disc image** (game partition at an offset, as redump images have): the offsets are the
  Chaos Bleeds launcher's, where they are proven; this game's only image here is a bare xiso.
- **Another release** of this game: the message was drawn from a made-up certificate
  (`--capture error`), not from a real North American disc.
- The `btvs_xbe_check` dialog itself (test runs suppress it; only the stderr text was seen).
- The not-enough-space and disk-full paths.
