# The GitHub page and the RecompLauncher page (GitHub area), 22 Nov 2026

The model is the owner's public port `KaikoClanworth1/x-men-destiny-pc`. The repository is **private**,
has **no release and no tag**, and this area changes neither.

## What was matched to X-Men Destiny

| X-Men Destiny | Here |
|---|---|
| `README.md`: centred title, one-line pitch, "By KaikoClanworth", a wide picture, the IMPORTANT (no game) and WARNING (work in progress) boxes, then Pictures, What works today, How to install, Keys, Something wrong?, More, Credits, Legal | The same order and wording style, with this port's facts. The wide picture is the main menu, then a table of five more (`docs/media/`) |
| `docs/STATUS.md` (goals table, what works, what doesn't) | `docs/STATUS.md`, a summary of the areas' notes; says for each thing whether it was checked in hidden runs or by hand |
| `docs/BUILDING.md` | `docs/BUILDING.md` (the build steps that were in the old README) |
| `.github/ISSUE_TEMPLATE/bug_report.yml` + `config.yml` (blank issues off) | The same form, asking for the files of a Report a Problem folder; tells the player not to attach the report's `.zip` or `save` folder |
| `recomplauncher.json` in the repository's top folder | A draft, see below |
| Description "Unofficial native PC port of X-Men: Destiny (Xbox 360). Bring your own disc." | Set: "Unofficial native PC port of Buffy the Vampire Slayer (Xbox, 2002). Bring your own disc." |
| Topics: none there (`wwe-svr2011-pc` has `pc-port, static-recompilation, ...`) | Set: `pc-port`, `static-recompilation`, `xbox`, `buffy-the-vampire-slayer`, `windows` |
| No homepage, no social preview picture | None set |
| `LICENSE` (MIT for the port's own work, with a "what this covers" part and a credit request) | **Not added**: the owner's decision (below) |
| `.gitattributes` | Not added: it would change line endings in other areas' files |

House style, seen in all three public ports: the "no game in this repository" box at the top, "Bring your
own disc" in the description, a Claude Code credit, and a Legal paragraph naming the game's owners.
Specific to X-Men Destiny and not copied: its feature claims, its mod menu and F-keys, its build-tools
requirement (this port brings its compiler in the download).

Every claim in the README and `STATUS.md` comes from `docs/*.md` as of 22 Nov. Things that had gone
stale elsewhere and were **not** repeated: the old key table (the defaults changed on 21 Nov;
`docs/controls.md` is right), "nobody has played it yet" (the owner has), "no icon".

## Waiting on the owner

1. ~~Pictures of the port running.~~ **Allowed by the owner on 22 Nov**, as in X-Men Destiny. Six are in
   `docs/media/` (1280x720 `.jpg`): `main-menu` (the wide picture at the top), `level`, `fight`,
   `graphics-options`, `controls-keyboard`, `report-saved`. They are frame dumps the game wrote itself in
   hidden test runs (`port/runs/shots/`: `1000_menu3_1080_wide_after_main_menu`,
   `0300_level1_gameplay_3840x2160_true_widescreen`, `1208_wide1080_hud_weapon_after`,
   `1000_menu3_1080_wide_after_graphics_page`, `btn_keys2_controls_page_after_1080p`,
   `0251_menu2_report_saved_short_place`), scaled down; the 4:3 ones have black sides. This is the one
   exception to "no game material": pictures for the README only. `docs/media/banner.svg` (drawn for the
   port) is kept for a social preview or a header. Still worth replacing with pictures taken while playing
   by hand: a cut-scene, the high school, a PlayStation pad's button pictures.
2. **A licence for the port's own code.** The other ports have a top-level `LICENSE` (MIT, plus what it
   does not cover: the game, and third-party parts). There is none here; only `xboxrecomp/` carries one.
   The README's Legal paragraph therefore makes no licence statement about the port's own code.
3. **Going public.** RecompLauncher installs from public GitHub releases, so the page there needs it.
   Before that: this file's point 2, and a look at the history for anything that should not be public
   (this area did not audit it).
4. **The first release.** Not published; the zip stays local by the owner's choice.

## The RecompLauncher page

**What it is.** [RecompLauncher](https://recomplauncher.com) (github.com/elripalda/RecompLauncher) is a
launcher and catalogue for PC recomps. The owner's ports are listed there: `/game/x-men-destiny-pc`,
`/game/buffy-chaos-bleeds-pc`, `/game/wwe-svr2011-pc`, `/game/wwe-crush-hour-pc`, `/game/mua3-native`.
A game's page is made on the site from a submission (the GitHub repository, and which parts it supports).
What the launcher does with the game comes from one file, **`recomplauncher.json`**, which must be **at the
top of the release zip** beside the program; the copy in the repository is only the source. The launcher
downloads the newest GitHub release itself. Guide: `docs/DEVELOPERS.md` in that repository; schema:
`https://recomplauncher.com/schema/recomplauncher.json`.

Another port of the same game is already listed there ("Hellmouth", `/game/hellmouth-buffy-recomp`, by
someone else), so the name on the page should tell them apart.

**The draft**: [`recomplauncher.json`](../recomplauncher.json), in the form of X-Men Destiny's (named
`inputs`, a `prepare` step that builds the game, `start`, `saves`, `folders`, `exit_codes`, `looks`,
`actions`). It parses as JSON; it has **not** been run in RecompLauncher. What it says:

- asks for the disc image (`.iso` / `.xiso`, European release);
- set-up runs `"Buffy Launcher.exe" --install <image> "<install>\Game" --quiet` (the launcher's windowless
  install, `docs/install.md`), up to 2 hours;
- Play starts `Game\buffy_tvs.exe`;
- saves are `Game\XboxData\HardDisk` and `Game\btvs_settings.ini`; logs, reports and crashes are named;
- no settings are offered in RecompLauncher: this port's settings are all in the game.

**Text for the submission** (X-Men Destiny's page reads "Native Windows port of the 2011 Xbox 360 game
X-Men: Destiny, built on your PC from your own copy. Early and unfinished."):

- Name: `Buffy the Vampire Slayer PC Port`
- Repository: `KaikoClanworth1/buffy-tvs-xbox-pc`
- Line: "Native Windows port of the 2002 Xbox game Buffy the Vampire Slayer, built on your PC from your own
  disc image. Early and unfinished."
- Parts supported: Launch and Preparation (from `recomplauncher.json`); no Settings, no Verification.
- Console: original Xbox. Systems: Windows x64.

**Placeholders, filled when the release exists**

| Placeholder | Where | Value |
|---|---|---|
| `RELEASE_VERSION` | `recomplauncher.json`, `prepare.version` | the release's version (`BTVS_VERSION` in `port/CMakeLists.txt`, today `0.0.1`). It must change with every release, because every release needs the game built again |
| `RELEASE_TAG` | the release | `v<version>` |
| `RELEASE_ZIP` | the release's file | today `Buffy-the-Vampire-Slayer-PC-v<version>.zip`; RecompLauncher picks a file by the words `Windows` and `x64` in its name, so it should become `…-v<version>-Windows-x64.zip` (request to Extras, `docs/AREAS.md`) |
| `RELEASE_SIZE` | release notes | about 101 MB today |
| `RELEASE_SHA256` | last line of the release notes, as in X-Men Destiny's | `sha256sum` of the zip |

**Steps to finish, in order** (each outside step needs the owner's go-ahead)

1. Extras: `package.sh` puts `recomplauncher.json` (with `RELEASE_VERSION` filled in) at the top of the
   zip and names the zip `…-Windows-x64.zip`; the `.pdb` either leaves the zip or keeps `pdb` in its name
   (RecompLauncher skips files named so only at release level, not inside a zip, so inside is fine).
2. Try it before any release: in RecompLauncher, Settings → Developers → Developer tools → "Add a build",
   pick the unpacked release folder, read its check, set it up with the disc image, press Play. To settle
   there:
   - that `--install` into a `Game` folder **inside** the launcher's own folder works (never tried);
   - that Play from RecompLauncher (which starts `buffy_tvs.exe` itself, not through `Buffy Launcher.exe`)
     behaves as the launcher's Play does;
   - progress: the launcher prints `[ 40%] text`; RecompLauncher shows a bar only for lines of the form
     `progress 0.4 text` (request to Extras). Without it the set-up shows no progress for 8 minutes;
   - exit code 1 covers both "wrong disc" and "build failed" today; separate codes would let the page say
     which (request to Extras).
3. Owner: pictures and licence decided; repository made public.
4. Owner: publish the release `v<version>` with the zip. Release notes in X-Men Destiny's form: title
   "Buffy the Vampire Slayer PC Port v<version>", one line and the "no game in this download" box, How to
   install, What's in, Known limits (from `docs/STATUS.md`), Something wrong?, and the zip's SHA-256.
5. Owner: submit at `https://recomplauncher.com/submit?platform=pc` with the text above. The owner's
   developer account there is how the other ports' pages are kept; pictures for the page are uploaded
   there, not taken from this repository.
6. After it is listed: add the "Get it on RecompLauncher" badge (from `recomplauncher.com/press`) to the
   README if the owner wants it (X-Men Destiny's README has none), and set the repository's homepage to
   the game's page (`mua3-native` has `https://recomplauncher.com`; X-Men Destiny has none).

## Release conventions (for the launcher's updater and RecompLauncher), 22 Nov

Proposed by the GitHub area for Extras' updater in `Buffy Launcher.exe`; the same list is in
`docs/AREAS.md` for Extras to accept or change. Modelled on `x-men-destiny-pc` (tag `v0.1.0`, title
"X-Men: Destiny PC Port v0.1.0", one file `XMenDestiny-v0.1.0-Windows-x64.zip`, SHA-256 as the last line
of the notes) and on Chaos Bleeds' `launcher_update.inc`, which the updater is modelled on. **Nothing
here has been tried: there is no release, and the repository is private.**

| | Convention | Why |
|---|---|---|
| **Version** | `BTVS_VERSION` in `port/CMakeLists.txt`, three numbers `X.Y.Z`, the only place it is written. It only ever goes up. | The launcher, the game's log, `report.txt` and the zip all take it from there |
| **Tag** | `v` + that version, exactly: `v0.0.1`. Made on the commit the zip was built from, never moved afterwards. A mistake gets a new version, not a new zip under the old tag. | The updater compares the tag with its own version, number by number (`0.10.0` is newer than `0.9.0`) |
| **Release title** | `Buffy the Vampire Slayer PC Port v0.0.1` | As X-Men Destiny's |
| **Which release counts** | The newest by version that is **not a draft, not a pre-release** and has the zip below. Not "latest by date". | A pre-release is a test build: RecompLauncher treats it as a beta too, and players stay on the full release |
| **The zip** | Exactly one: `Buffy-the-Vampire-Slayer-PC-v0.0.1-Windows-x64.zip`. The updater takes the file whose name ends in `-Windows-x64.zip`. | RecompLauncher picks the download by the words `Windows` and `x64`; one rule for both |
| **Inside the zip** | Everything directly at the top or under one folder (today `Buffy the Vampire Slayer PC/`): either works for RecompLauncher, the updater should accept both. `recomplauncher.json` beside `Buffy Launcher.exe`. **No `btvs_settings.ini`, no `launcher.ini`, no `XboxData`**: an update must never bring a file the player's install wrote. | An update unpacks over the folder; a settings file in a release would replace the player's |
| **Checksum** | Two, the same value: GitHub's own SHA-256 of the file (the asset's `digest` field in the API, `sha256:<hex>`; what Chaos Bleeds' updater and RecompLauncher check), and a second file `Buffy-the-Vampire-Slayer-PC-v0.0.1-Windows-x64.zip.sha256` holding `<hex>  <file name>` (the form `sha256sum -c` reads). The updater checks the download against the `digest`, falls back to the `.sha256` file when the API gives none, and refuses the update when neither matches. The same value is the last line of the notes. | RecompLauncher never takes a `.sha256` file for the game |
| **Other files in a release** | None. If symbols are ever published apart from the zip, the name has `pdb` or `symbols` in it; a source archive has `source`. | RecompLauncher skips those names |
| **Release notes** | The form below. The **first paragraph is one or two plain sentences on what changed for the player**, with no markdown but bold: the updater may show it beside "Update". | |
| **A rebuild after updating** | Every release so far changes the kit, so after an update the game's code is built again from the files already in the game folder (the launcher's `btvs_game.ok` check decides, not the notes). The notes say how long. | The player should know the first Play after an update takes minutes |
| **Where the updater asks** | `https://api.github.com/repos/KaikoClanworth1/buffy-tvs-xbox-pc/releases?per_page=20`, no sign-in, no token in the program. | While the repository is private this answers 404: the updater must treat that as "no update" quietly, and it cannot be tested against this repository until it is public |

### The launcher's updater and RecompLauncher on one install

RecompLauncher downloads the newest release itself, unpacks it over its install folder, and (because
`prepare.version` in `recomplauncher.json` changes with every release) runs the set-up step again. If the
launcher's own updater also replaced files there, RecompLauncher would no longer know which release the
folder holds. So one of them owns an install, decided when it is made:

- RecompLauncher runs the set-up step and the game with **`RECOMPLAUNCHER=1`** in the environment (also
  `RECOMPLAUNCHER_VERSION`, `RECOMPLAUNCHER_DATA`). When `--install` sees it, the launcher marks the game
  folder as RecompLauncher's (a line in a file of Extras' choosing that stays with the game folder and is
  not in the release zip).
- In a folder marked so, `Buffy Launcher.exe` opened by hand **does not check, download or replace
  anything**. It may say "Updates for this copy come through RecompLauncher".
- An install made by the launcher's own window or by `--install` without that variable is the launcher's:
  it updates itself, and RecompLauncher does not know it exists.
- The mark is removed only by installing again without the variable. Neither side ever edits the other's
  files beyond what unpacking a release does.

Still open for RecompLauncher (only matter once it is tried there): its `keeps_copy` (building again
after an update without asking for the disc image, from the disc files the game folder keeps) needs the
image to be **one argument that can be left out**, such as `--image=<file>`; with today's
`--install <image> <folder>` leaving the image out shifts the folder. Until then an update through
RecompLauncher asks the player for the disc image again.

### Release notes, the form

```
The first release of the Buffy the Vampire Slayer PC port. **Early and unfinished: expect faults, and please report them.**

> **There is no game in this download.** You need your own copy of *Buffy the Vampire Slayer* (2002) for the original Xbox, European (PAL) release, as a disc image (`.iso`). The launcher builds the PC version from it, on your PC.

## How to install
1. Download `Buffy-the-Vampire-Slayer-PC-vX.Y.Z-Windows-x64.zip` below and unzip it anywhere.
2. Open `Buffy Launcher.exe`, choose your disc image and where to install the game.
3. Press **Install**. This takes about 8 minutes, once.
4. Press **Play**.

**Already installed?** (from the second release on; wording once the updater is in) ...

**You need:** Windows 10 or 11 (64-bit), a graphics card with Direct3D 11, about 2.2 GB of free space (0.9 GB more while it builds). No other tools.

## What's in        (first release)   /   ## What changed   (later ones)
## Known limits     (from docs/STATUS.md; ends with a link to it)
## Something wrong? (Report a Problem or F12, then an issue; install_log.txt if installing failed)

SHA-256 of the zip: `<hex>`
```

## Release checklist

Every step that publishes something (5 to 8) needs the owner's go-ahead for that release.

1. **Version**: raise `BTVS_VERSION` in `port/CMakeLists.txt`; commit.
2. **State**: `docs/STATUS.md` and the README's "What works today" say what is true of this build; the
   README's "No release has been published yet" note goes with the first release.
3. **Build and package** (Extras): `bash port/tools/build.sh`, `bash port/tools/package.sh`. The script's own
   check must pass (no game data, no translated game code). Then by eye: the zip is named
   `…-vX.Y.Z-Windows-x64.zip`; `recomplauncher.json` is in it with `prepare.version` equal to the version;
   no settings, saves or `launcher.ini` in it; `"Buffy Launcher.exe" --version` from the unpacked zip says X.Y.Z.
4. **Try the zip, not the build folder**: unpack it somewhere new, `--install` from the disc image into an
   empty folder, `--play` hidden and muted (`docs/install.md`), exit code 0. From the second release on,
   also: install the previous release, let the updater take this zip (from a local test address, since
   nothing is published yet), and check that saves and `btvs_settings.ini` are untouched and the game is
   built again.
5. **Checksum file**: `sha256sum` of the zip into `<zip name>.sha256`.
6. **Push** `main`, then tag that commit `vX.Y.Z` and push the tag.
7. **Draft the release** on that tag with the title and notes above and both files; read the draft on
   GitHub; compare the zip's SHA-256 with the one GitHub shows for the file.
8. **Publish** (not as a pre-release, unless it is meant as a test build).
9. **Afterwards**: an installed previous release offers the update; RecompLauncher, once the port is listed,
   shows the new version (nothing to submit again: it reads the releases). Note the release in `docs/github.md`.

## Audit before going public (22 Nov, `main` at 88f3898)

The owner's go-ahead (relayed 22 Nov): first release, repository public, submit to RecompLauncher.
What was looked at, over **every commit of the only branch on GitHub** (`main`, 157 commits; no tags, no
other branch), not just the newest files:

| Looked for | How | Found |
|---|---|---|
| Game files | every file name and type that was ever committed (1,003 blobs) | None. No `.iso`, `.xbe` of the game, `.map`, `.xmv`, `.pak`, `.dds`, `.wav`, `.bin`, `.dmp`, `.rec`, `.zip`, `.dll`, `.exe`; nothing ever under `game_files/`, `GAME/`, `dist/`, `port/runs/`, `port/src/recomp/gen/`, `reports/`, `crashes/`, `ShaderCache/` |
| Binary files at all | the same list | `port/icon/btvs.ico` (the drawn icon), `docs/launcher-build.png` (the launcher's window), the six README pictures in `docs/media/` (allowed by the owner), and two of xboxrecomp's own test files: `xboxrecomp/tools/conformance/test.xbe` (4 KB, a test program, also in the public Chaos Bleeds repository) and `xboxrecomp/tests/wma_decoder/fixtures/sine.wma` (a sine tone) |
| Translated game code, data tables | largest files; C files with dense tables of bytes | None. The only table is `port/src/btvs_native_shader_keys.h`: the port's list of texture-stage set-ups (render states, not game code; Graphics' note in `docs/AREAS.md`) |
| The disc's real icon or title image | file list; `docs/icon.md` | Not in the repository: made on the player's PC at install |
| From `final.map` | by reading | Function names and addresses of the game appear where the port hooks them (`port/src/manual_functions.json`, the `btvs_*.c` files, the area notes). That is the same in the owner's public ports; the map itself was never committed |
| Secrets | every commit: GitHub, AWS, OpenAI-style and Slack tokens, private keys, `password=` / `api_key=` with a value | None |
| E-mail addresses | every commit's files; all commit messages | None in files. Messages carry only `Co-Authored-By: Claude ... <noreply@anthropic.com>` |
| The owner's e-mail, Windows user name, `C:\Users\...` paths | every commit's files and messages | None. Paths under `D:\Xbox Games Ports\...` appear (no user name in them) |
| Who the commits say they are from | author and committer of all 157 | Only `KaikoClanworth1 <KaikoClanworth1@users.noreply.github.com>`, the same as on `x-men-destiny-pc`, `buffy-chaos-bleeds-pc` and `wwe-svr2011-pc`. Nothing new is exposed |
| Submodules | `.gitmodules` | Four, all public: KhronosGroup/Vulkan-Headers, GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator, zeux/volk, KhronosGroup/glslang |

**Result: nothing found that must stay private.** Not covered: this is a search by name, type and pattern,
not a reading of all 157 commits' text; the area notes quote log lines and describe the game's inner
workings in detail, as the public ports' notes do. Still open and unchanged: no `LICENSE` for the port's
own code (the owner has not answered; the README makes no licence statement).

## How X-Men Destiny's RecompLauncher artwork is done

- **Nothing of it is in the repository.** `x-men-destiny-pc` holds only port screenshots (`docs/images/`,
  ten 1280x720 `.jpg`) and `recomplauncher.json`. So no official artwork goes into this repository either.
- **The page's pictures live on RecompLauncher's own server**, named in its catalogue
  (`https://recomplauncher.com/pc-catalog.json`, entry `x-men-destiny-pc`): `icon` (512x512 PNG), `cover`
  (the box-art shape, JPEG), `hero` (a wide backdrop, JPEG), `logo` (the game's logo on a clear
  background, PNG; 798x300 there), all under `recompstore.com/media/pc-<id>-...`. They are given on the
  site (the submission, then the owner's developer account there), not taken from GitHub.
- The catalogue entry also has: `name`, `game`, `origin` (`xbox`), `blurb` (one line), `description`
  (three short paragraphs), `needs`, `credits`, `features` (a list of lines), `tags`, `genre`, and
  `assets.windows` (the pattern for the release file, `XMenDestiny-*-Windows-x64.zip` there). The site adds
  the game's facts itself from IGDB (`about`: this game is IGDB 5760).
- **Where X-Men Destiny's four pictures were taken from is not recorded** anywhere in that repository or
  on this PC, so this cannot be copied blindly: the owner picks the official cover, backdrop and logo when
  submitting (the other listed port of this game, "Hellmouth", already shows official ones on the site).

Text ready for the submission, in the catalogue's fields:

- `name`: Buffy the Vampire Slayer PC Port ("Hellmouth" is the other one)
- `game`: Buffy the Vampire Slayer; `origin`: xbox; `genre`: action
- `blurb`: Native Windows port of the 2002 Xbox game Buffy the Vampire Slayer, built on your PC from your own disc image. Early and unfinished.
- `description`: This is a native Windows port of Buffy the Vampire Slayer, the 2002 Xbox game. It is not an emulator. The download holds no game, so you need your own Xbox disc of the European (PAL) release as an .iso. The included launcher reads that image and builds the PC version on your own machine, which takes about 8 minutes the first time; the compiler it needs comes in the download. / The port is early. The game starts and its first levels can be played, at resolutions up to 4K, in widescreen, at 60 fps and above, with controller, keyboard and mouse. No level has been played through to its end yet, and it has only run on the developer's PC. / If something goes wrong, choose Options > Report a Problem or press F12, and report it with the files it saves.
- `needs`: Your own Buffy the Vampire Slayer (2002) Xbox disc image (.iso), European (PAL) release; Windows 10/11 64-bit; a Direct3D 11 graphics card; about 2.2 GB free (0.9 GB more while it builds). No build tools.
- `credits`: KaikoClanworth: project lead, direction and testing. / Claude Code (Anthropic's AI coding assistant) wrote most of the port's code, tools and docs. / Built on xboxrecomp by sp00nz (the recompiler and Xbox runtime; parts derive from xemu), SDL and LLVM.
- `features`: Up to 4K, windowed, borderless or fullscreen / Widescreen with a wider view in the levels / 60 fps and above, with a limit you choose / Xbox, PlayStation and Switch pads, keyboard and mouse, rebinding, button pictures that follow your device / PC options in the game's own menu: Graphics, PC Audio, PC Controls, Memory, Exit / Report a Problem (F12) that records what you did / Updates from the launcher
- `tags`: 4K, Widescreen, High FPS, Controller
- `assets.windows`: `Buffy-the-Vampire-Slayer-PC-*-Windows-x64.zip`

## Requests made to other areas

In `docs/AREAS.md`, to Extras: the stale key table and "early development build" lines in
`port/launcher/Read Me.txt`; `recomplauncher.json` in the zip; the zip's name; `progress` lines and
separate exit codes for `--install`; a licence file for SDL in the release; the release conventions above
for the updater.
