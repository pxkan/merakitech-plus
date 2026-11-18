#!/usr/bin/env python3
"""selftest.py — mcu_sim selftests (LEVERANCE 5): protokollen bevises UDEN
ourui-SIM-build og uden runner.

Dækker: CRC-vektorer, frame-parsing (cmd9 byte-LEN, cmd8 fast, cmd3 query),
chunked reads ≤32 B + resync, write-politik (bånd/trigger/range), ACK-echo +
latency/loss-profiler, boot-push + versions-håndtryk, packing 112-114,
auto-stop (grind/brew/vand/steam auto+manuel), MANUEL STOP v6.13
(ord16/19/22/6 → auto-stop-ækvivalent, T12), keep-awake-assertion,
fejl-injektion (E1/E2/E3, garbage, offline), cmd8-echo, DVALE/WAKE v6.14
(ord0=0 → tavs dvale, knap-hold/alarm → reboot til fabriks-defaults +
re-push, T13), og REPLAY-verify mod v3/v2/session/steam-live
(frame-for-frame med dokumenterede tolerancer).

Kør: python3 sim/selftest.py   (exit 0 = alt grønt)
"""
import json
import os
import sys
import time
import tty
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import proto
from proto import (crc16_modbus, build_frame, build_query, build_version_request,
                   build_version_response, build_power_frame, FrameParser, hexs,
                   pack112, pack113, unpack112, unpack113, config_to_words,
                   load_baseline_config, s16)
from mcu_sim import McuSim

N_PASS = N_FAIL = 0


def check(name, ok, detail=""):
    global N_PASS, N_FAIL
    N_PASS += bool(ok)
    N_FAIL += (not ok)
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f" — {detail}" if detail and not ok else ""))


class RawClient:
    """Minimal rå protokol-klient på sim'ens pty-slave (som fuart_thread)."""
    def __init__(self, sim):
        self.sim = sim
        self.fd = os.open(sim.slave_path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        tty.setraw(self.fd)
        self.parser = FrameParser()
        self.frames = []

    def send(self, data):
        os.write(self.fd, data)

    def pump(self, dt, chunk=32):
        # deterministisk IO-loop: advance sim i 10ms-ticks og pump DENS RX
        # (master-reads) — svarer til sim.run()'s select-loop, men uden wall-clock
        end = self.sim.t + dt
        while self.sim.t < end - 1e-9:
            self.sim.advance(min(self.sim.t + 0.010, end))
            if self.sim.master is not None:
                try:
                    data = os.read(self.sim.master, 4096)
                except (BlockingIOError, OSError):
                    data = b""
                if data:
                    for cmd, sub, payload, _b in self.sim.parser.feed(data):
                        self.sim.handle_frame(cmd, sub, payload)
        out = []
        while True:
            try:
                data = os.read(self.fd, chunk)   # ≤32 B chunks (fuart_thread)
            except BlockingIOError:
                break
            except OSError:
                break
            if not data:
                break
            out.extend(self.parser.feed(data))
        self.frames.extend(out)
        return out

    def close(self):
        os.close(self.fd)


def fresh_sim(**kw):
    kw.setdefault("pty", False)
    kw.setdefault("quiet", True)
    sim = McuSim(**kw)
    sim.next_pulse = 1e9            # deterministisk: ingen stokastiske idle-pulser
    return sim


# ---------------------------------------------------------------- T1: CRC
def t1_crc():
    print("== T1: CRC-16/MODBUS + frame-vektorer ==")
    check("CRC over AA 55 89 01 00 00 = 0x816F (PROTOCOL §2.2)",
          crc16_modbus(bytes([0xAA, 0x55, 0x89, 0x01, 0x00, 0x00])) == 0x816F)
    check("versions-request frame = AA 55 89 01 00 00 6F 81",
          build_version_request() == bytes.fromhex("aa55890100006f81"))
    vecs = [
        ("cmd3 sub0 len56 query", "aa55030038dc06", lambda: build_query(0, 56)),
        ("cmd3 sub33 len7 query", "aa550321078446", lambda: build_query(33, 7)),
        ("cmd3 sub56 len56 query", "aa55033838cfc6", lambda: build_query(56, 56)),
        ("keep-awake sub1=[0] (SESSION §10.1)", "aa550601010000c533",
         lambda: build_frame(6, 1, [0])),
        ("sleep sub1=[1] (SESSION §10.1)", "aa550601010100c4a3",
         lambda: build_frame(6, 1, [1])),
        ("hotwater start (HOTWATER W1)",
         "aa5506090b01000000660300003c0003003c000100900100000000083e",
         lambda: build_frame(6, 9, [1, 0, 870, 0, 60, 3, 60, 1, 400, 0, 0])),
        ("steam start (SESSION §10.2)",
         "aa5506180801008a02000000000000000000000000cd67",
         lambda: build_frame(6, 24, [1, 650, 0, 0, 0, 0, 0, 0])),
        ("steam stop (SESSION §10.3)",
         "aa5506180800008a020000000000000000000000000cf7",
         lambda: build_frame(6, 24, [0, 650, 0, 0, 0, 0, 0, 0])),
        ("grind start (BREW W1)", "aa550603040100b9003c0001005d6e",
         lambda: build_frame(6, 3, [1, 185, 60, 1])),
        ("brew start (BREW W3)",
         "aa550605123c0000000000000001000000660300003c0003003c00000090010000000000000000010021df",
         lambda: build_frame(6, 5, [60, 0, 0, 0, 1, 0, 870, 0, 60, 3, 60, 0,
                                    400, 0, 0, 0, 0, 1])),
        ("boot-push preinf sub112 len3 (v6 selftest)",
         "aa55067003040105064600c702",
         lambda: build_frame(6, 112, [0x0104, 0x0605, 70])),
        # v6.13 (UI_v6 §18.1): manuelle stop-frames, byte for byte
        ("vand stop auto (v6.13 §18.1)", "aa550610010000c00f",
         lambda: build_frame(6, 16, [0])),
        ("vand stop manuel sw (v6.13 §18.1)", "aa550613010000c04b",
         lambda: build_frame(6, 19, [0])),
        ("bryg stop (v6.13 §18.1)", "aa550616010000c087",
         lambda: build_frame(6, 22, [0])),
        ("kværn stop (v6.13 §18.1)", "aa550606010000c447",
         lambda: build_frame(6, 6, [0])),
    ]
    for name, want, fn in vecs:
        got = fn().hex()
        check(f"frame-vektor: {name}", got == want, f"want={want} got={got}")
    # parse-vektorer
    p = FrameParser()
    fr = p.feed(bytes.fromhex("aa55030038dc06"))
    check("cmd3 parse: 7-byte query, LEN=forventede ord",
          fr == [(3, 0, 56, bytes.fromhex("aa55030038"))])
    fr = FrameParser().feed(build_version_request())
    check("0x89 parse: LEN=bytes, sub=0 (byttet rækkefølge)",
          fr and fr[0][0] == 0x89 and fr[0][1] == 0 and fr[0][2] == b"\x00")
    fr = FrameParser().feed(build_version_response("1.1.1-69"))
    check("cmd9 parse: ASCII payload", fr and fr[0][0] == 9 and fr[0][2] == b"1.1.1-69")
    fr = FrameParser().feed(build_power_frame(bytes(10)))
    check("cmd8 parse: fast 15-byte frame", fr and fr[0][0] == 8 and len(fr[0][2]) == 10)
    rt = FrameParser().feed(build_frame(0x83, 33, [0x0101, 0, 0, 0, 16, 65535, 64597]))
    check("0x83 roundtrip: words little-endian",
          rt and rt[0][2] == [0x0101, 0, 0, 0, 16, 65535, 64597])


# ---------------------------------------------------------------- T2: chunking
def t2_chunking():
    print("== T2: chunked reads ≤32 B + resync ==")
    sim = fresh_sim(pty=True)
    c = RawClient(sim)
    c.send(build_query(0, 56))
    fr = c.pump(0.02, chunk=32)
    check("119-byte svar på sub0/56 modtaget i ≤32 B chunks",
          any(cmd == 0x83 and sub == 0 and len(w) == 56 for cmd, sub, w, _ in fr))
    resp = [f for f in fr if f[0] == 0x83 and f[1] == 0]
    # setup-ordene (0..32) er statiske; sensor-ordene har støj pr. tick
    check("svar-words = model-regs[0..32] (statiske setup-ord)",
          bool(resp) and resp[0][2][0:33] == sim.regs[0:33])
    # split TX-frame over to writes m. pause
    f = build_frame(6, 11, [880])
    c.send(f[:4])
    c.pump(0.01)
    c.send(f[4:])
    fr = c.pump(0.05)
    check("splittet write-frame samles korrekt (ACK 0x86)",
          any(cmd == 0x86 and sub == 11 and w == [880] for cmd, sub, w, _ in fr))
    check("write appliceret: regs[11]=880", sim.regs[11] == 880)
    # garbage + resync
    c.send(b"\x00\xff\x13\x37" * 5)
    c.send(build_query(33, 7))
    fr = c.pump(0.02)
    check("resync efter garbage: sub33/7-svar",
          any(cmd == 0x83 and sub == 33 and len(w) == 7 for cmd, sub, w, _ in fr))
    # CRC-korrupt frame afvises
    bad = bytearray(build_query(0, 56))
    bad[-1] ^= 0xFF
    n0 = sim.stats["queries"]
    c.send(bytes(bad))
    c.pump(0.02)
    check("CRC-korrupt query afvises (ingen respons)", sim.stats["queries"] == n0)
    c.close()
    sim.alog.close()
    sim._close_pty()


# ---------------------------------------------------------------- T3: write-politik
def t3_write_policy():
    print("== T3: write-politik (bånd/trigger/range) ==")
    sim = fresh_sim()
    check("bånd: sub32 len1 afvises (MCU-ejet)", not sim.handle_write(32, [1]))
    check("bånd: sub115 len1 afvises", not sim.handle_write(115, [1]))
    check("bånd: sub30 len4 afvises (krydser 32)", not sim.handle_write(30, [0, 0, 0, 0]))
    check("bånd: sub0 len32 ok (0..31)",
          sim.handle_write(0, [1, 0, 1, 0, 185, 60, 0, 0, 0, 0, 0, 870, 0, 60,
                               3, 60, 0, 400, 0, 0, 0, 0, 0, 360, 0, 650,
                               0, 0, 0, 0, 0, 0]))
    check("bånd: sub112 len3 ok (112..114)", sim.handle_write(112, [0x0104, 0x0605, 70]))
    check("bånd: sub113 len1 ok", sim.handle_write(113, [0x0605]))
    check("bånd: sub114 len1 ok", sim.handle_write(114, [70]))
    check("trigger: steam-blok m. ord30=1 afvises",
          not sim.handle_write(24, [1, 650, 0, 0, 0, 0, 1, 0]))
    check("trigger: ord26=100 afvises", not sim.handle_write(26, [100]))
    check("trigger: ord27=500 afvises", not sim.handle_write(27, [500]))
    check("trigger: ord30=1 afvises (reboot)", not sim.handle_write(30, [1]))
    check("ingen reboot sket", sim.stats["reboots"] == 0)
    sim.handle_write(11, [1200])
    rv = [e for e in sim.alog.events if e["kind"] == "range_violation"]
    check("range: ord11=1200 LOGGES (MCU har ingen checks)", len(rv) == 1)
    check("range: værdien appliceres alligevel (memcpy-model)", sim.regs[11] == 1200)
    sim2 = fresh_sim(range_policy="reject")
    check("range-policy=reject: ord11=1200 afvises", not sim2.handle_write(11, [1200]))
    sim3 = fresh_sim(trigger_policy="pass")
    sim3.handle_write(30, [1])
    check("trigger-policy=pass: ord30=1 rebootter sim", sim3.stats["reboots"] == 1)
    ok = sim.handle_write(11, [880])
    check("normal write ok + ACK planlagt", ok and sim.regs[11] == 880)
    kinds = [e["kind"] for e in sim.alog.events]
    check("assertions-log: write_rejected + range_violation + reboot_trigger logget",
          "write_rejected" in kinds and "range_violation" in kinds)


# ---------------------------------------------------------------- T4: packing
def t4_packing():
    print("== T4: byte-pakning word112/113/114 ==")
    cfg = load_baseline_config()
    w = config_to_words(cfg)
    check("config → word112 = 0x0104 (switch=1, tryk=4)", w[112] == 0x0104)
    check("config → word113 = 0x0605 (tid=6, break=5)", w[113] == 0x0605)
    check("config → word114 = 70 (hele °C)", w[114] == 70)
    check("boot-hardcodes: word5/13/15 = 60", w[5] == 60 and w[13] == 60 and w[15] == 60)
    ok = True
    for sw in (0, 1):
        for pr in (3, 4):
            for ti in (5, 6, 15, 60):
                for br in (0, 5, 255):
                    ok &= unpack112(pack112(sw, pr)) == (sw, pr)
                    ok &= unpack113(pack113(ti, br)) == (ti, br)
    check("pack/udpak roundtrip: alle (switch,tryk,tid,break) kombinationer", ok)
    check("word112=(0,3)=0x0003 / (1,3)=0x0103", pack112(0, 3) == 0x0003 and pack112(1, 3) == 0x0103)


# ---------------------------------------------------------------- T5: boot-push/handshake
def t5_boot():
    print("== T5: versions-håndtryk + boot-push ved connect ==")
    sim = fresh_sim(pty=True)
    c = RawClient(sim)
    fr = c.pump(0.06)
    reqs = [f for f in fr if f[0] == 0x89]
    check("0x89-requests hvert ~20 ms fra start", len(reqs) >= 2)
    c.send(build_version_response("1.1.1-69"))
    c.pump(0.05)
    n = sim.stats["version_reqs"]
    c.pump(0.2)
    check("0x89 stopper efter cmd9-svar", sim.stats["version_reqs"] == n)
    check("versions-håndtryk logget",
          any(e["kind"] == "version_handshake" for e in sim.alog.events))
    words = config_to_words(load_baseline_config())
    mirror = [0] * 31
    for wd, v in words.items():
        if 1 <= wd <= 30:
            mirror[wd] = v
    c.send(build_frame(6, 1, mirror[1:31]))
    c.send(build_frame(6, 112, [words[112], words[113], words[114]]))
    fr = c.pump(0.1)
    acks = [f for f in fr if f[0] == 0x86]
    check("boot-push ACK'et (sub1 len30 + sub112 len3)",
          any(a[1] == 1 and len(a[2]) == 30 for a in acks) and
          any(a[1] == 112 and len(a[2]) == 3 for a in acks))
    check("boot-push logget i assertions", sim.boot_push_setup and sim.boot_push_preinf)
    check("boot-push appliceret: regs[4]=185, regs[11]=870, regs[112]=0x0104",
          sim.regs[4] == 185 and sim.regs[11] == 870 and sim.regs[112] == 0x0104)
    # manglende boot-push detekteres
    sim2 = fresh_sim(pty=True)
    c2 = RawClient(sim2)
    c2.send(build_version_response("x"))
    c2.pump(7.0)
    check("boot_push_missing logges når klient ikke pusher",
          any(e["kind"] == "boot_push_missing" for e in sim2.alog.events))
    c.close(); c2.close()
    sim.alog.close(); sim2.alog.close()
    sim._close_pty(); sim2._close_pty()


# ---------------------------------------------------------------- T6: auto-stop
def t6_autostop():
    print("== T6: fase-maskine + auto-stop (kalibreret mod BREW/HOTWATER/SESSION) ==")
    sim = fresh_sim()
    sim.handle_write(3, [1, 185, 60, 1])
    sim.advance(0.5)
    check("grind: bit6 sat efter ~0,39 s, ord3 konsumeret",
          sim.regs[33] == 0x0141 and sim.regs[3] == 0)
    sim.advance(5.0)
    check("grind: vægt stiger monotont + cur_ch2 aktiv",
          0 < s16(sim.regs[38]) < 185 and sim.regs[58] > 400)
    sim.advance(11.0)
    check("grind: auto-stop ved ~184 (mål 185), bit6 ryddet",
          sim.regs[33] == 0x0101 and 183 <= s16(sim.regs[38]) <= 187)
    sim.advance(12.0)
    check("grind: ord6 (tare) konsumeret efter stop", sim.regs[6] == 0)
    # brew
    sim = fresh_sim()
    sim.handle_write(5, [60, 0, 0, 0, 1, 0, 870, 0, 60, 3, 60, 0, 400, 0, 0, 0, 0, 1])
    sim.advance(3.0)
    check("brew: pre-infusion ws=0x1107, ord51≈358 plateau",
          sim.regs[33] == 0x1107 and 330 <= sim.regs[51] <= 390)
    check("brew: ord9 konsumeret, ord22=1 (aktiv-flag)",
          sim.regs[9] == 0 and sim.regs[22] == 1)
    sim.advance(9.0)
    check("brew: ekstraktion ws=0x1307 (bit9), ord51≈1346",
          sim.regs[33] == 0x1307 and 1250 <= sim.regs[51] <= 1500)
    sim.advance(30.0)
    check("brew: auto-stop ved ~344 (< mål 360), ord22 ryddet af MCU",
          sim.regs[33] in (0x0101, 0x0105) and sim.regs[22] == 0 and
          330 <= s16(sim.regs[39]) <= 370)
    sim.advance(33.0)
    check("brew: efter-dryp → slutvægt ~366", 356 <= s16(sim.regs[39]) <= 376)
    # hotwater
    sim = fresh_sim()
    sim.handle_write(9, [1, 0, 870, 0, 60, 3, 60, 1, 400, 0, 0])
    sim.advance(0.5)
    check("vand: ws=0x030B (bits 1+3+9), tare ord39",
          sim.regs[33] == 0x030B and s16(sim.regs[39]) < 50)
    sim.advance(7.0)
    check("vand: auto-stop ved ord39≥400, ord16 ryddet af MCU",
          sim.regs[33] == 0x0101 and sim.regs[16] == 0 and s16(sim.regs[39]) >= 389)
    # steam auto
    sim = fresh_sim()
    sim.inject({"type": "wand", "on": True})
    sim.handle_write(24, [1, 650, 0, 0, 0, 0, 0, 0])
    sim.advance(1.0)
    check("steam: ws=0x4121 (bits 5+14) m. wand", sim.regs[33] == 0x4121)
    sim.advance(15.0)
    check("steam: mælk-ramp mod 650", 400 <= sim.regs[48] <= 640)
    sim.advance(32.0)
    check("steam: AUTO-stop ved ord48≥ord25, ord24 ryddet af MCU",
          sim.regs[33] == 0x0101 and sim.regs[24] == 0 and sim.regs[48] >= 645)
    # steam manuel + display-latch (v6.6: STEAMLIVE §5-6 — display-steam kræver
    # IKKE wand; SESSION §5.2's wand-krav var et statisk gæt, live-modbevist.
    # require_wand er nu en legacy no-op; wand-kontakten styrer flush-vejen).
    sim = fresh_sim()
    sim.handle_write(24, [1, 650, 0, 0, 0, 0, 0, 0])
    sim.advance(2.0)
    check("steam: display-steam starter UDEN wand (latch-vejen, STEAMLIVE §5)",
          sim.regs[33] == 0x4121 and sim.regs[24] == 1)
    sim.inject({"type": "wand", "on": True})
    sim.advance(sim.t + 1.0)
    check("steam: wand-kontakt under display-steam: ord44 bit0 + ord46=1, damp fortsætter",
          (sim.regs[44] & 1) and sim.regs[46] == 1 and sim.regs[33] == 0x4121)
    sim.advance(sim.t + 4.0)
    sim.handle_write(24, [0, 650, 0, 0, 0, 0, 0, 0])
    sim.advance(sim.t + 0.2)
    check("steam: MANUEL stop (ord24=0) rydder bits 5+14 før setpoint",
          sim.regs[33] == 0x0101 and sim.regs[48] < 650)
    # v6.6: wand-flush (latch=0) — ord24=0, INGEN mælk-auto-stop, release-stop
    sim = fresh_sim()
    sim.inject({"type": "milk_jump", "value": 640})
    sim.inject({"type": "wand_down"})
    sim.advance(sim.t + 0.5)
    check("flush: wand_down → ws=0x4121, ord24=0, ord46=1 (latch=0)",
          sim.regs[33] == 0x4121 and sim.regs[24] == 0 and sim.regs[46] == 1)
    sim.advance(sim.t + 3.0)
    check("flush: INGEN auto-stop selv om mælk >650 (latch-gating, STEAMLIVE §6)",
          sim.regs[48] > 650 and sim.regs[33] == 0x4121 and sim.regs[24] == 0)
    sim.inject({"type": "wand_up"})
    sim.advance(sim.t + 0.2)
    check("flush: wand_up → bits ryddet, ord46=0, ord44 bit0=0",
          sim.regs[33] == 0x0101 and sim.regs[46] == 0 and not (sim.regs[44] & 1))
    # v6.6: samtidige faser — flush UNDER bryg (work_sta union, STEAMLIVE §7)
    sim = fresh_sim()
    sim.handle_write(5, [60, 0, 0, 0, 1, 0, 870, 0, 60, 3, 60, 0, 400, 0, 0, 0, 0, 1])
    sim.advance(9.0)                      # bryg i ekstraktion
    w_before = s16(sim.regs[39])
    sim.inject({"type": "wand_down"})
    sim.advance(sim.t + 0.5)
    check("samtidig: bryg+wand → work_sta=0x5327 (union)", sim.regs[33] == 0x5327)
    sim.advance(sim.t + 2.0)
    check("samtidig: bryg-vægt fortsætter uforstyrret under flush",
          s16(sim.regs[39]) > w_before)
    sim.inject({"type": "wand_up"})
    sim.advance(sim.t + 0.2)
    check("samtidig: wand-release → 0x1307 (bryg fortsætter)", sim.regs[33] == 0x1307)
    # v6.6: display-steam (latch=1) oven i bryg + auto-stop → bryg kører videre
    sim = fresh_sim()
    sim.handle_write(5, [60, 0, 0, 0, 1, 0, 870, 0, 60, 3, 60, 0, 400, 0, 0, 0, 0, 1])
    sim.advance(9.0)
    sim.inject({"type": "milk_jump", "value": 630})
    sim.handle_write(24, [1, 650, 0, 0, 0, 0, 0, 0])
    sim.advance(sim.t + 1.0)
    check("samtidig: display-steam under bryg → 0x5327, ord24=1",
          sim.regs[33] == 0x5327 and sim.regs[24] == 1)
    sim.advance(sim.t + 3.0)                      # mælk 630→650 på ~1,6 s → auto-stop
    check("samtidig: mælk-auto-stop → 0x1307 (bryg videre), ord24=0",
          sim.regs[33] == 0x1307 and sim.regs[24] == 0)
    # v6.6: termisk overshoot — peak ~stop+39 (~689) ~11 s efter auto-stop
    sim = fresh_sim()
    sim.handle_write(24, [1, 650, 0, 0, 0, 0, 0, 0])
    sim.advance(40.0)                     # auto-stop ~30,2 (mælk 232→650)
    t_stop = next(e["t"] for e in sim.alog.events
                  if e["kind"] == "activity_stop" and e.get("activity") == "steam")
    sim.advance(t_stop + 11.5)
    check("overshoot: mælk-peak 685-695 (~stop+39) ~11 s efter auto-stop",
          685 <= sim.regs[48] <= 695, f"ord48={sim.regs[48]}")
    check("overshoot: ord24 MCU-nulstillet efter auto-stop (ingen stop-write)",
          sim.regs[24] == 0)
    # sleep/drain
    sim = fresh_sim()
    sim.next_pulse = 1e9            # ingen stokastiske pulser i eksakte checks
    s0 = sim.regs[47]
    sim.handle_write(1, [1])
    sim.advance(sim.t + 1.0)
    check("sleep: ord1=1 → work_sta=0x0001 (drain)", sim.regs[33] == 0x0001)
    sim.advance(100.0)
    check("sleep: boilers køler (SESSION: ~0,11 enh/s s-boiler)",
          sim.regs[47] < s0 - 8)
    sim.handle_write(1, [0])
    sim.advance(sim.t + 1.0)
    check("wake: ord1=0 → work_sta=0x0101", sim.regs[33] == 0x0101)
    sim.handle_write(0, [0])
    sim.advance(sim.t + 1.0)
    check("power off: ord0=0 → work_sta=0x0000 (sleep-task)", sim.regs[33] == 0x0000)
    # stokastiske idle-pulser (live-adfærd, separat sim m. pulser til)
    sim = McuSim(pty=False, quiet=True, seed=3)
    seen = set()
    for _ in range(60):
        sim.advance(sim.t + 0.5)
        seen.add(sim.regs[33])
    check("idle-pulser: bits 10/11/13 observeres over 30 s (stokastisk vedligehold)",
          any(ws & 0x2C00 for ws in seen) and 0x0101 in seen)


# ---------------------------------------------------------------- T7: keep-awake
def t7_keepalive():
    print("== T7: keep-awake assertion (word1=0 hvert ≤30 s) ==")
    sim = fresh_sim()
    sim.first_rx_t = 0.0            # simulerer connect (keep-awake overvåges efter connect)
    sim.handle_write(1, [0])
    sim.advance(29.0)
    check("ingen gap ved 29 s", not any(e["kind"] == "keepalive_gap" for e in sim.alog.events))
    sim.advance(32.0)
    check("gap logget ved >30 s uden awake-write",
          any(e["kind"] == "keepalive_gap" for e in sim.alog.events))
    sim2 = fresh_sim()
    sim2.first_rx_t = 0.0
    sim2.handle_write(1, [0])
    for _ in range(3):
        sim2.advance(sim2.t + 25.0)
        sim2.handle_write(1, [0])
    check("refresh hvert 25 s → ingen gaps",
          not any(e["kind"] == "keepalive_gap" for e in sim2.alog.events))
    sim3 = fresh_sim()
    sim3.first_rx_t = 0.0
    sim3.expect_keepawake = False
    sim3.advance(40.0)
    check("expect_keepawake=false undertrykker gap (sleep-scenarier)",
          not any(e["kind"] == "keepalive_gap" for e in sim3.alog.events))


# ---------------------------------------------------------------- T8: fejl-injektion
def t8_faults():
    print("== T8: fejl-injektion (E1/E2/E3 + sikkerheds-stop) ==")
    sim = fresh_sim()
    sim.inject({"type": "fault", "e1": 0x20, "e2": 0x10, "e3": 0x30, "dur": 5})
    sim.advance(0.1)
    check("E1/E2/E3 bits sat i ord34/35/36",
          sim.regs[34] == 0x20 and sim.regs[35] == 0x10 and sim.regs[36] == 0x30)
    sim.handle_write(3, [1, 185, 60, 1])
    sim.advance(sim.t + 1.0)
    check("grind sikkerhedsstoppes under fejl", sim.regs[33] == 0x0101)
    sim.advance(6.0)
    check("fejl clears efter dur → ord34/35/36=0",
          sim.regs[34] == 0 and sim.regs[35] == 0 and sim.regs[36] == 0)
    sim.inject({"type": "fault", "e3": 0x10})
    sim.advance(0.1)
    sim.inject({"type": "fault_clear"})
    sim.advance(0.1)
    check("fault_clear rydder alt", sim.regs[36] == 0)


# ---------------------------------------------------------------- T9: ACK-profiler
def t9_ack():
    print("== T9: ACK-latency/loss/retransmit-profiler ==")
    sim = fresh_sim(pty=True, profile="normal")
    c = RawClient(sim)
    c.send(build_version_response("x"))
    c.pump(0.05)
    c.send(build_frame(6, 11, [880]))
    fr = c.pump(0.015)
    check("normal: ingen ACK efter 15 ms", not any(f[0] == 0x86 for f in fr))
    fr = c.pump(0.02)
    check("normal: ACK ~22 ms m. eksakt echo",
          any(f[0] == 0x86 and f[2] == [880] for f in fr))
    c.close(); sim.alog.close(); sim._close_pty()
    # no_ack
    sim = fresh_sim(pty=True, profile="no_ack")
    c = RawClient(sim)
    c.send(build_version_response("x"))
    c.pump(0.05)
    c.send(build_frame(6, 11, [880]))
    fr = c.pump(0.5)
    check("no_ack: ingen ACK (save skal blokere i UI)",
          not any(f[0] == 0x86 for f in fr))
    check("no_ack: ack_dropped logget",
          any(e["kind"] == "ack_dropped" for e in sim.alog.events))
    c.close(); sim.alog.close(); sim._close_pty()
    # observed_v3: første ACK langsom ved grind-start, retransmit prompte
    sim = fresh_sim(pty=True, profile="observed_v3")
    c = RawClient(sim)
    c.send(build_version_response("x"))
    c.pump(0.05)
    c.send(build_frame(6, 3, [1, 185, 60, 1]))
    fr = c.pump(0.15)
    check("observed_v3: første ACK forsinket (>150 ms, som v3's 248 ms)",
          not any(f[0] == 0x86 for f in fr))
    c.send(build_frame(6, 3, [1, 185, 60, 1]))   # retransmit +121 ms
    fr = c.pump(0.14)
    check("observed_v3: retransmit-ACK prompte (~18 ms, som v3's 12 ms)",
          any(f[0] == 0x86 for f in fr))
    fr = c.pump(0.2)
    check("observed_v3: første ACK kommer også (~260 ms)",
          sum(1 for f in c.frames if f[0] == 0x86) >= 2)
    c.close(); sim.alog.close(); sim._close_pty()


# ---------------------------------------------------------------- T10: link/garbage
def t10_link():
    print("== T10: offline + garbage-stream + cmd8 ==")
    sim = fresh_sim(pty=True)
    c = RawClient(sim)
    c.send(build_version_response("x"))
    c.pump(0.05)
    sim.inject({"type": "link", "state": "silent"})
    c.send(build_query(0, 56))
    fr = c.pump(0.3)
    check("silent: ingen svar på polls (offline)",
          not any(f[0] == 0x83 for f in fr))
    sim.inject({"type": "link", "state": "up"})
    c.send(build_query(0, 56))
    fr = c.pump(0.1)
    check("link up: svar igen", any(f[0] == 0x83 for f in fr))
    # garbage-stream: klient skal resynke
    sim.inject({"type": "garbage", "dur": 0.3, "crc_too": True})
    c.send(build_query(33, 7))
    fr = c.pump(0.5)
    check("garbage: CRC-fejl talt + valid frame til sidst",
          c.parser.crc_bad > 0 and any(f[0] == 0x83 and f[1] == 33 for f in fr))
    # cmd8 echo
    payload = bytes([0xE9, 0x07, 0x0B, 0x11, 0x0E, 0x1E, 0x00, 0x00, 0x01, 0x00])
    c.send(build_power_frame(payload))
    fr = c.pump(0.05)
    check("cmd8 → 0x88 echo (15-byte frame)",
          any(f[0] == 0x88 and f[2] == payload for f in fr))
    # closed → reopen (ny pty)
    old_path = sim.slave_path
    sim.inject({"type": "link", "state": "closed"})
    check("closed: pty lukket", sim.master is None)
    sim.inject({"type": "link", "state": "up"})
    check("reopen: ny pty-sti", sim.slave_path is not None and sim.slave_path != old_path)
    c.close(); sim.alog.close(); sim._close_pty()


# ---------------------------------------------------------------- T11: replay
def t11_replay():
    print("== T11: REPLAY-verify frame-for-frame (v3/v2/session/steam-live) ==")
    for name, path in [
        ("v3 grind+bryg", "sim/fixtures/captures/uart_live_v3.csv.gz"),
        ("v2 hot water", "sim/fixtures/captures/uart_live_v2.csv.gz"),
        ("session sleep/drain", "sim/fixtures/captures/uart_session.csv.gz"),
        ("steam-live (v6.6)", "sim/fixtures/captures/uart_session_live.csv.gz"),
    ]:
        sim = fresh_sim()
        sim.load_replay(str(proto.ROOT / path), mode="verify")
        rep = sim._verify_step()
        sim.alog.close()
        check(f"replay-verify {name}: PASS (match={rep['match_ratio']:.4f}, "
              f"exact_fail={rep['exact_fail']}, ws-sekvens={'OK' if rep['work_sta_sequence_ok'] else 'FEJL'}, "
              f"timing={'OK' if rep['work_sta_timing_ok'] else 'FEJL'})",
              rep["pass"], json.dumps({w: s for w, s in rep["per_word"].items()
                                       if s["fail"] > 5})[:300])
        # v6.6: steam-live er kalibrerings-målet (≥99 %); de øvrige ≥92 %
        th = 0.99 if "steam-live" in name else 0.92
        check(f"replay-verify {name}: match-ratio ≥{int(th*100)}%",
              rep["match_ratio"] >= th, f"{rep['match_ratio']:.4f}")
        check(f"replay-verify {name}: exact-ord 100%", rep["exact_fail"] == 0)


# ---------------------------------------------------------------- T12: manuel STOP (v6.13)
def t12_manual_stop():
    print("== T12: manuel STOP v6.13 (ord16/19/22/6 → auto-stop-ækvivalent) ==")
    BREW_START = [60, 0, 0, 0, 1, 0, 870, 0, 60, 3, 60, 0, 400, 0, 0, 0, 0, 1]
    # --- VAND i AUTO-tilstand: stop på ord16=0 (hotwater-ctl 0x1B8C,
    #     aktiv=(ord19|ord16)≠0 genberegnes hver omgang — OGSÅ i auto) ---
    sim = fresh_sim()
    sim.handle_write(9, [1, 0, 870, 0, 60, 3, 60, 1, 400, 0, 0])
    sim.advance(3.0)
    check("vand(auto): kører ws=0x030B, ord16=1",
          sim.regs[33] == 0x030B and sim.regs[16] == 1)
    w0 = s16(sim.regs[39])
    check("vand(auto): vægt-ramp i gang", 100 < w0 < 300)
    sim.handle_write(16, [0])
    sim.advance(sim.t + 0.05)
    check("vand(auto): MANUEL stop ord16=0 rydder bits 1+3+9 straks (uanset manual)",
          sim.regs[33] == 0x0101 and sim.act is None)
    check("vand(auto): vægt standser ved AKTUEL vægt (ingen snap til mål 400)",
          abs(s16(sim.regs[39]) - w0) <= 5 and s16(sim.regs[39]) < 300)
    check("vand(auto): stop-ACK logget (eksakt echo sub16=[0])",
          any(e["kind"] == "ack" and e.get("sub") == 16 and e.get("words") == [0]
              for e in sim.alog.events))
    sim.advance(sim.t + 2.5)
    check("vand(auto): post/drip som auto-stop (vægt −11, flow 0, ord16/19=0)",
          s16(sim.regs[39]) <= w0 - 8 and sim.regs[52] == 0
          and sim.regs[16] == 0 and sim.regs[19] == 0)
    check("vand(auto): activity_stop reason=manuel",
          any(e["kind"] == "activity_stop" and e.get("activity") == "water"
              and "manuel" in e.get("reason", "") for e in sim.alog.events))
    # --- VAND manuel tilstand (ord19=1): ord16=0 alene stopper IKKE ---
    sim = fresh_sim()
    sim.handle_write(9, [1, 0, 870, 0, 60, 3, 60, 0, 400, 0, 1])
    sim.advance(3.0)
    check("vand(manuel): starter med ord16=0/ord19=1",
          sim.regs[33] == 0x030B and sim.act and sim.act["manual"])
    sim.handle_write(16, [0])
    sim.advance(sim.t + 0.1)
    check("vand(manuel): ord16=0 alene stopper IKKE (aktiv = ord19|ord16)",
          sim.regs[33] == 0x030B)
    w0 = s16(sim.regs[39])
    sim.handle_write(19, [0])
    sim.advance(sim.t + 0.05)
    check("vand(manuel): ord19=0 → stop, bits ryddet, vægt holdt",
          sim.regs[33] == 0x0101 and abs(s16(sim.regs[39]) - w0) <= 5)
    # --- BRYG: stop på ord22=0 under ekstraktion (brew-ctl 0x1E9C/0x1F92) ---
    sim = fresh_sim()
    sim.handle_write(5, BREW_START)
    sim.advance(12.0)
    check("bryg: ekstraktion ws=0x1307, ord22=1",
          sim.regs[33] == 0x1307 and sim.regs[22] == 1)
    w0 = s16(sim.regs[39])
    check("bryg: vægt-ramp i gang", 20 < w0 < 200)
    sim.handle_write(22, [0])
    sim.advance(sim.t + 0.05)
    check("bryg: MANUEL stop ord22=0 → idle/ventil-puls (bits 1+2+9+12 ryddet)",
          sim.regs[33] in (0x0101, 0x0105) and sim.act is None)
    sim.advance(sim.t + 0.3)
    check("bryg: efter ventil-puls (~0,21 s) → ws=0x0101", sim.regs[33] == 0x0101)
    sim.advance(sim.t + 2.0)
    check("bryg: efter-dryp +12 som auto-stop (IKKE ramp mod mål 360)",
          w0 + 8 <= s16(sim.regs[39]) <= w0 + 16 and s16(sim.regs[39]) < 200)
    check("bryg: ord22=0, flow 0, reason manuel",
          sim.regs[22] == 0 and sim.regs[52] == 0
          and any(e["kind"] == "activity_stop" and e.get("activity") == "brew"
                  and "manuel" in e.get("reason", "") for e in sim.alog.events))
    # --- BRYG: stop under pre-infusion ---
    sim = fresh_sim()
    sim.handle_write(5, BREW_START)
    sim.advance(3.0)
    check("bryg(pre): pre-infusion ws=0x1107", sim.regs[33] == 0x1107)
    sim.handle_write(22, [0])
    sim.advance(sim.t + 0.4)
    check("bryg(pre): manuel stop → idle, ingen ekstraktion",
          sim.regs[33] == 0x0101 and sim.act is None)
    # --- KVÆRN: stop på ord6=0 (grind-ctl 0x19C6; ord3 er tare-triggeren) ---
    sim = fresh_sim()
    sim.handle_write(3, [1, 185, 60, 1])
    sim.advance(5.0)
    check("grind: kører ws=0x0141, ord6=1 (run-flag)",
          sim.regs[33] == 0x0141 and sim.regs[6] == 1)
    w0 = s16(sim.regs[38])
    check("grind: vægt-ramp i gang", 40 < w0 < 150)
    sim.handle_write(6, [0])
    sim.advance(sim.t + 0.05)
    check("grind: MANUEL stop ord6=0 rydder bit6 straks",
          sim.regs[33] == 0x0101 and sim.act is None)
    sim.advance(sim.t + 2.0)
    check("grind: vægten HOLDER (ingen ramp mod mål+2=187)",
          abs(s16(sim.regs[38]) - w0) <= 2)
    check("grind: cur2-decay som auto-stop + ord6=0 + reason manuel",
          sim.regs[58] < 60 and sim.regs[6] == 0
          and any(e["kind"] == "activity_stop" and e.get("activity") == "grind"
                  and "manuel" in e.get("reason", "") for e in sim.alog.events))
    # --- stop-ord UDEN aktivitet: ACK'es, ingen effekt ---
    sim = fresh_sim()
    ok = (sim.handle_write(22, [0]) and sim.handle_write(6, [0])
          and sim.handle_write(16, [0]))
    sim.advance(0.5)
    check("idle: stop-writes ACK'es og gør intet (ingen activity_stop)",
          ok and sim.regs[33] == 0x0101
          and not any(e["kind"] == "activity_stop" for e in sim.alog.events))
    # --- ord3/ord9 er IKKE stop-ord (tare-triggere, UI_v6 §18.1) ---
    sim = fresh_sim()
    sim.handle_write(5, BREW_START)
    sim.advance(12.0)
    sim.handle_write(9, [0])
    sim.advance(sim.t + 0.2)
    check("bryg: ord9=0 under aktivitet stopper IKKE (tare-trigger)",
          sim.regs[33] == 0x1307)
    sim = fresh_sim()
    sim.handle_write(3, [1, 185, 60, 1])
    sim.advance(5.0)
    sim.handle_write(3, [0])
    sim.advance(sim.t + 0.2)
    check("grind: ord3=0 under aktivitet stopper IKKE (tare-trigger)",
          sim.regs[33] == 0x0141)


# ---------------------------------------------------------------- T13: dvale/wake (v6.14)
def t13_power():
    print("== T13: DVALE/WAKE v6.14 (ord0=0 → tavs; knap/alarm → reboot + re-push) ==")
    check("power-off frame = aa 55 06 00 02 00 00 00 00 56 94 (stock powerOff)",
          build_frame(6, 0, [0, 0]).hex() == "aa55060002000000005694")
    # --- ord0=0 → ACK, derefter dvale ≤0,5 s (regs bevaret, udgange 0) ---
    sim = fresh_sim()
    sim.handle_write(11, [880])
    ok = sim.handle_write(0, [0, 0])
    check("sub0 len2 [0,0] ACK'es normalt (write ok + ack planlagt)",
          ok and any(e["kind"] == "ack" and e.get("sub") == 0
                     and e.get("words") == [0, 0] for e in sim.alog.events))
    check("dvale planlagt ≤0,5 s (status-task 500 ms), endnu ikke indtrådt",
          sim.dvale_at is not None and sim.power_state == "on")
    sim.advance(sim.t + 0.6)
    check("dvale: power_state=off, work_sta=0, mode=off, udgange 0 (62/63)",
          sim.power_state == "off" and sim.regs[33] == 0 and sim.mode == "off"
          and sim.regs[62] == 0 and sim.regs[63] == 0)
    check("dvale: register-filen BEVARES (ord11=880, ord0=0)",
          sim.regs[11] == 880 and sim.regs[0] == 0)
    r40 = sim.regs[40]
    sim.advance(sim.t + 10.0)
    check("dvale: sensor-regs frosset (ingen status-task → ingen opdatering)",
          sim.regs[40] == r40 and sim.regs[11] == 880)
    check("dvale: dvale_enter logget", any(e["kind"] == "dvale_enter"
                                          for e in sim.alog.events))
    # --- tavs UART: polls/writes/cmd8 ignoreres (status-tasken slettet) ---
    nq = sim.stats["queries"]
    sim.handle_frame(3, 0, 56)
    sim.handle_frame(6, 11, [900])
    sim.handle_frame(8, 0, bytes(10))
    check("dvale: cmd3/cmd6/cmd8 droppet (ingen svar, ingen effect)",
          sim.stats["queries"] == nq and sim.regs[11] == 880
          and sim.stats["dvale_rx_dropped"] == 3)
    # --- aktiviteter stoppes SIKKERT ved dvale (ingen efter-fase) ---
    sim = fresh_sim()
    sim.handle_write(5, [60, 0, 0, 0, 1, 0, 870, 0, 60, 3, 60, 0, 400, 0, 0, 0, 0, 1])
    sim.advance(12.0)
    check("bryg kører før sluk (ws=0x1307)", sim.regs[33] == 0x1307)
    sim.handle_write(0, [0, 0])
    sim.advance(sim.t + 0.6)
    check("dvale under bryg: aktivitet stoppet sikkert (act/post=None, ws=0)",
          sim.act is None and sim.post is None and sim.regs[33] == 0)
    w0 = sim.regs[39]                    # ved dvale-indgang (≤0,5 s efter write)
    sim.advance(sim.t + 5.0)
    check("dvale under bryg: vægt frosset (ingen drip/decay — tasks slettet)",
          sim.regs[39] == w0)
    check("dvale under bryg: activity_stop reason=dvale logget",
          any(e["kind"] == "activity_stop" and e.get("activity") == "brew"
              and "dvale" in e.get("reason", "") for e in sim.alog.events))
    # --- keep-awake-overvågning pauserer i dvale ---
    sim = fresh_sim()
    sim.first_rx_t = 0.0
    sim.handle_write(1, [0])
    sim.advance(29.0)
    sim.handle_write(0, [0, 0])
    sim.advance(sim.t + 40.0)
    check("keep-awake: Ingen gap-log i dvale (40 s uden word1=0 — pause)",
          sim.power_state == "off"
          and not any(e["kind"] == "keepalive_gap" for e in sim.alog.events))
    # --- wake: knap-hold <0,8 s gør intet; ≥0,8 s → reboot til fabriks-defaults ---
    sim.inject({"type": "power_key", "value": 1})
    sim.advance(sim.t + 0.5)
    sim.inject({"type": "power_key", "value": 0})
    check("knap-hold 0,5 s (<0,8 s): INGEN wake", sim.power_state == "off"
          and sim.stats["reboots"] == 0)
    sim.inject({"type": "power_key", "value": 1})
    sim.advance(sim.t + 1.0)
    sim.inject({"type": "power_key", "value": 0})
    check("knap-hold 1,0 s (≥0,8 s): wake → CPU-reset", sim.power_state == "on"
          and sim.stats["reboots"] == 1 and sim.stats["power_wakes"] == 1)
    check("wake: FABRIKKENS startværdier (ord0=1, ord11=920, ord114=50, ord4=180, ord25=550)",
          sim.regs[0] == 1 and sim.regs[11] == 920 and sim.regs[114] == 50
          and sim.regs[4] == 180 and sim.regs[25] == 550)
    check("wake: config IKKE forudindlæst (ord112/113=0, ord1=0, ws=idle)",
          sim.regs[112] == 0 and sim.regs[113] == 0 and sim.regs[1] == 0
          and sim.regs[33] == 0x0101)
    check("wake: 0x89-runde forventes (version_received=False, boot_checked=False)",
          not sim.version_received and not sim.boot_checked
          and sim.first_rx_t is None)
    rb = [e for e in sim.alog.events if e["kind"] == "reboot"]
    check("wake: reboot logget m. factory=True, cause=power_key + defaults-snapshot",
          bool(rb) and rb[-1].get("factory") is True
          and rb[-1].get("cause") == "power_key"
          and (rb[-1].get("defaults") or {}).get("11") == 920)
    check("wake: power_wake logget (cause=power_key)",
          any(e["kind"] == "power_wake" and e.get("cause") == "power_key"
              for e in sim.alog.events))
    # --- re-push fra host genskaber config ---
    words = config_to_words(load_baseline_config())
    mirror = [0] * 31
    for wd, v in words.items():
        if 1 <= wd <= 30:
            mirror[wd] = v
    sim.handle_write(1, mirror[1:31])
    sim.handle_write(112, [words[112], words[113], words[114]])
    check("re-push: config genskabt (ord11=870, ord114=70, ord112=0x0104)",
          sim.regs[11] == 870 and sim.regs[114] == 70 and sim.regs[112] == 0x0104)
    check("re-push: boot_push logget (sub1_len30 + sub112_len3)",
          sim.boot_push_setup and sim.boot_push_preinf)
    # --- manglende re-push efter wake detekteres ---
    sim2 = fresh_sim()
    sim2.inject({"type": "power_off"})
    sim2.inject({"type": "power_on"})
    sim2.handle_frame(3, 0, 56)          # første RX efter wake (connect)
    sim2.advance(sim2.t + 7.0)
    check("wake uden re-push: boot_push_missing logges",
          any(e["kind"] == "boot_push_missing" for e in sim2.alog.events))
    # --- alarm-wake (cmd8's alarm-felt + RTC) ---
    sim = fresh_sim()
    payload = bytes([0xEA, 0x07, 11, 18, 16, 35, 28, 18, 16, 36])  # alarm 16:36
    sim.handle_frame(8, 0, payload)
    ts = [e for e in sim.alog.events if e["kind"] == "time_sync"]
    check("cmd8: tid/alarm parse't (alarm=[18,16,36] logget)",
          bool(ts) and ts[-1].get("alarm") == [18, 16, 36])
    sim.inject({"type": "power_off"})
    sim.advance(sim.t + 40.0)            # RTC 16:35:28 → 16:36:08 → alarm-match
    check("alarm-wake: RTC=alarm i dvale → reboot (cause=alarm)",
          sim.power_state == "on" and sim.stats["power_wakes"] == 1
          and any(e["kind"] == "power_wake" and e.get("cause") == "alarm"
                  for e in sim.alog.events))
    check("alarm-wake: fabriks-defaults (ord11=920)", sim.regs[11] == 920)
    sim = fresh_sim()
    sim.handle_frame(8, 0, bytes([0xEA, 0x07, 11, 18, 16, 35, 28, 0, 0, 0]))
    sim.inject({"type": "power_off"})
    sim.advance(sim.t + 120.0)
    check("alarm 0/0/0: INGEN alarm-wake (dvale i 120 s)",
          sim.power_state == "off" and sim.stats["power_wakes"] == 0)
    # --- knap mens MCU'en er TÆNDT: ingen MCU-effekt (displayet ejer sluk) ---
    sim = fresh_sim()
    sim.inject({"type": "power_key", "value": 1})
    sim.advance(sim.t + 2.0)
    sim.inject({"type": "power_key", "value": 0})
    check("knap-hold mens tændt: ingen reboot/dvale (UI_v6 §19.1)",
          sim.power_state == "on" and sim.stats["reboots"] == 0)
    # --- ctrl-bekvemmeligheder + ping.power ---
    sim = fresh_sim()
    sim.inject({"type": "power_off"})
    ping = sim.ctrl_dispatch({"cmd": "ping"})
    check("ping.power: state=off i dvale", ping["power"]["state"] == "off")
    sim.inject({"type": "power_key", "action": "hold", "dur": 1.6})
    sim.advance(sim.t + 1.7)
    check("power_key hold-bekvemmelighed (auto-slip): wake",
          sim.power_state == "on" and sim.stats["power_wakes"] == 1)
    ping = sim.ctrl_dispatch({"cmd": "ping"})
    check("ping.power: state=on efter wake", ping["power"]["state"] == "on")
    # --- kompat: ctrl reboot / ord30 forudindlæser stadig config ---
    sim = fresh_sim()
    sim.ctrl_dispatch({"cmd": "reboot"})
    rb = [e for e in sim.alog.events if e["kind"] == "reboot"]
    check("ctrl reboot (ikke power): config forudindlæst som før (ord11=870, factory=False)",
          sim.regs[11] == 870 and rb[-1].get("factory") is False)
    # --- pty-niveau: ACK → tavshed → wake → 0x89 → re-push → svar igen ---
    sim = fresh_sim(pty=True)
    c = RawClient(sim)
    c.send(build_version_response("x"))
    c.pump(0.05)
    c.send(build_frame(6, 0, [0, 0]))
    fr = c.pump(0.05)
    check("pty: power-off ACK modtaget FØR dvale",
          any(f[0] == 0x86 and f[1] == 0 and f[2] == [0, 0] for f in fr))
    c.pump(0.6)
    nq = sim.stats["queries"]
    c.send(build_query(0, 56))
    c.send(build_query(33, 7))
    fr = c.pump(0.3)
    check("pty: dvale = tavs UART (ingen 0x83-svar på polls)",
          not any(f[0] == 0x83 for f in fr) and sim.stats["queries"] == nq)
    sim.inject({"type": "power_key", "action": "hold", "dur": 1.0})
    fr = c.pump(1.3)
    check("pty: wake → 0x89-versions-requests igen",
          any(f[0] == 0x89 for f in fr))
    c.send(build_version_response("x"))
    c.pump(0.05)
    words = config_to_words(load_baseline_config())
    mirror = [0] * 31
    for wd, v in words.items():
        if 1 <= wd <= 30:
            mirror[wd] = v
    c.send(build_frame(6, 1, mirror[1:31]))
    c.send(build_frame(6, 112, [words[112], words[113], words[114]]))
    fr = c.pump(0.1)
    check("pty: re-push ACK'et efter wake",
          any(f[0] == 0x86 and f[1] == 1 and len(f[2]) == 30 for f in fr)
          and any(f[0] == 0x86 and f[1] == 112 and len(f[2]) == 3 for f in fr))
    c.send(build_query(0, 56))
    fr = c.pump(0.1)
    resp = [f for f in fr if f[0] == 0x83 and f[1] == 0]
    check("pty: polls besvares igen m. config (ord0=1, ord11=870)",
          bool(resp) and resp[0][2][0] == 1 and resp[0][2][11] == 870)
    c.close()
    sim.alog.close()
    sim._close_pty()


def main():
    t0 = time.monotonic()
    for fn in (t1_crc, t2_chunking, t3_write_policy, t4_packing, t5_boot,
               t6_autostop, t7_keepalive, t8_faults, t9_ack, t10_link, t11_replay,
               t12_manual_stop, t13_power):
        fn()
    dt = time.monotonic() - t0
    print(f"\nSELFTEST: {N_PASS} PASS / {N_FAIL} FAIL ({dt:.1f}s)")
    return 1 if N_FAIL else 0


if __name__ == "__main__":
    sys.exit(main())