#!/usr/bin/env python3
"""gen_scenarios.py — genererer sim/scenarios/*.json (reproducerbar kilde).

Scenarie-tidslinjer er kalibreret mod mcu_sim'ens fase-forsinkelser
(grind/brew +0,39 s, vand +0,18 s, damp +0,30 s) og de observerede kurver
(BREW.md/HOTWATER.md/SESSION.md). Settings-save-matrix udledes af
parity_spec.json (min/max/step pr. parameter).

v6.5-scenarios (RECONCILE.md): forventningerne asserter ourui's VOGTEDE
adfærd (UI_v6.md §4.1/4.3/4.5/4.6 + frame-politik): PRE-INF. TID = sub113
len1, KVÆRN-TILSTAND = sub2 len1, GEM af uændret værdi = 'unchanged',
PAUSE-TID låst, LYSSTYRKE 0 afvises, init/patch ikke eksponeret, fejl/spærret
start = toast i stedet for MCU-sikkerhedsstop, out-of-spec boot-push = BLOK.
Tryk-mål slås op via layout-krogen. Reference-klientens oracle-adfærd
(parity word-for-word, uvogtet) ligger i sim/sim_ui_profile-ref.json.

Kør: python3 sim/gen_scenarios.py   (overskriver scenarios/*.json)
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
    "idle", "Idle/klar: poll-rotation, keep-awake, boot-push, ingen aktivitet", 12,
    [snap(6, "idle")],
    [{"kind": "phase_hold", "t0": 2, "t1": 11.5, "value": "READY", "min_ratio": 0.95},
     {"kind": "work_sta_seen", "t0": 1, "t1": 12, "value": "0x0101"},
     {"kind": "word", "word": 34, "t": 6, "window": 3, "value": 0},
     {"kind": "word", "word": 35, "t": 6, "window": 3, "value": 0},
     {"kind": "word", "word": 36, "t": 6, "window": 3, "value": 0},
     {"kind": "sim_log", "assert": "no_writes_between", "t0": 1, "t1": 11.5,
      "except_subs": [0, 1, 112]},
     {"kind": "point", "t": 6, "point": [240, 8], "color": [98, 222, 168], "tol": 30},
     # v6.5-scenarios: ourui's gauge-bue (r 207-221) i fase-gradienten — ikke
     # reference-klientens side-bånd. Ref-oracle: sim_ui_profile-ref.json.
     {"kind": "point", "t": 6, "point": [240, 20], "color": [61, 163, 130], "tol": 30},
     # v6.5-scenarios: mørk vignette-baggrund m. hvid hero-tekst (fasen vises
     # i ring/chip/gauge) — ikke reference-klientens fase-fyldte cirkel.
     {"kind": "region", "t": 6, "rect": [200, 200, 80, 80],
      "color": [32, 36, 42], "tol": 20}]))

# ---------------------------------------------------------------- sleep→wake
SCENARIOS.append(base(
    "sleep_wake", "Sleep/drain (ord1=1) → wake (ord1=0) → opvarmning → klar", 28,
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
# v6.5-scenarios: tryk-målet via layout-krogen ("grind") — SIM-build'et giver
# ourui's rigtige KVÆRN-knap (150,372), reference-klienten sin egen (130,160).
SCENARIOS.append(base(
    "grind", "Grind via TOUCH hold-for-start (650 ms): vægt-ramp → auto-stop", 17,
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
      "reason_contains": "vægt", "t0": 11, "t1": 14},
     {"kind": "phase", "t": 15.5, "window": 1.5, "value": "READY"}]))

# ---------------------------------------------------------------- brew
# v6.6-kalibrering (STEAMLIVE §3/§8): bryg-start +0,17 s (live), vægt-ramp
# ~18,6 enh/s fra ~pre+2,75 s, prædiktivt stop ~mål-8 (352) → stop ~29,9 s
# (før: 31,3). Tre forventelses-vinduer justeret 1:1 (check-antal uændret):
# ord51-ext til 29,5; ord39-max 365 (efter-dryp i vinduet); bit2-puls fra 29,5.
SCENARIOS.append(base(
    "brew", "Bryg: pre-infusion plateau (ord51≈358, 6 s) → ekstraktion (≈1346) "
            "→ prædiktivt vægt-stop → efter-dryp", 40,
    [ui(2, "action", name="brew"),
     snap(5, "preinfusion"), snap(15, "extraction"), snap(33, "stop")],
    [{"kind": "phase_seq", "t0": 0, "t1": 40,
      "values": ["READY", "PRE", "EXT", "READY"]},
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
      "reason_contains": "vægt", "t0": 29, "t1": 34},
     {"kind": "phase", "t": 35, "window": 2, "value": "READY"}]))

# ---------------------------------------------------------------- hotwater
SCENARIOS.append(base(
    "hotwater", "Varmt vand: sub9 len11 → vægt-dosering → auto-stop ved ord39≥ord17", 15,
    [ui(2, "action", name="water"), snap(5, "dispensing"), snap(12, "done")],
    [{"kind": "phase", "t": 4, "window": 1.5, "value": "WATER"},
     {"kind": "work_sta_seen", "t0": 2.5, "t1": 9, "value": "0x030B"},
     {"kind": "word_hold", "word": 39, "t0": 2.5, "t1": 8.4, "min": 0, "max": 412},
     {"kind": "word_hold", "word": 39, "t0": 8.8, "t1": 9.1, "min": 394, "max": 415},
     {"kind": "word", "word": 16, "t": 10, "window": 1, "value": 0},
     {"kind": "word_hold", "word": 39, "t0": 11, "t1": 13.5, "min": 380, "max": 405},
     {"kind": "word_hold", "word": 52, "t0": 3.5, "t1": 8, "min": 450, "max": 700},
     {"kind": "sim_log", "assert": "activity_stop", "activity": "water",
      "reason_contains": "vægt", "t0": 7, "t1": 10},
     {"kind": "phase", "t": 12, "window": 1.5, "value": "READY"}]))

# ---------------------------------------------------------------- steam auto
# v6.6 (STEAMLIVE §5-6): display-steam er latch-vejen (ord24=1, ingen wand-
# gating); wand-kontakten er flush-vejen. Rækkefølgen er derfor display-start
# FØR wand-down (wand først ville starte en flush, og UI'ets vagter spærrer
# derefter steam-starten — 'VENT TIL AKTIVITET ER SLUT'). Forventningerne er
# uændrede; wand'en er stadig nede under hele dampningen (som i live).
SCENARIOS.append(base(
    "steam_auto", "Damp m. wand: mælk-ramp → AUTO-stop ved ord48≥ord25 (ingen stop-write)", 40,
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
      "reason_contains": "mælk", "t0": 30, "t1": 35},
     {"kind": "sim_log", "assert": "no_write_word", "word": 24,
      "t0": 3, "t1": 31},
     {"kind": "phase", "t": 36, "window": 2, "value": "READY"}]))

# ---------------------------------------------------------------- steam manuel
# v6.6: display-start før wand-down (samme årsag som steam_auto).
SCENARIOS.append(base(
    "steam_manual_stop", "Damp m. wand → MANUEL stop (sub24 len8 ord24=0) før setpoint", 12,
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
      "reason_contains": "manuel", "t0": 5.5, "t1": 8},
     {"kind": "phase", "t": 9, "window": 1.5, "value": "READY"}]))

# ---------------------------------------------------------------- steam wand/flush
# v6.6 NY (STEAMLIVE §6-7): fysisk wand UNDER bryg = flush (latch=0):
# work_sta 0x5327 (bryg ∪ damp), ord24=0, ord46=1, INGEN auto-stop selv om
# mælken passerer 650; stop ved wand-release; bryg-vægt/flow fortsætter
# uforstyrret; flush = INGEN UART-writes.
SCENARIOS.append(base(
    "steam_wand_flush", "Wand-flush UNDER bryg: 0x5327, ord24=0, ingen auto-stop "
                        ">650, release-stop, bryg fortsætter (ingen flush-writes)", 34,
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
      "reason_contains": "vægt", "t0": 27.5, "t1": 30},
     {"kind": "word", "word": 22, "t": 30, "window": 1, "value": 0},
     {"kind": "phase", "t": 32, "window": 1.5, "value": "READY"}]))

# ---------------------------------------------------------------- steam overshoot
# v6.6 NY (STEAMLIVE §5.2): kold mælk (~12 °C) → ramp ~1,4 °C/s → AUTO-stop
# ved 650 → termisk overshoot +3,9 °C: peak ~689 ~11 s efter stop (eksponentiel
# approach), ord24 MCU-nulstillet ~0,18 s efter bits, cur1 ~7340 / ord51 ~16650.
SCENARIOS.append(base(
    "steam_auto_overshoot", "Kold mælk → auto-stop 650 → overshoot-peak ~689 "
                            "(+39) ~11 s efter stop; cur1 7340 / ord51 16650", 60,
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
      "reason_contains": "mælk", "t0": 41, "t1": 45},
     {"kind": "word", "word": 24, "t": 44.5, "window": 1, "value": 0},
     {"kind": "phase", "t": 46, "window": 1.5, "value": "READY"},
     {"kind": "word_min_seen", "word": 48, "t0": 50, "t1": 56, "min": 680},
     {"kind": "word_hold", "word": 48, "t0": 52, "t1": 57, "min": 680, "max": 695},
     {"kind": "word_min_seen", "word": 48, "t0": 53, "t1": 56, "min": 685},
     {"kind": "sim_log", "assert": "no_write_word", "word": 24,
      "t0": 3, "t1": 42}]))

# ---------------------------------------------------------------- fejl E1/E2/E3
# v6.5-scenarios: ourui's act_guard spærrer start under aktiv fejl ('FEJL AKTIV
# · START SPÆRRET') — der sendes ingen start-frame, så sim'en har intet bryg at
# sikkerheds-stoppe. Ref-oracle (uvogtet start → MCU-stop): sim_ui_profile-ref.json.
SCENARIOS.append(base(
    "fault_e1", "E1 (gruppe >110°): fejl-UI + bryg-start SPÆRRET (act_guard-toast)", 12,
    [sim_ev(3, {"type": "fault", "e1": 32, "dur": 5}),
     ui(4, "action", name="brew"), snap(5, "error")],
    [{"kind": "phase", "t": 5, "window": 1.5, "value": "ERROR"},
     {"kind": "word", "word": 34, "t": 5, "window": 1.5, "value": 32},
     {"kind": "toast", "t": 4.5, "window": 1.0, "contains": "FEJL AKTIV"},
     {"kind": "word", "word": 22, "t": 6.5, "window": 1, "value": 0},
     {"kind": "phase", "t": 9.5, "window": 1.5, "value": "READY"}]))

SCENARIOS.append(base(
    "fault_e2", "E2 bit4 (mælk-NTC): damp sikkerhedsstoppes", 16,
    [sim_ev(1, {"type": "wand", "on": True}),
     ui(2, "action", name="steam"),
     sim_ev(5, {"type": "fault", "e2": 16, "dur": 4}),
     snap(6.5, "error")],
    [{"kind": "phase", "t": 3.5, "window": 1, "value": "STEAM"},
     {"kind": "phase", "t": 6.5, "window": 1.5, "value": "ERROR"},
     {"kind": "word", "word": 35, "t": 6.5, "window": 1.5, "value": 16},
     {"kind": "sim_log", "assert": "activity_stop", "activity": "steam",
      "reason_contains": "fejl", "t0": 4.5, "t1": 7},
     {"kind": "phase", "t": 11, "window": 1.5, "value": "READY"}]))

SCENARIOS.append(base(
    "fault_e3", "E3 bit4 (brygkedel >120°): fejl-UI i idle", 12,
    [sim_ev(3, {"type": "fault", "e3": 16, "dur": 5}), snap(5, "error")],
    [{"kind": "phase", "t": 5, "window": 1.5, "value": "ERROR"},
     {"kind": "word", "word": 36, "t": 5, "window": 1.5, "value": 16},
     {"kind": "phase", "t": 9.5, "window": 1.5, "value": "READY"}]))

# ---------------------------------------------------------------- CRC-garbage
SCENARIOS.append(base(
    "crc_garbage", "CRC-korruption + garbage-stream: UI skal resynke og bevare link", 12,
    [sim_ev(3, {"type": "garbage", "dur": 2.5, "crc_too": True}),
     snap(4, "garbage"), snap(9, "recovered")],
    [{"kind": "crc_bad_min", "value": 1},
     {"kind": "phase_hold", "t0": 2, "t1": 11, "value": "READY", "min_ratio": 0.9},
     {"kind": "link_up_at", "t": 8},
     {"kind": "cmd6", "field": "fail", "op": "==", "value": 0}]))

# ---------------------------------------------------------------- no-ACK
SCENARIOS.append(base(
    "no_ack", "ACK-udeblivelse: GEM skal blokere (retransmit→FAIL, config uændret), "
              "derefter recovery med normal profil", 12,
    [ui(3, "save", param="brew_temp", value=880),
     ev(6, "sim", cmd="ack_profile", profile="normal"),
     ui(7, "save", param="brew_temp", value=880),
     snap(4, "save-blocked"), snap(9, "save-ok")],
    [{"kind": "save", "t": 3.6, "window": 0.9, "state": "fail", "param": "brew_temp"},
     {"kind": "toast", "t": 3.6, "window": 1.2, "contains": "GEM FEJLET"},
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
    "offline", "Offline (link silent i 5 s): UI viser OFFLINE → recovery ved link up", 14,
    [sim_ev(3, {"type": "link", "state": "silent"}),
     sim_ev(8, {"type": "link", "state": "up"}),
     snap(6, "offline"), snap(11, "recovered")],
    [{"kind": "offline_seen", "t0": 4.5, "t1": 8},
     {"kind": "phase", "t": 6, "window": 1.5, "value": "OFFLINE"},
     {"kind": "link_up_at", "t": 9.5},
     {"kind": "phase", "t": 11, "window": 1.5, "value": "READY"}]))

# ---------------------------------------------------------------- out-of-spec
# v6.5-scenarios: ourui's frame-politik afviser HELE sub1 len30-blokken før
# UART'en (BLOK +1) — sim'en ser aldrig en værdi uden for spec. Ref-oracle
# (boot-push sender værdierne, sim logger range_violation): sim_ui_profile-ref.json.
SCENARIOS.append(base(
    "out_of_spec", "Out-of-spec config: UI spærrer start + frame-politikken "
                   "blokerer boot-push (BLOK≥1); rettelse → start virker", 14,
    [ui(3, "action", name="brew"),
     ev(6, "runner", op="config_set", field="brewTemper", value=870),
     ev(6.2, "runner", op="config_set", field="autoGrindWeight", value=185),
     ev(6.4, "runner", op="config_set", field="AtuoSteamTarge", value=650,
        reload=True),
     ui(8, "action", name="brew"),
     snap(3.5, "oor-blocked"), snap(10, "brew-after-fix")],
    [{"kind": "config_oor_min", "t0": 1, "t1": 5, "min": 3},
     {"kind": "toast", "t": 3.5, "window": 1, "contains": "UDEN FOR SPEC"},
     {"kind": "sim_log", "assert": "no_writes_between", "t0": 1, "t1": 5.5,
      "except_subs": [0, 1, 112]},
     {"kind": "cmd6", "field": "block", "op": ">=", "value": 1},
     {"kind": "phase", "t": 10, "window": 2, "value": "PRE"}],
    config={"brewTemper": 1200, "autoGrindWeight": 999, "AtuoSteamTarge": 710},
    universal={"ranges": False}))

# ---------------------------------------------------------------- sprog EN/DA
SCENARIOS.append(base(
    "language", "Sprog-setting (config-only): stock index 0=EN..10; ourui's labels "
                "er fast dansk (DA findes ikke i stock's 11 sprog — dokumenteret)", 10,
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
    "boot_flag_toggle", "BOOT-UI toggle: /customer/new touch/rm i sandbox (kun flag, "
                        "ingen MCU-writes, ingen reboot)", 10,
    [ui(2, "boot_flag", value=True),
     ui(6, "boot_flag", value=False),
     snap(3.5, "flag-B"), snap(7.5, "flag-A")],
    [{"kind": "flag", "t": 3, "exists": True},
     {"kind": "word", "word": 0, "t": 3.5, "window": 1, "value": 1},
     {"kind": "flag", "t": 7, "exists": False},
     {"kind": "sim_log", "assert": "no_writes_between", "t0": 1, "t1": 9,
      "except_subs": [0, 1, 112]}]))

# ---------------------------------------------------------------- runtime-skift
SCENARIOS.append(base(
    "runtime_switch", "SKIFT TIL ORIGINAL NU (OURUI_SWITCH_DRY=1): bye-view → tilbage, "
                      "flag uændret, proces bliver kørende", 10,
    [ui(3, "runtime_switch"), snap(3.6, "bye"), snap(7, "back")],
    [{"kind": "view", "t": 3.6, "window": 0.9, "value": "bye"},
     {"kind": "view", "t": 5.5, "window": 1, "value": "main"},
     {"kind": "flag", "t": 8, "exists": False},
     {"kind": "sim_log", "assert": "no_writes_between", "t0": 1, "t1": 9,
      "except_subs": [0, 1, 112]}]))

# ---------------------------------------------------------------- gestus-switcher
SCENARIOS.append(base(
    "gesture_switcher", "Gestus-switcher: ÆGTE switcher_host i fil-mode (3×1,6 s "
                        "hjørne-hold → trigger; 2 hold → ingen) + UI ignorerer hjørnet", 12,
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
    "replay_v3", "REPLAY serve: v3 grind+bryg-trace som sandhed (8× hastighed)", 42,
    [snap(7, "grind"), snap(14.8, "pre"), snap(16.5, "ext"), snap(19, "stop")],
    [{"kind": "phase", "t": 7, "window": 0.8, "value": "GRIND"},
     {"kind": "work_sta_seen", "t0": 6.3, "t1": 7.8, "value": "0x0141"},
     {"kind": "phase", "t": 14.7, "window": 0.6, "value": "PRE"},
     {"kind": "phase", "t": 16.5, "window": 1, "value": "EXT"},
     # v6.5-scenarios: vinduet sluttet ved 14,9 (ikke 15,0): trykspringet til
     # ekstraktions-plateauet er målt 14,99-15,02 s ved 8× — 10-20 ms margin.
     # ourui skriver en state-linje pr. ændret frame (op til 30/s), så linjer
     # kan falde i vinduets sidste millisekunder. Rent timing, ikke adfærd.
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
    "replay_v2", "REPLAY serve: v2 hot water-trace (2 dispenseringer, 8× hastighed)", 20,
    [snap(6.3, "water1"), snap(11.8, "water2")],
    [{"kind": "phase", "t": 6.3, "window": 0.6, "value": "WATER"},
     {"kind": "phase", "t": 11.8, "window": 0.6, "value": "WATER"},
     {"kind": "replay_truth", "word": 39, "t0": 6.4, "t1": 6.9, "peak_min": 400},
     {"kind": "word_hold", "word": 39, "t0": 7.2, "t1": 7.7, "min": 385, "max": 402},
     {"kind": "word", "word": 16, "t": 7.5, "window": 1, "value": 0},
     {"kind": "phase", "t": 14, "window": 2, "value": "READY"}],
    replay={"csv": "sim/fixtures/captures/uart_live_v2.csv.gz", "mode": "serve", "speed": 8}))

SCENARIOS.append(base(
    "replay_session", "REPLAY serve: session sleep/drain-trace (8×): SLEEP det meste "
                      "af tiden (keep-awake writes giver kortvarige flickers — "
                      "dokumenteret), kølende boilers", 20,
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
    # (param-id, persist-type som ourui v6 håndterer den)
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
        """ourui's FAKTISKE GEM-writes (UI_v6.md §4.1) — ikke parity_spec
        ord-for-ord. Ref-oracle ligger i sim/sim_ui_profile-ref.json."""
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
            return 2, [value]           # sub2 len1 — IKKE len2 m. start-ordet 3
        wr = p["write"]
        words = [0] * wr["len"]
        words[wr.get("offset", 0)] = value
        return wr["sub"], words

    for pid in mcu_params:
        p = params[pid]
        for v in values_for(p):
            events.append(ui(t, "save", param=pid, value=v))
            if pid == "break_time":
                # PAUSE-TID er LÅST (UI_v6.md §4.5: MCU 0x1E10 overskriver
                # ord113 lo-byte): ingen editor, GEM afvises ('LÅST'), ingen
                # write, config uændret.
                expect.append(nowrite(t - 0.05, t + 0.8))
                expect.append({"kind": "config", "field": p["config_field"],
                               "t": round(t + 0.8, 2),
                               "value": cfg[p["config_field"]]})
                expect.append({"kind": "save_rejected", "param": pid,
                               "t": round(t + 0.5, 2), "window": 0.45})
                t += step_dt
                continue
            if pid == "grind_mode" and v == cfg.get(p["config_field"]):
                # GEM af den allerede gemte værdi: 'unchanged' — editoren
                # lukkes, hverken MCU eller config skrives.
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
            cfg[p["config_field"]] = v      # løbende tilstand til packing
            t += step_dt
    for pid in cfg_params:
        p = params[pid]
        for v in values_for(p):
            events.append(ui(t, "save", param=pid, value=v))
            expect.append(nowrite(t - 0.05, t + 0.8))
            if pid in ("init_state", "lang_patch"):
                # init/patch er ikke brugerindstillinger og ikke eksponeret
                # (UI_v6.md §4.5): krogen afviser ('LÅST'), feltet uændret.
                expect.append({"kind": "config", "field": p["config_field"],
                               "t": round(t + 0.8, 2),
                               "value": cfg[p["config_field"]]})
            elif pid == "backlight" and v == 0:
                # LYSSTYRKE 0 er bevidst udeladt (UI_v6.md §4.3: duty 100 =
                # mørk skærm); intervallet er 1..10, GEM afvises ('UDEN FOR
                # GRÆNSER'), config uændret.
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
        "Settings-GEM matrix: min/max/step/original pr. parity_spec-parameter "
        "(ourui's vogtede GEM-sti: writes + config-persist; config-only uden "
        "writes; låste/udeladte felter afvises; uændret værdi = 'unchanged')",
        duration, events, expect)


SCENARIOS.append(gen_matrix())


def main():
    OUT.mkdir(exist_ok=True)
    for s in SCENARIOS:
        p = OUT / f"{s['name']}.json"
        p.write_text(json.dumps(s, indent=2, ensure_ascii=False) + "\n")
        print(f"skrevet {p.name} ({s['duration_s']}s, "
              f"{len(s['events'])} events, {len(s['expect'])} expects)")
    print(f"\n{len(SCENARIOS)} scenarier i {OUT}")


if __name__ == "__main__":
    main()