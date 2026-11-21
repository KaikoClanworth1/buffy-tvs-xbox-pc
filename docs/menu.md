# Menu and settings

Owner: the Menu session. Files: `port/src/btvs_menu.c`, `btvs_settings.c/.h`, this file.

## Status (2026-11-21)

Checked in the running game with hidden test runs (`run.sh`, scripted pad, frame dumps). The
pictures are in the main checkout's `port/runs/shots/` (`*_menu_*.png`).

| Thing | State |
|---|---|
| Settings store | Works in the game: values written by the pages are in the `.ini` after the run and are read back on the next start (a second run opened the Graphics page showing the 800 x 600, VSync off, Stretched of the first) |
| Main menu **Exit** | Verified: Down reaches it, A ends the process in order (`btvs_quit`, exit code 0, 15 s into the run) |
| **Options** list with the PC entries | Verified: drawn in the game's menu font, Up/Down reach every line, A opens each page, B on a page returns to the list, B again leaves Options |
| **Graphics** page | Verified: every line changes with Left/Right; Display Mode, Resolution, VSync and Picture were seen applied by Graphics at once (`[gfx] display: 800x600 ...`, the frame dumps change size). FPS Limit and Overlay are written; whether they take effect was not looked at (Graphics) |
| **PC Audio** page | Verified: values change and are saved. Their effect on the sound was not checked (runs are muted; Audio) |
| **PC Controls** page | Verified: Sensitivity, Invert Look, Rumble change and are saved. **Key Bindings**: the list, Left/Right to the other bindings, **Y clears a binding** (seen on screen and in the `.ini`), A shows "Press a Key..." and the page comes back when nothing is pressed (8 s). **Not verified: a new key actually being taken.** Controls' capture reads the real keyboard and only while the game window is in front, so a hidden run cannot press the key (asked for in `docs/AREAS.md`). Not driven: Reset Bindings, scrolling past seven lines |
| **Memory** page | Verified: PC Memory, Game's PC Memory, Video Memory, Game's Video Memory, Game Heap, Heap Peak (Logs' `btvs_mem_stats`) |
| **Report a Problem** (Options) | Verified: category, writing details with the controller (A adds, Y deletes), Send Report, "Saving Report...", "Report Saved" with the short place; Back returns. The report held the picture, log, trail, settings, input recording. **Open Folder** is on the page but was not pressed (it opens Explorer; test runs only log it) |
| **Report a Problem** (pause menu) | Verified in the first level: fourth line of the list under Quit, in the same font; A opens the report page, Send saves a report, B returns to the pause menu and the game; the line is gone while "Are you sure you want to QUIT?" is up. Its picture is the pause menu, not the game (see Open points) |
| PC wording | Verified: the Start Game page says "Launch a New Game on this PC called", centred, with no block count and no picture of the console, and A still goes on to the difficulty page and the level; the game's message "Buffy The Vampire Slayer only allows 10 saved games. ..." without "your Xbox Hard Disk". The other nine reworded messages were written (log: 10 of 10) but not brought up |
| Typing the report's details on a keyboard | Verified: `WM_CHAR` messages posted to the hidden window ("Hi 42!") appeared in Details and went into the report. Real key presses on a visible window: not checked |
| Highlighted line without its black copy | Verified at 640x480 and 1920x1080 widescreen, on the game's own pages (main menu, Options, Display) and a PC page: one bright line on the glow, no dark copy. With `Menu Shadow: Original` (`BTVS_MENU_HIGHLIGHT_SHADOW=1`) the game's own look is back. The Graphics line itself was not pressed in a run (the value was set through the environment). In a level (pause menu): not looked at |
| A real controller, a visible window | Not checked: every run was hidden and scripted |

Faults found by these runs and fixed: Send Report froze the game (the report maker waited on the
game thread itself; reports are now made on a helper thread); the Xbox wording; the pages were in
the small text font, not the game's menu font; the pad's **Y** is the game's "delete" command
(`0x28`), not X, so the hints named the wrong button.

`BTVS_MENU_PC_ENTRIES=0` runs the game with its menus untouched.

How the runs were driven: `BTVS_PAD_SCRIPT` with frame-counted presses (`f90 DOWN`), because a
stall on the shared CPUs swallows a 0.15 s press; presses in a menu's first two seconds are ignored
(its fade-in); Up does not wrap on the main menu (Down five times, Up twice lands on Options whether
Load Game is greyed or not). The game restarts itself to load a level and reads the script again, so
the pause-menu run swaps the script file once the first process has read it.

## The game's front end

Namespace `GUI`, objects `GUI*.obj` in `final.map`. Guest address = map address − `0x3EF460`.

### Classes

- **`TManager`** (`g_lpManager` at guest `0x7E6BD0`; the game's is a `TBuffyManager`).
  Loads `interfc\buffy.mbf` (`LoadMBF`), owns the menus and shows one at a time.
  `+0x48` menu instances by id−1, `+0x4C` menu table from the file, `+0x54` current menu,
  `+0x20+4t` / `+0x2C+4t` resource arrays and counts by type `t` (0 string, 1 image, 2 font).
  Vtable: `+0x14 DrawImage`, `+0x18 GetTextExtent`, `+0x1C DrawString(text, fontData, colour, x, y, ?, selected)`,
  `+0x34 FillRect`, `+0x38 SetMenu(id, rememberAsBack)`, `+0x48 PollCommands`,
  `+0x4C GetResourceByHandle(type, n)`, `+0x50 GetCurrentMenu`.
  `TBuffyManager::SetMenu` starts a fade and keeps the request in `+0x478`/`+0x47C`;
  `ExecuteMenuChange` does it. The GUI sounds are `TSound` members of the manager
  (`+0x78` back, `+0x198` and `+0x378` select, `+0x2B8` error).
- **`TControl`**: `+4` parent, `+8,+C` position, `+10,+14` size, `+18` state
  (0 normal, 1 selected, 2 disabled, 3 hidden), `+1C` id, `+20` flags (4 = the selection skips it).
- **`TCanvas` / `TMenu`**: `+2C` control list (`+30` head; node = control, next, prev), `+3C`
  selected node, `+40` menu to go back to, `+44` background image. `TCanvas::OnCommand`:
  `0x25` previous line, `0x26` next line, anything else goes to the selected control.
  `TMenu::OnCommand`: `0x29` → `SetMenu(back)`.
- **`TBuffyMenu`** (base of every Buffy menu): the roto/particle background, button fades,
  sounds (`OnCommandSounds` vtable `+0x98`, `PlayFocusedSound` `+0x9C`), `+0x48` "loaded".
  Commands are ignored while `s_fadeMode` (guest `0x7F041C`) is set.
- **`TTextButton`** (0x44 bytes, vtable guest `0x663C74`): a menu line. `+28` alignment,
  `+34` text (heap copy), `+38` font in use, `+3C` normal font, `+40` selected font
  (font resource: `+10` colour, `+14` font data). On command `0x27` it sends its parent
  `OnMessage(this, 5, id, 0)`; that message is how every menu learns a line was pressed.
- Others: `TStaticText`, `TStaticImage`, `TImageButton` (the A/B button pictures),
  `TScrollBar` (sliders), `TListBox`, `TActiveText`.

Commands (`TManager::PollCommands` polls `0x23..0x29` each frame, with auto-repeat for
the first four): `0x23` left, `0x24` right, `0x25` up, `0x26` down, `0x27` select (A),
`0x28` delete (Y; seen in a run), `0x29` back (B; the Back button too). Left/right order is read from `TMenuAudio::OnCommand`
(`0x23` lowers a slider).

### How a menu is defined

`buffy.mbf` (inside every `pak\res_*.pak`, uncompressed, path string `interfc\buffy.mbf`)
is self-describing: resources (strings, images, fonts), a table of 15 menus, then per menu
`class\0 name\0 id image count` and per control `class\0 … name\0 id tabOrder nProps` followed
by named properties (`Offset`, `Extent`, `Visible`, `Text`, `Normal Font`, `Active Font`, …;
type 2 = bool, 3 = int, 4 = point). Classes are created by name through the `TMenuDef` /
`TCtrlDef` registries (`DefaultT…Constructor` functions).

| id | Class | id | Class | id | Class |
|---|---|---|---|---|---|
| 1 | `TMenuMain` | 6 | `TMenuController` | 11 | `TMenuCredits` (Extras) |
| 2 | `TMenuStartGame` | 7 | `TMenuAudio` | 12 | `TMenuNameSession` |
| 3 | `TMenuLoadGame` | 8 | `TMenuDisplay` | 13 | `TMenuDifficulty` |
| 4 | `TMenuOptions` | 9 | `TMenuPause` | 14 | `TMenuCamera` |
| 5 | `TMenuLoadSession` | 10 | `TMenuTitle` | 15 | `TMenuResurrect` |

Screen is 640×480. Fonts by number: 2 large, 1 large selected, 4 small, 3 small selected
(`media\Font_*.xpr`; names in the exe: `Font_Hellish`, `Font_Hellish2`, `Font_Holy`, `Font_Holy2`).

- **Main** (1): Start Game 68,170 · Load Game 68,230 · Options 69,290 · Extras 69,348, large font.
  `OnMessage` ids: 1 → name session (12) after save-space checks, 2 → 5, 3 → 4, 131 → 11.
- **Options** (4): Controller 121,117 (id 41) · Camera 121,163 (136) · Audio 121,210 (42) ·
  Display 121,261 (43) · Difficulty 121,312 (120, hidden once a map is loaded); A/B pictures
  at the right from y 320. `OnMessage` → `SetMenu(6 / 14 / 7 / 8 / 13, true)`.
- **Pause** (9): Resume 273,76 (85) · Options 273,128 (86) · Quit 273,180 (87), the
  confirm-quit lines, objective text at 104,233, secrets at 242,352.

### Leaving the game

The retail game has no "quit". `TSessionManager::QuitToMain` (map `0x626F50`) is the pause
menu's Quit; `TSessionManager::ShellRebootToDashboard` (`0x626F40`, to
`TSessionStorageManager_Implement` in `xmain.obj`) reboots to the dashboard through
`XLaunchNewImageA` / `HalReturnToFirmware`. `TIF_EngineAPI_Implement::Sys_DoesPlatformReboot`
and the `LOST MEMORY FROM REBOOT` string in `xmain.obj` suggest the game also relaunches its
own image (between levels); that is boot/Graphics territory, not used here.

## What the port adds

Nothing is drawn by the port. Every PC line is a real `TTextButton` made with the game's
`DefaultTTextButtonConstructor`, given the fonts of a neighbouring line and appended to the
menu's own control list (`TLinkList::Add`), so the game draws, highlights, navigates and
sounds it like its own.

| Where | Line | Does |
|---|---|---|
| Main menu | **Exit** (69,406) | `btvs_quit()` (Graphics): settings saved, window and device closed, process ends |
| Options, right column (x 340) | **Graphics**, **PC Audio**, **PC Controls**, **Memory** | open a page |
| Options, 121,363 | **Report a Problem** | opens the report page |
| Pause menu, fourth line of its list | **Report a Problem** | `SetMenu(Options)` and opens the report page |

The pause menu's own lines (Resume 76, Options 128, Quit 180) are moved to 70 / 110 / 150 so the
fourth fits at 190, above the objective text; it is hidden with them while the game asks
"Are you sure you want to QUIT?".

A page is the Options menu with its lines swapped: the list is hidden (state 3) and a pool
of ten lines made once (a heading and nine rows at x 121) is relabelled. **Style**: the heading
and every line the player can select are in the game's menu font (the font of its own lists,
with the same highlight); lines of description are in the game's small text font, the one it
uses itself for descriptions ("Launch a New Game ...", the objective on the pause menu). Heading
at y 70, menu-font lines 38 apart from 112, description lines 27. Left/Right change the selected
value, A steps it forward or presses the line, B returns to the Options list (and saves
`btvs_settings.ini`).

- **Graphics**: Display Mode (`display.mode`), Resolution (`display.width/height`; Desktop,
  640×480 … 1600×1200, 1280×720 … 3840×2160), VSync, FPS Limit (30/60/90/120/144/165/240/Uncapped),
  Picture (`display.widescreen`: Original 4:3 / Widescreen / Stretched), Overlay (`display.show_fps`),
  Menu Shadow (`menu.highlight_shadow`: Off / Original; see below).
- **PC Audio**: Master / Music / Effects / Speech, 0–100 in steps of 5 (`audio.*`). The game's own
  Audio page (its three sliders) is untouched; these are the PC-side levels.
- **PC Controls**: Sensitivity, Invert Look, Rumble (`controls.*`), Key Bindings, Reset Bindings.
  **Key Bindings** shows one binding of each action at a time (a line in the menu font has no
  room for three): Keyboard 1, Keyboard 2 or Controller, changed with Left/Right and named in
  the description line. A waits for a key or button (`btvs_input_capture_begin`), Y clears the
  binding; seven actions are on screen and the list scrolls.
- **Memory**: "PC Memory" (in use of total), "Game's PC Memory", "Video Memory" (in use of total),
  "Game's Video Memory", "Game Heap" (of the Xbox's 64 MB) and "Heap Peak", from Logs'
  `btvs_mem_stats` and Graphics' `btvs_gpu_info`. Refreshed twice a second.
- **Report a Problem**: Problem (six categories), Details (A to write with the controller:
  Left/Right letter, A add, Y delete, B done; or type on the keyboard through `btvs_menu_char`;
  the line shows the end of a long text), Send Report → `btvs_report_create("<category>: <details>")`
  on a helper thread ("Saving Report..." meanwhile). Then "Report Saved", the place in short
  ("In the game's folder: reports\<date-time> (and a .zip beside it)"), "Nothing was sent anywhere",
  **Open Folder** (opens it in Explorer; not in test runs) and Back.
- **Start Game page**: the picture of the Xbox console is not shown. It is a button the page's
  A press goes through, so it is kept and drawn off screen rather than removed.

Pages only write settings; the owning area applies them from `btvs_setting_on_change`.

### Hooks

`recomp_lookup_manual()` (in `recomp_manual.c`) asks `btvs_menu_lookup()` first, so the
classes are hooked where the game calls them through their vtables. **No regen is needed.**

| Guest | Function | Why |
|---|---|---|
| `0x223230` | `TMenuOptions::OnLoad` | add the entries and the pool; open a pending page |
| `0x2232E0` | `TMenuOptions::OnMessage` | a PC line was pressed |
| `0x2233F0` | `TMenuOptions::OnCommand` | Left/Right/B/X while a page is open |
| `0x218410` | `TBuffyManager::DrawString` | the highlighted line without its black copy |
| `0x219480` | `TBuffyMenu::OnUpdate` | refresh an open page |
| `0x222470`, `0x2226B0` | `TMenuMain::OnLoad`, `OnMessage` | Exit |
| `0x223760`, `0x223A60`, `0x223D70` | `TMenuPause::OnLoad`, `OnMessage`, `OnUpdate` | Report a Problem with the list; `btvs_report_prepare()`; hidden in confirm-quit |
| `0x222F00` | `TMenuNameSession::OnUpdate` | the Start Game page without its block count |

### PC wording

Texts that name the Xbox are overwritten where they lie, never with a longer text (`pc_wording()`,
run when the main, Options or pause menu loads):

- the menu file's strings: "Launch a New Game on your Xbox Hard Disk called" → "... on this PC
  called", "Hard Disk" → "This PC", "Total Blocks" / "Free Blocks" / "- 50,000+" emptied;
- ten messages in the game's data (too many saved games, not enough free blocks, "don't turn off
  your Xbox console", "problem with the disc") reworded for a PC, keeping their `%i`;
- the Start Game page hides its four block-count lines every frame (the game writes "50,000+" itself)
  and its picture of the console.

Left as they are: "MU 1" / "MU 2" and the
memory-unit messages (those pages and messages cannot come up without memory units), the button
pictures (A, B, X, Y).

Functions of other areas (`btvs_log`, `btvs_trail`, `btvs_report_*`, `btvs_input_*` binding
calls) are declared in `btvs_menu.c` with the owners' draft prototypes and fall back to
stand-ins through `/alternatename` until the real ones are linked.

### Open points

- A report made from the pause menu shows the pause menu, not the game: the screenshot is the
  next frame shown. Needs the renderer's last shown frame (asked for in `docs/AREAS.md`).
- The Options entries "PC Audio" and "PC Controls" stand beside the game's own "Audio" and "Controls".
- A line added to a menu is drawn lower in its box than the game's own lines when the box is tall
  (seen on the pause menu: 25 pixels with a 50-high box). The cause was not found; added lines
  use a box as high as the text.
- Numbers in the game's menu font are hard to read at 640x480 (its "." looks like ","); sizes of
  10 GB and more are shown without decimals.
- The highlighted line has a dark double in every menu, the game's own lines too (Graphics' known fault).
- No overlay fallback was needed (task step 4); nothing here uses `btvs_draw_text`.

## The highlighted line's black copy

The game itself draws the selected line of a menu twice. `GUI::TBuffyManager::DrawString`
(`0x218410`), when the line is in its "selected" font and the manager's byte `+0x474` is set,
draws every letter in black (alpha = the menu's fade) at the line's own place (`0x218783`), and
then the same letter in the line's colour 5 units right and 5 up (`0x2187E1`, one letter late;
5.0 is the float at `0x61BD10`). So the line "lifts" off a black copy of itself, over the purple
glow. The port drew it exactly as asked (same size, 5 units apart at 640x480, 11 pixels at
1920x1080), on the game's own pages as well as the PC pages: neither a renderer fault nor
something the PC pages add. Whether an Xbox on a television shows it as plainly was not
checked (no picture of the original was at hand).

The owner asked for it to go, so by default the port draws the line once, at its own place:
the hook clears `+0x474` and sets `+0x475` around the call, which sends the function down its
single-draw branch (`0x2188EB`) and still keeps it out of the plain `TManager::DrawString`.
**Graphics -> Menu Shadow: Original** (`[menu] highlight_shadow=1`) gives the game's own look.

## Settings store

`btvs_setting_int/float/str(section, key, default)`, `btvs_setting_set_*`,
`btvs_setting_on_change(section, key, fn, user)`, `btvs_settings_save()`,
`btvs_settings_path()`. File `btvs_settings.ini` beside the exe (written through a temp
file; also at exit). `BTVS_<SECTION>_<KEY>=value` overrides a value for one run and is never
written back; `BTVS_SETTINGS_INI=<path>` uses another file. The agreed names are listed at
the top of `btvs_settings.h`.
