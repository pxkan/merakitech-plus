#!/usr/bin/env python3
"""ref_client.py — protokol-level REFERENCE-KLIENT til E2E-testmiljøet.

Beviser mcu_sim'en indtil Fable's ourui SIM-build lander. Implementerer samme
SIM-kontrakt (CONTRACT.md): state-JSONL pr. frame, fb-buffer-fil, touch via
evdev-FIFO (tinj_hold-format), ctrl-socket, boot-push, keep-awake (word1=0),
poll-rotation, write-politik (bånd/trigger/range) og ourui v6's ACK/retransmit-
politik (150 ms timeout, præcis én retransmit, derefter FAIL).

UI'et er en MINIMAL emulering (home + 4 aktivitets-knapper m. 650 ms hold-for-
start + settings-save via ctrl) med en simpel syntetisk fb-renderer (cirkel-clip
r234, fase-farver, page-/toast-regioner). Den erstatter IKKE ourui's rigtige
UI — men den beviser sim + runner + assertions-pipelinen.

Kun Python stdlib. Ingen enhedskontakt.
"""
import argparse
import json
import os
import select
import socket
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import proto
from proto import (REGN, FB_BYTES, W, H, crc16_modbus, build_frame, build_query,
                   build_version_response, build_power_frame, FrameParser,
                   phase_of, s16, config_to_words, load_baseline_config,
                   load_spec, word_bounds, pack112, pack113, unpack112,
                   unpack113, EV_FMT, EV_SYN, EV_KEY, EV_ABS, SYN_REPORT,
                   BTN_TOUCH, ABS_X, ABS_Y, ABS_MT_POSITION_X,
                   ABS_MT_POSITION_Y, ABS_MT_TRACKING_ID, touch_raw)

POLL_INTERVAL = 0.100       # stock: ~100 ms rotation
POLL_ROTATION = [(0, 56), (33, 7), (56, 56), (33, 7)]
EXT_INTERVAL = 1.0          # ord 112..114 @ 1 s (som ourui v6)
KEEPALIVE_INTERVAL = 30.0
ACK_TIMEOUT = 0.150         # ourui v6: 150 ms, præcis én retransmit
LINK_TIMEOUT = 1.5          # ourui: link_up = now-last_rx < 1500 ms
HOLD_START_MS = 650         # ourui: hold-for-start på aktivitets-knapper
# v6.14 (UI_v6 §19): tænd/sluk-knappen — samme semantik som ourui/stock
PW_HOLD_S = 1.5             # stock: 2 sekund-skift (1,0-2,0 s); midten
PW_GAP_S = 0.080            # cmd8 → usleep(80 ms) → cmd6 sub0 len2 [0,0]
PW_WAKE_S = 10.0            # TÆNDER…: så længe ventes på ord0=1
PW_SEEN_S = 0.6             # ord0=1 set så længe (m. forbindelse) → vågen

# ourui v6 fase-farver (RGB) — bruges til fb-rendering + region-assertions
PHASE_RGB = {
    "OFFLINE": (90, 90, 96), "SLEEP": (70, 80, 120), "ERROR": (255, 96, 96),
    "HEAT": (255, 186, 84), "READY": (98, 222, 168), "GRIND": (174, 228, 112),
    "PRE": (120, 190, 255), "EXT": (90, 160, 255), "WATER": (110, 200, 230),
    "STEAM": (220, 220, 240), "DONE": (140, 230, 180),
}
PHASE_BG = {
    "OFFLINE": (24, 24, 28), "SLEEP": (18, 20, 34), "ERROR": (130, 36, 40),
    "HEAT": (150, 78, 26), "READY": (24, 104, 92), "GRIND": (62, 116, 46),
    "PRE": (26, 52, 96), "EXT": (22, 44, 88), "WATER": (24, 58, 74),
    "STEAM": (58, 58, 70), "DONE": (28, 96, 72),
}
PAGE_RGB = {"home": (40, 44, 52), "settings": (52, 44, 60),
            "detail": (60, 52, 44), "sys": (44, 52, 60), "diag": (36, 40, 44),
            "bye": (60, 40, 40), "off": (16, 16, 18)}
# home-knapper (x, y, r) — reference-layout (ikke ourui's rigtige layout)
BUTTONS = {"grind": (130, 160, 58), "brew": (350, 160, 58),
           "water": (130, 330, 58), "steam": (350, 330, 58)}
SETTINGS_BTN = (240, 430, 26)


def rgb2bgra(r, g, b):
    return bytes((b, g, r, 255))


class RefClient:
    def __init__(self, args):
        self.args = args
        self.root = Path(args.root) if args.root else None
        self.cfg_path = Path(args.config)
        self.spec = load_spec(args.spec) if args.spec else load_spec()
        self.bounds = word_bounds(self.spec)
        self.params = {p["id"]: p for p in self.spec["parameters"]}
        self.cfg = {}
        self.config_missing = False
        self.config_oor = []
        self.load_config()
        # mirror
        self.regs = [0] * REGN
        self.reg_seen = [False] * REGN
        for w, v in config_to_words(self.cfg).items():
            self.regs[w] = v
        self.parser = FrameParser()
        self.rx_ok = 0
        self.rx_bad = 0
        self.last_rx = -1e9
        # write-kø (ourui v6-politik)
        self.wq = None            # {sub, words, sent, tries, raw}
        self.c6_ok = self.c6_retx = self.c6_fail = self.c6_block = 0
        # UI-state
        self.page = "home"
        self.toast = None         # {text, kind, until}
        self.lang = int(self.cfg.get("language", 0))
        self.save_state = None    # {state: pending|ok|fail, param, value}
        self.activity_pending = None
        self.activity_seen = False
        # v6.13: manuel STOP (samme state-felt som ourui's stop:{req,ok,fail})
        self.stop_req = self.stop_ok = self.stop_fail = 0
        self.stop_pending = None      # aktiv stop-bestilling (kind)
        self.stop_last = None
        self.stop_t0 = 0.0
        self._stop_sw_followup = False  # vand: sub19=[0] efter sub16-ACK
        self.view_bye = False
        # v6.14: tænd/sluk-knappen (samme state-felt som ourui's power:{...})
        self.pw_state = "on"          # on|turning_off|off|turning_on
        self.pw_key_down = False
        self.pw_fired = False
        self.pw_blk_shown = False
        self.pw_push_set = False
        self.pw_f8 = None             # sidste cmd8-frame (til ack8-match)
        self.pw_t_press = self.pw_t_down = 0.0
        self.pw_t_req = self.pw_t_state = self.pw_t_on1 = 0.0
        self.pw_n = {"presses": 0, "short": 0, "offs": 0, "ons": 0,
                     "blocked": 0, "fails": 0, "ack8": 0, "auto_on": 0,
                     "wake_timeouts": 0}
        # touch
        self.tch_down = False
        self.tch_x = self.tch_y = 0
        self.tch_t0 = 0.0
        self.tch_btn = None
        # timing
        self.t0 = time.monotonic()
        self.now = 0.0
        self.next_poll = 0.0
        self.poll_idx = 0
        self.next_ext = 0.0
        self.next_awake = 0.0
        self.next_render = 0.0
        self.boot_pushed = False
        self.version_sent = False
        self.running = True
        self.exit_code = 0
        self.fb = bytearray(FB_BYTES)
        self.last_fb_write = -1e9
        self.frame_ms = 0.0
        # io
        self.ufd = os.open(args.tty, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            import tty
            tty.setraw(self.ufd)          # 8N1 raw, VMIN=0/VTIME=0 (non-blocking poll)
        except (ImportError, OSError):
            pass
        self.state_f = open(args.state, "a", buffering=1) if args.state else None
        self.fb_path = Path(args.fb) if args.fb else None
        self.touch_fd = None
        if args.touch:
            tp = Path(args.touch)
            if not tp.exists():
                os.mkfifo(str(tp))
            self.touch_fd = os.open(str(tp), os.O_RDONLY | os.O_NONBLOCK)
            # hold FIFO'en åben i skrive-ende så reads ikke EOF'er
            self.touch_wfd = os.open(str(tp), os.O_WRONLY | os.O_NONBLOCK)
        self.ctrl_listener = None
        self.ctrl_clients = {}
        if args.ctrl:
            cp = Path(args.ctrl)
            cp.unlink(missing_ok=True)
            self.ctrl_listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.ctrl_listener.bind(str(cp))
            self.ctrl_listener.listen(4)
            self.ctrl_listener.setblocking(False)
        self.log(f"reference-klient klar: tty={args.tty} config={self.cfg_path}")

    def log(self, msg):
        print(f"[ref_client t={self.now:7.2f}] {msg}", flush=True)

    # ------------------------------------------------------------ config
    def load_config(self):
        try:
            self.cfg = json.loads(self.cfg_path.read_text())
            self.config_missing = False
        except (OSError, json.JSONDecodeError):
            self.cfg = {}
            self.config_missing = True
        self.config_oor = []
        for p in self.spec["parameters"]:
            f = p.get("config_field")
            if not f or p.get("min") is None or p.get("max") is None:
                continue
            if p.get("packing"):
                continue
            v = self.cfg.get(f)
            if isinstance(v, (int, float)) and not (p["min"] <= v <= p["max"]):
                self.config_oor.append({"param": p["id"], "field": f, "value": v,
                                        "min": p["min"], "max": p["max"]})
        self.lang = int(self.cfg.get("language", 0))

    def config_set(self, field, value):
        """Atomisk config-skrivning (tmp+rename, som ourui/REVERT-mønsteret)."""
        cfg = dict(self.cfg)
        cfg[field] = value
        tmp = self.cfg_path.with_suffix(".json.tmp")
        tmp.write_text(json.dumps(cfg, indent="\t", ensure_ascii=False) + "\n")
        os.replace(str(tmp), str(self.cfg_path))
        self.cfg = cfg
        self.load_config()

    def boot_flag(self):
        if not self.root:
            return False
        return (self.root / "customer" / "new").exists()

    def set_boot_flag(self, on):
        if not self.root:
            return False
        p = self.root / "customer" / "new"
        if on:
            p.parent.mkdir(parents=True, exist_ok=True)
            p.touch()
        else:
            p.unlink(missing_ok=True)
        return True

    # ------------------------------------------------------------ UART
    def uart_send(self, data):
        try:
            os.write(self.ufd, data)
        except OSError as e:
            self.log(f"uart write-fejl: {e}")

    def frame_query(self, sub, n):
        self.uart_send(build_query(sub, n))

    def wq_push(self, sub, words, intent=()):
        """Frame-politik før send (ourui v6 wq_push_t): bånd + trigger + range.
        intent = mængde af ord der eksplicit må være start/trigger-ord."""
        n = len(words)
        end = sub + n
        if not (end < 33 or end in (113, 114, 115)):
            self.c6_block += 1
            self.toast_set(f"BLOKERET · BÅND sub{sub}", "err")
            return False
        for i, w in enumerate(words):
            word = sub + i
            if word in (26, 27, 30) and w != 0:
                self.c6_block += 1
                self.toast_set(f"BLOKERET · TRIGGER ord{word}", "err")
                return False
            if word in (3, 6, 9, 16, 19, 22, 24) and w != 0 and word not in intent:
                self.c6_block += 1
                self.toast_set(f"BLOKERET · START ord{word} uden intent", "err")
                return False
            if word in self.bounds:
                lo, hi, _ = self.bounds[word]
                if not (lo <= w <= hi):
                    self.c6_block += 1
                    self.toast_set(f"BLOKERET · RANGE ord{word}={w}", "err")
                    return False
        if self.wq is not None:
            self.toast_set("VENT · SKRIVNING I GANG", "warn")
            return False
        raw = build_frame(0x06, sub, words)
        self.wq = {"sub": sub, "words": list(words), "sent": self.now,
                   "tries": 1, "raw": raw}
        self.uart_send(raw)
        return True

    def wq_step(self):
        if not self.wq:
            return
        if self.now - self.wq["sent"] >= ACK_TIMEOUT:
            if self.wq["tries"] == 1:
                self.wq["tries"] = 2
                self.wq["sent"] = self.now
                self.c6_retx += 1
                self.uart_send(self.wq["raw"])
            else:
                self.c6_fail += 1
                self.save_state = {"state": "fail",
                                   "param": (self.save_state or {}).get("param"),
                                   "value": (self.save_state or {}).get("value")}
                if self.pw_state == "turning_off":
                    self.pw_fail()          # v6.14: PowerOff kom ikke igennem
                elif self.stop_pending:
                    self.stop_fail += 1
                    self.stop_pending = None
                    self._stop_sw_followup = False
                    self.toast_set("STOP: NO ACK · TRY AGAIN", "err")
                else:
                    self.toast_set("GEM FEJLET · INGEN ACK · INTET GEMT", "err")
                self.log(f"write FAIL (ingen ACK): sub{self.wq['sub']}")
                self.wq = None

    def on_ack(self, sub, words):
        if self.wq and self.wq["sub"] == sub and self.wq["words"] == list(words):
            self.c6_ok += 1
            self.wq = None
            if (self.pw_state == "turning_off" and self.pw_push_set
                    and sub == 0 and list(words) == [0, 0]):
                # v6.14: PowerOff ACK'et → SLUKKET (MCU'en går i dvale)
                self.pw_state = "off"
                self.pw_t_state = self.now
                self.pw_t_on1 = 0.0
            if self._stop_sw_followup and sub == 16 and list(words) == [0]:
                self._stop_sw_followup = False
                self.wq_push(19, [0])      # vand-stop del 2 (ord19≠0 ved stop)
            if self.save_state and self.save_state.get("state") == "pending":
                p = self.save_state.get("param")
                v = self.save_state.get("value")
                prm = self.params.get(p)
                if prm and prm.get("config_field"):
                    self.config_set(prm["config_field"], v)
                self.save_state = {"state": "ok", "param": p, "value": v}
                self.toast_set("GEMT · CONTROLLER + CONFIG", "ok")
            self.log(f"ACK ok: sub{sub} len{len(words)}")

    def rx_feed(self, data):
        for cmd, sub, payload, _body in self.parser.feed(data):
            self.last_rx = self.now
            if cmd == 0x83:
                self.regs[sub:sub + len(payload)] = payload
                for i in range(len(payload)):
                    self.reg_seen[sub + i] = True
                self.rx_ok += 1
            elif cmd == 0x86:
                self.rx_ok += 1
                self.on_ack(sub, payload)
            elif cmd == 0x89:
                if self.version_sent:
                    # MCU'en sender 0x89 igen efter gennemført håndtryk →
                    # den er genstartet (reboot/link reopen): re-handshake
                    # + boot-push som ved ny connect (PARITY §1.4).
                    self.version_sent = False
                    self.boot_pushed = False
                    self.log("MCU-genstart detekteret (0x89 igen) → re-handshake + boot-push")
                if not self.version_sent:
                    ver = str(self.cfg.get("softversion", "1.1.1-69"))
                    self.uart_send(build_version_response(ver))
                    self.version_sent = True
                    self.log(f"versions-svar sendt: {ver}")
            elif cmd == 0x88:
                self.rx_ok += 1
                if self.pw_f8 and payload == self.pw_f8[3:13]:
                    self.pw_n["ack8"] += 1    # v6.14: MCU'ens 0x88-kvittering
        self.rx_bad = self.parser.crc_bad

    def boot_push(self):
        """PARITY §1.4: sub1 len30 + sub112 len3 ved opstart (fra config-mirror)."""
        words = config_to_words(self.cfg)
        mirror = [0] * 31
        for w, v in words.items():
            if 1 <= w <= 30:
                mirror[w] = v
        # word1=0 (awake) — keep-awake-fix (SESSION §10.1)
        mirror[1] = 0
        self.uart_send(build_frame(0x06, 1, mirror[1:31]))
        self.uart_send(build_frame(0x06, 112, [words[112], words[113], words[114]]))
        self.uart_send(build_frame(0x06, 0, [1]))       # PowerOn
        self.uart_send(build_frame(0x06, 1, [0]))       # keep-awake
        self.boot_pushed = True
        self.next_awake = self.now + KEEPALIVE_INTERVAL
        self.log("boot-push: sub1 len30 + sub112 len3 + power/awake")

    # ------------------------------------------------------------ handlinger
    def act_push(self, kind):
        # STOP må aldrig blokeres af aktiv fase (ourui v6 act_push_steam_stop)
        if kind == "steam_stop":
            g = lambda k, d: int(self.cfg.get(k, d))
            mt = min(700, max(450, g("AtuoSteamTarge", 650)))
            ok = self.wq_push(24, [0, mt, 0, 0, 0, 0, 0, 0])
            if ok:
                self.activity_pending = "steam_stop"
                self.toast_set("STOP: DAMP", "ok")
            return ok
        if kind in ("grind_stop", "brew_stop", "water_stop"):
            # v6.13 (UI_v6 §18): stop-frames = kun nuller, ingen intent,
            # samme write-kø/ACK-politik som alt andet. Vand: sub16=[0]
            # (+ sub19=[0] hvis ord19≠0 — aldrig en halv vand-stop).
            if kind == "grind_stop":
                ok = self.wq_push(6, [0])
            elif kind == "brew_stop":
                ok = self.wq_push(22, [0])
            else:
                ok = self.wq_push(16, [0])
                if ok and self.reg_seen[19] and self.regs[19] != 0:
                    self._stop_sw_followup = True
            if ok:
                self.stop_req += 1
                self.stop_pending = kind if self._stop_running(kind) else None
                self.stop_last = kind
                self.stop_t0 = self.now
                self.toast_set(f"STOP: {kind[:-5].upper()}", "ok")
            return ok
        if self.config_missing:
            self.toast_set("CONFIG MANGLER · START SPÆRRET", "err")
            return False
        if self.config_oor:
            self.toast_set("CONFIG UDEN FOR SPEC · RET FØRST", "err")
            return False
        ph = phase_of(self.regs, self.link_up())
        if ph in ("GRIND", "PRE", "EXT", "WATER", "STEAM"):
            self.toast_set("VENT TIL AKTIVITET ER SLUT", "warn")
            return False
        if ph == "OFFLINE":
            self.toast_set("OFFLINE · INGEN START", "err")
            return False
        g = lambda k, d: int(self.cfg.get(k, d))
        if kind == "grind":
            ok = self.wq_push(3, [1, g("autoGrindWeight", 185), 60, 1], intent=(3, 6))
        elif kind == "brew":
            # sync kun hvis mirror afviger fra config (som ourui v6 act_push)
            if self.reg_seen[23] and self.regs[23] != g("autobrewWeight", 360):
                if not self.wq_push(23, [g("autobrewWeight", 360)]):
                    return False
            p = [pack112(g("preInfusionSwitch", 1), g("preInfusionPressure", 4)),
                 pack113(g("preInfusionTime", 6), g("breakTime", 5)),
                 g("headTemper", 70)]
            if self.reg_seen[112] and (p[0] != self.regs[112]
                                       or (p[1] >> 8) != (self.regs[113] >> 8)
                                       or p[2] != self.regs[114]):
                if not self.wq_push(112, p):
                    return False
            b = [60, 0, 0, 0, 1, 0, g("brewTemper", 870), 0, 60,
                 g("steamGears", 3), 60, 0, g("autoHotWaterTarget", 400),
                 0, 0, 0, 0, 1]
            ok = self.wq_push(5, b, intent=(9, 22))
        elif kind == "water":
            h = [1, 0, g("brewTemper", 870), 0, 60, g("steamGears", 3), 60,
                 1, g("autoHotWaterTarget", 400), 0, 0]
            ok = self.wq_push(9, h, intent=(9, 16))
        elif kind == "steam":
            ok = self.wq_push(24, [1, g("AtuoSteamTarge", 650), 0, 0, 0, 0, 0, 0],
                              intent=(24,))
        else:
            return False
        if ok:
            self.activity_pending = kind
            self.toast_set(f"START: {kind.upper()}", "ok")
        return ok

    def _stop_running(self, kind):
        """v6.13: samme bits som ourui's stop_running() (confirmation-vagten)."""
        ws = self.regs[33]
        if kind == "grind_stop":
            return bool(ws & 0x0040)
        if kind == "brew_stop":
            return (ws & 0x0006) == 0x0006
        if kind == "water_stop":
            return (ws & 0x000A) == 0x000A
        return False

    # ------------------------------------------------------------ v6.14: tænd/sluk
    # Samme semantik som ourui v6.14 / stock (UI_v6 §19.1): HOLD 1,5 s →
    # cmd8 (alarm 0/0/0) → ≥80 ms → cmd6 sub0 len2 [0,0] → ACK → SLUKKET.
    # Spærret under bryg/damp/kværn (stock stopper ALDRIG en aktivitet ved
    # sluk; varmt vand spærrer ikke). Keep-awake pauserer mens der er slukket.
    # MCU'en vågner selv (knap-hold/alarm → reset); svarer den ord0=1 i 0,6 s
    # → PowerOn (sub0=[1] + sub1=[0]) + HJEM. Re-push sker via 0x89-runden
    # (rx_feed ovenfor: boot_pushed=False → boot_push() igen).
    def pw_busy(self):
        ws = self.regs[33] if self.reg_seen[33] else 0
        return bool((ws & 0x1000) or (ws & 0x4000)
                    or (self.reg_seen[22] and self.regs[22])
                    or (self.reg_seen[24] and self.regs[24])
                    or (self.reg_seen[6] and self.regs[6])
                    or self.activity_pending in ("grind", "brew", "steam"))

    def pw_key(self, val):
        if val == 1 and not self.pw_key_down:
            self.pw_key_down = True
            self.pw_t_press = self.pw_t_down = self.now
            self.pw_fired = False
            self.pw_blk_shown = False
            self.pw_n["presses"] += 1
            if self.pw_state == "off":
                self.pw_fired = True
                self.pw_state = "turning_on"      # TÆNDER… (MCU'en vågner selv)
                self.pw_t_state = self.now
                self.pw_t_on1 = 0.0
        elif val == 0 and self.pw_key_down:
            self.pw_key_down = False
            if self.pw_state == "on" and not self.pw_fired:
                self.pw_n["short"] += 1
            self.pw_fired = False

    def pw_mcu_on(self):
        if self.link_up() and self.reg_seen[0] and self.regs[0] == 1:
            if not self.pw_t_on1:
                self.pw_t_on1 = self.now
            return self.now - self.pw_t_on1 >= PW_SEEN_S
        self.pw_t_on1 = 0.0
        return False

    def pw_on(self):
        self.pw_state = "on"
        self.pw_t_state = self.now
        self.pw_t_on1 = 0.0
        self.pw_n["ons"] += 1
        self.page = "home"
        self.uart_send(build_frame(0x06, 0, [1]))   # PowerOn (som app_init)
        self.uart_send(build_frame(0x06, 1, [0]))   # vågen
        self.next_awake = self.now + KEEPALIVE_INTERVAL
        self.toast_set("MASKINEN ER TÆNDT", "ok")

    def pw_fail(self):
        self.pw_state = "on"
        self.pw_t_state = self.now
        self.pw_n["fails"] += 1
        self.page = "home"
        self.toast_set("SLUK: INTET SVAR · PRØV IGEN", "err")

    def pw_off_begin(self):
        lt = time.localtime()
        payload = bytes([lt.tm_year & 0xFF, (lt.tm_year >> 8) & 0xFF,
                         lt.tm_mon, lt.tm_mday, lt.tm_hour, lt.tm_min,
                         lt.tm_sec, 0, 0, 0])       # alarm 0/0/0 (ingen ugeplan)
        self.pw_f8 = build_power_frame(payload)
        self.uart_send(self.pw_f8)
        self.pw_state = "turning_off"
        self.pw_t_req = self.pw_t_state = self.now
        self.pw_push_set = False
        self.pw_t_on1 = 0.0
        self.pw_n["offs"] += 1
        self.toast = None

    def pw_step(self):
        if self.pw_state == "on" and self.pw_key_down and not self.pw_fired:
            if self.pw_busy():
                self.pw_t_down = self.now     # stock: e60 tæller ikke under aktivitet
                if self.now - self.pw_t_press >= PW_HOLD_S and not self.pw_blk_shown:
                    self.pw_blk_shown = True
                    self.pw_n["blocked"] += 1
                    self.toast_set("STOP AKTIVITETEN FØRST · SÅ SLUK", "warn")
            elif self.now - self.pw_t_down >= PW_HOLD_S:
                self.pw_fired = True
                if not self.link_up() or not self.reg_seen[0] or self.regs[0] == 0:
                    self.pw_n["fails"] += 1
                    self.toast_set("INGEN FORBINDELSE · SLUKKER IKKE", "warn")
                else:
                    self.pw_off_begin()
        if self.pw_state == "turning_off":
            if not self.pw_push_set and self.now - self.pw_t_req >= PW_GAP_S:
                self.pw_push_set = True
                if not self.wq_push(0, [0, 0]):
                    self.pw_fail()
            # ACK → off i on_ack(); FAIL → pw_fail() i wq_step()
        elif self.pw_state == "off":
            if self.pw_mcu_on():              # MCU'en vågnede af sig selv (alarm/knap)
                self.pw_n["auto_on"] += 1
                self.pw_on()
        elif self.pw_state == "turning_on":
            if self.pw_mcu_on():
                self.pw_on()
            elif self.now - self.pw_t_state >= PW_WAKE_S:
                self.pw_n["wake_timeouts"] += 1
                self.pw_state = "off"
                self.pw_t_state = self.now
                self.pw_t_on1 = 0.0

    def save_param(self, pid, value):
        """Settings-GEM: MCU-write først (hvor ordet kendes), så config (PARITY §5.8)."""
        prm = self.params.get(pid)
        if not prm:
            self.toast_set(f"UKENDT PARAMETER {pid}", "err")
            return False
        if prm.get("persist") in ("readonly", "config-readonly", "config-unused",
                                  "config-excluded"):
            self.toast_set("LÅST · INGEN EDITOR", "warn")
            return False
        value = int(value)
        if prm.get("min") is not None and prm.get("max") is not None:
            if not (prm["min"] <= value <= prm["max"]):
                self.c6_block += 1
                self.toast_set(f"AFVIST · {pid}={value} UDEN FOR SPEC", "err")
                return False
        self.save_state = {"state": "pending", "param": pid, "value": value}
        wr = prm.get("write") or {}
        # hotwater_mode: parity_spec's write-blok (sub9 len11) er en START-blok,
        # ikke en settings-write — ourui v6 persisterer KUN config (M_NONE,
        # "OURUI STARTER KUN AUTO"). Behandles derfor som config-only.
        config_only_override = {"hotwater_mode"}
        if wr.get("sub") is None or not prm.get("words") or pid in config_only_override:
            # config-only (tier 2): ingen MCU-write
            self.config_set(prm["config_field"], value)
            self.save_state = {"state": "ok", "param": pid, "value": value}
            self.toast_set("GEMT · STOCK-UI: EFTER GENSTART", "ok")
            return True
        if prm.get("packing"):
            # byte-pakket word112/113: bevar øvrige bytes fra config
            sw = int(self.cfg.get("preInfusionSwitch", 1))
            pr = int(self.cfg.get("preInfusionPressure", 4))
            ti = int(self.cfg.get("preInfusionTime", 6))
            br = int(self.cfg.get("breakTime", 5))
            if pid == "preinfusion_switch":
                sw = value
            elif pid == "preinfusion_pressure":
                pr = value
            elif pid == "preinfusion_time":
                ti = value
            elif pid == "break_time":
                br = value
            words = [pack112(sw, pr), pack113(ti, br)]
            sub, n = 112, 2
            if pid == "head_temp":
                words, sub, n = [value], 114, 1
        else:
            sub, n = wr["sub"], wr["len"]
            words = [0] * n
            off = wr.get("offset", 0)
            words[off] = value
            if pid == "grind_mode":
                words = [value, 0]            # sub2 len2 jf. parity_spec (word3=0)
        ok = self.wq_push(sub, words)
        if not ok:
            self.save_state = {"state": "fail", "param": pid, "value": value}
        return ok

    def toast_set(self, text, kind):
        self.toast = {"text": text, "kind": kind, "until": self.now + 3.0}

    def link_up(self):
        return self.now - self.last_rx < LINK_TIMEOUT

    # ------------------------------------------------------------ touch
    def touch_event(self, typ, x, y):
        if typ == "down":
            self.tch_down = True
            self.tch_x, self.tch_y, self.tch_t0 = x, y, self.now
            self.tch_btn = self.hit_button(x, y)
        elif typ == "up":
            if self.tch_down and self.tch_btn:
                held_ms = (self.now - self.tch_t0) * 1000
                btn = self.tch_btn
                if btn == "settings":
                    self.page = "settings"
                elif btn in BUTTONS and held_ms >= HOLD_START_MS:
                    self.act_push(btn)
                elif btn in BUTTONS:
                    self.toast_set("HOLD ≥650 ms FOR START", "warn")
            self.tch_down = False
            self.tch_btn = None

    def hit_button(self, x, y):
        for name, (bx, by, br) in BUTTONS.items():
            if (x - bx) ** 2 + (y - by) ** 2 <= br * br:
                return name
        bx, by, br = SETTINGS_BTN
        if (x - bx) ** 2 + (y - by) ** 2 <= br * br:
            return "settings"
        return None

    def inp_read(self):
        if self.touch_fd is None:
            return
        try:
            data = os.read(self.touch_fd, 4096)
        except BlockingIOError:
            return
        except OSError:
            return
        pend_down = pend_up = False
        x = y = None
        for off in range(0, len(data) - 15, 16):
            sec, usec, typ, code, val = EV_FMT.unpack_from(data, off)
            if typ == EV_ABS:
                if code in (ABS_X, ABS_MT_POSITION_X):
                    x = val * 480 // 4096
                elif code in (ABS_Y, ABS_MT_POSITION_Y):
                    y = val * 480 // 4096
                elif code == ABS_MT_TRACKING_ID:
                    if val >= 0:
                        pend_down = True
                    else:
                        pend_up = True
            elif typ == EV_KEY and code == BTN_TOUCH:
                if val:
                    pend_down = True
                else:
                    pend_up = True
            elif typ == EV_SYN and code == SYN_REPORT:
                if pend_down and not self.tch_down:
                    self.touch_event("down", x if x is not None else self.tch_x,
                                     y if y is not None else self.tch_y)
                if pend_up and self.tch_down:
                    self.touch_event("up", self.tch_x, self.tch_y)
                pend_down = pend_up = False

    # ------------------------------------------------------------ fb-rendering
    # Rendereren er en simpel syntetisk emulering (IKKE ourui's rigtige UI):
    # cirkel-clip r234, fase-ring + baggrund, page-region (top), knapper, toast.
    # Base-frames caches pr. (fase, page) så kun små overlays tegnes pr. frame.
    def _build_base(self, ph, page):
        bg = rgb2bgra(*PHASE_BG.get(ph, (20, 20, 24)))
        ring = rgb2bgra(*PHASE_RGB.get(ph, (128, 128, 128)))
        page_c = rgb2bgra(*PAGE_RGB.get(page, (40, 40, 48)))
        black = bytes(4)
        fb = bytearray(FB_BYTES)
        cx = cy = 240
        r_safe = 234
        r_in = r_safe - 7
        for y in range(H):
            dy = y - cy + 0.5
            row = y * W * 4
            # række-spans: udenfor | ring | indre
            dx_max = int((r_safe * r_safe - dy * dy) ** 0.5) if abs(dy) < r_safe else -1
            dx_in = int((r_in * r_in - dy * dy) ** 0.5) if abs(dy) < r_in else -1
            for x in range(W):
                o = row + x * 4
                dx = x - cx + 0.5
                if abs(dx) > dx_max:
                    fb[o:o + 4] = black
                elif abs(dx) > dx_in:
                    fb[o:o + 4] = ring
                elif y < 44:
                    fb[o:o + 4] = page_c
                else:
                    fb[o:o + 4] = bg
        return bytes(fb)

    def _circle_offsets(self, bx, by, br, margin=8):
        offs = []
        cx = cy = 240
        lim = (234 - margin) ** 2
        for y in range(max(0, by - br), min(H, by + br)):
            for x in range(max(0, bx - br), min(W, bx + br)):
                if (x - bx) ** 2 + (y - by) ** 2 <= br * br:
                    if (x - cx + 0.5) ** 2 + (y - cy + 0.5) ** 2 <= lim:
                        offs.append((y * W + x) * 4)
        return offs

    def render(self):
        t_start = time.monotonic()
        ph = phase_of(self.regs, self.link_up())
        page = "off" if self.pw_state != "on" else self.page   # v6.14: sluk-side
        key = (ph, page)
        if not hasattr(self, "_base_cache"):
            self._base_cache = {}
            self._btn_offs = {n: self._circle_offsets(bx, by, br)
                              for n, (bx, by, br) in BUTTONS.items()}
            self._set_offs = self._circle_offsets(*SETTINGS_BTN)
            self._toast_offs = []
            for y in range(395, 415):
                for x in range(120, 360):
                    if (x - 239.5) ** 2 + (y - 239.5) ** 2 <= 226 ** 2:
                        self._toast_offs.append((y * W + x) * 4)
        if key not in self._base_cache:
            self._base_cache[key] = self._build_base(ph, self.page)
        fb = bytearray(self._base_cache[key])
        if page == "home":
            btn_rgb = {"grind": (174, 228, 112), "brew": (90, 160, 255),
                       "water": (110, 200, 230), "steam": (220, 220, 240)}
            for name, offs in self._btn_offs.items():
                rgb = btn_rgb[name]
                c = rgb2bgra(*rgb) if self.activity_pending == name else \
                    rgb2bgra(*(v // 3 for v in rgb))
                for o in offs:
                    fb[o:o + 4] = c
            c = rgb2bgra(200, 180, 120)
            for o in self._set_offs:
                fb[o:o + 4] = c
        if self.toast and self.now < self.toast["until"]:
            tc = {"ok": (98, 222, 168), "err": (255, 96, 96),
                  "warn": (255, 186, 84)}.get(self.toast["kind"], (200, 200, 200))
            c = rgb2bgra(*tc)
            for o in self._toast_offs:
                fb[o:o + 4] = c
        self.fb = fb
        self.frame_ms = (time.monotonic() - t_start) * 1000
        if self.fb_path and self.now - self.last_fb_write >= 0.1:
            self.fb_dump(self.fb_path)
            self.last_fb_write = self.now

    def fb_dump(self, path):
        p = Path(path)
        tmp = p.with_suffix(p.suffix + ".tmp")
        tmp.write_bytes(bytes(self.fb))
        os.replace(str(tmp), str(p))

    # ------------------------------------------------------------ state
    def emit_state(self):
        if not self.state_f:
            return
        ph = phase_of(self.regs, self.link_up())
        # v6.6: spejler nu også trigger-/flag-/wand-ordene (3/6/9/16/19/22/24/
        # 44/46) som ourui eksporterer — scenariernes word-checks måler dermed
        # ægte værdier i stedet for runnerens manglende-nøgle=0 (additivt).
        shown = [0, 1, 3, 4, 6, 9, 11, 14, 16, 17, 19, 22, 23, 24, 25, 33,
                 34, 35, 36, 38, 39, 40, 42, 44, 46, 47, 48, 50, 51, 52, 58,
                 112, 113, 114]
        st = {
            "t_ms": int(self.now * 1000),
            "page": "off" if self.pw_state != "on" else self.page,
            "view": "bye" if self.view_bye else "main",
            "phase": ph,
            "work_sta": self.regs[33],
            "regs": {str(w): self.regs[w] for w in shown},
            "regs_s": {str(w): s16(self.regs[w]) for w in (38, 39)},
            "toast": self.toast if self.toast and self.now < self.toast["until"] else None,
            "lang": self.lang,
            "boot_flag": self.boot_flag(),
            "config_missing": self.config_missing,
            "config_oor": self.config_oor,
            "link_up": self.link_up(),
            "crc_ok": self.rx_ok, "crc_bad": self.rx_bad,
            "cmd6": {"ok": self.c6_ok, "retx": self.c6_retx,
                     "fail": self.c6_fail, "block": self.c6_block},
            "save": self.save_state,
            "activity_pending": self.activity_pending,
            "stop": {"pending": self.stop_pending, "last": self.stop_last,
                     "req": self.stop_req, "ok": self.stop_ok,
                     "fail": self.stop_fail},
            "power": {"state": self.pw_state, "backlight_off": False,
                      **self.pw_n},
            "frame_ms": round(self.frame_ms, 2),
            "boot_pushed": self.boot_pushed,
            "client": "reference",
        }
        self.state_f.write(json.dumps(st, ensure_ascii=False) + "\n")

    # ------------------------------------------------------------ ctrl
    def ctrl_dispatch(self, req):
        cmd = req.get("cmd")
        if cmd == "ping":
            return {"ok": True, "t": self.now, "page": self.page,
                    "phase": phase_of(self.regs, self.link_up())}
        if cmd == "action":
            return {"ok": self.act_push(req.get("name", ""))}
        if cmd == "key":
            # v6.14: tænd/sluk-knappen (samme krog som ourui's SIM-build):
            # {"cmd":"key","value":1|0[,"code":116]}
            v = int(req.get("value", -1))
            if v not in (0, 1):
                return {"ok": False, "error": "value skal vaere 0 eller 1"}
            self.pw_key(v)
            return {"ok": True, "power": self.pw_state}
        if cmd == "raw_write":
            # test-hook: emulerer stock-UI writes (fx MachSleep sub1=[1])
            return {"ok": self.wq_push(int(req.get("sub", 0)),
                                       [int(w) for w in req.get("words", [])])}
        if cmd == "save":
            return {"ok": self.save_param(req.get("param", ""), req.get("value", 0))}
        if cmd == "page":
            self.page = req.get("name", "home")
            return {"ok": True}
        if cmd == "touch":
            x, y, ms = int(req.get("x", 240)), int(req.get("y", 240)), int(req.get("ms", 80))
            self.touch_event("down", x, y)
            if ms < HOLD_START_MS:
                self.touch_event("up", x, y)
            else:
                # simuleret hold: down nu, up efter ms via intern timer
                self._hold_up_at = self.now + ms / 1000.0
            return {"ok": True}
        if cmd == "boot_flag":
            return {"ok": self.set_boot_flag(bool(req.get("value")))}
        if cmd == "runtime_switch":
            self.view_bye = True
            self.page = "bye"
            self.toast_set("SKIFT TIL ORIGINAL NU", "warn")
            if os.environ.get("OURUI_SWITCH_DRY"):
                self._bye_back_at = self.now + 1.0
                return {"ok": True, "dry": True}
            self._exit_at = self.now + 0.3
            return {"ok": True, "dry": False}
        if cmd == "config_reload":
            self.load_config()
            return {"ok": True, "oor": self.config_oor}
        if cmd == "fb_dump":
            self.fb_dump(req.get("path", str(self.fb_path)))
            return {"ok": True}
        if cmd == "state_now":
            self.emit_state()
            return {"ok": True}
        if cmd == "layout":
            # v6.5-scenarios: samme krog-form som ourui's SIM-build (CONTRACT
            # TILLÆG v6.4 B), men med REFERENCE-layoutet (BUTTONS ovenfor) —
            # ikke ourui's rigtige geometri. Bruges af scenariernes touch-
            # hændelser med "layout"-navn i stedet for syntetiske koordinater.
            targets = [{"name": n, "x": bx, "y": by, "r": br}
                       for n, (bx, by, br) in BUTTONS.items()]
            bx, by, br = SETTINGS_BTN
            targets.append({"name": "settings", "x": bx, "y": by, "r": br})
            return {"ok": True, "page": self.page, "targets": targets}
        if cmd == "exit":
            self.exit_code = int(req.get("code", 0))
            self.running = False
            return {"ok": True}
        return {"ok": False, "error": f"ukendt cmd {cmd}"}

    def ctrl_io(self):
        if self.ctrl_listener:
            try:
                conn, _ = self.ctrl_listener.accept()
                conn.setblocking(False)
                self.ctrl_clients[conn.fileno()] = {"sock": conn, "buf": b""}
            except BlockingIOError:
                pass
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
                    c["sock"].sendall((json.dumps(resp) + "\n").encode())
                except OSError:
                    pass

    # ------------------------------------------------------------ hoved-loop
    def run(self):
        self._hold_up_at = None
        self._bye_back_at = None
        self._exit_at = None
        # vent på 0x89 → versions-svar, så boot-push (sim rækkefølge)
        self.next_poll = 0.3
        self.next_ext = 1.0
        while self.running:
            self.now = time.monotonic() - self.t0
            rfds = [self.ufd] + list(self.ctrl_clients)
            if self.ctrl_listener:
                rfds.append(self.ctrl_listener.fileno())
            if self.touch_fd is not None:
                rfds.append(self.touch_fd)
            try:
                r, _, _ = select.select(rfds, [], [], 0.005)
            except (OSError, ValueError):
                r = []
            for fd in r:
                if self.ctrl_listener and fd == self.ctrl_listener.fileno():
                    self.ctrl_io()
                elif fd in self.ctrl_clients:
                    self.ctrl_io()
                elif fd == self.ufd:
                    try:
                        data = os.read(self.ufd, 32)   # ≤32 B chunks (fuart_thread)
                    except BlockingIOError:
                        data = b""
                    except OSError:
                        data = b""
                    if data:
                        self.rx_feed(data)
                elif self.touch_fd is not None and fd == self.touch_fd:
                    self.inp_read()
            self.ctrl_io()
            if not self.boot_pushed and self.now >= 0.15:
                self.boot_push()
            if self.version_sent or self.now > 2.0:
                if self.now >= self.next_poll:
                    self.next_poll = self.now + POLL_INTERVAL
                    sub, n = POLL_ROTATION[self.poll_idx % len(POLL_ROTATION)]
                    self.poll_idx += 1
                    self.frame_query(sub, n)
                if self.now >= self.next_ext:
                    self.next_ext = self.now + EXT_INTERVAL
                    self.frame_query(112, 3)
            if self.boot_pushed and self.now >= self.next_awake \
                    and self.pw_state == "on":    # v6.14: keep-awake pause i dvale
                if self.wq is None:
                    self.wq_push(1, [0])          # keep-awake word1=0 (v6-fix)
                    self.next_awake = self.now + KEEPALIVE_INTERVAL
                else:
                    self.next_awake = self.now + 0.5   # prøv igen snart (≤30 s-gap)
            self.wq_step()
            self.pw_step()                        # v6.14: tænd/sluk-knappen
            self.inp_read()
            if self._hold_up_at and self.now >= self._hold_up_at:
                self._hold_up_at = None
                self.touch_event("up", self.tch_x, self.tch_y)
            if self._bye_back_at and self.now >= self._bye_back_at:
                self._bye_back_at = None
                self.view_bye = False
                self.page = "home"
            if self._exit_at and self.now >= self._exit_at:
                self.running = False
            # aktivitet færdig → toast + pending ryddes (først EFTER set-fase set)
            ph = phase_of(self.regs, self.link_up())
            if self.activity_pending:
                if ph in ("GRIND", "PRE", "EXT", "WATER", "STEAM"):
                    self.activity_seen = True
                elif getattr(self, "activity_seen", False) and \
                        ph in ("READY", "SLEEP", "ERROR", "OFFLINE"):
                    self.toast_set(f"FÆRDIG: {self.activity_pending.upper()}", "ok")
                    self.activity_pending = None
                    self.activity_seen = False
            # v6.13: stop-bekræftelse (ourui stop_step): først når MCU'en har
            # ryddet bits'ene — et ACK alene tæller ikke. 5 s uden clear = fail.
            if self.stop_pending:
                if not self._stop_running(self.stop_pending):
                    self.stop_ok += 1
                    self.toast_set(f"{self.stop_pending[:-5].upper()} STOPPED", "ok")
                    self.stop_pending = None
                elif self.now - self.stop_t0 >= 5.0:
                    self.stop_fail += 1
                    self.toast_set("STOP NOT CONFIRMED · TRY AGAIN", "warn")
                    self.stop_pending = None
            if self.now >= self.next_render:
                self.render()
                self.emit_state()
                self.next_render = self.now + 0.1
        if self.state_f:
            self.emit_state()
            self.state_f.close()
        if self.fb_path:
            self.fb_dump(self.fb_path)
        os.close(self.ufd)
        if self.touch_fd is not None:
            os.close(self.touch_fd)
            os.close(self.touch_wfd)
        if self.ctrl_listener:
            self.ctrl_listener.close()
            Path(self.args.ctrl).unlink(missing_ok=True)
        self.log(f"exit {self.exit_code}")
        return self.exit_code


def main():
    ap = argparse.ArgumentParser(description="Reference-klient til mcu_sim (E2E)")
    ap.add_argument("--tty", default=os.environ.get("OURUI_SIM_TTY"))
    ap.add_argument("--state", default=os.environ.get("OURUI_SIM_STATE"))
    ap.add_argument("--fb", default=os.environ.get("OURUI_SIM_FB"))
    ap.add_argument("--touch", default=os.environ.get("OURUI_SIM_TOUCH"))
    ap.add_argument("--ctrl", default=os.environ.get("OURUI_SIM_CTRL"))
    ap.add_argument("--config", default=os.environ.get("OURUI_CONFIG",
                    str(proto.BASELINE_CONFIG)))
    ap.add_argument("--root", default=os.environ.get("OURUI_SIM_ROOT",
                    os.environ.get("OURUI_ROOT")))
    ap.add_argument("--spec")
    args = ap.parse_args()
    if not args.tty:
        print("OURUI_SIM_TTY/--tty mangler", file=sys.stderr)
        sys.exit(2)
    try:
        client = RefClient(args)
    except OSError as e:
        print(f"init-fejl: {e}", file=sys.stderr)
        sys.exit(3)
    try:
        sys.exit(client.run())
    except KeyboardInterrupt:
        sys.exit(130)


if __name__ == "__main__":
    main()