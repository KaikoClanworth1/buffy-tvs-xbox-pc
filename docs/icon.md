# The icon

The port's own art, drawn by a program: nothing in it is copied from the disc, the box or the show's
logo. Everything is in `port/icon/`.

| File | What |
|---|---|
| `make_icon.py` | draws the designs (Pillow + numpy) and writes the `.ico` |
| `chosen.txt` | the name of the design in the build |
| `btvs.ico` | the icon both programs embed: 16, 20, 24, 32, 40, 48, 64, 128, 256 px |
| `btvs_icon.rc` | `1 ICON "btvs.ico"`, compiled into `buffy_tvs.exe` and `Buffy Launcher.exe` |
| `check_icon.py` | reads a program's icon resource (the program is not run) and lists its sizes |

**To change the icon: replace `port/icon/btvs.ico`** (one file) and build. To take another of the
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

An icon from the player's own disc for a shortcut would be cheap if a shortcut existed: the launcher
already reads that image (128 x 128) on the player's PC; writing it out as an `.ico` beside the game is
a few dozen lines. There is no shortcut to give it to yet.
