#!/usr/bin/env python3
"""install_v614_B.py — install ourui v6.14 (physical on/off button) in slot B and make it LIVE
without a reboot, without config/flag/vendor-OTA writes.

Reuses boot_install.py's DeviceBackend (telnet_run + push_nc, md5 gates, fb capture)
and the pattern from install_v613_B.py (same-fs atomic mv against ETXTBSY + watchdog exec-reload).
Phases (run separately so the reload can be inspected before/after):
  baseline  read-only starting point + gates + before-fb
  install   backup v6.13 -> demo_v9.v613.bak + ourui_v6_13.bin; stage v6.14;
            atomic same-fs install as /new_customer/demo_v9 + run.sh target; invariant gates
  reload    watchdog exec-reload (kill B -> test.sh new_customer starts the NEW v6.14);
            fallback manual B launch; gate md5(/proc/<pid>/exe) == v6.14
  verify    fb home + DIAGNOSE (VERSION V6.14) + back home; end state; /tmp clean
  status    read-only overview

Policy: no reboot; no writes to config.json, /customer/new, /customer/demo.sh,
/customer/demo_v9, vendor OTA. All writes limited to /new_customer (ourui's slot)
+ /tmp (tmpfs, cleaned up). Guard line/switcher untouched. v6.11 backups must stay intact.
"""
import json
import pathlib
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent          # boot/ondevice
BOOT = HERE.parent
sys.path.insert(0, str(BOOT))
import boot_install as bi

OUT = HERE / "install_v614_B"
OUT.mkdir(exist_ok=True)

V611 = "725386ccadade594275f20cc998885eb"
V613 = "163692b5b3e11d8294471c5a0eae578b"
V614 = "1dd63f42e4b0d649ef851660fc123a10"
CONFIG_BASE = "<config-md5-redacted>"   # the user's timemode-SAVE baseline (UNTOUCHED)
DEMO_SH = "b8e3a47a7cc3f222066154ce82f85ee9"        # v6.11 legacy guard (UNTOUCHED)
SWITCHER_V612 = "e6bcb460ddeec8504e45d664641cc4b3"
VENDOR_A = "d44c2414b2ac6f0c9370ed6c9f549903"
B_EXE = bi.B_EXE                                     # /new_customer/demo_v9
BOOTDIR = bi.BOOTDIR                                 # /new_customer/ourui/boot
OURUI_DIR = "/new_customer/ourui"
STAGE = OURUI_DIR + "/ourui_v6_14.bin"               # v6.14 artifact on device (same-fs)
BAK_B = BOOTDIR + "/demo_v9.v613.bak"                # v6.13 backup of B (revert)
BAK_RUN = OURUI_DIR + "/ourui_v6_13.bin"             # v6.13 backup of the run.sh target
BAK_B_V611 = BOOTDIR + "/demo_v9.v611.bak"           # earlier rounds (must be intact)
BAK_RUN_V611 = OURUI_DIR + "/ourui_v6_11.bin"


def log(msg):
    bi.log(f"[v614B] {msg}")


def save(name, obj):
    (OUT / name).write_text(json.dumps(obj, indent=1, ensure_ascii=False))


def demo_exe_md5(be, pid):
    """md5 of the binary the process has actually mapped (via /proc/<pid>/exe).
    Also works when the exe link has a '(deleted)' suffix (old inode after an atomic mv)."""
    return be.md5(f"/proc/{pid}/exe")


def demo_state(be):
    """demo_state with normalized exe path: strip the ' (deleted)' suffix."""
    return [(p, e.replace(" (deleted)", "")) for p, e in be.demo_state()]


def require_idle_b(be):
    st = demo_state(be)
    bi.gate(len(st) == 1 and st[0][1] == B_EXE, f"exactly one B UI running: {st}")
    pid = st[0][0]
    # ourui's 100 ms poll loop sleeps in hrtimer_nanosleep; on this kernel it is
    # sometimes sampled as D (uninterruptible) even though it is healthy. D is accepted ONLY if
    # wchan is nanosleep (a genuinely hung process would sit in an ubifs/wait wchan).
    state, wchan = "", ""
    for _ in range(4):
        out = be.telnet(f"awk '{{print $3}}' /proc/{pid}/stat; cat /proc/{pid}/wchan 2>/dev/null; echo")
        lines = [l.strip() for l in out.splitlines() if l.strip()]
        state = lines[0] if lines else ""
        wchan = lines[1] if len(lines) > 1 else ""
        if state in ("S", "R"):
            break
        time.sleep(0.7)
    ok = state in ("S", "R") or (state == "D" and "nanosleep" in wchan)
    bi.gate(ok, f"B is idle/healthy (state {state}, wchan {wchan or '-'}; not T/Z/hung): pid {pid}")
    return pid, state


def phase_baseline(be):
    log("=== PHASE baseline (read-only) ===")
    pid, state = require_idle_b(be)
    sw = be.switcher_pids()
    bi.gate(len(sw) == 1, f"switcher running (one PID): {sw}")
    bi.gate(be.exists(bi.FLAG), "flag /customer/new = JA/present (B on next boot)")
    cfg = be.md5("/customer/config.json")
    bi.gate(cfg == CONFIG_BASE, f"config == user baseline {CONFIG_BASE[:8]}… (must not be touched)")
    b_md5 = be.md5(B_EXE)
    bi.gate(b_md5 == V613, f"B runs v6.13 ({V613[:8]}…)")
    run_md5 = be.md5(OURUI_DIR + "/ourui")
    bi.gate(run_md5 == V613, f"run.sh target == v6.13 ({V613[:8]}…)")
    live = demo_exe_md5(be, pid)
    bi.gate(live == V613, f"md5(/proc/{pid}/exe) == v6.13 (live-binary technique works)")
    bi.gate(be.md5("/customer/demo.sh") == DEMO_SH, "demo.sh == v6.11 legacy guard (untouched)")
    bi.gate(be.md5("/customer/demo_v9") == VENDOR_A, "vendor A untouched")
    bi.gate(be.md5(BOOTDIR + "/switcher") == SWITCHER_V612, "switcher == v6.12 (untouched)")
    wd = be.telnet("ps -A | grep 'test.sh' | grep -v grep")
    bi.gate("new_customer" in wd, f"watchdog = test.sh new_customer (B-slot): {wd.strip()}")
    # earlier backups intact?
    bak11b = be.md5(BAK_B_V611)
    bak11r = be.md5(BAK_RUN_V611)
    bi.gate(bak11b == V611 and bak11r == V611, f"v6.11 backups intact ({bak11b}, {bak11r})")
    up = be.telnet("cut -d' ' -f1 /proc/uptime")
    tmp = be.telnet("ls /tmp/")
    be.capture("v614B_foer_home")
    save("baseline.json", {"pid": pid, "state": state, "switcher": sw, "flag": "JA",
                           "config_md5": cfg, "b_md5": b_md5, "run_md5": run_md5,
                           "live_exe_md5": live, "watchdog": wd.strip(),
                           "bak_v611_b": bak11b, "bak_v611_run": bak11r,
                           "uptime": up, "tmp": tmp})
    log(f"baseline OK: B pid {pid} v6.13, switcher {sw}, flag JA (present), config {cfg[:8]}…")


def phase_install(be):
    log("=== PHASE install (backup + stage + atomic install) ===")
    pid, _ = require_idle_b(be)
    bi.gate(be.md5(B_EXE) == V613, "B == v6.13 before backup")
    bi.gate(be.md5("/customer/config.json") == CONFIG_BASE, "config at user baseline")
    # local source verification
    src = bi.OURUI / "ourui"
    bi.elf_arm_static(src)
    import hashlib
    src_md5 = hashlib.md5(src.read_bytes()).hexdigest()
    bi.gate(src_md5 == V614, f"local ourui_v6 == v6.14 ({V614}) — md5 read at run time")
    # 1. backup of the current B (v6.13) — same-fs cp -p + md5 gate
    be.cp(B_EXE, BAK_B)
    bi.gate(be.md5(BAK_B) == V613, f"backup demo_v9.v613.bak == v6.13 ({V613[:8]}…)")
    # run.sh target backup: ourui_v6_13.bin may already exist (staged in the v6.13 round)
    if be.md5(BAK_RUN) == V613:
        log(f"{BAK_RUN} already exists with the v6.13 md5 — kept as backup")
    else:
        be.cp(OURUI_DIR + "/ourui", BAK_RUN)
    bi.gate(be.md5(BAK_RUN) == V613, f"backup ourui_v6_13.bin == v6.13 ({V613[:8]}…)")
    # 2. stage v6.14 on /new_customer (same fs as dest) — push_nc md5 round-trip
    be.push(src, STAGE)
    bi.gate(be.md5(STAGE) == V614, f"stage ourui_v6_14.bin == v6.14 (round-trip)")
    # 3. atomic same-fs install as B's demo_v9 (mv over a running exe = safe; the old inode is kept)
    be.install_verified(STAGE, B_EXE, V614, 0o755)
    bi.gate(be.md5(B_EXE) == V614, f"/new_customer/demo_v9 == v6.14 ({V614[:8]}…)")
    # 4. run.sh target (same pattern; not used at B boot but kept consistent)
    be.install_verified(STAGE, OURUI_DIR + "/ourui", V614, 0o755)
    bi.gate(be.md5(OURUI_DIR + "/ourui") == V614, "run.sh target ourui == v6.14")
    # 5. invariants: guard/switcher/demo.sh/config/flag/vendor must be untouched
    bi.gate(be.md5("/customer/demo.sh") == DEMO_SH, "demo.sh UNTOUCHED (guard line preserved)")
    bi.gate(be.md5(BOOTDIR + "/switcher") == SWITCHER_V612, "switcher UNTOUCHED (v6.12)")
    bi.gate(be.md5("/customer/config.json") == CONFIG_BASE, "config UNTOUCHED")
    bi.gate(be.exists(bi.FLAG), "flag JA (present) UNTOUCHED")
    bi.gate(be.md5("/customer/demo_v9") == VENDOR_A, "vendor A UNTOUCHED")
    bi.gate(be.md5(BAK_B_V611) == V611 and be.md5(BAK_RUN_V611) == V611, "v6.11 backups still intact")
    # B still runs the OLD v6.13 (old inode) until reload — proof that install did not interfere
    live = demo_exe_md5(be, pid)
    bi.gate(live == V613, f"running B is still v6.13 (old inode, md5 {live[:8]}…) — reload pending")
    save("install.json", {"b_md5": be.md5(B_EXE), "run_md5": be.md5(OURUI_DIR + "/ourui"),
                          "bak_b": be.md5(BAK_B), "bak_run": be.md5(BAK_RUN),
                          "stage": be.md5(STAGE), "live_before_reload": live,
                          "demo_sh": be.md5("/customer/demo.sh"), "config": be.md5("/customer/config.json")})
    log("install OK: B file + run.sh target = v6.14; backups v6.13 (+v6.11) intact; invariants untouched; reload pending")


def phase_reload(be):
    log("=== PHASE reload (watchdog exec-reload, NO reboot) ===")
    pid, state = require_idle_b(be)
    bi.gate(be.md5(B_EXE) == V614, "the B file is v6.14 BEFORE reload (install has run)")
    wd = be.telnet("ps -A | grep 'test.sh' | grep -v grep")
    bi.gate("new_customer" in wd, "watchdog test.sh new_customer alive (restarts B)")
    up0 = be.telnet("cut -d' ' -f1 /proc/uptime")
    log(f"watchdog exec-reload: kill -TERM {pid} (v6.13) -> watchdog starts /new_customer/demo_v9 (v6.14)")
    log("Rationale: does NOT start stock A (OTA not blocked; no OTA-check/flag risk); no reboot.")
    be.telnet(f"kill -TERM {pid}; echo TERM_SENDT", timeout=15)
    # poll for a new B PID (watchdog ~1–3 s); fallback manual launch if needed
    newpid = None
    t0 = time.time()
    while time.time() - t0 < 20:
        time.sleep(1.5)
        st = demo_state(be)
        for p, e in st:
            if e == B_EXE and p != pid:
                newpid = p
                break
        if newpid:
            break
    if newpid is None:
        log("watchdog restart not seen within 20 s -> manual B launch (fallback, still no reboot/stock)")
        be.telnet("cd /new_customer; export LD_LIBRARY_PATH=/new_customer/libs:/config/wifi; "
                  "nohup ./demo_v9 >>/tmp/ourui_boot.log 2>&1 & sleep 2; echo NY=$(pidof demo_v9)", timeout=30)
        st = demo_state(be)
        for p, e in st:
            if e == B_EXE and p != pid:
                newpid = p
                break
    bi.gate(newpid is not None, "new B PID came up (watchdog or fallback)")
    st = demo_state(be)
    bi.gate(len(st) == 1 and st[0][0] == newpid and st[0][1] == B_EXE,
            f"exactly one B UI (new pid {newpid}, old {pid} gone): {st}")
    # THE CORE CHECK: the LIVE process maps the v6.14 binary
    live = demo_exe_md5(be, newpid)
    bi.gate(live == V614, f"md5(/proc/{newpid}/exe) == v6.14 ({V614}) — LIVE binary is v6.14")
    nstate, nwchan = "", ""
    for _ in range(4):
        out = be.telnet(f"awk '{{print $3}}' /proc/{newpid}/stat; cat /proc/{newpid}/wchan 2>/dev/null; echo")
        lines = [l.strip() for l in out.splitlines() if l.strip()]
        nstate = lines[0] if lines else ""
        nwchan = lines[1] if len(lines) > 1 else ""
        if nstate in ("S", "R"):
            break
        time.sleep(0.7)
    bi.gate(nstate in ("S", "R") or (nstate == "D" and "nanosleep" in nwchan),
            f"new B healthy (state {nstate}, wchan {nwchan or '-'})")
    bi.gate(be.md5("/customer/config.json") == CONFIG_BASE, "config UNTOUCHED after reload")
    bi.gate(be.exists(bi.FLAG), "flag JA (present) UNTOUCHED after reload")
    bi.gate(be.md5("/customer/demo.sh") == DEMO_SH, "demo.sh UNTOUCHED after reload")
    up1 = be.telnet("cut -d' ' -f1 /proc/uptime")
    bi.gate(float(up1) > float(up0), f"no reboot (uptime {up0} -> {up1})")
    save("reload.json", {"old_pid": pid, "new_pid": newpid, "live_exe_md5": live,
                         "new_state": nstate, "uptime_before": up0, "uptime_after": up1,
                         "method": "watchdog exec-reload (no reboot, no stock A)"})
    log(f"reload OK: B pid {pid} (v6.13) -> pid {newpid} (v6.14 LIVE), state {nstate}, no reboot")


def phase_verify(be):
    log("=== PHASE verify (fb V6.14 + end state) ===")
    st = demo_state(be)
    bi.gate(len(st) == 1 and st[0][1] == B_EXE, f"B running: {st}")
    pid = st[0][0]
    bi.gate(demo_exe_md5(be, pid) == V614, "live binary == v6.14")
    # let the boot splash (1.5 s) finish so we land on HOME before capture/navigation
    time.sleep(2.5)
    # fb: home, then DIAGNOSE (the VERSION row) via one left-edge tap (wrap HOME->DIAG)
    be.push(bi.OURUI / "tinj", "/tmp/tinj")
    be.capture("v614B_efter_home")
    be.telnet("/tmp/tinj tap 20 240", timeout=15)   # HOME -> DIAGNOSE (T_EDGE_L, wrap)
    time.sleep(1.2)
    be.capture("v614B_diag_version")
    be.telnet("/tmp/tinj tap 460 240", timeout=15)  # DIAGNOSE -> HOME (T_EDGE_R)
    time.sleep(1.0)
    be.capture("v614B_slut_home")
    # end invariants
    end = {
        "demo": demo_state(be),
        "live_exe_md5": demo_exe_md5(be, pid),
        "switcher": be.switcher_pids(),
        "flag": "JA" if be.exists(bi.FLAG) else "NEJ",
        "config_md5": be.md5("/customer/config.json"),
        "demo_sh_md5": be.md5("/customer/demo.sh"),
        "b_md5": be.md5(B_EXE),
        "run_md5": be.md5(OURUI_DIR + "/ourui"),
        "bak_b_md5": be.md5(BAK_B),
        "bak_run_md5": be.md5(BAK_RUN),
        "bak_v611_b_md5": be.md5(BAK_B_V611),
        "bak_v611_run_md5": be.md5(BAK_RUN_V611),
        "switcher_md5": be.md5(BOOTDIR + "/switcher"),
        "vendor_a_md5": be.md5("/customer/demo_v9"),
        "uptime": be.telnet("cut -d' ' -f1 /proc/uptime"),
        "tmp": be.telnet("ls /tmp/"),
    }
    bi.gate(end["config_md5"] == CONFIG_BASE, "config UNTOUCHED (end)")
    bi.gate(end["flag"] == "JA", "flag JA (present) UNTOUCHED (end)")
    bi.gate(end["demo_sh_md5"] == DEMO_SH, "demo.sh UNTOUCHED (end)")
    bi.gate(end["switcher_md5"] == SWITCHER_V612, "switcher v6.12 UNTOUCHED (end)")
    bi.gate(end["bak_b_md5"] == V613 and end["bak_run_md5"] == V613, "v6.13 backups intact (end)")
    bi.gate(end["bak_v611_b_md5"] == V611 and end["bak_v611_run_md5"] == V611, "v6.11 backups intact (end)")
    bi.gate(end["vendor_a_md5"] == VENDOR_A, "vendor A UNTOUCHED (end)")
    # remove our own /tmp files (tinj, fbpage, push tmp); keep the normal runtime logs
    be.telnet("rm -f /tmp/tinj /tmp/fbpage /tmp/.push_nc_tmp /tmp/nc.err; ls /tmp/")
    end["tmp_clean"] = be.telnet("ls /tmp/")
    save("end_state.json", end)
    log("verify OK: fb home + DIAGNOSE(V6.14) + end home; all invariants untouched; /tmp cleaned")
    log(f"END: B pid {pid} v6.14 LIVE, switcher {end['switcher']}, flag JA (present), config {end['config_md5'][:8]}…")


def phase_status(be):
    st = demo_state(be)
    log(f"status: demo={st} switcher={be.switcher_pids()} flag={'JA' if be.exists(bi.FLAG) else 'NEJ'}")
    for p in (B_EXE, OURUI_DIR + "/ourui", "/customer/config.json", "/customer/demo.sh",
              BAK_B, BAK_RUN, BAK_B_V611, BAK_RUN_V611, BOOTDIR + "/switcher"):
        log(f"  md5 {p}: {be.md5(p)}")
    for pid, e in st:
        log(f"  live /proc/{pid}/exe md5: {demo_exe_md5(be, pid)}")


PHASES = {"baseline": phase_baseline, "install": phase_install, "reload": phase_reload,
          "verify": phase_verify, "status": phase_status}


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in PHASES:
        print("usage: install_v614_B.py baseline|install|reload|verify|status")
        sys.exit(2)
    be = bi.DeviceBackend()
    PHASES[sys.argv[1]](be)


if __name__ == "__main__":
    main()
