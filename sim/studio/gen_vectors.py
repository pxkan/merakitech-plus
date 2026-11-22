#!/usr/bin/env python3
"""gen_vectors.py — print the fixed protocol vectors that sim/studio/test_machine.mjs hardcodes.

Derives every vector from sim/proto.py (the Python source of truth), so the JS port is checked against
the same CRC / framing / parser / config packing the Python sim uses. Python stdlib only, offline.

Usage:  python3 sim/studio/gen_vectors.py      (paste the JSON into test_machine.mjs if proto.py changes)
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import proto  # noqa: E402

hx = lambda b: proto.hexs(b)

out = {}
# CRC-16/MODBUS: the classic check string + a frame body
out["crc"] = {
    "123456789": proto.crc16_modbus(b"123456789"),
    "aa5506160100": proto.crc16_modbus(bytes.fromhex("aa5506160100")),
    "aa5503210007": proto.crc16_modbus(bytes.fromhex("aa5503210007")),
}
# build_frame / build_query / version frames / cmd8
out["frames"] = {
    "write_brew_stop": hx(proto.build_frame(0x06, 22, [0])),
    "ack_grind_start": hx(proto.build_frame(0x86, 3, [1, 185, 60, 1])),
    "write_steam_start": hx(proto.build_frame(0x06, 24, [1, 650, 0, 0, 0, 0, 0, 0])),
    "write_neg": hx(proto.build_frame(0x06, 39, [-3])),
    "query_sub33_len7": hx(proto.build_query(33, 7)),
    "query_sub0_len56": hx(proto.build_query(0, 56)),
    "version_request": hx(proto.build_version_request()),
    "version_response": hx(proto.build_version_response("1.1.1-69")),
    "power_frame": hx(proto.build_power_frame(bytes([0xEA, 0x07, 11, 21, 7, 30, 0, 22, 6, 15]))),
}
# FrameParser: garbage + a bad-CRC frame + mixed frame kinds, fed in 5-byte chunks
bad = bytearray(proto.build_frame(0x83, 33, [0x0101, 0, 0, 0, 16, 0, 0]))
bad[-1] ^= 0xFF
stream = (b"\x00\x13\xaa" + proto.build_query(33, 7) + bytes(bad)
          + proto.build_frame(0x06, 22, [0]) + proto.build_version_request()
          + proto.build_version_response("1.1.1-69")
          + proto.build_power_frame(bytes([0xEA, 0x07, 11, 21, 7, 30, 0, 22, 6, 15]))
          + proto.build_frame(0x83, 112, [260, 1541, 70]) + b"\xaa")
p = proto.FrameParser()
parsed = []
for i in range(0, len(stream), 5):
    for cmd, sub, payload, _body in p.feed(stream[i:i + 5]):
        if isinstance(payload, (bytes, bytearray)):
            payload = hx(payload)
        parsed.append([cmd, sub, payload])
out["parser"] = {"stream": hx(stream), "chunk": 5, "frames": parsed,
                 "crc_ok": p.crc_ok, "crc_bad": p.crc_bad, "garbage": p.garbage}
# config_to_words on the fixture config + the MCU's boot words 0..32 / 112..114
cfg = proto.load_baseline_config()
words = proto.config_to_words(cfg)
regs = [0] * proto.REGN
for w, v in proto.MCU_DEFAULTS.items():
    regs[w] = v
for w, v in words.items():
    regs[w] = v
out["config_words"] = {str(k): v for k, v in sorted(words.items())}
out["boot_regs_0_32"] = regs[0:33]
out["reply_sub112_len3"] = hx(proto.build_frame(0x83, 112, regs[112:115]))
out["serial_words"] = [v for _, v in sorted(proto.serial_words().items())]
out["word_bounds"] = {str(k): list(v) for k, v in sorted(proto.word_bounds(proto.load_spec()).items())}
print(json.dumps(out, indent=1))
