#!/usr/bin/env python3
"""mcu_sim.py — komplet SIMULERET powerMcu (MerakiTech Gen 1) til E2E-tests.

Taler UART-protokollen (PROTOCOL.md) over en pty som ourui's /dev/ttyS1
redirectes til i SIM-build. Ingen kontakt med fysisk enhed — kun lokal udvikling.

Model:
  * 129-ords register-file initialiseret fra sandbox-config.json (PARITY §1.3,
    inkl. byte-pakning word112/113/114) + MCU-defaults + sensor-baseline.
  * cmd3 read-query → 0x83-svar med præcis de sub/len der spørges om (≤32 B
    chunked reads håndteres af klientens parser; sim svarer i fulde frames).
  * cmd6 write → bånd-validering (0..31 + 112..114), trigger-politik (26/27/30
    skal være 0, ellers AFVIS+log), range-log (parity-grænser), memcpy+ACK-echo
    (0x86) med konfigurerbar latency/loss/retransmit-profil.
  * cmd8 → 0x88-echo; cmd9-svar på 0x89-versions-request (hvert 20 ms indtil svar).
  * Boot-push (sub1 len30 + sub112 len3) forventes/logges ved connect.
  * Fase-maskine med syntetiske sensor-kurver kalibreret mod BREW.md/HOTWATER.md/
    SESSION.md (grind/brew/hotwater/steam/sleep/drain/idle-pulser/heating).
  * Fejl-injektion: E1/E2/E3, CRC-garbage, ACK-udeblivelse, offline, wand,
    out-of-spec-config, kop-fjernelse, reboot.
  * Manuel STOP (v6.13, SIM_HANDOFF/UI_v6 §18): cmd6 sub16/sub19=0 (vand,
    også auto-tilstand), sub22=0 (bryg), sub6=0 (kværn) kører MCU'ens egen
    auto-stop-vej (post/drip/decay) og ACK'es; auto-stop er uændret.
  * DVALE/WAKE (v6.14, SIM_HANDOFF/UI_v6 §19): ord0=0 (cmd6 sub0 len2 [0,0])
    → ACK, derefter ≤0,5 s til dvale (sleep-task 0x2F52): work_sta=0, udgange
    0, aktiviteter stoppes sikkert, regs bevares, og UART'en er TAVS (status-
    tasken slettet → ingen svar på cmd3/cmd6, ingen 0x89). Keep-awake-overvåg-
    ningen pauserer mens der er slukket. Vågner KUN ved knap-hold ≥0,8 s
    (ctrl power_key) eller RTC-alarm (cmd8's alarm-felt) → CPU-reset til
    FABRIKKENS startværdier (ord0=1, ord11=920, ord114=50 …) + 0x89-runde →
    host'ens boot-push/re-push forventes igen (config genskabes).
  * Assertions-log (JSONL): ALLE writes m. validering + keep-awake-gap (>30 s)
    + trigger-/bånd-/range-overtrædelser → læses af run_e2e.py.
  * REPLAY: serve (afspil frames.csv som sandhed på live polls) og verify
    (kør fase-maskinen mod optagne writes og sammenlign svar frame-for-frame
    med dokumenterede tolerancer).

Kun Python stdlib. Brug: se README.md / --help.
"""
import argparse
import bisect
import calendar
import csv
import gzip
import heapq
import json
import math
import os
import random
import select
import socket
import struct
import sys
import termios
import time
import tty
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import proto
from proto import (REGN, WS_IDLE, WS_SLEEP, WS_GRIND, WS_BREW_PRE, WS_BREW_EXT,
                   WS_WATER, WS_STEAM, WS_PULSE_MASK, crc16_modbus, build_frame,
                   build_version_request, hexs, s16, reg_name, config_to_words,
                   load_baseline_config, load_spec, word_bounds, pack112, pack113,
                   unpack112, unpack113, MCU_DEFAULTS, SENSOR_BASELINE,
                   serial_words, ACK_PROFILES, FrameParser)

TICK = 0.010            # 10 ms status-task (PROTOCOL §6)
VER_INTERVAL = 0.020    # 0x89 hvert 20 ms indtil cmd9-svar
POLL_LATENCY = 0.005    # ~5 ms svartid på polls (observeret ~10 ms inkl. UART)
KEEPALIVE_MAX = 30.0    # word1=0 forventes hvert ≤30 s (ourui keep-awake)
BOOT_PUSH_WINDOW = 6.0  # boot-push forventes inden for 6 s efter første RX
DVALE_DELAY = 0.5       # v6.14: status-tasken tjekker ord0 hvert 500 ms (0x29D4)
POWER_KEY_HOLD_MIN = 0.8  # v6.14: PC.8 lav i 8×100 ms efter et slip → wake/reset

# Grænser for boolske/trigger-ord (ikke i parity_spec's parameter-tabel)
BOOL_WORDS = {0: (0, 1), 1: (0, 1), 3: (0, 1), 6: (0, 1), 7: (0, 1),
              8: (0, 1), 9: (0, 1), 16: (0, 1), 19: (0, 1), 20: (0, 1),
              22: (0, 1), 24: (0, 1), 28: (0, 2), 29: (0, 2)}
TRIGGER_WORDS = (26, 27, 30)


class AssertLog:
    """JSONL assertions-log som e2e-runneren læser."""
    def __init__(self, path=None):
        self.f = open(path, "a", buffering=1) if path else None
        self.events = []

    def log(self, t, _kind, **kw):
        e = {"t": round(t, 4), "kind": _kind, **kw}
        self.events.append(e)
        if self.f:
            self.f.write(json.dumps(e, ensure_ascii=False) + "\n")

    def close(self):
        if self.f:
            self.f.close()


class McuSim:
    """MCU-model + pty-link + ctrl-socket + replay-motor."""

    def __init__(self, config=None, spec=None, seed=1, profile="normal",
                 require_wand=True, range_policy="log", trigger_policy="reject",
                 assert_path=None, pty=True, ctrl_path=None, pty_file=None,
                 quiet=False):
        # require_wand er fra v6.6 en LEGACY no-op (bevaret for ctrl/API-kompat):
        # STEAMLIVE §5-6 beviste at display-steam (ord24=1) kører UDEN wand-
        # kontakt (latch-vejen), og wand-flush er kontakt-edge-styret. Flaget
        # gates ikke længere nogen vej; ping/ctrl rapporterer det stadig.
        self.quiet = quiet
        self.rng = random.Random(seed)
        self.spec = spec or load_spec()
        self.bounds = word_bounds(self.spec)
        self.cfg = config if config is not None else load_baseline_config()
        self.require_wand = require_wand
        self.range_policy = range_policy
        self.trigger_policy = trigger_policy
        self.profile = dict(ACK_PROFILES[profile]) if isinstance(profile, str) else dict(profile)
        self.alog = AssertLog(assert_path)
        self.stats = {"writes_ok": 0, "writes_rejected": 0, "acks_sent": 0,
                      "acks_dropped": 0, "queries": 0, "crc_bad_rx": 0,
                      "garbage_tx_bytes": 0, "version_reqs": 0, "boot_pushes": 0,
                      "reboots": 0, "dvale_entries": 0, "power_wakes": 0,
                      "dvale_rx_dropped": 0}
        # ---- v6.14: dvale/wake (UI_v6 §19, SIM_HANDOFF) ----
        self.power_state = "on"       # on|off (MCU'en er binær; UI har turning_*)
        self.dvale_at = None          # planlagt dvale-indgang (ord0=0 skrevet)
        self.power_key_down = False   # simuleret PC.8-knap (ctrl power_key)
        self.power_key_t0 = 0.0
        self._key_up_at = None        # "hold"-bekvemmelighed: auto-slip
        self.rtc = None               # {y,mo,d,h,mi,s} fra cmd8 (MCU'ens RTC)
        self.rtc_frac = 0.0
        self.alarm = None             # (dag,time,minut) fra cmd8; None = 0/0/0
        self.alarm_fired = False
        # ---- register-model ----
        self.regs = [0] * REGN
        for w, v in MCU_DEFAULTS.items():
            self.regs[w] = v
        for w, v in SENSOR_BASELINE.items():
            self.regs[w] = v
        for w, v in serial_words().items():
            self.regs[w] = v
        for w, v in config_to_words(self.cfg).items():
            self.regs[w] = v
        # ---- fase-maskine ----
        self.t = 0.0
        self.mode = "idle"            # idle|grind|brew|water|steam|sleep|off
        self.act = None               # aktiv fase-data (primær: grind/brew/water)
        self.pending = None           # forsinket start (trigger → aktuator)
        self.post = None              # efter-stop (drip/decay/puls)
        # v6.6 (STEAMLIVE §5-7): UAFHÆNGIG damp-kanal m. latch-gating.
        #   latch=1 (display): ord24=1 → auto-stop ved ord48≥ord25, MCU rydder
        #     selv bits 5+14 og nulstiller ord24 (~0,18 s efter bits, live-målt).
        #   latch=0 (wand/flush): PE.2-kontakt → bits 5+14 UDEN ord24, INGEN
        #     mælk-auto-stop (selv >650); stop ved wand-release. Kan køre
        #     SAMTIDIGT med primær aktivitet (work_sta = union, fx 0x5327).
        self.steam_ch = None          # {"latch","t0","milk0","target","src"}
        self.pending_steam = None     # forsinket display-start (write → bits)
        self.steam_clear_at = None    # MCU's forsinkede ord24-nulstilling
        # v6.6: damp-efter-fase (mælk-overshoot/afkøling, boiler-recovery,
        # cur-decay) er UAFHÆNGIG af primære aktiviteter — live køler mælken
        # videre gennem grind/bryg. {"t0","auto"|"flush","milk_stop","milk_peak"}
        self.steam_post = None
        self.wand_state = bool(self.regs[44] & 1)   # PE.2-kontakt (debounced)
        self.steam_consumed = False   # damp har sænket s-vandniveau → refill
        # float-akkumulatorer for langsomme temperaturer/niveauer (int-regs
        # truncere ellers); 68/70 er akkumulerende fra v6.6 (live-refill)
        self.ft = {w: float(self.regs[w]) for w in (40, 42, 47, 48, 68, 70)}
        self.ft_last = {w: self.regs[w] for w in self.ft}
        # ADC-baselines fra seed (replay) resp. SENSOR_BASELINE (live)
        self.adc_base = {w: self.regs[w] for w in (49, 69, 71, 72)}
        self.wraw_base = {53: self.regs[53], 55: self.regs[55]}
        self.next_pulse = self.rng.uniform(2.0, 6.0)
        self.pulse = None
        # v6.6: små vedligehold-varmecyklusser (cur1 ~400-1000, ord51 ~400-900)
        # — observeret i ALLE captures (v3/v2/session/steam-live: 28-47 % af
        # idle-frames har cur1 200-1500). Uden ws-bits (live: 0x0101 under dem).
        self.next_small = self.rng.uniform(1.0, 3.0)
        self.small_pulse = None
        self.faults = {}              # word -> (value, until|None)
        self.version_received = False
        self.next_ver = 0.0
        self.first_rx_t = None
        self.boot_push_setup = False
        self.boot_push_preinf = False
        self.last_awake_write = 0.0
        self.expect_keepawake = True
        self.keepalive_logged_at = -1e9
        self.boot_checked = False
        self.last_write_t = -1e9
        self.link = "up"              # up|silent|closed
        self.garbage_until = -1.0
        self.garbage_crc = False
        # ---- replay ----
        self.replay = None            # dict m. frames/timelines (se load_replay)
        self.rp_mode = None           # serve|verify
        self.rp_speed = 1.0
        self.rp_t = 0.0
        self.rp_idx = 0
        self.rp_verify = None
        self.rp_hold = False          # serve: replay clock frozen until 'replay_anchor'
        # ---- link/io ----
        self.pty = pty
        self.master = self.slave = None
        self.slave_path = None
        self.pty_file = pty_file
        if pty:
            self._open_pty()
        self.parser = FrameParser()
        self.txq = []                 # heap (due, seq, bytes)
        self.txseq = 0
        self.ctrl_path = ctrl_path
        self.ctrl_listener = None
        self.ctrl_clients = {}        # fd -> buf
        if ctrl_path:
            self._open_ctrl()
        self.running = True
        self.t0_wall = time.monotonic()
        # play-mode: live hastighed/pause (1.0 = uændret wall-clock-realtid)
        self.speed = 1.0
        self.paused = False
        self.calm = 1.0          # 1.0 = rå (tro mod optagelser); <1 = median-3+EMA-udjævning (demo/play)
        self.calm_prev = {}
        self.calm_hist = {}
        self.maint = True        # idle-vedligeholdspulser (deadhead); False = demo-only uden pulser
        self._last_wall = 0.0

    # ------------------------------------------------------------ link-setup
    def _open_pty(self):
        self.master, self.slave = os.openpty()
        os.set_blocking(self.master, False)
        # raw 8N1 på slave-siden (som displayets set_opt @0x144950):
        # ingen echo/ONLCR/ICRNL/IXON/IUTF8 — binære frames må ikke forvrænges
        try:
            tty.setraw(self.slave)
            attrs = termios.tcgetattr(self.slave)
            attrs[0] &= ~(termios.IXON | termios.IXOFF | termios.IXANY)
            attrs[1] &= ~termios.OPOST
            attrs[2] = (attrs[2] & ~termios.CSIZE) | termios.CS8
            attrs[2] &= ~(termios.PARENB | termios.CSTOPB)
            attrs[4] = termios.B57600
            attrs[5] = termios.B57600
            attrs[6][termios.VMIN] = 0
            attrs[6][termios.VTIME] = 0
            termios.tcsetattr(self.slave, termios.TCSANOW, attrs)
        except (OSError, termios.error):
            pass
        self.slave_path = os.ttyname(self.slave)
        if self.pty_file:
            Path(self.pty_file).write_text(self.slave_path + "\n")
        if not self.quiet:
            print(f"[mcu_sim] pty: {self.slave_path}", flush=True)

    def _close_pty(self):
        for fd in (self.master, self.slave):
            try:
                os.close(fd)
            except OSError:
                pass
        self.master = self.slave = None

    def _open_ctrl(self):
        p = Path(self.ctrl_path)
        p.unlink(missing_ok=True)
        self.ctrl_listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.ctrl_listener.bind(str(p))
        self.ctrl_listener.listen(4)
        self.ctrl_listener.setblocking(False)

    def log(self, msg):
        if not self.quiet:
            print(f"[mcu_sim t={self.t:8.3f}] {msg}", flush=True)

    # ------------------------------------------------------------ tx-planlægning
    def send_tx(self, data: bytes, delay: float = 0.0):
        if self.link != "up" or not self.pty or self.power_state == "off":
            return
        self.txseq += 1
        heapq.heappush(self.txq, (self.t + delay, self.txseq, data))

    def _flush_tx(self):
        while self.txq and self.txq[0][0] <= self.t:
            _, _, data = heapq.heappop(self.txq)
            if self.link != "up" or self.power_state == "off":
                continue
            try:
                os.write(self.master, data)
            except BlockingIOError:
                self.txseq += 1
                heapq.heappush(self.txq, (self.t + 0.005, self.txseq, data))
            except OSError as e:
                self.alog.log(self.t, "link_tx_error", err=str(e))
                self.link = "silent"

    # ------------------------------------------------------------ writes
    def _check_write(self, sub, words):
        """Returnér (ok, reasons[]) — bånd + trigger + range jf. opgave/PARITY."""
        reasons = []
        n = len(words)
        end = sub + n
        band_ok = end < 33 or end in (113, 114, 115)
        if not band_ok or end > REGN:
            reasons.append(f"band:sub{sub}+len{n}={end}")
        for i, w in enumerate(words):
            word = sub + i
            if word in TRIGGER_WORDS and w != 0:
                reasons.append(f"trigger:word{word}={w}")
        if self.range_policy != "off":
            for i, w in enumerate(words):
                word = sub + i
                if word in self.bounds:
                    lo, hi, _ = self.bounds[word]
                    if not (lo <= w <= hi):
                        reasons.append(f"range:word{word}={w}∉[{lo},{hi}]")
                elif word in BOOL_WORDS:
                    lo, hi = BOOL_WORDS[word]
                    if not (lo <= w <= hi):
                        reasons.append(f"range:word{word}={w}∉[{lo},{hi}]")
                elif word == 112:
                    sw, pr = unpack112(w)
                    if pr not in (3, 4):
                        reasons.append(f"range:word112.tryk={pr}∉[3,4]")
                    if sw not in (0, 1):
                        reasons.append(f"range:word112.switch={sw}")
                elif word == 113:
                    ti, br = unpack113(w)
                    if ti < 5:
                        reasons.append(f"range:word113.tid={ti}<5")
        return (not reasons), reasons

    def handle_write(self, sub, words, source="live"):
        n = len(words)
        ok, reasons = self._check_write(sub, words)
        band_ok = not any(r.startswith("band:") for r in reasons)
        trig_ok = not any(r.startswith("trigger:") for r in reasons)
        range_ok = not any(r.startswith("range:") for r in reasons)
        entry = {"sub": sub, "len": n, "words": list(words),
                 "named": {reg_name(sub + i): w for i, w in enumerate(words)},
                 "source": source}
        if not band_ok:
            self.stats["writes_rejected"] += 1
            self.alog.log(self.t, "write_rejected", verdict="band", reasons=reasons, **entry)
            self.log(f"AFVIST (bånd) cmd6 sub{sub} len{n}: {reasons}")
            return False
        if not trig_ok and self.trigger_policy == "reject":
            self.stats["writes_rejected"] += 1
            self.alog.log(self.t, "write_rejected", verdict="trigger", reasons=reasons, **entry)
            self.log(f"AFVIST (trigger) cmd6 sub{sub} len{n}: {reasons}")
            return False
        if not range_ok:
            for r in reasons:
                if r.startswith("range:"):
                    self.alog.log(self.t, "range_violation", reason=r, **entry)
            if self.range_policy == "reject":
                self.stats["writes_rejected"] += 1
                self.alog.log(self.t, "write_rejected", verdict="range", reasons=reasons, **entry)
                return False
        # memcpy (REGISTERMAP §3: ingen MCU range-checks; sim logger dem)
        self.regs[sub:sub + n] = [w & 0xFFFF for w in words]
        self.stats["writes_ok"] += 1
        prev_write_t = self.last_write_t
        self.last_write_t = self.t
        self.alog.log(self.t, "write", verdict="ok",
                      range_violations=[r for r in reasons if r.startswith("range:")],
                      **entry)
        # faktiske tilstands-skrivninger (til assertions-laget)
        for i, w in enumerate(words):
            word = sub + i
            if word == 1:
                if w == 0:
                    self.last_awake_write = self.t
                    self.keepalive_logged_at = -1e9
                else:
                    self.alog.log(self.t, "sleep_write", word=1, value=w)
            if word == 0:
                self.alog.log(self.t, "power_write", word=0, value=w)
                # v6.14: ord0=0 → status-tasken starter sleep inden for ≤0,5 s
                # (0x29D4 tjekker hvert 500 ms). ACK'es normalt først.
                if w == 0 and self.power_state == "on" and self.dvale_at is None:
                    self.dvale_at = self.t + DVALE_DELAY
            if word == 30 and w != 0:
                self.alog.log(self.t, "reboot_trigger", word=30, value=w)
                if self.trigger_policy == "pass":
                    self.do_reboot(cause="word30")
            if word in (26, 27) and w != 0:
                self.alog.log(self.t, "calib_trigger", word=word, value=w)
        # boot-push-detektion (PARITY §1.4)
        if sub == 1 and n == 30:
            self.boot_push_setup = True
            self.stats["boot_pushes"] += 1
            self.alog.log(self.t, "boot_push", block="sub1_len30")
        if sub == 112 and n == 3:
            self.boot_push_preinf = True
            self.stats["boot_pushes"] += 1
            self.alog.log(self.t, "boot_push", block="sub112_len3")
        # ACK 0x86 = eksakt echo, m. latency/loss-profil
        if source == "live":
            self._schedule_ack(sub, words, prev_write_t)
        return True

    def _schedule_ack(self, sub, words, prev_write_t=-1e9):
        p = self.profile
        if self.rng.random() < p.get("loss", 0.0):
            self.stats["acks_dropped"] += 1
            self.alog.log(self.t, "ack_dropped", sub=sub, len=len(words))
            return
        heavy = False
        for i, w in enumerate(words):
            word = sub + i
            if word == 3 and w == 1:
                heavy = True
            if word == 9 and w == 1 and sub <= 22 < sub + len(words) and words[22 - sub] == 1:
                heavy = True
        delay_ms = p["latency_ms"]
        if heavy and p.get("first_delay_ms") and self.t - prev_write_t > 2.0:
            delay_ms = p["first_delay_ms"]   # v3: første ACK ~250-280 ms ved grind/brew-start
        delay_ms += self.rng.uniform(-p.get("jitter_ms", 0), p.get("jitter_ms", 0))
        ack = build_frame(0x86, sub, words)
        self.send_tx(ack, max(0.001, delay_ms / 1000.0))
        self.stats["acks_sent"] += 1
        # v6.13: ACK-echo logges så scenarier kan asserte stop-writens ACK
        # byte for byte (sub+words = eksakt echo, jf. 0x99C6).
        self.alog.log(self.t, "ack", sub=sub, len=len(words), words=list(words))

    # ------------------------------------------------------------ øvrige cmds
    def handle_query(self, sub, n):
        if sub + n > REGN:
            self.alog.log(self.t, "read_rejected", sub=sub, len=n)
            self.log(f"AFVIST (03 error) sub{sub} len{n}")
            return
        self.stats["queries"] += 1
        self.send_tx(build_frame(0x83, sub, self.regs[sub:sub + n]), POLL_LATENCY)

    def handle_frame(self, cmd, sub, payload):
        if self.power_state == "off":
            # v6.14: dvale — status-tasken (RX-parser + svar) er slettet:
            # alt på UART'en ignoreres (polls, writes, cmd8). Tavs MCU.
            self.stats["dvale_rx_dropped"] += 1
            return
        if self.link == "silent":
            return
        if self.first_rx_t is None:
            self.first_rx_t = self.t
            self.alog.log(self.t, "link_connect")
        if cmd == 0x03:
            self.handle_query(sub, payload if isinstance(payload, int) else 0)
        elif cmd == 0x06:
            self.handle_write(sub, payload)
        elif cmd == 0x08:
            # v6.14: cmd8 = tid/alarm (10 bytes: år u16, måned 1-12, dag,
            # time, minut, sekund, alarm dag/time/minut). MCU'en (0x9C88)
            # gemmer dem og svarer 0x88-echo. Sleep-tasken vågner ved alarm
            # (dag+time+minut ≠ 0 og RTC = alarmen) — model: RTC'en går med
            # sim-tiden, og alarm-match i dvale → CPU-reset (_power_wake).
            p = payload
            self.rtc = {"y": p[0] | (p[1] << 8), "mo": p[2], "d": p[3],
                        "h": p[4], "mi": p[5], "s": p[6]}
            self.rtc_frac = 0.0
            ad, ah, am = p[7], p[8], p[9]
            self.alarm = (ad, ah, am) if (ad or ah or am) else None
            self.alarm_fired = False
            self.alog.log(self.t, "time_sync", payload=hexs(payload),
                          alarm=[ad, ah, am])
            body = bytes([0xAA, 0x55, 0x88]) + payload
            self.send_tx(body + struct.pack("<H", crc16_modbus(body)), 0.005)
        elif cmd == 0x09:
            self.version_received = True
            ver = payload.decode("ascii", "replace").rstrip("\x00")
            self.alog.log(self.t, "version_handshake", version=ver)
            self.log(f"versions-håndtryk OK: {ver}")
        else:
            self.alog.log(self.t, "unknown_cmd", cmd=cmd, sub=sub)

    # ------------------------------------------------------------ fase-maskine
    def _start_activity(self, kind):
        r = self.regs
        self.post = None              # ny aktivitet overtager efter-fasen
        if kind == "grind":
            target = r[4]
            # v6.6 (STEAMLIVE §8): live-stop ~187 ved mål 185 (target+2);
            # cur2 peak 1500 / mean 868 (live; v3: 1355/656 — inden for fast-tol)
            self.act = {"kind": kind, "t0": self.t, "target": target,
                        "stop_w": target + 2, "rate": 19.4}
            r[3] = 0                      # trigger konsumeret (BREW §5.1)
            r[38] = 0                     # tare
            r[33] = WS_GRIND
            self.mode = "grind"
        elif kind == "brew":
            target = r[23]
            pre = unpack113(r[113])[0] if unpack112(r[112])[0] else 0
            # v6.6 (STEAMLIVE §3/§7): vægt-ramp starter ~pre+2,75 s efter start,
            # rate ~18,6 enh/s, prædiktivt stop ~mål-8 (live 352/360, efter-dryp
            # +5..15). Bit9-pulsens placering er stokastisk (v3: el 1,14 s;
            # steam-live: el 4,97 s) — sim bruger v3-placeringen; verify filtrerer
            # korte pre-pulser fra ws-sekvensen (som idle-pulser, se _verify_report).
            self.act = {"kind": kind, "t0": self.t, "target": target,
                        "pre": float(pre), "stop_w": max(1, target - 8),
                        "rate": 18.6, "drip_t": pre + 2.75,
                        "bit9_pulse_done": pre < 2, "flush_overlap": False}
            r[9] = 0
            r[39] = 0xFFFd                # tare ~ -3 (pucken absorberer)
            r[33] = WS_BREW_PRE
            self.mode = "brew"
        elif kind == "water":
            target = r[17]
            manual = r[16] == 0 and r[19] == 1
            self.act = {"kind": kind, "t0": self.t, "target": target,
                        "manual": manual, "rate": 63.8,
                        "overshoot": self.rng.randint(0, 10)}
            r[9] = 0
            r[39] = 0
            r[33] = WS_WATER
            self.mode = "water"
        self.alog.log(self.t, "activity_start", activity=kind,
                      target=self.act.get("target"), work_sta=hex(r[33]))
        self.log(f"START {kind} (work_sta={r[33]:#06x})")

    def _start_steam(self, latch, src="display"):
        """v6.6: damp-kanalen (latch=1 display / latch=0 wand-flush)."""
        r = self.regs
        if self.steam_ch:
            # display-start oven i kørende flush: latch overtager (mælk fortsætter)
            self.steam_ch.update(latch=1, t0=self.t, milk0=self.ft[48],
                                 target=r[25], src="display")
        else:
            self.steam_ch = {"latch": latch, "t0": self.t, "milk0": self.ft[48],
                             "target": r[25] if latch else None, "src": src}
        self.steam_post = None        # ny damp overtager mælk-efter-fasen
        if self.act is None:
            self.mode = "steam"
        r[33] |= 0x4020
        self.alog.log(self.t, "activity_start", activity="steam",
                      target=r[25] if latch else None, work_sta=hex(r[33]),
                      latch=latch, src=src)
        self.log(f"START steam ({src}, latch={latch}, work_sta={r[33]:#06x})")

    def _stop_steam(self, reason):
        """v6.6: stop damp-kanalen. Auto-stop (latch=1, mælk≥mål) → MCU rydder
        selv ord24 ~0,18 s efter bits (live t+96,676→96,838) + termisk
        overshoot +39 enh (~11 s til peak, STEAMLIVE §5.2). Manuel/fejl-stop
        bevarer v6.5's lille deterministiske top. Flush (latch=0): kort
        boiler-efterslæb, mælken overlades til idle/verify-sync."""
        r = self.regs
        sc = self.steam_ch or {}
        auto = bool(sc.get("latch")) and "mælk" in reason
        r[33] &= ~0x4020              # primær-aktivitetens bits bevares (union)
        milk_stop = r[48]
        if sc.get("latch"):
            if auto:
                self.steam_clear_at = self.t + 0.18
                self.steam_post = {"t0": self.t, "auto": True,
                                   "milk_stop": milk_stop,
                                   "milk_peak": milk_stop + 39}
            else:
                r[24] = 0             # manuel/fejl: ryd straks (skrivningen/fejlen)
                self.steam_clear_at = None
                self.steam_post = {"t0": self.t, "auto": False,
                                   "milk_stop": milk_stop,
                                   "milk_peak": milk_stop + self.rng.randint(1, 8)}
        else:
            self.steam_post = {"t0": self.t, "flush": True}
        self.steam_ch = None
        self.mode = ("off" if r[0] == 0 else "sleep" if r[1] == 1 else
                     (self.act["kind"] if self.act else "idle"))
        self.alog.log(self.t, "activity_stop", activity="steam", reason=reason,
                      work_sta=hex(r[33]), weight=r[38],
                      latch=sc.get("latch"), milk=milk_stop)
        self.log(f"STOP steam ({reason}, work_sta={r[33]:#06x})")

    def _stop_activity(self, reason, manual=False):
        r = self.regs
        act = self.act or {}
        kind = act.get("kind", self.mode)
        if kind == "grind":
            r[33] = WS_IDLE
            # v6.13: MANUEL stop (ord6=0) standser kværnen øjeblikkeligt —
            # vægten holder op med at stige (w_end=w0). Auto-stop når allerede
            # stop_w=mål+2, så efter-fasen dér er flad; manuel stop må IKKE
            # rampe videre mod målet. Cur2-decay + ord6-clear er fælles.
            self.post = {"kind": "grind", "t0": self.t, "w0": r[38],
                         "w_end": r[38] if manual else
                         min(65535, act.get("target", r[38]) + 2),
                         "tare_clear_at": self.t + 0.4}
        elif kind == "brew":
            r[33] = WS_IDLE
            # v6.6: ord22 ryddes ~0,16 s efter stop (live 24,577→24,739 /
            # 245,851→246,007); efter-dryp +12 (live +5..+15). Post-stop
            # bit2-pulsen undertrykkes hvis flush overlappede brygen (live
            # bryg#2: pulsen (+179 ms, 90 ms) blev ikke samplet af sub33 —
            # ws-sekvensen i verify skal matche den OPTAGNE sekvens).
            self.post = {"kind": "brew", "t0": self.t, "w0": r[39],
                         "w_end": r[39] + 12, "pulse_until": self.t + 0.21,
                         "pulse": not act.get("flush_overlap"),
                         "flag_clear_at": self.t + 0.16}
        elif kind == "water":
            r[33] = WS_IDLE
            r[16] = 0                     # MCU rydder selv hotwater_auto
            r[19] = 0
            self.post = {"kind": "water", "t0": self.t, "w0": r[39],
                         "w_end": max(0, r[39] - 11)}
        if self.steam_ch:
            r[33] |= 0x4020           # damp kører videre oven i stoppet
        self.alog.log(self.t, "activity_stop", activity=kind, reason=reason,
                      work_sta=hex(r[33]), manual=manual,
                      weight=r[39] if kind in ("brew", "water") else r[38])
        self.log(f"STOP {kind} ({reason}, work_sta={r[33]:#06x})")
        self.act = None
        self.mode = ("off" if r[0] == 0 else "sleep" if r[1] == 1 else
                     ("steam" if self.steam_ch else "idle"))

    # ------------------------------------------------------------ v6.14: dvale/wake
    def _enter_dvale(self):
        """v6.14 (MEM03 sleep-task 0x2F52, UI_v6 §19): ord0=0 → work_sta=0,
        udgange 0, boiler_heat/head_heat/status/run slettes. Aktiviteter
        stoppes SIKKERT (ingen efter-fase — tasks'ene er væk), register-
        filen bevares, og UART'en er tavs (ingen svar, ingen 0x89)."""
        act_kind = self.act["kind"] if self.act else None
        had_steam = self.steam_ch is not None
        if act_kind:
            self.alog.log(self.t, "activity_stop", activity=act_kind,
                          reason="dvale (ord0=0)", work_sta=hex(0))
        if had_steam:
            self.alog.log(self.t, "activity_stop", activity="steam",
                          reason="dvale (ord0=0)", work_sta=hex(0))
        self.act = self.pending = self.post = None
        self.steam_ch = self.pending_steam = self.steam_clear_at = None
        self.steam_post = None
        self.pulse = self.small_pulse = None
        self.regs[33] = 0x0000
        self.regs[62] = 0                 # udgange 0 (0x2F0C): varme-effekt
        self.regs[63] = 0
        self.txq = []                     # intet mere på vej ud
        self.power_state = "off"
        self.dvale_at = None
        self.mode = "off"
        self.stats["dvale_entries"] += 1
        self.alog.log(self.t, "dvale_enter", word0=0, work_sta=0,
                      activity=act_kind, steam=had_steam)
        self.log("DVALE (ord0=0): tavs UART, work_sta=0, regs bevaret")

    def _power_wake(self, cause):
        """v6.14: wake fra dvale (knap-hold ≥0,8 s eller alarm) → CPU-reset
        (0x326) til FABRIKKENS startværdier — ikke config. Host'ens
        boot-push/re-push (0x89-runden) lægger config'en tilbage."""
        if self.power_state != "off":
            return
        self.stats["power_wakes"] += 1
        self.alog.log(self.t, "power_wake", cause=cause)
        self.do_reboot(factory=True, cause=cause)

    def _power_key_release(self):
        """PC.8 sluppet: holdt ≥0,8 s (8×100 ms) i dvale → wake/reset.
        Mens MCU'en er tændt, gør knappen intet i MCU'en (displayet ejer
        sluk-semantikken — UI_v6 §19.1)."""
        if not self.power_key_down:
            return
        held = self.t - self.power_key_t0
        self.power_key_down = False
        self.alog.log(self.t, "power_key", value=0, held_s=round(held, 3))
        if self.power_state == "off" and held >= POWER_KEY_HOLD_MIN:
            self._power_wake("power_key")

    def _tick_rtc(self, dt):
        """MCU'ens RTC (sat af cmd8) går med sim-tiden. Alarm-match i dvale
        (dag+time+minut ≠ 0) → wake/reset (0x2FE2–0x3036 → 0x326)."""
        if self.rtc is None:
            return
        self.rtc_frac += dt
        r = self.rtc
        while self.rtc_frac >= 1.0:
            self.rtc_frac -= 1.0
            r["s"] += 1
            if r["s"] >= 60:
                r["s"] = 0
                r["mi"] += 1
            if r["mi"] >= 60:
                r["mi"] = 0
                r["h"] += 1
            if r["h"] >= 24:
                r["h"] = 0
                r["d"] += 1
                if r["d"] > calendar.monthrange(r["y"], r["mo"])[1]:
                    r["d"] = 1
                    r["mo"] += 1
                    if r["mo"] > 12:
                        r["mo"] = 1
                        r["y"] += 1
        if (self.alarm and not self.alarm_fired and self.power_state == "off"
                and (r["d"], r["h"], r["mi"]) == self.alarm):
            self.alarm_fired = True
            self._power_wake("alarm")

    def do_reboot(self, factory=False, cause="ctrl"):
        self.stats["reboots"] += 1
        self.regs = [0] * REGN
        for w, v in MCU_DEFAULTS.items():
            self.regs[w] = v
        for w, v in SENSOR_BASELINE.items():
            self.regs[w] = v
        for w, v in serial_words().items():
            self.regs[w] = v
        defaults = None
        if factory:
            # v6.14 power-wake: CPU-reset til startværdierne (REGISTERMAP §2,
            # init 0x9966) — config'en er IKKE forudindlæst; host'ens
            # boot-push/re-push forventes (boot_push_missing ellers).
            self.regs[33] = WS_IDLE       # ord0=1, ord1=0 → vågen idle
            defaults = {str(w): self.regs[w] for w in (0, 4, 11, 14, 17, 23, 25, 114)}
        else:
            # kompat: ctrl reboot / ord30 forudindlæser config (som før)
            for w, v in config_to_words(self.cfg).items():
                self.regs[w] = v
            self.regs[33] = WS_SLEEP
        self.alog.log(self.t, "reboot", factory=factory, cause=cause,
                      defaults=defaults)
        self.ft = {w: float(self.regs[w]) for w in (40, 42, 47, 48, 68, 70)}
        self.ft_last = {w: self.regs[w] for w in self.ft}
        self.adc_base = {w: self.regs[w] for w in (49, 69, 71, 72)}
        self.wraw_base = {53: self.regs[53], 55: self.regs[55]}
        self.mode = "idle"
        self.act = self.pending = self.post = None
        self.steam_ch = self.pending_steam = self.steam_clear_at = None
        self.steam_post = None
        self.wand_state = bool(self.regs[44] & 1)
        self.steam_consumed = False
        self.version_received = False
        self.next_ver = self.t + 0.05
        self.boot_push_setup = self.boot_push_preinf = False
        # ny connect-runde: boot-push/versions-håndtryk forventes igen
        self.boot_checked = False
        self.first_rx_t = None
        self.last_awake_write = self.t
        self.keepalive_logged_at = -1e9
        # v6.14: reboot tager MCU'en ud af dvale (ord0 starter som 1)
        self.power_state = "on"
        self.dvale_at = None
        self.power_key_down = False
        self._key_up_at = None
        if factory:
            self.alarm = None             # alarmen lå i RAM (0x20000E18) — væk ved reset
            self.alarm_fired = False
        self.log(f"REBOOT ({cause}" + (", fabriks-startværdier)" if factory else ")"))

    def _tick_triggers(self):
        r = self.regs
        awake = r[0] == 1 and r[1] == 0
        if self.pending:
            if self.t >= self.pending["at"]:
                kind = self.pending["kind"]
                self.pending = None
                if self.mode in ("idle",) and awake:
                    self._start_activity(kind)
        # v6.6: display-steam (ord24=1) er en UAFHÆNGIG latch-vej — kræver IKKE
        # wand-kontakt (STEAMLIVE §5: live wand-bit0=0 under hele display-steam;
        # require_wand-gatingen var et verify-hul, nu fjernet). Kan starte oven i
        # en kørende primær aktivitet (bits 5+14 er disjunkte fra bryg/grind).
        if self.pending_steam:
            if self.t >= self.pending_steam["at"]:
                self.pending_steam = None
                if awake and not (r[34] or r[35] or r[36]):
                    self._start_steam(latch=1, src="display")
        # MCU's forsinkede ord24-nulstilling efter auto-stop (live ~188 ms)
        # — FØR trigger-checken, så den ikke genstarter dampen på ord24=1.
        if self.steam_clear_at is not None and self.t >= self.steam_clear_at:
            r[24] = 0
            self.steam_clear_at = None
            self.alog.log(self.t, "steam_ord24_clear", word=24)
        if (r[24] != 0 and self.pending_steam is None
                and (self.steam_ch is None or not self.steam_ch["latch"])
                and awake and not (r[34] or r[35] or r[36])
                and self.steam_clear_at is None):
            # ord24=1 under en kørende flush (latch=0) → display-latch overtager
            self.pending_steam = {"at": self.t + 0.30}
        # primære triggers (kun i idle — som UI'ets vagter)
        if self.mode not in ("idle",) or self.pending:
            return
        if not awake:
            return
        if r[3] != 0:
            self.pending = {"kind": "grind", "at": self.t + 0.39}
        elif r[9] != 0:
            if r[16] == 1 or r[19] == 1:
                self.pending = {"kind": "water", "at": self.t + 0.18}
            elif r[22] == 1:
                # v6.6: live write→bryg-bits ~0,17 s (STEAMLIVE §3: 218,956→
                # 219,123); v3's 0,39 s var for sent til steam-live's ws-timing.
                self.pending = {"kind": "brew", "at": self.t + 0.17}
            else:
                r[9] = 0                  # ren tare uden produkt-flag
                r[39] = 0

    def _sync_ft(self):
        """Synk float-akkumulatorer ved eksterne ændringer (inject/replay/
        verify-physync) — ikke ved vores egen støj (ellers random-walk)."""
        for w in self.ft:
            if self.regs[w] != self.ft_last.get(w):
                self.ft[w] = float(self.regs[w])

    @staticmethod
    def _milk_rate(temp):
        """v6.6 mælk-ramp (STEAMLIVE §5-kurven): midter-rate 14,0 enh/s
        (1,403 °C/s, bevaret fra v6.5); kold start ~9,8 enh/s (<150) og
        slut-fase ~12,6 enh/s (≥600) — lineær-fit fra live-kurven
        (116→155: 9,6; 223→500: 14,15; 571→646: 12,5)."""
        if temp < 150:
            return 9.8
        if temp < 250:
            return 9.8 + (temp - 150) * 0.042
        if temp < 600:
            return 14.0
        return 12.6

    def _tick_activity(self, dt):
        r = self.regs
        a = self.act
        if not a:
            return
        self._sync_ft()
        el = self.t - a["t0"]
        kind = a["kind"]
        # fejl → sikkerheds-stop (MCU stop-path, SESSION §5.2)
        if r[34] or r[35] or r[36]:
            self._stop_activity("fejl E1/E2/E3")
            return
        # v6.13 (SIM_HANDOFF/UI_v6 §18): MANUEL stop. Kontrol-tasks'ene
        # genberegner run-flagene hver 10 ms-omgang, og MCU'ens eget
        # auto-stop skriver præcis de samme ord til 0 — en manuel write
        # rammer derfor samme stop-vej (post/drip/decay):
        #   kværn: kører mens ord6≠0 (grind-ctl 0x19C6; ord3 er tare).
        #   bryg:  running=(ord22≠0) ved ændring (brew-ctl 0x1E9C/0x1F94).
        #   vand:  aktiv=(ord19|ord16)≠0 (hotwater-ctl 0x1B8C) — OGSÅ i
        #          auto-tilstand; stop ved aktuel vægt (ingen snap til mål).
        if kind == "grind" and r[6] == 0:
            self._stop_activity("manuel stop ord6=0", manual=True)
            return
        if kind == "brew" and r[22] == 0:
            self._stop_activity("manuel stop ord22=0")
            return
        if kind == "water" and r[16] == 0 and r[19] == 0:
            self._stop_activity("manuel stop ord16/19=0")
            return
        if kind == "grind":
            w = min(a["stop_w"], int(el * a["rate"]))
            r[38] = w & 0xFFFF
            r[53] = self.wraw_base[53] + w // 40
            # v6.6: cur2 live peak 1500 / mean 868 (STEAMLIVE §8; v3: 1355/656)
            r[58] = 1500 if el < 0.25 else 868 + self.rng.randint(-60, 60)
            r[33] = WS_GRIND
            if w >= a["stop_w"] or el > max(5, r[5]):
                self._stop_activity("vægt-mål" if w >= a["stop_w"] else "timeout ord5")
        elif kind == "brew":
            pre = a["pre"]
            if el < pre + 0.55:
                r[33] = WS_BREW_PRE
                if not a["bit9_pulse_done"] and 1.14 <= el <= 1.36:
                    r[33] |= 0x0200       # kort bit9-puls (BREW §5.2; stokastisk
                                          # placering — verify filtrerer, se §9)
                if el > 1.36:
                    a["bit9_pulse_done"] = True
                r[51] = 358 + self.rng.randint(-9, 9)
                r[52] = 360 + self.rng.randint(-20, 20)
                r[50] = 1533 + self.rng.randint(-40, 40)
                r[39] = 0xFFFd
            else:
                ee = el - (pre + 0.55)
                r[33] = WS_BREW_EXT
                if ee < 0.4:
                    r[51] = 1481
                    r[52] = 919
                else:
                    decay = min(1.0, (ee - 0.4) / 21.0)
                    r[51] = int(1379 - 54 * decay) + self.rng.randint(-15, 15)
                    r[52] = (637 if ee < 1.0 else
                             int(85 + 143 * min(1.0, ee / 21.0)) + self.rng.randint(-30, 30))
                r[50] = 1046 + self.rng.randint(-40, 40)
                if el >= a["drip_t"]:
                    x = el - a["drip_t"]
                    if a.get("quad"):
                        w0, b, ac = a["quad"]   # seedet pre-capture-bryg (fit)
                        w = int(w0 + b * x + ac * x * x)
                    else:
                        w = int(x * a["rate"])
                    r[39] = min(a["stop_w"], w) & 0xFFFF
                    r[55] = self.wraw_base[55] + w // 45
                    if r[39] >= a["stop_w"]:
                        self._stop_activity("vægt-mål (prædiktiv)")
                elif el > 180:
                    self._stop_activity("sikkerhed")
        elif kind == "water":
            r[33] = WS_WATER
            r[50] = 1556 if el < 0.15 else 1050 + self.rng.randint(-30, 30)
            r[52] = min(610, int(el * 1300)) + self.rng.randint(-15, 15)
            r[51] = 1300 + self.rng.randint(-20, 20)
            w = int(el * a["rate"])
            r[39] = w & 0xFFFF
            r[55] = self.wraw_base[55] + w // 50
            # v6.13: manuel stop (ord16/19=0) håndteres øverst i tick'en —
            # også i auto-tilstand — og stopper ved AKTUEL vægt (r[39]=w),
            # ikke ved målet. Auto-stoppet snapper til mål+overshoot som før.
            if w >= a["target"]:
                r[39] = (a["target"] + a["overshoot"]) & 0xFFFF
                self._stop_activity("vægt-mål ord39≥ord17")
            elif el > 180:
                self._stop_activity("sikkerhed")

    def _tick_steam(self, dt):
        """v6.6: damp-kanalen — kører uafhængigt af (og samtidigt med) primær
        aktivitet. To veje (STEAMLIVE §5-6):
          latch=1 display: mælk-ramp + AUTO-stop ved ord48≥ord25; cur1 ~7340,
            ord51 ~16650 (deadhead), ord52=0, s-boiler drop ~6,5→0,4 enh/s.
          latch=0 wand-flush: INGEN mælk-auto-stop (selv >650); stop ved
            wand-release; s-boiler drop ~11,6 enh/s (fri udluftning);
            cur1/ord51 ≈ bryg-niveau (live flush-under-bryg ~1024/~1387).
        ord46 = wand-kontakt 0/1 (STEAMLIVE §6.3-korrektionen af REGISTERMAP)."""
        r = self.regs
        sc = self.steam_ch
        if not sc:
            return
        self._sync_ft()
        el = self.t - sc["t0"]
        if r[0] == 0 or r[1] == 1:
            self._stop_steam("sleep/power-off")
            return
        if r[34] or r[35] or r[36]:
            self._stop_steam("fejl E1/E2/E3")
            return
        if self.act and self.act["kind"] == "brew":
            self.act["flush_overlap"] = True
        # mælk (fælles termisk model; flush uden mælk-kande varmes proben
        # hurtigere i live — verify-physync synker de få flush-frames)
        self.ft[48] += self._milk_rate(self.ft[48]) * dt
        r[48] = int(self.ft[48]) & 0xFFFF
        # s-boiler (ord47) + s-vandniveau (ord68)
        if sc["latch"]:
            if el < 2.5:
                pass                  # live: boiler holder ~1300 de første ~2,5 s
            elif el < 8.5:
                self.ft[47] = max(1243.0, self.ft[47] - 9.5 * dt)
            else:
                self.ft[47] = self._approach(self.ft[47], 1253, 0.4 * dt)
            if el < 1.5:                # flash-kogning ved start (live -230/s)
                self.ft[68] = max(800.0, self.ft[68] - 230.0 * dt)
            else:
                self.ft[68] = self._approach(self.ft[68], 858, 2.0 * dt)
            r[68] = max(0, int(self.ft[68]) + self.rng.randint(-12, 12))
        else:
            self.ft[47] = max(1240.0, self.ft[47] - 11.6 * dt)
            # live flush-træk: ~200 enh på ~1,5 s (fri udluftning, STEAMLIVE §7)
            self.ft[68] = max(1050.0, self.ft[68] - 130.0 * dt)
            r[68] = max(0, int(self.ft[68]) + self.rng.randint(-5, 5))
        r[47] = max(0, int(self.ft[47]) + self.rng.randint(-3, 3))
        self.steam_consumed = True
        # strøm/tryk/flow
        if sc["latch"]:
            if el < 5:
                r[50] = 54 + self.rng.randint(-10, 20)
                r[51] = 93 + self.rng.randint(-20, 40)
            else:
                r[50] = 7340 + self.rng.randint(-200, 300)   # live median 7340
                r[51] = 16650 + self.rng.randint(-180, 300)  # deadhead/max-drive
            r[52] = 0                   # damp genereres i kedlen, ingen vand-flow
        else:
            if self.act is None:
                r[50] = 1024 + self.rng.randint(-30, 30)
                r[51] = 1387 + self.rng.randint(-20, 20)
                r[52] = 0
            elif self.act["kind"] == "brew":
                r[51] += 85             # live: bryg-tryk ~1301→1387 under flush
        if not (self.act and self.act["kind"] == "grind"):
            r[58] = 5 + self.rng.randint(0, 1)   # kværn fra under damp
        r[46] = 1 if (r[44] & 1) else 0
        # v6.6: opdatér ft_last STRAKS for ord skrevet fra ft i dette tick —
        # ellers ser senere _sync_ft-kald vores egen støj/truncering som en
        # "ekstern ændring" (random-walk i boiler/niveau + tabt ramp-rate).
        for w in (47, 48, 68):
            self.ft_last[w] = r[w]
        # work_sta = union (primær-bits | damp-bits | vedligehold-pulser)
        r[33] |= 0x4020
        if self.pulse and self.t < self.pulse["until"]:
            r[33] |= self.pulse["bits"]
        # stop-betingelser pr. vej (latch-gating, STEAMLIVE §6.1)
        if sc["latch"]:
            if r[24] == 0:
                self._stop_steam("manuel stop ord24=0")
            elif r[35] & 0x10:
                self._stop_steam("E2 mælk-NTC")
            elif r[48] >= sc["target"]:
                self._stop_steam("mælk ≥ setpoint ord48≥ord25")
            elif el > max(5, r[15]):
                self._stop_steam("timeout ord15")
        else:
            if not (r[44] & 1):
                self._stop_steam("wand released")
            elif el > max(5, r[15]):
                self._stop_steam("timeout ord15")

    def _tick_post(self, dt):
        r = self.regs
        p = self.post
        if not p:
            return
        self._sync_ft()
        el = self.t - p["t0"]
        kind = p["kind"]
        if kind == "grind":
            r[38] = int(p["w0"] + (p["w_end"] - p["w0"]) * min(1.0, el / 1.0)) & 0xFFFF
            r[58] = max(5, int(727 * math.exp(-2.5 * el)))
            if self.t >= p.get("tare_clear_at", 1e9):
                r[6] = 0                  # ord6 konsumeres sent (BREW §5.1)
            if el > 2.5:
                self.post = None
        elif kind == "brew":
            if p.get("pulse", True) and el < 0.21:
                r[33] = 0x0105            # post-stop bit2-puls (BREW §5.2)
            else:
                r[33] = WS_IDLE
            if self.steam_ch:
                r[33] |= 0x4020           # damp kører videre oven i bryg-stop
            if self.t >= p.get("flag_clear_at", 0):
                r[22] = 0                 # MCU rydder bryg-flag ~0,16 s efter stop
            r[39] = int(p["w0"] + (p["w_end"] - p["w0"]) * min(1.0, el / 1.6)) & 0xFFFF
            if not self.steam_ch:         # damp-kanalen ejer 50/51/52 hvis aktiv
                r[51] = int(1300 * math.exp(-3.0 * el)) if el < 1.5 else self.rng.randint(0, 12)
                if 0.4 < el < 2.2 and self.rng.random() < 0.06:
                    r[51] = self.rng.choice([4876, 10385, 15428])   # deadhead-spikes
                r[52] = 0
                r[50] = max(5, int(1046 * math.exp(-2.2 * el)))
            if el > 3.0:
                self.post = None
        elif kind == "water":
            r[39] = int(p["w0"] + (p["w_end"] - p["w0"]) * min(1.0, el / 2.0)) & 0xFFFF
            r[50] = max(5, int(1050 * math.exp(-2.2 * el)))
            r[52] = max(0, int(610 * max(0.0, 1.0 - el / 1.7)))
            r[51] = self.rng.randint(0, 8) if el > 0.5 else int(1300 * math.exp(-3.0 * el))
            if el > 2.5:
                self.post = None

    def _tick_steam_post(self, dt):
        """v6.6: damp-efter-fasen — UAFHÆNGIG af primære aktiviteter (live:
        mælken køler videre gennem grind/bryg, STEAMLIVE §5.2/§3). Ejer ord48
        (mælk) og ord47 (boiler-recovery); strøm/tryk/flow kun når ingen primær
        aktivitet/efter-fase kører (ellers ejer de dem)."""
        sp = self.steam_post
        if not sp:
            return
        self._sync_ft()
        r = self.regs
        el = self.t - sp["t0"]
        if sp.get("flush"):
            # wand-flush release: s-boiler drop ~11,6 enh/s fortsætter ~0,9 s
            # (termisk efterslæb, live 1259→1249), derefter recovery ~3 enh/s.
            if el < 0.9:
                self.ft[47] = max(1240.0, self.ft[47] - 11.6 * dt)
            else:
                self.ft[47] = self._approach(self.ft[47], 1300, 3.0 * dt)
            r[47] = max(0, int(self.ft[47]) + self.rng.randint(-3, 3))
            self.ft_last[47] = r[47]
            if not self.act and not self.post:
                r[50] = 5 + self.rng.randint(0, 1)
                r[51] = self.rng.randint(0, 1)
                r[52] = 0
            if el > 2.5:
                self.steam_post = None
            return
        r[46] = 1 if (r[44] & 1) else 0
        if not (self.act and self.act["kind"] == "grind"):
            r[58] = 5 + self.rng.randint(0, 1)
        if sp.get("auto"):
            # v6.6 AUTO-stop-overshoot (STEAMLIVE §5.2, live-målt +3,9 °C):
            # mælken fortsætter OPAD i ~11 s efter stop (termisk inertion i
            # probe/kande): eksponentiel approach mod peak = stop+39
            # (tau 3,2 s → ~97 % ved 11 s), holder toppen ~40 s (live
            # 689-690 i t+108..158), køler derefter ~0,3/0,52 enh/s.
            if el < 11.5:
                self.ft[48] += (sp["milk_peak"] - self.ft[48]) * (dt / 3.2)
                r[48] = int(self.ft[48]) & 0xFFFF
            elif el < 50:
                r[48] = int(self.ft[48] + self.rng.randint(-1, 1)) & 0xFFFF
            else:
                cool = 0.3 if el < 130 else 0.52
                self.ft[48] = max(230.0, self.ft[48] - cool * dt)
                r[48] = int(self.ft[48]) & 0xFFFF
            # s-boiler recovers til ~1313 (live 1249→1313 på ~11 s),
            # decay ~1,1 enh/s til ~1296 (live 107→125 s), derefter PID
            if el < 12:
                self.ft[47] = self._approach(self.ft[47], 1313, 5.8 * dt)
            elif el < 30:
                self.ft[47] = self._approach(self.ft[47], 1296, 1.1 * dt)
            else:
                self.ft[47] = self._approach(self.ft[47], 1300, 0.3 * dt)
            r[47] = max(0, int(self.ft[47]) + self.rng.randint(-3, 3))
            # cur1: ~7300 i ~3 s, derefter oscillerende decay (kedel-refill).
            # Efter ~19 s overtager idle-pulserne (live: vedligehold-cyklusser
            # fortsætter i resten af vinduet).
            if not self.act and not self.post and el < 19:
                if el < 3:
                    r[50] = 7300 + self.rng.randint(-150, 150)
                    r[51] = 16570 + self.rng.randint(-120, 120)
                else:
                    r[50] = max(5, int(7300 * math.exp(-(el - 3) / 3.5)))
                    if self.rng.random() < 0.5:
                        r[50] = self.rng.randint(800, 5500)
                    r[51] = self.rng.choice([0, 0, self.rng.randint(3000, 16670)])
                r[52] = 0
            if el > 50 and self.ft[48] <= 240:
                self.steam_post = None
        else:
            # v6.5 Manuel/fejl-stop: DETERMINISTISK lille top (+1..8),
            # holdes ≥0,15 s (≥ ét poll-interval) før 0,5 enh/s-afkøling
            # (steam_auto-flake-fixet, uændret for bagudkompatibilitet).
            r[48] = int(sp["milk_peak"] - max(0.0, el - 0.15) * 0.5) & 0xFFFF
            if not self.act and not self.post:
                r[50] = 5 + self.rng.randint(0, 1)
                r[51] = self.rng.randint(0, 1)
                r[52] = 0
            if el > 4.0:
                self.steam_post = None
        # ft_last straks (truncerings/random-walk-værn, som _tick_steam)
        for w in (47, 48):
            self.ft_last[w] = r[w]

    def _tick_idle_sensors(self, dt):
        r = self.regs
        self._sync_ft()
        awake = r[0] == 1 and r[1] == 0
        # v6.6: damp-kanalen er uafhængig af primær aktivitet
        steam_act = self.steam_ch is not None
        # v6.5-scenarios: under efter-damp (post) ejer _tick_post ord48 —
        # idle-støjen (±1) må ikke overskrive toppen (v6.4-flakens andet led).
        steam_post = self.steam_post is not None
        flush_post = bool(self.steam_post and self.steam_post.get("flush"))
        # boiler-vedligehold (awake) / køling (sleep/drain/off)
        if awake:
            self.ft[40] = self._approach(self.ft[40], 997, 5.0 * dt)
            r[40] = int(self.ft[40]) + self.rng.randint(-3, 3)
            if not steam_act and not steam_post:
                self.ft[47] = self._approach(self.ft[47], 1300, 5.0 * dt)
                r[47] = int(self.ft[47]) + self.rng.randint(-3, 3)
            r[62] = 100
            r[63] = 100
        else:
            # SESSION §5.1: s-boiler 102,5→94,3 °C / 729 s = 0,1125 enh/s;
            # e-boiler 85,2→80,1 °C / 729 s = 0,07 enh/s (varme fra, drain)
            self.ft[47] = max(200.0, self.ft[47] - 0.1125 * dt)
            self.ft[40] = max(200.0, self.ft[40] - 0.07 * dt)
            r[47] = int(self.ft[47])
            r[40] = int(self.ft[40])
        r[40] = max(0, min(65535, r[40]))
        r[41] = r[40]                     # spejl altid identisk (live-bevist)
        r[47] = max(0, min(65535, r[47]))
        # e-head (ord42): v6.6 (STEAMLIVE §9): kold head (<600) varmes op mod
        # ligevægt ~585 med tau ~155 s (live 467→583 over 729 s); varm head
        # PID-holder omkring setpoint (v3-adfærd, uændret).
        head_target = r[114] * 10 if awake else 230
        if awake and self.ft[42] < 600:
            self.ft[42] += (580 - self.ft[42]) * (dt / 155.0)
        else:
            self.ft[42] = self._approach(self.ft[42], head_target,
                                         (0.03 if awake else 0.05) * dt)
        r[42] = int(self.ft[42]) + self.rng.randint(-2, 2)
        r[128] = r[42]
        if not steam_act and not (steam_post and not flush_post):
            self.ft[48] = self._approach(self.ft[48], 230, 0.05 * dt)
            r[48] = max(0, int(self.ft[48]) + self.rng.randint(-1, 1))
        # vandniveauer (v6.6, STEAMLIVE §9): ord68 (s-boiler) flash-koger under
        # damp (ejes af _tick_steam) og refilles derefter mod ~1345 (hurtigt til
        # ~1270, langsomt videre); lavt niveau (<1200) stiger ~0,35 enh/s.
        # ord70 (e-boiler) refilles mod ~910 med tau ~280 s fra lavt niveau.
        # Sleep: langsom stigning (session-capture: +0,055/+0,06 enh/s).
        if awake:
            if not steam_act:         # damp-kanalen ejer ord68 mens den kører
                if self.steam_consumed and self.ft[68] < 1340:
                    # live refill: ~20 enh/s op til ~1080, derefter eksponentiel
                    # approach mod ~1290 (k 0,05), til sidst langsomt mod ~1345
                    if self.ft[68] < 1080:
                        self.ft[68] += 20.0 * dt
                    elif self.ft[68] < 1285:
                        self.ft[68] = self._approach(
                            self.ft[68], 1290,
                            max(0.5, (1290 - self.ft[68]) * 0.05) * dt)
                    else:
                        self.ft[68] = self._approach(
                            self.ft[68], 1345,
                            max(0.15, (1345 - self.ft[68]) * 0.004) * dt)
                elif self.ft[68] < 1200:
                    self.ft[68] += 0.35 * dt
                r[68] = int(self.ft[68]) + self.rng.randint(-3, 3)
            if self.ft[70] < 900:
                self.ft[70] += (910 - self.ft[70]) * (dt / 280.0)
            r[70] = int(self.ft[70]) + self.rng.randint(-3, 3)
        else:
            self.ft[68] += 0.055 * dt
            self.ft[70] += 0.06 * dt
            r[68] = int(self.ft[68])
            r[70] = int(self.ft[70])
        for w in self.ft:
            self.ft_last[w] = r[w]
        # strøm/flow/tryk-baseline (damp-efter-fasens cur-decay ejer dem i ~19 s)
        sp_cur = bool(self.steam_post and not self.steam_post.get("flush")
                      and (self.t - self.steam_post["t0"]) < 19)
        if not self.act and not self.post and not steam_act and not sp_cur:
            r[50] = 5 + self.rng.randint(0, 1)
            r[58] = 5 + self.rng.randint(0, 1)
            r[52] = 0
            r[51] = self.rng.randint(0, 1)
        # net/ADC-støj omkring SEED-værdier (ikke-akkumulerende)
        r[49] = self.adc_base[49] + self.rng.randint(-3, 1)
        r[61] = 50
        r[69] = self.adc_base[69] + self.rng.randint(-1, 1)
        r[71] = self.adc_base[71] + self.rng.randint(-2, 2)
        r[72] = self.adc_base[72] + self.rng.randint(-2, 2)
        # rå vægt-celler følger kalibrerede vægte svagt (BREW §8; live /45)
        r[53] = self.wraw_base[53] + max(0, s16(r[38])) // 40 + self.rng.randint(-1, 1)
        r[55] = self.wraw_base[55] + max(0, s16(r[39])) // 45 + self.rng.randint(-1, 1)
        # work_sta i idle/sleep/off + stokastiske vedligehold-pulser.
        # v6.6 (STEAMLIVE §9.3 pkt. 12): pulserne (bits 10/11/13) kører også
        # UNDER damp (live 0x6121/0x4521) — her overlægges kun ws-bitsene;
        # damp-kanalen/efter-fasen ejer stadig strøm/tryk.
        if self.act or self.post:
            return
        if r[0] == 0:
            r[33] = 0x0000                # sleep-task (word0=0)
            self.mode = "off"
        elif r[1] == 1:
            r[33] = WS_SLEEP              # drain-task
            self.mode = "sleep"
        else:
            self.mode = "steam" if steam_act else "idle"
            base = WS_IDLE | (0x4020 if steam_act else 0)
            if self.pulse and self.t < self.pulse["until"]:
                r[33] = base | self.pulse["bits"]
                if not steam_act and not sp_cur:
                    r[50] = int(self.pulse["cur"] * self.calm)
                    r[51] = int(self.pulse["p"] * self.calm)
            else:
                self.pulse = None
                r[33] = base
                if self.t >= self.next_pulse and self.maint:
                    bits = self.rng.choice([0x0400, 0x0800, 0x2000, 0x0C00, 0x2800])
                    self.pulse = {"until": self.t + self.rng.uniform(0.10, 0.25),
                                  "bits": bits, "cur": self.rng.randint(800, 4900),
                                  "p": self.rng.randint(3000, 16670)}
                    self.next_pulse = self.t + self.rng.uniform(3.0, 9.0)
            # v6.6: små vedligehold-varmecyklusser (cur1 400-1000, ord51
            # 400-900 i ~60 % af cyklusserne) — UDEN ws-bits (live 0x0101).
            if not steam_act and not sp_cur:
                if self.small_pulse and self.t < self.small_pulse["until"]:
                    if not (self.pulse and self.t < self.pulse["until"]):
                        r[50] = int(self.small_pulse["cur"] * self.calm)
                        if self.small_pulse["p"]:
                            r[51] = int(self.small_pulse["p"] * self.calm)
                else:
                    self.small_pulse = None
                    if self.t >= self.next_small and self.maint:
                        self.small_pulse = {
                            "until": self.t + self.rng.uniform(0.5, 1.5),
                            "cur": self.rng.randint(400, 1000),
                            "p": (self.rng.randint(400, 900)
                                  if self.rng.random() < 0.6 else 0)}
                        self.next_small = self.t + self.rng.uniform(1.5, 4.0)

    @staticmethod
    def _approach(cur, target, step):
        if cur < target:
            return min(target, cur + step)
        return max(target, cur - step)

    def _tick_faults(self):
        for w in (34, 35, 36):
            if w in self.faults:
                val, until = self.faults[w]
                if until is not None and self.t >= until:
                    del self.faults[w]
                    self.regs[w] = 0
                    self.alog.log(self.t, "fault_clear", word=w)
                else:
                    self.regs[w] = val
            elif self.regs[w] and w not in self.faults:
                pass                      # naturlige fejl (ikke injectet) beholdes

    def _tick_keepalive(self):
        if self.power_state == "off":
            return                        # v6.14: keep-awake pauserer i dvale
        if not self.expect_keepawake or self.first_rx_t is None:
            return
        gap = self.t - self.last_awake_write
        # klientens keep-awake-periode er wall-clock (30 s); ved play-hastighed
        # >1× svarer det til 30*speed sim-sekunder (ved 1× uændret adfærd)
        kmax = KEEPALIVE_MAX * max(1.0, self.speed)
        if gap > kmax + 0.5 and self.t - self.keepalive_logged_at > kmax:
            self.keepalive_logged_at = self.t
            self.alog.log(self.t, "keepalive_gap", gap_s=round(gap, 2),
                          expected_max_s=KEEPALIVE_MAX)

    def _tick_boot_push_check(self):
        if self.power_state == "off":
            return                        # v6.14: ingen RX forventes i dvale
        if self.first_rx_t is None or self.replay or self.boot_checked:
            return
        if self.t - self.first_rx_t > BOOT_PUSH_WINDOW:
            if not self.boot_push_setup:
                self.alog.log(self.t, "boot_push_missing", block="sub1_len30")
            if not self.boot_push_preinf:
                self.alog.log(self.t, "boot_push_missing", block="sub112_len3")
            self.boot_checked = True      # kun én check pr. connect

    # ------------------------------------------------------------ replay-motor
    def load_replay(self, csv_path, mode="serve", speed=1.0):
        frames = []
        t0 = None
        opener = gzip.open if str(csv_path).endswith(".gz") else open
        with opener(csv_path, "rt", newline="") as f:
            for row in csv.DictReader(f):
                raw = bytes.fromhex(row["raw_hex"].replace(" ", ""))
                parsed = list(FrameParser().feed(raw))
                if not parsed:
                    continue
                cmd, sub, payload, _ = parsed[0]
                hh, mm, ss = row["ts"].split(":")
                ts = int(hh) * 3600 + int(mm) * 60 + float(ss)
                if t0 is None:
                    t0 = ts
                direction = "tx" if row["direction"].startswith("display") else "rx"
                if cmd in (0x08, 0x88, 0x09, 0x89):
                    continue              # ikke register-data
                words = payload if isinstance(payload, list) else []
                frames.append({"t": ts - t0, "dir": direction, "cmd": cmd,
                               "sub": sub, "words": words})
        self.replay = {"frames": frames, "path": str(csv_path),
                       "duration": frames[-1]["t"] if frames else 0}
        self.rp_mode = mode
        self.rp_speed = speed
        self.rp_t = 0.0
        self.rp_idx = 0
        # seed: første RX-svar pr. bånd = start-sandhed
        seen = set()
        for fr in frames:
            if fr["dir"] == "rx" and fr["cmd"] == 0x83:
                key = (fr["sub"], len(fr["words"]))
                if key not in seen:
                    seen.add(key)
                    self.regs[fr["sub"]:fr["sub"] + len(fr["words"])] = fr["words"]
        # serve: versions-håndtryk køres stadig (MCU "genstartet"); verify: ingen pty
        self.version_received = (mode == "verify")
        self.next_ver = 0.0
        self.ft = {w: float(self.regs[w]) for w in (40, 42, 47, 48, 68, 70)}
        self.ft_last = {w: self.regs[w] for w in self.ft}
        self.adc_base = {w: self.regs[w] for w in (49, 69, 71, 72)}
        self.wraw_base = {53: self.regs[53], 55: self.regs[55]}
        self.mode = "sleep" if self.regs[1] == 1 else "idle"
        self.act = self.pending = self.post = None
        self.steam_ch = self.pending_steam = self.steam_clear_at = None
        self.steam_post = None
        self.wand_state = bool(self.regs[44] & 1)
        self.steam_consumed = False
        if mode == "verify":
            self._seed_precapture_brew()
            self._verify_init()
        self.alog.log(0, "replay_load", path=str(csv_path), mode=mode,
                      frames=len(frames), duration=self.replay["duration"])
        self.log(f"REPLAY {mode}: {len(frames)} frames, {self.replay['duration']:.1f} s")

    def _seed_precapture_brew(self):
        """v6.6 (STEAMLIVE §9.3): starter optagelsen MIDT i en bryg (start-write
        før vinduet — et replay-artefakt), seed'es bryg-aktiviteten ud fra de
        observerede dynamikker: pre→ext-tidspunktet giver t0; vægt-rampens
        start/slut giver rate/drip_t/stop_w. Bit9-pulsen ligger før vinduet
        (usynlig i optagelsen) og undertrykkes. Kun verify-mode."""
        r = self.regs
        ws = r[33] & ~WS_PULSE_MASK
        if (ws & 0x0006) != 0x0006 or ws & 0x4060:
            return
        frames = self.replay["frames"]
        pre = unpack113(r[113])[0] if unpack112(r[112])[0] else 0
        t_ext = t_stop = t_ramp = None
        w_stop = w_ramp = None
        for fr in frames:
            if fr["dir"] != "rx" or fr["cmd"] != 0x83:
                continue
            if fr["sub"] <= 33 < fr["sub"] + len(fr["words"]):
                m = fr["words"][33 - fr["sub"]] & ~WS_PULSE_MASK
                if t_ext is None and m == WS_BREW_EXT:
                    t_ext = fr["t"]
                if t_ext is not None and t_stop is None and m in (WS_IDLE, 0x0105):
                    t_stop = fr["t"]
            if fr["sub"] <= 39 < fr["sub"] + len(fr["words"]):
                w39 = s16(fr["words"][39 - fr["sub"]])
                if t_stop is not None and w_stop is None and abs(fr["t"] - t_stop) < 0.5:
                    w_stop = w39
                if t_ext is not None and t_ramp is None and w39 > 10:
                    t_ramp, w_ramp = fr["t"], w39
        if t_ext is None:
            t_ext = frames[0]["t"] if frames else 0.0
        t0 = t_ext - (pre + 0.55)
        target = r[23]
        if (t_stop is not None and t_ramp is not None and w_stop is not None
                and w_stop > 10 and t_stop > t_ramp):
            rate = max(5.0, (w_stop - (w_ramp or 0)) / (t_stop - t_ramp))
            stop_w = w_stop
            # Live vægt-ramps AKCELERERER (flow stiger gennem ekstraktionen):
            # fit en kvadratisk w(x) = w0 + b·x + a·x² gennem ramp-start,
            # midtpunkt og stop (x = tid siden ramp-start). Lineær fallback.
            t_mid = (t_ramp + t_stop) / 2
            w_mid, best = None, 1e9
            for fr in frames:
                if (fr["dir"] == "rx" and fr["cmd"] == 0x83
                        and fr["sub"] <= 39 < fr["sub"] + len(fr["words"])):
                    d = abs(fr["t"] - t_mid)
                    if d < best:
                        best, w_mid = d, s16(fr["words"][39 - fr["sub"]])
            quad = None
            x3 = t_stop - t_ramp
            x2 = t_mid - t_ramp
            if w_mid is not None and 0 < x2 < x3:
                d2 = w_mid - (w_ramp or 0)
                d3 = w_stop - (w_ramp or 0)
                ac = (d3 - d2 * x3 / x2) / (x3 * (x3 - x2))
                b = (d2 - ac * x2 * x2) / x2
                if ac >= 0 and b > 0:
                    quad = ((w_ramp or 0), b, ac)
            if quad:
                drip_t = t_ramp - t0      # kvadraten starter ved w_ramp
            else:
                # rampen starter ved w=0 i sim'en — kompensér for fit-interceptet
                drip_t = t_ramp - t0 - (w_ramp or 0) / rate
        else:
            rate, stop_w, drip_t = 18.6, max(1, target - 8), pre + 2.75
            quad = None
        self.act = {"kind": "brew", "t0": t0, "target": target, "pre": float(pre),
                    "stop_w": stop_w, "rate": rate, "drip_t": drip_t,
                    "quad": quad,
                    "bit9_pulse_done": True, "flush_overlap": False, "seeded": True}
        self.mode = "brew"
        self.alog.log(0, "replay_seed_brew", t0=round(t0, 3), rate=round(rate, 2),
                      stop_w=stop_w, drip_t=round(drip_t, 2))
        self.log(f"SEED bryg (start før vinduet): t0={t0:.2f} rate={rate:.1f} "
                 f"stop_w={stop_w} drip_t={drip_t:.2f}")

    def _replay_advance(self, dt):
        if not self.replay:
            return
        if self.rp_mode == "serve":
            if self.rp_hold:
                return
            self.rp_t += dt * self.rp_speed
            frames = self.replay["frames"]
            while self.rp_idx < len(frames) and frames[self.rp_idx]["t"] <= self.rp_t:
                fr = frames[self.rp_idx]
                self.rp_idx += 1
                if fr["dir"] == "rx" and fr["cmd"] == 0x83:
                    self.regs[fr["sub"]:fr["sub"] + len(fr["words"])] = fr["words"]
                elif fr["dir"] == "tx" and fr["cmd"] == 0x06:
                    self.handle_write(fr["sub"], fr["words"], source="replay")
            if self.rp_idx >= len(frames) and not self.replay.get("ended"):
                self.replay["ended"] = True
                self.alog.log(self.t, "replay_end", rp_t=self.rp_t)

    # ---- verify: frame-for-frame sammenligning mod optaget sandhed ----
    # Tolerance-klasser (dokumenteret i README.md):
    #   exact  : setup/flag/statisk — skal matche præcist
    #   event  : trigger/work_sta — tidsforskudt match ±0,75 s tilladt
    #   sensor : temperaturer/ADC — |Δ| ≤ 15; v6.6: fysiske hændelser synkes
    #            (ord48 mælk-påfyldning/probe: rec<sim-30 el. dev>60; ord68
    #            vandforbrug: dev>60) — som weight-physync
    #   weight : vægte — |Δ| ≤ 30 eller tidsforskudt; fysiske spring synkes
    #            (v6.6: >150 for kalibrerede, >40 for rå celler + base-justering)
    #   fast   : strøm/flow/tryk — |Δ| ≤ max(400, 35 %) eller tidsforskudt ±15 %;
    #            stokastiske idle-vedligeholdscyklusser tåles på begge sider
    #            (min<200 vs max>200 — små 400-1000 og store >1000, alle captures)
    # ws-sekvens: korte bit9-pre-pulser filtreres på begge sider (stokastisk
    # placering, v3 vs. steam-live — se _filter_bit9_pulses).
    EXACT_WORDS = set([0, 1, 2, 4, 5, 7, 8, 10, 12, 13, 14, 15, 17, 18, 19, 20,
                       21, 23, 25, 26, 27, 28, 29, 30, 31, 32, 34, 35, 36, 37,
                       43, 44, 45, 46, 54, 56, 57, 61, 62, 63, 66, 67, 73]
                      + list(range(59, 61)) + list(range(64, 66))
                      + list(range(74, 112)) + [112, 113, 114, 128])
    EVENT_WORDS = {3, 6, 9, 16, 22, 24, 33}
    SENSOR_WORDS = {40, 41, 42, 47, 48, 49, 68, 69, 70, 71, 72}
    WEIGHT_WORDS = {38, 39, 53, 55}
    FAST_WORDS = {50, 51, 52, 58}
    TIMESHIFT = 0.75

    def _verify_init(self):
        frames = self.replay["frames"]
        self.rp_verify = {
            "words": {}, "n": 0, "ok": 0, "exact_fail": 0,
            "physical_sync": 0, "spike_ok": 0, "timeshift_ok": 0,
            "transitions_rec": [], "transitions_sim": [],
            "last_ws_rec": None, "last_ws_sim": None,
            "events": [],
        }
        # optagne værdi-tidslinjer pr. ord (til tidsforskudt match);
        # work_sta maskeres for stokastiske idle-pulser (bits 10/11/13)
        self.rp_timelines = {}
        for fr in frames:
            if fr["dir"] == "rx" and fr["cmd"] == 0x83:
                for i, w in enumerate(fr["words"]):
                    word = fr["sub"] + i
                    if word == 33:
                        w &= ~WS_PULSE_MASK
                    tl = self.rp_timelines.setdefault(word, [])
                    if not tl or tl[-1][1] != w:
                        tl.append((fr["t"], w))
        self.rp_frame_idx = 0

    def _verify_step(self):
        """Kaldes i verify-mode: advance sim til næste optagne frame og sammenlign."""
        frames = self.replay["frames"]
        v = self.rp_verify
        while self.rp_frame_idx < len(frames):
            fr = frames[self.rp_frame_idx]
            self.advance(fr["t"])
            self.rp_frame_idx += 1
            if fr["dir"] == "tx" and fr["cmd"] == 0x06:
                self.handle_write(fr["sub"], fr["words"], source="replay")
                v["events"].append({"t": fr["t"], "kind": "write", "sub": fr["sub"],
                                    "words": fr["words"]})
            elif fr["dir"] == "rx" and fr["cmd"] == 0x83:
                # v6.6: wand-kontakten er en FYSISK input (ingen UART-write,
                # STEAMLIVE §6) — i verify rekonstrueres den fra optagelsens
                # ord44 bit0, så flush-modellen testes autonomt mod sandheden.
                if fr["sub"] <= 44 < fr["sub"] + len(fr["words"]):
                    rec_wand = bool(fr["words"][44 - fr["sub"]] & 1)
                    if rec_wand != self.wand_state:
                        self.inject({"type": "wand", "on": rec_wand})
                ws_rec = fr["words"][0] if fr["sub"] == 33 else None
                if ws_rec is not None:
                    m = ws_rec & ~WS_PULSE_MASK
                    if m != v["last_ws_rec"]:
                        v["transitions_rec"].append((fr["t"], m))
                        v["last_ws_rec"] = m
                ws_sim = self.regs[33] & ~WS_PULSE_MASK
                if ws_sim != v["last_ws_sim"]:
                    v["transitions_sim"].append((self.t, ws_sim))
                    v["last_ws_sim"] = ws_sim
                for i, rec in enumerate(fr["words"]):
                    word = fr["sub"] + i
                    sim = self.regs[word]
                    self._verify_word(word, fr["t"], sim, rec)
        return self._verify_report()

    def _verify_word(self, word, t, sim, rec):
        v = self.rp_verify
        st = v["words"].setdefault(word, {"n": 0, "exact": 0, "tol": 0,
                                          "timeshift": 0, "spike": 0,
                                          "physync": 0, "fail": 0, "max_dev": 0})
        st["n"] += 1
        v["n"] += 1
        if word == 33:
            sim &= ~WS_PULSE_MASK
            rec &= ~WS_PULSE_MASK
        dev = abs(s16(sim) - s16(rec))
        st["max_dev"] = max(st["max_dev"], dev)
        if sim == rec:
            st["exact"] += 1
            v["ok"] += 1
            return
        cls = ("exact" if word in self.EXACT_WORDS else
               "event" if word in self.EVENT_WORDS else
               "sensor" if word in self.SENSOR_WORDS else
               "weight" if word in self.WEIGHT_WORDS else
               "fast" if word in self.FAST_WORDS else "exact")
        if cls == "exact":
            st["fail"] += 1
            v["exact_fail"] += 1
            return
        if cls == "sensor":
            if dev <= 15:
                st["tol"] += 1
                v["ok"] += 1
                return
            # v6.6 fysiske sensor-hændelser synkes fra sandheden (som weights):
            # ord48: kold mælk hældt i / probe flyttet (rec < sim-20) eller
            # damp-jet direkte på proben under flush (dev > 60). ord68:
            # vandforbrug/refill-hændelser (dev > 40).
            if word == 48 and (dev > 60 or s16(rec) < s16(sim) - 20):
                self.regs[word] = rec
                st["physync"] += 1
                v["physical_sync"] += 1
                v["ok"] += 1
                return
            if word == 68 and dev > 40:
                self.regs[word] = rec
                st["physync"] += 1
                v["physical_sync"] += 1
                v["ok"] += 1
                return
        if cls == "weight":
            if dev <= 30:
                st["tol"] += 1
                v["ok"] += 1
                return
            # v6.6: kop fjernet / tare-glitch = spring >100 (rå celler >40);
            # synk fra sandheden OG justér rå-celle-basen så formlen følger med.
            # (100 ikke 150: en kop-fjernelse kan lande på en mellemværdi hvis
            # afstand til slutværdien er <150 — så "hænger" sim'en fast der.)
            th = 40 if word in (53, 55) else 100
            if dev > th:
                self.regs[word] = rec
                if word == 55:
                    self.wraw_base[55] = rec - max(0, s16(self.regs[39])) // 45
                elif word == 53:
                    self.wraw_base[53] = rec - max(0, s16(self.regs[38])) // 40
                elif word == 39:
                    rec55 = self._tl_value(55, t)
                    if rec55 is not None:
                        self.wraw_base[55] = rec55 - max(0, s16(rec)) // 45
                elif word == 38:
                    rec53 = self._tl_value(53, t)
                    if rec53 is not None:
                        self.wraw_base[53] = rec53 - max(0, s16(rec)) // 40
                st["physync"] += 1
                v["physical_sync"] += 1
                v["ok"] += 1
                return
        if cls == "fast":
            ws_m = self.regs[33] & ~WS_PULSE_MASK
            idle = ws_m in (WS_IDLE, 0x0105) and not self.act
            # v6.6: stokastiske idle-vedligeholdscyklusser tåles på begge sider:
            # live cur1 400-1000 / ord51 400-900 i ~30-45 % af idle-frames
            # (ALLE captures) — fasen kan ikke matches deterministisk. Samme
            # princip som de store idle-spikes (>1000 vs <200).
            if idle and min(sim, rec) < 200 and max(sim, rec) > 200:
                st["spike"] += 1
                v["spike_ok"] += 1
                v["ok"] += 1
                return
            if dev <= max(400, 0.35 * abs(s16(rec))):
                st["tol"] += 1
                v["ok"] += 1
                return
        # tidsforskudt match: findes sim-værdien i optagelsen inden for ±0,75 s?
        tl = self.rp_timelines.get(word, [])
        times = [x[0] for x in tl]
        lo = bisect.bisect_left(times, t - self.TIMESHIFT)
        hi = bisect.bisect_right(times, t + self.TIMESHIFT)
        window = [x[1] for x in tl[lo:hi]]
        if lo > 0:
            # v6.6-fix: værdien der var AKTIV ved vinduets start (sidste ændring
            # før lo) — ellers fejler lange statiske perioder (fx trigger-ord
            # der står i minutter mellem to ændringer).
            window.append(tl[lo - 1][1])
        if sim in window:
            st["timeshift"] += 1
            v["timeshift_ok"] += 1
            v["ok"] += 1
            return
        if cls == "fast" and window and any(
                abs(s16(sim) - s16(w)) <= max(200, 0.15 * abs(s16(w))) for w in window):
            st["timeshift"] += 1
            v["timeshift_ok"] += 1
            v["ok"] += 1
            return
        st["fail"] += 1

    def _tl_value(self, word, t):
        """Optaget værdi af word til tid t (sidste ændring ≤ t)."""
        val = None
        for tt, vv in self.rp_timelines.get(word, []):
            if tt <= t:
                val = vv
            else:
                break
        return val

    @staticmethod
    def _filter_bit9_pulses(trans):
        """v6.6: korte bit9-pulser UNDER pre-infusion (0x1107→0x1307→0x1107,
        <0,5 s) er stokastiske: v3-capture har pulsen ved el 1,14 s, steam-live
        ved el 4,97 s — samme pre-længde, forskellig placering. Ingen deterministisk
        model kan ramme begge; pulserne filtreres derfor fra ws-sekvensen på
        BEGGE sider (samme princip som WS_PULSE_MASK for idle-pulser bits 10/11/13)."""
        out = []
        skip_return = False
        for i, (t, w) in enumerate(trans):
            if skip_return:
                skip_return = False   # pulsen retur til pre er ikke en rigtig transition
                continue
            if (w == WS_BREW_EXT and 0 < i < len(trans) - 1
                    and trans[i - 1][1] == WS_BREW_PRE
                    and trans[i + 1][1] == WS_BREW_PRE
                    and trans[i + 1][0] - t < 0.5):
                skip_return = True
                continue
            out.append((t, w))
        return out

    def _verify_report(self):
        v = self.rp_verify
        ratio = v["ok"] / v["n"] if v["n"] else 0.0
        f_rec = self._filter_bit9_pulses(v["transitions_rec"])
        f_sim = self._filter_bit9_pulses(v["transitions_sim"])
        tr_rec = [x[1] for x in f_rec]
        tr_sim = [x[1] for x in f_sim]
        seq_ok = tr_rec == tr_sim
        timing_ok = True
        if seq_ok and len(f_rec) == len(f_sim):
            for (ta, _), (tb, _) in zip(f_rec, f_sim):
                if abs(ta - tb) > 1.5:
                    timing_ok = False
        exact_ok = v["exact_fail"] == 0
        passed = exact_ok and ratio >= 0.92 and seq_ok and timing_ok
        rep = {
            "replay": self.replay["path"], "frames": len(self.replay["frames"]),
            "duration_s": self.replay["duration"],
            "samples": v["n"], "match_ratio": round(ratio, 5),
            "exact_fail": v["exact_fail"], "physical_sync": v["physical_sync"],
            "spike_ok": v["spike_ok"], "timeshift_ok": v["timeshift_ok"],
            "work_sta_sequence_ok": seq_ok, "work_sta_timing_ok": timing_ok,
            "transitions_recorded": [(round(t, 3), hex(w)) for t, w in v["transitions_rec"]],
            "transitions_sim": [(round(t, 3), hex(w)) for t, w in v["transitions_sim"]],
            "per_word": {str(w): s for w, s in sorted(v["words"].items())},
            "pass": passed,
            "criteria": "exact=100%, match≥92%, work_sta-sekvens identisk, timing ±1,5 s",
        }
        return rep

    # ------------------------------------------------------------ ctrl-socket
    def _ctrl_accept(self):
        try:
            conn, _ = self.ctrl_listener.accept()
        except BlockingIOError:
            return
        conn.setblocking(False)
        self.ctrl_clients[conn.fileno()] = {"sock": conn, "buf": b""}

    def _ctrl_read(self):
        for fd in list(self.ctrl_clients):
            c = self.ctrl_clients[fd]
            try:
                data = c["sock"].recv(65536)
            except BlockingIOError:
                continue
            except OSError:
                data = b""
            if not data:
                c["sock"].close()
                del self.ctrl_clients[fd]
                continue
            c["buf"] += data
            while b"\n" in c["buf"]:
                line, c["buf"] = c["buf"].split(b"\n", 1)
                if not line.strip():
                    continue
                try:
                    req = json.loads(line)
                except json.JSONDecodeError:
                    continue
                resp = self.ctrl_dispatch(req)
                resp["id"] = req.get("id")
                try:
                    c["sock"].sendall((json.dumps(resp, ensure_ascii=False) + "\n").encode())
                except OSError:
                    pass

    def ctrl_dispatch(self, req):
        cmd = req.get("cmd")
        if cmd == "ping":
            return {"ok": True, "t": self.t, "mode": self.mode, "link": self.link,
                    "work_sta": self.regs[33], "version": self.version_received,
                    "speed": self.speed, "paused": self.paused,
                    "profile": self.profile, "require_wand": self.require_wand,
                    "wand": self.wand_state,
                    "steam": ({"latch": self.steam_ch["latch"],
                               "src": self.steam_ch["src"]}
                              if self.steam_ch else None),
                    # v6.14: MCU'ens power-tilstand (dvale/wake)
                    "power": {"state": self.power_state,
                              "key_down": self.power_key_down,
                              "hold_s": (round(self.t - self.power_key_t0, 3)
                                         if self.power_key_down else 0.0),
                              "dvale_pending": self.dvale_at is not None,
                              "alarm": list(self.alarm) if self.alarm else None,
                              "wakes": self.stats["power_wakes"],
                              "rx_dropped": self.stats["dvale_rx_dropped"]}}
        if cmd == "regs":
            words = req.get("words", list(range(REGN)))
            return {"ok": True, "values": {str(w): self.regs[w] for w in words}}
        if cmd == "stats":
            return {"ok": True, "stats": self.stats,
                    "boot_push_setup": self.boot_push_setup,
                    "boot_push_preinf": self.boot_push_preinf}
        if cmd == "replay_window":
            # sandheds-vindue: min/max for et ord i optagelsen inden for
            # serve-vægtid [t0,t1] (omregnes med speed) — aliasing-sikker
            if not self.replay:
                return {"ok": False, "error": "ingen replay"}
            w = int(req.get("word", 39))
            t0 = float(req.get("t0", 0)) * self.rp_speed
            t1 = float(req.get("t1", 1e9)) * self.rp_speed
            vals = []
            for fr in self.replay["frames"]:
                if fr["dir"] == "rx" and fr["cmd"] == 0x83 and t0 <= fr["t"] <= t1:
                    if fr["sub"] <= w < fr["sub"] + len(fr["words"]):
                        vals.append(fr["words"][w - fr["sub"]])
            if req.get("signed"):
                vals = [s16(v) for v in vals]
            return {"ok": True, "n": len(vals),
                    "min": min(vals) if vals else None,
                    "max": max(vals) if vals else None}
        if cmd == "replay_anchor":
            # Start the serve replay on the E2E runner's clock: 'elapsed' is the wall time
            # since the UI client's clock origin (t_ms=0), so capture time = elapsed * speed
            # stays aligned with the UI's timestamps no matter how long startup took.
            if not self.replay or self.rp_mode != "serve":
                return {"ok": False, "error": "no serve replay loaded"}
            self.rp_t = max(0.0, float(req.get("elapsed", 0.0))) * self.rp_speed
            self.rp_hold = False
            self.alog.log(self.t, "replay_anchor", rp_t=round(self.rp_t, 3))
            return {"ok": True, "rp_t": self.rp_t}
        if cmd == "inject":
            return {"ok": self.inject(req.get("event", {}))}
        if cmd == "expect_keepawake":
            self.expect_keepawake = bool(req.get("value", True))
            if self.expect_keepawake:
                self.last_awake_write = self.t
            return {"ok": True}
        if cmd == "ack_profile":
            p = req.get("profile", "normal")
            self.profile = dict(ACK_PROFILES[p]) if isinstance(p, str) else dict(p)
            return {"ok": True, "profile": self.profile}
        if cmd == "speed":
            # play-mode: skalér sim-tiden (1×/2×/8×). UI-klienten kører stadig
            # wall-clock-realtid; kun MCU'ens kurver/tid fremskyndes.
            try:
                self.speed = max(0.1, min(64.0, float(req.get("factor", 1.0))))
            except (TypeError, ValueError):
                return {"ok": False, "error": "ugyldig factor"}
            return {"ok": True, "speed": self.speed}
        if cmd == "pause":
            self.paused = bool(req.get("value", True))
            return {"ok": True, "paused": self.paused}
        if cmd == "require_wand":
            self.require_wand = bool(req.get("value", True))
            return {"ok": True, "require_wand": self.require_wand}
        if cmd == "calm":
            self.calm = max(0.05, min(1.0, float(req.get("factor", 1.0))))
            if "maint" in req:
                self.maint = bool(req["maint"])
            self.calm_prev = {}
            self.calm_hist = {}
            return {"ok": True, "calm": self.calm, "maint": self.maint}
        if cmd == "reboot":
            self.do_reboot(cause="ctrl")
            return {"ok": True}
        if cmd == "exit":
            self.running = False
            return {"ok": True}
        return {"ok": False, "error": f"ukendt cmd: {cmd}"}

    def inject(self, ev):
        """Sim-side hændelse (fra runner/scenarie)."""
        typ = ev.get("type")
        r = self.regs
        if typ == "fault":
            for w, key in ((34, "e1"), (35, "e2"), (36, "e3")):
                if key in ev:
                    dur = ev.get("dur")
                    self.faults[w] = (int(ev[key]) & 0xFFFF,
                                      self.t + dur if dur else None)
                    r[w] = int(ev[key]) & 0xFFFF
                    self.alog.log(self.t, "fault_set", word=w, value=r[w], dur=dur)
            return True
        if typ == "fault_clear":
            for w in (34, 35, 36):
                self.faults.pop(w, None)
                r[w] = 0
            self.alog.log(self.t, "fault_clear", word="all")
            return True
        if typ == "set_reg":
            w = int(ev["word"])
            r[w] = int(ev["value"]) & 0xFFFF
            self.alog.log(self.t, "set_reg", word=w, value=r[w])
            return True
        if typ in ("wand", "wand_down", "wand_up"):
            # v6.6: PE.2 wand-kontakt (STEAMLIVE §6). ord44 bit0 + ord46 spejler
            # kontakten (0/1 — IKKE flowmeter-tæller; REGISTERMAP-korrektionen).
            # Wand-EDGE starter/stopper flush-vejen (latch=0, ingen ord24,
            # ingen mælk-auto-stop); display-steam (latch=1) påvirkes ikke.
            on = (typ == "wand_down") or (typ == "wand" and bool(ev.get("on", True)))
            if typ == "wand_up":
                on = False
            if on == self.wand_state:
                return True               # idempotent (ingen kant)
            self.wand_state = on
            r[44] = (r[44] | 1) if on else (r[44] & ~1)
            r[46] = 1 if on else 0
            self.alog.log(self.t, "wand", on=on, input_bits=r[44], ord46=r[46])
            if on:
                if (self.steam_ch is None and r[0] == 1 and r[1] == 0
                        and r[63] == 100 and not (r[34] or r[35] or r[36])):
                    self._start_steam(latch=0, src="wand")
            elif self.steam_ch is not None and not self.steam_ch["latch"]:
                self._stop_steam("wand released")
            return True
        if typ == "power_key":
            # v6.14: simuleret tænd/sluk-knap (PC.8 / k62091). value=1 ned,
            # value=0 slip; hold ≥0,8 s i dvale → wake/reset. Eller ét samlet
            # hold: {"action":"hold","dur":1.6} (auto-slip efter dur).
            if ev.get("action") == "hold":
                dur = float(ev.get("dur", 1.6))
                self.power_key_down = True
                self.power_key_t0 = self.t
                self._key_up_at = self.t + dur
                self.alog.log(self.t, "power_key", value=1, hold_s=dur)
                return True
            v = int(ev.get("value", 1))
            if v == 1:
                if not self.power_key_down:
                    self.power_key_down = True
                    self.power_key_t0 = self.t
                    self.alog.log(self.t, "power_key", value=1)
            else:
                self._power_key_release()
            return True
        if typ == "power_off":
            # bekvemmelighed: dvale uden UART-write (som ord0=0 skrevet)
            if self.power_state == "on":
                self.regs[0] = 0
                self.alog.log(self.t, "power_write", word=0, value=0,
                              source="inject")
                self._enter_dvale()
            return True
        if typ == "power_on":
            # bekvemmelighed: wake/reset uden knap-hold (som alarm/knap)
            self._power_wake("ctrl")
            return True
        if typ == "alarm":
            # sæt alarmen direkte (som cmd8's alarm-felt): dag/time/minut
            self.alarm = (int(ev.get("day", 0)), int(ev.get("hour", 0)),
                          int(ev.get("minute", 0)))
            if not any(self.alarm):
                self.alarm = None
            self.alarm_fired = False
            self.alog.log(self.t, "alarm_set", alarm=list(self.alarm)
                          if self.alarm else [0, 0, 0])
            return True
        if typ == "garbage":
            self.garbage_until = self.t + float(ev.get("dur", 2.0))
            self.garbage_crc = bool(ev.get("crc_too", True))
            self.alog.log(self.t, "garbage_start", dur=ev.get("dur", 2.0))
            return True
        if typ == "link":
            state = ev.get("state", "up")
            if state == "closed" and self.link != "closed":
                self._close_pty()
                self.link = "closed"
                self.alog.log(self.t, "link_closed")
            elif state == "up" and self.link == "closed":
                self._open_pty()
                self.link = "up"
                self.version_received = False
                self.alog.log(self.t, "link_reopened", pty=self.slave_path)
            elif state in ("up", "silent"):
                self.link = state
                self.alog.log(self.t, "link_state", state=state)
            return True
        if typ == "cup_removed":
            w = 38 if ev.get("scale") == "grind" else 39
            r[w] = int(ev.get("value", 64607)) & 0xFFFF
            self.alog.log(self.t, "cup_removed", word=w, value=r[w])
            return True
        if typ == "milk_jump":
            r[48] = int(ev["value"]) & 0xFFFF
            return True
        if typ == "bad_len":
            # v6.5-scenarios (rx-stall-repro): én 0x83-frame med BIT-FEJL I
            # LÆNGDE-BYTEN (default: sæt bit7 → L+128 > REGN). En parser der
            # blindt venter på 2*L+7 bytes går i stå til bufferen løber over
            # (UI_v6.md v6.4-fund: ~3,5 s i ourui's rx_feed). CRC'en er stadig
            # gyldig for den OPRINDELIGE længde — fejlen sidder kun i LEN.
            sub = int(ev.get("sub", 33))
            ln = max(1, min(int(ev.get("len", 7)), 60))
            fr = bytearray(build_frame(0x83, sub, self.regs[sub:sub + ln]))
            fr[4] = int(ev.get("len_byte", fr[4] | 0x80)) & 0xFF
            self.send_tx(bytes(fr))
            self.alog.log(self.t, "bad_len_inject", sub=sub, len=ln,
                          len_byte=fr[4], frame=hexs(bytes(fr)))
            return True
        self.alog.log(self.t, "inject_unknown", event=ev)
        return False

    # ------------------------------------------------------------ hoved-loop
    def advance(self, target_t):
        """Advance sim-tiden til target_t i ≤10 ms ticks (deterministisk)."""
        while self.t < target_t - 1e-9:
            dt = min(TICK, target_t - self.t)
            self.t += dt
            # v6.14: ord0=0 skrevet → dvale inden for ≤0,5 s (status-task 500 ms)
            if self.dvale_at is not None and self.t >= self.dvale_at:
                self._enter_dvale()
            if self.power_state == "off":
                # Dvale: regs frosset, UART tavs, aktiviteter standset. Kun
                # RTC/alarm og knap-hold kan vække MCU'en (CPU-reset).
                self._tick_rtc(dt)
                if self._key_up_at is not None and self.t >= self._key_up_at:
                    self._key_up_at = None
                    self._power_key_release()
                continue
            self._tick_rtc(dt)
            if self._key_up_at is not None and self.t >= self._key_up_at:
                self._key_up_at = None
                self._power_key_release()
            self._tick_faults()
            if self.replay and self.rp_mode == "serve":
                self._replay_advance(dt)
            else:
                self._tick_triggers()
                self._tick_activity(dt)
                self._tick_steam(dt)
                self._tick_post(dt)
                self._tick_steam_post(dt)
                self._tick_idle_sensors(dt)
            self._tick_keepalive()
            self._tick_boot_push_check()
            if self.calm < 1.0 and not self.replay:
                a = self.calm
                for w in (38, 39, 40, 42, 47, 48, 50, 51, 52, 53, 55, 58, 69):
                    signed = w in (38, 39)
                    raw = s16(self.regs[w]) if signed else self.regs[w]
                    hist = self.calm_hist.setdefault(w, [])
                    hist.append(raw)
                    if len(hist) > 3:
                        hist.pop(0)
                    med = sorted(hist)[len(hist) // 2]      # median-3: dræber enkelt-sample spikes
                    prev = self.calm_prev.get(w, med)
                    val = int(round(prev + (med - prev) * a))
                    if signed and abs(val) < 2.5:      # deadbånd: <0,25 g → 0 (brugers ønske)
                        val = 0
                    self.regs[w] = (val & 0xFFFF) if signed else val
                    self.calm_prev[w] = val
            if self.pty and self.link == "up":
                if not self.version_received and self.t >= self.next_ver:
                    self.send_tx(build_version_request())
                    self.stats["version_reqs"] += 1
                    self.next_ver = self.t + VER_INTERVAL
                if self.t < self.garbage_until:
                    n = self.rng.randint(1, 24)
                    junk = bytes(self.rng.getrandbits(8) for _ in range(n))
                    try:
                        os.write(self.master, junk)
                        self.stats["garbage_tx_bytes"] += n
                    except OSError:
                        pass
                    if self.garbage_crc and self.rng.random() < 0.3:
                        bad = bytearray(build_frame(0x83, 33, self.regs[33:40]))
                        bad[-1] ^= 0xFF       # CRC-korruption
                        try:
                            os.write(self.master, bytes(bad))
                        except OSError:
                            pass
                self._flush_tx()

    def run(self):
        while self.running:
            wall = time.monotonic() - self.t0_wall
            dt = wall - self._last_wall
            self._last_wall = wall
            if not self.paused and dt > 0:
                self.advance(self.t + dt * self.speed)
            rfds = list(self.ctrl_clients)
            if self.ctrl_listener:
                rfds.append(self.ctrl_listener.fileno())
            if self.pty and self.master is not None and self.link != "closed":
                rfds.append(self.master)
            try:
                r, _, _ = select.select(rfds, [], [], 0.005)
            except (OSError, ValueError):
                r = []
            for fd in r:
                if self.ctrl_listener and fd == self.ctrl_listener.fileno():
                    self._ctrl_accept()
                elif fd in self.ctrl_clients:
                    self._ctrl_read()
                elif self.pty and self.master is not None and fd == self.master:
                    try:
                        data = os.read(self.master, 4096)
                    except BlockingIOError:
                        data = b""
                    except OSError:
                        data = b""
                        if self.link == "up":
                            self.link = "silent"
                    if data:
                        for cmd, sub, payload, _body in self.parser.feed(data):
                            self.handle_frame(cmd, sub, payload)
        self.alog.log(self.t, "sim_exit", stats=self.stats)
        self.alog.close()
        self._close_pty()
        if self.ctrl_listener:
            self.ctrl_listener.close()
            Path(self.ctrl_path).unlink(missing_ok=True)


# ---------------------------------------------------------------- CLI
def main():
    ap = argparse.ArgumentParser(description="Simuleret MerakiTech powerMcu (E2E)")
    ap.add_argument("--config", help="sandbox config.json (default: sim/fixtures/config.json)")
    ap.add_argument("--spec", help="parity_spec.json (default: sim/fixtures/parity_spec.json)")
    ap.add_argument("--pty-file", help="fil hvor pty-slave-stien skrives")
    ap.add_argument("--ctrl", help="unix-socket til injektion/kontrol (JSONL)")
    ap.add_argument("--assert-log", help="JSONL assertions-log")
    ap.add_argument("--profile", default="normal",
                    help="ACK-profil: normal|observed_v3|slow|lossy|no_ack (eller JSON)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--no-require-wand", action="store_true",
                    help="legacy no-op (v6.6): display-steam kræver ikke wand "
                         "(STEAMLIVE §5); flaget bevares for kompatibilitet")
    ap.add_argument("--range-policy", choices=["log", "reject", "off"], default="log")
    ap.add_argument("--trigger-policy", choices=["reject", "pass"], default="reject")
    ap.add_argument("--replay", help="frames.csv til REPLAY-mode")
    ap.add_argument("--replay-mode", choices=["serve", "verify"], default="serve")
    ap.add_argument("--speed", type=float, default=1.0, help="replay-afspilningshastighed")
    ap.add_argument("--replay-hold", action="store_true",
                    help="serve replay: hold the replay clock at 0 until a 'replay_anchor' ctrl command")
    ap.add_argument("--verify-report", help="JSON-rapport for replay-verify")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    cfg = load_baseline_config(args.config) if args.config else load_baseline_config()
    spec = load_spec(args.spec) if args.spec else load_spec()
    profile = args.profile
    if profile.strip().startswith("{"):
        profile = json.loads(profile)

    if args.replay and args.replay_mode == "verify":
        sim = McuSim(config=cfg, spec=spec, seed=args.seed, profile=profile,
                     require_wand=not args.no_require_wand,
                     range_policy=args.range_policy, trigger_policy=args.trigger_policy,
                     assert_path=args.assert_log, pty=False, quiet=args.quiet)
        sim.load_replay(args.replay, mode="verify")
        rep = sim._verify_step()
        sim.alog.close()
        if args.verify_report:
            Path(args.verify_report).write_text(json.dumps(rep, indent=2, ensure_ascii=False))
        print(f"[replay-verify] {args.replay}: samples={rep['samples']} "
              f"match={rep['match_ratio']:.4f} exact_fail={rep['exact_fail']} "
              f"ws-sekvens={'OK' if rep['work_sta_sequence_ok'] else 'FEJL'} "
              f"timing={'OK' if rep['work_sta_timing_ok'] else 'FEJL'} "
              f"→ {'PASS' if rep['pass'] else 'FAIL'}")
        if not args.quiet:
            for t, w in rep["transitions_sim"]:
                print(f"  sim  t={t:8.3f} work_sta={w}")
        sys.exit(0 if rep["pass"] else 1)

    sim = McuSim(config=cfg, spec=spec, seed=args.seed, profile=profile,
                 require_wand=not args.no_require_wand,
                 range_policy=args.range_policy, trigger_policy=args.trigger_policy,
                 assert_path=args.assert_log, pty=True, ctrl_path=args.ctrl,
                 pty_file=args.pty_file, quiet=args.quiet)
    if args.replay:
        sim.load_replay(args.replay, mode="serve", speed=args.speed)
        sim.rp_hold = args.replay_hold
    try:
        sim.run()
    except KeyboardInterrupt:
        sim.running = False
        sim.alog.log(sim.t, "sim_interrupt")
        sim.alog.close()


if __name__ == "__main__":
    main()