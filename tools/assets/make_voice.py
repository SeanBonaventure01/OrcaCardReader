"""Builds a voice pack (firmware/components/audio/assets/voice_<pack>.bin) from a line list.

    python3 tools/assets/make_voice.py profanity --voice "Ava (Premium)" --voice Bruce [--play]

Reads tools/assets/voice/<pack>.txt ("accept: ..." / "reject: ..." lines) and speaks every
line in every --voice; the costume picks a voice at random per tap. A clip comes from
tools/assets/voice/<pack>/<voice-slug>/<line-slug>.wav if that exists (drop in your own or
AI-generated recordings, e.g. .../bruce/eat-shit.wav), otherwise from macOS text-to-speech
(`say`; Siri voices aren't available to it). Clips are trimmed, normalised, resampled to
16 kHz mono and IMA ADPCM encoded; the blob format is documented in
firmware/components/fx_core/include/orca/fx/voice.hpp. Plain Python, no packages needed.

--play plays each clip through the Mac's speakers as encoded, to check before flashing.
"""
import argparse
import array
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
import wave

ROOT = pathlib.Path(__file__).resolve().parents[2]
VOICE_DIR = ROOT / "tools/assets/voice"
OUT_DIR = ROOT / "firmware/components/audio/assets"
RATE = 16000          # plenty for speech on a small speaker; the costume upsamples to 22.05 kHz
PEAK = 24000          # leaves headroom for the bong ringing underneath
KINDS = {"accept": 0, "reject": 1}

INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]
STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767]


def slug(text):
    return re.sub(r"[^a-z]+", "-", text.lower()).strip("-")


def read_lines(pack):
    lines = []
    for n, raw in enumerate((VOICE_DIR / f"{pack}.txt").read_text().splitlines(), 1):
        raw = raw.strip()
        if not raw or raw.startswith("#"):
            continue
        kind, sep, text = raw.partition(":")
        if not sep or kind.strip() not in KINDS or not text.strip():
            sys.exit(f"{pack}.txt:{n}: expected 'accept: text' or 'reject: text'")
        lines.append((kind.strip(), text.strip()))
    if not 1 <= len(lines) <= 255:
        sys.exit(f"{pack}.txt: need 1-255 lines, got {len(lines)}")
    return lines


def to_pcm(src, tmp):
    """Any audio file -> 22.05 kHz mono 16-bit samples, via macOS afconvert."""
    out = tmp / "clip.wav"
    subprocess.run(["afconvert", "-f", "WAVE", "-d", f"LEI16@{RATE}", "-c", "1", str(src), str(out)], check=True)
    with wave.open(str(out)) as w:
        pcm = array.array("h", w.readframes(w.getnframes()))
    return list(pcm)


def tidy(pcm):
    """Trims leading/trailing silence (keeping 30 ms), normalises, and fades the ends."""
    loud = [i for i, s in enumerate(pcm) if abs(s) > 600]
    if not loud:
        return pcm
    pad = RATE * 30 // 1000
    pcm = pcm[max(0, loud[0] - pad): loud[-1] + pad]
    peak = max(abs(s) for s in pcm) or 1
    pcm = [int(s * PEAK / peak) for s in pcm]
    fade = min(len(pcm) // 4, RATE // 100)
    for i in range(fade):
        pcm[i] = pcm[i] * i // fade
        pcm[-1 - i] = pcm[-1 - i] * i // fade
    return pcm


def adpcm_encode(pcm):
    """Same encoder as fx::adpcm_encode (host tests check the C++ one round-trips)."""
    out = bytearray((len(pcm) + 1) // 2)
    pred, index = 0, 0
    for i, sample in enumerate(pcm):
        step = STEP_TABLE[index]
        diff = sample - pred
        code = 0
        if diff < 0:
            code, diff = 8, -diff
        if diff >= step:
            code |= 4
            diff -= step
        if diff >= step >> 1:
            code |= 2
            diff -= step >> 1
        if diff >= step >> 2:
            code |= 1
        # Track exactly what the decoder reconstructs.
        d = step >> 3
        if code & 4:
            d += step
        if code & 2:
            d += step >> 1
        if code & 1:
            d += step >> 2
        pred = max(-32768, min(32767, pred - d if code & 8 else pred + d))
        index = max(0, min(88, index + INDEX_TABLE[code]))
        out[i // 2] |= code << 4 if i % 2 else code
    return bytes(out)


def adpcm_decode(data, samples):
    out, pred, index = [], 0, 0
    for i in range(samples):
        code = data[i // 2] >> 4 if i % 2 else data[i // 2] & 0x0F
        step = STEP_TABLE[index]
        d = step >> 3
        if code & 4:
            d += step
        if code & 2:
            d += step >> 1
        if code & 1:
            d += step >> 2
        pred = max(-32768, min(32767, pred - d if code & 8 else pred + d))
        index = max(0, min(88, index + INDEX_TABLE[code]))
        out.append(pred)
    return out


def build_blob(voices, entries):
    names = b"".join(bytes([len(v.encode())]) + v.encode() for v in voices)
    head = bytearray(b"ORCV" + struct.pack("<BBHB", 2, len(entries), RATE, len(voices)) + names)
    header = len(head) + sum(15 + len(text.encode()) for _, _, text, _, _ in entries)
    data = bytearray()
    for kind, voice, text, clip, samples in entries:
        t = text.encode()
        if len(t) > 255:
            sys.exit(f"line too long: {text}")
        head += struct.pack("<BBBIII", KINDS[kind], voice, len(t), header + len(data), len(clip), samples) + t
        data += clip
    return bytes(head + data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pack")
    ap.add_argument("--voice", action="append", help="macOS voice (see `say -v '?'`); repeat for several")
    ap.add_argument("--rate", type=int, default=175, help="words per minute")
    ap.add_argument("--play", action="store_true")
    args = ap.parse_args()
    voices = args.voice or ["Samantha"]
    if len(voices) > 255:
        sys.exit("too many voices")

    lines = read_lines(args.pack)
    if len(lines) * len(voices) > 255:
        sys.exit(f"{len(lines)} lines x {len(voices)} voices is more than 255 clips")
    entries = []
    with tempfile.TemporaryDirectory() as t:
        tmp = pathlib.Path(t)
        for v, voice in enumerate(voices):
            print(voice)
            for kind, text in lines:
                own = VOICE_DIR / args.pack / slug(voice) / f"{slug(text)}.wav"
                if own.exists():
                    src, how = own, "recording"
                else:
                    src, how = tmp / "say.aiff", "say"
                    subprocess.run(["say", "-v", voice, "-r", str(args.rate), "-o", str(src), text], check=True)
                pcm = tidy(to_pcm(src, tmp))
                clip = adpcm_encode(pcm)
                entries.append((kind, v, text, clip, len(pcm)))
                print(f"  {kind:6} {len(pcm) / RATE:4.1f} s {len(clip) / 1024:5.1f} KB  ({how})  {text}")
                if args.play:
                    out = tmp / "check.wav"
                    with wave.open(str(out), "wb") as w:
                        w.setnchannels(1), w.setsampwidth(2), w.setframerate(RATE)
                        w.writeframes(array.array("h", adpcm_decode(clip, len(pcm))).tobytes())
                    subprocess.run(["afplay", str(out)], check=True)
    blob = build_blob(voices, entries)
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    out = OUT_DIR / f"voice_{args.pack}.bin"
    out.write_bytes(blob)
    print(f"wrote {out.relative_to(ROOT)}: {len(entries)} clips in {len(voices)} voices, {len(blob) / 1024:.0f} KB")


if __name__ == "__main__":
    main()
