# The GitHub page and the RecompLauncher page (GitHub area), 22 Nov 2026

The model is the owner's public port `KaikoClanworth1/x-men-destiny-pc`. The repository is **private**,
has **no release and no tag**, and this area changes neither.

## What was matched to X-Men Destiny

| X-Men Destiny | Here |
|---|---|
| `README.md`: centred title, one-line pitch, "By KaikoClanworth", a wide picture, the IMPORTANT (no game) and WARNING (work in progress) boxes, then Pictures, What works today, How to install, Keys, Something wrong?, More, Credits, Legal | The same order and wording style, with this port's facts. The wide picture is `docs/media/banner.svg`, drawn for the port (the moon and stake of `port/icon/make_icon.py`), because no game picture is in the repository yet |
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

1. **Pictures of the port running.** X-Men Destiny's README shows game pictures (`docs/images/*.jpg`,
   1280x720, ten of them: main menu, a level, a fight, a cut-scene, the Graphics page, the mod menu and four
   of the setup). Here the rule is no game material in the repository, so none was added. If the owner
   allows the same here: put them in `docs/media/` as `.jpg`, replace the marked `PICTURES` comment in
   `README.md` with X-Men Destiny's two-column table, make the first one (the main menu) the wide picture at
   the top, and add its sentence to Legal: "the pictures above show the port running and what they show
   belongs to the game's owners". The banner then moves out or stays as a smaller header.
2. **A licence for the port's own code.** The other ports have a top-level `LICENSE` (MIT, plus what it
   does not cover: the game, and third-party parts). There is none here; only `xboxrecomp/` carries one.
   The README's Legal paragraph therefore makes no licence statement about the port's own code.
3. **Going public.** RecompLauncher installs from public GitHub releases, so the page there needs it.
   Before that: this file's point 1 and 2, and a look at the history for anything that should not be public
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

## Requests made to other areas

In `docs/AREAS.md`, to Extras: the stale key table and "early development build" lines in
`port/launcher/Read Me.txt`; `recomplauncher.json` in the zip; the zip's name; `progress` lines and
separate exit codes for `--install`; a licence file for SDL in the release.
