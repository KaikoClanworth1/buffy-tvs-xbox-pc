# The icon

Two icons. **The game's real one** (the picture the Xbox dashboard showed) is made on the player's PC
from their own disc at install and is what the installed game shows. **The port's own drawing** is what
a release ships: both programs carry it, the launcher keeps it (it runs before any disc is chosen), and
the game keeps it if the real one cannot be made.

## The game's real icon, from the player's disc

Nothing of it is in the repository or in a release. `port/launcher/btvs_icon.inc`.

**What is on the disc** (decoded to `port/runs/shots/disc_*.png`, ignored):

| Picture | Where | What it is |
|---|---|---|
| title image | `$$XTIMAGE` in `Default.xbe`, 128 x 128, DXT1, no transparency (the same bytes as `UDATA/45410012/TitleImage.xbx`) | **the dashboard's icon**: the game's logo over a full moon on dark purple. Used. |
| save image | `$$XSIMAGE`, 64 x 64, DXT1 (`SaveImage.xbx`) | what the dashboard showed beside a saved game: the logo's B in a ring. Not used. |
| `media/launch_buffy.xpr`, `launch_indyj.xpr` | 2 MB each | launch pictures for a demo disc's menu, not icons. Not looked into further. |

**At install** (after the copy and the build; `disc_icon` in the launcher), and for the owner's `GAME`
in `deploy.sh` through `"Buffy Launcher.exe" --make-icon <game folder>`:

1. The title image is read from the game folder's `Default.xbe`.
2. `btvs_icon.ico` is written beside the game: 16, 20, 24, 32, 40, 48, 64, 128, 256 px. 128 is the
   disc's picture as it is. Smaller sizes are area averages of it (every new pixel is the mean of the
   area it covers). 256 is a bicubic enlargement, stored as PNG.
3. The same nine images are written into the installed `buffy_tvs.exe` as icon group 1, replacing the
   drawn icon's. That is the resource Explorer shows and both of the game's window classes load
   (`BuffySessionWindow`, `BuffyRecompGpu`), so Explorer, the title bar, the taskbar and Alt+Tab all
   follow with no change in the game's code. Explorer is told to look at the file again.

The exe is changed in a copy beside it (`buffy_tvs.exe.icon.tmp`), which then takes its place with the
same patience as the freshly linked DLL (a virus scanner may hold it; 5 s of retries). None of it can
fail an install: on any failure the exe is as shipped, with the drawn icon, and `install_log.txt` says
why (`icon: ...`). While the game runs the exe cannot be replaced; `deploy.sh` is never run then.

**How it looks.** 128 and 64 px: as on the Xbox. 256 px: soft, the logo's letters readable, block edges
from the disc's compression smoothed but visible up close; a smarter enlargement would not add detail
the 128 px picture does not have, so none was tried. 48 and 32 px: the moon and the logo's shape, the
words not readable. 16 px (title bar): a pale moon over a smudge on purple. A variant with the save
image's B at 16 to 32 px reads better there and is on the sheet as row 2; it is not built (the request
was the dashboard's icon).

Sheet: `python port/icon/ico_sheet.py port/runs/shots/icon_real.png "label=GAME/btvs_icon.ico" ...`
shows the images stored in an `.ico` at 256, 48, 32 and 16 px on light and dark.

**Checked** (22 Nov, build 72ba548):

- `python port/icon/check_icon.py GAME/buffy_tvs.exe --ico GAME/btvs_icon.ico`: nine images, each the
  same bytes as the generated file's. `GAME/Buffy Launcher.exe` still has the drawn icon's nine.
- The exe before and after, section by section (prebuilt exe, 70 MB): `.text`, `.rdata`, `.data`,
  `.pdata` at the same addresses with the same bytes; `.rsrc` larger (146 to 374 KB); `.reloc` moved up
  behind it, same bytes; the import, debug, thread-local and load-config directories unchanged. The exe
  is not signed (checksum field 0 before and after), so there is no signature to break.
- **A release's exe with the game DLL**: the same on the release-shape install of 21 Nov (an exe with
  no resources at all, 3 MB, plus `btvs_game.dll`): a `.rsrc` section added, everything else as above,
  thread-local directory unchanged; run hidden afterwards: main menu, level 1 loaded, exit code 0. The
  DLL reads the exe's thread-local block by offsets that the resource section has no part in.
- Hidden runs of `GAME` after the change: both window classes have a 32 px and a 16 px icon; crash
  reports still find the `.pdb` (`guest function names: 42784 from the .pdb`).
- `package.sh`'s check on a test zip: refuses `btvs_icon.ico` (any `.ico`, `.icon.tmp`, `.xbx`) and
  refuses a `buffy_tvs.exe` or launcher whose icon is not the drawn one.

**Not done, not seen:**

- The icon on a real screen. Windows caches icons: Explorer or the taskbar can show the old one for a
  while after an install.
- A full install from a packaged release with this launcher (the step was run by `--make-icon` on
  finished installs, and by `deploy.sh`).
- Shortcuts: the installer makes none, so there is none to give the icon to.
- A virus scanner's view of an exe changed after it was unpacked: not tried beyond this PC's Defender.
- If the resource cannot be written the game's windows show the drawn icon; loading `btvs_icon.ico` at
  run time instead would be in Graphics' window code (noted in `docs/AREAS.md`, not needed today).
- The launcher's window shows the disc's picture inside it once a game is installed (it did before);
  its own title-bar icon stays the drawn one.

## The port's own drawing

The port's own art, drawn by a program: nothing in it is copied from the disc, the box or the show's
logo. Everything is in `port/icon/`.

| File | What |
|---|---|
| `make_icon.py` | draws the designs (Pillow + numpy) and writes the `.ico` |
| `chosen.txt` | the name of the design in the build |
| `btvs.ico` | the icon both programs embed: 16, 20, 24, 32, 40, 48, 64, 128, 256 px |
| `btvs_icon.rc` | `1 ICON "btvs.ico"`, compiled into `buffy_tvs.exe` and `Buffy Launcher.exe` |
| `check_icon.py` | reads a program's icon resource (the program is not run) and lists its sizes; `--ico <file>` compares with that file |
| `ico_sheet.py` | a picture of the images inside `.ico` files, for looking at them |

**To change the drawn icon: replace `port/icon/btvs.ico`** (one file) and build. To take another of the
drawn designs: `python port/icon/make_icon.py --pick <name>`, which rewrites `btvs.ico` and
`chosen.txt`.

## The designs

`moon_stake` (in the build), `slayer`, `graveyard`, `moon_badge`: the deep purple of the game's menus,
a full moon, a wooden stake. The owner's preview sheet, every design at 256, 48, 32 and 16 px on a
light and a dark background: `python port/icon/make_icon.py --sheet port/runs/shots/icon_candidates.png`
(`--all <folder>` writes each design as an `.ico`).

Each size is drawn on its own, eight times larger (four at 128 and 256) and averaged down, not one
big picture shrunk. Below 40 px the craters, the moon's glow and the wood's two faces and grain are
left out; lines have a least width in pixels (the stake 2.1 px at 16 px, its dark edge 0.75 px); the
moon's centre and radius are put on the pixel grid; the `slayer` below 24 px loses her raised arm and
the stake stands beside her; the `graveyard` below 24 px is only the moon and the cross, its bars a
whole number of pixels wide. The 256 px image is stored as PNG, the others as 32-bit bitmaps.

## Where a player sees it

| Place | How | Checked |
|---|---|---|
| `buffy_tvs.exe`, `Buffy Launcher.exe` in Explorer | icon group 1 in each exe | `check_icon.py`: nine images in each, byte for byte the `.ico`'s |
| The game's window: title bar, taskbar, Alt+Tab | both window classes (the session window and the game's) load resource 1 of the exe; that code was already there | from outside in a hidden run: both classes have a big and a small icon set (see `docs/install.md`) |
| The launcher's window | its class loads resource 1 (it had Windows' default) | by the code and the resource; the window was not opened |
| The picture inside the launcher's window | still the title image read from the player's own disc on their PC (`$$XTIMAGE` in `Default.xbe`); it no longer replaces the window's icon | - |
| Start menu / desktop shortcut | the installer makes none | - |
| A release (`package.sh`) | its exe is linked again from the build's objects; the icon's `.res` is now on that link line | not run since |

Not seen: the icon on a real screen (title bar, taskbar, Explorer at each view size). Windows keeps
icons in a cache: after a change Explorer can show the old one for a while.
