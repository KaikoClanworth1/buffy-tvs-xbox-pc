# Audio

The game's sound on **XAudio2**: the Xbox DSOUND library is cut off at its API and replaced by a
software mixer whose final mix goes to one XAudio2 source voice. No MCPX/APU emulation, no DSP
image, no legacy DirectSound.

| File | What |
|---|---|
| `port/src/btvs_audio.h/.c` | the mixer (voices, Xbox ADPCM, resampling, 3D pan, streams) and the XAudio2 device. No game code in it |
| `port/src/btvs_audio_dsound.c` | every DSOUND entry point the game calls, mapped onto the mixer |
| `port/src/btvs_wma.c` | WMA music: why the game's own decoder is kept, and its self-test |
| `port/src/btvs_audio_test.c` | `btvs_audio_test.exe`: plays real game sound data to a WAV with no game running |
| `port/src/manual_functions.json` | the 38 DSOUND functions replaced by host code |

## State (2026-11-21)

| | State | How it was checked |
|---|---|---|
| Mixer: Xbox ADPCM buffers, looping, cursors, pitch, volume, 3D pan | **works** | `btvs_audio_test rpe / pak / scene` on real dialogue and effects (Results) |
| Sound quality of voices | **fixed 21 Nov** after the owner heard them as scratchy: a wrong sample in every ADPCM block and a crude resampler (Scratchy voices, below). The owner has not heard the fix yet | dialogue lines decoded independently match the game's output to 32-43 dB (`port/tools/audio_cmp.py`) |
| Streams with packet completion | **works** in the test program | `btvs_audio_test scene`: 62 of 62 packets completed, byte count exact |
| XAudio2 device path | **works** | `btvs_audio_test device`: 15 of 15 runs keep time within 10 ms, no underruns, no XAudio2 glitches |
| `BTVS_MUTE` (no device, same timing), `BTVS_AUDIO_DUMP` | **works** | `btvs_audio_test live`: 6 of 6 runs within 15 ms of the wall clock on a loaded PC, no device opened |
| Game exe links with the audio layer | **works** | `bash port/tools/build.sh` |
| WMA music through the game's own decoder | **works** | `BTVS_WMA_TEST` on 4 tracks: right format, length within 0.1 s of the file's header, no gaps or clicks in the dump (In the game, below) |
| DSOUND replacement inside the game | **active** (regen of 2026-11-20) | log line `DirectSound replaced by the port's mixer`; every sound below went through it |
| Title / main-menu music | **works** | muted run on current main: 107 s of music, one dropout under 0.2 s (a slow frame of the test game); tracks are read into memory, so slow disk reads no longer break it |
| Menu effects (move, select, back) | **work** | same run: 10 effects started by the scripted pad, each in the log and in the dump |
| Logo and intro movie sound | **works** in a test run with a few short dropouts (was 867 gaps while the game had one CPU) | all five movies, right length (107 s), 14 dropouts under 0.2 s and one of 0.43 s, no clicks; an unloaded PC was not checked |
| Music looping | **works** | 190 s menu run: the 119 s track ends and starts again, second pass matches the first |
| Music moving on to the next piece | **fixed 21 Nov** (the piece before could play again; "keeps repeating", below). The owner has not heard the fix yet | 300 s cemetery run: the switch is in the log and the dump has the new piece |
| Looping buffers | **work** in the level | ambience loops had restarted 12 and 17 times when stopped |
| Dialogue (`.rpe`) in the level | **works** | the opening cutscene's seven lines are tagged speech, do not overlap and play to their end |
| Effects in the level (ambience, footsteps, attacks) | **work** | 466 buffers in a scripted run, effects follow the scripted presses, nothing clipped |
| 3D panning | **works** (was missing until 21 Nov) | 297 of 297 off-centre sounds louder on their side; not checked against the picture |
| Volume settings (Menu's PC Audio page) | **applied live**: master, music, effects by slider presses; speech by its setting in a level | pad script against the dump; speech at 50 starts each line at a quarter gain |
| Lip-sync against the picture, fights, later levels | **not checked** | - |
| PCM (non-ADPCM) buffers, stereo buffers | written, **not exercised**: every sound found on the disc so far is mono ADPCM; PCM is exercised only as a stream | - |

## How the game uses sound

All of it is in `winsound.obj` (the engine's `TSoundManager`), on top of the XDK 4432 DSOUND
library. Addresses are XBE addresses; names are from `final.map`.

**Effects and dialogue**: `CSoundBufferDS` / `CSoundBufferDS3D`. Every sound is a `CSample`
holding a whole RIFF/WAVE file in memory. For each playing instance the game calls
`IDirectSound_CreateSoundBuffer` (0 bytes, the sample's `WAVEFORMATEX`), then
`SetBufferData(sample data, size)`, `Play`, and every frame `Commit`: `SetHeadroom(0)`,
`SetVolume`, `SetFrequency`, and for 3D sounds `SetAllParameters` + `SetI3DL2Source`.
`Stop` is `Stop` + `SetBufferData(NULL, 0)`. Pause ("Freeze") is `Stop` / `Play`.
`GetCurrentPosition` and `GetStatus` drive lip-sync, subtitles timing and "is it still playing".

- No `Lock`/`Unlock`, no `SetCurrentPosition`, no play/loop regions, no mix-bin or pitch calls, no
  notifications: they are not even linked into the XBE.
- 3D: the game works out distance attenuation, occlusion and obstruction itself
  (`CSoundBufferDS3D::ComputeDBVolume`, `S_GetAttenuation`) and sends the result with `SetVolume`.
  It sets `flMinDistance = flMaxDistance = 1e6` in `DS3DBUFFER` (0x4C bytes in this XDK), so
  DSOUND adds no roll-off; DSOUND's 3D only supplies **direction**.
- The listener (`CSoundListenerDS::Commit`): `SetPosition`, `SetOrientation`, `SetVelocity`,
  `SetDistanceFactor`, `SetRolloffFactor`, `SetDopplerFactor`, `SetI3DL2Listener` (all deferred),
  then `CommitDeferredSettings`.
- Formats on the disc: every dialogue line and every effect checked is **Xbox ADPCM, 22050 Hz,
  mono** (format tag 0x69, 36-byte blocks of 64 samples).

**Music**: `CMusicBufferDS` (`IMusic.obj` decides what plays). The standard XDK streaming
pattern, on the game's own thread (`MultiThread_Callback`, every 20 ms):

1. `fopen` the `.wma`; `WmaCreateInMemoryDecoder(WMAStreamCallback, this, 0, &wfx, &decoder)`.
   The decoder asks for file bytes through the callback (`fseek` + `fread` into two alternating
   buffers).
2. `DirectSoundCreateStream` with `dwMaxAttachedPackets = 6` and the decoder's format, no callback.
3. Pump: for each of 6 packet slots whose status is no longer "pending":
   `decoder->Process(NULL, &packet)` for 0x2000 bytes of PCM (`ProcessSource`; a short read means
   end of file, then `decoder->Flush()` restarts it: that is how music loops), then
   `stream->Process(&packet, NULL)` with `pdwStatus = &status[i]` (`ProcessRenderer`).
4. `IDirectSoundStream_SetVolume`, `IDirectSoundStream_Pause`.

The stream is called through its vtable (`0x0071295C`: AddRef, Release, GetInfo, GetStatus,
Process, Discontinuity, Flush).

**Movies** (`CWMVPlayer`, Graphics area): `DirectSoundCreateStream` with 0x40 packets,
`IDirectSoundStream_Pause`, `SetVolume`, and `IDirectSound_GetTime` as the video clock. They get
the same stream implementation, and `GetTime` returns the mixer clock.

**Start-up** (`TSoundManager::Init`): `DirectSoundCreate`, `DirectSoundUseFullHRTF`,
`DownloadEffectsImage` (reads `Sound\dsstdfx.bin`), `SetCooperativeLevel`, `EnableHeadphones`.
`DirectSoundDoWork` is called every frame.

### DSOUND entry points replaced (38)

| | Functions |
|---|---|
| Device | `DirectSoundCreate`, `IDirectSound_Release`, `_SetCooperativeLevel`, `_EnableHeadphones`, `_DownloadEffectsImage`, `_GetTime`, `DirectSoundDoWork`, `DirectSoundUseFullHRTF` |
| Listener | `IDirectSound_SetPosition`, `_SetOrientation`, `_SetVelocity`, `_SetDistanceFactor`, `_SetRolloffFactor`, `_SetDopplerFactor`, `_SetI3DL2Listener`, `_CommitDeferredSettings` |
| Buffers | `IDirectSound_CreateSoundBuffer`, `IDirectSoundBuffer_Release`, `_SetBufferData`, `_Play`, `_Stop`, `_GetStatus`, `_GetCurrentPosition`, `_SetVolume`, `_SetHeadroom`, `_SetFrequency`, `_SetAllParameters`, `_SetI3DL2Source` |
| Streams | `DirectSoundCreateStream`, `IDirectSoundStream_SetVolume`, `_Pause`, and the vtable methods `CDirectSoundStream::AddRef`, `Release`, `GetInfo`, `GetStatus`, `Process`, `Discontinuity`, `Flush` |

That is every DSOUND function game code reaches (found by listing the callers of each DSOUND
symbol in the translated code, plus the stream vtable). `XAudioCalculatePitch` is plain maths and
stays translated. Everything below the API (`CMcpxAPU`, `CMcpxVoiceClient`, `CHrtfSource`, ...)
is no longer reached.

### Sound data

| Where | What |
|---|---|
| `Sound\Music\*.wma` (62 files, 95 MB) | WMA v2 (format tag 0x161), stereo, 44100 Hz (at least one track, `bela_short`, is 48000 Hz), 128 kbit/s, in ASF |
| `dialogue\*.rpe` (8 files, 366 lines) | container: `u32 version (1)`, `u32 count`, 24 reserved bytes, then `count` × `{ char name[32]; u32 offset; u32 size; u32 stamp; }`; each entry is a RIFF/WAVE (Xbox ADPCM 22050 Hz mono) with lip-sync chunks `wrds` and `phon` after `data` (parsed by `ESound.obj`). Read by `TResourcePack::ReadIndex` (`StdRPE.obj`) |
| `pak\*.pak` (65 files) | effects are whole RIFF/WAVE files inside the paks (137 in `res_null.pak`), same format, no lip-sync chunks |
| `Sound\dsstdfx.bin` | DSP program for the console's effects processor: I3DL2 reverb, cross-talk filters. Not usable off the console; see below |

## The replacement

**Mixer** (`btvs_audio.c`). One thread mixes 10 ms periods at 48 kHz stereo. Each DSOUND buffer or
stream is a voice:

- buffers are decoded **in place from game memory** as they play (ADPCM one 64-sample block at a
  time), exactly the bytes the game attached with `SetBufferData`;
- streams copy each packet, and when the mixer has played it the packet's status word goes from
  pending (`0x8000000A`) to `0` and its completed size is filled in, which is what the game polls;
- linear-interpolating resampler (22.05 / 44.1 kHz → 48 kHz, and `SetFrequency` pitch);
- volume in hundredths of a dB minus headroom; gains are ramped across each period, so volume and
  pan changes do not click;
- 3D: direction only (see above), as a constant-power pan against the listener's right vector
  (left-handed, like D3D), over 80 % of the arc so the far ear is never empty;
- the play cursor (`GetCurrentPosition`) is the mixer's own read position, in bytes.

**Output**. With a device the thread keeps 5 periods (50 ms) queued on an XAudio2 source voice
(48 kHz 16-bit stereo, no SRC) and is woken by XAudio2's buffer-end callback. If the device goes
away the mixer carries on silently and tries again every 2 s.

**Why not one XAudio2 voice per game buffer**: muted test runs have to behave exactly like audible
ones (cursors, packet completion, the audio clock) with no device open, and `BTVS_AUDIO_DUMP` has to
capture the final mix even when muted. With the mix made in the port both are the same code path.

**Mute** (`BTVS_MUTE=1`): no XAudio2 object is created at all. The same thread mixes the same
periods, paced by the performance counter instead of the device.

**Dump** (`BTVS_AUDIO_DUMP=<path.wav>`): the final mix, 48 kHz 16-bit stereo, written as it is
mixed (after all volumes). The header is kept up to date twice a second, so a run that is killed
still leaves a valid file.

**Volumes**: `[audio] master`, `music`, `effects`, `speech` (0..100, Menu's settings store; applied
live through a change callback). The slider value is squared. Streams are *music* (and movie
sound); a buffer whose sample is followed by a `wrds`/`phon` chunk is *speech*; every other buffer
is *effects*. These sit on top of the game's own music / SFX / voice volumes, which the game
applies itself before `SetVolume`.

**Linking**. `manual_functions.json` names the 38 functions; after a regen the recompiler leaves
their bodies out, and `/alternatename` in `btvs_audio_dsound.c` points each name at its host
function. Before the regen the original bodies are still in `btvs_recomp.lib` and win, so the file
links either way.

### WMA: the game's own decoder, not Media Foundation

The Xbox WMA decoder (XBE sections `WMADEC`, `WMADECXM`) is plain x86 with no hardware under it; it
is already translated with the rest of the game. It is **kept**: the game creates it, feeds it
file bytes and gets PCM exactly as on the console, and that PCM lands in the stream functions
above. Reasons:

- nothing to replace: the game talks to the decoder through an object with twelve methods and its
  own read callback; a Media Foundation version would have to imitate all of that, including
  `Flush` (looping) and short reads;
- the same samples as the console, on every PC, with no codec dependency (Windows N editions have
  no Media Foundation);
- it only calls `malloc`/`free`, `pow`, and for the unused file-handle mode a few Win32 file
  functions.

`BTVS_WMA_TEST` runs the translated decoder over a real music file to prove this. It passes since
the regen of 2026-11-20 (In the game, below).

### Not modelled

| What | Effect | Notes |
|---|---|---|
| I3DL2 reverb (`SetI3DL2Listener`, `SetI3DL2Source`), `dsstdfx.bin` | no environmental reverb, no occlusion filtering (occlusion *volume* still works: the game computes it) | the DSP image only runs on the console's effects processor. A host reverb fed from the same I3DL2 parameters is the way to add it |
| Doppler (`SetDopplerFactor`, velocities) | no pitch shift on fast-moving sources | |
| Sound cones | none | the game leaves the cone wide open |
| HRTF | plain pan instead of the console's HRTF filters | |
| Stream packet callbacks / completion events | not signalled (logged once if a packet carries one) | the game's music and movie code poll the status word instead |

## Checks

Test runs are silent, so everything is checked from WAV files and counters.

```bash
bash port/tools/build.sh btvs_audio_test
T=port/build/Release/btvs_audio_test.exe ; G=<main checkout>/game_files
bash port/tools/limit.sh $T port/runs/dlg.wav   rpe   $G/dialogue/G00_Dialogue.rpe      # every line, one by one
bash port/tools/limit.sh $T port/runs/sfx.wav   pak   $G/pak/res_null.pak              # every effect in a pak
bash port/tools/limit.sh $T port/runs/scene.wav scene $G/dialogue/G00_Dialogue.rpe      # 3D circle, pitch sweep, fade, stream
bash port/tools/limit.sh $T port/runs/late.wav  late  $G/dialogue/G00_Dialogue.rpe      # a stream that ran dry skips late sound, music does not
bash port/tools/limit.sh $T port/runs/live.wav  live  $G/dialogue/G00_Dialogue.rpe 5    # real-time thread, BTVS_MUTE=1
bash port/tools/limit.sh $T port/runs/dev.wav   device $G/dialogue/G00_Dialogue.rpe 5   # XAudio2 device, zero-volume mix
bash port/tools/run.sh wma 120 BTVS_WMA_TEST=<G>\\Sound\\Music\\BFY_theme_shrt.wma BTVS_AUDIO_DUMP=<wav>
```

`rpe` / `pak` check for each sound: it plays for exactly its length, the cursor is at the middle
at half time, and it is not silent. `scene` checks that every stream packet completes and that
the looping sound keeps going. `live` checks that the mixer follows the wall clock without a
device and that none is opened; `device` that the XAudio2 path keeps time with no underruns (the
mix is at zero volume, so nothing is heard). None of these makes a sound.

In the game (muted, hidden; give each run its own `BTVS_GAME_CPUS` while test games share a CPU):

```bash
bash port/tools/run.sh aud_menu 100 BTVS_SKIP_MOVIES=1 BTVS_GAME_CPUS=400 BTVS_AUDIO_DUMP=<wav> BTVS_AUDIO_LOG=1 BTVS_FPS_LOG=1   "BTVS_PAD_SCRIPT=after title; 12.0 START; after menu; 4.0 DOWN; +1.5 DOWN; +1.5 UP; +1.5 UP; +2 A; +5 B; +3 DOWN; +1 DOWN; +1 UP"
bash port/tools/run.sh aud_movie 150 BTVS_GAME_CPUS=800 BTVS_AUDIO_DUMP=<wav> BTVS_AUDIO_LOG=1 BTVS_FPS_LOG=1   # logos, intro, title
grep -E "^\[audio\]" port/runs/aud_menu.err      # buffer starts; per stream and second: packets in, queued, ran dry, late frames dropped
```

Results are below.

## Results before the game booted (2026-11-20)

All runs: `btvs_audio_test` on CPUs 8-15 at low priority while other sessions were building (so
the PC was busy), each with a timeout. **No run made a sound**: `rpe`, `pak` and `scene` use the
offline mixer (no device, no thread); `live` runs with `BTVS_MUTE=1`; `device` opens the XAudio2
device but mixes at zero gain *and* sets the device's master voice to volume 0, and every such run
reported a mix peak of exactly 0.

| Run | Result |
|---|---|
| `rpe G00_Dialogue.rpe` | 22 of 22 lines: right length, cursor at the middle at half time, peaks 0.99-1.00. Dump 161.6 s, RMS 0.168, 0.2 % of the energy above 8 kHz (speech, not decoder noise) |
| `pak res_null.pak` | 137 of 137 effects pass the same checks. Dump 107.4 s |
| `scene` | loop circling in 3D (left/right balance moves through the dump), pitch sweep, fade, one-shots, and a 44.1 kHz PCM stream: 62 of 62 packets completed. The scene clips on purpose (peak 1.28: full-scale dialogue plus a tone plus one-shots) to exercise the clamp |
| `live` (muted, real-time thread) | 6 runs: mixed time within -9..+15 ms of the wall clock over 7-9 s; no device opened |
| `device` (XAudio2, zero volume) | 15 runs after the fix below: within -5..+9 ms over 5-10 s, 0 underruns, 0 XAudio2 glitches, 0 ms stalled |
| `BTVS_WMA_TEST doom_hit2.wma` | failed at start then (see below); passes since the regen (In the game) |

Two problems these runs found and fixed:

- **Second-long freezes of the audio clock** with the device open (up to 1.9 s in 5 s). Cause: the
  WAV dump was written from the mixer thread and the disk was busy. The dump now has its own
  writer thread and an 8 s ring; the mixer thread never touches the disk. Independently of that,
  if the device itself ever stops taking data for more than 60 ms, the mix now carries on by the
  wall clock and drops those periods (counted in `btvs_audio_stats().stalled_frames`), so the
  game's sound-timed logic cannot freeze with the device. The mixer thread runs under the
  multimedia scheduler ("Pro Audio") when a device is open.
- **The muted clock fell behind on a starved CPU** (it gave up after 0.5 s of lag). It now catches
  up after hold-ups of up to 3 s.

**WMA self-test, as it failed before the regen.** `XapiInitProcess` runs, the process heap comes up, the decoder object is
constructed, and it reads the file through the callback (30 bytes at 0, 24 bytes at 30: the ASF
header and the first object header). Then it rejects the file with `0x80004005`. Cause: a
recompiler bug, not the decoder. `WMA_ParseAsfHeader` compares each object's GUID with
`repe cmpsd` + `jne`; the lifter only treated the *byte* forms (`cmpsb`/`scasb`) as setting the
flags, so the branch after a `repe cmpsd` tested the flags of the instruction before it and always
fell through to the first case. Every object was parsed as a File Properties object. **All 38**
`repe cmpsw/cmpsd` / `repne scasw/scasd` sites in the game are affected (GUID, palette and
wide-string compares), so this matters for the boot too. Fixed in
`xboxrecomp/tools/recomp/lifter.py` (flag tracking now covers every width); it takes effect at the
next regen. The self-test has to be run again after that; until it passes, "the game's own
decoder works" is the plan, not a result. If it turns out not to (the decoder also has MMX paths
picked by a CPUID check), the fallback is to replace `WmaCreateInMemoryDecoder` and the decoder's
methods with Media Foundation.

Also seen in that run, outside Audio: `[ICALL] Failed to resolve VA 0x005804A4` inside
`XapiInitProcess` (a computed jump inside `memmove_00580200`). The Chaos Bleeds port replaced
`memmove` with a host function for the same reason.

## In the game (2026-11-20, after the regen)

The game boots to its title screen and main menu; levels do not load yet. All runs through
`port/tools/run.sh`: muted (`BTVS_MUTE=1`, no audio device opened), hidden, CPUs 8-15, with a time
limit. **No run made a sound.** Sound was judged from the `BTVS_AUDIO_DUMP` WAV with a script
(loudness per 10 s, silent spans, gaps of 10-200 ms inside sound, sample steps, clipped samples)
and from `BTVS_AUDIO_LOG=1` (every buffer start; once a second per stream: packets in, queued,
times run dry, late frames dropped).

**WMA decoder self-test** (`BTVS_WMA_TEST`, no game running): passes.

| Track | Decoder says | Decoded | File header | Dump |
|---|---|---|---|---|
| `bela_short.wma` | 48000 Hz stereo | 135.04 s | 135.12 s | no gaps, no clicks, 9 clipped samples (full-scale music) |
| `theme_buffywaits02.wma` (title) | 44100 Hz stereo | 118.75 s | 118.84 s | no gaps, no clicks, no clipping |
| `BFY_beauty1.wma` | - | 64.43 s | 64.47 s | not analysed |
| `agnus_dei_1.wma` | - | 8.17 s | 8.14 s | not analysed |

It decodes at 22-29 times real time on one CPU. Media Foundation is not needed.

**Title and main menu** (`BTVS_SKIP_MOVIES=1`, pad script: Start at the title, then down, down,
up, up, A, B, down, down, up; 100 s):

- Music: the game opens a 44100 Hz stereo stream of 6 packets and feeds it about 22 packets
  (180 KB) a second. The dump holds music from 3.4 s to the end at the level the game sets
  (RMS 0.05-0.06, peak 0.34, nothing clipped).
- Effects: 10 buffer starts, one per scripted press (Xbox ADPCM, mono, 22050 and 44100 Hz, volume
  -1166 and -1666), all category "effects".
- Faults: the music ran dry twice (60 ms and 120 ms of silence) in the best run, 7 times (up to
  0.9 s) in another. Every gap sits on a frame hitch of the whole game (`BTVS_FPS_LOG`: worst
  frames of 160-1000 ms at an idle menu). The game's music thread wakes every 20 ms and keeps only
  0.28 s queued, and it shares the game's one CPU with a thread that spins on a kernel wait
  (about a million `KeWaitForMultipleObjects` calls a second). That is in the runtime, not in the
  audio code; reported to Graphics in `docs/AREAS.md`. The mixer did what the console's would: it
  plays silence while the queue is empty and carries on with the next packet.

**Logos and intro movie** (150 s): the sound of all five movies is in the dump, then the title
music. It is **not clean**: 478-700 gaps of 10-200 ms and a dozen longer ones per run, because the
movie decoder ran at a median of 21-25 frames a second with hitches up to a second and delivered
its sound late. Two things in the test set-up make this worse than it would be for a player, and
neither was removed: test games run at below-normal priority, and every test game puts the game's
threads on the same CPU (CPU 8) unless `BTVS_GAME_CPUS` says otherwise (with two test games at
once the movies ran at 7 frames a second). Whether movie sound is clean on an unloaded PC is
**not known**.

One audio fault was found there and fixed. The movie's picture follows the audio clock. When the
decoder falls behind, the clock runs on, the player decodes faster to catch up, and its sound
arrives in a burst: the queue filled to the mixer's limit of 128 packets, further packets were
refused (sound lost, clicks), and the sound trailed the picture by seconds. Now a stream that ran
dry for up to 2 s skips late sound once it is there, down to a reserve of 0.35 s, so picture and
sound are back together after a hitch. Music never has more than 0.28 s queued, so it is never
skipped and carries on where it stopped. Checked offline with `btvs_audio_test <wav> late <rpe>`:
500 ms gap then a 1.86 s backlog: 501 ms skipped; then a 557 ms backlog: 207 ms skipped; then 6
packets: nothing; a 3 s stop then a new piece: nothing; every packet completed in all four. In
the game the case did not come up again after the fix (the queue stayed at 32 packets or under),
so there it is **not confirmed**.

The earlier offline tests (`rpe` 22 of 22, `pak` 137 of 137, `scene` 62 of 62 packets) still pass
after these changes.

## In a level (2026-11-21)

Build `dae87b9` and later, first level (`m00_spanishmission_1`), all runs muted and hidden through
`port/tools/run.sh` with `BTVS_AUDIO_DUMP` and `BTVS_AUDIO_LOG=1`. **No run made a sound.** The
game restarts itself to load a level, so a run writes two dumps: `<name>.wav` (menus) and
`<name>_r1.wav` (the level).

```bash
bash port/tools/run.sh aud_lvl 225 BTVS_SKIP_MOVIES=1 BTVS_GAME_CPUS=400 BTVS_AUDIO_LOG=1 BTVS_FPS_LOG=1 BTVS_INPUT_LOG=1 BTVS_AUDIO_DUMP=runs/audio/lvl.wav \
  "BTVS_PAD_SCRIPT=1.0 START every 1.5; after menu; 2.5 A every 2.5; after menu12; 2.5 A every 2.5; after menu13; 2.5 A every 2.5" \
  "BTVS_PAD_SCRIPT_1=after level; 55 LSU for 4; 60 X; +0.6 X; +0.6 X; +0.6 Y; +0.8 A; 64 LSR for 3; 68 LSD for 3; 72 X; +0.5 X; +0.5 Y; 75 LSL for 4; 80 LSU for 6"
```

With `BTVS_AUDIO_LOG=1` every buffer start is logged with its length, category (`effect` /
`speech`), the gains it starts with and, for a 3D sound, its position and the listener's; a
stopped loop says how often it had started again, a sound stopped early says where, and a speech
line that reaches its end says so.

**What was verified (two level runs, 190 s and 205 s of level each)**

- **Sound in the level**: 243 and 466 buffers started, all Xbox ADPCM mono (22050 Hz, some 11025
  Hz), all 3D. Nothing clipped in either dump (peak 0.92), no dropouts in the mix.
- **Dialogue (`.rpe`)**: the opening cutscene's seven lines (5.87, 5.48, 5.42, 0.79, 7.88, 5.52,
  5.20 s) are tagged `speech`, start one after the other and never overlap, and all seven **play
  to their end**; the cutscene ends 2.3 s after the last line (cutscene 50.1 s and 50.6 s long in
  the two runs; the first line starts 5.7 / 6.0 s in, the last ends 47.8 / 48.3 s in).
- **Looping buffers**: the level's ambience loops (1.75 s and 2.42 s buffers) had started again 17
  and 12 times when the game stopped them.
- **Footsteps and attacks**: with the scripted stick the player moves (trail positions change) and
  0.26-0.43 s effects start at walking pace; each scripted X / Y press starts its effects within
  0.1 s. No enemy was fought (the script does not reach one).
- **3D panning: was broken, fixed.** In the first run all 243 sounds started with left = right. The
  game sets DSOUND's distance factor to the smallest float there is (it does its own distance
  roll-off and sends it with `SetVolume`), and the mixer took the direction only when the *scaled*
  distance was above zero. The direction now comes from the positions as they are. In the second
  run 297 of 297 off-centre sounds are louder on the side they are on (checked against position,
  listener position and listener front from the log; 161 more are within 16 units of the listener
  or straight ahead and are centred), and the dump's two channels differ by about 4 dB over
  10 s. The widest pan is 16 dB between the ears (the pan uses 80 % of the arc).
  Left/right follows DSOUND's left-handed convention (right = top x front); it was **not** checked
  against the picture.
- **Distance**: the game's own (`SetVolume` per sound, -3 dB for speech to -25 dB for far
  effects); the port adds none (`flMinDistance = flMaxDistance = 1e6`, roll-off factor 0).
- **Music in the level** plays through the same stream as in the menus (6 packets).
- **The loading screen's stream** (64 packets, made again every 2-3 s while the level loads) is fed
  at full volume but the dump is digital silence for the whole load: taken to be a silent sound
  track, not checked against the movie file.

**Not verified in a level**: lip-sync against the picture (the mouth follows
`GetCurrentPosition`, which is the mixer's own read position; only the sound side was checked),
combat against an enemy, occlusion, anything after the first minutes of the first level, other
levels, the pause menu's effect on sound.

## Looping music, volumes, movies again (2026-11-21)

**Music loop.** A 190 s run at the main menu (a DOWN press every 20 s, or the menu idles into a
movie after about 70 s): the 119.0 s title track plays through, fades out as the file itself does
(the decoder-only dump has the same fade), the game makes a new stream and the track starts again
1.0 s later; the first 3 s of the second pass match the first (correlation 0.99).

**The four volume settings, live.** The PC Audio page driven by pad script while music played
(`trail` shows each press; `[audio]` play lines show each effect's gain):

| Change | In the dump |
|---|---|
| Master 100 -> 50 (10 presses) | level falls to about a quarter (rms 0.045 -> 0.014; the slider is squared), back up with 50 -> 100 |
| Music 100 -> 0 | music gone (rms 0.003), menu effects still there at their old level |
| Effects 100 -> 0, step by step | each effect starts quieter than the last (gain 0.147 -> 0.000), then digital silence |
| Effects and Music back to 100 | both back |

`speech` uses the same per-category gain as `effects`; it was checked in the level with
`BTVS_AUDIO_SPEECH=50` (below), not by slider presses.

**Music gaps: two causes found, one fixed.**
1. *Slow file reads* (fixed). The decoder asks the game for file bytes on the music thread; reads
   of 200-300 ms were measured (the runtime also logs every 512-byte read), each one a hole in the
   music, since only 0.28 s is queued. The first read of a track now loads the whole file (1-3 MB)
   from the PC side in one go (6 ms) and later reads are copies (`BTVS_MUSIC_PRELOAD=0` turns it
   off). The game's own callback bookkeeping is kept; the first bytes are compared with what the
   game's own read returns, so a wrong file is never used.
2. *Whole-game hitches* (not audio, still there). After the fix every remaining gap sits on a frame
   that took 380-840 ms (`BTVS_FPS_LOG`), in the menus and in the level (level run: median 37
   frames a second in a test run, single frames up to 2.5 s). The music thread shares the game's
   one CPU with the main thread and does not run during such a frame. A run of 190 s at the main
   menu had 6 gaps of 0.3-0.6 s. The hitches were *not* caused by the spinning timer thread:
   with that fixed in a local experiment (below) they were unchanged.

**The spinning thread** is `XapiMmTimerThread`: it calls `KeWaitForMultipleObjects` on one guest
`KEVENT` (type 1, at a guest address) with no timeout; the runtime passes the guest address to
Windows as a handle, the wait fails at once (`ERROR_INVALID_HANDLE`) and the thread loops, about
a million times a second. Giving that wait the event's shadow, as `KeWaitForSingleObject` already
does, stops it (kernel calls in a 100 s menu run: 80 million -> 0.6 million). Tried locally, not
committed (runtime file); the diff is in the shared notes (`audio-kewait-experiment.diff`) and in
`docs/AREAS.md`.

**Movie sound: still breaks up, and why.** Logos and intro with the spin fix in place: the movie
stream ran dry 1122 times in 151 s, 867 gaps in the dump, the movies had not finished after 158 s
(they are about 100 s long). A profile of the game thread during the movies (50 samples): 66 % in
the translated WMV video decoder (`g_IDCTDecMMX_WMV2`, `DecodePMB`, motion compensation), 22 %
asleep, 12 % showing the frame. The decoder is simply slower than real time on the one CPU a test
game has, so the sound (decoded in the same loop) arrives late. Nothing is wrong on the audio
side; whether it keeps up on an unloaded PC at normal priority is **not known**.
The late-sound skip of 20 Nov still has not been seen working in the game (the queue never fills,
because the decoder never catches up).

## After the game went back to all CPUs (2026-11-21, main at `a70227f` and later)

Everything above was measured with the game's threads on one CPU (`BTVS_GAME_CPUS=400`, the
default until `a70227f`). Graphics then made the game's lock atomic and let its threads use all
the process's CPUs. Same runs again on that build, **without** `BTVS_GAME_CPUS`, two test games at
once, muted and hidden:

| Run | Before (one CPU) | Now |
|---|---|---|
| Main menu, 107 s of music | 6-7 gaps of 0.3-0.9 s, 35-55 frames a second | **1 dropout under 0.2 s**, the stream ran dry once, 60 frames a second (lowest second 41.8) |
| Logos and intro movie | 867 gaps, 156 clicks, not finished after 158 s | **finish after 107 s** (their length); 14 dropouts under 0.2 s and one of 0.43 s, no clicks, the stream ran dry 20 times in 149 s |
| First level, 115 s (this one still with `BTVS_GAME_CPUS=400`) | music ran dry in 45 of 150 seconds, median 37 frames a second | ran dry in 2 seconds, median 58; nothing clipped, no dropouts in the mix |

So movie sound and music are close to clean in a test run now; the few dropouts left sit on slow
frames of a below-normal-priority test game on a busy PC. Whether there are none on an unloaded
PC at normal priority was **not checked** (it needs the owner's ears or an unloaded run).
`[audio] speech = 50` in the level: the cutscene's lines start at a quarter of their gain (0.701
-> 0.175, the slider is squared), effects unchanged. The level's music (a 4.2 MB track) was read
ahead in 2 ms. The timer thread still spins on this build (178 million kernel calls in the run).

Use plain runs from now on; `BTVS_GAME_CPUS=400` / `800` in the commands above is only needed to
reproduce the old one-CPU behaviour.

## Scratchy voices (2026-11-21, the owner's first listen)

The owner heard the sound as "glitchy almost scratchy, mainly voices" on the 11:00 build. Two
faults in the mixer, both in every ADPCM sound and most audible on speech; both fixed.

1. **The ADPCM decoder played a padding sample in every block.** A 36-byte block is its header
   sample followed by 63 decoded ones; the 64th nibble is padding. The decoder skipped the header
   sample and played the padding nibble instead: one wrong sample out of 64, 344 times a second.
   Checked on the game's files: the 64th nibble is 0 in all 190,403 blocks of `res_null.pak` and
   `res_m00_spanishmission_1.pak` and in the dialogue, and the step index in a block's header is
   always the one reached after nibble 63, never after nibble 64.
2. **Voices were resampled with straight lines** between source samples. For a 22050 Hz voice that
   leaves copies of the sound above its own range only about 20 dB down, and dulls its top. The
   mixer now interpolates band-limited (16 source samples per output sample, Kaiser-windowed sinc,
   256 positions).

Measured by decoding the same lines with an independent decoder (`port/tools/audio_cmp.py`: header
sample + 63, exact FFT resampling to 48 kHz) and laying them over the mixer's output. "Match" is the
line over the difference; "comb" is the part of the difference that sits on multiples of 344.5 Hz
(one fault per block).

| Offline, three lines of `G00_Dialogue.rpe` | Before | After |
|---|---|---|
| Match | 10.4 / 23.1 / 19.9 dB | 39.8 / 39.7 / 35.7 dB |
| Comb (of the line's energy) | -26.6 / -44.6 / -38.0 dB | -58.3 / -65.6 / -57.1 dB |
| Energy above 11 kHz (the source has none) | -30.4 / -36.4 / -27.8 dB | -41.3 / -48.2 / -38.9 dB |
| Largest single-sample difference (full scale = 1) | 1.30 / 0.95 / 0.54 | 0.10 / 0.06 / 0.11 |

The right decoder with the old straight-line resampler would give a match of 22-26 dB (computed),
so both changes count.

| In the game, the seven lines of the first level's opening cutscene | Before (`lvl2`, music and ambience playing too) | After (`aud_r5`, music and effects at 0) |
|---|---|---|
| Comb | -13.8 to -31.4 dB | -49.9 to -62.6 dB |
| Match | 2.6 to 14.8 dB (includes the other sound) | 31.6 to 42.6 dB |
| Largest difference | - | 0.011 to 0.139 of a 0.74 peak |
| Clipped samples, dropouts | 0, 0 | 0, 0 |

```bash
bash port/tools/run.sh aud_r5 120 BTVS_SKIP_MOVIES=1 BTVS_MAP=m00_spanishmission_1 BTVS_AUDIO_LOG=1 \
  BTVS_AUDIO_MUSIC=0 BTVS_AUDIO_EFFECTS=0 BTVS_AUDIO_DUMP=runs/audio/r5_lvl.wav
python port/tools/audio_cmp.py port/runs/audio/r5_lvl.wav port/runs/aud_r5.err <main checkout>/game_files/install.dat
```

The cutscene lines are in `install.dat` (RIFF/WAVE with `wrds` / `phon` chunks), not in
`dialogue/*.rpe`. The offline tests (`rpe` 22 of 22, `pak` 137 of 137, `scene`, `late`) pass on the
new mixer. Band-limited interpolation overshoots a little on sources recorded at full scale (peak
1.06 for a dialogue line at full volume); in the game speech plays at 0.70 and nothing clipped.

**Ruled out**: clipping (0 samples in every level dump), wrong format or rate (lengths and the
overlay match), gaps or loop faults in speech (seven lines whole, no dropouts in the dump).

**Not known**: whether this is all the owner heard. The dump is what goes to the device, so
anything after it (the device queue running empty, XAudio2 glitches) does not show in a muted run.
From this build on the log gets a `[audio] device:` line, at most every 30 s and only when
something happened, counting empty queues, XAudio2 glitches, dropped periods and clipped samples.
If the owner still hears crackle, that line in `btvs_log.txt` says whether it is the device side.

## "Some audio keeps repeating when fighting" (2026-11-21, the owner on the 19:22 build)

**Found and fixed: a music piece could play again in place of the next one.** The port reads each
music track into memory at its first read (Music gaps, above) and kept it for the music object
that asked. The game uses a music object and its `FILE` again for the next piece, and the only
check against that was the first read's bytes: the decoder's first read is 30 bytes, and **59 of
the 62 tracks in `Sound\Music` begin with the same 30 bytes** (the other three, `fight_02/03/04`,
share theirs). So the held track was handed out again: the piece before played a second time
instead of the new one. The held track is now tied to the track's name.

Caught in a run (cemetery, 300 s, music only in the dump): at 237.3 s the game put
`bvs_waits_lp1.wma` into the object that held `theme_buffywaits03.wma`. With the fix the log says
`music: ... follows ... in the same music object; the held track is dropped` and the dump after
that point is the new piece (the first 4 s of the old one are found nowhere in 236-250 s,
correlation -0.03; the same test finds them at 155.7 s with 1.000). The old build was not run
again for this; that it replayed the old piece follows from the code and the identical headers.

**Effects and speech in a fight: nothing repeats that the game did not ask for.** Two cemetery
fights with scripted attacks (`BTVS_MAP=m03_cemetery_1`, X every 0.5 s, Y every 2.3 s), 60 frames
a second and a 240 limit (80-105 reached in the test run), with every `Play`, `Stop` and
`SetBufferData` the game makes in the log (`BTVS_AUDIO_LOG=1`, lines `ds   #n ...`):

| | 60 fps | 240 limit |
|---|---|---|
| `Play` calls | 1419 | 1566 |
| on a buffer that was still playing | 0 | 0 |
| `Play` without new sample data (a resume or replay) | 0 | - |
| `Stop` not followed by the game detaching the sample | 0 | - |
| one-shots started with the looping flag | 0 audible (18 loops at -100 dB, each stopped within 50 ms) | 7 such |
| same sample at the same place in the same frame | 10 doubles | 9 doubles, 3 triples |
| loops left running loud | none (two ambience loops, stopped at exit) | none |

Every one-shot ends once; `GetStatus` goes to "not playing" and the game then stops and detaches
it. The most played audible samples are hit and grunt variants at irregular times (27 plays of
the commonest in 100 s). So the suspects on the effect side (one-shots looping, status never
ending, `Stop` ignored, stale buffer data, the new resampler's end handling) were not seen.

**Not known**: whether the music fault is what the owner heard; they did not say which sound. No
recording of their fight exists (`btvs_frames.rec` covered other levels). If it is still there
after this build, a Report a Problem in the fight, or the name of the sound, is needed.

## Still to do

1. An unloaded run at normal priority (or the owner listening): movies, menu music and a level
   with no dropouts at all.
2. `XapiMmTimerThread` still spins (`docs/AREAS.md` has the fix that was tried).
3. Lip-sync against the picture; a fight; later levels; the pause menu's effect on sound.
4. Decide with Menu whether `[audio]` sliders drive the game's own three volumes instead of
   sitting on top of them. Movie sound currently follows the Music slider.
5. Reverb from the I3DL2 parameters, if the levels sound too dry without it.
