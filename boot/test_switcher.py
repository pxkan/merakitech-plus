#!/usr/bin/env python3
"""test_switcher.py — lokal verifikation af switcher uden enhed (v6.12).

To lag:
 1. C-self-test (switcher_selftest): deterministisk unit-test af center-hold-
    gestus'en (≥5000 ms i radius 40 om (240,240)), corner-fallback, raw→screen-
    skalering i BEGGE ranges (device 0..480 + sim 0..4095), pick_ranges (EVIOCGABS
    auto-detektion), sysfs-samples (grab-bypass b) og mode-klassifikation.
 2. End-to-end fil-mode: switcher_host (SAMME daemon-kode som ARM-binæren, blot
    bygget til host) læser en syntetisk evdev-stream fra fil via
    SWITCHER_INPUT_FILE. Gestus-timing følger event-tidsstempler, så en 5,2 s
    gestus afvikles deterministisk på millisekunder. På Mac findes hverken
    /proc eller pidof → mode-check skal logge "ingen demo_v9 → ingen handling"
    (beviser at mode-check-logikken eksekveres og er sikker uden demo).

Fil-mode's raw-default er nu enhedens konstant 0..480 (SWITCHER_RAW_MAX overstyrer;
sim-passede streams bruger 4095). Gestus-default = center; corner testes via
SWITCHER_GESTURE=corner (fallback).

Beviser gemmes i test-logs/. Brug: python3 test_switcher.py
"""
import os
import pathlib
import struct
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent.parent / "build"   # ./build.sh output
LOGS = HERE / "test-logs-switcher"

# evdev-konstanter (samme værdier som switcher.c / linux/input.h)
EV_SYN, EV_KEY, EV_ABS = 0x00, 0x01, 0x03
SYN_REPORT, BTN_TOUCH = 0x00, 0x14A
ABS_X, ABS_Y = 0x00, 0x01
ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_MT_TRACKING_ID = 0x35, 0x36, 0x39
EV_FMT = struct.Struct("<iiHhi")  # 16 byte: kernel 4.9/arm timeval (32-bit)


def ev(t, sec, usec, typ, code, val):
    t.append(EV_FMT.pack(sec, usec, typ, code, val))


def raw(px, rmax=480):
    """tinj.c-formlen: raw = px*(max-min+1)/480 (device-default 0..480)."""
    return px * (rmax + 1) // 480


def hold(t, ms_clock, x, y, hold_ms, rmax=480):
    """Ét hold; returnerer ny clock (ms)."""
    sec, usec = divmod(ms_clock, 1000)
    usec *= 1000
    ev(t, sec, usec, EV_ABS, ABS_MT_TRACKING_ID, 1)
    ev(t, sec, usec, EV_ABS, ABS_MT_POSITION_X, raw(x, rmax))
    ev(t, sec, usec, EV_ABS, ABS_MT_POSITION_Y, raw(y, rmax))
    ev(t, sec, usec, EV_ABS, ABS_X, raw(x, rmax))
    ev(t, sec, usec, EV_ABS, ABS_Y, raw(y, rmax))
    ev(t, sec, usec, EV_KEY, BTN_TOUCH, 1)
    ev(t, sec, usec, EV_SYN, SYN_REPORT, 0)
    ms_clock += hold_ms
    sec, usec = divmod(ms_clock, 1000)
    usec *= 1000
    ev(t, sec, usec, EV_ABS, ABS_MT_TRACKING_ID, -1)
    ev(t, sec, usec, EV_KEY, BTN_TOUCH, 0)
    ev(t, sec, usec, EV_SYN, SYN_REPORT, 0)
    return ms_clock


def make_stream(presses, x=240, y=240, hold_ms=5200, gap_ms=300, t0_ms=100000,
                rmax=480):
    """presses = antal holds; genererer rå input_event-bytes."""
    t, clock = [], t0_ms
    for i in range(presses):
        clock = hold(t, clock, x, y, hold_ms, rmax)
        if i < presses - 1:
            clock += gap_ms
    return b"".join(t)


def make_drift_stream():
    """center-hold der driver ud af zonen efter 2 s og tilbage efter 4 s."""
    t = []
    sec, usec = 100, 0
    ev(t, sec, usec, EV_ABS, ABS_MT_TRACKING_ID, 1)
    for code, v in ((ABS_MT_POSITION_X, raw(240)), (ABS_MT_POSITION_Y, raw(240)),
                    (ABS_X, raw(240)), (ABS_Y, raw(240))):
        ev(t, sec, usec, EV_ABS, code, v)
    ev(t, sec, usec, EV_KEY, BTN_TOUCH, 1)
    ev(t, sec, usec, EV_SYN, SYN_REPORT, 0)
    sec, usec = 102, 0                               # +2 s: drift ud
    for code, v in ((ABS_MT_POSITION_X, raw(350)), (ABS_MT_POSITION_Y, raw(350)),
                    (ABS_X, raw(350)), (ABS_Y, raw(350))):
        ev(t, sec, usec, EV_ABS, code, v)
    ev(t, sec, usec, EV_SYN, SYN_REPORT, 0)
    sec, usec = 104, 0                               # +4 s: tilbage
    for code, v in ((ABS_MT_POSITION_X, raw(240)), (ABS_MT_POSITION_Y, raw(240)),
                    (ABS_X, raw(240)), (ABS_Y, raw(240))):
        ev(t, sec, usec, EV_ABS, code, v)
    ev(t, sec, usec, EV_SYN, SYN_REPORT, 0)
    sec, usec = 105, 200000                          # +5,2 s: up
    ev(t, sec, usec, EV_ABS, ABS_MT_TRACKING_ID, -1)
    ev(t, sec, usec, EV_KEY, BTN_TOUCH, 0)
    ev(t, sec, usec, EV_SYN, SYN_REPORT, 0)
    return b"".join(t)


def run_host_daemon(stream_bytes, name, extra_env=None):
    """Kør switcher_host i fil-mode; returnerer log-teksten."""
    LOGS.mkdir(exist_ok=True)
    stream = LOGS / f"{name}.evdev"
    logfile = LOGS / f"{name}.switcher.log"
    lockfile = LOGS / f"{name}.lock"
    stream.write_bytes(stream_bytes)
    for f in (logfile, lockfile):
        f.unlink(missing_ok=True)
    env = dict(os.environ,
               SWITCHER_INPUT_FILE=str(stream),
               SWITCHER_LOG=str(logfile),
               SWITCHER_LOCK=str(lockfile))
    for k in ("SWITCHER_GESTURE", "SWITCHER_RAW_MAX", "SWITCHER_HOLD_MS",
              "SWITCHER_CENTER_X", "SWITCHER_CENTER_Y", "SWITCHER_CENTER_R"):
        env.pop(k, None)          # deterministisk: ingen arv fra omgivelserne
    if extra_env:
        env.update(extra_env)
    r = subprocess.run([str(HERE / "switcher_host")], env=env,
                       capture_output=True, text=True, timeout=30)
    if r.returncode != 0:
        raise RuntimeError(f"switcher_host rc={r.returncode}: {r.stderr}")
    return logfile.read_text()


def main():
    LOGS.mkdir(exist_ok=True)
    fails = 0

    def gate(ok, label, log=""):
        nonlocal fails
        print("  →", "OK" if ok else "FEJL", label)
        if not ok:
            fails += 1
            print(log)

    # 1. C-self-test
    print("== C-self-test (switcher_selftest) ==")
    r = subprocess.run([str(HERE / "switcher_selftest")], capture_output=True, text=True)
    (LOGS / "selftest.log").write_text(r.stdout + r.stderr)
    print(r.stdout.strip())
    if r.returncode != 0:
        fails += 1

    # 2. center positiv: ét hold 5,2 s i (240,240), raw-default 480 (device)
    print("\n== fil-mode: CENTER-hold 5,2 s i (240,240) (positiv) ==")
    log = run_host_daemon(make_stream(1), "v612_center_positiv")
    print(log.strip())
    gate("GESTUS UDLØST" in log and "ingen demo_v9" in log,
         "center-hold → trigger + sikker mode-check uden demo", log)

    # 3. center negativ: for kort (3,0 s)
    print("\n== fil-mode: CENTER-hold 3,0 s (negativ) ==")
    log = run_host_daemon(make_stream(1, hold_ms=3000), "v612_center_kort")
    gate("GESTUS UDLØST" not in log, "3,0 s < 5 s → ingen trigger", log)

    # 4. center negativ: off-center (300,240)
    print("\n== fil-mode: CENTER-hold i (300,240) (negativ) ==")
    log = run_host_daemon(make_stream(1, x=300, y=240), "v612_center_off")
    gate("GESTUS UDLØST" not in log, "uden for radius 40 → ingen trigger", log)

    # 5. center negativ: drift ud af zonen under hold
    print("\n== fil-mode: CENTER-hold m. drift ud af zonen (negativ) ==")
    log = run_host_daemon(make_drift_stream(), "v612_center_drift")
    gate("GESTUS UDLØST" not in log, "drift ud af zonen → ingen trigger", log)

    # 6. center med sim-range 4095 (SWITCHER_RAW_MAX override)
    print("\n== fil-mode: CENTER-hold, raw 0..4095 + SWITCHER_RAW_MAX=4095 ==")
    log = run_host_daemon(make_stream(1, rmax=4095), "v612_center_4095",
                          {"SWITCHER_RAW_MAX": "4095"})
    gate("GESTUS UDLØST" in log, "sim-range stream + override → trigger", log)

    # 7. corner-fallback positiv: 3×1,6 s i (75,75), raw 4095
    print("\n== fil-mode: CORNER-fallback 3×1,6 s (positiv) ==")
    log = run_host_daemon(make_stream(3, x=75, y=75, hold_ms=1600, rmax=4095),
                          "v612_corner_positiv",
                          {"SWITCHER_GESTURE": "corner", "SWITCHER_RAW_MAX": "4095"})
    print(log.strip())
    gate("GESTUS UDLØST" in log and "gyldig long-press i hjørne talt (2/3)" in log,
         "corner-fallback → trigger + tælling", log)

    # 8. corner negativ: kun 2 holds
    print("\n== fil-mode: CORNER 2 holds (negativ) ==")
    log = run_host_daemon(make_stream(2, x=75, y=75, hold_ms=1600, rmax=4095),
                          "v612_corner_2holds",
                          {"SWITCHER_GESTURE": "corner", "SWITCHER_RAW_MAX": "4095"})
    gate("GESTUS UDLØST" not in log and "(2/3)" in log, "2 holds → ingen trigger", log)

    # 9. center-default ignorér corner-sekvens (ingen trigger)
    print("\n== fil-mode: corner-sekvens mod CENTER-default (negativ) ==")
    log = run_host_daemon(make_stream(3, x=75, y=75, hold_ms=1600, rmax=4095),
                          "v612_center_vs_corner", {"SWITCHER_RAW_MAX": "4095"})
    gate("GESTUS UDLØST" not in log, "3×1,6 s hjørne → ingen trigger i center-mode", log)

    # 10. env: custom center + radius + hold
    print("\n== fil-mode: SWITCHER_CENTER_X/Y/R + HOLD_MS override ==")
    log = run_host_daemon(make_stream(1, x=100, y=120, hold_ms=2200),
                          "v612_center_env",
                          {"SWITCHER_CENTER_X": "100", "SWITCHER_CENTER_Y": "120",
                           "SWITCHER_CENTER_R": "30", "SWITCHER_HOLD_MS": "2000"})
    gate("GESTUS UDLØST" in log, "custom center (100,120) r30 hold 2000 → trigger", log)

    print(f"\n{'ALT GRØNT' if fails == 0 else f'{fails} FEJL'}")
    print(f"Beviser: {LOGS}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
