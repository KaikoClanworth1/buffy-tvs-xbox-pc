"""Compare the speech in a BTVS_AUDIO_DUMP with the same lines decoded here from the game's files.

    python port/tools/audio_cmp.py <dump.wav> <log with BTVS_AUDIO_LOG=1 lines> <source>...

<source>: files that hold the lines as RIFF/WAVE (Xbox ADPCM 22050 Hz mono): game_files/install.dat
(the cutscenes' lines), dialogue/*.rpe, pak/*.pak. Every `play ... speech` line of the log is looked
up in the sources by its length, decoded with this file's own decoder (independent of the port's),
resampled to 48 kHz exactly (FFT) and laid over the dump. Printed per line:

  match        the line over (dump - line), in dB. 30 dB and more: the dump is the line. Other sound
               playing at the same time counts as difference, so dump with BTVS_AUDIO_MUSIC=0
               BTVS_AUDIO_EFFECTS=0 for a clean figure.
  above 11 kHz how much of the dump's energy lies above the line's own range (resampler leftovers)
  comb         how much of the difference sits on multiples of 344.5 Hz (22050 / 64: one fault per
               ADPCM block, the rasp fixed on 21 Nov)
  largest difference   a click that is not in the source shows here

Needs numpy.
"""
import re
import struct
import sys
import wave

import numpy as np

STEPS = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80,
         88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
         598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749,
         3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
         12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8] * 2


def decode(data):
    """Xbox ADPCM, mono: per 36-byte block the header sample, then 63 nibbles (the 64th is padding)."""
    out = []
    for b in range(len(data) // 36):
        blk = data[b * 36:(b + 1) * 36]
        pred, idx = struct.unpack_from('<hB', blk, 0)
        idx = min(idx, 88)
        out.append(pred)
        k = 0
        for byte in blk[4:]:
            for nib in (byte & 15, byte >> 4):
                k += 1
                if k == 64:
                    break
                step = STEPS[idx]
                diff = step >> 3
                if nib & 4: diff += step
                if nib & 2: diff += step >> 1
                if nib & 1: diff += step >> 2
                pred = max(-32768, min(32767, pred - diff if nib & 8 else pred + diff))
                idx = max(0, min(88, idx + INDEX[nib]))
                out.append(pred)
    return np.array(out, float) / 32768


def up(x):
    """22050 -> 48000 Hz (320/147), exactly band-limited."""
    n = len(x) // 147 * 147
    spec = np.fft.rfft(x[:n])
    m = n // 147 * 320
    wide = np.zeros(m // 2 + 1, complex)
    wide[:len(spec)] = spec
    return np.fft.irfft(wide, m) * (m / n)


def waves(path):
    """Every RIFF/WAVE inside a file: (offset, fmt chunk, data chunk)."""
    d = open(path, 'rb').read()
    out, i = [], 0
    while True:
        i = d.find(b'RIFF', i)
        if i < 0:
            return out
        if d[i + 8:i + 16] != b'WAVEfmt ':
            i += 4
            continue
        w = d[i:i + 8 + struct.unpack_from('<I', d, i + 4)[0]]
        p, fmt, data = 12, None, None
        while p + 8 <= len(w):
            cid, cs = w[p:p + 4], struct.unpack_from('<I', w, p + 4)[0]
            if cid == b'fmt ': fmt = w[p + 8:p + 8 + cs]
            if cid == b'data': data = w[p + 8:p + 8 + cs]
            p += 8 + cs + (cs & 1)
        if fmt and data is not None:
            out.append((i, fmt, data))
        i += 8


def band(x, lo, hi):
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1 / 48000)
    return spec[(f >= lo) & (f < hi)].sum()


def db(x):
    return 10 * np.log10(x + 1e-20)


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    w = wave.open(sys.argv[1])
    a = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2).astype(float) / 32768
    mono = a.mean(axis=1)
    cand = []
    for p in sys.argv[3:]:
        cand += [(p[-20:] + "@%x" % o, f, d) for o, f, d in waves(p)
                 if struct.unpack_from('<HHI', f, 0) == (0x69, 1, 22050)]
    log = open(sys.argv[2], errors='replace').read()
    plays = [float(m.group(1)) for m in
             re.finditer(r"play #\d+ ADPCM 22050 Hz 1ch ([0-9.]+) s vol -?\d+ speech", log)]
    print(len(plays), "speech lines in the log; clipped samples in the dump:",
          int((np.abs(a) >= 32767 / 32768).sum()))
    for dur in plays:
        best = None
        for name, fmt, data in cand:
            if abs(len(data) // 36 * 64 / 22050 - dur) >= 0.006:
                continue
            ref = up(decode(data))
            dec = 16
            r = ref[:len(ref) // dec * dec].reshape(-1, dec).mean(axis=1)
            m = mono[:len(mono) // dec * dec].reshape(-1, dec).mean(axis=1)
            n = 1 << int(np.ceil(np.log2(len(m) + len(r))))
            cc = np.fft.irfft(np.fft.rfft(m, n) * np.conj(np.fft.rfft(r, n)), n)[:len(m)]
            c0 = int(np.argmax(cc)) * dec
            for s in range(max(0, c0 - 40), c0 + 40):
                seg = mono[s:s + len(ref)]
                if len(seg) < len(ref):
                    break
                g = (seg @ ref) / (ref @ ref)
                e = seg - g * ref
                snr = db(g * g * (ref @ ref) / (e @ e + 1e-20))
                if best is None or snr > best[0]:
                    best = (snr, s, g, name, ref)
        if not best:
            print(f"{dur:.2f} s: no source of that length")
            continue
        snr, s, g, name, ref = best
        seg = mono[s:s + len(ref)]
        e = seg - g * ref
        spec = np.abs(np.fft.rfft(e * np.hanning(len(e)))) ** 2
        f = np.fft.rfftfreq(len(e), 1 / 48000)
        fb = 22050 / 64
        near = np.abs(f / fb - np.round(f / fb)) * fb < 3
        comb = spec[near & (f > 100) & (f < 11025)].sum() / band(seg, 0, 24001)
        print(f"{dur:.2f} s = {name}: at {s / 48000:.2f} s, gain {g:.3f}, match {snr:.1f} dB; "
              f"above 11 kHz {db(band(seg, 11025, 24001) / band(seg, 0, 24001)):.1f} dB; "
              f"comb {db(comb):.1f} dB; largest difference {np.abs(e).max():.3f} "
              f"(line peak {np.abs(seg).max():.3f})")


if __name__ == "__main__":
    main()
