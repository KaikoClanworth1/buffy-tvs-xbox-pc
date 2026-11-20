# Menu and settings

Owner: the Menu session. Files: `port/src/btvs_menu.c`, `btvs_settings.c/.h`, this file.

## Status (2026-11-20)

| Thing | State |
|---|---|
| Settings store (`btvs_settings.c`) | Done. Tested standalone: defaults, set, callbacks, save, reload, environment overrides |
| Front end reversed (below) | Done, from `final.map`, the disassembly and `interfc\buffy.mbf` |
| PC entries and pages (`btvs_menu.c`) | Written. Compiles. Page logic exercised against a **fake** menu (a harness that imitates the game's list and selection), not the game |
| Link | Done: `buffy_tvs.exe` builds and links with this code (worktree build against main's `btvs_recomp.lib`, Graphics sources not yet in the build) |
| Anything in the real game | **Not verified.** A hidden test run (`run.sh menu1 40`) crashes during boot, in the game's first file opens (`CreateFileA` under `File_Exists`, after 129 indirect calls), long before any menu; that is boot bring-up (Graphics). No frame dump exists |

What a first run in the game has to prove, in order: the hooks are reached (log lines
`options: PC entries added`, `main menu: Exit added`, `pause menu: Report a Problem added`);
the added lines draw in the right font and place; the fonts have the glyphs our labels use
(lower case, digits, `:` `%` `[` `]`); Up/Down reach the new lines; A opens a page; Left/Right
are commands `0x23`/`0x24` in that order; B leaves a page without leaving Options; Exit quits.
`BTVS_MENU_PC_ENTRIES=0` runs the game with its menus untouched if any of this misbehaves.

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
| Main menu | **Exit** (69,406) | saves settings, `exit(0)` |
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
  Send Report → `btvs_report_create("<category>: <details>")`, then a page showing
  `btvs_report_last_path()`.

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

Functions of other areas (`btvs_log`, `btvs_trail`, `btvs_report_*`, `btvs_input_*` binding
calls) are declared in `btvs_menu.c` with the owners' draft prototypes and fall back to
stand-ins through `/alternatename` until the real ones are linked.

### Open points

- Positions were chosen from the menu file's numbers, not from a picture: Exit at y 406 sits
  over the bottom border art, the right column may be too close to long left-column labels,
  and "Report a Problem" in the large font may be too wide.
- The pause menu's confirm-quit mode hides its own lines by id; ours stays visible there.
- Exit uses `exit(0)` from the game thread; if Graphics wants an orderly window/device
  shutdown it should offer a function and this will call it.
- No overlay fallback was needed so far (task step 4); nothing here uses `btvs_draw_text`.

## Settings store

`btvs_setting_int/float/str(section, key, default)`, `btvs_setting_set_*`,
`btvs_setting_on_change(section, key, fn, user)`, `btvs_settings_save()`,
`btvs_settings_path()`. File `btvs_settings.ini` beside the exe (written through a temp
file; also at exit). `BTVS_<SECTION>_<KEY>=value` overrides a value for one run and is never
written back; `BTVS_SETTINGS_INI=<path>` uses another file. The agreed names are listed at
the top of `btvs_settings.h`.
