#!/usr/bin/env python3
"""gen_scenarios.py — generates sim/scenarios/*.json (reproducible source).

Scenario timelines are calibrated against mcu_sim's phase delays
(grind/brew +0.39 s, water +0.18 s, steam +0.30 s) and the observed curves
(BREW.md/HOTWATER.md/SESSION.md). The settings-save matrix is derived from
parity_spec.json (min/max/step per parameter).

v6.5-scenarios (RECONCILE.md): the expectations assert merakiplus's GUARDED
behaviour (UI_v6.md §4.1/4.3/4.5/4.6 + frame policy): PRE-INF. TIME = sub113
len1, GRIND MODE = sub2 len1, SAVE of an unchanged value = 'unchanged',
PAUSE TIME locked, BRIGHTNESS 0 rejected, init/patch not exposed, error/blocked
start = toast instead of an MCU safety stop, out-of-spec boot push = BLOCK.
Touch targets are looked up via the layout hook. The reference client's oracle
behaviour (parity word-for-word, unguarded) lives in sim/sim_ui_profile-ref.json.

Run: python3 sim/gen_scenarios.py   (overwrites scenarios/*.json)
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import proto
from proto import load_spec, load_baseline_config, pack112, pack113

HERE = Path(__file__).resolve().parent
OUT = HERE / "scenarios"


def ev(t, target, **kw):
    return {"t": t, "target": target, **kw}


def ui(t, cmd, **kw):
    return ev(t, "ui", cmd=cmd, **kw)


def sim_ev(t, event):
    return ev(t, "sim", event=event)


def snap(t, label):
    return ev(t, "runner", op="snapshot", label=label)


def base(name, desc, duration, events, expect, **kw):
    s = {"name": name, "description": desc, "duration_s": duration,
         "events": events, "expect": expect}
    s.update(kw)
    return s


SCENARIOS = []

# ---------------------------------------------------------------- idle
SCENARIOS.append(base(
    "idle", "Idle/ready: poll rotation, keep-awake, boot push, no activity", 12,
    [snap(6, "idle")],
    [{"kind": "phase_hold", "t0": 2, "t1": 11.5, "value": "READY", "min_ratio": 0.95},
     {"kind": "work_sta_seen", "t0": 1, "t1": 12, "value": "0x0101"},
     {"kind": "word", "word": 34, "t": 6, "window": 3, "value": 0},
     {"kind": "word", "word": 35, "t": 6, "window": 3, "value": 0},
     {"kind": "word", "word": 36, "t": 6, "window": 3, "value": 0},
     {"kind": "sim_log", "assert": "no_writes_between", "t0": 1, "t1": 11.5,
      "except_subs": [0, 1, 112]},
     # v7.0 (OEM look): READY is shown by the boiler rings. A ring whose boiler is still
     # heating is a faint track (alpha 95, ~[211,152,125] here) + a partial white arc; at
     # READY it is drawn solid white. Sampled on the solid 2-px stroke at the top of the
     # BREW ring (160,326 r72 -> y 253) and the STEAM ring (320,326), far from the label gaps.
     # (v6: point (240,8) = the phase ring in the READY green.)
     {"kind": "point", "t": 6, "point": [160, 253], "color": [255, 255, 255], "tol": 30},
     {"kind": "point", "t": 6, "point": [320, 253], "color": [255, 255, 255], "tol": 30},
     # v7.0: HOME's chevron (SYSTEM) is drawn white at the top centre (tip at 239,23).
     # (v6: point (240,20) = the gauge arc.) Ref oracle: sim_ui_profile-ref.json.
     {"kind": "point", "t": 6, "point": [239, 23], "color": [248, 244, 242], "tol": 30},
     # v7.0: the copper background between the four rings (bgpure; the dark bgdark error
     # layer would be ~[30,20,17]); the region is clear of the ring strokes and averages
     # the guilloche texture. (v6: dark vignette [32,36,42] in 200,200,80,80.)
     {"kind": "region", "t": 6, "rect": [214, 214, 52, 52],
      "color": [188, 94, 42], "tol": 20}]))

# ---------------------------------------------------------------- sleep→wake
SCENARIOS.append(base(
    "sleep_wake", "Sleep/drain (word1=1) → wake (word1=0) → heat-up → ready", 28,
    [ev(1.5, "sim", cmd="expect_keepawake", value=False),
     ui(2, "raw_write", sub=1, words=[1]),
     sim_ev(2.5, {"type": "set_reg", "word": 40, "value": 800}),
     snap(5, "sleep"),
     ui(12, "raw_write", sub=1, words=[0]),
     ev(12, "sim", cmd="expect_keepawake", value=True),
     snap(15, "heating"), snap(25, "ready")],
    [{"kind": "phase_hold", "t0": 3, "t1": 11, "value": "SLEEP", "min_ratio": 0.9},
     {"kind": "word", "word": 1, "t": 5, "window": 2, "value": 1},
     {"kind": "work_sta_seen", "t0": 3, "t1": 11, "value": "0x0001"},
     {"kind": "word", "word": 1, "t": 13.5, "window": 1, "value": 0},
     {"kind": "phase_hold", "t0": 13.5, "t1": 20, "value": "HEAT", "min_ratio": 0.8},
     {"kind": "phase", "t": 24.5, "window": 2, "value": "READY"},
     {"kind": "sim_log", "assert": "sleep_write_seen"},
     {"kind": "sim_log", "assert": "awake_write_seen"}]))

# ---------------------------------------------------------------- grind (touch)
# v6.5-scenarios: touch target via the layout hook ("grind") — the SIM build gives
# merakiplus's real GRIND button (150,372), the reference client its own (130,160).
SCENARIOS.append(base(
    "grind", "Grind via TOUCH hold-for-start (650 ms): weight ramp → auto-stop", 17,
    [ui(2, "touch", layout="grind", ms=700),
     snap(5, "grinding"), snap(14, "done")],
    [{"kind": "phase", "t": 4, "window": 1.2, "value": "GRIND"},
     {"kind": "work_sta_seen", "t0": 3, "t1": 13, "value": "0x0141"},
     {"kind": "word_hold", "word": 38, "t0": 3.5, "t1": 12.4,
      "min": 0, "max": 190, "signed": True},
     {"kind": "word_hold", "word": 38, "t0": 13.8, "t1": 15.5,
      "min": 184, "max": 188, "signed": True},
     {"kind": "word", "word": 3, "t": 13.5, "window": 1, "value": 0},
     {"kind": "word", "word": 6, "t": 14.2, "window": 1, "value": 0},
     {"kind": "word_hold", "word": 58, "t0": 4, "t1": 11, "min": 400, "max": 1500},
     {"kind": "sim_log", "assert": "activity_start", "activity": "grind",
      "t0": 2.5, "t1": 4},
     {"kind": "sim_log", "assert": "activity_stop", "activity": "grind",
      "reason_contains": "weight", "t0": 11, "t1": 14},
     {"kind": "phase", "t": 15.5, "window": 1.5, "value": "READY"}]))

# ---------------------------------------------------------------- brew
# v6.6 calibration (STEAMLIVE §3/§8): brew start +0.17 s (live), weight ramp
# ~18.6 units/s from ~pre+2.75 s, predictive stop ~target-8 (352) → stop ~29.9 s
# (before: 31.3). Three expectation windows adjusted 1:1 (check count unchanged):
# word51 ext to 29.5; word39 max 365 (post-drip in the window); bit2 pulse from 29.5.
SCENARIOS.append(base(
    "brew", "Brew: pre-infusion plateau (word51≈358, 6 s) → extraction (≈1346) "
            "→ predictive weight stop → post-drip", 40,
    [ui(2, "action", name="brew"),
     snap(5, "preinfusion"), snap(15, "extraction"), snap(33, "stop")],
    [{"kind": "phase_seq", "t0": 0, "t1": 40,
      "values": ["READY", "PRE", "EXT", "READY"]},
     # v7.0: the live view shows the solid white STOP pill (200x56 at 140,364), sampled on
     # its left and right ends, clear of the label and the rounded caps. Ref: REF profile.
     {"kind": "region", "t": 15, "rect": [152, 376, 20, 32],
      "color": [255, 255, 255], "tol": 20},
     {"kind": "region", "t": 15, "rect": [308, 376, 20, 32],
      "color": [255, 255, 255], "tol": 20},
     {"kind": "phase", "t": 4, "window": 1.5, "value": "PRE"},
     {"kind": "phase", "t": 12, "window": 2, "value": "EXT"},
     {"kind": "word_hold", "word": 51, "t0": 3.2, "t1": 8.2, "min": 330, "max": 395},
     {"kind": "word_hold", "word": 51, "t0": 10.5, "t1": 29.5, "min": 1250, "max": 1500},
     {"kind": "word_hold", "word": 39, "t0": 12, "t1": 31,
      "min": -10, "max": 365, "signed": True},
     {"kind": "work_sta_seen", "t0": 9, "t1": 31, "value": "0x1307"},
     {"kind": "work_sta_seen", "t0": 29.5, "t1": 32.6, "value": "0x0105"},
     {"kind": "word", "word": 22, "t": 33, "window": 1.5, "value": 0},
     {"kind": "word_hold", "word": 39, "t0": 33.5, "t1": 36.5,
      "min": 356, "max": 376, "signed": True},
     {"kind": "sim_log", "assert": "activity_stop", "activity": "brew",
      "reason_contains": "weight", "t0": 29, "t1": 34},
     {"kind": "phase", "t": 35, "window": 2, "value": "READY"}]))

# ---------------------------------------------------------------- hotwater
SCENARIOS.append(base(
    "hotwater", "Hot water: sub9 len11 → weight dosing → auto-stop at word39≥word17", 15,
    [ui(2, "action", name="water"), snap(5, "dispensing"), snap(12, "done")],
    [{"kind": "phase", "t": 4, "window": 1.5, "value": "WATER"},
     {"kind": "work_sta_seen", "t0": 2.5, "t1": 9, "value": "0x030B"},
     {"kind": "word_hold", "word": 39, "t0": 2.5, "t1": 8.4, "min": 0, "max": 412},
     {"kind": "word_hold", "word": 39, "t0": 8.8, "t1": 9.1, "min": 394, "max": 415},
     {"kind": "word", "word": 16, "t": 10, "window": 1, "value": 0},
     {"kind": "word_hold", "word": 39, "t0": 11, "t1": 13.5, "min": 380, "max": 405},
     {"kind": "word_hold", "word": 52, "t0": 3.5, "t1": 8, "min": 450, "max": 700},
     {"kind": "sim_log", "assert": "activity_stop", "activity": "water",
      "reason_contains": "weight", "t0": 7, "t1": 10},
     {"kind": "phase", "t": 12, "window": 1.5, "value": "READY"}]))

# ---------------------------------------------------------------- steam auto
# v6.6 (STEAMLIVE §5-6): display steam is the latch path (word24=1, no wand
# gating); the wand switch is the flush path. The order is therefore display start
# BEFORE wand down (wand first would start a flush, and the UI's guards would then
# block the steam start — 'WAIT UNTIL THE ACTIVITY ENDS'). The expectations are
# unchanged; the wand is still down for the whole steaming (as on the live machine).
SCENARIOS.append(base(
    "steam_auto", "Steam w/ wand: milk ramp → AUTO stop at word48≥word25 (no stop write)", 40,
    [ui(2, "action", name="steam"),
     sim_ev(3, {"type": "wand", "on": True}),
     snap(10, "steaming"), snap(34, "auto-stop")],
    [{"kind": "phase", "t": 5, "window": 2, "value": "STEAM"},
     {"kind": "work_sta_seen", "t0": 3, "t1": 32, "value": "0x4121"},
     {"kind": "word_hold", "word": 48, "t0": 4, "t1": 31, "min": 230, "max": 660},
     {"kind": "word_min_seen", "word": 48, "t0": 30, "t1": 35.5, "min": 650},
     {"kind": "word_hold", "word": 48, "t0": 33, "t1": 34.5, "min": 640, "max": 680},
     {"kind": "word", "word": 24, "t": 34, "window": 1.5, "value": 0},
     {"kind": "sim_log", "assert": "activity_stop", "activity": "steam",
      "reason_contains": "milk", "t0": 30, "t1": 35},
     {"kind": "sim_log", "assert": "no_write_word", "word": 24,
      "t0": 3, "t1": 31},
     {"kind": "phase", "t": 36, "window": 2, "value": "READY"}]))

# ---------------------------------------------------------------- steam manual
# v6.6: display start before wand down (same reason as steam_auto).
SCENARIOS.append(base(
    "steam_manual_stop", "Steam w/ wand → MANUAL stop (sub24 len8 word24=0) before setpoint", 12,
    [ui(2, "action", name="steam"),
     sim_ev(3, {"type": "wand", "on": True}),
     ui(6, "action", name="steam_stop"),
     snap(4, "steaming"), snap(9, "stopped")],
    [{"kind": "phase", "t": 4, "window": 1.5, "value": "STEAM"},
     {"kind": "sim_log", "assert": "write_words", "sub": 24,
      "words": [0, 650, 0, 0, 0, 0, 0, 0], "t0": 5.8, "t1": 7},
     {"kind": "word", "word": 24, "t": 7.5, "window": 1, "value": 0},
     {"kind": "word_hold", "word": 48, "t0": 7, "t1": 10, "min": 240, "max": 330},
     {"kind": "sim_log", "assert": "activity_stop", "activity": "steam",
      "reason_contains": "manual", "t0": 5.5, "t1": 8},
     {"kind": "phase", "t": 9, "window": 1.5, "value": "READY"}]))

# ---------------------------------------------------------------- steam wand/flush
# v6.6 NEW (STEAMLIVE §6-7): physical wand DURING brew = flush (latch=0):
# work_sta 0x5327 (brew ∪ steam), word24=0, word46=1, NO auto-stop even though
# the milk passes 650; stop on wand release; brew weight/flow continues
# undisturbed; flush = NO UART writes.
SCENARIOS.append(base(
    "steam_wand_flush", "Wand flush DURING brew: 0x5327, word24=0, no auto-stop "
                        ">650, release stop, brew continues (no flush writes)", 34,
    [ui(1, "action", name="brew"),
     sim_ev(9.5, {"type": "milk_jump", "value": 600}),
     sim_ev(10, {"type": "wand_down"}),
     sim_ev(16, {"type": "wand_up"}),
     snap(12, "flush-under-brew"), snap(20, "brew-continues"), snap(31, "ready")],
    [{"kind": "phase", "t": 4, "window": 1, "value": "PRE"},
     {"kind": "phase", "t": 9, "window": 0.7, "value": "EXT"},
     {"kind": "work_sta_seen", "t0": 8, "t1": 9.8, "value": "0x1307"},
     {"kind": "sim_log", "assert": "activity_start", "activity": "steam",
      "t0": 9.8, "t1": 10.5},
     {"kind": "work_sta_seen", "t0": 10.2, "t1": 15.8, "value": "0x5327"},
     {"kind": "phase", "t": 13, "window": 1, "value": "STEAM"},
     {"kind": "word", "word": 24, "t": 13, "window": 1, "value": 0},
     {"kind": "word", "word": 46, "t": 13, "window": 1, "value": 1},
     {"kind": "word", "word": 44, "t": 13, "window": 1, "value": 1,
      "mask": "0x0001"},
     {"kind": "word_hold", "word": 48, "t0": 14.6, "t1": 15.8,
      "min": 650, "max": 720},
     {"kind": "word_min_seen", "word": 48, "t0": 14.5, "t1": 16, "min": 660},
     {"kind": "sim_log", "assert": "no_write_word", "word": 24,
      "t0": 9, "t1": 17},
     {"kind": "sim_log", "assert": "activity_stop", "activity": "steam",
      "reason_contains": "wand", "t0": 15.5, "t1": 16.5},
     {"kind": "work_sta_seen", "t0": 16.5, "t1": 20, "value": "0x1307"},
     {"kind": "phase", "t": 18, "window": 1, "value": "EXT"},
     {"kind": "word_hold", "word": 39, "t0": 12.5, "t1": 15.5,
      "min": 40, "max": 120, "signed": True},
     {"kind": "word_min_seen", "word": 39, "t0": 15.2, "t1": 16.2,
      "min": 100, "signed": True},
     {"kind": "sim_log", "assert": "activity_stop", "activity": "brew",
      "reason_contains": "weight", "t0": 27.5, "t1": 30},
     {"kind": "word", "word": 22, "t": 30, "window": 1, "value": 0},
     {"kind": "phase", "t": 32, "window": 1.5, "value": "READY"}]))

# ---------------------------------------------------------------- steam overshoot
# v6.6 NEW (STEAMLIVE §5.2): cold milk (~12 °C) → ramp ~1.4 °C/s → AUTO stop
# at 650 → thermal overshoot +3.9 °C: peak ~689 ~11 s after stop (exponential
# approach), word24 cleared by the MCU ~0.18 s after the bits, cur1 ~7340 / word51 ~16650.
SCENARIOS.append(base(
    "steam_auto_overshoot", "Cold milk → auto-stop 650 → overshoot peak ~689 "
                            "(+39) ~11 s after stop; cur1 7340 / word51 16650", 60,
    [sim_ev(1, {"type": "milk_jump", "value": 116}),
     ui(2, "action", name="steam"),
     snap(20, "steaming"), snap(45, "auto-stop"), snap(55, "overshoot-peak")],
    [{"kind": "phase", "t": 10, "window": 2, "value": "STEAM"},
     {"kind": "word_hold", "word": 48, "t0": 6, "t1": 40, "min": 110, "max": 645},
     {"kind": "word_hold", "word": 50, "t0": 10, "t1": 40, "min": 6000, "max": 8000},
     {"kind": "word_hold", "word": 51, "t0": 10, "t1": 40,
      "min": 14000, "max": 17500},
     {"kind": "word", "word": 46, "t": 20, "window": 2, "value": 0},
     {"kind": "word", "word": 44, "t": 20, "window": 2, "value": 0,
      "mask": "0x0001"},
     {"kind": "sim_log", "assert": "activity_stop", "activity": "steam",
      "reason_contains": "milk", "t0": 41, "t1": 45},
     {"kind": "word", "word": 24, "t": 44.5, "window": 1, "value": 0},
     {"kind": "phase", "t": 46, "window": 1.5, "value": "READY"},
     {"kind": "word_min_seen", "word": 48, "t0": 50, "t1": 56, "min": 680},
     {"kind": "word_hold", "word": 48, "t0": 52, "t1": 57, "min": 680, "max": 695},
     {"kind": "word_min_seen", "word": 48, "t0": 53, "t1": 56, "min": 685},
     {"kind": "sim_log", "assert": "no_write_word", "word": 24,
      "t0": 3, "t1": 42}]))

# ---------------------------------------------------------------- faults E1/E2/E3
# v6.5-scenarios: merakiplus's act_guard blocks start during an active error (DA toast
# 'FEJL AKTIV · START SPÆRRET') — no start frame is sent, so the sim has no brew to
# safety-stop. Ref oracle (unguarded start → MCU stop): sim_ui_profile-ref.json.
SCENARIOS.append(base(
    "fault_e1", "E1 (group >110°): error UI + brew start BLOCKED (act_guard toast)", 12,
    [sim_ev(3, {"type": "fault", "e1": 32, "dur": 5}),
     ui(4, "action", name="brew"), snap(5, "error")],
    [{"kind": "phase", "t": 5, "window": 1.5, "value": "ERROR"},
     {"kind": "word", "word": 34, "t": 5, "window": 1.5, "value": 32},
     {"kind": "toast", "t": 4.5, "window": 1.0, "contains": "Fejl aktiv"},
     {"kind": "word", "word": 22, "t": 6.5, "window": 1, "value": 0},
     {"kind": "phase", "t": 9.5, "window": 1.5, "value": "READY"},
     # v7.0: the error view is the DARK layer (bgdark, espresso brown-black — not the
     # copper ~[188,94,42]), sampled below the hint line and above the clock, with the red
     # error ring (240,170 r44-47; its bottom stroke at 240,215). Ref oracle: REF profile.
     {"kind": "region", "t": 5, "rect": [180, 345, 120, 60],
      "color": [28, 18, 15], "tol": 20},
     {"kind": "point", "t": 5, "point": [240, 215], "color": [255, 92, 72], "tol": 40}]))

SCENARIOS.append(base(
    "fault_e2", "E2 bit4 (milk NTC): steam is safety-stopped", 16,
    [sim_ev(1, {"type": "wand", "on": True}),
     ui(2, "action", name="steam"),
     sim_ev(5, {"type": "fault", "e2": 16, "dur": 4}),
     snap(6.5, "error")],
    [{"kind": "phase", "t": 3.5, "window": 1, "value": "STEAM"},
     {"kind": "phase", "t": 6.5, "window": 1.5, "value": "ERROR"},
     {"kind": "word", "word": 35, "t": 6.5, "window": 1.5, "value": 16},
     {"kind": "sim_log", "assert": "activity_stop", "activity": "steam",
      "reason_contains": "fault", "t0": 4.5, "t1": 7},
     {"kind": "phase", "t": 11, "window": 1.5, "value": "READY"}]))

SCENARIOS.append(base(
    "fault_e3", "E3 bit4 (brew boiler >120°): error UI in idle", 12,
    [sim_ev(3, {"type": "fault", "e3": 16, "dur": 5}), snap(5, "error")],
    [{"kind": "phase", "t": 5, "window": 1.5, "value": "ERROR"},
     {"kind": "word", "word": 36, "t": 5, "window": 1.5, "value": 16},
     {"kind": "phase", "t": 9.5, "window": 1.5, "value": "READY"}]))

# ---------------------------------------------------------------- CRC-garbage
SCENARIOS.append(base(
    "crc_garbage", "CRC corruption + garbage stream: UI must resync and keep the link", 12,
    [sim_ev(3, {"type": "garbage", "dur": 2.5, "crc_too": True}),
     snap(4, "garbage"), snap(9, "recovered")],
    [{"kind": "crc_bad_min", "value": 1},
     {"kind": "phase_hold", "t0": 2, "t1": 11, "value": "READY", "min_ratio": 0.9},
     {"kind": "link_up_at", "t": 8},
     {"kind": "cmd6", "field": "fail", "op": "==", "value": 0}]))

# ---------------------------------------------------------------- no-ACK
SCENARIOS.append(base(
    "no_ack", "Missing ACK: SAVE must block (retransmit→FAIL, config unchanged), "
              "then recovery with the normal profile", 12,
    [ui(3, "save", param="brew_temp", value=880),
     ev(6, "sim", cmd="ack_profile", profile="normal"),
     ui(7, "save", param="brew_temp", value=880),
     snap(4, "save-blocked"), snap(9, "save-ok")],
    [{"kind": "save", "t": 3.6, "window": 0.9, "state": "fail", "param": "brew_temp"},
     {"kind": "toast", "t": 3.6, "window": 1.2, "contains": "Gem fejlet"},
     {"kind": "config", "field": "brewTemper", "t": 5, "value": 870},
     {"kind": "sim_log", "assert": "acks_dropped_min", "value": 2},
     {"kind": "sim_log", "assert": "writes_min", "sub": 11, "value": 2,
      "t0": 2.8, "t1": 4.2},
     {"kind": "cmd6", "field": "fail", "op": ">=", "value": 1},
     {"kind": "cmd6", "field": "retx", "op": ">=", "value": 1},
     {"kind": "save", "t": 8, "window": 1, "state": "ok", "param": "brew_temp"},
     {"kind": "config", "field": "brewTemper", "t": 9, "value": 880}],
    sim={"profile": "no_ack"},
    universal={"no_cmd6_fail": False}))

# ---------------------------------------------------------------- offline
SCENARIOS.append(base(
    "offline", "Offline (link silent for 5 s): UI shows OFFLINE → recovery on link up", 14,
    [sim_ev(3, {"type": "link", "state": "silent"}),
     sim_ev(8, {"type": "link", "state": "up"}),
     snap(6, "offline"), snap(11, "recovered")],
    [{"kind": "offline_seen", "t0": 4.5, "t1": 8},
     {"kind": "phase", "t": 6, "window": 1.5, "value": "OFFLINE"},
     {"kind": "link_up_at", "t": 9.5},
     {"kind": "phase", "t": 11, "window": 1.5, "value": "READY"}]))

# ---------------------------------------------------------------- out-of-spec
# v6.5-scenarios: merakiplus's frame policy rejects the WHOLE sub1 len30 block before
# the UART (BLOCK +1) — the sim never sees an out-of-spec value. Ref oracle
# (boot push sends the values, sim logs range_violation): sim_ui_profile-ref.json.
SCENARIOS.append(base(
    "out_of_spec", "Out-of-spec config: UI blocks start + the frame policy "
                   "blocks the boot push (BLOCK≥1); fix → start works", 14,
    [ui(3, "action", name="brew"),
     ev(6, "runner", op="config_set", field="brewTemper", value=870),
     ev(6.2, "runner", op="config_set", field="autoGrindWeight", value=185),
     ev(6.4, "runner", op="config_set", field="AtuoSteamTarge", value=650,
        reload=True),
     ui(8, "action", name="brew"),
     snap(3.5, "oor-blocked"), snap(10, "brew-after-fix")],
    [{"kind": "config_oor_min", "t0": 1, "t1": 5, "min": 3},
     {"kind": "toast", "t": 3.5, "window": 1, "contains": "uden for spec"},
     {"kind": "sim_log", "assert": "no_writes_between", "t0": 1, "t1": 5.5,
      "except_subs": [0, 1, 112]},
     {"kind": "cmd6", "field": "block", "op": ">=", "value": 1},
     {"kind": "phase", "t": 10, "window": 2, "value": "PRE"}],
    config={"brewTemper": 1200, "autoGrindWeight": 999, "AtuoSteamTarge": 710},
    universal={"ranges": False}))

# ---------------------------------------------------------------- language EN/DA
SCENARIOS.append(base(
    "language", "Language setting (config-only): stock index 0=EN..10; merakiplus's labels "
                "are fixed Danish (DA is not among stock's 11 languages — documented)", 10,
    [ui(2, "save", param="language", value=1),
     ui(5, "save", param="language", value=0),
     snap(3, "lang-de"), snap(7, "lang-en")],
    [{"kind": "config", "field": "language", "t": 3, "value": 1},
     {"kind": "lang", "t": 3.2, "window": 1, "value": 1},
     {"kind": "config", "field": "language", "t": 6, "value": 0},
     {"kind": "lang", "t": 6.5, "window": 1, "value": 0},
     {"kind": "sim_log", "assert": "no_writes_between", "t0": 1.5, "t1": 8,
      "except_subs": [0, 1, 112]}]))

# ---------------------------------------------------------------- boot-flag
SCENARIOS.append(base(
    "boot_flag_toggle", "BOOT-UI toggle: /customer/new touch/rm in sandbox (flag only, "
                        "no MCU writes, no reboot)", 10,
    [ui(2, "boot_flag", value=True),
     ui(6, "boot_flag", value=False),
     snap(3.5, "flag-B"), snap(7.5, "flag-A")],
    [{"kind": "flag", "t": 3, "exists": True},
     {"kind": "word", "word": 0, "t": 3.5, "window": 1, "value": 1},
     {"kind": "flag", "t": 7, "exists": False},
     {"kind": "sim_log", "assert": "no_writes_between", "t0": 1, "t1": 9,
      "except_subs": [0, 1, 112]}]))

# ---------------------------------------------------------------- runtime switch
SCENARIOS.append(base(
    "runtime_switch", "SWITCH TO ORIGINAL NOW (MERAKIPLUS_SWITCH_DRY=1): bye view → back, "
                      "flag unchanged, process keeps running", 10,
    [ui(3, "runtime_switch"), snap(3.6, "bye"), snap(7, "back")],
    [{"kind": "view", "t": 3.6, "window": 0.9, "value": "bye"},
     {"kind": "view", "t": 5.5, "window": 1, "value": "main"},
     {"kind": "flag", "t": 8, "exists": False},
     {"kind": "sim_log", "assert": "no_writes_between", "t0": 1, "t1": 9,
      "except_subs": [0, 1, 112]}]))

# ---------------------------------------------------------------- gesture switcher
SCENARIOS.append(base(
    "gesture_switcher", "Gesture switcher: REAL switcher_host in file mode (3×1.6 s "
                        "corner hold → trigger; 2 holds → none) + UI ignores the corner", 12,
    [ev(1, "runner", op="gesture", name="positiv", presses=3, inject_ui=True),
     ev(5, "runner", op="gesture", name="negativ_2holds", presses=2),
     snap(8, "home")],
    [{"kind": "gesture", "name": "positiv", "expect_trigger": True},
     {"kind": "gesture", "name": "negativ_2holds", "expect_trigger": False},
     {"kind": "phase_hold", "t0": 2, "t1": 11, "value": "READY", "min_ratio": 0.9},
     {"kind": "sim_log", "assert": "no_writes_between", "t0": 1, "t1": 11,
      "except_subs": [0, 1, 112]}]))

# ---------------------------------------------------------------- replay v2/v3/session
SCENARIOS.append(base(
    "replay_v3", "REPLAY serve: v3 grind+brew trace as ground truth (8× speed)", 42,
    [snap(7, "grind"), snap(14.8, "pre"), snap(16.5, "ext"), snap(19, "stop")],
    [{"kind": "phase", "t": 7, "window": 0.8, "value": "GRIND"},
     {"kind": "work_sta_seen", "t0": 6.3, "t1": 7.8, "value": "0x0141"},
     {"kind": "phase", "t": 14.7, "window": 0.6, "value": "PRE"},
     {"kind": "phase", "t": 16.5, "window": 1, "value": "EXT"},
     # v6.5-scenarios: window ends at 14.9 (not 15.0): the pressure jump to the
     # extraction plateau is measured at 14.99-15.02 s at 8× — 10-20 ms margin.
     # merakiplus writes one state line per changed frame (up to 30/s), so lines
     # can land in the window's last milliseconds. Pure timing, not behaviour.
     {"kind": "word_hold", "word": 51, "t0": 14.5, "t1": 14.9, "min": 300, "max": 420},
     {"kind": "word_hold", "word": 51, "t0": 15.6, "t1": 17.5, "min": 1200, "max": 1500},
     {"kind": "work_sta_seen", "t0": 15, "t1": 18, "value": "0x1307"},
     {"kind": "word_hold", "word": 39, "t0": 17.75, "t1": 18.1,
      "min": 330, "max": 380, "signed": True},
     {"kind": "replay_truth", "word": 39, "t0": 17.7, "t1": 18.05,
      "val_min": 320, "val_max": 370, "signed": True},
     {"kind": "replay_truth", "word": 51, "t0": 14.4, "t1": 14.95,
      "val_min": 300, "val_max": 420},
     {"kind": "phase", "t": 20, "window": 2, "value": "READY"}],
    replay={"csv": "sim/fixtures/captures/uart_live_v3.csv.gz", "mode": "serve", "speed": 8}))

SCENARIOS.append(base(
    "replay_v2", "REPLAY serve: v2 hot water trace (2 dispenses, 8× speed)", 20,
    [snap(6.3, "water1"), snap(11.8, "water2")],
    [{"kind": "phase", "t": 6.3, "window": 0.6, "value": "WATER"},
     {"kind": "phase", "t": 11.8, "window": 0.6, "value": "WATER"},
     {"kind": "replay_truth", "word": 39, "t0": 6.4, "t1": 6.9, "peak_min": 400},
     {"kind": "word_hold", "word": 39, "t0": 7.2, "t1": 7.7, "min": 385, "max": 402},
     {"kind": "word", "word": 16, "t": 7.5, "window": 1, "value": 0},
     {"kind": "phase", "t": 14, "window": 2, "value": "READY"}],
    replay={"csv": "sim/fixtures/captures/uart_live_v2.csv.gz", "mode": "serve", "speed": 8}))

SCENARIOS.append(base(
    "replay_session", "REPLAY serve: session sleep/drain trace (8×): SLEEP most "
                      "of the time (keep-awake writes cause brief flickers — "
                      "documented), cooling boilers", 20,
    [snap(5, "sleep"), snap(15, "cooling")],
    [{"kind": "phase_hold", "t0": 2, "t1": 19, "value": "SLEEP", "min_ratio": 0.75},
     {"kind": "work_sta_seen", "t0": 2, "t1": 19, "value": "0x0001"},
     {"kind": "word_hold", "word": 47, "t0": 2, "t1": 19, "min": 990, "max": 1030},
     {"kind": "word_hold", "word": 40, "t0": 2, "t1": 19, "min": 830, "max": 860}],
    replay={"csv": "sim/fixtures/captures/uart_session.csv.gz", "mode": "serve", "speed": 8}))

# ---------------------------------------------------------------- settings-matrix
def gen_matrix():
    spec = load_spec()
    cfg = load_baseline_config()
    params = {p["id"]: p for p in spec["parameters"]}
    # (param id, persist type as merakiplus v6 handles it)
    mcu_params = ["grind_weight", "brew_weight", "hotwater_weight", "brew_temp",
                  "head_temp", "steam_milk_target", "steam_gear",
                  "preinfusion_pressure", "preinfusion_switch", "preinfusion_time",
                  "break_time", "grind_mode"]
    cfg_params = ["backlight", "volume", "temp_unit", "language",
                  "hotwater_shortcut", "sumtime", "cleartip", "timemode",
                  "automatically", "init_state", "lang_patch", "hotwater_mode"]
    locked_params = ["hotwater_temp", "sleep_mode", "sleep_setting", "brew_mode",
                     "wifi_onoff"]
    events, expect = [], []
    t = 2.0
    step_dt = 0.9

    def values_for(p):
        vals = []
        lo, hi, st = p.get("min"), p.get("max"), p.get("step") or 1
        orig = cfg.get(p["config_field"])
        if lo is not None:
            vals.append(lo)
        if hi is not None:
            vals.append(hi)
        if lo is not None and lo + st != lo and (hi is None or lo + st <= hi):
            vals.append(lo + st)
        if isinstance(orig, int):
            vals.append(orig)
        out = []
        for v in vals:
            if v not in out:
                out.append(v)
        return out

    def nowrite(t0, t1):
        return {"kind": "sim_log", "assert": "no_writes_between",
                "t0": round(t0, 2), "t1": round(t1, 2),
                "except_subs": [0, 1, 112]}

    def expected_words(pid, value, cfg_now):
        """merakiplus's ACTUAL SAVE writes (UI_v6.md §4.1) — not parity_spec
        word-for-word. The ref oracle lives in sim/sim_ui_profile-ref.json."""
        p = params[pid]
        if pid == "preinfusion_time":
            # sub113 len1 (stock: usart_send(6,113,1) @0xeb700/0xeb880)
            br = int(cfg_now.get("breakTime", 5))
            return 113, [pack113(value, br)]
        if pid in ("preinfusion_pressure", "preinfusion_switch"):
            sw = int(cfg_now.get("preInfusionSwitch", 1))
            pr = int(cfg_now.get("preInfusionPressure", 4))
            if pid == "preinfusion_switch":
                sw = value
            else:
                pr = value
            ti = int(cfg_now.get("preInfusionTime", 6))
            br = int(cfg_now.get("breakTime", 5))
            return 112, [pack112(sw, pr), pack113(ti, br)]
        if pid == "head_temp":
            return 114, [value]
        if pid == "grind_mode":
            return 2, [value]           # sub2 len1 — NOT len2 w/ start word 3
        wr = p["write"]
        words = [0] * wr["len"]
        words[wr.get("offset", 0)] = value
        return wr["sub"], words

    for pid in mcu_params:
        p = params[pid]
        for v in values_for(p):
            events.append(ui(t, "save", param=pid, value=v))
            if pid == "break_time":
                # PAUSE TIME is LOCKED (UI_v6.md §4.5: MCU 0x1E10 overwrites
                # word113 lo byte): no editor, SAVE rejected ('LOCKED'), no
                # write, config unchanged.
                expect.append(nowrite(t - 0.05, t + 0.8))
                expect.append({"kind": "config", "field": p["config_field"],
                               "t": round(t + 0.8, 2),
                               "value": cfg[p["config_field"]]})
                expect.append({"kind": "save_rejected", "param": pid,
                               "t": round(t + 0.5, 2), "window": 0.45})
                t += step_dt
                continue
            if pid == "grind_mode" and v == cfg.get(p["config_field"]):
                # SAVE of the already-saved value: 'unchanged' — the editor
                # closes, neither MCU nor config is written.
                expect.append(nowrite(t - 0.05, t + 0.8))
                expect.append({"kind": "config", "field": p["config_field"],
                               "t": round(t + 0.8, 2), "value": v})
                expect.append({"kind": "save", "t": round(t + 0.5, 2),
                               "window": 0.45, "state": "unchanged",
                               "param": pid})
                t += step_dt
                continue
            sub, words = expected_words(pid, v, cfg)
            expect.append({"kind": "sim_log", "assert": "write_words",
                           "sub": sub, "words": words,
                           "t0": round(t - 0.05, 2), "t1": round(t + 0.8, 2)})
            expect.append({"kind": "config", "field": p["config_field"],
                           "t": round(t + 0.8, 2), "value": v})
            expect.append({"kind": "save", "t": round(t + 0.5, 2), "window": 0.45,
                           "state": "ok", "param": pid})
            cfg[p["config_field"]] = v      # running state for packing
            t += step_dt
    for pid in cfg_params:
        p = params[pid]
        for v in values_for(p):
            events.append(ui(t, "save", param=pid, value=v))
            expect.append(nowrite(t - 0.05, t + 0.8))
            if pid in ("init_state", "lang_patch"):
                # init/patch are not user settings and not exposed
                # (UI_v6.md §4.5): the hook rejects ('LOCKED'), field unchanged.
                expect.append({"kind": "config", "field": p["config_field"],
                               "t": round(t + 0.8, 2),
                               "value": cfg[p["config_field"]]})
            elif pid == "backlight" and v == 0:
                # BRIGHTNESS 0 is deliberately omitted (UI_v6.md §4.3: duty 100 =
                # dark screen); the range is 1..10, SAVE rejected ('OUT OF
                # RANGE'), config unchanged.
                expect.append({"kind": "config", "field": p["config_field"],
                               "t": round(t + 0.8, 2),
                               "value": cfg[p["config_field"]]})
            else:
                expect.append({"kind": "config", "field": p["config_field"],
                               "t": round(t + 0.8, 2), "value": v})
                cfg[p["config_field"]] = v
            t += step_dt
    for pid in locked_params:
        events.append(ui(t, "save", param=pid, value=1))
        expect.append({"kind": "save_rejected", "param": pid,
                       "t": round(t + 0.3, 2), "window": 0.6})
        t += 0.6
    events.append(snap(round(t + 0.5, 2), "matrix-done"))
    duration = round(t + 2, 1)
    return base(
        "settings_save_matrix",
        "Settings SAVE matrix: min/max/step/original per parity_spec parameter "
        "(merakiplus's guarded SAVE path: writes + config persist; config-only without "
        "writes; locked/omitted fields rejected; unchanged value = 'unchanged')",
        duration, events, expect)


SCENARIOS.append(gen_matrix())


def main():
    OUT.mkdir(exist_ok=True)
    for s in SCENARIOS:
        p = OUT / f"{s['name']}.json"
        p.write_text(json.dumps(s, indent=2, ensure_ascii=False) + "\n")
        print(f"wrote {p.name} ({s['duration_s']}s, "
              f"{len(s['events'])} events, {len(s['expect'])} expects)")
    print(f"\n{len(SCENARIOS)} scenarios in {OUT}")


if __name__ == "__main__":
    main()