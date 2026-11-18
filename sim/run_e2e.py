#!/usr/bin/env python3
"""run_e2e.py — E2E-runner for ourui's SIMULEREREDE testmiljø.

Orkestrering pr. scenarie (sim/scenarios/*.json):
  (a) sandbox-rod: config-kopi, flag-fil, /proc-mocks, fb-buffer, touch-FIFO
  (b) mcu_sim på pty (+ ctrl-socket + assertions-log)
  (c) ourui SIM-build hvis det findes (CONTRACT.md), ellers reference-klienten
      (sim/ref_client.py) som beviser sim'en
  (d) injicerer touch/scenarie-hændelser (evdev-FIFO / ctrl-sockets)
  (e) sampler UI-state (JSONL) + fb-frames (BGRA → PNG)
  (f) assertions: scenarie-forventninger + circle-tjek + farve/region-tjek
      + sim-log-assertions (keep-awake, trigger-politik, bånd, grænser)
  (g) rapport: JSON + MD + PNG-snapshots/contact-sheet pr. scenarie

Kun Python stdlib. Ingen enhedskontakt. Brug:
  python3 sim/run_e2e.py --list
  python3 sim/run_e2e.py --scenario brew
  python3 sim/run_e2e.py --all
  python3 sim/run_e2e.py --scenario brew --ui-bin /sti/til/ourui_v6_sim
  python3 sim/run_e2e.py --all --no-ui-profile   # råt — scenarierne asserter ourui's
                                                 # vogtede adfærd (v6.5-scenarios)
  python3 sim/run_e2e.py --all --ref-client      # reference-oracle med REF-profil
"""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import proto
import png
from proto import (W, H, FB_BYTES, REGN, s16, touch_stream, load_baseline_config,
                   load_spec, pack112, pack113)

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SCEN_DIR = HERE / "scenarios"
REPORTS = HERE / "reports"
SWITCHER_HOST = ROOT / "build" / "switcher_host"
# ./build.sh output first, then the prebuilt SIM binary for this platform (bin/ourui_sim-<os>-<arch>)
SIM_BUILD_CANDIDATES = [ROOT / "build" / "ourui_sim",
                        ROOT / "bin" / ("ourui_sim-%s-%s" % ({"darwin": "macos"}.get(sys.platform, sys.platform),
                                                              {"aarch64": "arm64", "amd64": "x86_64"}.get(
                                                                  os.uname().machine.lower(), os.uname().machine.lower())))]
# v6.4 (CONTRACT.md "TILLÆG v6.4"): UI-profil = overrides mellem reference-klientens
# syntetiske layout/adfærd og det rigtige ourui. Slås fra med --no-ui-profile.
# v6.5-scenarios (RECONCILE.md): scenarierne asserter nu DIREKTE den vogtede ourui-
# adfærd. UI-profilen er derfor TOM (mekanismen er bagudkompatibel), og reference-
# klienten får sin egen REF-profil med de omvendte overrides (oracle-adfærden).
# Den historiske v6.4-profil kan stadig gives eksplicit med --ui-profile.
UI_PROFILE_DEFAULT = HERE / "sim_ui_profile-ui.json"
REF_PROFILE_DEFAULT = HERE / "sim_ui_profile-ref.json"


def _pmatch(item, pat):
    """Alle nøgler i pat skal findes i item med samme værdi (tal: ±0.02)."""
    for k, v in pat.items():
        if k not in item:
            return False
        w = item[k]
        if isinstance(v, (int, float)) and not isinstance(v, bool) \
                and isinstance(w, (int, float)) and not isinstance(w, bool):
            if abs(v - w) > 0.02:
                return False
        elif v != w:
            return False
    return True


def apply_ui_profile(scen, profile):
    """Returnér (ny scenarie-dict, anvendte overrides, fejl). Scenarie-filen røres
    ikke. Hver override skal ramme PRÆCIS det antal forventninger/hændelser den
    angiver (default 1) — ellers meldes fejl, så en forældet profil ikke kan skjule
    noget. Forventninger erstattes 1:1 (antal checks er uændret)."""
    out = json.loads(json.dumps(scen))
    applied, errors = [], []
    for ov in (profile.get("scenarios", {}) or {}).get(scen.get("name"), []):
        why = ov.get("why", "")
        if "match_event" in ov:
            hits = [e for e in out.get("events", []) if _pmatch(e, ov["match_event"])]
            if len(hits) != ov.get("count", 1):
                errors.append(f"event-match {ov['match_event']} ramte {len(hits)}")
                continue
            for e in hits:
                e.update(ov.get("set", {}))
            applied.append({"type": "event", "match": ov["match_event"],
                            "set": ov.get("set", {}), "why": why})
        elif "match" in ov:
            exp = out.get("expect", [])
            idx = [i for i, e in enumerate(exp) if _pmatch(e, ov["match"])]
            repl = ov.get("replace")
            if len(idx) != ov.get("count", 1) or not isinstance(repl, dict):
                errors.append(f"expect-match {ov['match']} ramte {len(idx)}")
                continue
            for i in idx:
                new = dict(repl)
                for k in ov.get("keep", []):          # fx tidsvinduet fra originalen
                    if k in exp[i]:
                        new.setdefault(k, exp[i][k])
                new["_ui_profile"] = why
                exp[i] = new
            applied.append({"type": "expect", "match": ov["match"],
                            "replace": repl, "n": len(idx), "why": why})
    return out, applied, errors

# circle-tjek portet fra artifacts/ourui/check_circle.py (samme kriterier)
CX = CY = 240.0
R_PHYS, R_SAFE = 240.0, 234.0


def circle_check(raw):
    if len(raw) < FB_BYTES:
        return False, {"error": f"for kort: {len(raw)}"}
    viol = 0
    maxr = 0.0
    worst = None
    for y in range(H):
        dy = y - CY + 0.5
        base = y * W * 4
        for x in range(W):
            o = base + x * 4
            b, g, r = raw[o], raw[o + 1], raw[o + 2]
            if b == 0 and g == 0 and r == 0:
                continue
            dx = x - CX + 0.5
            rad = (dx * dx + dy * dy) ** 0.5
            if rad > maxr:
                maxr = rad
            if rad > R_PHYS:
                viol += 1
                if worst is None:
                    worst = [x, y, round(rad, 2), [r, g, b]]
    corners = []
    for (x, y) in [(0, 0), (479, 0), (0, 479), (479, 479)]:
        o = (y * W + x) * 4
        corners.append([raw[o + 2], raw[o + 1], raw[o]])
    corner_ok = all(c == [0, 0, 0] for c in corners)
    ok = viol == 0 and corner_ok and maxr <= R_SAFE + 0.71
    return ok, {"violations": viol, "max_radius": round(maxr, 2),
                "corners_black": corner_ok, "worst": worst}


class Ctrl:
    """JSONL-klient til sim/ui ctrl-unix-sockets."""
    def __init__(self, path):
        self.path = str(path)
        self.seq = 0

    def call(self, req, timeout=5.0):
        self.seq += 1
        req = dict(req, id=self.seq)
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(timeout)
        s.connect(self.path)
        s.sendall((json.dumps(req) + "\n").encode())
        f = s.makefile("r")
        line = f.readline()
        s.close()
        if not line:
            raise RuntimeError(f"ctrl uden svar: {req}")
        return json.loads(line)


def find_ui_bin(explicit=None):
    if explicit:
        p = Path(explicit)
        return p if p.exists() else None
    env = os.environ.get("OURUI_SIM_BIN")
    if env and Path(env).exists():
        return Path(env)
    for p in SIM_BUILD_CANDIDATES:
        if p.exists():
            return p
    return None


class ScenarioRun:
    def __init__(self, scen, run_dir, ui_bin=None, keep_sandbox=False,
                 ui_lang=None, ui_profile=None):
        self.ui_lang = ui_lang
        self.profile_applied, self.profile_errors = [], []
        if ui_profile:                      # v6.5-scenarios: også REF-profilen (reference-klient)
            scen, self.profile_applied, self.profile_errors = \
                apply_ui_profile(scen, ui_profile)
        self.scen = scen
        self.name = scen["name"]
        self.dir = Path(run_dir) / self.name
        self.dir.mkdir(parents=True, exist_ok=True)
        self.sandbox = self.dir / "sandbox"
        self.snaps_dir = self.dir / "snapshots"
        self.snaps_dir.mkdir(exist_ok=True)
        self.ui_bin = ui_bin
        self.keep_sandbox = keep_sandbox
        self.samples = []          # (t, state)
        self.cfg_samples = []      # (t, cfg-dict)
        self.flag_samples = []     # (t, bool)
        self.snapshots = []        # {t, label, bin, png, raw}
        self.checks = []
        self.gesture_results = {}
        self.replay_truth_cache = {}
        self.sim_proc = self.ui_proc = None
        self.sim_ctrl = self.ui_ctrl = None
        self.pty_path = None
        self.state_path = self.dir / "state.jsonl"
        self.fb_path = self.sandbox / "fb.bin"
        self.assert_path = self.dir / "sim_assertions.jsonl"
        self.sim_log = []
        self.ui_exit = None
        self.client_kind = "reference"
        self.t0 = None
        # AF_UNIX-stier må være ≤104 bytes (macOS) — scratch-stien er for lang,
        # så ctrl-sockets lægges i en kort /tmp-mappe pr. kørsel.
        import tempfile
        self.sock_dir = Path(tempfile.mkdtemp(prefix=f"ourui_e2e_{os.getpid()}_",
                                              dir="/tmp"))
        self.sim_ctrl_path = self.sock_dir / f"{self.name[:24]}_sim.ctrl"
        self.ui_ctrl_path = self.sock_dir / f"{self.name[:24]}_ui.ctrl"

    # ------------------------------------------------------------ (a) sandbox
    def make_sandbox(self):
        sb = self.sandbox
        (sb / "customer").mkdir(parents=True, exist_ok=True)
        (sb / "new_customer" / "ourui" / "boot").mkdir(parents=True, exist_ok=True)
        (sb / "tmp").mkdir(exist_ok=True)
        (sb / "proc").mkdir(exist_ok=True)
        (sb / "dev" / "input").mkdir(parents=True, exist_ok=True)
        (sb / "sys" / "class" / "pwm" / "pwmchip0" / "pwm2").mkdir(parents=True, exist_ok=True)
        # config-kopi + scenarie-overrides
        cfg = load_baseline_config()
        cfg.update(self.scen.get("config", {}))
        self.cfg_path = sb / "customer" / "config.json"
        self.cfg_path.write_text(json.dumps(cfg, indent="\t", ensure_ascii=False) + "\n")
        # boot-flag (scenarie kan starte med B-boot)
        self.flag_path = sb / "customer" / "new"
        if self.scen.get("boot_flag_initial"):
            self.flag_path.touch()
        # /proc-mocks (nok til ourui's slot-/exe-checks)
        (sb / "proc" / "uptime").write_text("12345.67 8901.23\n")
        (sb / "proc" / "meminfo").write_text("MemTotal: 128000 kB\nMemFree: 41000 kB\n")
        (sb / "proc" / "version").write_text("Linux version 4.9.84 (mock) #1 SMP\n")
        (sb / "sys" / "class" / "pwm" / "pwmchip0" / "pwm2" / "duty_cycle").write_text("37\n")
        # v6.3/v6.4: ourui's UI-sprog ligger i sin egen fil; i sandkassen (ingen fil = engelsk)
        self.lang_path = sb / "new_customer" / "ourui" / "lang"
        if self.ui_lang:
            self.lang_path.write_text(self.ui_lang)
        # fb-buffer + touch-FIFO oprettes af klient/sim; placeholder:
        self.fb_path.write_bytes(bytes(FB_BYTES))
        self.touch_path = sb / "dev" / "input" / "event0.fifo"
        # state-fil frisk
        self.state_path.write_text("")
        self.assert_path.write_text("")

    # ------------------------------------------------------------ (b) sim
    def start_sim(self):
        simcfg = self.scen.get("sim", {})
        cmd = [sys.executable, str(HERE / "mcu_sim.py"),
               "--config", str(self.cfg_path),
               "--pty-file", str(self.dir / "pty_path"),
               "--ctrl", str(self.sim_ctrl_path),
               "--assert-log", str(self.assert_path),
               "--profile", simcfg.get("profile", "normal"),
               "--seed", str(simcfg.get("seed", 1)),
               "--range-policy", simcfg.get("range_policy", "log"),
               "--trigger-policy", simcfg.get("trigger_policy", "reject"),
               "--quiet"]
        if simcfg.get("require_wand") is False:
            cmd.append("--no-require-wand")
        rp = self.scen.get("replay")
        if rp:
            cmd += ["--replay", str(ROOT / rp["csv"]),
                    "--replay-mode", rp.get("mode", "serve"),
                    "--speed", str(rp.get("speed", 1.0))]
        pf = self.dir / "pty_path"
        pf.unlink(missing_ok=True)      # gammel fil fra tidligere kørsel i samme mappe
        self.sim_proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                         stderr=subprocess.PIPE, text=True)
        for _ in range(200):
            if pf.exists() and pf.read_text().strip():
                break
            if self.sim_proc.poll() is not None:
                raise RuntimeError("mcu_sim døde ved opstart: "
                                   + (self.sim_proc.stderr.read() or ""))
            time.sleep(0.05)
        self.pty_path = pf.read_text().strip()
        self.sim_ctrl = Ctrl(self.sim_ctrl_path)
        self.sim_ctrl.call({"cmd": "ping"})

    # ------------------------------------------------------------ (c) UI-klient
    def start_ui(self):
        env = dict(os.environ)
        env.update({
            "OURUI_SIM": "1",
            "OURUI_SIM_ROOT": str(self.sandbox),
            "OURUI_ROOT": str(self.sandbox),
            "OURUI_SIM_TTY": self.pty_path,
            "OURUI_SIM_STATE": str(self.state_path),
            "OURUI_SIM_FB": str(self.fb_path),
            "OURUI_SIM_TOUCH": str(self.touch_path),
            "OURUI_SIM_CTRL": str(self.ui_ctrl_path),
            "OURUI_CONFIG": str(self.cfg_path),
            "OURUI_BAKDIR": str(self.sandbox / "customer" / "bak"),
            "OURUI_LANGFILE": str(self.lang_path),
            # v6.3+: UI-sprogfil ("en"/"da") uden for /new_customer-default;
            # manglende fil = fallback EN (uændret adfærd for ældre builds)
            "OURUI_LANGFILE": str(self.sandbox / "new_customer" / "ourui" / "lang"),
            "OURUI_NOGRAB": "1",
            "OURUI_NOBL": "1",
            "OURUI_SWITCH_DRY": "1",
        })
        if self.ui_bin:
            self.client_kind = "sim-build"
            cmd = [str(self.ui_bin)]
        else:
            self.client_kind = "reference"
            cmd = [sys.executable, str(HERE / "ref_client.py")]
        self.ui_proc = subprocess.Popen(cmd, env=env, stdout=subprocess.DEVNULL,
                                        stderr=subprocess.PIPE, text=True)
        self.ui_ctrl = Ctrl(self.ui_ctrl_path)
        # vent på første state-linje (kontrakt: state pr. frame)
        for _ in range(200):
            if self.state_path.exists() and self.state_path.stat().st_size > 0:
                break
            if self.ui_proc.poll() is not None:
                raise RuntimeError("UI-klient døde ved opstart: "
                                   + (self.ui_proc.stderr.read() or ""))
            time.sleep(0.05)
        self.ui_ctrl.call({"cmd": "ping"})

    # ------------------------------------------------------------ (d) events
    def fire_event(self, ev):
        target = ev.get("target", "ui")
        if target == "sim":
            if "event" in ev:
                self.sim_ctrl.call({"cmd": "inject", "event": ev["event"]})
            else:
                self.sim_ctrl.call({k: v for k, v in ev.items()
                                    if k not in ("t", "target")})
        elif target == "ui":
            req = {k: v for k, v in ev.items() if k not in ("t", "target")}
            if req.get("cmd") == "touch" and "layout" in req:
                # v6.5-scenarios: tryk-målet slås op i klientens layout-krog
                # (CONTRACT TILLÆG v6.4 B) i stedet for syntetiske koordinater.
                # SIM-build'et returnerer ourui's rigtige geometri; reference-
                # klienten returnerer sit eget reference-layout.
                name = req.pop("layout")
                resp = self.ui_ctrl.call({"cmd": "layout"})
                tgt = next((t for t in resp.get("targets", [])
                            if t.get("name") == name), None)
                if tgt is None:
                    raise RuntimeError(f"layout-mål '{name}' findes ikke "
                                       f"(page={resp.get('page')})")
                req["x"], req["y"] = int(tgt["x"]), int(tgt["y"])
            self.ui_ctrl.call(req)
        elif target == "runner":
            self.runner_op(ev)

    def runner_op(self, ev):
        op = ev.get("op")
        if op == "snapshot":
            self.take_snapshot(ev.get("label", f"t{ev.get('t', 0):.0f}"), force=True)
        elif op == "config_set":
            cfg = json.loads(self.cfg_path.read_text())
            cfg[ev["field"]] = ev["value"]
            tmp = self.cfg_path.with_suffix(".tmp")
            tmp.write_text(json.dumps(cfg, indent="\t", ensure_ascii=False) + "\n")
            os.replace(str(tmp), str(self.cfg_path))
            if ev.get("reload"):
                self.ui_ctrl.call({"cmd": "config_reload"})
        elif op == "flag":
            if ev.get("value"):
                self.flag_path.touch()
            else:
                self.flag_path.unlink(missing_ok=True)
        elif op == "gesture":
            self.run_gesture(ev)
        elif op == "touch_stream":
            stream = touch_stream(ev.get("presses", 1), ev.get("x", 240),
                                  ev.get("y", 240), ev.get("hold_ms", 80),
                                  ev.get("gap_ms", 300),
                                  t0_ms=int(ev.get("t", 0) * 1000) + 100000)
            with open(self.touch_path, "wb") as f:
                f.write(stream)

    def run_gesture(self, ev):
        """Kør den ÆGTE switcher-daemon (host-build) i fil-mode mod en
        syntetisk evdev-stream (samme format som tinj_hold)."""
        name = ev.get("name", "gesture")
        presses = ev.get("presses", 3)
        stream = touch_stream(presses, ev.get("x", 75), ev.get("y", 75),
                              ev.get("hold_ms", 1600), ev.get("gap_ms", 300))
        gdir = self.dir / "gesture"
        gdir.mkdir(exist_ok=True)
        sf = gdir / f"{name}.evdev"
        lf = gdir / f"{name}.switcher.log"
        kf = gdir / f"{name}.lock"
        sf.write_bytes(stream)
        lf.unlink(missing_ok=True)
        kf.unlink(missing_ok=True)
        result = {"ran": False, "trigger": False, "log": ""}
        if SWITCHER_HOST.exists():
            env = dict(os.environ, SWITCHER_INPUT_FILE=str(sf),
                       SWITCHER_LOG=str(lf), SWITCHER_LOCK=str(kf),
                       SWITCHER_GESTURE=ev.get("gesture", "corner"),
                       SWITCHER_RAW_MAX="4095")
            try:
                r = subprocess.run([str(SWITCHER_HOST)], env=env,
                                   capture_output=True, text=True, timeout=30)
                log = lf.read_text() if lf.exists() else ""
                result = {"ran": True, "rc": r.returncode,
                          "trigger": "GESTUS UDLØST" in log, "log": log}
            except subprocess.TimeoutExpired:
                result = {"ran": True, "trigger": False, "log": "TIMEOUT"}
        self.gesture_results[name] = result
        if ev.get("inject_ui"):
            with open(self.touch_path, "wb") as f:
                f.write(stream)

    # ------------------------------------------------------------ (e) sampling
    def read_states(self):
        try:
            with open(self.state_path) as f:
                for line in f:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        st = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    t = st.get("t_ms", 0) / 1000.0
                    if not self.samples or t > self.samples[-1][0] or \
                            st.get("client") != self.samples[-1][1].get("client"):
                        self.samples.append((t, st))
        except OSError:
            pass

    def take_snapshot(self, label, force=False):
        try:
            raw = self.fb_path.read_bytes()
        except OSError:
            return
        if len(raw) < FB_BYTES:
            return
        idx = len(self.snapshots)
        binp = self.snaps_dir / f"{idx:02d}_{label}.bin"
        binp.write_bytes(raw)
        self.snapshots.append({"t": round(time.monotonic() - self.t0, 2),
                               "label": label, "bin": str(binp), "raw": raw})

    def sample_loop(self):
        duration = float(self.scen.get("duration_s", 20))
        events = sorted(self.scen.get("events", []), key=lambda e: e.get("t", 0))
        ei = 0
        last_cfg = last_snap = -1e9
        self.t0 = time.monotonic()
        while True:
            now = time.monotonic() - self.t0
            if now >= duration:
                break
            if self.ui_proc.poll() is not None:
                break
            while ei < len(events) and events[ei].get("t", 0) <= now:
                try:
                    self.fire_event(events[ei])
                except Exception as e:
                    self.checks.append({"name": f"event@{events[ei].get('t')}",
                                        "ok": False, "detail": str(e)})
                ei += 1
            self.read_states()
            if now - last_cfg >= 0.5:
                last_cfg = now
                try:
                    self.cfg_samples.append((now, json.loads(self.cfg_path.read_text())))
                except (OSError, json.JSONDecodeError):
                    pass
                self.flag_samples.append((now, self.flag_path.exists()))
            if now - last_snap >= 2.5:
                last_snap = now
                self.take_snapshot(f"auto{now:.0f}s")
            time.sleep(0.02)
        # sidste sampling + exit
        self.read_states()
        self.take_snapshot("final", force=True)
        try:
            self.ui_ctrl.call({"cmd": "exit", "code": 0}, timeout=2.0)
        except Exception:
            pass
        try:
            self.ui_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.ui_proc.terminate()
            self.ui_proc.wait(timeout=3)
        self.ui_exit = self.ui_proc.returncode
        # prefetch replay-sandhedsvinduer MENS sim'en stadig kører
        for ex in self.scen.get("expect", []):
            if ex.get("kind") == "replay_truth":
                key = (ex["word"], ex["t0"], ex["t1"], ex.get("signed", False))
                try:
                    self.replay_truth_cache[key] = self.sim_ctrl.call(
                        {"cmd": "replay_window", "word": ex["word"],
                         "t0": ex["t0"], "t1": ex["t1"],
                         "signed": ex.get("signed", False)})
                except Exception as e:
                    self.replay_truth_cache[key] = {"ok": False, "error": str(e)}
        try:
            self.sim_ctrl.call({"cmd": "exit"}, timeout=2.0)
        except Exception:
            pass
        try:
            self.sim_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.sim_proc.terminate()
        self.read_states()
        try:
            self.sim_log = [json.loads(l) for l in
                            self.assert_path.read_text().splitlines() if l.strip()]
        except OSError:
            self.sim_log = []

    # ------------------------------------------------------------ (f) assertions
    def add(self, name, ok, detail=""):
        self.checks.append({"name": name, "ok": bool(ok), "detail": detail})

    def states_in(self, t0, t1):
        return [st for (t, st) in self.samples if t0 <= t <= t1]

    def nearest_state(self, t, max_dist=1.5):
        best, bd = None, 1e9
        for (ts, st) in self.samples:
            d = abs(ts - t)
            if d < bd:
                best, bd = st, d
        return best if bd <= max_dist else None

    def eval_expect(self, ex):
        kind = ex.get("kind")
        win = ex.get("window", 0.75)
        if kind == "phase":
            sts = self.states_in(ex["t"] - win, ex["t"] + win)
            ok = any(s.get("phase") == ex["value"] for s in sts)
            self.add(f"phase={ex['value']}@{ex['t']}", ok,
                     f"set={[s.get('phase') for s in sts][:8]}")
        elif kind == "phase_hold":
            sts = self.states_in(ex["t0"], ex["t1"])
            hits = sum(1 for s in sts if s.get("phase") == ex["value"])
            ratio = hits / len(sts) if sts else 0.0
            ok = bool(sts) and ratio >= ex.get("min_ratio", 0.9)
            self.add(f"phase_hold={ex['value']}[{ex['t0']},{ex['t1']}]", ok,
                     f"ratio={ratio:.2f} n={len(sts)}")
        elif kind == "phase_seq":
            sts = self.states_in(ex.get("t0", 0), ex.get("t1", 1e9))
            seq = []
            for s in sts:
                p = s.get("phase")
                if not seq or seq[-1] != p:
                    seq.append(p)
            want = ex["values"]
            i = 0
            for p in seq:
                if i < len(want) and p == want[i]:
                    i += 1
            self.add(f"phase_seq={want}", i == len(want), f"set={seq}")
        elif kind == "word":
            sts = self.states_in(ex["t"] - win, ex["t"] + win)
            mask = int(ex.get("mask", 0xFFFF), 0) if isinstance(ex.get("mask"), str) \
                else ex.get("mask", 0xFFFF)
            val = int(ex["value"], 0) if isinstance(ex["value"], str) else ex["value"]
            w = str(ex["word"])
            ok = any((int(s.get("regs", {}).get(w, 0)) & mask) == (val & mask)
                     for s in sts)
            self.add(f"word{w}={val:#x}@{ex['t']}", ok,
                     f"set={[s.get('regs', {}).get(w) for s in sts][:8]}")
        elif kind == "word_hold":
            sts = self.states_in(ex["t0"], ex["t1"])
            w = str(ex["word"])
            vals = []
            for s in sts:
                v = s.get("regs", {}).get(w)
                if v is None:
                    continue
                v = int(v)
                vals.append(s16(v) if ex.get("signed") else v)
            ok = bool(vals) and all(ex["min"] <= v <= ex["max"] for v in vals)
            self.add(f"word{w}∈[{ex['min']},{ex['max']}][{ex['t0']},{ex['t1']}]",
                     ok, f"n={len(vals)} min={min(vals) if vals else '-'} "
                     f"max={max(vals) if vals else '-'}")
        elif kind == "word_min_seen":
            sts = self.states_in(ex.get("t0", 0), ex.get("t1", 1e9))
            w = str(ex["word"])
            vals = [s16(int(s["regs"][w])) if ex.get("signed") else int(s["regs"][w])
                    for s in sts if w in s.get("regs", {})]
            ok = bool(vals) and max(vals) >= ex["min"]
            self.add(f"word{w}≥{ex['min']} set i [{ex.get('t0', 0)},{ex.get('t1', 1e9)}]",
                     ok, f"max={max(vals) if vals else '-'}")
        elif kind == "work_sta_seen":
            sts = self.states_in(ex.get("t0", 0), ex.get("t1", 1e9))
            mask = int(ex.get("mask", "0x2C00"), 0)
            val = int(ex["value"], 0)
            ok = any((int(s.get("work_sta", 0)) & ~mask) == val for s in sts)
            self.add(f"work_sta={val:#x} set i [{ex.get('t0', 0)},{ex.get('t1', 1e9)}]",
                     ok, f"set={sorted({hex(int(s.get('work_sta', 0)) & ~mask) for s in sts})}")
        elif kind == "stop_ok":
            # v6.13: state.stop.ok (ourui + reference-klient) — stop-bekræftelse
            # når MCU'en har ryddet aktivitetens bits (ikke blot ACK).
            sts = self.states_in(ex.get("t0", 0), ex.get("t1", 1e9))
            oks = [int((s.get("stop") or {}).get("ok", 0)) for s in sts]
            fails = [int((s.get("stop") or {}).get("fail", 0)) for s in sts]
            ok = bool(oks) and max(oks) >= ex.get("min", 1)
            if "max_fail" in ex:
                ok = ok and max(fails) <= ex["max_fail"]
            self.add(f"stop.ok≥{ex.get('min', 1)} i [{ex.get('t0', 0)},{ex.get('t1', 1e9)}]",
                     ok, f"ok={max(oks) if oks else '-'} fail={max(fails) if fails else '-'}")
        elif kind == "power":
            # v6.14: state.power.state (on|turning_off|off|turning_on)
            sts = self.states_in(ex["t"] - win, ex["t"] + win)
            ok = any((s.get("power") or {}).get("state") == ex["value"] for s in sts)
            self.add(f"power={ex['value']}@{ex['t']}", ok,
                     f"set={[(s.get('power') or {}).get('state') for s in sts][:8]}")
        elif kind == "power_hold":
            sts = self.states_in(ex["t0"], ex["t1"])
            hits = sum(1 for s in sts
                       if (s.get("power") or {}).get("state") == ex["value"])
            ratio = hits / len(sts) if sts else 0.0
            ok = bool(sts) and ratio >= ex.get("min_ratio", 0.9)
            self.add(f"power_hold={ex['value']}[{ex['t0']},{ex['t1']}]", ok,
                     f"ratio={ratio:.2f} n={len(sts)}")
        elif kind == "power_counter":
            # v6.14: state.power-tæller (offs/ons/blocked/fails/ack8/auto_on/…)
            sts = self.states_in(ex.get("t0", 0), ex.get("t1", 1e9))
            vals = [int((s.get("power") or {}).get(ex["field"], 0)) for s in sts]
            ok = bool(vals) and max(vals) >= ex.get("min", 1)
            self.add(f"power.{ex['field']}≥{ex.get('min', 1)}", ok,
                     f"max={max(vals) if vals else '-'}")
        elif kind == "page":
            sts = self.states_in(ex["t"] - win, ex["t"] + win)
            ok = any(s.get("page") == ex["value"] for s in sts)
            self.add(f"page={ex['value']}@{ex['t']}", ok)
        elif kind == "view":
            sts = self.states_in(ex["t"] - win, ex["t"] + win)
            ok = any(s.get("view") == ex["value"] for s in sts)
            self.add(f"view={ex['value']}@{ex['t']}", ok)
        elif kind == "toast":
            sts = self.states_in(ex["t"] - win, ex["t"] + win)
            ok = any(ex["contains"] in (s.get("toast") or {}).get("text", "")
                     for s in sts)
            self.add(f"toast~'{ex['contains']}'@{ex['t']}", ok)
        elif kind == "lang":
            sts = self.states_in(ex["t"] - win, ex["t"] + win)
            ok = any(s.get("lang") == ex["value"] for s in sts)
            self.add(f"lang={ex['value']}@{ex['t']}", ok)
        elif kind == "config":
            # sidste sample PÅ ELLER FØR forventningstidspunktet (undgå at
            # næste saves ACK når at overskrive værdien i en nærmere sample)
            best = None
            for (t, cfg) in self.cfg_samples:
                if t <= ex["t"] + 0.05:
                    best = cfg
            ok = best is not None and best.get(ex["field"]) == ex["value"]
            self.add(f"config.{ex['field']}={ex['value']}@{ex['t']}", ok,
                     f"fundet={best.get(ex['field']) if best else None}")
        elif kind == "flag":
            best = None
            for (t, fl) in self.flag_samples:
                if t <= ex["t"] + 0.05:
                    best = fl
            ok = best is not None and best == ex["exists"]
            self.add(f"boot-flag exists={ex['exists']}@{ex['t']}", ok)
        elif kind == "save":
            sts = self.states_in(ex["t"] - win, ex["t"] + win)
            ok = any((s.get("save") or {}).get("state") == ex["state"] and
                     (ex.get("param") is None or
                      (s.get("save") or {}).get("param") == ex["param"])
                     for s in sts)
            self.add(f"save.{ex['state']}({ex.get('param', '')})@{ex['t']}", ok)
        elif kind == "save_rejected":
            sts = self.states_in(ex["t"] - win, ex["t"] + win)
            ok = any(("AFVIST" in (s.get("toast") or {}).get("text", "") or
                      "LÅST" in (s.get("toast") or {}).get("text", "")) for s in sts)
            self.add(f"save afvist ({ex.get('param', '')})@{ex['t']}", ok)
        elif kind == "exit_code":
            self.add(f"exit_code={ex['value']}", self.ui_exit == ex["value"],
                     f"faktisk={self.ui_exit}")
        elif kind == "crc_bad_min":
            last = self.samples[-1][1] if self.samples else {}
            ok = last.get("crc_bad", 0) >= ex["value"]
            self.add(f"crc_bad≥{ex['value']}", ok, f"faktisk={last.get('crc_bad')}")
        elif kind == "offline_seen":
            sts = self.states_in(ex.get("t0", 0), ex.get("t1", 1e9))
            ok = any(s.get("phase") == "OFFLINE" or not s.get("link_up", True)
                     for s in sts)
            self.add("offline set", ok)
        elif kind == "link_up_at":
            st = self.nearest_state(ex["t"])
            ok = st is not None and st.get("link_up", False)
            self.add(f"link up@{ex['t']}", ok)
        elif kind == "config_oor_min":
            sts = self.states_in(ex.get("t0", 0), ex.get("t1", 1e9))
            ok = any(len(s.get("config_oor", [])) >= ex.get("min", 1) for s in sts)
            self.add(f"config_oor≥{ex.get('min', 1)}", ok)
        elif kind == "cmd6":
            last = self.samples[-1][1] if self.samples else {}
            c6 = last.get("cmd6", {})
            field, op, val = ex["field"], ex.get("op", ">="), ex["value"]
            have = c6.get(field, 0)
            ok = {">=": have >= val, "==": have == val, "<=": have <= val}[op]
            self.add(f"cmd6.{field}{op}{val}", ok, f"faktisk={have}")
        elif kind == "sim_log":
            self.eval_sim_log(ex)
        elif kind == "circle":
            bad = []
            for sn in self.snapshots:
                ok, det = circle_check(sn["raw"])
                if not ok:
                    bad.append({"label": sn["label"], **det})
            self.add(f"circle-tjek ({len(self.snapshots)} snapshots)",
                     not bad and bool(self.snapshots), json.dumps(bad[:3]))
        elif kind == "region":
            sn = self.nearest_snapshot(ex["t"])
            if sn is None:
                self.add(f"region@{ex['t']}", False, "ingen snapshot")
                return
            x, y, rw, rh = ex["rect"]
            raw = sn["raw"]
            rs = gs = bs = n = 0
            for yy in range(y, min(y + rh, H)):
                for xx in range(x, min(x + rw, W)):
                    o = (yy * W + xx) * 4
                    bs += raw[o]; gs += raw[o + 1]; rs += raw[o + 2]; n += 1
            mean = [rs / n, gs / n, bs / n]
            tol = ex.get("tol", 40)
            ok = all(abs(mean[i] - ex["color"][i]) <= tol for i in range(3))
            self.add(f"region{ex['rect']}~{ex['color']}@{ex['t']}", ok,
                     f"mean={[round(v, 1) for v in mean]}")
        elif kind == "point":
            sn = self.nearest_snapshot(ex["t"])
            if sn is None:
                self.add(f"point@{ex['t']}", False, "ingen snapshot")
                return
            x, y = ex["point"]
            o = (y * W + x) * 4
            raw = sn["raw"]
            got = [raw[o + 2], raw[o + 1], raw[o]]
            tol = ex.get("tol", 30)
            ok = all(abs(got[i] - ex["color"][i]) <= tol for i in range(3))
            self.add(f"point({x},{y})~{ex['color']}@{ex['t']}", ok, f"got={got}")
        elif kind == "replay_truth":
            # sandheden i optagelsen (aliasing-sikker ved høj replay-hastighed);
            # prefetchet i sample_loop mens sim'en stadig kører
            key = (ex["word"], ex["t0"], ex["t1"], ex.get("signed", False))
            resp = self.replay_truth_cache.get(key, {"ok": False, "error": "ikke prefetchet"})
            ok = resp.get("ok") and resp.get("n", 0) > 0
            if ok and "peak_min" in ex:
                ok = resp["max"] is not None and resp["max"] >= ex["peak_min"]
            if ok and "val_min" in ex:
                ok = resp["min"] is not None and resp["min"] >= ex["val_min"]
            if ok and "val_max" in ex:
                ok = resp["max"] is not None and resp["max"] <= ex["val_max"]
            self.add(f"replay-truth word{ex['word']}[{ex['t0']},{ex['t1']}]",
                     ok, f"min={resp.get('min')} max={resp.get('max')} n={resp.get('n')}")
        elif kind == "gesture":
            res = self.gesture_results.get(ex["name"], {})
            ok = res.get("ran") and res.get("trigger") == ex["expect_trigger"]
            self.add(f"gesture {ex['name']} trigger={ex['expect_trigger']}", ok,
                     res.get("log", "")[-200:] if not ok else "")
        elif kind == "contract":
            need = {"t_ms", "page", "phase", "work_sta", "regs", "lang",
                    "boot_flag", "crc_ok", "crc_bad", "cmd6", "frame_ms"}
            last = self.samples[-1][1] if self.samples else {}
            missing = need - set(last.keys())
            fb_ok = self.fb_path.exists() and self.fb_path.stat().st_size >= FB_BYTES
            self.add("SIM-kontrakt: state-felter + fb-buffer", not missing and fb_ok,
                     f"mangler={sorted(missing)} fb_ok={fb_ok}")
        else:
            self.add(f"ukendt expect-kind: {kind}", False)
        if ex.get("_ui_profile") and self.checks:
            self.checks[-1]["name"] = "[ui-profil] " + self.checks[-1]["name"]
            self.checks[-1]["ui_profile"] = ex["_ui_profile"]

    def nearest_snapshot(self, t, max_dist=2.0):
        best, bd = None, 1e9
        for sn in self.snapshots:
            d = abs(sn["t"] - t)
            if d < bd:
                best, bd = sn, d
        return best if bd <= max_dist else None

    def eval_sim_log(self, ex):
        a = ex["assert"]
        log = self.sim_log
        live = [e for e in log if e.get("source", "live") == "live"]
        t0, t1 = ex.get("t0", 0), ex.get("t1", 1e9)
        if a == "boot_push":
            bt0 = ex.get("t0", 0)     # v6.14: t0 = kun push efter tidspunkt (re-push)
            blocks = {e.get("block") for e in log
                      if e["kind"] == "boot_push" and e["t"] >= bt0}
            ok = {"sub1_len30", "sub112_len3"} <= blocks
            self.add(f"sim: boot-push (sub1 len30 + sub112 len3)"
                     + (f" efter t={bt0}" if bt0 else ""), ok, str(blocks))
        elif a == "version":
            ok = any(e["kind"] == "version_handshake" for e in log)
            self.add("sim: versions-håndtryk (0x89→cmd9)", ok)
        elif a == "keepalive_ok":
            gaps = [e for e in log if e["kind"] == "keepalive_gap"]
            self.add("sim: keep-awake word1=0 ≤30 s", not gaps, json.dumps(gaps[:3]))
        elif a == "no_reboot":
            bad = [e for e in log if e["kind"] == "reboot_trigger"]
            self.add("sim: ingen ord30=1 (reboot)", not bad, json.dumps(bad[:3]))
        elif a == "no_calib":
            bad = [e for e in log if e["kind"] == "calib_trigger"]
            self.add("sim: ingen ord26/27≠0 (kalibrering)", not bad, json.dumps(bad[:3]))
        elif a == "no_band_rejects":
            bad = [e for e in log if e["kind"] == "write_rejected"
                   and e.get("verdict") == "band"]
            self.add("sim: ingen bånd-overskridelser", not bad, json.dumps(bad[:3]))
        elif a == "range_max":
            bad = [e for e in log if e["kind"] == "range_violation"]
            ok = len(bad) <= ex.get("max", 0)
            self.add(f"sim: range-overtrædelser ≤{ex.get('max', 0)}", ok,
                     f"n={len(bad)} {[e.get('reason') for e in bad[:5]]}")
        elif a == "range_min":
            bad = [e for e in log if e["kind"] == "range_violation"]
            ok = len(bad) >= ex.get("min", 1)
            self.add(f"sim: range-overtrædelser ≥{ex.get('min', 1)} (logget)", ok,
                     f"n={len(bad)}")
        elif a == "write_words":
            words = ex["words"]
            hits = [e for e in live if e["kind"] == "write"
                    and e.get("sub") == ex["sub"] and e.get("words") == words
                    and t0 <= e["t"] <= t1]
            self.add(f"sim: write sub{ex['sub']}={words} i [{t0},{t1}]",
                     bool(hits), f"n={len(hits)}")
        elif a == "ack_words":
            # v6.13: ACK-echo (0x86) for en bestemt write — sub+words er
            # eksakt echo (mcu_sim logger ved planlægning, efter loss-profil).
            words = ex["words"]
            hits = [e for e in log if e["kind"] == "ack"
                    and e.get("sub") == ex["sub"] and e.get("words") == words
                    and t0 <= e["t"] <= t1]
            self.add(f"sim: ACK sub{ex['sub']}={words} i [{t0},{t1}]",
                     bool(hits), f"n={len(hits)}")
        elif a == "no_write_word":
            w = ex["word"]
            bad = []
            for e in live:
                if e["kind"] != "write" or not (t0 <= e["t"] <= t1):
                    continue
                sub, words = e.get("sub", 0), e.get("words", [])
                if sub <= w < sub + len(words) and words[w - sub] != 0:
                    bad.append(e)
            self.add(f"sim: ingen writes til ord{w}≠0 i [{t0},{t1}]", not bad,
                     json.dumps(bad[:2]))
        elif a == "no_writes_between":
            exc = set(ex.get("except_subs", [0, 1, 112]))
            bad = [e for e in live if e["kind"] == "write"
                   and t0 <= e["t"] <= t1 and e.get("sub") not in exc]
            self.add(f"sim: ingen writes i [{t0},{t1}] (undt. {sorted(exc)})",
                     not bad, json.dumps([{k: e.get(k) for k in ('t', 'sub', 'words')}
                                          for e in bad[:3]]))
        elif a == "activity_stop":
            hits = [e for e in log if e["kind"] == "activity_stop"
                    and e.get("activity") == ex["activity"]
                    and t0 <= e["t"] <= t1
                    and ex.get("reason_contains", "") in e.get("reason", "")]
            self.add(f"sim: {ex['activity']} stoppet ({ex.get('reason_contains', '')})",
                     bool(hits), json.dumps(hits[:2]))
        elif a == "activity_start":
            hits = [e for e in log if e["kind"] == "activity_start"
                    and e.get("activity") == ex["activity"] and t0 <= e["t"] <= t1]
            self.add(f"sim: {ex['activity']} startet i [{t0},{t1}]", bool(hits))
        elif a == "sleep_write_seen":
            ok = any(e["kind"] == "sleep_write" for e in log)
            self.add("sim: sleep-write (ord1=1) set", ok)
        elif a == "awake_write_seen":
            ok = any(e["kind"] == "write" and e.get("sub") == 1
                     and e.get("words") == [0] for e in live)
            self.add("sim: awake-write (ord1=0) set", ok)
        elif a == "acks_dropped_min":
            n = sum(1 for e in log if e["kind"] == "ack_dropped")
            self.add(f"sim: ACK-tab ≥{ex['value']}", n >= ex["value"], f"n={n}")
        elif a == "writes_min":
            n = sum(1 for e in live if e["kind"] == "write"
                    and e.get("sub") == ex["sub"] and t0 <= e["t"] <= t1)
            self.add(f"sim: writes sub{ex['sub']} ≥{ex['value']} i [{t0},{t1}]",
                     n >= ex["value"], f"n={n}")
        # ---- v6.14: dvale/wake (SIM_HANDOFF, UI_v6 §19) ----
        elif a == "dvale_seen":
            hits = [e for e in log if e["kind"] == "dvale_enter"
                    and t0 <= e["t"] <= t1]
            self.add(f"sim: dvale (ord0=0 → tavs) i [{t0},{t1}]", bool(hits),
                     json.dumps(hits[:2]))
        elif a == "no_acks_between":
            bad = [e for e in log if e["kind"] == "ack" and t0 <= e["t"] <= t1]
            self.add(f"sim: ingen ACK i [{t0},{t1}] (tavs UART)", not bad,
                     json.dumps(bad[:3]))
        elif a == "time_sync_seen":
            hits = [e for e in log if e["kind"] == "time_sync"
                    and t0 <= e["t"] <= t1
                    and (not ex.get("alarm0") or e.get("alarm") == [0, 0, 0])]
            self.add(f"sim: cmd8 tid/alarm" + (" (0/0/0)" if ex.get("alarm0") else "")
                     + f" i [{t0},{t1}]", bool(hits), f"n={len(hits)}")
        elif a == "power_wake_seen":
            hits = [e for e in log if e["kind"] == "power_wake"
                    and t0 <= e["t"] <= t1
                    and ex.get("cause_contains", "") in e.get("cause", "")]
            self.add(f"sim: power-wake ({ex.get('cause_contains', '')}) i [{t0},{t1}]",
                     bool(hits), json.dumps(hits[:2]))
        elif a == "reboot_seen":
            hits = [e for e in log if e["kind"] == "reboot"
                    and t0 <= e["t"] <= t1
                    and (not ex.get("factory") or e.get("factory"))]
            self.add("sim: reboot" + (" (fabriks-startværdier)" if ex.get("factory") else "")
                     + f" i [{t0},{t1}]", bool(hits), json.dumps(hits[:2]))
        elif a == "factory_defaults":
            hits = [e for e in log if e["kind"] == "reboot" and e.get("factory")
                    and t0 <= e["t"] <= t1]
            want = {"0": 1, "11": 920, "114": 50}   # REGISTERMAP §2 (init 0x9966)
            ok = any(all((e.get("defaults") or {}).get(k) == v
                         for k, v in want.items()) for e in hits)
            self.add(f"sim: fabriks-defaults (ord0=1, ord11=920, ord114=50) i [{t0},{t1}]",
                     ok, json.dumps([e.get("defaults") for e in hits][:2]))
        else:
            self.add(f"ukendt sim_log-assert: {a}", False)

    def eval_universal(self):
        uni = self.scen.get("universal", {})
        if uni.get("circle", True):
            self.eval_expect({"kind": "circle"})
        if uni.get("contract", True):
            self.eval_expect({"kind": "contract"})
        if uni.get("no_reboot", True):
            self.eval_sim_log({"assert": "no_reboot"})
        if uni.get("no_calib", True):
            self.eval_sim_log({"assert": "no_calib"})
        if uni.get("no_band_rejects", True):
            self.eval_sim_log({"assert": "no_band_rejects"})
        if uni.get("ranges", True):
            self.eval_sim_log({"assert": "range_max", "max": 0})
        if uni.get("keepawake", True):
            self.eval_sim_log({"assert": "keepalive_ok"})
        if uni.get("boot_push", True):
            self.eval_sim_log({"assert": "boot_push"})
            self.eval_sim_log({"assert": "version"})
        if uni.get("exit0", True):
            self.eval_expect({"kind": "exit_code", "value": 0})
        if uni.get("no_cmd6_fail", True):
            self.eval_expect({"kind": "cmd6", "field": "fail", "op": "==", "value": 0})

    # ------------------------------------------------------------ (g) rapport
    def finish(self):
        for ex in self.scen.get("expect", []):
            try:
                self.eval_expect(ex)
            except Exception as e:
                self.add(f"expect-fejl {ex.get('kind')}", False, str(e))
        self.eval_universal()
        for err in self.profile_errors:
            self.add(f"ui-profil: {err}", False, "override matcher ikke scenariet")
        passed = all(c["ok"] for c in self.checks)
        # PNG + contact-sheet (auto-snapshots beholder kun PNG — .bin er 921 KB
        # og slettes efter konvertering; eksplicitte/final-snapshots beholdes)
        sheet_items = []
        for sn in self.snapshots:
            pngp = Path(sn["bin"]).with_suffix(".png")
            try:
                png.fb2png(sn["raw"], pngp)
                sn["png"] = str(pngp)
                if sn["label"].startswith("auto"):
                    Path(sn["bin"]).unlink(missing_ok=True)
                    sn["bin"] = None
            except Exception as e:
                sn["png_error"] = str(e)
        steps = max(1, len(self.snapshots) // 16)
        for sn in self.snapshots[::steps][:16]:
            ok, _ = circle_check(sn["raw"])
            sheet_items.append((sn["label"], sn["raw"], ok and passed))
        sheet = self.dir / "contact_sheet.png"
        if sheet_items:
            png.contact_sheet(sheet_items, sheet,
                              title=f"{self.name} {'PASS' if passed else 'FAIL'}")
        # final state-opsummering
        last = self.samples[-1][1] if self.samples else {}
        report = {
            "name": self.name,
            "description": self.scen.get("description", ""),
            "pass": passed,
            "client": self.client_kind,
            "ui_bin": str(self.ui_bin) if self.ui_bin else None,
            "ui_lang": (last.get("ui_lang") or self.ui_lang),
            "ui_profile": self.profile_applied,
            "frame_ms": {k: last.get(k) for k in ("frame_ms_avg", "frame_ms_peak", "frames")},
            "duration_s": self.scen.get("duration_s"),
            "samples": len(self.samples),
            "snapshots": [{k: v for k, v in sn.items() if k != "raw"}
                          for sn in self.snapshots],
            "contact_sheet": str(sheet) if sheet_items else None,
            "checks": self.checks,
            "final_state": {k: last.get(k) for k in
                            ("phase", "work_sta", "page", "cmd6", "crc_ok",
                             "crc_bad", "config_oor", "boot_flag", "lang")},
            "sim_stats": next((e.get("stats") for e in reversed(self.sim_log)
                               if e["kind"] == "sim_exit"), None),
            "gesture_results": self.gesture_results,
            "sandbox": str(self.sandbox),
            "sim_assertions_log": str(self.assert_path),
            "state_jsonl": str(self.state_path),
        }
        (self.dir / "report.json").write_text(json.dumps(report, indent=2,
                                                        ensure_ascii=False))
        shutil.rmtree(self.sock_dir, ignore_errors=True)
        return report


def load_scenarios(scen_dir=None):
    out = {}
    for p in sorted(Path(scen_dir or SCEN_DIR).glob("*.json")):
        try:
            s = json.loads(p.read_text())
            s.setdefault("name", p.stem)
            out[s["name"]] = s
        except json.JSONDecodeError as e:
            print(f"ADVARSEL: {p} kan ikke parses: {e}", file=sys.stderr)
    return out


def write_summary(reports, run_dir):
    lines = ["# E2E-rapport — ourui SIM-miljø", ""]
    lines.append(f"**Kørsel:** {run_dir.name} · "
                 f"{time.strftime('%Y-%m-%d %H:%M:%S')} · "
                 f"klient: {reports[0]['client'] if reports else '-'}"
                 + (f" · UI-sprog: {reports[0].get('ui_lang')}"
                    if reports and reports[0].get("ui_lang") else ""))
    nov = sum(len(r.get("ui_profile") or []) for r in reports)
    if nov:
        lines.append("")
        lines.append(f"**UI-profil:** {nov} dokumenterede overrides anvendt "
                     "(reference-layout/-adfærd → rigtigt ourui; se nederst og "
                     "CONTRACT.md TILLÆG v6.4). Checks mærket `[ui-profil]`.")
    lines.append("")
    npass = sum(1 for r in reports if r["pass"])
    ntot = sum(len(r["checks"]) for r in reports)
    nokt = sum(1 for r in reports for c in r["checks"] if c["ok"])
    lines.append(f"**Resultat: {npass}/{len(reports)} scenarier PASS** · "
                 f"checks: {nokt}/{ntot}")
    lines.append("")
    lines.append("| Scenarie | Resultat | Checks | Klient | Beskrivelse |")
    lines.append("|---|---|---|---|---|")
    for r in reports:
        nok = sum(1 for c in r["checks"] if c["ok"])
        lines.append(f"| {r['name']} | {'✅ PASS' if r['pass'] else '❌ FAIL'} | "
                     f"{nok}/{len(r['checks'])} | {r['client']} | "
                     f"{r['description'][:70]} |")
    lines.append("")
    for r in reports:
        lines.append(f"## {r['name']} — {'PASS' if r['pass'] else 'FAIL'}")
        lines.append("")
        if r.get("contact_sheet"):
            lines.append(f"![contact sheet]({r['contact_sheet']})")
            lines.append("")
        fails = [c for c in r["checks"] if not c["ok"]]
        if fails:
            lines.append("**Fejlede checks:**")
            for c in fails:
                lines.append(f"- `{c['name']}`: {c['detail'][:300]}")
            lines.append("")
        lines.append(f"Slut-tilstand: `{json.dumps(r['final_state'], ensure_ascii=False)}`")
        lines.append("")
        lines.append(f"Detaljer: [report.json]({r['name']}/report.json) · "
                     f"[sim-assertions]({r['name']}/sim_assertions.jsonl) · "
                     f"[state]({r['name']}/state.jsonl)")
        lines.append("")
    if nov:
        lines.append("## UI-profil: anvendte overrides")
        lines.append("")
        lines.append("| Scenarie | Type | Original | Erstattet med | Begrundelse |")
        lines.append("|---|---|---|---|---|")
        for r in reports:
            for ov in r.get("ui_profile") or []:
                new = ov.get("replace") if ov["type"] == "expect" else ov.get("set")
                lines.append(f"| {r['name']} | {ov['type']} | "
                             f"`{json.dumps(ov['match'], ensure_ascii=False)}` | "
                             f"`{json.dumps(new, ensure_ascii=False)}` | {ov.get('why', '')} |")
        lines.append("")
    fts = [r["frame_ms"] for r in reports if (r.get("frame_ms") or {}).get("frames")]
    if fts:
        lines.append("## Frame-tid (SIM-build på VÆRTEN — ikke enheden)")
        lines.append("")
        lines.append("| Scenarie | Frames | Snit ms | Max ms |")
        lines.append("|---|---|---|---|")
        for r in reports:
            ft = r.get("frame_ms") or {}
            if ft.get("frames"):
                lines.append(f"| {r['name']} | {ft['frames']} | "
                             f"{ft['frame_ms_avg']:.3f} | {ft['frame_ms_peak']:.3f} |")
        lines.append("")
    (run_dir / "report.md").write_text("\n".join(lines))
    (run_dir / "report.json").write_text(json.dumps(
        {"run": run_dir.name, "pass": npass, "total": len(reports),
         "scenarios": reports}, indent=2, ensure_ascii=False))


def main():
    ap = argparse.ArgumentParser(description="E2E-runner for ourui SIM-miljø")
    ap.add_argument("--scenario", help="kør ét scenarie (navn)")
    ap.add_argument("--all", action="store_true", help="kør alle scenarier")
    ap.add_argument("--list", action="store_true", help="list scenarier")
    ap.add_argument("--ui-bin", help="ourui SIM-build (ellers reference-klient)")
    ap.add_argument("--ref-client", action="store_true",
                    help="brug reference-klienten (sim/ref_client.py) selv hvis et SIM-build "
                         "findes — oracle-kørsel med REF-profilen")
    ap.add_argument("--reports-dir", default=str(REPORTS))
    ap.add_argument("--keep-sandbox", action="store_true")
    ap.add_argument("--ui-lang", choices=["en", "da"],
                    help="ourui's UI-sprog (skrives i sandkassens sprog-fil; default: ingen fil = engelsk)")
    ap.add_argument("--ui-profile", help="profil m. overrides (default: sim/sim_ui_profile-ui.json "
                                         "[tom] for SIM-build, sim/sim_ui_profile-ref.json for reference-klienten)")
    ap.add_argument("--no-ui-profile", action="store_true",
                    help="kør scenarierne RÅ, uden nogen profil-overrides")
    ap.add_argument("--run-name", help="navn på kørsels-mappen (default: run-<tidsstempel>)")
    ap.add_argument("--scen-dir", help="alternativ scenarie-mappe (default: sim/scenarios)")
    ap.add_argument("--quick", action="store_true",
                    help="HURTIG dev-iteration: lille kerne-subset, ét sprog (en), ingen REF/steam30/replay. "
                         "Fuld suite køres kun ved CI/CD eller endeligt milestone.")
    ap.add_argument("--parallel", type=int, default=1, metavar="N",
                    help="kør op til N scenarier samtidigt (uafhængige sandkasser/pty'er)")
    args = ap.parse_args()

    scens = load_scenarios(args.scen_dir)
    if args.list:
        for name, s in scens.items():
            print(f"{name:24s} {s.get('duration_s', '?'):>5}s  "
                  f"{s.get('description', '')[:70]}")
        return 0
    if not args.scenario and not args.all:
        ap.print_help()
        return 2
    names = list(scens) if args.all else [args.scenario]
    if args.quick:
        quick = ["idle", "brew", "steam_auto", "no_ack", "out_of_spec"]
        names = [n for n in quick if n in scens] if args.all or not args.scenario else names
        if not args.ui_lang:
            args.ui_lang = "en"
        print(f"[e2e] QUICK dev-mode: {len(names)} kerne-scenarier, ét sprog (en). "
              f"Fuld suite = CI/CD / endeligt milestone.")
    if args.scenario and args.scenario not in scens:
        print(f"ukendt scenarie: {args.scenario} (brug --list)", file=sys.stderr)
        return 2
    ui_bin = find_ui_bin(args.ui_bin)
    if ui_bin and ui_bin.resolve() == (HERE / "ref_client.py").resolve():
        ui_bin = None                     # --ui-bin sim/ref_client.py = reference-klienten
    if args.ref_client:
        ui_bin = None                     # --ref-client: tving oracle-kørsel
    if ui_bin:
        print(f"[e2e] ourui SIM-build: {ui_bin}")
    else:
        print("[e2e] intet ourui SIM-build fundet — kører REFERENCE-klient "
              "(sim/ref_client.py). Se CONTRACT.md for SIM-build-krav.")
    # v6.5-scenarios: scenarierne asserter ourui's vogtede adfærd. SIM-build'et
    # køres med den TOMME UI-profil; reference-klienten med REF-profilen (de
    # omvendte overrides, så oracle-kørsler stadig er grønne). --no-ui-profile
    # kører helt råt; --ui-profile <fil> vælger eksplicit (bagudkompatibelt).
    profile = None
    if not args.no_ui_profile:
        default_pp = UI_PROFILE_DEFAULT if ui_bin else REF_PROFILE_DEFAULT
        pp = Path(args.ui_profile) if args.ui_profile else default_pp
        if pp.exists():
            profile = json.loads(pp.read_text())
            nov = sum(len(v) for v in (profile.get("scenarios") or {}).values())
            print(f"[e2e] {'UI' if ui_bin else 'REF'}-profil: {pp} ({nov} overrides)")
        elif args.ui_profile:
            print(f"UI-profil findes ikke: {pp}", file=sys.stderr)
            return 2
    run_dir = Path(args.reports_dir) / (args.run_name or
                                        "run-" + time.strftime("%Y%m%d-%H%M%S"))
    run_dir.mkdir(parents=True, exist_ok=True)
    reports = []

    def run_one(name):
        scen = scens[name]
        print(f"\n[e2e] === {name} ({scen.get('duration_s', '?')}s) ===", flush=True)
        sr = ScenarioRun(scen, run_dir, ui_bin=ui_bin,
                         keep_sandbox=args.keep_sandbox,
                         ui_lang=args.ui_lang, ui_profile=profile)
        try:
            sr.make_sandbox()
            sr.start_sim()
            sr.start_ui()
            sr.sample_loop()
            rep = sr.finish()
        except Exception as e:
            import traceback
            traceback.print_exc()
            rep = {"name": name, "pass": False, "client": sr.client_kind,
                   "description": scen.get("description", ""),
                   "checks": [{"name": "runner-fejl", "ok": False, "detail": str(e)}],
                   "final_state": {}, "snapshots": [], "samples": 0,
                   "contact_sheet": None, "sim_stats": None, "gesture_results": {},
                   "duration_s": scen.get("duration_s"), "ui_bin": str(ui_bin) if ui_bin else None,
                   "sandbox": str(sr.sandbox), "sim_assertions_log": str(sr.assert_path),
                   "state_jsonl": str(sr.state_path)}
            for proc in (sr.ui_proc, sr.sim_proc):
                if proc and proc.poll() is None:
                    proc.terminate()
            shutil.rmtree(sr.sock_dir, ignore_errors=True)
        nok = sum(1 for c in rep["checks"] if c["ok"])
        print(f"[e2e] {name}: {'PASS' if rep['pass'] else 'FAIL'} "
              f"({nok}/{len(rep['checks'])} checks)", flush=True)
        for c in rep["checks"]:
            if not c["ok"]:
                print(f"      ✗ {c['name']}: {c['detail'][:160]}")
        return rep

    if args.parallel > 1:
        from concurrent.futures import ThreadPoolExecutor
        with ThreadPoolExecutor(max_workers=args.parallel) as ex:
            reports = list(ex.map(run_one, names))
    else:
        reports = [run_one(n) for n in names]
    write_summary(reports, run_dir)
    npass = sum(1 for r in reports if r["pass"])
    print(f"\n[e2e] SAMLET: {npass}/{len(reports)} PASS · rapport: {run_dir}/report.md")
    return 0 if npass == len(reports) else 1


if __name__ == "__main__":
    sys.exit(main())