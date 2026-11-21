# Menu and settings

Owner: the Menu session. Files: `port/src/btvs_menu.c`, `btvs_settings.c/.h`, this file.

## Status (2026-11-21)

Checked in the running game with hidden test runs (`run.sh`, scripted pad, frame dumps). The
pictures are in the main checkout's `port/runs/shots/` (`*_menu_*.png`).

| Thing | State |
|---|---|
| Settings store | Works in the game: values written by the pages are in the `.ini` after the run and are read back on the next start (a second run opened the Graphics page showing the 800 x 600, VSync off, Stretched of the first) |
| Main menu **Exit** | Verified: Down reaches it, A ends the process in order (`btvs_quit`, exit code 0, 15 s into the run) |
| **Options** list with the PC entries | Verified: drawn in the game's font, Up/Down reach every line, A opens each page, B on a page returns to the list, B again leaves Options |
| **Graphics** page | Verified: every line changes with Left/Right; Display Mode, Resolution, VSync and Picture were seen applied by Graphics at once (`[gfx] display: 800x600 ...`, the frame dumps change size). FPS Limit and Overlay are written; whether they take effect was not looked at (Graphics) |
| **PC Audio** page | Verified: values change and are saved. Their effect on the sound was not checked (runs are muted; Audio) |
| **PC Controls** page | Verified: Sensitivity, Invert Look, Rumble change and are saved. **Key Bindings** lists Controls' real bindings in three slots, Left/Right pick the slot, B returns. Not driven: capturing a new key (A), clearing one (X), Reset Bindings, scrolling past nine lines |
| **Memory** page | Verified: system memory, this game, video memory (total / in use / this game), the game heap and its peak, from Logs' `btvs_mem_stats` |
| **Report a Problem** (Options) | Verified: category, writing details with the controller, Send Report, "Saving the report...", then the folder it was saved in; Back returns. The report held the picture, log, trail, settings, input recording |
| **Report a Problem** (pause menu) | Verified in the first level: the line is in the pause menu, A opens the report page, Send saves a report, Back and B return to the pause menu and the game. Its picture is the pause menu, not the game (see Open points) |
| PC wording | Verified: "Launch a New Game on this PC called" on the Start Game page and "Buffy The Vampire Slayer only allows 10 saved games. ..." (the game's message without "your Xbox Hard Disk"). Written but not seen: the block count gone from the Start Game page and its first line centred (Start Game is closed by the 10 test saves, `docs/AREAS.md`), the other nine messages |
| Typing the report's details on a keyboard | Not checked (test runs have no window input) |
| A real controller, a visible window | Not checked: every run was hidden and scripted |

Faults found by these runs and fixed: Send Report froze the game (the report maker waited on the
game thread itself; reports are now made on a helper thread); the Xbox wording.

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
`0x28` delete (X), `0x29` back (B). Left/right order is read from `TMenuAudio::OnCommand`
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
| Pause menu, 64,376 (small font) | **Report a Problem** | `SetMenu(Options)` and opens the report page |

A page is the Options menu with its lines swapped: the list is hidden (state 3) and a pool
of ten lines made once (a heading and nine rows, small font, x 121, y 76 / 110 + 29·n) is
relabelled. Left/Right change the selected value, A steps it forward or presses the line,
B returns to the Options list (and saves `btvs_settings.ini`).

- **Graphics**: Display Mode (`display.mode`), Resolution (`display.width/height`; Desktop,
  640×480 … 1600×1200, 1280×720 … 3840×2160), VSync, FPS Limit (30/60/90/120/144/165/240/Uncapped),
  Picture (`display.widescreen`: Original 4:3 / Widescreen / Stretched), Overlay (`display.show_fps`).
- **PC Audio**: Master / Music / Effects / Speech, 0–100 in steps of 5 (`audio.*`). The game's own
  Audio page (its three sliders) is untouched; these are the PC-side levels.
- **PC Controls**: Sensitivity, Invert Look, Rumble (`controls.*`), Key Bindings, Reset Bindings.
  **Key Bindings** lists Controls' actions with their three slots; Left/Right pick the slot,
  A waits for a key or button (`btvs_input_capture_begin`), X clears it; the list scrolls.
- **Memory**: system RAM in use / total and this process (measured here), video memory total and
  this game's use from Graphics' `btvs_gpu_info`; all of it plus the game's heap from Logs'
  `btvs_mem_stats` once `BTVS_HAVE_MEM_STATS` is defined. Refreshed twice a second.
- **Report a Problem**: Problem (six categories), Details (A to write with the controller:
  Left/Right letter, A add, X delete, B done; or type on the keyboard through `btvs_menu_char`),
  Send Report → `btvs_report_create("<category>: <details>")` on a helper thread ("Saving the
  report..." meanwhile), then a page showing `btvs_report_last_path()`.

Pages only write settings; the owning area applies them from `btvs_setting_on_change`.

### Hooks

`recomp_lookup_manual()` (in `recomp_manual.c`) asks `btvs_menu_lookup()` first, so the
classes are hooked where the game calls them through their vtables. **No regen is needed.**

| Guest | Function | Why |
|---|---|---|
| `0x223230` | `TMenuOptions::OnLoad` | add the entries and the pool; open a pending page |
| `0x2232E0` | `TMenuOptions::OnMessage` | a PC line was pressed |
| `0x2233F0` | `TMenuOptions::OnCommand` | Left/Right/B/X while a page is open |
| `0x219480` | `TBuffyMenu::OnUpdate` | refresh an open page |
| `0x222470`, `0x2226B0` | `TMenuMain::OnLoad`, `OnMessage` | Exit |
| `0x223760`, `0x223A60` | `TMenuPause::OnLoad`, `OnMessage` | Report a Problem; `btvs_report_prepare()` |
| `0x222F00` | `TMenuNameSession::OnUpdate` | the Start Game page without its block count |

### PC wording

Texts that name the Xbox are overwritten where they lie, never with a longer text (`pc_wording()`,
run when the main, Options or pause menu loads):

- the menu file's strings: "Launch a New Game on your Xbox Hard Disk called" → "... on this PC
  called", "Hard Disk" → "This PC", "Total Blocks" / "Free Blocks" / "- 50,000+" emptied;
- ten messages in the game's data (too many saved games, not enough free blocks, "don't turn off
  your Xbox console", "problem with the disc") reworded for a PC, keeping their `%i`;
- the Start Game page hides its four block-count lines every frame (the game writes "50,000+" itself).

Left as they are: the picture of the console on the Start Game page, "MU 1" / "MU 2" and the
memory-unit messages (those pages and messages cannot come up without memory units), the button
pictures (A, B, X, Y).

Functions of other areas (`btvs_log`, `btvs_trail`, `btvs_report_*`, `btvs_input_*` binding
calls) are declared in `btvs_menu.c` with the owners' draft prototypes and fall back to
stand-ins through `/alternatename` until the real ones are linked.

### Open points

- A report made from the pause menu shows the pause menu, not the game: the screenshot is the
  next frame shown. Needs the renderer's last shown frame (asked for in `docs/AREAS.md`).
- The pause menu's confirm-quit mode hides its own lines by id; "Report a Problem" stays visible
  there (not looked at in a run).
- "Report a Problem" sits at the bottom left of the pause menu in the small font; the Options
  entries "PC Audio" and "PC Controls" stand beside the game's own "Audio" and "Controls".
- The highlighted line has a dark double in every menu, the game's own lines too (Graphics' known fault).
- No overlay fallback was needed (task step 4); nothing here uses `btvs_draw_text`.

## Settings store

`btvs_setting_int/float/str(section, key, default)`, `btvs_setting_set_*`,
`btvs_setting_on_change(section, key, fn, user)`, `btvs_settings_save()`,
`btvs_settings_path()`. File `btvs_settings.ini` beside the exe (written through a temp
file; also at exit). `BTVS_<SECTION>_<KEY>=value` overrides a value for one run and is never
written back; `BTVS_SETTINGS_INI=<path>` uses another file. The agreed names are listed at
the top of `btvs_settings.h`.
