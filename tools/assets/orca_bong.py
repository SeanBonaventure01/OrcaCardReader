"""Measure the real ORCA reader's "bong" from a phone video and write a denoised copy.

    python3 tools/assets/orca_bong.py tools/assets/reference/IMG_1719.MOV [--pcm bong.pcm]

Prints the partial table used by fx::kOrcaBong (frequency, level, attack, decay) and, with
--pcm, writes the noise-filtered recording as 16-bit mono 22.05 kHz raw PCM (for comparing by
ear; the firmware synthesizes the bong from the table instead).
Needs numpy + scipy, and macOS `afconvert` to pull the audio out of the video.

How it works:
- The bong is a ~605 Hz tone plus overtones, so the noise floor is measured from the quiet
  tail of the clip (after the bong has died away).
- Partials: peaks in the spectrum of the first 200 ms. For each, the STFT band energy minus
  the noise floor gives an envelope; attack = time from -20 dB to the peak, decay = a straight
  line fit (in dB) from the peak until it reaches the noise floor.
- Denoising: spectral subtraction, multiplied by a comb that only passes +-25 Hz around each
  partial, and each partial is cut once it has been quiet for 30 ms (removes "musical noise").
"""
import argparse
import os
import subprocess
import tempfile

import numpy as np
import scipy.io.wavfile as wavfile
import scipy.signal as sg

NOISE_FROM_S = 0.55  # the bong has died into the noise by here
OUT_RATE = 22050


def load_audio(path):
    with tempfile.TemporaryDirectory() as tmp:
        wav = os.path.join(tmp, "audio.wav")
        subprocess.run(["afconvert", "-f", "WAVE", "-d", "LEI16@48000", "-c", "1", path, wav], check=True)
        sr, x = wavfile.read(wav)
    return sr, x.astype(float) / 32768


def find_partials(x, sr, count=7):
    """The fundamental is the strongest peak above the room rumble; overtones are the
    strongest peak within 1% of each whole multiple of it (room noise has peaks too)."""
    seg = x[int(0.02 * sr) : int(0.22 * sr)] * np.hanning(int(0.2 * sr))
    spec = np.abs(np.fft.rfft(seg, 1 << 18))
    freqs = np.fft.rfftfreq(1 << 18, 1 / sr)
    f0 = freqs[np.argmax(spec * (freqs > 300))]
    partials = []
    for k in range(1, count + 1):
        near = np.abs(freqs - k * f0) < 0.01 * k * f0
        partials.append(freqs[np.argmax(spec * near)])
    return partials


def measure(x, sr, partial_freqs):
    f, t, Z = sg.stft(x, sr, nperseg=2048, noverlap=2048 - 64)
    P = np.abs(Z) ** 2
    noise = P[:, t > NOISE_FROM_S].mean(1)
    rows = []
    for f0 in partial_freqs:
        k = np.argmin(abs(f - f0))
        band, nb = P[k - 2 : k + 3].sum(0), noise[k - 2 : k + 3].sum()
        db = 10 * np.log10(np.clip(band - nb, 1e-12, None))
        ip = int(np.argmax(np.where(t < 0.2, db, -np.inf)))
        j = ip
        while j < len(t) - 1 and db[j] > 10 * np.log10(nb) + 3 and t[j] < 0.6:
            j += 1
        decay = np.polyfit(t[ip:j], db[ip:j], 1)[0] if j - ip > 3 else float("nan")
        i0 = ip
        while i0 > 0 and db[i0] > db[ip] - 20:
            i0 -= 1
        rows.append(dict(freq=f0, peak_db=db[ip], attack=t[ip] - t[i0], decay=decay))
    ref = max(r["peak_db"] for r in rows)
    for r in rows:
        r["level_db"] = r["peak_db"] - ref
    return rows


def denoise(x, sr, partial_freqs, length_s=0.6):
    n, hop = 2048, 256
    f, t, Z = sg.stft(x, sr, nperseg=n, noverlap=n - hop)
    P = np.abs(Z) ** 2
    N = P[:, t > NOISE_FROM_S].mean(1, keepdims=True)
    comb = np.zeros(len(f))
    for f0 in partial_freqs:
        comb[abs(f - f0) < 25] = 1.0
    G = sg.medfilt2d(np.clip(1 - 1.5 * N / (P + 1e-20), 0, 1) * comb[:, None], (1, 9))
    for f0 in partial_freqs:
        rows = np.where(abs(f - f0) < 25)[0]
        g, on, quiet = G[rows].max(0), False, 0
        for j in range(len(t)):
            if g[j] > 0.3:
                on, quiet = True, 0
            elif on:
                quiet += 1
            if on and quiet * hop / sr > 0.03:
                G[rows, j:] = 0
                break
    _, y = sg.istft(Z * G, sr, nperseg=n, noverlap=n - hop)
    onset = int(np.argmax(np.abs(y) > 0.1 * np.abs(y).max())) - int(0.005 * sr)
    y = y[max(onset, 0) : max(onset, 0) + int(length_s * sr)]
    fade = int(0.12 * sr)
    y[-fade:] *= np.cos(np.linspace(0, np.pi / 2, fade)) ** 2
    y[:48] *= np.linspace(0, 1, 48)
    y = sg.resample_poly(y, OUT_RATE, sr) if sr != OUT_RATE else y
    return np.clip(y / np.abs(y).max() * 22000, -32768, 32767).astype("<i2")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("--pcm", help="write the denoised bong here (raw 16-bit mono 22.05 kHz)")
    args = ap.parse_args()
    sr, x = load_audio(args.video)
    freqs = find_partials(x, sr)
    print("const std::vector<Partial> kOrcaBong = {  // freq, level dB, attack s, decay dB/s")
    for r in measure(x, sr, freqs):
        print(f"    {{{r['freq']:.1f}, {r['level_db']:.1f}, {r['attack']:.3f}, {r['decay']:.1f}}},")
    print("};")
    if args.pcm:
        pcm = denoise(x, sr, freqs)
        pcm.tofile(args.pcm)
        print(f"wrote {args.pcm} ({len(pcm)} samples, {len(pcm) / OUT_RATE:.2f} s)")


if __name__ == "__main__":
    main()
