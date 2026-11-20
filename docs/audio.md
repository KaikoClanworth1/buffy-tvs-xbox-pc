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

## State (2026-11-20)

| | State | How it was checked |
|---|---|---|
| Mixer: Xbox ADPCM buffers, looping, cursors, pitch, volume, 3D pan | **works** | `btvs_audio_test rpe / pak / scene` on real dialogue and effects (Results) |
| Streams with packet completion | **works** in the test program | `btvs_audio_test scene`: 62 of 62 packets completed, byte count exact |
| XAudio2 device path | **works** | `btvs_audio_test device`: 15 of 15 runs keep time within 10 ms, no underruns, no XAudio2 glitches |
| `BTVS_MUTE` (no device, same timing), `BTVS_AUDIO_DUMP` | **works** | `btvs_audio_test live`: 6 of 6 runs within 15 ms of the wall clock on a loaded PC, no device opened |
| Game exe links with the audio layer | **works** | `bash port/tools/build.sh` |
| WMA music through the game's own decoder | **works** | `BTVS_WMA_TEST` on 4 tracks: right format, length within 0.1 s of the file's header, no gaps or clicks in the dump (In the game, below) |
| DSOUND replacement inside the game | **active** (regen of 2026-11-20) | log line `DirectSound replaced by the port's mixer`; every sound below went through it |
| Title / main-menu music | **works**; breaks up when the whole game hitches (not an audio fault, see below) | muted run, `BTVS_AUDIO_DUMP`: 95 s of music, 2 gaps (60 and 120 ms) at two frame hitches |
| Menu effects (move, select, back) | **work** | same run: 10 effects started by the scripted pad, each in the log and in the dump |
| Logo and intro movie sound | **present, but breaks up in test runs**: the movie decoder does not keep to real time there | dump has the sound of all five movies; 478-700 short gaps per run (below) |
| Music looping, track changes | **not checked** (no run was longer than the 119 s title track) | - |
| Dialogue, 3D sound, speech/effects split, clipping in a level | **not checked**: levels do not load yet (`docs/graphics.md`) | - |
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

## Still to do

1. Movie sound and music on an unhitched game: rerun the two runs above once the spinning thread
   and the shared test CPU are dealt with (`docs/AREAS.md`), and expect no gaps.
2. A run longer than a track, to see the music loop (decoder `Flush`) and change.
3. In a level, once one loads: effects and dialogue present, speech/effects split right
   (`BTVS_AUDIO_LOG` prints each voice's category), 3D pan, no clipping.
4. Audio lines still go to stderr; move them to `btvs_log` now that it exists.
5. Decide with Menu whether `[audio]` sliders drive the game's own three volumes instead of
   sitting on top of them.
6. Reverb from the I3DL2 parameters, if the levels sound too dry without it.
