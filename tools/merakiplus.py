#!/usr/bin/env python3
"""merakiplus.py — install / revert / inspect Meraki+ on a MerakiTech Gen1/Gen2 display.

Normally used through ./install.sh and ./revert.sh. Python 3.8+, standard library only.

    merakiplus.py status  --ip ADDR            read-only overview (safe)
    merakiplus.py scan                          find the display on your /24 (telnet fingerprint)
    merakiplus.py install --ip ADDR [--persist] [--gesture] [--dry-run] [--yes]
    merakiplus.py revert  --ip ADDR [--purge]  [--dry-run] [--yes]

How it talks to the display: the vendor firmware runs an unauthenticated root telnet
server on port 23. Commands are sent over that; files are pushed with the display's own
busybox `nc -l` (raw TCP), and every file is md5-verified after transfer and after install.

Safety model (see README "Security and OTA" and the disclaimer):
  * Slot A (/customer/demo_v9, libs, test.sh) is never written. config.json is never written.
  * Slot B's existing binary and /customer/demo.sh are backed up (md5-verified) before anything
    is changed; backups are never overwritten by later runs.
  * Replacements are atomic: copy to a temp file on the SAME filesystem, md5-gate, then rename.
    (A rename over a running executable is fine; a cross-fs copy over it fails with ETXTBSY.)
  * The boot flag (/customer/new) is only written with --persist. /customer/demo.sh is only
    written with --gesture (one guard line, reversible).
  * No reboot is ever sent. Activation uses the vendor watchdog (exec-reload) or a
    stop-first runtime switch (SIGSTOP old UI, start new, verify, then kill old; on failure
    the old UI is resumed with SIGCONT).
"""
import argparse
import base64
import datetime
import gzip
import hashlib
import ipaddress
import os
import pathlib
import random
import re
import socket
import sys
import tempfile
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor

VERSION = "7.0"
REPO = "thomasgrastein/merakitech-plus"
RAW_BASE = "https://raw.githubusercontent.com/" + REPO + "/{ref}/bin/{name}"
RAW_AUDIO = "https://raw.githubusercontent.com/" + REPO + "/{ref}/audio/{name}"

# md5 of the shipped device binaries in bin/ (kept in sync with bin/MD5SUMS by CI).
BIN_MD5 = {
    "merakiplus": "f33ce8a1e8c8591cdcd65bf8aea5f97e",
    "switcher": "c9d937fa36c51e2e58276041b6761ac0",
    "grab_shim.so": "c42076c807c54d477a5868977404186d",
    "merakiplus_snd": "a7659c176b8ef78385b0b612a69e81ca",
}
# v6.18: Meraki+'s own sounds (tools/gen_audio.py; docs/audio.md; v6.19: marimba / glockenspiel timbres) — installed next to the sound helper
AUDIO_MD5 = {
    "start.wav": "a03cb807d1cdb629b0614fdb740ec71d",
    "ready_extract.wav": "4e7e24f18a80ee2f237e2f5ed220afbe",
    "ready_steam.wav": "483d57ef122dc9929b004441fe01bc44",
}
MERAKIPLUS_MARK = "@(#)merakiplus "        # embedded in every Meraki+ binary (src/merakiplus.c MERAKIPLUS_MARK)

# ---- device layout (vendor A/B scheme, see docs/boot.md) ----
A_DIR, B_DIR = "/customer", "/new_customer"
A_EXE, B_EXE = "/customer/demo_v9", "/new_customer/demo_v9"
DEMO_SH, TEST_SH, FLAG = "/customer/demo.sh", "/customer/test.sh", "/customer/new"
OURDIR = "/new_customer/merakiplus"
BOOTDIR = OURDIR + "/boot"
STAGE = OURDIR + "/merakiplus"                          # staged copy (same fs as slot B)
BAK_B = BOOTDIR + "/demo_v9.vendor"                     # slot B's original binary
# A newer vendor build found in slot B later (vendor OTA) is backed up as BAK_B.<md5>; STATE names
# the backup revert restores (bak_b=, bak_b_md5=).
BAK_B_RE = re.compile("^" + re.escape(BAK_B) + "(?:\\.[0-9a-f]{32})?$")
STATE_KEYS = ("orig_flag", "orig_b", "first_install", "bak_b", "bak_b_md5")
BAK_B_ABSENT = BOOTDIR + "/demo_v9.vendor.absent"       # marker: slot B had no binary
BAK_SH = BOOTDIR + "/demo.sh.bak"                       # original /customer/demo.sh
STATE = BOOTDIR + "/merakiplus.state"
SND_HELPER = OURDIR + "/merakiplus_snd"                 # v6.18: plays a sound via the display's own libmi_ao.so
AUDIO_DIR = OURDIR + "/audio"                           # v6.18: start.wav, ready_extract.wav, ready_steam.wav
SWITCHER, SHIM = BOOTDIR + "/switcher", BOOTDIR + "/grab_shim.so"
GUARD_LINE = ("[ -x /new_customer/merakiplus/boot/switcher ] && { [ -f /new_customer/merakiplus/boot/grab_shim.so ] "
              "&& export LD_PRELOAD=/new_customer/merakiplus/boot/grab_shim.so${LD_PRELOAD:+:$LD_PRELOAD}; "
              "/new_customer/merakiplus/boot/switcher & }")
GUARD_KEY = "/new_customer/merakiplus/boot/switcher"    # any line containing this is ours
ENV_PREFIX = "MERAKIPLUS_"                              # prefix of the UI's environment variables
ANCHOR = 'if [ "$MOUNT_SUCCESS" -eq 1 ] && [ -f /customer/new ]; then'
INSMOD_MARKER = "# kernel_mod_list"
B_LDP, A_LDP = "/new_customer/libs:/config/wifi", "/customer/libs:/config/wifi"

# ============================================================ legacy migration only
# BEGIN legacy-compat (legacy migration only)
# Installs made before the rename (releases up to v6.16) used a different identifier for the
# install directory, the staged binary, the UI's environment variables and the binary marker.
# This block is the ONLY place in the repository where that identifier may appear
# (tests/hygiene.py enforces it). Nothing else may use these literals.
LEGACY_ID = "ourui"
LEGACY_DIR = B_DIR + "/" + LEGACY_ID                    # former OURDIR
LEGACY_GUARD_KEY = LEGACY_DIR + "/boot/switcher"        # former GUARD_KEY (demo.sh guard line)
LEGACY_ENV_PREFIX = LEGACY_ID.upper() + "_"             # former ENV_PREFIX (only used in a dev run.sh)
LEGACY_MARKS = ("@(#)" + LEGACY_ID + " ",               # v6.2 .. v6.16 what(1) marker
                "== " + LEGACY_ID + ": config.json aendret")   # v6.1 (no marker; its exit text)


def legacy_path(path):
    """The pre-rename location of a path under OURDIR."""
    return LEGACY_DIR + path[len(OURDIR):] if path.startswith(OURDIR) else path


def legacy_rewrite(data):
    """Rewrite a pre-rename helper script (a dev run.sh) to the current identifiers."""
    return (data.replace(LEGACY_ENV_PREFIX.encode(), ENV_PREFIX.encode())
                .replace(LEGACY_ID.encode(), pathlib.PurePosixPath(OURDIR).name.encode()))


def legacy_inspect(dev):
    """{'dir': legacy install directory exists, 'guard': legacy guard lines in demo.sh}."""
    _, g = dev.sh(f"grep -c -F '{LEGACY_GUARD_KEY}' {dev.d(DEMO_SH)} 2>/dev/null; true", check=False)
    last = g.strip().splitlines()[-1] if g.strip() else ""
    return {"dir": dev.exists(LEGACY_DIR), "guard": int(last) if last.isdigit() else 0}


def legacy_migrate(dev, o, st, rewrite_guard):
    """Move a pre-rename install to OURDIR (backups, state, switcher, language, schedule, settings
    backups, a dev run.sh) and, with rewrite_guard, turn its demo.sh guard line into GUARD_LINE.
    Idempotent. Returns True when something was (or, in --dry-run, would be) changed."""
    lg = st["legacy"]
    if not lg["dir"] and not (lg["guard"] and rewrite_guard):
        return False
    o.step(f"Migrating an install from before the rename ({LEGACY_DIR} -> {OURDIR})")
    if lg["dir"]:
        old, new = dev.d(LEGACY_DIR), dev.d(OURDIR)
        if not dev.exists(OURDIR):
            dev.w(f"mv {old} {new} && sync")
            o.ok(f"{LEGACY_DIR} moved to {OURDIR} (same filesystem, atomic)")
        else:
            # both exist (interrupted migration): move what the new directory lacks, never overwrite;
            # anything left over is kept aside in OURDIR/legacy-leftover (removed only by --purge)
            dev.w(f"cd {old} && for f in $(find . -type f -o -type l); do [ -e {new}/$f ] || "
                  f"{{ mkdir -p {new}/$(dirname $f) && mv $f {new}/$f; }}; done; cd /; "
                  f"if [ -n \"$(find {old} -type f -o -type l)\" ]; then rm -rf {new}/legacy-leftover; "
                  f"mv {old} {new}/legacy-leftover; else rm -rf {old}; fi; sync")
            o.ok(f"{LEGACY_DIR} merged into {OURDIR} (existing files kept)")
        src = OURDIR if not dev.dry else LEGACY_DIR
        old_stage = src + "/" + LEGACY_ID
        if dev.exists(old_stage):
            dev.w(f"rm -f {dev.d(old_stage)}; sync")
            o.ok("old staged copy removed (install stages the new binary as " + STAGE + ")")
        run_sh = src + "/run.sh"
        if dev.exists(run_sh):
            cur = dev.read(run_sh)
            new_b = legacy_rewrite(cur)
            if new_b != cur:
                dev.push(new_b, OURDIR + "/run.sh", mode="755")
                o.ok("run.sh rewritten to the new directory, binary name and environment variables")
    if lg["guard"] and rewrite_guard:
        cur_sh = dev.read(DEMO_SH)
        lines = cur_sh.decode("utf-8", "surrogateescape").splitlines(keepends=True)
        out, placed = [], any(GUARD_KEY in l for l in lines)
        for l in lines:
            if LEGACY_GUARD_KEY in l:
                if not placed:
                    out.append(GUARD_LINE + "\n")
                    placed = True
                continue
            out.append(l)
        new_sh = "".join(out).encode("utf-8", "surrogateescape")
        check_demo_sh_edit(cur_sh, new_sh)
        dev.push(new_sh, DEMO_SH, mode="755")
        if not dev.dry and dev.read(DEMO_SH) != new_sh:
            raise Abort("demo.sh read-back differs from what was written — run ./revert.sh now")
        o.ok("demo.sh: the old guard line now points at " + BOOTDIR)
    return True
# END legacy-compat

IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240


class Abort(Exception):
    """Stop cleanly with a message (nothing half-done is left behind at the call sites)."""


def md5b(b):
    return hashlib.md5(b).hexdigest()


def strip_guard(data):
    """demo.sh bytes without any Meraki+ guard line (current or pre-rename)."""
    return b"".join(l for l in data.splitlines(keepends=True)
                    if GUARD_KEY.encode() not in l and LEGACY_GUARD_KEY.encode() not in l)


def check_demo_sh_edit(cur, new):
    """Refuse to write a demo.sh that differs from the current one in anything but guard lines
    (or is empty): the vendor boot script mounts slot B, loads the drivers and starts the UI."""
    if not cur.strip() or not new.strip() or not strip_guard(new).strip():
        raise Abort("Refusing to write an empty /customer/demo.sh — demo.sh was NOT changed.")
    if strip_guard(new) != strip_guard(cur) or sum(GUARD_KEY.encode() in l for l in new.splitlines()) > 1:
        raise Abort("Refusing to write /customer/demo.sh: the new content differs from the current one in "
                    "more than the Meraki+ guard line — demo.sh was NOT changed.")


# ============================================================ console helpers
class Out:
    def __init__(self, logfile=None, verbose=False):
        self.logf = open(logfile, "a", encoding="utf-8") if logfile else None
        self.verbose = verbose

    def _log(self, s):
        if self.logf:
            self.logf.write(datetime.datetime.now().strftime("%H:%M:%S ") + s + "\n")
            self.logf.flush()

    def say(self, s=""):
        print(s, flush=True)
        self._log(s)

    def step(self, s):
        self.say("\n==> " + s)

    def ok(self, s):
        self.say("  [ok] " + s)

    def warn(self, s):
        self.say("  [!]  " + s)

    def debug(self, s):
        self._log(s)
        if self.verbose:
            print("      " + s.replace("\n", "\n      "), flush=True)


def confirm(question, assume_yes):
    if assume_yes:
        return True
    try:
        tty = sys.stdin if sys.stdin.isatty() else open("/dev/tty")
    except OSError:
        raise Abort("No terminal to ask for confirmation — re-run with --yes if you are sure.")
    sys.stdout.write(question + " [y/N] ")
    sys.stdout.flush()
    ans = tty.readline().strip().lower()
    return ans in ("y", "yes", "j", "ja")


# ============================================================ telnet transport
class Device:
    """One persistent telnet shell on the display. sh() always runs (use it for reads);
    w() is for writes and is skipped in --dry-run."""

    def __init__(self, host, port, out, dry=False, prefix=""):
        self.host, self.port, self.o, self.dry = host, port, out, dry
        self.p = prefix                 # test hook: path prefix for the mock device (tests/)
        self.sock = None
        self.changed = False            # set once anything on the display was modified

    # -- low level --
    def _strip_iac(self, chunk):
        clean, i = bytearray(), 0
        while i < len(chunk):
            b = chunk[i]
            if b == IAC and i + 1 < len(chunk):
                cmd = chunk[i + 1]
                if cmd in (DO, DONT, WILL, WONT) and i + 2 < len(chunk):
                    try:
                        self.sock.sendall(bytes([IAC, WONT if cmd in (DO, DONT) else DONT, chunk[i + 2]]))
                    except OSError:
                        pass
                    i += 3
                    continue
                if cmd == SB:
                    j = chunk.find(bytes([IAC, SE]), i)
                    i = j + 2 if j >= 0 else len(chunk)
                    continue
                if cmd == IAC:
                    clean.append(IAC)
                i += 2
                continue
            clean.append(b)
            i += 1
        return bytes(clean)

    def connect(self):
        try:
            self.sock = socket.create_connection((self.host, self.port), timeout=8)
        except OSError as e:
            raise Abort(f"Cannot reach {self.host}:{self.port} ({e}). Is the machine on, on the same "
                        f"network, and is the IP right? Try: ./install.sh --scan")
        self.sock.settimeout(0.4)
        banner, t0 = b"", time.time()
        while time.time() - t0 < 1.2:
            try:
                c = self.sock.recv(65536)
                if not c:
                    break
                banner += self._strip_iac(c)
            except socket.timeout:
                if banner:
                    break
        if re.search(rb"login:|[Pp]assword:", banner):
            raise Abort("The telnet server asks for a login. This firmware is not the open-telnet "
                        "variant Meraki+ was developed on — stopping without changing anything.")

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock = None

    def sh(self, cmd, timeout=30, check=True):
        """Run one shell command, return (rc, output). Raises Abort on transport failure."""
        if self.sock is None:
            self.connect()
        tag = "%08x" % random.getrandbits(32)
        wire = f"printf 'S_%s\\n' {tag}; {{ {cmd} ; }} 2>&1; printf 'E_%s:%s\\n' {tag} $?\r\n"
        self.o.debug("$ " + cmd)
        try:
            self.sock.sendall(wire.encode())
        except OSError as e:
            self.close()
            raise Abort(f"Lost the telnet connection ({e}).")
        end_re = re.compile(rb"(?:^|\n)E_" + tag.encode() + rb":(\d+)\r?\n")
        data, deadline, hung_up = b"", time.time() + timeout, False
        while time.time() < deadline:
            try:
                c = self.sock.recv(65536)
                if not c:
                    hung_up = True
                    break
                data += self._strip_iac(c)
                if end_re.search(data):
                    break
            except socket.timeout:
                continue
        m = end_re.search(data)
        if not m:
            self.close()
            if hung_up:
                raise Abort(f"The display closed the telnet connection during: {cmd[:80]}")
            raise Abort(f"No answer from the display within {timeout}s for: {cmd[:80]}")
        text = data[:m.start()].decode("utf-8", "replace").replace("\r", "")
        lines = text.split("\n")
        try:
            body = "\n".join(lines[lines.index("S_" + tag) + 1:])
        except ValueError:
            body = text
        rc = int(m.group(1))
        self.o.debug(f"  rc={rc} " + body[-1500:])
        if check and rc != 0:
            raise Abort(f"Command failed on the display (rc={rc}): {cmd[:120]}\n{body[-600:]}")
        return rc, body.strip("\n")

    def w(self, cmd, timeout=30, check=True):
        if self.dry:
            self.o.say("  [dry-run] would run: " + cmd)
            return 0, ""
        self.changed = True
        return self.sh(cmd, timeout, check)

    # -- helpers (all paths go through d() so the mock device can relocate them) --
    def d(self, path):
        return self.p + path

    def exists(self, path):
        return self.sh(f"[ -e {self.d(path)} ] && echo Y || echo N")[1].strip().endswith("Y")

    def md5(self, path):
        rc, out = self.sh(f"md5sum {self.d(path)} 2>/dev/null", check=False)
        m = re.search(r"\b([0-9a-f]{32})\b", out)
        return m.group(1) if (rc == 0 and m) else None

    def size(self, path):
        """Size in bytes of a file on the display, or None."""
        rc, out = self.sh(f"wc -c < {self.d(path)} 2>/dev/null", check=False)
        last = out.strip().splitlines()[-1].strip() if out.strip() else ""
        return int(last) if rc == 0 and last.isdigit() else None

    def is_ours(self, path):
        pats = " ".join(f"-e '{m}'" for m in (MERAKIPLUS_MARK,) + LEGACY_MARKS)
        return self.sh(f"grep -q -F {pats} {self.d(path)} 2>/dev/null && echo Y || echo N",
                       check=False)[1].strip().endswith("Y")

    def read(self, path):
        """Byte-exact download (cat through the pty would mangle tabs/newlines), verified against
        the display's own md5 of the file. The pipeline's rc is base64's, so a failing gzip (out of
        memory, missing applet) would otherwise look like a successful read of an empty file."""
        want = self.md5(path)
        if want is None:
            raise Abort(f"Cannot read {path} on the display (missing or unreadable).")
        _, out = self.sh(f"gzip -c {self.d(path)} | base64", timeout=60)
        b64 = "".join(l.strip() for l in out.splitlines() if re.fullmatch(r"[A-Za-z0-9+/=]+", l.strip()))
        try:
            data = gzip.decompress(base64.b64decode(b64))
        except (OSError, EOFError, ValueError) as e:
            raise Abort(f"Reading {path} from the display failed ({e}); nothing was written for this step.")
        if md5b(data) != want:
            raise Abort(f"Reading {path} from the display failed verification (got {len(data)} bytes, md5 "
                        f"{md5b(data)}; the display reports md5 {want}). Nothing was written for this step.")
        return data

    def ui_procs(self):
        """[(pid, exe)] for every running process whose exe is a demo_v9."""
        return [(p, e) for p, e, _ in self.ui_procs_state()]

    def ui_procs_state(self):
        """[(pid, exe, stopped)] for every process whose exe is a demo_v9; stopped is True for a
        process in state T (SIGSTOPped, e.g. by an interrupted stop-first switch).
        Scans /proc instead of `pidof demo_v9`: the vendor watchdog greps `ps` for 'demo_v9',
        and a helper process with that string in its command line would fool it."""
        # After a binary is renamed over, the kernel reports "<path> (deleted)" for the old
        # inode that is still running — that process must still be seen (and stopped).
        _, out = self.sh(f"for d in {self.d('/proc')}/[0-9]*; do e=$(readlink $d/exe 2>/dev/null); "
                         f"case \"$e\" in */demo_v9|*'/demo_v9 (deleted)') echo \"P ${{d##*/}} $e\"; "
                         f"echo \"S ${{d##*/}} $(grep '^State:' $d/status 2>/dev/null)\";; esac; done",
                         check=False)
        res, stopped = [], set()
        for ln in out.splitlines():
            parts = ln.split(" ", 2)
            if len(parts) == 3 and parts[0] == "P" and parts[1].isdigit():
                exe = parts[2].strip()
                if exe.endswith(" (deleted)"):
                    exe = exe[:-len(" (deleted)")]
                if self.p and exe.startswith(self.p):
                    exe = exe[len(self.p):]
                res.append((int(parts[1]), exe))
            elif len(parts) == 3 and parts[0] == "S" and parts[1].isdigit():
                f = parts[2].split()
                if len(f) > 1 and f[0] == "State:" and f[1] in ("T", "t"):
                    stopped.add(int(parts[1]))
        return [(p, e, p in stopped) for p, e in res]

    def watchdog_slot(self):
        """'A' / 'B' / None — which slot the vendor watchdog (test.sh <slot>) restarts."""
        _, out = self.sh(f"ps 2>/dev/null | grep '[t]est.sh' | sed 's/^/W /'; "
                         f"for d in {self.d('/proc')}/[0-9]*; do c=$(tr '\\000' ' ' < $d/cmdline 2>/dev/null); "
                         f"case \"$c\" in *test.sh*) echo \"W $c\";; esac; done", check=False)
        for ln in out.splitlines():
            if ln.startswith("W ") and "test.sh" in ln:
                if re.search(r"test\.sh\s+new_customer", ln):
                    return "B"
                if re.search(r"test\.sh\s+customer", ln):
                    return "A"
        return None

    def pids_named(self, name):
        _, out = self.sh(f"for d in {self.d('/proc')}/[0-9]*; do e=$(readlink $d/exe 2>/dev/null); "
                         f"case \"$e\" in */{name}|*'/{name} (deleted)') echo \"P ${{d##*/}}\";; esac; done", check=False)
        return [int(l.split()[1]) for l in out.splitlines() if l.startswith("P ") and l.split()[1].isdigit()]

    def push(self, local_bytes, dest, mode="755"):
        """Raw upload via the display's busybox `nc -l`, written to dest.part on dest's own
        filesystem, md5-verified, then renamed over dest (atomic)."""
        want = md5b(local_bytes)
        dpath = self.d(dest)
        part = dpath + ".part"
        if self.dry:
            self.o.say(f"  [dry-run] would upload {len(local_bytes)} bytes (md5 {want}) -> {dest}")
            return want
        last_err = None
        self.changed = True
        for attempt in range(3):
            port = random.randint(40000, 49999)
            _, out = self.sh(f"rm -f {part}; nohup nc -l -p {port} > {part} 2>/dev/null < /dev/null & "
                             f"echo NCPID=$!; sleep 1")
            ncpid = (re.search(r"NCPID=(\d+)", out) or [None, ""])[1]
            sent = False
            for _ in range(10):
                try:
                    s = socket.create_connection((self.host, port), timeout=10)
                    s.settimeout(30)
                    s.sendall(local_bytes)
                    s.shutdown(socket.SHUT_WR)
                    try:
                        while s.recv(4096):
                            pass
                    except OSError:
                        pass
                    s.close()
                    sent = True
                    break
                except OSError as e:
                    last_err = e
                    time.sleep(0.7)
            got = None
            for _ in range(20 if sent else 1):
                got = self.md5(dest + ".part")
                if got == want:
                    break
                time.sleep(0.5)
            if ncpid:
                self.sh(f"kill {ncpid} 2>/dev/null; true", check=False)
            if got == want:
                rc, out = self.sh(f"chmod {mode} {part} && mv -f {part} {dpath} && sync", check=False)
                if rc != 0:
                    self.sh(f"rm -f {part}; sync", check=False)
                    raise Abort(f"Could not move the verified upload into place: {dest} (rc={rc}) {out[-300:]}")
                if self.md5(dest) != want:
                    raise Abort(f"md5 mismatch after rename: {dest}")
                self.o.debug(f"push ok {dest} {want}")
                return want
            self.sh(f"rm -f {part}", check=False)
            last_err = last_err or f"md5 {got} != {want}"
            self.o.warn(f"upload attempt {attempt + 1} failed ({last_err})" + ("; retrying" if attempt < 2 else ""))
        raise Abort(f"Could not upload {dest}: {last_err}")

    def atomic_copy(self, src, dest, want_md5, mode="755"):
        """cp src -> .dest.tmp (same fs as dest), md5-gate, rename over dest, verify."""
        dp = pathlib.PurePosixPath(self.d(dest))
        tmp = str(dp.parent / ("." + dp.name + ".tmp"))
        if self.dry:
            self.o.say(f"  [dry-run] would copy {src} -> {dest} via {tmp} (md5 {want_md5[:8]}…)")
            return
        self.changed = True
        rc, out = self.sh(f"rm -f {tmp}; cp {self.d(src)} {tmp} && chmod {mode} {tmp} && sync", check=False)
        if rc != 0:                   # e.g. ENOSPC after a partial write: never leave the partial file
            self.sh(f"rm -f {tmp}; sync", check=False)
            raise Abort(f"Copying {src} -> {dest} failed on the display (rc={rc}): {out[-300:]}\n"
                        f"{dest} left untouched; the partial temp file was removed.")
        got = self.md5(str(pathlib.PurePosixPath(dest).parent / ("." + dp.name + ".tmp")))
        if got != want_md5:
            self.sh(f"rm -f {tmp}", check=False)
            raise Abort(f"Copy to {tmp} has md5 {got}, expected {want_md5}; {dest} left untouched.")
        rc, out = self.sh(f"mv -f {tmp} {self.d(dest)} && sync", check=False)
        if rc != 0:
            self.sh(f"rm -f {tmp}; sync", check=False)
            raise Abort(f"Could not rename {tmp} over {dest} (rc={rc}): {out[-300:]}")
        if self.md5(dest) != want_md5:
            raise Abort(f"{dest} has the wrong md5 after rename — run revert.sh.")


# ============================================================ binaries
def repo_root():
    return pathlib.Path(__file__).resolve().parent.parent


def is_checkout():
    """True when this script runs from a git clone (bin/MD5SUMS next to it). install.sh's
    standalone (curl) mode runs a copy from a mktemp dir, whose parent is e.g. /tmp."""
    return (repo_root() / "bin" / "MD5SUMS").is_file()


def default_log_dir():
    return repo_root() / "logs" if is_checkout() else pathlib.Path.home() / "merakiplus-logs"


def get_binary(name, args, out, sub="bin"):
    """Local bin/<name> (git clone) or download from GitHub raw; md5-verified either way.
    sub="audio": the same for audio/<name> (v6.18), gated by AUDIO_MD5."""
    want = (AUDIO_MD5 if sub == "audio" else BIN_MD5)[name]
    cands = []
    if getattr(args, "bin_dir", None) and sub == "bin":
        cands.append(pathlib.Path(args.bin_dir) / name)
    if is_checkout():
        cands.append(repo_root() / sub / name)
    for p in cands:
        if p.is_file():
            data = p.read_bytes()
            if md5b(data) != want:
                raise Abort(f"{p} has md5 {md5b(data)}, expected {want}. Corrupt or modified file — "
                            f"re-download the repo (or pass --bin-dir to a verified copy).")
            out.ok(f"{name}: local {p} (md5 {want})")
            return data
    url = (RAW_AUDIO if sub == "audio" else RAW_BASE).format(ref=args.ref, name=name)
    out.say(f"  downloading {url}")
    try:
        with urllib.request.urlopen(url, timeout=60) as r:
            data = r.read()
    except Exception as e:
        raise Abort(f"Download failed ({e}). Clone the repo instead: git clone https://github.com/{REPO}")
    if md5b(data) != want:
        raise Abort(f"Downloaded {name} has md5 {md5b(data)}, expected {want} — refusing to use it.")
    out.ok(f"{name}: downloaded, md5 {want} verified")
    return data


def check_arm_elf(data, name, shared=False):
    if not (data[:4] == b"\x7fELF" and data[4] == 1 and int.from_bytes(data[18:20], "little") == 40):
        raise Abort(f"{name} is not a 32-bit ARM ELF")
    et = int.from_bytes(data[16:18], "little")
    if et != (3 if shared else 2):
        raise Abort(f"{name}: unexpected ELF type {et}")


# ============================================================ state inspection
def inspect(dev):
    st = {}
    rc, out = dev.sh(f"[ -f {dev.d(DEMO_SH)} ] && [ -f {dev.d(A_EXE)} ] && [ -f {dev.d(TEST_SH)} ] "
                     f"&& grep -q new_customer {dev.d(DEMO_SH)} && echo FP_OK; uname -m", check=False)
    st["fingerprint"] = "FP_OK" in out
    st["arch"] = out.splitlines()[-1].strip() if out.strip() else "?"
    _, mnt = dev.sh("cat /proc/mounts", check=False)
    st["b_mounted"] = bool(dev.p) or any(len(l.split()) > 1 and l.split()[1] == B_DIR for l in mnt.splitlines())
    ps = dev.ui_procs_state()
    st["procs"] = [(p, e) for p, e, _ in ps]
    st["stopped"] = [p for p, _, stop in ps if stop]
    st["watchdog"] = dev.watchdog_slot()
    st["flag"] = dev.exists(FLAG)
    st["b_exists"] = dev.exists(B_EXE)
    st["b_md5"] = dev.md5(B_EXE) if st["b_exists"] else None
    st["b_ours"] = dev.is_ours(B_EXE) if st["b_exists"] else False
    st["a_md5"] = dev.md5(A_EXE)
    st["legacy"] = legacy_inspect(dev)

    def where(path):      # current location, or the pre-rename one while a migration is pending
        if dev.exists(path):
            return path
        return legacy_path(path) if st["legacy"]["dir"] and dev.exists(legacy_path(path)) else None
    st["bak_b"] = bool(where(BAK_B))
    st["bak_b_absent"] = bool(where(BAK_B_ABSENT))
    st["bak_sh"] = bool(where(BAK_SH))
    _, g = dev.sh(f"grep -c -F '{GUARD_KEY}' {dev.d(DEMO_SH)} 2>/dev/null; true", check=False)
    st["guard_lines"] = (int(g.strip().splitlines()[-1]) if g.strip() and g.strip().splitlines()[-1].isdigit() else 0) \
        + st["legacy"]["guard"]
    st["switcher_pids"] = dev.pids_named("switcher")
    st["state"] = {}
    sp = where(STATE)
    if sp:
        _, s = dev.sh(f"cat {dev.d(sp)}", check=False)
        st["state"] = dict(l.split("=", 1) for l in s.splitlines() if "=" in l)
    _, df = dev.sh(f"df -k {dev.d(B_DIR)} 2>/dev/null | tail -n 1", check=False)
    try:
        st["b_free_kb"] = int(df.split()[3])
    except (IndexError, ValueError):
        st["b_free_kb"] = None
    return st


def slot_of(exe):
    return {A_EXE: "A (stock)", B_EXE: "B"}.get(exe, exe)


def print_status(o, st):
    o.say(f"  device fingerprint : {'MerakiTech/Sanbon display (OK)' if st['fingerprint'] else 'NOT RECOGNISED'}"
          f"  arch={st['arch']}")
    o.say(f"  running UI         : " + (", ".join(f"pid {p} = {slot_of(e)}"
                                                    + (" (PAUSED by SIGSTOP)" if p in st.get("stopped", ()) else "")
                                                    for p, e in st["procs"]) or "none"))
    o.say(f"  watchdog restarts  : slot {st['watchdog'] or '?'}")
    o.say(f"  boot flag          : {'SET -> next boot starts slot B' if st['flag'] else 'not set -> next boot starts slot A (stock)'}")
    b = "missing" if not st["b_exists"] else ("Meraki+" if st["b_ours"] else "vendor/other") + f" (md5 {st['b_md5']})"
    o.say(f"  slot B binary      : {b}")
    o.say(f"  slot A binary      : md5 {st['a_md5']} (never modified)")
    o.say(f"  demo.sh guard line : {st['guard_lines']}   switcher running: {bool(st['switcher_pids'])}")
    o.say(f"  backups            : slot-B {'yes' if st['bak_b'] or st['state'].get('bak_b') else ('was-empty' if st['bak_b_absent'] else 'no')}"
          f", demo.sh {'yes' if st['bak_sh'] else 'no'}")
    if st["legacy"]["dir"] or st["legacy"]["guard"]:
        o.say("  older install      : found in the pre-rename layout — migrated by install / revert")


def vendor_backup(dev, st):
    """(device path, md5) of the slot-B vendor backup that revert restores, or (None, None).
    STATE names it (bak_b / bak_b_md5); installs that predate that only have BAK_B."""
    sp = st["state"].get("bak_b", "")
    if BAK_B_RE.match(sp) and dev.exists(sp):
        want = st["state"].get("bak_b_md5", "")
        return sp, (want if re.fullmatch(r"[0-9a-f]{32}", want) else dev.md5(sp))
    if st["bak_b"]:
        path = BAK_B if dev.exists(BAK_B) or not st["legacy"]["dir"] else legacy_path(BAK_B)
        return path, dev.md5(path)
    return None, None


def plan_vendor_backup(dev, st):
    """Where slot B's current binary must be backed up before install overwrites it, or None
    (slot B empty, already Meraki+, or that exact vendor binary is already backed up). A vendor
    build that differs from the existing backup (a vendor OTA since then) gets its own
    md5-named backup: no vendor binary is ever destroyed without a copy."""
    if not st["b_exists"] or st["b_ours"]:
        return None
    bak_path, bak_md5 = vendor_backup(dev, st)
    if bak_md5 == st["b_md5"]:
        return None
    if not (bak_path or st["bak_b"] or st["bak_b_absent"]):
        return BAK_B
    return BAK_B + "." + st["b_md5"]


def write_state(dev, state):
    lines = "".join(f"{k}={state[k]}\\n" for k in STATE_KEYS
                    if k in state and re.fullmatch(r"[A-Za-z0-9_.:/+-]*", str(state[k])))
    dev.w(f"printf '{lines}' > {dev.d(STATE)}.tmp && mv -f {dev.d(STATE)}.tmp {dev.d(STATE)} && sync")


def leftover_globs(dev):
    """Shell globs of the temp files an interrupted install can leave (atomic_copy .X.tmp,
    push X.part, STATE.tmp). All are ours by name; revert removes them."""
    return " ".join(dev.d(g) for g in (
        B_DIR + "/.demo_v9.tmp", A_DIR + "/.demo.sh.tmp", DEMO_SH + ".part",
        BOOTDIR + "/.*.tmp", BOOTDIR + "/*.tmp", BOOTDIR + "/*.part",
        OURDIR + "/.*.tmp", OURDIR + "/*.part", AUDIO_DIR + "/*.part"))


def clean_leftovers(dev, o):
    globs = leftover_globs(dev)
    _, out = dev.sh(f"for f in {globs}; do [ -e \"$f\" ] && echo \"L $f\"; done; true", check=False)
    found = [l[2:] for l in out.splitlines() if l.startswith("L ")]
    if found:
        dev.w(f"rm -f {globs}; sync")
        o.ok(f"removed {len(found)} temporary file(s) left by an interrupted run")


def check_free_space(dev, o, st, uploads, merakiplus):
    """Space on /new_customer for what this install writes: the vendor backup (if one is taken),
    the staged binary plus the temp copy that sits beside slot B's old inode while the running
    UI still holds it, and every helper/sound that is not already up to date (each is written
    as X.part next to the old file)."""
    if st["b_free_kb"] is None:
        return
    want = BIN_MD5["merakiplus"]
    need, parts = 0, []
    if plan_vendor_backup(dev, st):
        bsize = dev.size(B_EXE) or 0
        need += bsize
        parts.append(f"vendor backup {bsize // 1024} KB")
    m = (len(merakiplus) if dev.md5(STAGE) != want else 0) + (len(merakiplus) if st["b_md5"] != want else 0)
    up = sum(len(data) for dest, data in uploads if dev.md5(dest) != md5b(data))
    need += m + up
    if m:
        parts.append(f"Meraki+ {m // 1024} KB")
    if up:
        parts.append(f"helpers/sounds {up // 1024} KB")
    if not need:
        return
    need_kb = (need + 1023) // 1024 + 64          # + a margin for directory blocks / the log
    if st["b_free_kb"] < need_kb:
        raise Abort(f"Only {st['b_free_kb']} KB free on /new_customer — this install needs about {need_kb} KB "
                    f"({', '.join(parts)}). Nothing was changed.")
    o.ok(f"free space on /new_customer: {st['b_free_kb']} KB (needs about {need_kb} KB)")


def require_device(st):
    if not st["fingerprint"]:
        raise Abort("This does not look like a MerakiTech/Sanbon display (no /customer/demo.sh, "
                    "/customer/demo_v9 and /customer/test.sh). Nothing was changed.")
    if not st["arch"].startswith("armv7"):
        raise Abort(f"Unexpected CPU architecture '{st['arch']}' (expected armv7l). Nothing was changed.")
    if not st["b_mounted"]:
        raise Abort("Slot B (/new_customer) is not mounted on this display, so Meraki+ cannot be "
                    "installed there. Nothing was changed.")


def resume_stopped(dev, o, st):
    """A UI left SIGSTOPped (frozen screen) by an interrupted earlier run is resumed before
    anything else; the watchdog never restarts it, because its pid still exists."""
    if not st.get("stopped"):
        return st
    pids = " ".join(map(str, st["stopped"]))
    if dev.dry:
        o.say(f"  [dry-run] would resume the paused UI process(es) {pids} (kill -CONT)")
        return st
    o.warn(f"UI process(es) {pids} are paused (SIGSTOP), probably by an interrupted earlier run — resuming")
    dev.sh(f"kill -CONT {pids}", check=False)
    st2 = inspect(dev)
    if st2["stopped"]:
        raise Abort(f"Could not resume the paused UI process(es) {st2['stopped']}. Power-cycle the machine.")
    o.ok("paused UI resumed")
    return st2


# ============================================================ activation (no reboot)
def find_new(dev, exe, old, budget):
    t_end = time.time() + budget
    while time.time() < t_end:
        procs = dev.ui_procs()
        new = [p for p, e in procs if e == exe and p not in old]
        if new:
            return new[0], procs
        time.sleep(1)
    return None, dev.ui_procs()


def stop_first_switch(dev, o, target_dir, target_exe, ldp, logname):
    """Runtime switch without reboot: SIGSTOP the running UI(s) (the watchdog still sees the
    pid, so it does not race us), start the target, verify it, then kill the old one.
    On failure the old UI is resumed with SIGCONT and the new one is killed."""
    old = [p for p, _ in dev.ui_procs()]
    olds = " ".join(map(str, old))
    dev.changed = True
    try:
        if olds:
            dev.sh(f"for p in {olds}; do kill -STOP $p; done")
        dev.sh(f"cd {dev.d(target_dir)} && LD_LIBRARY_PATH={ldp} nohup ./demo_v9 > {dev.d('/tmp/' + logname)} "
               f"2>&1 < /dev/null & sleep 2")
        pid, procs = find_new(dev, target_exe, old, 8)
    except BaseException:
        # Ctrl-C, a dropped connection or a timeout between STOP and the final kill: never leave
        # the old UI frozen. Reconnect, resume it, stop a half-started new one, then re-raise.
        undo_switch(dev, o, olds, old, target_exe)
        raise
    if pid is None:
        if olds:
            dev.sh(f"for p in {olds}; do kill -CONT $p; done", check=False)
        stray = [p for p, e in procs if e == target_exe and p not in old]
        if stray:
            dev.sh("kill -9 " + " ".join(map(str, stray)), check=False)
        raise Abort(f"{target_exe} did not start; the previous UI was resumed. "
                    f"See /tmp/{logname} on the display.")
    if olds:
        dev.sh(f"kill -9 {olds}; sleep 1", check=False)
    return pid


def undo_switch(dev, o, olds, old, target_exe):
    dev.close()                       # the connection may be dead or mid-command; sh() reconnects
    try:
        if olds:
            dev.sh(f"for p in {olds}; do kill -CONT $p; done", timeout=10, check=False)
        stray = [p for p, e in dev.ui_procs() if e == target_exe and p not in old]
        if stray:
            dev.sh("kill -9 " + " ".join(map(str, stray)), timeout=10, check=False)
        o.warn("the UI switch was interrupted; the previous UI was resumed")
    except BaseException as e:        # best effort; the original error is re-raised by the caller
        o.warn(f"the UI switch was interrupted and the previous UI could not be resumed ({e}). "
               f"Run the same command again: it resumes a paused UI first.")


def activate_meraki(dev, o, st, want_md5):
    """Make the freshly installed slot-B binary the running UI."""
    procs = st["procs"]
    if len(procs) > 1:
        raise Abort(f"Several UIs are running at once ({procs}) — unusual state; not touching it. "
                    "Power-cycle the machine and run install again.")
    if procs and procs[0][1] == B_EXE and dev.md5(f"/proc/{procs[0][0]}/exe") == want_md5:
        o.ok(f"Meraki+ {VERSION} is already the running UI (pid {procs[0][0]})")
        return procs[0][0]
    if dev.dry:
        how = "watchdog exec-reload (kill -TERM, watchdog restarts slot B)" if st["watchdog"] == "B" \
            else "stop-first runtime switch (SIGSTOP stock, start slot B, verify, kill stock)"
        o.say(f"  [dry-run] would activate via {how}")
        return None
    if st["watchdog"] == "B" and procs:
        old = procs[0][0]
        o.say(f"  watchdog exec-reload: kill -TERM {old}; the vendor watchdog restarts {B_EXE}")
        dev.changed = True
        dev.sh(f"kill -TERM {old}")
        pid, _ = find_new(dev, B_EXE, [old], 15)
        if pid is None:
            o.warn("watchdog did not restart slot B within 15 s — starting it directly")
            pid = stop_first_switch(dev, o, B_DIR, B_EXE, B_LDP, "merakiplus_boot.log")
    else:
        o.say("  stop-first runtime switch: pause stock UI, start Meraki+, verify, then stop stock")
        pid = stop_first_switch(dev, o, B_DIR, B_EXE, B_LDP, "merakiplus_boot.log")
    return pid


def verify_running(dev, o, pid, want_md5):
    exe = dev.sh(f"readlink {dev.d('/proc')}/{pid}/exe", check=False)[1].strip()
    if exe != dev.d(B_EXE):
        raise Abort(f"pid {pid} runs {exe}, not {B_EXE}")
    live = dev.md5(f"/proc/{pid}/exe")
    if live != want_md5:
        raise Abort(f"The running binary has md5 {live}, expected {want_md5}")
    o.ok(f"running UI pid {pid}: exe {B_EXE}, live md5 {live}")
    time.sleep(5)
    if not dev.exists(f"/proc/{pid}/exe"):
        raise Abort(f"Meraki+ (pid {pid}) exited within 5 s. The vendor watchdog will restart a UI; "
                    "run ./revert.sh to go back to stock and please open an issue.")
    o.ok("still running after 5 s (no crash loop)")
    _, lg = dev.sh(f"grep 'net-vagt startet' {dev.d('/tmp/merakiplus_net.log')} 2>/dev/null | tail -n 1", check=False)
    if ("V" + VERSION) in lg:
        o.ok(f"Meraki+ log confirms version V{VERSION}")


# ============================================================ install
def cmd_install(dev, o, args):
    o.step("1/7 Checking the display (read-only)")
    st = inspect(dev)
    print_status(o, st)
    require_device(st)
    st = resume_stopped(dev, o, st)
    if not st["procs"]:
        o.warn("no UI process running right now (the watchdog usually restarts one within a second)")

    o.step("2/7 Getting and verifying the Meraki+ binaries")
    merakiplus = get_binary("merakiplus", args, o)
    check_arm_elf(merakiplus, "merakiplus")
    want = BIN_MD5["merakiplus"]
    snd = get_binary("merakiplus_snd", args, o)                  # v6.18: sound helper + our own sounds
    check_arm_elf(snd, "merakiplus_snd")
    sounds = {n: get_binary(n, args, o, sub="audio") for n in AUDIO_MD5}
    extra = {}
    if args.gesture:
        extra["switcher"] = get_binary("switcher", args, o)
        check_arm_elf(extra["switcher"], "switcher")
        extra["grab_shim.so"] = get_binary("grab_shim.so", args, o)
        check_arm_elf(extra["grab_shim.so"], "grab_shim.so", shared=True)
    uploads = [(SND_HELPER, snd)] + [(AUDIO_DIR + "/" + n, d) for n, d in sounds.items()] + \
        [(dest, extra[name]) for name, dest in (("switcher", SWITCHER), ("grab_shim.so", SHIM)) if name in extra]
    check_free_space(dev, o, st, uploads, merakiplus)

    o.say("")
    o.say("  Meraki+ will replace the display's touch UI. The coffee machine's own controller is not")
    o.say("  changed. Make sure the machine is IDLE: not brewing, steaming, grinding or dispensing water.")
    if not dev.dry and not confirm("  Machine is idle and you accept the risks described in the README?", args.yes):
        raise Abort("Cancelled by user. Nothing was changed.")

    if legacy_migrate(dev, o, st, rewrite_guard=True) and not dev.dry:
        st = inspect(dev)
    o.step("3/7 Backups (only taken once; never overwritten)")
    dev.w(f"mkdir -p {dev.d(BOOTDIR)}")
    bak_dest = plan_vendor_backup(dev, st)
    had_backup = bool(vendor_backup(dev, st)[0] or st["bak_b_absent"])
    new_state = {}
    if bak_dest:
        if bak_dest != BAK_B and dev.md5(bak_dest) == st["b_md5"]:
            o.ok(f"slot-B vendor binary already backed up as {bak_dest}")
        else:
            dev.atomic_copy(B_EXE, bak_dest, st["b_md5"])
            o.ok(f"slot-B vendor binary -> {bak_dest} (md5 {st['b_md5']})")
        if had_backup:
            o.warn("slot B held a different vendor build than the existing backup (a vendor update?) — "
                   "backed it up too; revert restores this newer build and its boot flag")
        new_state = {"orig_flag": 1 if st["flag"] else 0, "orig_b": "vendor",
                     "bak_b": bak_dest, "bak_b_md5": st["b_md5"]}
    elif had_backup:
        o.ok("slot-B backup already exists — kept as is")
    elif not st["b_exists"]:
        dev.w(f"touch {dev.d(BAK_B_ABSENT)} && sync")
        o.ok("slot B was empty — recorded (revert will leave it empty again)")
    else:
        o.warn("slot B already holds a Meraki+ build and there is no vendor backup "
               "(older manual install). Revert will then leave slot B empty and boot stock A.")
    if st["bak_sh"]:
        o.ok("demo.sh backup already exists — kept as is")
    elif st["guard_lines"]:
        o.warn("demo.sh already contains a Meraki+ guard line but no backup exists; revert will strip the line")
    else:
        dev.atomic_copy(DEMO_SH, BAK_SH, dev.md5(DEMO_SH), mode="644")
        o.ok(f"/customer/demo.sh -> {BAK_SH}")
    state = dict(st["state"])
    if not state:
        state = {"orig_flag": 1 if st["flag"] else 0,
                 "orig_b": "absent" if not st["b_exists"] else ("ours" if st["b_ours"] else "vendor"),
                 "first_install": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")}
    state.update(new_state)
    if state != st["state"]:
        write_state(dev, state)
        o.ok("original boot state recorded (used by revert)")

    o.step(f"4/7 Installing Meraki+ {VERSION} into slot B")
    if dev.md5(STAGE) == want:
        o.ok(f"staged copy {STAGE} already up to date")
    else:
        dev.push(merakiplus, STAGE)
        o.ok(f"uploaded and verified {STAGE}")
    install_sound(dev, o, snd, sounds)
    if st["b_md5"] == want:
        o.ok(f"{B_EXE} already is Meraki+ {VERSION}")
    else:
        dev.atomic_copy(STAGE, B_EXE, want)
        o.ok(f"{B_EXE} replaced atomically (md5 {want})")

    o.step("5/7 Optional parts")
    if args.gesture:
        for name, dest in (("switcher", SWITCHER), ("grab_shim.so", SHIM)):
            if dev.md5(dest) == BIN_MD5[name]:
                o.ok(f"{dest} up to date")
            else:
                dev.push(extra[name], dest, mode="755")
                o.ok(f"{dest} uploaded and verified")
        install_guard(dev, o)
    else:
        o.say("  --gesture not given: /customer/demo.sh is left untouched")
    if args.persist and args.no_activate:
        o.warn("--persist with --no-activate: the boot flag is set although Meraki+ is not started and "
               "verified now. If it does not come up at the next boot, run ./revert.sh")
        set_boot_flag(dev, o, st)
    elif args.persist:
        o.say("  --persist: the boot flag is set once Meraki+ has been verified running (step 7)")
    elif st["flag"]:
        o.warn("the boot flag was ALREADY set before this install, so the machine boots slot B = Meraki+")
    else:
        o.say("  --persist not given: after a power cycle the machine starts the stock UI again")

    o.step("6/7 Activating Meraki+ now (no reboot)")
    if args.no_activate:
        o.say("  --no-activate given: skipping")
        pid = None
    else:
        pid = activate_meraki(dev, o, inspect(dev) if not dev.dry else st, want)

    o.step("7/7 Verifying")
    if pid and not dev.dry:
        verify_running(dev, o, pid, want)
    if args.persist and not args.no_activate:
        set_boot_flag(dev, o, st)
    if not dev.dry:
        if dev.md5(B_EXE) != want:
            raise Abort("slot B binary md5 changed unexpectedly")
        if dev.md5(A_EXE) != st["a_md5"]:
            raise Abort("slot A binary changed — this must never happen; please open an issue")
        o.ok("slot A untouched, slot B verified")
    o.say("")
    o.say(f"Done. Meraki+ {VERSION} is {'installed (dry-run: nothing was changed)' if dev.dry else 'running'}.")
    o.say("  Check on the screen: the gear on HOME -> SYSTEM shows 'MERAKI+ V" + VERSION + "' under its title.")
    o.say(f"  Undo everything:     ./revert.sh --ip {args.ip}")


def set_boot_flag(dev, o, st):
    """--persist: boot slot B (Meraki+) at every power-on."""
    if st["flag"]:
        o.ok("boot flag already set: Meraki+ starts at every boot")
    else:
        dev.w(f"touch {dev.d(FLAG)} && sync")
        o.ok("boot flag set: Meraki+ starts at every boot (undo: ./revert.sh, or BOOT-UI in Meraki+ settings)")


def install_sound(dev, o, snd, sounds):
    """v6.18: the sound helper + the three WAVs (before the UI is activated, so it finds them). md5-gated, idempotent."""
    if dev.md5(SND_HELPER) == BIN_MD5["merakiplus_snd"]:
        o.ok(f"{SND_HELPER} up to date")
    else:
        dev.push(snd, SND_HELPER, mode="755")
        o.ok(f"{SND_HELPER} uploaded and verified (sound player; uses the display's own libmi_ao.so)")
    dev.w(f"mkdir -p {dev.d(AUDIO_DIR)}")
    for name, data in sounds.items():
        dest = AUDIO_DIR + "/" + name
        if dev.md5(dest) == AUDIO_MD5[name]:
            o.ok(f"{dest} up to date")
        else:
            dev.push(data, dest, mode="644")
            o.ok(f"{dest} uploaded and verified")


def install_guard(dev, o):
    raw = dev.read(DEMO_SH)
    content = raw.decode("utf-8", "surrogateescape")
    lines = content.splitlines(keepends=True)
    ours = [l for l in lines if GUARD_KEY in l or LEGACY_GUARD_KEY in l]
    if len(ours) == 1 and ours[0].rstrip("\n") == GUARD_LINE:
        o.ok("demo.sh guard line already present")
        return
    base = [l for l in lines if GUARD_KEY not in l and LEGACY_GUARD_KEY not in l]
    anchors = [i for i, l in enumerate(base) if l.rstrip("\n") == ANCHOR]
    insm = [i for i, l in enumerate(base) if l.strip() == INSMOD_MARKER]
    if not anchors or not insm or anchors[-1] < insm[-1] or \
            "starting from B" not in "".join(base[anchors[-1]:anchors[-1] + 4]):
        raise Abort("demo.sh does not have the expected layout (slot branch after '# kernel_mod_list'). "
                    "Your firmware differs from the one Meraki+ was developed on — skipping --gesture "
                    "is safe; demo.sh was NOT changed.")
    base.insert(anchors[-1], GUARD_LINE + "\n")
    new = "".join(base).encode("utf-8", "surrogateescape")
    check_demo_sh_edit(raw, new)
    dev.push(new, DEMO_SH, mode="755")
    if not dev.dry:
        back = dev.read(DEMO_SH)
        if back != new:
            raise Abort("demo.sh read-back differs from what was written — run ./revert.sh now")
    o.ok("demo.sh: one guard line added (starts the gesture switcher at boot)")
    o.say("  note: the gesture becomes active from the next power-on (the switcher and the touch-grab")
    o.say("        shim for the stock UI are started by demo.sh at boot)")


# ============================================================ revert
def cmd_revert(dev, o, args):
    o.step("1/6 Checking the display (read-only)")
    st = inspect(dev)
    print_status(o, st)
    require_device(st)
    st = resume_stopped(dev, o, st)
    # a UI running from slot B is only Meraki+ when slot B holds Meraki+ (b_ours): after a vendor
    # OTA the stock UI itself runs from B with the boot flag set, and that must be left alone
    nothing = not st["b_ours"] and not st["guard_lines"] and not st["switcher_pids"] and not st["state"]
    if nothing and not args.purge:
        clean_leftovers(dev, o)
        o.say("\nMeraki+ is not installed here — nothing to revert.")
        return
    if not dev.dry and not confirm("  Revert this display to the stock UI now?", args.yes):
        raise Abort("Cancelled by user. Nothing was changed.")
    if legacy_migrate(dev, o, st, rewrite_guard=False) and not dev.dry:
        st = dict(inspect(dev), a_md5=st["a_md5"])

    def at(path):         # --dry-run: a pending migration has not moved the file yet
        return legacy_path(path) if dev.dry and st["legacy"]["dir"] and not dev.exists(path) else path

    o.step("2/6 Switching the running UI back to stock (no reboot)")
    running_ours = [p for p, e in st["procs"] if e == B_EXE and st["b_ours"]]
    if running_ours:
        if dev.dry:
            o.say("  [dry-run] would stop Meraki+ and start the stock UI from slot A (stop-first)")
        else:
            pid = stop_first_switch(dev, o, A_DIR, A_EXE, A_LDP, "a_relaunch.log")
            o.ok(f"stock UI running again (pid {pid}, {A_EXE})")
    else:
        o.ok("Meraki+ is not the running UI")

    o.step("3/6 Restoring /customer/demo.sh")
    if st["guard_lines"]:
        cur = dev.read(DEMO_SH)
        stripped = strip_guard(cur)
        check_demo_sh_edit(cur, stripped)
        bak_md5 = dev.md5(at(BAK_SH)) if st["bak_sh"] else None
        if bak_md5 and bak_md5 == md5b(stripped):
            dev.atomic_copy(BAK_SH, DEMO_SH, bak_md5)
            o.ok(f"demo.sh restored from backup (md5 {bak_md5})")
        else:
            if bak_md5:
                o.warn("demo.sh changed since the backup (e.g. a vendor update) — removing only our line")
            dev.push(stripped, DEMO_SH, mode="755")
            o.ok("guard line removed from demo.sh")
    else:
        o.ok("demo.sh has no Meraki+ line")

    o.step("4/6 Removing the gesture switcher")
    if st["switcher_pids"]:
        dev.w("kill " + " ".join(map(str, st["switcher_pids"])) + " 2>/dev/null; true")
    dev.w(f"rm -f {dev.d(SWITCHER)} {dev.d(SHIM)} {dev.d(BOOTDIR + '/tinj_hold')} {dev.d(BOOTDIR + '/fbpage')}; sync")
    o.ok("switcher stopped and removed (if it was there)")

    o.step("5/6 Restoring slot B and the boot flag")
    restored_vendor = False
    bak_path, bak_md5 = vendor_backup(dev, st)
    foreign_b = st["b_exists"] and not st["b_ours"] and st["b_md5"] != bak_md5
    if st["b_ours"] and bak_path:
        have = dev.md5(bak_path)
        if have != bak_md5:
            raise Abort(f"The slot-B vendor backup {bak_path} has md5 {have}, expected {bak_md5} (damaged?). "
                        f"Slot B was left as Meraki+; the stock UI in slot A is unaffected.")
        dev.atomic_copy(bak_path, B_EXE, bak_md5)
        o.ok(f"slot B vendor binary restored from {bak_path} (md5 {bak_md5})")
        restored_vendor = True
    elif st["b_ours"]:
        dev.w(f"rm -f {dev.d(B_EXE)} && sync")
        o.ok("Meraki+ removed from slot B (slot B was empty / had no vendor backup)")
    elif foreign_b:
        o.ok("slot B holds a vendor build that is not Meraki+ (e.g. from a vendor update) — left as it is")
    elif st["b_exists"]:
        o.ok("slot B already holds the vendor binary")
        restored_vendor = True
    orig_flag = st["state"].get("orig_flag") == "1"
    if foreign_b or not (st["state"] or st["b_ours"]):
        # the flag belongs to the vendor build in slot B, or Meraki+ never recorded it: not ours to change
        o.ok("boot flag left as it is (" + ("set" if st["flag"] else "not set") + ")")
    elif orig_flag and restored_vendor:
        if not st["flag"]:
            dev.w(f"touch {dev.d(FLAG)} && sync")
        o.ok("boot flag kept as it was before Meraki+ (vendor slot B)")
    elif st["flag"]:
        dev.w(f"rm -f {dev.d(FLAG)} && sync")
        o.ok("boot flag removed: next boot starts the stock UI (slot A)")
    else:
        o.ok("boot flag not set: next boot starts the stock UI (slot A)")
    clean_leftovers(dev, o)
    if args.purge:
        dev.w(f"rm -rf {dev.d(OURDIR)} {dev.d(LEGACY_DIR)} && sync")
        o.ok(f"{OURDIR} removed (backups, language file and settings backups included)")
    else:
        dev.w(f"rm -f {dev.d(STAGE)} {dev.d(STATE)} {dev.d(SND_HELPER)}; rm -rf {dev.d(AUDIO_DIR)}; sync")
        o.say(f"  backups kept in {BOOTDIR} (remove with --purge); the sound player and sounds were removed")

    o.step("6/6 Verifying")
    if dev.dry:
        o.say("\nDry-run finished: nothing was changed.")
        return
    end = inspect(dev)
    print_status(o, end)
    if end["a_md5"] != st["a_md5"]:
        raise Abort("slot A changed — this must never happen; please open an issue")
    if end["guard_lines"] or end["switcher_pids"]:
        raise Abort("demo.sh / switcher not fully removed — see the status above")
    if any(e == B_EXE for _, e in end["procs"]) and end["b_ours"]:
        raise Abort("Meraki+ is still running")
    o.say("\nDone. The display runs the stock UI; Meraki+ is removed.")


# ============================================================ scan / status
def local_ipv4():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))            # no packet is sent; just picks the outgoing interface
        return s.getsockname()[0]
    finally:
        s.close()


def cmd_scan(o, args):
    net = ipaddress.ip_network(args.subnet, strict=False) if args.subnet else \
        ipaddress.ip_network(local_ipv4() + "/24", strict=False)
    o.say(f"Scanning {net} for an open telnet port (23) …")

    def probe(ip):
        try:
            socket.create_connection((str(ip), 23), timeout=0.6).close()
            return str(ip)
        except OSError:
            return None
    with ThreadPoolExecutor(64) as ex:
        hits = [h for h in ex.map(probe, net.hosts()) if h]
    found = []
    for ip in hits:
        dev = Device(ip, 23, Out())
        try:
            dev.connect()
            st_fp = dev.sh(f"[ -f {A_EXE} ] && [ -f {DEMO_SH} ] && [ -d {B_DIR} ] && echo FP_OK", timeout=8,
                           check=False)[1]
            if "FP_OK" in st_fp:
                found.append(ip)
                o.say(f"  {ip}: MerakiTech/Sanbon display")
            else:
                o.say(f"  {ip}: telnet, but not a MerakiTech display")
        except Abort:
            o.say(f"  {ip}: telnet, no usable shell")
        finally:
            dev.close()
    if not found:
        raise Abort("No display found. Is the machine switched on and on the same Wi-Fi/LAN?")
    return found


def resolve_ip(o, args):
    if args.ip:
        return args.ip
    if args.scan:
        found = cmd_scan(o, args)
        if len(found) > 1:
            raise Abort("More than one display found — pass --ip explicitly.")
        o.say(f"Using {found[0]}")
        return found[0]
    raise Abort("Give the display's address with --ip ADDR (or use --scan).")


def main(argv=None):
    ap = argparse.ArgumentParser(prog="merakiplus", description="Meraki+ installer for MerakiTech Gen1/Gen2 displays")
    sub = ap.add_subparsers(dest="cmd", required=True)

    def common(p):
        p.add_argument("--ip", help="display IP address")
        p.add_argument("--scan", action="store_true", help="find the display on the local /24 network")
        p.add_argument("--subnet", help="network to scan instead of the local /24, e.g. 192.168.0.0/24")
        p.add_argument("--port", type=int, default=23, help=argparse.SUPPRESS)
        p.add_argument("--dry-run", action="store_true", help="show what would happen; change nothing")
        p.add_argument("--yes", "-y", action="store_true", help="do not ask for confirmation")
        p.add_argument("--verbose", "-v", action="store_true", help="print every device command")
        p.add_argument("--log", help="log file (default: logs/merakiplus-<time>.log)")
        p.add_argument("--test-prefix", help=argparse.SUPPRESS)      # mock device (tests/)
    p = sub.add_parser("install", help="install / update Meraki+ in slot B and start it")
    common(p)
    p.add_argument("--slot", default="B", choices=["B"], help="target slot (only B is supported)")
    p.add_argument("--persist", action="store_true", help="also set the boot flag: start Meraki+ at every boot")
    p.add_argument("--gesture", action="store_true",
                   help="also install the center-hold gesture switcher (adds ONE line to /customer/demo.sh)")
    p.add_argument("--no-activate", action="store_true", help="install only; do not start Meraki+ now")
    p.add_argument("--ref", default=os.environ.get("MERAKIPLUS_REF", "main"), help="git ref to download bin/ from")
    p.add_argument("--bin-dir", help="use binaries from this directory (md5-checked)")
    p = sub.add_parser("revert", help="restore the stock UI and remove Meraki+")
    common(p)
    p.add_argument("--purge", action="store_true", help="also delete /new_customer/merakiplus (backups, language, settings backups)")
    p = sub.add_parser("status", help="read-only overview")
    common(p)
    p = sub.add_parser("scan", help="find displays on the local network")
    p.add_argument("--subnet")
    args = ap.parse_args(argv)

    logfile = None
    if args.cmd in ("install", "revert"):
        logfile = args.log or str(default_log_dir() / datetime.datetime.now().strftime("merakiplus-%Y%m%d-%H%M%S.log"))
    try:
        if logfile:
            pathlib.Path(logfile).parent.mkdir(parents=True, exist_ok=True)
        o = Out(logfile, getattr(args, "verbose", False))
    except OSError as e:
        print(f"  [!]  cannot write the log file {logfile} ({e}); continuing without a log", flush=True)
        logfile = None
        o = Out(None, getattr(args, "verbose", False))
    dev = None
    try:
        if args.cmd == "scan":
            cmd_scan(o, args)
            return 0
        ip = resolve_ip(o, args)
        args.ip = ip
        dev = Device(ip, args.port, o, dry=args.dry_run, prefix=args.test_prefix or "")
        dev.connect()
        if args.cmd == "status":
            print_status(o, inspect(dev))
        elif args.cmd == "install":
            if args.dry_run:
                o.say("DRY-RUN: reads only, nothing on the display will be changed.")
            cmd_install(dev, o, args)
        elif args.cmd == "revert":
            if args.dry_run:
                o.say("DRY-RUN: reads only, nothing on the display will be changed.")
            cmd_revert(dev, o, args)
        if logfile:
            o.say(f"Log: {logfile}")
        return 0
    except Abort as e:
        o.say(f"\nSTOPPED: {e}")
        if dev is not None and dev.changed:
            o.say(f"Slot A (the stock UI) is never modified. To get back to a clean stock state: ./revert.sh --ip {args.ip}")
        elif dev is not None:
            o.say("Nothing on the display was changed.")
        if logfile:
            o.say(f"Log: {logfile}")
        return 1
    except KeyboardInterrupt:
        o.say("\nInterrupted. Files on the display are only replaced atomically (temp file + rename) and a paused "
              "UI is resumed, but the run may be incomplete. Run the same command again to finish it, or "
              "./revert.sh to go back to the stock UI.")
        return 130
    finally:
        if dev:
            dev.close()


if __name__ == "__main__":
    sys.exit(main())
