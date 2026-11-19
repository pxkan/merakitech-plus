#!/usr/bin/env python3
"""hygiene.py — repository checks that must hold for a public repo (run in CI):

  * bin/MD5SUMS matches the files in bin/, and tools/merakiplus.py BIN_MD5 matches bin/MD5SUMS
  * no vendor firmware/binaries: the only ELF/Mach-O files tracked are our builds in bin/
  * no personal paths, private IP addresses (other than documented placeholders), MAC addresses
  * sim/fixtures/config.json carries placeholder Wi-Fi credentials only
  * the pre-rename identifier (tools/merakiplus.py LEGACY_ID) appears nowhere — not in file names,
    text or binaries — except inside the "legacy-compat" block of tools/merakiplus.py
"""
import hashlib
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import merakiplus  # noqa: E402

fails = []


def fail(msg):
    fails.append(msg)
    print("  [FAIL] " + msg)


def tracked():
    try:
        out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True).stdout
        files = [ROOT / f for f in out.decode().split("\0") if f]
        if files:
            return files
    except Exception:
        pass
    skip = {".git", "build", "logs", "__pycache__", "reports"}
    return [p for p in ROOT.rglob("*") if p.is_file() and not (set(p.relative_to(ROOT).parts) & skip)]


files = tracked()

# 1. checksums
sums = {}
for line in (ROOT / "bin/MD5SUMS").read_text().splitlines():
    h, name = line.split(None, 1)
    sums[name.strip()] = h
for name, h in sums.items():
    p = ROOT / "bin" / name
    if not p.is_file():
        fail(f"bin/MD5SUMS lists missing file {name}")
    elif hashlib.md5(p.read_bytes()).hexdigest() != h:
        fail(f"bin/{name} does not match bin/MD5SUMS")
for p in (ROOT / "bin").iterdir():
    if p.name not in sums and p.name not in ("MD5SUMS", "README.md"):
        fail(f"bin/{p.name} is not listed in bin/MD5SUMS")
for name, h in merakiplus.BIN_MD5.items():
    if sums.get(name) != h:
        fail(f"tools/merakiplus.py BIN_MD5[{name}] != bin/MD5SUMS")

# 2. binaries only in bin/, no vendor file names anywhere
VENDOR = re.compile(r"(^|/)(demo_v9[^/]*|MEM03_[^/]*\.bin|config\.bin|upgrade\.sh|test\.sh|demo\.sh|[^/]*\.dis)$")
for p in files:
    rel = p.relative_to(ROOT).as_posix()
    head = p.read_bytes()[:4] if p.is_file() else b""
    if head in (b"\x7fELF", b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe") and not rel.startswith("bin/"):
        fail(f"binary outside bin/: {rel}")
    if VENDOR.search(rel):
        fail(f"vendor-looking file name: {rel}")

# 3. personal data in text files
ALLOWED_IPS = {"192.168.1.50", "192.168.1.100", "192.168.0.0", "10.0.0.0"}
PRIV = re.compile(r"\b(?:192\.168|10\.\d{1,3}|172\.(?:1[6-9]|2\d|3[01]))\.\d{1,3}\.\d{1,3}\b")
MAC = re.compile(r"\b(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}\b")
HOME = re.compile(r"/(?:Users|home)/[A-Za-z][\w.-]+/")
# a config-style password / ssid value that is not one of the documented placeholders
CRED = re.compile(r"""["'`](password|ssid)["'`]?\s*[:=]\s*\\?["'`]([^"'`\\<]{3,})""")
CRED_OK = {"ExampleAP", "notarealpw", "TestNet"}
for p in files:
    rel = p.relative_to(ROOT).as_posix()
    if rel.startswith("bin/") or p.suffix in (".png", ".gz"):
        continue
    try:
        t = p.read_text(encoding="utf-8")
    except (UnicodeDecodeError, OSError):
        continue
    for m in PRIV.finditer(t):
        if m.group(0) not in ALLOWED_IPS:
            fail(f"{rel}: private IP address {m.group(0)}")
    for m in MAC.finditer(t):
        fail(f"{rel}: MAC-like address {m.group(0)}")
    for m in CRED.finditer(t):
        if m.group(2).strip() not in CRED_OK and not m.group(2).startswith(("%s", "IKKE")):
            fail(f"{rel}: possible credential {m.group(1)}=…")
    for m in HOME.finditer(t):
        if rel != "tests/hygiene.py":
            fail(f"{rel}: personal path {m.group(0)}")
for p in (ROOT / "bin").iterdir():
    if HOME.search(p.read_bytes().decode("latin-1")):
        fail(f"bin/{p.name} embeds a home-directory path")

# 4. fixture credentials are placeholders
cfg = json.loads((ROOT / "sim/fixtures/config.json").read_text())
if (cfg.get("ssid"), cfg.get("password")) != ("ExampleAP", "notarealpw"):
    fail("sim/fixtures/config.json must keep the placeholder Wi-Fi credentials")

# 5. the pre-rename identifier lives only in the legacy-compat block of tools/merakiplus.py
OLD = merakiplus.LEGACY_ID.encode()
BEGIN, END = b"# BEGIN legacy-compat", b"# END legacy-compat"
for p in files:
    rel = p.relative_to(ROOT).as_posix()
    if OLD in rel.lower().encode():
        fail(f"file name contains the pre-rename identifier: {rel}")
    if not p.is_file():
        continue
    data = p.read_bytes()
    if rel == "tools/merakiplus.py":
        if data.count(BEGIN) != 1 or data.count(END) != 1 or data.index(BEGIN) > data.index(END):
            fail("tools/merakiplus.py: exactly one legacy-compat BEGIN/END block expected")
            continue
        data = data[:data.index(BEGIN)] + data[data.index(END):]
    n = data.lower().count(OLD)
    if n:
        fail(f"{rel}: {n} occurrence(s) of the pre-rename identifier outside the legacy-compat block")

print("HYGIENE: " + ("OK" if not fails else f"{len(fails)} problem(s)"))
sys.exit(1 if fails else 0)
