#!/usr/bin/env python3
"""gen_audio.py — synthesizes Meraki+'s own sounds (audio/*.wav). Python stdlib only, no samples, no vendor audio.

    python3 tools/gen_audio.py            write audio/start.wav, audio/ready_extract.wav, audio/ready_steam.wav
    python3 tools/gen_audio.py --check    regenerate in memory and compare with the committed files (CI / hygiene)

All three are 16 kHz, mono, 16-bit PCM WAV (~100 KB together). The helper merakiplus_snd upsamples them to the 48 kHz
the display's audio output runs at (docs/audio.md). Since v6.19 they are struck-instrument timbres instead of plain
sine beeps, and each sound has its own instrument, register and rhythm so you can tell by ear which boiler is ready:

  start.wav          START JINGLE (power-on): a warm rising arpeggio G4-C5-E5-G5 on a marimba + soft electric-bell
                     layer, the notes ringing into each other, capped by a C6 bell; ~1.4 s
  ready_extract.wav  BREW (extract) BOILER READY: a low wooden marimba double note going up a fifth (G4 -> D5), ~0.8 s
  ready_steam.wav    STEAM BOILER READY: a bright glockenspiel triple pip, E major rising (E6 G#6 B6), ~0.75 s

How (modal synthesis, like a struck bar): every note is a sum of decaying partials at the instrument's own frequency
ratios — marimba 1 : 3.9 : 9.2 (a tuned wooden bar; the overtones die much faster than the fundamental), glockenspiel
1 : 2.76 : 5.40 (a metal bar, inharmonic and long-ringing) — with a raised-cosine mallet attack of 1.5..4 ms and a raised-cosine damping at the end (no click). The
jingle adds a two-operator FM "electric bell" (1 : 1, decaying index; 1 : 3 on the top C6, harmonic) for warmth. Partials above 6 kHz are dropped (the
files are 16 kHz and get upsampled linearly on the display). A small Schroeder room (4 damped combs + 2 all-passes, ~18 %
wet) gives the notes a short, soft tail. Every sound is DC-blocked, peak-normalized to -4 dBFS and faded out at the end.

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
TOP_HZ = 6000.0          # drop partials above this (Nyquist is 8 kHz; the display upsamples x3 linearly)
PEAK = 0.63              # -4 dBFS

# (frequency ratio, relative amplitude, decay in 1/s) per instrument; real bars: the higher a mode, the faster it dies
MARIMBA = ((1.0, 1.0, 5.5), (3.9, 0.32, 22.0), (9.2, 0.07, 48.0))
MARIMBA_LOW = ((1.0, 1.0, 4.2), (2.0, 0.10, 9.0), (3.9, 0.26, 20.0), (9.2, 0.05, 45.0))
GLOCK = ((1.0, 1.0, 4.5), (2.76, 0.42, 10.0), (5.40, 0.16, 22.0), (8.93, 0.05, 40.0))


def shape(out, attack, release):
    """Raised-cosine fade-in (the mallet) and fade-out (the note is damped instead of cut: no click)."""
    n = len(out)
    na, nr = max(1, int(attack * RATE)), max(1, int(release * RATE))
    for i in range(min(na, n)):
        out[i] *= 0.5 - 0.5 * math.cos(math.pi * i / na)
    for i in range(min(nr, n)):
        out[n - 1 - i] *= 0.5 - 0.5 * math.cos(math.pi * i / nr)
    return out


def strike(freq, dur, modes, amp=1.0, attack=0.003, release=0.06):
    """One struck note: decaying partials (modal synthesis), mallet attack, damped release."""
    n = int(round(dur * RATE))
    parts = [(2 * math.pi * freq * r / RATE, a, math.exp(-d / RATE)) for r, a, d in modes if freq * r < TOP_HZ]
    norm = amp / sum(a for _, a, _ in parts)
    out = [0.0] * n
    for w, a, k in parts:
        g = a * norm
        for i in range(n):
            out[i] += g * math.sin(w * i)
            g *= k
    return shape(out, attack, release)


def fm_bell(freq, dur, amp=1.0, ratio=1.0, index=1.6, index_decay=7.0, decay=3.2, attack=0.004, release=0.06):
    """Two-operator FM: a sine carrier phase-modulated by a sine at ratio x freq; the index decays (bright -> mellow)."""
    n = int(round(dur * RATE))
    wc, wm = 2 * math.pi * freq / RATE, 2 * math.pi * freq * ratio / RATE
    out = []
    for i in range(n):
        t = i / RATE
        out.append(amp * math.exp(-decay * t) * math.sin(wc * i + index * math.exp(-index_decay * t) * math.sin(wm * i)))
    return shape(out, attack, release)


def layer(*sigs):
    n = max(len(s) for s in sigs)
    out = [0.0] * n
    for s in sigs:
        for i, v in enumerate(s):
            out[i] += v
    return out


def mix(parts, length):
    buf = [0.0] * int(round(length * RATE))
    for at, samples in parts:
        o = int(round(at * RATE))
        for i, v in enumerate(samples):
            if o + i < len(buf):
                buf[o + i] += v
    return buf


def room(x, wet=0.18, fb=0.72, damp=0.35):
    """Small Schroeder reverb: 4 parallel low-pass-damped feedback combs, then 2 series all-passes."""
    n = len(x)
    acc = [0.0] * n
    for ms in (29.7, 37.1, 41.1, 43.7):
        d = int(ms * RATE / 1000)
        line = [0.0] * d
        lp = 0.0
        p = 0
        for i in range(n):
            y = line[p]
            lp = y * (1 - damp) + lp * damp
            line[p] = x[i] + lp * fb
            p = (p + 1) % d
            acc[i] += y * 0.25
    for ms, g in ((5.0, 0.7), (1.7, 0.7)):
        d = int(ms * RATE / 1000)
        line = [0.0] * d
        p = 0
        for i in range(n):
            b = line[p]
            v = acc[i] + g * b
            line[p] = v
            acc[i] = b - g * v
            p = (p + 1) % d
    return [(1 - wet) * a + wet * b for a, b in zip(x, acc)]


def finish(x, fade=0.08):
    """DC block, peak-normalize to PEAK, fade the last `fade` seconds out."""
    y, prev_x, prev_y = [], 0.0, 0.0
    for v in x:
        prev_y = v - prev_x + 0.995 * prev_y
        prev_x = v
        y.append(prev_y)
    m = max(abs(v) for v in y) or 1.0
    y = [v * PEAK / m for v in y]
    nf = int(fade * RATE)
    for i in range(nf):
        y[-1 - i] *= i / nf
    return y


def start_jingle():
    # G4 C5 E5 G5 (warm marimba + soft FM electric bell), then a C6 bell on top; every note rings on (a pedalled chord)
    seq = [(0.00, 392.00), (0.11, 523.25), (0.22, 659.25), (0.33, 783.99)]
    end = 1.36
    parts = [(at, layer(strike(f, end - at, MARIMBA, amp=0.50, attack=0.004, release=0.30),
                        fm_bell(f, end - at, amp=0.30, ratio=1.0, index=1.4, index_decay=9.0, decay=3.0, release=0.30)))
             for at, f in seq]
    parts.append((0.46, layer(strike(1046.50, end - 0.46, GLOCK, amp=0.42, attack=0.002, release=0.30),
                              fm_bell(1046.50, end - 0.46, amp=0.20, ratio=3.0, index=1.0, index_decay=6.0, decay=3.2, release=0.30),
                              strike(523.25, end - 0.46, MARIMBA, amp=0.22, attack=0.004, release=0.30))))
    return finish(room(mix(parts, 1.42)))


def ready_extract():
    # a low wooden "do-ding": G4 then D5 (up a fifth), the second a little louder and longer
    parts = [(0.00, strike(392.00, 0.34, MARIMBA_LOW, amp=0.80, attack=0.004, release=0.14)),
             (0.17, layer(strike(587.33, 0.57, MARIMBA_LOW, amp=1.00, attack=0.004, release=0.22),
                          fm_bell(587.33, 0.57, amp=0.18, ratio=1.0, index=0.8, index_decay=12.0, decay=4.5, release=0.22)))]
    return finish(room(mix(parts, 0.80), wet=0.16))


def ready_steam():
    # three quick glockenspiel pips, E major rising, the last one ringing out
    parts = [(0.00, strike(1318.51, 0.26, GLOCK, amp=0.70, attack=0.0015, release=0.16)),
             (0.09, strike(1661.22, 0.26, GLOCK, amp=0.75, attack=0.0015, release=0.16)),
             (0.18, strike(1975.53, 0.52, GLOCK, amp=0.90, attack=0.0015, release=0.25))]
    return finish(room(mix(parts, 0.75), wet=0.20))


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
