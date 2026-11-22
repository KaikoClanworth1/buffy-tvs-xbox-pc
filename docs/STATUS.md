# State of the port

Written 22 Nov 2026, from the notes each area keeps in this folder. Those notes are the detail; this page is the summary.

**How things were checked.** Most of the port has been checked by automated runs with the window hidden, the sound off and a scripted controller, through pictures and sound the game writes to files. That is called "hidden runs" below. "By hand" means the developer played it on a real screen. Nothing has been tried on a second PC.

| Goal | State |
|---|---|
| **1. Graphics**: the game's Direct3D 8 drawn natively, no emulation | ✅ Menus and levels draw through Direct3D 11. Some picture faults remain. |
| **2. Sound** through XAudio2 | ✅ Music, effects, speech and movie sound. Reverb and Doppler aren't built. |
| **3. Controls**: XInput and SDL pads, keyboard, rebinding | ✅ In. Hidden runs only: no real controller has been tried while playing. |
| **4. 4K and other resolutions, widescreen** | ✅ Up to 3840x2160; a wider view in the levels. |
| **5. 60 fps and above** | ✅ 60 fps in hidden runs; 100 to 175 fps by hand. |
| **6. No emulation** | ✅ |
| **7. Fullscreen, borderless, windowed** | 🔧 In. Switching between them hasn't been checked by hand. |
| **8. Report a Problem** that records what the player did | ✅ In. Hidden runs only. |
| **9. PC pages in the game's own menus** | ✅ Exit, Graphics, PC Audio, PC Controls, Memory, Report a Problem. Hidden runs only. |
| **10. Easy install** from the player's disc image | 🔧 Works on the developer's PC. No release yet. |
| **11. Memory view** | ✅ In. |

## What works

- **The game** from start-up into the levels: logos, intro movie, title, menus, a new game. By hand: parts of the Spanish mission, the high school, the hub and the Bronze, on a 2560x1440 screen. In hidden runs also the start of the cemetery, with a fight.
- **Picture**: resolutions from 640x480 to 3840x2160 or the desktop's; windowed, borderless and fullscreen; a frame-rate limit of 30, 60, 90, 120, 144, 165, 240 or none; VSync; a frame-rate display.
- **Widescreen**: *Original 4:3*, *Widescreen* (a wider view in the levels) or *Stretched*. Menus, the on-screen display and movies stay 4:3 with black sides.
- **Sound** through XAudio2: music, menu sounds, movie sound, speech, level effects and 3D panning, with *Master*, *Music*, *Effects* and *Speech* volumes.
- **Controls**: Xbox pads through XInput; DualSense, DualShock 4 and Switch Pro pads through SDL; keyboard and mouse; up to four pads, plugged in or out while playing. Bindings are changed in *Options → PC Controls*. Button pictures follow the device in use.
- **Report a Problem** (*Options*, the pause menu, or F12): a folder in `reports` with a picture, the log, the last things the player did, the settings and an input recording. A developer can start the game straight back at that spot from it. Crashes and hangs write their own reports into `crashes`.
- **Memory page**: the PC's memory and video memory, how much the game uses, and the game's own heap.
- **The launcher**: checks the disc image, copies the game's files, translates the game's code, compiles it with the compiler that comes in the download, and prepares the shaders. 7 minutes 38 seconds on the developer's PC with four processors. A stopped install carries on where it stopped. At install it also makes the game's icon from the disc.

## What doesn't yet

- **No level has been played through**, and going from one level to the next by playing hasn't been done.
- **Only the European (PAL) release** of the disc is supported. The North American disc is refused; it needs its own build.
- **No release yet.** The launcher can update from GitHub releases, tried only against a stand-in on the developer's PC. The install has run on the developer's PC only, never on a PC without developer tools installed.
- **Picture faults**: levels are dark at the game's default brightness (the game's own brightness setting cures it); menu and on-screen text is blocky at 4K; the faint sheen on characters and props doesn't shift as it should; the game's own screen captures aren't drawn; dark corners on the pause backdrop in widescreen; in cut-scenes a flame can show one wrong frame.
- **Some physics objects jump about.** The cause hasn't been found.
- **Sound**: scratchy voices and repeating sound were heard by hand and have been corrected, but the correction hasn't been listened to yet. No reverb, Doppler or headphone 3D. The mix can clip at full master volume. Lip movement and fights haven't been checked.
- **Controls**: no real controller, PlayStation or Switch pad has been tried while playing; rumble has never been felt; taking a new key on the bindings page hasn't been verified.
- **Not checked by hand**: VSync, switching fullscreen with F11 or Alt+Enter, the F12 key, the launcher's window during a build, the game's icon in Explorer and the taskbar.
- **Install folders** with letters outside plain English (ASCII) in their path are refused.
- **Reports** aren't cleaned of private details: the log in a report shows the Windows user name in folder paths.
