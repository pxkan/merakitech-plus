#!/usr/bin/env python3
"""boot_install.py — DEVELOPER REFERENCE (the original step-by-step installer used during
development). End users: use ./install.sh and ./revert.sh instead.

boot-persistens + on-device UI-skift til MerakiTech Gen 1.

FORBEREDELSESSCRIPT: enheden er offline nu; device-trinene køres KUN senere når
brugeren er ved maskinen (ét kald pr. trin, klare gates, emergency ved enhver fejl).
Lokal dry-run køres mod en mock-rod (--mock-root) der efterligner /customer,
/new_customer, /tmp og /proc — beviser backup/linje-tilføjelse/revert-md5'er uden enhed.

TRIN (rækkefølge):
  mock-setup   byg lokal mock-rod fra artifacts/ (kun med --mock-root)
  backup       md5+kopi: demo.sh → boot/demo.sh.bak, B's demo_v9 → demo_v9.vendor,
               flag-tilstand noteres, config-md5 baseline-gate
  install      upload switcher + ourui (artifacts/ourui/ourui_v6, md5 læses VED
               KØRSEL så v6.1 altid er fersk) som /new_customer/demo_v9 + helpers;
               TILFØJ ÉN guard-linje i /customer/demo.sh før slot-branch (atomisk)
  boot-test    flag+reboot → B verificeres (exe-link+fb); rm flag+reboot → A;
               gestus via tinj_hold → runtime-skift til B UDEN reboot; skift tilbage
  revert-test  genskab demo.sh fra .bak (md5-match), kill switcher, rm flag, reboot
               → stock A urørt; genskab B's vendor-demo_v9 (md5-match); /tmp ren;
               config-md5 uændret
  persist      --default-boot A|B: sæt/fjern /customer/new (brugerens valg, gælder
               næste boot; ingen reboot). Kan også gives som afslutning på ethvert trin.
  emergency    genskab demo.sh.bak + kill switcher + lad maskinen stå i A (køres
               automatisk ved enhver fejl i device-trin; kan også kaldes manuelt)
  status       read-only overblik (flag, PIDs/exe, md5'er, state)

BRUG:
  python3 boot_install.py mock-setup --mock-root mock_root
  python3 boot_install.py backup     --mock-root mock_root
  python3 boot_install.py install    --mock-root mock_root
  python3 boot_install.py boot-test  --mock-root mock_root
  python3 boot_install.py revert-test --mock-root mock_root
  python3 boot_install.py persist --mock-root mock_root --default-boot A
  # senere, ved maskinen (uden --mock-root = rigtig enhed via telnet/push_nc):
  python3 boot_install.py backup
  python3 boot_install.py install
  python3 boot_install.py boot-test
  python3 boot_install.py persist --default-boot B   # eller A

Politik: ingen kontakt med enheden uden eksplicit device-kald; slot A's binary,
libs, pinit og test.sh røres ALDRIG; config.json røres ikke; ingen writes til
UART/fb; reboot kun i boot-test/revert-test (eksplicitte trin). Al dokumentation: BOOT.md.
"""
import argparse
import base64
import datetime
import hashlib
import json
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

# Repo layout: this file lives in boot/dev/. Binaries are taken from build/ (./build.sh).
# mock-setup needs YOUR OWN copies of the vendor files (pulled from your display) in
# $VENDOR_DIR (demo.sh, demo_v9, test.sh, config.json) — they are not in this repo.
DEV = pathlib.Path(__file__).resolve().parent            # boot/dev
ROOT = DEV.parent.parent                                 # repo root
HERE = ROOT / "build"                                    # switcher, tinj_hold, grab_shim.so
OURUI = ROOT / "build"                                   # ourui, fbpage
ART = pathlib.Path(os.environ.get("VENDOR_DIR", str(ROOT / "vendor-local")))
TELNET = DEV / "telnet_run.py"
PUSH = DEV / "push_nc.py"
LOG = ROOT / "logs" / "boot_install.log"
BACKUPS = ROOT / "logs" / "backups"
TESTLOGS = ROOT / "logs" / "test-logs"

BASELINE_CONFIG_MD5 = "<config-md5-redacted>"   # WRITETEST.md §1
BOOTDIR = "/new_customer/ourui/boot"
STATE_PATH = BOOTDIR + "/state.json"
DEMO_SH = "/customer/demo.sh"
FLAG = "/customer/new"
# v6.12 (GESTUS-FIX): guard-linjen exporterer LD_PRELOAD=grab_shim.so FØR ./demo_v9
# (bypass c: stock's EVIOCGRAB no-op'es → read-only switcher ser events). Shim'en
# scopes selv til /customer/demo_v9 (exe-check) — ourui's grab i B-mode bevares.
# Stadig PRÆCIS ÉN linje; -f-test gør manglende .so harmløs. LD_PRELOAD arves af
# test.sh-watchdog'en → også watchdog-genstartet stock får shim'en.
GUARD_LINE = ("[ -x /new_customer/ourui/boot/switcher ] && { [ -f /new_customer/ourui/boot/grab_shim.so ] "
              "&& export LD_PRELOAD=/new_customer/ourui/boot/grab_shim.so${LD_PRELOAD:+:$LD_PRELOAD}; "
              "/new_customer/ourui/boot/switcher & }")
LEGACY_GUARD_LINE = "[ -x /new_customer/ourui/boot/switcher ] && /new_customer/ourui/boot/switcher &"
ANCHOR = 'if [ "$MOUNT_SUCCESS" -eq 1 ] && [ -f /customer/new ]; then'
INSMOD_MARKER = "# kernel_mod_list"
REVERT_LINE = ("REVERT (BOOT.md §5): cp /new_customer/ourui/boot/demo.sh.bak "
               "/customer/.demo_sh.tmp && md5sum /customer/.demo_sh.tmp "
               "&& mv /customer/.demo_sh.tmp /customer/demo.sh && sync")
A_EXE, B_EXE = "/customer/demo_v9", "/new_customer/demo_v9"
TMP_CLEANUP = ["/tmp/tinj_hold", "/tmp/fbpage", "/tmp/switcher.log",
               "/tmp/ourui_boot.log", "/tmp/switcher.lock", "/tmp/.push_nc_tmp",
               "/tmp/nc.err", "/tmp/fb_boot.bin", "/tmp/a_relaunch.log"]


def log(msg):
    ts = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
    line = f"[{ts}] {msg}"
    print(line)
    LOG.parent.mkdir(parents=True, exist_ok=True)
    with LOG.open("a") as f:
        f.write(line + "\n")


def md5b(data):
    return hashlib.md5(data).hexdigest()


class GateError(RuntimeError):
    pass


def gate(cond, msg):
    if not cond:
        raise GateError(msg)
    log(f"[GATE OK] {msg}")


def elf_arm_static(path):
    """Sanity: 32-bit ARM ELF uden PT_INTERP (statisk)."""
    b = path.read_bytes()
    gate(b[:4] == b"\x7fELF" and b[4] == 1, f"{path.name}: 32-bit ELF")
    gate(int.from_bytes(b[18:20], "little") == 40, f"{path.name}: e_machine=ARM(40)")
    phoff = int.from_bytes(b[28:32], "little")
    phentsize = int.from_bytes(b[42:44], "little")
    phnum = int.from_bytes(b[44:46], "little")
    for i in range(phnum):
        ptype = int.from_bytes(b[phoff + i * phentsize:phoff + i * phentsize + 4], "little")
        gate(ptype != 3, f"{path.name}: ingen PT_INTERP (statisk)")


# ================================ BACKENDS ================================

class DeviceBackend:
    """Rigtig enhed via etableret telnet_run.py + push_nc.py (root@$TELNET_HOST)."""
    name = "device"

    def __init__(self):
        self.port = 4460
        self.host = os.environ["TELNET_HOST"]  # set TELNET_HOST=<display-ip>

    def telnet(self, cmd, timeout=40, allow_fail=False):
        log(f"$ {cmd}")
        r = subprocess.run([sys.executable, str(TELNET), cmd, str(timeout)],
                           capture_output=True, text=True, timeout=timeout + 30)
        out = r.stdout.strip()
        if out:
            log("  → " + out.replace("\n", "\n  → ")[:4000])
        if r.returncode != 0 and not allow_fail:
            raise RuntimeError(f"telnet rc={r.returncode}: {cmd}\n{r.stderr}")
        return out

    def push(self, local, dest):
        self.port += 1
        local = pathlib.Path(local)
        log(f"[push] {local} → {dest} (nc port {self.port})")
        r = subprocess.run([sys.executable, str(PUSH), str(local), dest, str(self.port)],
                           capture_output=True, text=True, timeout=300)
        sys.stderr.write(r.stderr)
        if r.returncode != 0:
            raise RuntimeError(f"push fejlede: {local} → {dest}\n{r.stdout}\n{r.stderr}")
        return md5b(local.read_bytes())

    def md5(self, path):
        out = self.telnet(f"md5sum {path}", allow_fail=True)
        m = re.search(r"([0-9a-f]{32})", out)
        return m.group(1) if m else None

    def read(self, path):
        # Byte-eksakt download: `cat` via telnet-pty'en tabs-ekspanderer og
        # taber trailing newline (målt on-device 2026-11-17: demo.sh 5511 B
        # blev til 5642 B via cat). gzip+base64 er immun over for pty'en.
        out = self.telnet(f"gzip -c {path} | base64", timeout=120)
        b64 = "".join(l.strip() for l in out.splitlines()
                      if l.strip() and re.fullmatch(r"[A-Za-z0-9+/=]+", l.strip()))
        return gzip_decompress(base64.b64decode(b64))

    def exists(self, path):
        return "YES" in self.telnet(f"[ -e {path} ] && echo YES || echo NO")

    def cp(self, src, dst):
        self.telnet(f"cp -p {src} {dst}")
        gate(self.md5(src) == self.md5(dst), f"cp md5-match: {src} → {dst}")

    def rm(self, path):
        self.telnet(f"rm -f {path}")

    def touch(self, path):
        self.telnet(f"touch {path}")

    def mkdir_p(self, path):
        self.telnet(f"mkdir -p {path}")

    def chmod(self, path, mode):
        self.telnet(f"chmod {mode:o} {path}")

    def sync(self):
        self.telnet("sync")

    def readlink(self, path):
        return self.telnet(f"readlink {path}", allow_fail=True).strip()

    def install_verified(self, src_on_device, dest, expected_md5, mode=0o755):
        """Atomisk cross-fs-install: cp→tmp I DEST'S FS, md5-gate, mv (atomisk), chmod."""
        d = pathlib.PurePosixPath(dest)
        tmp = str(d.parent / ("." + d.name + ".tmp"))
        self.telnet(f"cp {src_on_device} {tmp}")
        gate(self.md5(tmp) == expected_md5, f"tmp-md5 == forventet ({expected_md5[:8]}…): {tmp}")
        self.telnet(f"mv {tmp} {dest} && chmod {mode:o} {dest} && sync && md5sum {dest}")
        gate(self.md5(dest) == expected_md5, f"installeret md5-match: {dest}")

    def write_small(self, path, data, mode=0o644):
        """Lille tekstfil via base64-heredoc → tmp+mv (WRITETEST-mønsteret)."""
        d = pathlib.PurePosixPath(path)
        tmp = str(d.parent / ("." + d.name + ".tmp"))
        b64 = base64.b64encode(data).decode()
        wrapped = "\n".join(b64[i:i + 76] for i in range(0, len(b64), 76))
        self.telnet(f"base64 -d > {tmp} <<'B64EOF'\n{wrapped}\nB64EOF\nmd5sum {tmp}")
        gate(self.md5(tmp) == md5b(data), f"heredoc-md5-match: {tmp}")
        self.telnet(f"mv {tmp} {path} && chmod {mode:o} {path} && sync")

    def demo_state(self):
        """[(pid, exe)] for alle demo_v9-PIDs."""
        out = self.telnet("pidof demo_v9", allow_fail=True)
        res = []
        for pid in out.split():
            if pid.isdigit():
                res.append((int(pid), self.readlink(f"/proc/{pid}/exe")))
        return res

    def switcher_pids(self):
        out = self.telnet("pidof switcher", allow_fail=True)
        return [int(p) for p in out.split() if p.isdigit()]

    def kill_switcher(self):
        self.telnet("kill $(pidof switcher) 2>/dev/null; echo switcher_kill_done",
                    allow_fail=True)

    def reboot(self):
        log("[reboot] sync; reboot (forbindelsen forventes afbrudt)")
        self.telnet("sync; reboot", timeout=15, allow_fail=True)

    def wait_up(self, budget=240):
        log(f"[reboot] venter på enhed (op til {budget} s) …")
        time.sleep(20)
        deadline = time.time() + budget
        while time.time() < deadline:
            try:
                st = self.demo_state()
                if st and st[0][1]:
                    log(f"[reboot] enhed oppe: demo_v9 {st}")
                    return st
            except Exception as e:
                log(f"[reboot] endnu ikke klar: {e}")
            time.sleep(5)
        raise RuntimeError("enhed kom ikke op efter reboot")

    def launch_a_stopfirst(self):
        """Runtime-skift B→A uden reboot (SIGSTOP-først, jf. switcher.c/BOOT.md)."""
        cmd = (
            'B=""; for p in $(pidof demo_v9); do '
            '[ "$(readlink /proc/$p/exe)" = /new_customer/demo_v9 ] && B=$p; done; '
            'echo B_PID=$B; '
            'if [ -n "$B" ]; then kill -STOP $B; fi; '
            'cd /customer; export LD_LIBRARY_PATH=/customer/libs:/config/wifi; '
            'nohup ./demo_v9 >/tmp/a_relaunch.log 2>&1 & '
            'sleep 1; if [ -n "$B" ]; then kill -9 $B; fi; sleep 1; '
            'echo NY=$(pidof demo_v9); '
            'for p in $(pidof demo_v9); do readlink /proc/$p/exe; done'
        )
        self.telnet(cmd, timeout=30)

    def launch_a_direct(self):
        self.telnet("cd /customer; export LD_LIBRARY_PATH=/customer/libs:/config/wifi; "
                    "nohup ./demo_v9 >/tmp/a_relaunch.log 2>&1 & sleep 2; "
                    "echo NY=$(pidof demo_v9)", timeout=30)

    def capture(self, name):
        """fb-capture (round_capture-mønsteret) → test-logs/<name>.bin/.png."""
        if self.md5("/tmp/fbpage") != md5b((OURUI / "fbpage").read_bytes()):
            self.push(OURUI / "fbpage", "/tmp/fbpage")
        out = self.telnet("dd if=/dev/fb0 of=/tmp/fb_boot.bin bs=1920 count=960 2>/dev/null; "
                          "/tmp/fbpage; gzip -c /tmp/fb_boot.bin | base64; rm -f /tmp/fb_boot.bin",
                          timeout=90)
        page, b64 = None, []
        for ln in out.splitlines():
            s = ln.strip()
            if page is None and re.fullmatch(r"[01]", s):
                page = int(s)
                continue
            if re.fullmatch(r"[A-Za-z0-9+/=]+", s) and len(s) >= 4:
                b64.append(s)
        gate(page is not None and b64, f"fb-capture parsebar: {name}")
        raw = gzip_decompress(base64.b64decode("".join(b64)))
        W = H = 480
        half = raw[page * W * H * 4:(page + 1) * W * H * 4]
        TESTLOGS.mkdir(exist_ok=True)
        (TESTLOGS / f"{name}.bin").write_bytes(half)
        half2png(half, TESTLOGS / f"{name}.png")
        log(f"[capture] {name}: aktiv page={page} → {TESTLOGS / (name + '.png')}")

    def gesture(self):
        """Injektér gestus i A-mode via tinj_hold (v6.12: center-hold 5,2 s i (240,240))."""
        self.push(HERE / "tinj_hold", "/tmp/tinj_hold")
        self.telnet("/tmp/tinj_hold gesture", timeout=30)

    def switcher_log(self):
        return self.telnet("cat /tmp/switcher.log 2>/dev/null", allow_fail=True)

    def clean_tmp(self):
        self.telnet("rm -f " + " ".join(TMP_CLEANUP) + "; ls /tmp/")


def gzip_decompress(b):
    import gzip
    return gzip.decompress(b)


def half2png(raw, path):
    W = H = 480

    def chunk(typ, data):
        return (struct.pack(">I", len(data)) + typ + data
                + struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF))

    lines = bytearray()
    for y in range(H):
        lines.append(0)
        row = raw[y * W * 4:(y + 1) * W * 4]
        for x in range(W):
            b, g, r = row[x * 4], row[x * 4 + 1], row[x * 4 + 2]
            lines += bytes((r, g, b))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(lines), 6))
           + chunk(b"IEND", b""))
    path.write_bytes(png)


class MockBackend:
    """Lokal mock-rod: /customer,/new_customer,/tmp,/proc,/dev mappes under root.
    Boot/reboot/gestus SIMULERES (markeret i log) — men al fil-logik (backup,
    linje-tilføjelse, md5-gates, revert) er ÆGTE kode mod ÆGTE filer."""
    name = "mock"

    def __init__(self, root):
        self.root = pathlib.Path(root).resolve()

    def p(self, path):
        for pre in ("/customer", "/new_customer", "/tmp", "/proc", "/dev"):
            if path == pre or path.startswith(pre + "/"):
                return self.root / path.lstrip("/")
        raise GateError(f"mock: sti uden for mock-rod: {path}")

    def md5(self, path):
        f = self.p(path)
        return md5b(f.read_bytes()) if f.is_file() else None

    def read(self, path):
        return self.p(path).read_bytes()

    def exists(self, path):
        return self.p(path).exists()

    def cp(self, src, dst):
        s, d = self.p(src), self.p(dst)
        d.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(s, d)
        gate(self.md5(src) == self.md5(dst), f"cp md5-match: {src} → {dst}")

    def rm(self, path):
        self.p(path).unlink(missing_ok=True)

    def touch(self, path):
        f = self.p(path)
        f.parent.mkdir(parents=True, exist_ok=True)
        f.touch()

    def mkdir_p(self, path):
        self.p(path).mkdir(parents=True, exist_ok=True)

    def chmod(self, path, mode):
        self.p(path).chmod(mode)

    def sync(self):
        log("[mock] sync (no-op)")

    def readlink(self, path):
        try:
            return os.readlink(self.p(path))
        except OSError:
            return ""

    def push(self, local, dest):
        local = pathlib.Path(local)
        d = self.p(dest)
        d.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(local, d)
        d.chmod(0o755)
        gate(self.md5(dest) == md5b(local.read_bytes()),
             f"push md5-match: {local.name} → {dest}")
        return md5b(local.read_bytes())

    def install_verified(self, src_on_device, dest, expected_md5, mode=0o755):
        d = self.p(dest)
        tmp = d.parent / ("." + d.name + ".tmp")
        shutil.copy2(self.p(src_on_device), tmp)
        gate(self.md5("/" + str(tmp.relative_to(self.root))) == expected_md5,
             f"tmp-md5 == forventet ({expected_md5[:8]}…)")
        os.replace(tmp, d)
        d.chmod(mode)
        gate(self.md5(dest) == expected_md5, f"installeret md5-match: {dest}")

    def write_small(self, path, data, mode=0o644):
        d = self.p(path)
        d.parent.mkdir(parents=True, exist_ok=True)
        tmp = d.parent / ("." + d.name + ".tmp")
        tmp.write_bytes(data)
        os.replace(tmp, d)
        d.chmod(mode)
        gate(self.md5(path) == md5b(data), f"write md5-match: {path}")

    # --- simuleret proc-tilstand ---
    def _procfile(self):
        return self.root / "proc" / "demo_state.json"

    def _set_demo(self, exe):
        pr = self.root / "proc"
        pr.mkdir(parents=True, exist_ok=True)
        n = int((pr / "next_pid").read_text()) if (pr / "next_pid").exists() else 1000
        n += 1
        (pr / "next_pid").write_text(str(n))
        link = pr / str(n) / "exe"
        link.parent.mkdir(parents=True, exist_ok=True)
        if link.is_symlink() or link.exists():
            link.unlink()
        link.symlink_to(exe)
        self._procfile().write_text(json.dumps([{"pid": n, "exe": exe}]))
        return n

    def demo_state(self):
        f = self._procfile()
        if not f.exists():
            return []
        return [(d["pid"], d["exe"]) for d in json.loads(f.read_text())]

    def switcher_pids(self):
        f = self.root / "proc" / "switcher.json"
        return [json.loads(f.read_text())["pid"]] if f.exists() else []

    def kill_switcher(self):
        (self.root / "proc" / "switcher.json").unlink(missing_ok=True)
        log("[mock] switcher dræbt")

    def reboot(self):
        """SIMULERET boot: vendor demo.sh-logikken (flag → slot) efterprøves."""
        pr = self.root / "proc"
        pr.mkdir(parents=True, exist_ok=True)
        bc = int((pr / "boot_count").read_text()) if (pr / "boot_count").exists() else 0
        bc += 1
        (pr / "boot_count").write_text(str(bc))
        flag = self.exists(FLAG)
        b_ok = self.exists("/new_customer/demo_v9")
        slot = "B" if (flag and b_ok) else "A"
        exe = B_EXE if slot == "B" else A_EXE
        self._set_demo(exe)
        # guard-linjen starter switcher hvis binary findes (vendor-boot-simulering)
        sh = self.read(DEMO_SH).decode()
        if (GUARD_LINE in sh or LEGACY_GUARD_LINE in sh) and self.exists(BOOTDIR + "/switcher"):
            (pr / "switcher.json").write_text(json.dumps({"pid": 9000 + bc}))
        else:
            (pr / "switcher.json").unlink(missing_ok=True)
        hist = json.loads((pr / "boot_history.json").read_text()) if (pr / "boot_history.json").exists() else []
        hist.append({"boot": bc, "flag": flag, "slot": slot, "exe": exe})
        (pr / "boot_history.json").write_text(json.dumps(hist, indent=1))
        log(f"[mock] SIMULERET reboot #{bc}: flag={flag} → boot {slot} ({exe})")

    def wait_up(self, budget=240):
        return self.demo_state()

    def launch_a_stopfirst(self):
        self._set_demo(A_EXE)
        log("[mock] SIMULERET runtime-skift B→A (SIGSTOP-først-mønsteret)")

    def launch_a_direct(self):
        self._set_demo(A_EXE)
        log("[mock] SIMULERET manuel A-launch")

    def capture(self, name):
        TESTLOGS.mkdir(exist_ok=True)
        st = self.demo_state()
        (TESTLOGS / f"mock_{name}.json").write_text(json.dumps(
            {"simuleret": True, "demo_state": st,
             "note": "ingen rigtig fb på mock — exe-link er beviset her"}, indent=1))
        log(f"[mock] SIMULERET fb-capture: {name} (demo_state={st})")

    def gesture(self):
        """ÆGTE gestus-kode køres: switcher_host i fil-mode med syntetisk stream."""
        sys.path.insert(0, str(HERE))
        from test_switcher import make_stream, run_host_daemon
        log("[mock] gestus: kører switcher_host (ægte C-kode) med syntetisk center-hold 5,2 s-stream")
        txt = run_host_daemon(make_stream(1), "mock_gestus")
        gate("GESTUS UDLØST" in txt, "gestus detekteret af switcher-koden (fil-mode)")
        self._set_demo(B_EXE)
        log("[mock] SIMULERET runtime-skift A→B som følge af gestus (exe → B)")

    def switcher_log(self):
        f = TESTLOGS / "mock_gestus.switcher.log"
        return f.read_text() if f.exists() else ""

    def clean_tmp(self):
        t = self.root / "tmp"
        if t.exists():
            for f in t.iterdir():
                if f.is_file():
                    f.unlink()
        log(f"[mock] /tmp ren: {[f.name for f in t.iterdir()] if t.exists() else []}")


# ================================ STATE ================================

def load_state(be):
    if be.exists(STATE_PATH):
        try:
            return json.loads(be.read(STATE_PATH).decode())
        except Exception as e:
            log(f"[state] kunne ikke parses ({e}) — tom state")
    return {}


def save_state(be, state):
    be.mkdir_p(BOOTDIR)
    be.write_small(STATE_PATH, json.dumps(state, indent=1).encode())
    BACKUPS.mkdir(parents=True, exist_ok=True)
    (BACKUPS / "state_snapshot.json").write_text(json.dumps(state, indent=1))


def now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")


# ================================ TRIN ================================

def step_mock_setup(be, args):
    gate(isinstance(be, MockBackend), "mock-setup kræver --mock-root")
    r = be.root
    for d in ("customer/libs", "customer/pinit", "new_customer/libs", "tmp", "proc", "dev/input"):
        (r / d).mkdir(parents=True, exist_ok=True)
    shutil.copy2(ART / "demo.sh", r / "customer/demo.sh")
    (r / "customer/demo.sh").chmod(0o755)
    shutil.copy2(ART / "demo_v9", r / "customer/demo_v9")
    shutil.copy2(ART / "demo_v9", r / "new_customer/demo_v9")
    shutil.copy2(ART / "test.sh", r / "customer/test.sh")
    shutil.copy2(ART / "config.json", r / "customer/config.json")
    be._set_demo(A_EXE)
    log(f"[mock-setup] rod={r} demo.sh md5={be.md5(DEMO_SH)} "
        f"vendor demo_v9 md5={be.md5('/customer/demo_v9')} config md5={be.md5('/customer/config.json')}")


def step_backup(be, args):
    gate(be.exists(DEMO_SH), "demo.sh findes på enheden")
    gate(be.exists("/new_customer/demo_v9"), "/new_customer mountet + B-slot befolket")
    cfg = be.md5("/customer/config.json")
    if cfg != BASELINE_CONFIG_MD5:
        gate(args.force, f"config-md5 {cfg} ≠ baseline {BASELINE_CONFIG_MD5} "
             "(kør REVERT.md's config-restore først, eller --force)")
    flag = be.exists(FLAG)
    be.mkdir_p(BOOTDIR)
    sh_md5 = be.md5(DEMO_SH)
    be.cp(DEMO_SH, BOOTDIR + "/demo.sh.bak")
    gate(be.md5(BOOTDIR + "/demo.sh.bak") == sh_md5, "demo.sh.bak md5-match")
    b_md5 = be.md5("/new_customer/demo_v9")
    be.cp("/new_customer/demo_v9", BOOTDIR + "/demo_v9.vendor")
    gate(be.md5(BOOTDIR + "/demo_v9.vendor") == b_md5, "demo_v9.vendor md5-match")
    state = load_state(be)
    state["backup"] = {"ts": now(), "demo_sh_md5": sh_md5, "vendor_b_md5": b_md5,
                       "config_md5": cfg, "flag_present": flag, "backend": be.name}
    save_state(be, state)
    BACKUPS.mkdir(parents=True, exist_ok=True)
    (BACKUPS / "demo.sh.orig").write_bytes(be.read(DEMO_SH))
    (BACKUPS / "md5_history.json").write_text(json.dumps(state["backup"], indent=1))
    log(f"[backup] FÆRDIG: demo.sh={sh_md5} vendor-B={b_md5} config={cfg} flag={flag}")


def step_install(be, args):
    state = load_state(be)
    gate("backup" in state, "backup-trin kørt først")
    cur_sh = be.md5(DEMO_SH)
    known = {state["backup"]["demo_sh_md5"]}
    if "install" in state:
        known.add(state["install"]["demo_sh_installed_md5"])
    gate(cur_sh in known,
         "demo.sh == backup eller == tidligere install (ingen OTA-drift; idempotent)")
    cfg = be.md5("/customer/config.json")
    if cfg != BASELINE_CONFIG_MD5:
        gate(args.force, f"config-md5 {cfg} ≠ baseline (se REVERT.md; eller --force)")
    st = be.demo_state()
    gate(st and all(e == A_EXE for _, e in st),
         f"install kræver A-mode (stock kører, B-binary er ikke i brug): {st}")
    # md5 læses VED KØRSEL — fanger Fables v6.1 hvis filen er ændret undervejs
    ourui_src = OURUI / "ourui"
    gate(ourui_src.is_file(), f"ourui-kilde findes: {ourui_src}")
    elf_arm_static(ourui_src)
    elf_arm_static(HERE / "switcher")
    elf_arm_static(HERE / "tinj_hold")
    ourui_md5 = md5b(ourui_src.read_bytes())
    log(f"[install] ourui-kilde: {ourui_src} md5={ourui_md5} "
        f"mtime={datetime.datetime.fromtimestamp(ourui_src.stat().st_mtime).isoformat()} "
        "(md5 læst ved kørsel → v6.1 fanges automatisk)")
    be.mkdir_p(BOOTDIR)
    be.mkdir_p("/new_customer/ourui")
    be.push(HERE / "switcher", BOOTDIR + "/switcher")
    be.push(HERE / "tinj_hold", BOOTDIR + "/tinj_hold")
    be.push(HERE / "grab_shim.so", BOOTDIR + "/grab_shim.so")
    be.push(OURUI / "fbpage", BOOTDIR + "/fbpage")
    be.push(ourui_src, "/new_customer/demo_v9")
    gate(be.md5("/new_customer/demo_v9") == ourui_md5, "ourui installeret som B's demo_v9 (md5)")
    # --- demo.sh: TILFØJ ÉN LINJE før slot-branch, efter mounts/insmod ---
    content = be.read(DEMO_SH).decode()
    if GUARD_LINE in content:
        log("[install] guard-linjen findes allerede (idempotent) — verificerer placering")
        new_content = content
    elif LEGACY_GUARD_LINE in content:
        # v6.11-installation → opgradér guard-linjen (shim-export), stadig ÉN linje
        gate(content.count(LEGACY_GUARD_LINE) == 1, "præcis ÉN legacy guard-linje")
        new_content = content.replace(LEGACY_GUARD_LINE, GUARD_LINE)
        gate(sum(1 for l in new_content.splitlines() if "/ourui/boot/switcher" in l) == 1,
             "præcis ÉN guard-linje efter opgradering")
        gate(new_content.index(GUARD_LINE) < new_content.rindex(ANCHOR),
             "guard-linjen står før boot-branchen")
        gate(new_content.index(GUARD_LINE) > new_content.rindex(INSMOD_MARKER),
             "guard-linjen står efter mounts/insmod")
        log("[install] legacy guard-linje opgraderet til v6.12 (LD_PRELOAD=grab_shim.so)")
    else:
        lines = content.splitlines(keepends=True)
        anchor_idx = max(i for i, l in enumerate(lines) if ANCHOR in l)
        insmod_idx = max(i for i, l in enumerate(lines) if INSMOD_MARKER in l)
        gate(anchor_idx > insmod_idx, "slot-branch-anchor ligger efter insmod/mounts")
        gate("starting from B" in "".join(lines[anchor_idx:anchor_idx + 6]),
             "anchor er BOOT-branchen (ikke insmod-branchen)")
        lines.insert(anchor_idx, GUARD_LINE + "\n")
        new_content = "".join(lines)
        log(f"[install] guard-linje indsat før linje {anchor_idx + 1} (slot-branch): {GUARD_LINE}")
    expected = md5b(new_content.encode())
    # skriv modificeret demo.sh til boot/demo.sh.new, så atomisk cross-fs-install
    local_new = HERE / "demo.sh.new"
    local_new.write_text(new_content)
    be.push(local_new, BOOTDIR + "/demo.sh.new")
    be.install_verified(BOOTDIR + "/demo.sh.new", DEMO_SH, expected, 0o755)
    readback = be.read(DEMO_SH).decode()
    gate(GUARD_LINE in readback and readback == new_content, "demo.sh readback == forventet indhold")
    log(f"[install] {REVERT_LINE}")
    state["install"] = {"ts": now(), "ourui_md5": ourui_md5, "ourui_src": str(ourui_src),
                        "switcher_md5": md5b((HERE / "switcher").read_bytes()),
                        "tinj_hold_md5": md5b((HERE / "tinj_hold").read_bytes()),
                        "grab_shim_md5": md5b((HERE / "grab_shim.so").read_bytes()),
                        "fbpage_md5": md5b((OURUI / "fbpage").read_bytes()),
                        "demo_sh_orig_md5": state["backup"]["demo_sh_md5"],
                        "demo_sh_installed_md5": expected, "guard_line": GUARD_LINE,
                        "revert_line": REVERT_LINE}
    save_state(be, state)
    (BACKUPS / "demo.sh.installed").write_text(new_content)
    log(f"[install] FÆRDIG: demo.sh={expected} ourui(B)={ourui_md5}")


def step_boot_test(be, args):
    state = load_state(be)
    gate("install" in state, "install-trin kørt først")
    gate(be.md5(DEMO_SH) == state["install"]["demo_sh_installed_md5"],
         "demo.sh matcher install-state")
    gate(GUARD_LINE in be.read(DEMO_SH).decode(), "guard-linje til stede")
    results = {}

    # 1. B-boot: flag + reboot → ourui
    be.touch(FLAG)
    be.sync()
    be.reboot()
    st = be.wait_up()
    gate(st and all(e == B_EXE for _, e in st), f"B booter ourui (exe-link): {st}")
    gate(be.switcher_pids(), "switcher startet af guard-linjen (pidof switcher)")
    be.capture("boot_B_ourui")
    results["b_boot"] = "OK"

    # 2. A-boot: rm flag + reboot → stock
    be.rm(FLAG)
    be.sync()
    be.reboot()
    st = be.wait_up()
    gate(st and all(e == A_EXE for _, e in st), f"A booter stock (exe-link): {st}")
    be.capture("boot_A_stock")
    results["a_boot"] = "OK"

    # 3. gestus i A-mode → runtime-skift til B UDEN reboot
    be.gesture()
    time.sleep(5 if be.name == "device" else 0)
    st = be.demo_state()
    gate(st and all(e == B_EXE for _, e in st),
         f"runtime-skift A→B uden reboot (exe-link, ingen A-PID): {st}")
    sl = be.switcher_log()
    if sl:
        TESTLOGS.mkdir(exist_ok=True)
        (TESTLOGS / f"{be.name}_switcher.log").write_text(sl)
        log("[boot-test] switcher.log gemt i test-logs/")
    be.capture("runtime_B_ourui")
    results["gesture_runtime_switch"] = "OK"

    # 4. skift tilbage til A (kill+launch A, SIGSTOP-først; ourui's knap kan også bruges)
    be.launch_a_stopfirst()
    st = be.demo_state()
    gate(len(st) >= 1 and all(e == A_EXE for _, e in st),
         f"tilbage i A uden reboot: {st}")
    be.capture("runtime_A_stock")
    results["switch_back"] = "OK"

    state["boot_test"] = {"ts": now(), "backend": be.name, "results": results,
                          "note": "mock = simuleret boot/gestus-switch; device = ægte"}
    save_state(be, state)
    log(f"[boot-test] FÆRDIG ({be.name}): {results} — maskinen står i A, flag fjernet")


def step_revert_test(be, args):
    state = load_state(be)
    gate("backup" in state, "backup-trin kørt (findes .bak)")
    orig = state["backup"]["demo_sh_md5"]
    gate(be.md5(BOOTDIR + "/demo.sh.bak") == orig, "demo.sh.bak md5 == oprindelig")
    # 1. genskab demo.sh (atomisk) + md5-match
    be.install_verified(BOOTDIR + "/demo.sh.bak", DEMO_SH, orig, 0o755)
    gate(GUARD_LINE not in be.read(DEMO_SH).decode(), "guard-linje væk fra demo.sh")
    # 2. kill switcher + rm flag + reboot → stock A urørt
    be.kill_switcher()
    be.rm(FLAG)
    be.sync()
    be.reboot()
    st = be.wait_up()
    gate(st and all(e == A_EXE for _, e in st), f"stock A booter urørt: {st}")
    gate(not be.switcher_pids(), "switcher kører ikke (linjen fjernet)")
    gate(be.md5(DEMO_SH) == orig, "demo.sh md5 == oprindelig efter revert")
    # 3. genskab B's vendor-demo_v9
    vendor = state["backup"]["vendor_b_md5"]
    gate(be.md5(BOOTDIR + "/demo_v9.vendor") == vendor, "demo_v9.vendor md5 == oprindelig B")
    be.install_verified(BOOTDIR + "/demo_v9.vendor", "/new_customer/demo_v9", vendor, 0o755)
    # 4. /tmp ren + config uændret
    be.clean_tmp()
    cfg = be.md5("/customer/config.json")
    gate(cfg == state["backup"]["config_md5"],
         f"config-md5 uændret ({cfg}) — hvis v6 har GEM'et: restore via REVERT.md først")
    state["revert_test"] = {"ts": now(), "backend": be.name,
                            "demo_sh_md5": orig, "vendor_b_md5": vendor, "config_md5": cfg}
    save_state(be, state)
    log(f"[revert-test] FÆRDIG: stock A urørt, demo.sh={orig}, vendor-B={vendor}, "
        f"config={cfg}, /tmp ren. Backups ligger stadig i {BOOTDIR}.")


def step_persist(be, args):
    choice = args.default_boot
    gate(choice in ("A", "B"), "--default-boot A|B påkrævet")
    if choice == "B":
        be.touch(FLAG)
    else:
        be.rm(FLAG)
    be.sync()
    gate(be.exists(FLAG) == (choice == "B"), f"flag-tilstand verificeret: {choice}")
    state = load_state(be)
    state["persist"] = {"ts": now(), "default_boot": choice}
    save_state(be, state)
    log(f"[persist] default-boot = {choice} (gælder NÆSTE boot; ingen reboot nu)")
    if choice == "B" and state.get("revert_test"):
        log("[persist] ADVARSEL: revert-test er kørt → B's demo_v9 er nu VENDOR-binary, "
            "ikke ourui. Kør install igen før B vælges som default.")


def step_emergency(be, args):
    log("[EMERGENCY] start — genskaber demo.sh, dræber switcher, efterlader maskinen i A")
    errs = []

    def try_(desc, fn):
        try:
            fn()
            log(f"[EMERGENCY] {desc}: OK")
        except Exception as e:
            errs.append(f"{desc}: {e}")
            log(f"[EMERGENCY] {desc}: FEJL {e}")

    state = load_state(be)
    orig = state.get("backup", {}).get("demo_sh_md5")
    if orig and be.exists(BOOTDIR + "/demo.sh.bak"):
        if be.md5(DEMO_SH) != orig:
            try_("genskab demo.sh fra .bak",
                 lambda: be.install_verified(BOOTDIR + "/demo.sh.bak", DEMO_SH, orig, 0o755))
        else:
            log("[EMERGENCY] demo.sh allerede == .bak")
    else:
        log("[EMERGENCY] ingen backup-state — demo.sh kan ikke genskabes automatisk; "
            "brug BOOT.md §5 manuel procedure")
    try_("kill switcher", be.kill_switcher)

    def ensure_a():
        st = be.demo_state()
        has_a = any(e == A_EXE for _, e in st)
        has_b = any(e == B_EXE for _, e in st)
        if has_a and has_b:
            # blandet tilstand (må ikke ske): dræb B, behold A
            if be.name == "device":
                for pid, e in st:
                    if e == B_EXE:
                        be.telnet(f"kill -9 {pid} 2>/dev/null; echo drabt_B_{pid}",
                                  allow_fail=True)
            else:
                be._set_demo(A_EXE)
            log("[EMERGENCY] blandet A+B: B dræbt, A beholdt")
        elif has_b:
            be.launch_a_stopfirst()
        elif not st:
            be.launch_a_direct()
        else:
            # A kører evt. SIGSTOP'et (switcher kan være dræbt midt i skift) → SIGCONT
            if be.name == "device":
                for pid, e in st:
                    if e == A_EXE:
                        be.telnet(f"kill -CONT {pid} 2>/dev/null; echo cont_{pid}",
                                  allow_fail=True)
            log("[EMERGENCY] A kører allerede (SIGCONT sendt for en sikkerheds skyld)")
    try_("sikr A-mode", ensure_a)
    try_("rm flag", lambda: be.rm(FLAG))
    try_("sync", be.sync)
    if errs:
        log(f"[EMERGENCY] UAFKLAREDE FEJL: {errs} — KØR MANUELT efter BOOT.md §5")
    else:
        log("[EMERGENCY] FÆRDIG: maskinen står i A, demo.sh genskabt, switcher dræbt, flag væk")
    return errs


def step_status(be, args):
    log(f"[status] backend={be.name} flag={be.exists(FLAG)}")
    log(f"[status] demo_v9: {be.demo_state()}  switcher: {be.switcher_pids()}")
    for p in (DEMO_SH, "/customer/demo_v9", "/new_customer/demo_v9",
              "/customer/config.json", BOOTDIR + "/demo.sh.bak",
              BOOTDIR + "/demo_v9.vendor", BOOTDIR + "/switcher"):
        log(f"[status] md5 {p}: {be.md5(p)}")
    state = load_state(be)
    log(f"[status] state-trin: {[k for k in state if k != 'ts']}")


STEPS = {"mock-setup": step_mock_setup, "backup": step_backup, "install": step_install,
         "boot-test": step_boot_test, "revert-test": step_revert_test,
         "persist": step_persist, "emergency": step_emergency, "status": step_status}


def main():
    ap = argparse.ArgumentParser(description="boot-persistens til MerakiTech Gen 1 (se BOOT.md)")
    ap.add_argument("step", choices=sorted(STEPS))
    ap.add_argument("--mock-root", help="lokal mock-rod (dry-run uden enhed)")
    ap.add_argument("--default-boot", choices=["A", "B"],
                    help="persistens-valg: sæt/fjern flag til sidst (påkrævet for persist)")
    ap.add_argument("--force", action="store_true",
                    help="overstyr config-md5-baseline-gate (logges!)")
    args = ap.parse_args()
    BACKUPS.mkdir(parents=True, exist_ok=True)
    TESTLOGS.mkdir(exist_ok=True)
    log(f"===== trin: {args.step} "
        f"({'mock: ' + args.mock_root if args.mock_root else 'DEVICE ' + os.environ.get('TELNET_HOST', '<display-ip>')}) =====")
    if args.mock_root:
        be = MockBackend(args.mock_root)
        if args.step != "mock-setup":
            gate(be.root.is_dir(), f"mock-rod findes: {be.root} (kør mock-setup først)")
    else:
        if args.step == "mock-setup":
            ap.error("mock-setup kræver --mock-root")
        be = DeviceBackend()
    if args.step == "persist" and not args.default_boot:
        ap.error("persist kræver --default-boot A|B")
    try:
        STEPS[args.step](be, args)
        if args.default_boot and args.step != "persist":
            step_persist(be, args)
    except Exception as e:
        log(f"[FEJL] {args.step}: {e}")
        if args.step not in ("emergency", "status", "mock-setup"):
            log("[FEJL] → automatisk emergency (genskab demo.sh + kill switcher + A)")
            step_emergency(be, args)
        sys.exit(1)
    log(f"===== trin {args.step} FÆRDIG =====")


if __name__ == "__main__":
    main()
