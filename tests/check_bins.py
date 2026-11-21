#!/usr/bin/env python3
"""check_bins.py — CI drift gate: the committed bin/ must equal fresh builds.

Run after ./build.sh and the Linux SIM cross-build:

    zig cc -target x86_64-linux-musl -DMERAKIPLUS_SIM -O2 -static -s src/merakiplus.c -lm \
        -o build/merakiplus_sim-linux-x86_64

These targets are bit-reproducible with zig 0.17.1, independent of the build machine
(see bin/README.md). A mismatch means bin/ was not rebuilt after a source change:
rebuild, then update bin/MD5SUMS and BIN_MD5 in tools/merakiplus.py (CONTRIBUTING.md).

Excluded on purpose:
  * grab_shim.so — the committed file is the on-device-verified build with only the
    non-loaded trailer trimmed; a fresh ./build.sh build differs by design (bin/README.md).
  * merakiplus_sim-macos-arm64 — Mach-O builds are not bit-reproducible; the committed
    build (made on macOS) is shipped as-is. tests/hygiene.py still md5-gates it.
"""
import hashlib
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
REPRODUCIBLE = (
    "merakiplus",
    "merakiplus_sim_arm",
    "switcher",
    "tinj_hold",
    "keyinj",
    "merakiplus_snd",
    "merakiplus_sim-linux-x86_64",
)


def md5(p):
    return hashlib.md5(p.read_bytes()).hexdigest()


fails = []
for name in REPRODUCIBLE:
    fresh, committed = ROOT / "build" / name, ROOT / "bin" / name
    if not fresh.is_file():
        fails.append(f"build/{name} missing — run ./build.sh and the Linux SIM cross-build first")
        continue
    if not committed.is_file():
        fails.append(f"bin/{name} missing")
        continue
    a, b = md5(fresh), md5(committed)
    if a == b:
        print(f"  [OK]    {name}  {a}")
    else:
        fails.append(f"bin/{name} drifted: committed {b} != fresh build {a} — "
                     "rebuild bin/ and update bin/MD5SUMS + BIN_MD5")

print("BIN DRIFT: " + ("OK" if not fails else f"{len(fails)} problem(s)"))
sys.exit(1 if fails else 0)