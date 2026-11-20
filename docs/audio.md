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
| Mixer: Xbox ADPCM and PCM buffers, looping, cursors, pitch, volume | **works** | `btvs_audio_test` on real dialogue and effects, see Checks |
| Streams with packet completion | **works** (in the test program) | `btvs_audio_test scene` |
| XAudio2 device path | see Checks | `btvs_audio_test device` (zero-volume mix) |
| `BTVS_MUTE`, `BTVS_AUDIO_DUMP` | **works** | `btvs_audio_test live` |
| WMA music through the game's own decoder | see Checks | `BTVS_WMA_TEST` self-test |
| DSOUND replacement inside the game | **written, not yet linked in**: needs a regen with `manual_functions.json` (requested in `docs/AREAS.md`) and a game that boots | - |
| Music / effects / dialogue at the title screen and in a level | **not checked yet**: the game does not boot yet (`docs/graphics.md`) | - |

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
| `Sound\Music\*.wma` (62 files, 95 MB) | WMA v2 (format tag 0x161), 44100 Hz stereo, 128 kbit/s, in ASF |
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

**Output**. With a device the thread keeps 4 periods (40 ms) queued on an XAudio2 source voice
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

`BTVS_WMA_TEST` (below) proves it by running the translated decoder over a real music file.

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
bash port/tools/limit.sh $T port/runs/live.wav  live  $G/dialogue/G00_Dialogue.rpe 5    # real-time thread, BTVS_MUTE=1
bash port/tools/limit.sh $T port/runs/dev.wav   device $G/dialogue/G00_Dialogue.rpe 5   # XAudio2 device, zero-volume mix
bash port/tools/run.sh wma 120 BTVS_WMA_TEST=<G>\\Sound\\Music\\BFY_theme_shrt.wma BTVS_AUDIO_DUMP=<wav>
```

`rpe` / `pak` check for each sound: it plays for exactly its length, the cursor is at the middle
at half time, and it is not silent. `scene` checks that every stream packet completes and that
the looping sound keeps going. `live` checks that the mixer follows the wall clock without a
device and that none is opened; `device` that the XAudio2 path keeps time with no underruns (the
mix is at zero volume, so nothing is heard). None of these makes a sound.

Results: see the end of this file.

## To do once the game boots

1. Regen with `manual_functions.json` (requested in `docs/AREAS.md`), rebuild.
2. `bash port/tools/run.sh title 60 BTVS_AUDIO_DUMP=... BTVS_AUDIO_LOG=1`: the log should show
   `DirectSound replaced by the port's mixer`, buffers starting, and the dump should hold the
   title music.
3. Check in a level: effects and dialogue present, speech/effects split right (`BTVS_AUDIO_LOG`
   prints each voice's category), no clipping (`btvs_audio_stats`), music loops.
4. Decide with Menu whether `[audio]` sliders drive the game's own three volumes instead of
   sitting on top of them.
5. Reverb from the I3DL2 parameters, if the levels sound too dry without it.
