#!/usr/bin/env python3
"""proto.py — fælles protokol-hjælpere til E2E-testmiljøet (MerakiTech Gen 1).

Kilder: PROTOCOL.md (frame AA 55 CMD SUB LEN + CRC-16/MODBUS little-endian),
REGISTERMAP.md (129-ords model), PARITY.md/parity_spec.json (packing 112-114,
grænser), BOOT.md/tinj_hold.c (evdev-touch-format).

Kun Python stdlib. Ingen enhedskontakt — alt kører lokalt mod mcu_sim's pty.
"""
import json
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SPEC_PATH = ROOT / "sim" / "fixtures" / "parity_spec.json"
BASELINE_CONFIG = ROOT / "sim" / "fixtures" / "config.json"

W = H = 480
FB_BYTES = W * H * 4          # 480x480 BGRA (uint32)
REGN = 129

# ---------------------------------------------------------------- CRC-16/MODBUS
def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc

# ---------------------------------------------------------------- frame-bygning
def build_frame(cmd: int, sub: int, words) -> bytes:
    """AA 55 CMD SUB LEN(ord) payload(16-bit LE) CRC16 LE."""
    payload = b"".join(struct.pack("<H", w & 0xFFFF) for w in words)
    body = bytes([0xAA, 0x55, cmd & 0xFF, sub & 0xFF, len(words) & 0xFF]) + payload
    return body + struct.pack("<H", crc16_modbus(body))

def build_query(sub: int, nwords: int) -> bytes:
    """7-byte cmd-3 read-query (ingen payload)."""
    body = bytes([0xAA, 0x55, 0x03, sub & 0xFF, nwords & 0xFF])
    return body + struct.pack("<H", crc16_modbus(body))

def build_version_request() -> bytes:
    """MCU'ens faste 0x89-frame (PROTOCOL §2.2): AA 55 89 01 00 00 6F 81."""
    return bytes([0xAA, 0x55, 0x89, 0x01, 0x00, 0x00, 0x6F, 0x81])

def build_version_response(ver: str) -> bytes:
    """cmd-9 versions-svar: LEN er BYTES (ikke ord), sub=0."""
    vb = ver.encode("ascii")
    body = bytes([0xAA, 0x55, 0x09, len(vb) & 0xFF, 0x00]) + vb
    return body + struct.pack("<H", crc16_modbus(body))

def build_power_frame(payload10: bytes) -> bytes:
    """cmd-8 power/tid-sync: fast 15-byte frame (10 bytes payload)."""
    assert len(payload10) == 10
    body = bytes([0xAA, 0x55, 0x08]) + payload10
    return body + struct.pack("<H", crc16_modbus(body))

# ---------------------------------------------------------------- frame-parsing
class FrameParser:
    """Resynkroniserende AA 55-parser (som displayets `check` @0x146164).

    Håndterer: chunkede reads (≤32 B), garbage før/efter frames, CRC-fejl,
    cmd-9/0x89 (LEN=bytes) og cmd-8/0x88 (fast 15 bytes).
    """
    def __init__(self):
        self.buf = bytearray()
        self.crc_ok = 0
        self.crc_bad = 0
        self.garbage = 0

    def feed(self, data: bytes):
        self.buf.extend(data)
        out = []
        i = 0
        buf = self.buf
        while i + 7 <= len(buf):
            if buf[i] != 0xAA or buf[i + 1] != 0x55:
                i += 1
                self.garbage += 1
                continue
            cmd = buf[i + 2]
            if cmd in (0x09, 0x89):
                # versions-frames: [3]=LEN(bytes), [4]=SUB (PROTOCOL §2.2)
                sub, ln = buf[i + 4], buf[i + 3]
                total = ln + 7
            else:
                sub, ln = buf[i + 3], buf[i + 4]
                if cmd == 0x03:
                    total = 7                       # read-query: LEN = forventede ord, ingen payload
                elif cmd in (0x08, 0x88):
                    total = 15                      # fast længde
                else:
                    total = 2 * ln + 7              # LEN = ord
            if total < 7 or i + total > len(buf):
                break                               # afvent flere bytes
            body = bytes(buf[i:i + total - 2])
            crc_rx = struct.unpack_from("<H", buf, i + total - 2)[0]
            if crc16_modbus(body) != crc_rx:
                self.crc_bad += 1
                i += 1                              # resync (som originalen)
                continue
            self.crc_ok += 1
            if cmd == 0x03:
                out.append((cmd, sub, ln, body))    # ln = antal ord der spørges om
            elif cmd in (0x09, 0x89):
                payload = bytes(buf[i + 5:i + total - 2])
                out.append((cmd, sub, payload, body))
            elif cmd in (0x08, 0x88):
                payload = bytes(buf[i + 3:i + total - 2])
                out.append((cmd, sub, payload, body))
            else:
                words = [struct.unpack_from("<H", body, 5 + 2 * k)[0] for k in range(ln)]
                out.append((cmd, sub, words, body))
            i += total
        del buf[:i]
        if len(buf) > 4096:                         # sikkerheds-ventil
            del buf[:-64]
        return out

def parse_frames(data: bytes):
    """Engangs-parse af en buffer (yielder (cmd, sub, words/payload))."""
    p = FrameParser()
    return p.feed(data)

def hexs(data: bytes) -> str:
    return " ".join(f"{b:02x}" for b in data)

def s16(v: int) -> int:
    v &= 0xFFFF
    return v - 0x10000 if v >= 0x8000 else v

# ---------------------------------------------------------------- registernavne
REG_NAMES = {
    0: "power", 1: "sleep_drain", 2: "grind_mode", 3: "grind_start_trigger",
    4: "grind_auto_weight", 5: "grind_over_time", 6: "grind_tare_trigger",
    7: "grind_run", 8: "self_inspection_mode", 9: "brew_start_trigger",
    10: "reserveret10", 11: "brew_set_temp", 12: "reserveret12",
    13: "brew_opskrift_param", 14: "steam_gear", 15: "steam_opskrift_param",
    16: "hotwater_auto", 17: "hotwater_weight", 18: "steam_set_temp",
    19: "hotwater_sw", 20: "vand_target", 21: "reserveret21",
    22: "brew_aktiv_flag", 23: "brew_auto_weight", 24: "steam_switch",
    25: "steam_milk_set_temp", 26: "grind_k_kalib", 27: "extract_k_kalib",
    28: "drain_param1", 29: "drain_param2", 30: "reboot_request",
    31: "reserveret31", 32: "reserveret32", 33: "work_sta",
    34: "E1_fejl", 35: "E2_fejl", 36: "E3_fejl", 37: "grinder_gear",
    38: "grind_vaegt_kal", 39: "brew_vaegt_kal", 40: "e_boiler_temp",
    41: "e_boiler_temp_spejl", 42: "e_head_temp", 43: "self_inspection_sta",
    44: "input_bits", 45: "flowmeter_brew", 46: "flowmeter_steam",
    47: "s_boiler_temp", 48: "milk_temp", 49: "netspaending_V",
    50: "stroem_ch1", 51: "tryk_kandidat", 52: "flow_hastighed",
    53: "grind_vaegt_raa", 54: "grind_k", 55: "brew_vaegt_raa",
    56: "extract_k", 57: "flowmeter_puls", 58: "stroem_ch2",
    59: "gears_min", 60: "gears_5", 61: "netfrekvens_Hz",
    62: "e_boiler_effekt", 63: "s_boiler_effekt", 64: "gears_25",
    65: "gears_max", 66: "bryg_taeller", 67: "set_voltage",
    68: "s_vandniveau", 69: "grinder_position_adc", 70: "e_vandniveau",
    71: "Mboard_temp", 72: "Dboard_temp", 73: "self_inspection_result",
    74: "gears_fu4", 75: "gears_40",
    112: "pro_sw_preinf", 113: "preinf_tid_break", 114: "head_set_temp",
    128: "e_head_temp_intern",
}
for _n in range(76, 80):
    REG_NAMES[_n] = f"reserveret{_n}"
for _n in range(80, 88):
    REG_NAMES[_n] = f"UID_A[{_n - 80}]"
for _n in range(88, 96):
    REG_NAMES[_n] = f"UID_B[{_n - 88}]"
for _n in range(96, 112):
    REG_NAMES[_n] = f"serienr[{_n - 96}]"
for _n in range(115, 128):
    REG_NAMES[_n] = f"reserveret{_n}"

def reg_name(n: int) -> str:
    return REG_NAMES.get(n, f"word{n}")

# ---------------------------------------------------------------- work_sta-faser
# SESSION.md §6 / BREW.md §4 / HOTWATER.md §5 (live-verificerede bits)
WS_IDLE = 0x0101          # bits 0+8 (altid sat i normal drift)
WS_SLEEP = 0x0001         # drain-task (word1=1)
WS_GRIND = 0x0141         # +bit6
WS_BREW_PRE = 0x1107      # +bits 1,2,12
WS_BREW_EXT = 0x1307      # +bits 1,2,9,12
WS_WATER = 0x030B         # +bits 1,3,9
WS_STEAM = 0x4121         # +bits 5,14 (statisk evidens, SESSION §5.2)
WS_PULSE_MASK = 0x2C00    # bits 10,11,13 = stokastiske idle-pulser (maskeres i replay)
WS_ACTIVITY_MASK = 0x537F & ~WS_PULSE_MASK  # alt undtagen idle-pulser

def phase_of(regs, link_up=True) -> str:
    """Samme fase-logik som ourui_v6.c phase_raw() (UI'ets forventning)."""
    if not link_up:
        return "OFFLINE"
    ws = regs[33]
    if (ws & 0x4020) == 0x4020:
        return "STEAM"
    if ws & 0x0040:
        return "GRIND"
    if (ws & 0x0006) == 0x0006:
        return "EXT" if (ws & 0x0200) else "PRE"
    if (ws & 0x000A) == 0x000A:
        return "WATER"
    if regs[34] or regs[35] or regs[36]:
        return "ERROR"
    if regs[1] == 1 or not (ws & 0x0100):
        return "SLEEP"
    if regs[40] < regs[11] - 20:
        return "HEAT"
    return "READY"

# ---------------------------------------------------------------- parity-spec
def load_spec(path=None) -> dict:
    p = Path(path) if path else SPEC_PATH
    return json.loads(p.read_text())

def word_bounds(spec: dict) -> dict:
    """word -> (min, max, step) for rene (ikke-pakkede) parameter-ord."""
    out = {}
    for prm in spec["parameters"]:
        if prm.get("packing") or not prm.get("words"):
            continue
        if prm.get("min") is None or prm.get("max") is None:
            continue
        if len(prm["words"]) == 1:
            out[prm["words"][0]] = (prm["min"], prm["max"], prm.get("step") or 1)
    return out

def pack112(switch: int, pressure: int) -> int:
    return ((switch & 1) << 8) | (pressure & 0xFF)

def pack113(time_s: int, break_s: int) -> int:
    return ((time_s & 0xFF) << 8) | (break_s & 0xFF)

def unpack112(w: int):
    return (w >> 8) & 1, w & 0xFF

def unpack113(w: int):
    return (w >> 8) & 0xFF, w & 0xFF

def config_to_words(cfg: dict) -> dict:
    """config.json -> setup-ord jf. PARITY §1.3 (model_data_init + boot-push).

    Inkluderer byte-pakning 112/113/114 og boot-hardcodes word5/13/15=60.
    """
    g = lambda k, d: cfg.get(k, d)
    words = {
        0: 1,                                   # power on (app_init PowerOn)
        1: 0,                                   # awake (keep-awake, SESSION §10.1)
        2: int(g("grindMode", 1)),
        4: int(g("autoGrindWeight", 185)),
        5: 60,                                  # hardcoded ved boot (PARITY §1.4)
        11: int(g("brewTemper", 870)),
        13: 60,                                 # hardcoded ved boot
        14: int(g("steamGears", 3)),
        15: 60,                                 # hardcoded ved boot
        17: int(g("autoHotWaterTarget", 400)),
        23: int(g("autobrewWeight", 360)),
        25: int(g("AtuoSteamTarge", 650)),
        112: pack112(int(g("preInfusionSwitch", 1)), int(g("preInfusionPressure", 4))),
        113: pack113(int(g("preInfusionTime", 6)), int(g("breakTime", 5))),
        114: int(g("headTemper", 70)),
    }
    return words

def load_baseline_config(path=None) -> dict:
    p = Path(path) if path else BASELINE_CONFIG
    return json.loads(p.read_text())

# MCU-defaults fra REGISTERMAP §2 (init 0x9966) — bruges før config/boot-push.
MCU_DEFAULTS = {0: 1, 2: 1, 4: 180, 5: 60, 11: 920, 13: 60, 14: 4,
                15: 60, 17: 360, 23: 360, 25: 550, 114: 50}

# Idle-baseline for sensor-ord (v3-capturens første frames, BREW.md §8).
SENSOR_BASELINE = {
    33: WS_IDLE, 37: 16, 38: 0, 39: 0, 40: 998, 41: 998, 42: 696,
    43: 0, 44: 12, 45: 0, 46: 0, 47: 1302, 48: 232, 49: 236, 50: 5,
    51: 0, 52: 0, 53: 1038, 54: 2116, 55: 1094, 56: 2112, 57: 1,
    58: 5, 59: 1278, 60: 1541, 61: 50, 62: 100, 63: 100, 64: 2906,
    65: 3219, 66: 0, 67: 220, 68: 1331, 69: 2304, 70: 1076, 71: 100,
    72: 100, 73: 0, 74: 1808, 75: 1,
    80: 13654, 81: 12590, 82: 13358, 88: 13654, 89: 12590, 90: 13358,
}
SERIAL_DEFAULT = b"B000000-0000-0000" + b"\x00" * 15   # 32 bytes -> ord 96..111

def serial_words(serial: bytes = SERIAL_DEFAULT) -> dict:
    return {96 + i: struct.unpack_from("<H", serial, 2 * i)[0] for i in range(16)}

# ---------------------------------------------------------------- evdev-touch
# tinj_hold.c / test_switcher.py: 16-byte input_event (32-bit timeval, ARM 4.9)
EV_SYN, EV_KEY, EV_ABS = 0x00, 0x01, 0x03
SYN_REPORT, BTN_TOUCH = 0x00, 0x14A
ABS_X, ABS_Y = 0x00, 0x01
ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_MT_TRACKING_ID = 0x35, 0x36, 0x39
EV_FMT = struct.Struct("<iiHhi")

def evdev_event(sec: int, usec: int, typ: int, code: int, val: int) -> bytes:
    return EV_FMT.pack(sec, usec, typ, code, val)

# Enhedens FAKTISKE ABS_MT-range (FINDINGS 'Gestus-debug': 0..480, ikke 4095).
# switcher/tinj/ourui auto-detekterer via EVIOCGABS med 480 som fallback-konstant.
# SIM's virtuelle touch-flade BEHOLDER sit deklarerede 0..4095 (ourui_v6.c SIM-HAL);
# streams der fodrer switcher_host i fil-mode skal passe rmax=4095 (eller 480) så
# screen<->raw er identisk med modtagerens range (switcher's fil-mode-default = 480).
DEVICE_RAW_MAX = 480

def touch_raw(px: int, rmin: int = 0, rmax: int = 4095) -> int:
    """tinj.c-formlen: raw = min + px*(max-min+1)/480.
    rmax=4095 = SIM's deklarerede range; rmax=DEVICE_RAW_MAX = enhedens range."""
    return rmin + px * (rmax - rmin + 1) // 480

def touch_stream(presses, x=75, y=75, hold_ms=1600, gap_ms=300, t0_ms=100000,
                 rmin=0, rmax=4095) -> bytes:
    """Samme event-sekvens som tinj_hold: down(TRACKING_ID,X,Y,X,Y,BTN_TOUCH,SYN)
    … hold … up(TRACKING_ID=-1,BTN_TOUCH=0,SYN). Tidsstempler styrer gestus-timing.
    presses = antal holds (tap = hold_ms 80)."""
    out = []
    clock = t0_ms
    rx, ry = touch_raw(x, rmin, rmax), touch_raw(y, rmin, rmax)
    for i in range(presses):
        sec, usec = divmod(clock, 1000)
        usec *= 1000
        for typ, code, val in (
            (EV_ABS, ABS_MT_TRACKING_ID, 1),
            (EV_ABS, ABS_MT_POSITION_X, rx), (EV_ABS, ABS_MT_POSITION_Y, ry),
            (EV_ABS, ABS_X, rx), (EV_ABS, ABS_Y, ry),
            (EV_KEY, BTN_TOUCH, 1), (EV_SYN, SYN_REPORT, 0),
        ):
            out.append(evdev_event(sec, usec, typ, code, val))
        clock += hold_ms
        sec, usec = divmod(clock, 1000)
        usec *= 1000
        for typ, code, val in (
            (EV_ABS, ABS_MT_TRACKING_ID, -1),
            (EV_KEY, BTN_TOUCH, 0), (EV_SYN, SYN_REPORT, 0),
        ):
            out.append(evdev_event(sec, usec, typ, code, val))
        if i < presses - 1:
            clock += gap_ms
    return b"".join(out)

# ---------------------------------------------------------------- ACK-profiler
# Observeret: v2 ~22-25 ms uden retransmit; v3 første ACK ~250-280 ms (MCU
# optaget af start-processing) → display retransmitter, retransmit-ACK ~12-23 ms.
ACK_PROFILES = {
    "normal":      {"latency_ms": 22, "jitter_ms": 3, "loss": 0.0, "first_delay_ms": 0},
    "observed_v3": {"latency_ms": 18, "jitter_ms": 5, "loss": 0.0, "first_delay_ms": 260},
    "slow":        {"latency_ms": 150, "jitter_ms": 20, "loss": 0.0, "first_delay_ms": 0},
    "lossy":       {"latency_ms": 22, "jitter_ms": 3, "loss": 0.35, "first_delay_ms": 0},
    "no_ack":      {"latency_ms": 22, "jitter_ms": 0, "loss": 1.0, "first_delay_ms": 0},
}