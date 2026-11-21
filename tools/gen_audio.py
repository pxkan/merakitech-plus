#!/usr/bin/env python3
"""gen_audio.py — synthesizes Meraki+'s own sounds (audio/*.wav). Python stdlib only, no samples, no vendor audio.

    python3 tools/gen_audio.py            write audio/start.wav, audio/ready_extract.wav, audio/ready_steam.wav
    python3 tools/gen_audio.py --check    regenerate in memory and compare with the committed files (CI / hygiene)

All three are 16 kHz, mono, 16-bit PCM WAV (small: ~64 KB together). The helper merakiplus_snd upsamples them to the
48 kHz the display's audio output runs at (docs/audio.md). Each sound has its own pitch, rhythm and timbre so you can
tell by ear which boiler is ready:

  start.wav          START JINGLE (power-on): a rising four-note bell arpeggio C5-E5-G5-C6, ~1.1 s
  ready_extract.wav  BREW (extract) BOILER READY: two warm, low notes going up a fifth (A4 -> E5), ~0.55 s
  ready_steam.wav    STEAM BOILER READY: three quick bright pips rising (E6 F#6 B6), ~0.5 s

--check tolerates a difference of 1 LSB per sample (math.sin may round differently in the last bit on other platforms).
The files are original work of this project (MIT, like the rest of the repository).
"""
import io
import math
import pathlib
import struct
import sys
import wave

RATE = 16000
ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "audio"


def note(freq, dur, amp=0.55, attack=0.008, decay=6.0, harmonics=((1, 1.0), (2, 0.35), (3, 0.12)), start=0.0, total=None):
    """One bell-like note: a few harmonics, quick attack, exponential decay, 15 ms release at the end."""
    n = int(round(dur * RATE))
    out = []
    for i in range(n):
        t = i / RATE
        env = min(1.0, t / attack) * math.exp(-decay * t)
        rel = min(1.0, (dur - t) / 0.015)
        s = sum(a * math.sin(2 * math.pi * freq * h * t) for h, a in harmonics)
        out.append(amp * env * rel * s / sum(a for _, a in harmonics))
    return out


def mix(parts, length):
    buf = [0.0] * int(round(length * RATE))
    for at, samples in parts:
        o = int(round(at * RATE))
        for i, v in enumerate(samples):
            if o + i < len(buf):
                buf[o + i] += v
    return buf


def start_jingle():
    seq = [(0.00, 523.25), (0.14, 659.25), (0.28, 783.99), (0.42, 1046.50)]
    parts = [(at, note(f, 0.62 if k == 3 else 0.40, amp=0.42, decay=4.5 if k == 3 else 7.0)) for k, (at, f) in enumerate(seq)]
    return mix(parts, 1.10)


def ready_extract():
    warm = ((1, 1.0), (2, 0.18), (3, 0.05))
    parts = [(0.00, note(440.00, 0.24, amp=0.55, decay=5.0, harmonics=warm)),
             (0.22, note(659.25, 0.33, amp=0.55, decay=5.5, harmonics=warm))]
    return mix(parts, 0.55)


def ready_steam():
    bright = ((1, 1.0), (2, 0.5), (4, 0.2))
    parts = [(0.00, note(1318.51, 0.11, amp=0.40, decay=14.0, harmonics=bright)),
             (0.13, note(1479.98, 0.11, amp=0.40, decay=14.0, harmonics=bright)),
             (0.26, note(1975.53, 0.24, amp=0.40, decay=9.0, harmonics=bright))]
    return mix(parts, 0.50)


SOUNDS = {"start.wav": start_jingle, "ready_extract.wav": ready_extract, "ready_steam.wav": ready_steam}


def to_wav(samples):
    pcm = b"".join(struct.pack("<h", max(-32767, min(32767, int(round(v * 32767))))) for v in samples)
    b = io.BytesIO()
    with wave.open(b, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm)
    return b.getvalue()


def pcm_of(data):
    with wave.open(io.BytesIO(data), "rb") as w:
        assert (w.getnchannels(), w.getsampwidth(), w.getframerate()) == (1, 2, RATE)
        raw = w.readframes(w.getnframes())
    return struct.unpack("<%dh" % (len(raw) // 2), raw)


def main():
    check = "--check" in sys.argv[1:]
    bad = 0
    for name, fn in SOUNDS.items():
        data = to_wav(fn())
        p = OUT / name
        if check:
            if not p.is_file():
                print(f"  [FAIL] {p.relative_to(ROOT)} missing")
                bad += 1
                continue
            a, b = pcm_of(data), pcm_of(p.read_bytes())
            same = len(a) == len(b) and all(abs(x - y) <= 1 for x, y in zip(a, b))
            print(f"  [{'OK' if same else 'FAIL'}] {p.relative_to(ROOT)} == gen_audio.py output ({len(b)} samples)")
            bad += not same
        else:
            OUT.mkdir(exist_ok=True)
            p.write_bytes(data)
            print(f"wrote {p.relative_to(ROOT)} ({len(data)} bytes, {len(data) / 2 / RATE:.2f} s)")
    if check:
        print("AUDIO: " + ("OK" if not bad else f"{bad} problem(s)"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
